// Win-FG frame generation for TOTKDroid: the compute chain of The412Banner/win-fg (MIT, LICENSE.md here; "win-fg —
// compute frame-generation engine"), ported to the NVN layer's Vulkan present. The shaders are win-fg's (embedded_shaders.hpp,
// from shaders/*.comp: our own optical flow after AMD's FidelityFX FSR3 optical flow, MIT, NOTICE_FIDELITYFX_OPTICALFLOW.md),
// and so is the chain (framegen.cpp, record_impl.inc): luma pyramids, C1 global-motion pre-warp (inverse-compositional
// Lucas-Kanade, the affine taken back out after the search), dense block-search flow over the pyramid, C2 TV-L1 flow
// regularization, expansion to full resolution, and the synthesis of the in-between frame.
//
// What TOTKDroid changes: the Vulkan calls go straight to volk's functions (no dispatch tables); the game's frame is
// copied into one of kWinfgRing input images (the previous game frame is the one before it); the descriptor sets are
// written once per size, not per frame, so a frame's commands never see another frame's bindings; the shared passes
// run once per game frame and each generated frame only runs the synthesis (win-fg recomputed the whole chain per
// generated frame); the per-frame buffers (UBOs, C1's reduce partials) have a slot each of the ring, and a slot is
// waited for (winfg_begin) before it is written again. Win-FG is built for every APK (not the GPL LSFG folder).
#ifdef __ANDROID__
#include "winfg.h"
#include "embedded_shaders.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include <cstdio>
#ifndef LOG
#define LOG(...) fprintf(stderr, __VA_ARGS__)
#endif

namespace winfg {
namespace {

constexpr int kLevels = 7;          // pyramid levels (the coarsest sets the reach of the search)
constexpr int kGmLevel = 3;         // C1: the 1/8-res luma level the LK estimate reads
constexpr int kGmThreads = 512;     // C1: reduce threads (8 groups of 64)
constexpr int kGmStride = 32;       // C1: floats per thread: 21 H + 6 b + coverage + residual + pad
constexpr float kFrDt = 0.25f, kFrLambda = 2.0f, kFrEdge = 8.0f, kFrEps = 0.05f;  // C2 defaults (win-fg's)
constexpr float kBeta = 8.0f, kLambda = 0.6f, kEpsilon = 0.07f, kPhotoScale = 7.5f;  // synthesis defaults (win-fg's)

// UBO layouts: they match the shaders' declarations (std140)
struct SynthUBO { float flowScale, alpha, beta, lambda, epsilon, photoScale, pad0, pad1; float hudX0, hudY0, hudX1, hudY1; };
struct FlowUBO { float flowScale; uint32_t level; float occlLo; float occlHi; };
struct GMUBO { float lin[4]; float linT[4]; float app[4]; float appT[4]; };
struct FlowRegUBO { float dt, lambda, edgeAlpha, epsTV; };

struct Img { VkImage image = VK_NULL_HANDLE; VkDeviceMemory mem = VK_NULL_HANDLE; VkImageView view = VK_NULL_HANDLE; VkExtent2D ext{}; };
struct Buf { VkBuffer buf = VK_NULL_HANDLE; VkDeviceMemory mem = VK_NULL_HANDLE; void *ptr = nullptr; };
struct Pipe { VkPipeline pipeline = VK_NULL_HANDLE; VkPipelineLayout layout = VK_NULL_HANDLE;
              VkDescriptorSetLayout setLayout = VK_NULL_HANDLE; VkShaderModule module = VK_NULL_HANDLE; };

// Device-wide: the pipelines and the sampler. Made once.
struct Shared {
    bool inited = false;
    VkDevice dev = VK_NULL_HANDLE;
    VkPhysicalDevice phys = VK_NULL_HANDLE;
    WinfgSync sync{nullptr, nullptr, nullptr};
    VkPhysicalDeviceMemoryProperties mp{};
    VkSampler linear = VK_NULL_HANDLE;
    Pipe pLuma, pDown, pFlow, pFlowM4, pExpand, pExpandM4, pSynth, pGmReduce, pGmPrewarp, pFlowReg;
};
Shared P;   // (dev, phys, sync: set by winfg_init)

// Per size (and options): everything the frames use. Made by winfg_build, dropped by winfg_free.
struct Size {
    bool ready = false;
    VkExtent2D ext{};
    WinfgOpts opts{};
    int F = 2;                       // the finest solved flow level (perf)
    VkDescriptorPool pool = VK_NULL_HANDLE;

    Img in[WINFG_RING];              // the game's frames, as copied in (slot = count % ring)
    Img pyrA[kLevels], pyrB[kLevels], pyrAs[kLevels], flowLvl[kLevels];
    Img expA, expB, flowRegA, flowRegB;

    Buf synthU[WINFG_RING][WINFG_MAX_GEN], expandU, flowU[kLevels], flowRegU, gmU[WINFG_RING], ssbo[WINFG_RING];

    VkDescriptorSet sLumaCurr[WINFG_RING], sLumaPrev[WINFG_RING];
    VkDescriptorSet sDownA[kLevels - 1], sDownB[kLevels - 1];
    VkDescriptorSet sFlow[kLevels];
    VkDescriptorSet sGmReduce[WINFG_RING], sGmPrewarp[WINFG_RING][kLevels];
    VkDescriptorSet sExpand[WINFG_RING];
    VkDescriptorSet sFlowReg0, sFlowRegAB, sFlowRegBA;
    VkDescriptorSet sSynth[WINFG_RING][WINFG_MAX_GEN];

    uint64_t slotSeq[WINFG_RING] = {};  // the submission that last used the slot (0: none)

    // C1 state (the running global affine; see updateGlobalMotion)
    double gmRun[6] = {};
    double gmLinAt[WINFG_RING][6] = {};
    bool gmSlotValid[WINFG_RING] = {};
    bool gmHaveEst = false;
    int gmStable = 0;
    bool gmEngaged = false;
    float gmTransPx = 0;
    unsigned long long gmFrames = 0;
};
Size S;

uint32_t memType(uint32_t bits, VkMemoryPropertyFlags props) {
    for (uint32_t i = 0; i < P.mp.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (P.mp.memoryTypes[i].propertyFlags & props) == props) return i;
    return 0;
}

bool makeBuf(VkDeviceSize size, VkBufferUsageFlags usage, bool host, Buf &b) {
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = size; bi.usage = usage; bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(P.dev, &bi, nullptr, &b.buf) != VK_SUCCESS) return false;
    VkMemoryRequirements mr; vkGetBufferMemoryRequirements(P.dev, b.buf, &mr);
    VkMemoryPropertyFlags want = host ? (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
                                      : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = mr.size; ai.memoryTypeIndex = memType(mr.memoryTypeBits, want);
    if (vkAllocateMemory(P.dev, &ai, nullptr, &b.mem) != VK_SUCCESS) return false;
    if (vkBindBufferMemory(P.dev, b.buf, b.mem, 0) != VK_SUCCESS) return false;
    if (host && vkMapMemory(P.dev, b.mem, 0, size, 0, &b.ptr) != VK_SUCCESS) return false;
    return true;
}
void freeBuf(Buf &b) {
    if (b.mem && b.ptr) vkUnmapMemory(P.dev, b.mem);
    if (b.buf) vkDestroyBuffer(P.dev, b.buf, nullptr);
    if (b.mem) vkFreeMemory(P.dev, b.mem, nullptr);
    b = Buf{};
}

bool makeImg(Img &out, VkExtent2D ext, VkFormat fmt, VkImageUsageFlags usage) {
    out = Img{}; out.ext = ext;
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType = VK_IMAGE_TYPE_2D; ci.format = fmt; ci.extent = {ext.width, ext.height, 1};
    ci.mipLevels = 1; ci.arrayLayers = 1; ci.samples = VK_SAMPLE_COUNT_1_BIT; ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = usage; ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE; ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(P.dev, &ci, nullptr, &out.image) != VK_SUCCESS) return false;
    VkMemoryRequirements mr; vkGetImageMemoryRequirements(P.dev, out.image, &mr);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = mr.size; ai.memoryTypeIndex = memType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(P.dev, &ai, nullptr, &out.mem) != VK_SUCCESS) return false;
    if (vkBindImageMemory(P.dev, out.image, out.mem, 0) != VK_SUCCESS) return false;
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = out.image; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = fmt;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    return vkCreateImageView(P.dev, &vi, nullptr, &out.view) == VK_SUCCESS;
}
void freeImg(Img &i) {
    if (i.view) vkDestroyImageView(P.dev, i.view, nullptr);
    if (i.image) vkDestroyImage(P.dev, i.image, nullptr);
    if (i.mem) vkFreeMemory(P.dev, i.mem, nullptr);
    i = Img{};
}

VkExtent2D levelExt(VkExtent2D e, int l) {
    return {e.width >> l ? e.width >> l : 1u, e.height >> l ? e.height >> l : 1u};
}

// Descriptor sets: allocated from the size's pool, written once (below)
VkDescriptorSet allocSet(VkDescriptorSetLayout layout) {
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = S.pool; ai.descriptorSetCount = 1; ai.pSetLayouts = &layout;
    VkDescriptorSet s = VK_NULL_HANDLE;
    vkAllocateDescriptorSets(P.dev, &ai, &s);
    return s;
}
void wImg(VkDescriptorSet set, uint32_t binding, VkDescriptorType type, VkImageView view, VkSampler samp, VkImageLayout layout) {
    VkDescriptorImageInfo ii{}; ii.imageView = view; ii.sampler = samp; ii.imageLayout = layout;
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = set; w.dstBinding = binding; w.descriptorCount = 1; w.descriptorType = type; w.pImageInfo = &ii;
    vkUpdateDescriptorSets(P.dev, 1, &w, 0, nullptr);
}
void wBuf(VkDescriptorSet set, uint32_t binding, VkDescriptorType type, VkBuffer buf, VkDeviceSize range) {
    VkDescriptorBufferInfo bi{}; bi.buffer = buf; bi.range = range;
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = set; w.dstBinding = binding; w.descriptorCount = 1; w.descriptorType = type; w.pBufferInfo = &bi;
    vkUpdateDescriptorSets(P.dev, 1, &w, 0, nullptr);
}

bool makePipe(Pipe &p, embedded::Shader id, std::vector<std::pair<uint32_t, VkDescriptorType>> binds) {
    std::vector<VkDescriptorSetLayoutBinding> b;
    for (auto &kv : binds) b.push_back({kv.first, kv.second, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr});
    VkDescriptorSetLayoutCreateInfo sl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    sl.bindingCount = (uint32_t)b.size(); sl.pBindings = b.data();
    if (vkCreateDescriptorSetLayout(P.dev, &sl, nullptr, &p.setLayout) != VK_SUCCESS) return false;
    embedded::Blob bl = embedded::blob(id);
    VkShaderModuleCreateInfo mi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    mi.codeSize = bl.words * 4; mi.pCode = bl.code;
    if (vkCreateShaderModule(P.dev, &mi, nullptr, &p.module) != VK_SUCCESS) return false;
    VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    li.setLayoutCount = 1; li.pSetLayouts = &p.setLayout;
    if (vkCreatePipelineLayout(P.dev, &li, nullptr, &p.layout) != VK_SUCCESS) return false;
    VkComputePipelineCreateInfo pi{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pi.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pi.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT; pi.stage.module = p.module; pi.stage.pName = "main";
    pi.layout = p.layout;
    return vkCreateComputePipelines(P.dev, VK_NULL_HANDLE, 1, &pi, nullptr, &p.pipeline) == VK_SUCCESS;
}
void freePipe(Pipe &p) {
    if (p.pipeline) vkDestroyPipeline(P.dev, p.pipeline, nullptr);
    if (p.layout) vkDestroyPipelineLayout(P.dev, p.layout, nullptr);
    if (p.setLayout) vkDestroyDescriptorSetLayout(P.dev, p.setLayout, nullptr);
    if (p.module) vkDestroyShaderModule(P.dev, p.module, nullptr);
    p = Pipe{};
}

// Barriers: image (layout + compute-to-compute hazards), and a general one between stages
void imgBarrier(VkCommandBuffer cb, VkImage img, VkImageLayout from, VkImageLayout to, VkPipelineStageFlags src,
                VkAccessFlags sa, VkPipelineStageFlags dst, VkAccessFlags da) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.oldLayout = from; b.newLayout = to; b.srcAccessMask = sa; b.dstAccessMask = da; b.image = img;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    vkCmdPipelineBarrier(cb, src, dst, 0, 0, nullptr, 0, nullptr, 1, &b);
}
// after a compute pass that wrote img (GENERAL throughout): the next pass may read and write it
void bar(VkCommandBuffer cb, VkImage img) {
    imgBarrier(cb, img, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
               VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
}
// an image every frame fully rewrites: its contents are discarded (UNDEFINED -> GENERAL)
void discard(VkCommandBuffer cb, VkImage img) {
    imgBarrier(cb, img, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
               VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
               VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);
}
void disp(VkCommandBuffer cb, const Pipe &p, VkDescriptorSet set, VkExtent2D e) {
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, p.pipeline);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, p.layout, 0, 1, &set, 0, nullptr);
    vkCmdDispatch(cb, (e.width + 15) / 16, (e.height + 15) / 16, 1);
}

// ── C1: CPU-side linear algebra (textbook; win-fg's) ─────────────────────────────────────────────
// 6x6 symmetric positive-definite solve via Cholesky (false: not positive-definite; the caller keeps the estimate)
bool cholSolve6(double A[6][6], const double b[6], double x[6]) {
    double L[6][6] = {{0}};
    for (int i = 0; i < 6; ++i) {
        for (int j = 0; j <= i; ++j) {
            double s = A[i][j];
            for (int k = 0; k < j; ++k) s -= L[i][k] * L[j][k];
            if (i == j) {
                if (s <= 1e-20) return false;
                L[i][j] = std::sqrt(s);
            } else {
                L[i][j] = s / L[j][j];
            }
        }
    }
    double y[6];
    for (int i = 0; i < 6; ++i) { double s = b[i]; for (int k = 0; k < i; ++k) s -= L[i][k] * y[k]; y[i] = s / L[i][i]; }
    for (int i = 5; i >= 0; --i) { double s = y[i]; for (int k = i + 1; k < 6; ++k) s -= L[k][i] * x[k]; x[i] = s / L[i][i]; }
    return true;
}
// params p=(p1..p6): M = I + [[p1,p3,p5],[p2,p4,p6]]
void affFromParams(const double p[6], double M[3][3]) {
    M[0][0] = 1.0 + p[0]; M[0][1] = p[2];       M[0][2] = p[4];
    M[1][0] = p[1];       M[1][1] = 1.0 + p[3]; M[1][2] = p[5];
    M[2][0] = 0.0; M[2][1] = 0.0; M[2][2] = 1.0;
}
void paramsFromAff(const double M[3][3], double p[6]) {
    p[0] = M[0][0] - 1.0; p[2] = M[0][1]; p[4] = M[0][2];
    p[1] = M[1][0];       p[3] = M[1][1] - 1.0; p[5] = M[1][2];
}
bool invAff(const double M[3][3], double out[3][3]) {
    double a = M[0][0], b = M[0][1], tx = M[0][2], c = M[1][0], d = M[1][1], ty = M[1][2];
    double det = a * d - b * c;
    if (std::fabs(det) < 1e-12) return false;
    double id = 1.0 / det;
    out[0][0] = d * id;  out[0][1] = -b * id; out[0][2] = (b * ty - d * tx) * id;
    out[1][0] = -c * id; out[1][1] = a * id;  out[1][2] = (c * tx - a * ty) * id;
    out[2][0] = 0.0; out[2][1] = 0.0; out[2][2] = 1.0;
    return true;
}
void mulAff(const double A[3][3], const double B[3][3], double out[3][3]) {
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 3; ++j)
            out[i][j] = A[i][0] * B[0][j] + A[i][1] * B[1][j] + A[i][2] * B[2][j];
    out[2][0] = 0.0; out[2][1] = 0.0; out[2][2] = 1.0;
}

// C1: reads the reduce partials of this ring slot (its submission was waited for: winfg_begin), runs one
// inverse-compositional Gauss-Newton step from the running estimate, and says whether the affine is applied this frame.
bool updateGlobalMotion(int k) {
    ++S.gmFrames;
    if (S.opts.gm == 2) {  // off: identity, and the estimate restarts when it is switched back on
        S.gmEngaged = false; S.gmStable = 0;
        for (int i = 0; i < 6; ++i) { S.gmRun[i] = 0.0; S.gmLinAt[k][i] = 0.0; }
        S.gmTransPx = 0;
        return false;
    }
    bool good = false;
    if (S.gmSlotValid[k]) {
        const float *pt = (const float *)S.ssbo[k].ptr;
        double sums[32];
        for (int q = 0; q < 32; ++q) sums[q] = 0.0;
        for (int t = 0; t < kGmThreads; ++t) {
            const float *row = pt + (size_t)t * kGmStride;
            for (int q = 0; q < kGmStride; ++q) sums[q] += (double)row[q];
        }
        double coverage = sums[27];
        double lvlpx = double((S.ext.width >> kGmLevel) * (S.ext.height >> kGmLevel));
        double minCov = 0.03 * (lvlpx > 0 ? lvlpx : 1.0);
        if (coverage > minCov && coverage > 64.0) {
            double H[6][6]; int idx = 0;
            for (int r = 0; r < 6; ++r) for (int c = r; c < 6; ++c) { H[r][c] = H[c][r] = sums[idx]; ++idx; }
            double b[6];
            for (int i = 0; i < 6; ++i) b[i] = sums[21 + i];
            for (int i = 0; i < 6; ++i) H[i][i] += 1e-3 * H[i][i] + 1e-9;  // Levenberg damping
            double dp[6];
            if (cholSolve6(H, b, dp)) {
                bool bounded = std::fabs(dp[0]) < 0.5 && std::fabs(dp[1]) < 0.5 && std::fabs(dp[2]) < 0.5 &&
                               std::fabs(dp[3]) < 0.5 && std::fabs(dp[4]) < 0.25 && std::fabs(dp[5]) < 0.25;
                if (bounded) {
                    double Mlin[3][3], Md[3][3], Mdi[3][3], Mnew[3][3];
                    affFromParams(S.gmLinAt[k], Mlin);
                    affFromParams(dp, Md);
                    if (invAff(Md, Mdi)) {
                        mulAff(Mlin, Mdi, Mnew);
                        double pnew[6];
                        paramsFromAff(Mnew, pnew);
                        bool sane = std::fabs(pnew[0]) < 0.6 && std::fabs(pnew[1]) < 0.6 && std::fabs(pnew[2]) < 0.6 &&
                                    std::fabs(pnew[3]) < 0.6 && std::fabs(pnew[4]) < 0.5 && std::fabs(pnew[5]) < 0.5;
                        if (sane) {
                            for (int i = 0; i < 6; ++i) S.gmRun[i] = pnew[i];
                            good = true; S.gmHaveEst = true;
                        }
                    }
                }
            }
        }
    }
    if (good) ++S.gmStable; else S.gmStable = 0;
    // auto: engaged after three good solves in a row; on: as soon as there is an estimate
    S.gmEngaged = S.opts.gm == 1 ? S.gmHaveEst : S.gmStable >= 3;
    for (int i = 0; i < 6; ++i) S.gmLinAt[k][i] = S.gmRun[i];
    S.gmSlotValid[k] = true;  // this frame's reduce writes the slot
    S.gmTransPx = (float)std::sqrt((S.gmRun[4] * S.ext.width) * (S.gmRun[4] * S.ext.width) +
                                   (S.gmRun[5] * S.ext.height) * (S.gmRun[5] * S.ext.height));
    return S.gmEngaged;
}

void freeSize() {
    for (int k = 0; k < WINFG_RING; ++k) {
        freeImg(S.in[k]);
        for (int g = 0; g < WINFG_MAX_GEN; ++g) freeBuf(S.synthU[k][g]);
        freeBuf(S.gmU[k]);
        freeBuf(S.ssbo[k]);
    }
    for (int l = 0; l < kLevels; ++l) {
        freeImg(S.pyrA[l]); freeImg(S.pyrB[l]); freeImg(S.pyrAs[l]); freeImg(S.flowLvl[l]);
        freeBuf(S.flowU[l]);
    }
    freeImg(S.expA); freeImg(S.expB); freeImg(S.flowRegA); freeImg(S.flowRegB);
    freeBuf(S.expandU); freeBuf(S.flowRegU);
    if (S.pool) vkDestroyDescriptorPool(P.dev, S.pool, nullptr);  // (frees every set of the size)
    S = Size{};  // (also the C1 state: a new size starts from the identity)
}

}  // namespace

// ── public ───────────────────────────────────────────────────────────────────────────────────────

bool winfg_init(VkDevice dev, VkPhysicalDevice phys, const WinfgSync *sync) {
    if (P.inited) return true;
    if (!dev || !phys || !sync || !sync->pending || !sync->completed || !sync->wait) return false;
    P.dev = dev; P.phys = phys; P.sync = *sync;
    vkGetPhysicalDeviceMemoryProperties(phys, &P.mp);
    // the formats the chain stores in and samples from (filtered: the luma and flow are read with the linear sampler)
    const VkFormat fmts[3] = {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R32_SFLOAT, VK_FORMAT_R16G16B16A16_SFLOAT};
    for (int i = 0; i < 3; ++i) {
        VkFormatProperties fp;
        vkGetPhysicalDeviceFormatProperties(phys, fmts[i], &fp);
        const VkFormatFeatureFlags need = VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                                          VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
        if ((fp.optimalTilingFeatures & need) != need) {
            LOG("[winfg] the GPU can't store and sample format %d with a linear filter: not available\n", (int)fmts[i]);
            return false;
        }
    }
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter = si.minFilter = VK_FILTER_LINEAR;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(P.dev, &si, nullptr, &P.linear) != VK_SUCCESS) return false;

    const auto Sm = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, W = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
               U = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, SB = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bool ok = makePipe(P.pLuma, embedded::OF3_LUMA, {{32, Sm}, {48, W}}) &&
              makePipe(P.pDown, embedded::OF3_DOWNSAMPLE, {{32, Sm}, {48, W}}) &&
              makePipe(P.pFlow, embedded::OF3_FLOW, {{0, U}, {32, Sm}, {33, Sm}, {34, Sm}, {48, W}}) &&
              makePipe(P.pFlowM4, embedded::OF3_FLOW_M4, {{0, U}, {32, Sm}, {33, Sm}, {34, Sm}, {48, W}}) &&
              makePipe(P.pExpand, embedded::OF3_EXPAND, {{0, U}, {1, U}, {32, Sm}, {48, W}, {49, W}}) &&
              makePipe(P.pExpandM4, embedded::OF3_EXPAND_M4, {{0, U}, {1, U}, {32, Sm}, {48, W}, {49, W}}) &&
              makePipe(P.pSynth, embedded::WFG_SYNTH, {{0, U}, {32, Sm}, {33, Sm}, {34, Sm}, {35, Sm}, {48, W}}) &&
              makePipe(P.pGmReduce, embedded::OF3_GM_REDUCE, {{1, U}, {16, SB}, {32, Sm}, {33, Sm}}) &&
              makePipe(P.pGmPrewarp, embedded::OF3_GM_PREWARP, {{1, U}, {32, Sm}, {48, W}}) &&
              makePipe(P.pFlowReg, embedded::OF3_FLOWREG, {{0, U}, {32, Sm}, {33, Sm}, {34, Sm}, {48, W}});
    if (!ok) {
        LOG("[winfg] the shaders' pipelines could not be made\n");
        return false;
    }
    P.inited = true;
    LOG("[winfg] frame generation pipelines ready (10 shaders)\n");
    return true;
}

bool winfg_built(void) { return S.ready; }

bool winfg_build(VkExtent2D e, const WinfgOpts *o, const VkImageView *outs, int nout) {
    if (!P.inited) return false;
    freeSize();
    if (!o || nout < WINFG_MAX_GEN || !e.width || !e.height) return false;
    S.opts = *o;
    if (S.opts.model != 3) S.opts.model = 4;
    if (S.opts.perf < 0 || S.opts.perf > 2) S.opts.perf = 1;
    if (S.opts.gm < 0 || S.opts.gm > 2) S.opts.gm = 0;
    if (S.opts.fr < 0 || S.opts.fr > 2) S.opts.fr = 0;
    if (S.opts.fr_iters < 0) S.opts.fr_iters = 0;
    if (S.opts.fr_iters > 16) S.opts.fr_iters = 16;
    if (!(S.opts.flow_scale >= 0.05f)) S.opts.flow_scale = 0.05f;
    if (S.opts.flow_scale > 4.0f) S.opts.flow_scale = 4.0f;
    S.ext = e;
    // perf: the finest level the block search reaches (win-fg's: quality 1, balanced 2, performance 3)
    S.F = S.opts.perf == 0 ? 1 : S.opts.perf == 2 ? 3 : 2;
    const int F = S.F;

    VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 256}, {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 256},
                                    {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 64}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 16}};
    VkDescriptorPoolCreateInfo pci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pci.maxSets = 256; pci.poolSizeCount = 4; pci.pPoolSizes = sizes;
    if (vkCreateDescriptorPool(P.dev, &pci, nullptr, &S.pool) != VK_SUCCESS) { freeSize(); return false; }

    const VkImageUsageFlags lumaUse = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    const VkImageUsageFlags flowUse = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    const VkImageUsageFlags regUse = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    bool ok = true;
    for (int k = 0; k < WINFG_RING && ok; ++k)
        ok = makeImg(S.in[k], e, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    for (int l = 0; l < kLevels && ok; ++l) {
        VkExtent2D le = levelExt(e, l);
        ok = makeImg(S.pyrA[l], le, VK_FORMAT_R32_SFLOAT, lumaUse) && makeImg(S.pyrB[l], le, VK_FORMAT_R32_SFLOAT, lumaUse) &&
             makeImg(S.flowLvl[l], le, VK_FORMAT_R16G16B16A16_SFLOAT, flowUse);
        if (ok && l >= F) ok = makeImg(S.pyrAs[l], le, VK_FORMAT_R32_SFLOAT, lumaUse);
    }
    ok = ok && makeImg(S.expA, e, VK_FORMAT_R16G16B16A16_SFLOAT, flowUse) &&
         makeImg(S.expB, e, VK_FORMAT_R16G16B16A16_SFLOAT, flowUse) &&
         makeImg(S.flowRegA, levelExt(e, F), VK_FORMAT_R16G16B16A16_SFLOAT, regUse) &&
         makeImg(S.flowRegB, levelExt(e, F), VK_FORMAT_R16G16B16A16_SFLOAT, regUse);

    const VkDescriptorType UBO = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    for (int k = 0; k < WINFG_RING && ok; ++k) {
        ok = makeBuf(sizeof(GMUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true, S.gmU[k]) &&
             makeBuf(sizeof(float) * kGmThreads * kGmStride, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true, S.ssbo[k]);
        if (ok) std::memset(S.ssbo[k].ptr, 0, sizeof(float) * kGmThreads * kGmStride);
        if (ok) std::memset(S.gmU[k].ptr, 0, sizeof(GMUBO));
        for (int g = 0; g < WINFG_MAX_GEN && ok; ++g) ok = makeBuf(sizeof(SynthUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true, S.synthU[k][g]);
    }
    ok = ok && makeBuf(sizeof(FlowUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true, S.expandU) &&
         makeBuf(sizeof(FlowRegUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true, S.flowRegU);
    for (int l = 0; l < kLevels && ok; ++l) ok = makeBuf(sizeof(FlowUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true, S.flowU[l]);
    if (!ok) {
        LOG("[winfg] the frame generation buffers and images for %ux%u could not be made\n", e.width, e.height);
        freeSize();
        return false;
    }
    {
        FlowUBO eu{S.opts.flow_scale, 0, 0.6f, 1.6f};
        std::memcpy(S.expandU.ptr, &eu, sizeof eu);
        for (int l = 0; l < kLevels; ++l) {
            FlowUBO fu{S.opts.flow_scale, (uint32_t)l, 0.6f, 1.6f};
            std::memcpy(S.flowU[l].ptr, &fu, sizeof fu);
        }
        FlowRegUBO ru{kFrDt, kFrLambda, kFrEdge, kFrEps};
        std::memcpy(S.flowRegU.ptr, &ru, sizeof ru);
    }

    // ── descriptor sets (all fixed; the frame's own images are chosen by its ring slot) ──
    const auto Sm = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, W = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
               SB = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    const auto GEN = VK_IMAGE_LAYOUT_GENERAL;
    const VkSampler lin = P.linear;
    for (int k = 0; k < WINFG_RING; ++k) {
        const int kp = (k + WINFG_RING - 1) % WINFG_RING;  // the previous game frame's slot
        S.sLumaCurr[k] = allocSet(P.pLuma.setLayout);
        S.sLumaPrev[k] = allocSet(P.pLuma.setLayout);
        wImg(S.sLumaCurr[k], 32, Sm, S.in[k].view, lin, GEN);
        wImg(S.sLumaCurr[k], 48, W, S.pyrA[0].view, VK_NULL_HANDLE, GEN);
        wImg(S.sLumaPrev[k], 32, Sm, S.in[kp].view, lin, GEN);
        wImg(S.sLumaPrev[k], 48, W, S.pyrB[0].view, VK_NULL_HANDLE, GEN);

        // C1 reduce: prev / curr luma at the GM level (template / image), the partials SSBO, the GM UBO
        S.sGmReduce[k] = allocSet(P.pGmReduce.setLayout);
        wBuf(S.sGmReduce[k], 1, UBO, S.gmU[k].buf, sizeof(GMUBO));
        wBuf(S.sGmReduce[k], 16, SB, S.ssbo[k].buf, VK_WHOLE_SIZE);
        wImg(S.sGmReduce[k], 32, Sm, S.pyrB[kGmLevel].view, lin, GEN);
        wImg(S.sGmReduce[k], 33, Sm, S.pyrA[kGmLevel].view, lin, GEN);

        // C1 pre-warp: the curr luma pyramid -> the stabilized one, for the levels the flow reads
        for (int l = F; l < kLevels; ++l) {
            S.sGmPrewarp[k][l] = allocSet(P.pGmPrewarp.setLayout);
            wBuf(S.sGmPrewarp[k][l], 1, UBO, S.gmU[k].buf, sizeof(GMUBO));
            wImg(S.sGmPrewarp[k][l], 32, Sm, S.pyrA[l].view, lin, GEN);
            wImg(S.sGmPrewarp[k][l], 48, W, S.pyrAs[l].view, VK_NULL_HANDLE, GEN);
        }

        // expansion: the finest solved flow, plus the GM add-back
        S.sExpand[k] = allocSet(P.pExpand.setLayout);
        wBuf(S.sExpand[k], 0, UBO, S.expandU.buf, sizeof(FlowUBO));
        wBuf(S.sExpand[k], 1, UBO, S.gmU[k].buf, sizeof(GMUBO));
        wImg(S.sExpand[k], 32, Sm, S.flowLvl[F].view, lin, GEN);
        wImg(S.sExpand[k], 48, W, S.expA.view, VK_NULL_HANDLE, GEN);
        wImg(S.sExpand[k], 49, W, S.expB.view, VK_NULL_HANDLE, GEN);

        // synthesis: prev and curr are the two game frames, the flow is shared; one set per generated frame
        for (int g = 0; g < WINFG_MAX_GEN; ++g) {
            S.sSynth[k][g] = allocSet(P.pSynth.setLayout);
            wBuf(S.sSynth[k][g], 0, UBO, S.synthU[k][g].buf, sizeof(SynthUBO));
            wImg(S.sSynth[k][g], 32, Sm, S.in[kp].view, lin, GEN);
            wImg(S.sSynth[k][g], 33, Sm, S.in[k].view, lin, GEN);
            wImg(S.sSynth[k][g], 34, Sm, S.expA.view, lin, GEN);
            wImg(S.sSynth[k][g], 35, Sm, S.expB.view, lin, GEN);
            wImg(S.sSynth[k][g], 48, W, outs[g], VK_NULL_HANDLE, GEN);
        }
    }
    // the flow of each level: prev luma (raw), curr luma (stabilized from the finest solved level down), the predictor
    // (the coarser level's flow, the coarsest its own) and the output
    for (int l = 0; l < kLevels; ++l) {
        int pred = (l + 1 < kLevels) ? l + 1 : l;
        VkImageView currLvl = l >= F ? S.pyrAs[l].view : S.pyrA[l].view;
        S.sFlow[l] = allocSet(P.pFlow.setLayout);  // (the M4 flow shader has the same bindings)
        wBuf(S.sFlow[l], 0, UBO, S.flowU[l].buf, sizeof(FlowUBO));
        wImg(S.sFlow[l], 32, Sm, S.pyrB[l].view, lin, GEN);
        wImg(S.sFlow[l], 33, Sm, currLvl, lin, GEN);
        wImg(S.sFlow[l], 34, Sm, S.flowLvl[pred].view, lin, GEN);
        wImg(S.sFlow[l], 48, W, S.flowLvl[l].view, VK_NULL_HANDLE, GEN);
    }
    for (int l = 1; l < kLevels; ++l) {
        S.sDownA[l - 1] = allocSet(P.pDown.setLayout);
        S.sDownB[l - 1] = allocSet(P.pDown.setLayout);
        wImg(S.sDownA[l - 1], 32, Sm, S.pyrA[l - 1].view, lin, GEN);
        wImg(S.sDownA[l - 1], 48, W, S.pyrA[l].view, VK_NULL_HANDLE, GEN);
        wImg(S.sDownB[l - 1], 32, Sm, S.pyrB[l - 1].view, lin, GEN);
        wImg(S.sDownB[l - 1], 48, W, S.pyrB[l].view, VK_NULL_HANDLE, GEN);
    }
    // C2: three sets for the ping-pong: f0 (the observation) is flowLvl[F]; u_in -> u_out alternates A and B
    S.sFlowReg0 = allocSet(P.pFlowReg.setLayout);
    S.sFlowRegAB = allocSet(P.pFlowReg.setLayout);
    S.sFlowRegBA = allocSet(P.pFlowReg.setLayout);
    for (VkDescriptorSet s : {S.sFlowReg0, S.sFlowRegAB, S.sFlowRegBA}) {
        wBuf(s, 0, UBO, S.flowRegU.buf, sizeof(FlowRegUBO));
        wImg(s, 33, Sm, S.flowLvl[F].view, lin, GEN);
        wImg(s, 34, Sm, S.pyrB[F].view, lin, GEN);
    }
    wImg(S.sFlowReg0, 32, Sm, S.flowLvl[F].view, lin, GEN);
    wImg(S.sFlowReg0, 48, W, S.flowRegA.view, VK_NULL_HANDLE, GEN);
    wImg(S.sFlowRegAB, 32, Sm, S.flowRegA.view, lin, GEN);
    wImg(S.sFlowRegAB, 48, W, S.flowRegB.view, VK_NULL_HANDLE, GEN);
    wImg(S.sFlowRegBA, 32, Sm, S.flowRegB.view, lin, GEN);
    wImg(S.sFlowRegBA, 48, W, S.flowRegA.view, VK_NULL_HANDLE, GEN);

    S.ready = true;
    LOG("[winfg] framegen %ux%u (%d levels, flow finest %d, model %d, C1 gm=%d, C2 fr=%d iters=%d)\n", e.width, e.height,
        kLevels, F, S.opts.model, S.opts.gm, S.opts.fr, S.opts.fr_iters);
    return true;
}

void winfg_free(void) {
    if (S.ready || S.pool) vkDeviceWaitIdle(P.dev);
    freeSize();
}

void winfg_begin(uint64_t count) {
    if (!S.ready) return;
    const int k = (int)(count % WINFG_RING);
    const uint64_t s = S.slotSeq[k];
    // (a submission not yet made is the frame's own: no wait for it)
    if (s && s < P.sync.pending() && s > P.sync.completed()) P.sync.wait(s);
}

VkImage winfg_input(VkCommandBuffer cb, uint64_t count) {
    const int k = (int)(count % WINFG_RING);
    discard(cb, S.in[k].image);
    return S.in[k].image;
}

void winfg_record(VkCommandBuffer cmd, uint64_t count, int ngen) {
    if (!S.ready || ngen < 0) return;
    if (ngen > WINFG_MAX_GEN) ngen = WINFG_MAX_GEN;
    const int k = (int)(count % WINFG_RING);
    S.slotSeq[k] = P.sync.pending();  // (the submission this command buffer goes into: the caller took its cb first)
    const bool m4 = S.opts.model == 4;
    const Pipe &flowP = m4 ? P.pFlowM4 : P.pFlow;
    const Pipe &expandP = m4 ? P.pExpandM4 : P.pExpand;
    const int F = S.F;
    const VkExtent2D ext = S.ext;

    const bool gmEng = updateGlobalMotion(k);
    {   // this slot's GM UBO: the running / applied affine (identity when not engaged: the passes are then no-ops)
        GMUBO g{};
        g.lin[0] = (float)S.gmRun[0]; g.lin[1] = (float)S.gmRun[1]; g.lin[2] = (float)S.gmRun[2]; g.lin[3] = (float)S.gmRun[3];
        g.linT[0] = (float)S.gmRun[4]; g.linT[1] = (float)S.gmRun[5]; g.linT[2] = gmEng ? 1.0f : 0.0f; g.linT[3] = 0.0f;
        if (gmEng) {
            g.app[0] = (float)S.gmRun[0]; g.app[1] = (float)S.gmRun[1]; g.app[2] = (float)S.gmRun[2]; g.app[3] = (float)S.gmRun[3];
            g.appT[0] = (float)S.gmRun[4]; g.appT[1] = (float)S.gmRun[5];
        }
        std::memcpy(S.gmU[k].ptr, &g, sizeof g);
    }
    static unsigned long long lastLog = 0;
    static int lastEng = -1;
    if ((int)gmEng != lastEng || S.gmFrames - lastLog >= 300) {
        LOG("[winfg] C1 global motion: engaged=%d mode=%d stable=%d transPx=%.1f (model %d)\n", gmEng ? 1 : 0, S.opts.gm,
            S.gmStable, S.gmTransPx, S.opts.model);
        lastEng = gmEng ? 1 : 0;
        lastLog = S.gmFrames;
    }
    // each generated frame's alpha: (g+1)/(ngen+1), in its own UBO
    for (int g = 0; g < ngen; ++g) {
        SynthUBO su{S.opts.flow_scale, (float)(g + 1) / (float)(ngen + 1), kBeta, kLambda, kEpsilon, kPhotoScale, 0, 0,
                    0, 0, 0, 0};
        std::memcpy(S.synthU[k][g].ptr, &su, sizeof su);
    }
    const bool frEng = S.opts.fr != 2 && S.opts.fr_iters > 0;

    // Internal images are fully rewritten every frame: discarded to GENERAL first (win-fg's).
    for (int l = 0; l < kLevels; ++l) { discard(cmd, S.pyrA[l].image); discard(cmd, S.pyrB[l].image); }
    for (int l = F; l < kLevels; ++l) discard(cmd, S.pyrAs[l].image);
    discard(cmd, S.expA.image); discard(cmd, S.expB.image);
    discard(cmd, S.flowRegA.image); discard(cmd, S.flowRegB.image);
    {   // the flow field starts at zero every frame (the coarsest level's predictor): no feedback between frames
        VkClearColorValue zero{};
        VkImageSubresourceRange rng{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        for (int l = F; l < kLevels; ++l) {
            imgBarrier(cmd, S.flowLvl[l].image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
            vkCmdClearColorImage(cmd, S.flowLvl[l].image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &rng);
            imgBarrier(cmd, S.flowLvl[l].image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
                       VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
        }
    }

    // 1) luma of the curr and prev game frames
    disp(cmd, P.pLuma, S.sLumaCurr[k], ext);
    disp(cmd, P.pLuma, S.sLumaPrev[k], ext);
    bar(cmd, S.pyrA[0].image); bar(cmd, S.pyrB[0].image);
    // 2) the pyramids
    for (int l = 1; l < kLevels; ++l) {
        disp(cmd, P.pDown, S.sDownA[l - 1], levelExt(ext, l));
        disp(cmd, P.pDown, S.sDownB[l - 1], levelExt(ext, l));
        bar(cmd, S.pyrA[l].image); bar(cmd, S.pyrB[l].image);
    }
    // 2b) C1: the LK normal equations over a fixed grid at the GM level, into this slot's SSBO (read back by a later
    //     frame, after its submission is waited for)
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, P.pGmReduce.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, P.pGmReduce.layout, 0, 1, &S.sGmReduce[k], 0, nullptr);
    vkCmdDispatch(cmd, kGmThreads / 64, 1, 1);
    {
        VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; mb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
    }
    // 2c) C1: the curr pyramid pre-warped by the applied affine (identity: an exact copy)
    for (int l = F; l < kLevels; ++l) {
        disp(cmd, P.pGmPrewarp, S.sGmPrewarp[k][l], levelExt(ext, l));
        bar(cmd, S.pyrAs[l].image);
    }
    // 3) the flow, coarse to fine, down to the finest solved level
    for (int l = kLevels - 1; l >= F; --l) {
        disp(cmd, flowP, S.sFlow[l], levelExt(ext, l));
        bar(cmd, S.flowLvl[l].image);
    }
    // 3b) C2: TV-L1 regularization of the finest level's flow (observation f0 = flowLvl[F], never written here); the
    //     last iterate is copied back, so the expansion reads it unchanged. Off: flowLvl[F] as it is.
    if (frEng) {
        const VkExtent2D fe = levelExt(ext, F);
        Img *last = nullptr;
        for (int it = 0; it < S.opts.fr_iters; ++it) {
            VkDescriptorSet set; Img *out;
            if (it == 0) { set = S.sFlowReg0; out = &S.flowRegA; }
            else if (it % 2 == 1) { set = S.sFlowRegAB; out = &S.flowRegB; }
            else { set = S.sFlowRegBA; out = &S.flowRegA; }
            disp(cmd, P.pFlowReg, set, fe);
            bar(cmd, out->image);
            last = out;
        }
        VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        mb.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        mb.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
        VkImageCopy region{};
        region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.extent = {fe.width, fe.height, 1};
        vkCmdCopyImage(cmd, last->image, VK_IMAGE_LAYOUT_GENERAL, S.flowLvl[F].image, VK_IMAGE_LAYOUT_GENERAL, 1, &region);
        VkMemoryBarrier mb2{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        mb2.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; mb2.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &mb2, 0, nullptr, 0, nullptr);
    }
    // 4) expansion to full resolution (+ the GM add-back)
    disp(cmd, expandP, S.sExpand[k], ext);
    bar(cmd, S.expA.image); bar(cmd, S.expB.image);
    // 5) the in-between frames
    for (int g = 0; g < ngen; ++g) disp(cmd, P.pSynth, S.sSynth[k][g], ext);
}

}  // namespace winfg

// The C entry points (the engine's namespace kept inside)
extern "C" {
bool winfg_init(VkDevice dev, VkPhysicalDevice phys, const WinfgSync *sync) { return winfg::winfg_init(dev, phys, sync); }
bool winfg_build(VkExtent2D e, const WinfgOpts *o, const VkImageView *outs, int nout) { return winfg::winfg_build(e, o, outs, nout); }
void winfg_free(void) { winfg::winfg_free(); }
bool winfg_built(void) { return winfg::winfg_built(); }
void winfg_begin(uint64_t count) { winfg::winfg_begin(count); }
VkImage winfg_input(VkCommandBuffer cb, uint64_t count) { return winfg::winfg_input(cb, count); }
void winfg_record(VkCommandBuffer cb, uint64_t count, int ngen) { winfg::winfg_record(cb, count, ngen); }
}

#endif  // __ANDROID__
