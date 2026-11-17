// Frame generation in the present, with two engines (the menu's "Frame generation": settings fg_engine, off by default).
// vk_present_now hands each game frame here before it shows it: the game's image is copied into the engine's input, and
// the frames between the previous game frame and this one are written to images of their own, recorded into the frame's
// command buffer ahead of the window's draws. The present then shows each of them (through the same upscaling and with
// the overlay drawn again: menu_render_again), held for an equal share of the game's frame interval, before the game's.
//  - Win-FG (winfg/, MIT): the compute chain of The412Banner/win-fg (MIT), built into every APK: motion estimation with the
//    global-motion pre-warp (C1) and the TV-L1 flow regularization (C2), then the synthesis. Quality: fg_perf.
//  - LSFG (lsfg/, GPL-3.0-or-later, ported from Eden through Swap2-Native; this file is TOTKDroid's own glue, MIT, after
//    Swap2-Native's vka_framegen.c): Lossless Scaling's frame generation. The user's own Lossless.dll is never in the APK:
//    looked for on the phone's storage (LSFG/, Download/, Documents/Emulation/bios, Switch2/, the storage's root, the app's
//    data folder), or named by LARECOMP_LSFG_DLL. Without TOTK_LSFG (a build without the GPL folder) it reports "not in
//    this build".
// Both: the images per game frame are fg_mult (2-4). Env (tests): LARECOMP_FG=0/1/2 (off / Win-FG / LSFG),
// LARECOMP_FG_MULT=2..4, LARECOMP_LSFG_FLOW=0 (auto) / 25..100, LARECOMP_LSFG_DLL.
#ifdef __ANDROID__
#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../paths.h"
#include "../../settings.h"
#include "vk.h"
#ifdef TOTK_LSFG
#include "lsfg/lsfg.h"
#endif
#include "winfg/winfg.h"

static char g_status[700] = "off";
static int g_state;  // 1 generating, 2 on and waiting, 3 on and no refresh left, 0 off, -1 unavailable (ui/perf_overlay.mm)
const char *vk_fg_status(void) { return g_status; }  // (the menu's line under the setting)
int vk_fg_state(void) { return g_state; }
static void status(const char *s) {
    g_state = !strncmp(s, "generating", 10) ? 1 : strstr(s, "no refresh left") ? 3 : !strncmp(s, "on", 2) ? 2
            : !strcmp(s, "off") ? 0 : -1;
    if (strcmp(g_status, s)) LOG("[fg] %s\n", s);
    snprintf(g_status, sizeof g_status, "%s", s);
}

static int env_int(const char *name, int def) {
    const char *e = getenv(name);
    return e && *e ? atoi(e) : def;
}
// The engine (settings.h: FG_OFF, FG_WINFG, FG_LSFG); LARECOMP_FG overrides it (tests)
static int fg_engine(void) {
    int v = env_int("LARECOMP_FG", g_settings.fg_engine);
    return v == FG_WINFG || v == FG_LSFG ? v : FG_OFF;
}
static bool fg_on(void) { return fg_engine() == FG_LSFG; }
bool vk_fg_built(void) { return true; }  // (Win-FG is in every build; LSFG reports its absence in the status)

// The bytes the frame generation wrote to the GPU since the start: its uniform blocks (the [device] line)
uint64_t vk_fg_upload_bytes(void) {
#ifdef TOTK_LSFG
    extern uint64_t lsfg_upload_bytes(void);  // (lsfg/lsfg_chain.c)
    return winfg_upload_bytes() + lsfg_upload_bytes();
#else
    return winfg_upload_bytes();
#endif
}

// The panel's refresh period (s), 0 when unknown
static double refresh_s(void) {
    const SDL_DisplayMode *m = SDL_GetCurrentDisplayMode(SDL_GetPrimaryDisplay());
    return m && m->refresh_rate > 1 ? 1.0 / m->refresh_rate : 0;
}

// ---- Win-FG (winfg/winfg.cpp, MIT) ----------------------------------------------------------------------------------
// The outputs: RGBA8 images the frames are drawn into and shown from, as LSFG's are. The engine keeps its own inputs.
static struct {
    bool built;                      // winfg_build made the size's resources
    bool failed;                     // this engine can't run here (said in the status); off, then on again, retries
    bool fresh;                      // the outputs were just made: their layout is set (UNDEFINED -> GENERAL) in this frame
    VkExtent2D ext;
    int perf;
    TexBackend out[WINFG_MAX_GEN];
    VkImageView out_view[WINFG_MAX_GEN];
    uint64_t count, last_ns;
    double interval;                 // the game's frame interval, smoothed (s)
    int ng_cur, ng_pend;             // the generated frames per game frame in use, and the frames a new count has held
} W;
#define WINFG_MAX_OUT 120.0          // the output rate Win-FG fills up to (a second), and no more than the display's refresh

static uint64_t w_pending(void) { return vk_pending_seq(); }
static uint64_t w_completed(void) { return vk_completed_seq(); }
static void w_wait(uint64_t seq) { vk_wait_seq(seq); }
static const WinfgSync w_sync = {w_pending, w_completed, w_wait};

static void wout_free(int i) {
    TexBackend *t = &W.out[i];
    if (W.out_view[i]) vkDestroyImageView(g_vk.dev, W.out_view[i], NULL);
    W.out_view[i] = VK_NULL_HANDLE;
    if (t->image) {
        vk_views_forget(t);
        vmaDestroyImage(g_vk.vma, t->image, t->mem);
    }
    memset(t, 0, sizeof *t);
}

static bool wout_new(VkExtent2D e, int i) {
    TexBackend *t = &W.out[i];
    memset(t, 0, sizeof *t);
    VkImageCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D,
                            .format = VK_FORMAT_R8G8B8A8_UNORM, .extent = {e.width, e.height, 1}, .mipLevels = 1,
                            .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
                            .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                     VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT};
    VmaAllocationCreateInfo ai = {.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
    if (vmaCreateImage(g_vk.vma, &ci, &ai, &t->image, &t->mem, NULL) != VK_SUCCESS) {
        LOG("[winfg] output image %ux%u: failed\n", e.width, e.height);
        return false;
    }
    t->format = VK_FORMAT_R8G8B8A8_UNORM, t->base_type = VK_IMAGE_VIEW_TYPE_2D, t->aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    t->width = e.width, t->height = e.height, t->depth = t->layers = t->levels = t->samples = 1;
    t->gpu_owned = t->uploaded = true;
    VkImageViewCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = t->image,
                                .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = VK_FORMAT_R8G8B8A8_UNORM,
                                .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    return vkCreateImageView(g_vk.dev, &vi, NULL, &W.out_view[i]) == VK_SUCCESS;
}

// Drops the engine's size resources and outputs (the GPU first: they may still be in use)
static void winfg_drop(void) {
    vk_wait_ready();
    vkDeviceWaitIdle(g_vk.dev);
    winfg_free();
    for (int i = 0; i < WINFG_MAX_GEN; i++) wout_free(i);
    W.built = false;
    W.count = 0;
    W.last_ns = 0;
    W.interval = 0;
    W.ng_cur = W.ng_pend = 0;
}

static void winfg_release(void) {  // (Win-FG off: its memory goes back)
    W.failed = false;
    if (!W.built && !W.out[0].image) return;
    winfg_drop();
    LOG("[winfg] off\n");
}

// The formats the engine stores and samples (winfg.cpp checks the linear filter itself)
static bool winfg_device_ok(void) {
    const VkFormat fmts[3] = {VK_FORMAT_R32_SFLOAT, VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R8G8B8A8_UNORM};
    for (int i = 0; i < 3; i++) {
        VkFormatProperties fp;
        vkGetPhysicalDeviceFormatProperties(g_vk.phys, fmts[i], &fp);
        if (!(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) ||
            !(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) {
            LOG("[winfg] the GPU can't store and sample format %d: not available\n", (int)fmts[i]);
            return false;
        }
    }
    return true;
}

// The game's image b about to be shown: the generated frames before it (gen, at most 3; their count returned) and how
// long each image of this game frame should stay (*hold, s). The work is recorded into the frame's command buffer.
static int winfg_frame(TexBackend *b, double *hold, TexBackend *gen[3]) {
    *hold = 0;
    if (W.failed) {
        status("unavailable: Win-FG can't run on this GPU (see the [winfg] lines)");
        return 0;
    }
    if (!b || !b->image || b->aspect != VK_IMAGE_ASPECT_COLOR_BIT || b->samples != 1) return 0;
    if (!winfg_device_ok() || !winfg_init(g_vk.dev, g_vk.phys, &w_sync)) {
        W.failed = true;
        status("unavailable: Win-FG can't run on this GPU (see the [winfg] lines)");
        return 0;
    }
    VkFormatProperties fp;
    vkGetPhysicalDeviceFormatProperties(g_vk.phys, b->format, &fp);
    if (!(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT)) {
        static int said;
        if (!said++) LOG("[winfg] the game's image (format %d) can't be copied into the chain: not generating\n", (int)b->format);
        status("on, but the game's image can't be read: not generating (Win-FG)");
        return 0;
    }
    int mult = env_int("LARECOMP_FG_MULT", g_settings.fg_mult);
    if (mult < 2) mult = 2;
    if (mult > 4) mult = 4;
    int perf = g_settings.fg_perf < 0 || g_settings.fg_perf > 2 ? 1 : g_settings.fg_perf;
    uint64_t now = SDL_GetTicksNS();
    if (W.last_ns) {  // (the game's frame interval: what the generated frames share out)
        double dt = (now - W.last_ns) / 1e9;
        if (dt > 0.25) dt = 0.25;
        W.interval = W.interval > 0 ? W.interval * 0.9 + dt * 0.1 : dt;
    }
    W.last_ns = now;
    VkExtent2D e = {b->width, b->height};
    if (!W.built || W.ext.width != e.width || W.ext.height != e.height || W.perf != perf) {
        winfg_drop();
        bool ok = true;
        for (int i = 0; i < WINFG_MAX_GEN && ok; i++) ok = wout_new(e, i);
        WinfgOpts o = {4, perf, 0, 0, 4, 1.0f};  // (model 4, bidirectional; C1 and C2 automatic; win-fg's defaults)
        if (ok) ok = winfg_build(e, &o, W.out_view, WINFG_MAX_GEN);
        if (!ok) {
            winfg_drop();
            W.failed = true;
            status("unavailable: the Win-FG chain could not be made (see the [winfg] lines)");
            return 0;
        }
        W.built = true, W.fresh = true, W.ext = e, W.perf = perf;
        LOG("[winfg] frame generation for %ux%u (perf %d, %dx)\n", e.width, e.height, perf, mult);
    }
    uint64_t count = W.count++;
    winfg_begin(count);  // (the slot's previous frame is done: its input and buffers may be written)
    VkCommandBuffer cb = vk_cmd();
    if (W.fresh) {  // (the outputs, first used now: nothing has read them, so their layout is set here, once)
        for (int i = 0; i < WINFG_MAX_GEN; i++) vk_image_init_layout(cb, W.out[i].image, VK_IMAGE_ASPECT_COLOR_BIT);
        W.fresh = false;
    }
    vk_full_barrier(cb);  // (the game's writes to its image)
    VkImage in = winfg_input(cb, count);
    VkImageBlit bl = {{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {{0, 0, 0}, {(int32_t)e.width, (int32_t)e.height, 1}},
                      {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {{0, 0, 0}, {(int32_t)e.width, (int32_t)e.height, 1}}};
    vkCmdBlitImage(cb, b->image, VK_IMAGE_LAYOUT_GENERAL, in, VK_IMAGE_LAYOUT_GENERAL, 1, &bl, VK_FILTER_NEAREST);
    vk_full_barrier(cb);
    // the first game frame has no previous one: nothing between
    double r = refresh_s(), period = W.interval > 0 ? W.interval : 1.0 / 30;
    // The generated frames per game frame: the setting, at most what fits the output cap at the game's rate (the
    // images (ng+1) a game frame: (ng+1)/period <= cap, one a refresh at most). The game switches between 30 and 60
    // by itself, which would flip that every few frames, and each flip respaces the frames (the ghosting): a new count
    // is taken only once it has held for half a second.
    double cap = WINFG_MAX_OUT;
    if (r > 0 && 1.0 / r < cap) cap = 1.0 / r;
    int fits = (int)floor(cap * period + 0.001) - 1;
    int want = mult - 1;
    if (want > fits) want = fits < 0 ? 0 : fits;
    if (want == W.ng_cur) {
        W.ng_pend = 0;
    } else if (++W.ng_pend >= 30) {
        W.ng_cur = want, W.ng_pend = 0;
    }
    size_t ng = count >= 1 && W.ng_cur > 0 ? (size_t)W.ng_cur : 0;
    // (every frame from the second on, even with no generated frame to show: the motion estimate keeps its history)
    if (count >= 1) winfg_record(cb, count, (int)ng);
    vk_full_barrier(cb);  // (the outputs, for the window's draws after this)
    for (size_t g = 0; g < ng; g++) gen[g] = &W.out[g];
    if (ng) *hold = period / (double)(ng + 1);
    char s[160];
    if (ng) snprintf(s, sizeof s, "generating: %zu frame%s per game frame (Win-FG)", ng, ng > 1 ? "s" : "");
    else snprintf(s, sizeof s, r > 0 && period / r < 1.9 ? "on: the display has no refresh left for a generated frame (Win-FG)"
                                                         : "on, waiting for a steady frame rate (Win-FG)");
    status(s);
    return (int)ng;
}

#ifndef TOTK_LSFG
bool vk_fg_lsfg_dll_present(void) { return false; }  // (no LSFG in this build)
static void lsfg_idle(void) {}
static int lsfg_frame(TexBackend *b, double *hold, TexBackend *gen[3]) {
    (void)b; (void)gen;
    *hold = 0;
    status(fg_on() ? "not in this build (built without TOTK_LSFG)" : "off");
    return 0;
}
#else

static struct {
    int state;  // 0 not tried, 1 the shaders loaded, -1 unavailable (said in g_status)
    lsfg_modules mods;
    lsfg_chain *chain;
    VkExtent2D ext;
    int flow;
    TexBackend out[LSFG_MAX_GENERATIONS];
    VkImageView out_view[LSFG_MAX_GENERATIONS];
    lsfg_pacer pacer;
    uint64_t count, last_ns;
    uint32_t warm_streak;
    double interval;  // the game's frame interval, smoothed (s)
} F;

static void unavailable(const char *why) {
    F.state = -1;
    status(why);
}

// What the shaders use: 16-bit floats, storage images of the extended formats and without a format, null descriptors
// (device.c turns them on where the driver has them), and storage images of the chain's formats
static bool device_ok(void) {
    VkPhysicalDeviceFeatures f;
    vkGetPhysicalDeviceFeatures(g_vk.phys, &f);
    const char *miss = !g_vk.shader_float16 ? "shaderFloat16"
                     : !f.shaderStorageImageExtendedFormats ? "shaderStorageImageExtendedFormats"
                     : !f.shaderStorageImageWriteWithoutFormat ? "shaderStorageImageWriteWithoutFormat" : NULL;
    // (no null descriptors - VK_EXT_robustness2, missing on Mali r44: the chain binds a blank image instead, lsfg_chain.c;
    // BOTWRecomp e3ccd7a, #402 there)
    if (!miss && !g_vk.null_desc) LOG("[lsfg] no null descriptors on this GPU: a blank image in the unbound slots\n");
    const VkFormat fmts[3] = {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8_UNORM, VK_FORMAT_R16G16B16A16_SFLOAT};
    for (int i = 0; i < 3 && !miss; i++) {
        VkFormatProperties fp;
        vkGetPhysicalDeviceFormatProperties(g_vk.phys, fmts[i], &fp);
        if (!(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) ||
            !(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
            miss = "storage images of R8, RGBA8 or RGBA16F";
    }
    if (miss) {
        char b[160];
        snprintf(b, sizeof b, "unavailable: the GPU lacks %s", miss);
        unavailable(b);
    }
    return !miss;
}

static bool is_dll(const char *p) {
    FILE *f = fopen(p, "rb");
    if (!f) return false;
    char mz[2] = {0};
    bool ok = fread(mz, 1, 2, f) == 2 && mz[0] == 'M' && mz[1] == 'Z';
    fseek(f, 0, SEEK_END);
    ok = ok && ftell(f) > 100000;
    fclose(f);
    return ok;
}

// The user's Lossless.dll: LARECOMP_LSFG_DLL, else the first of the usual places that has one the app can open (the access
// to all files reaches the storage's folders; the app's data folder, Android/data/com.totkrecompandroid/files, where
// Video > Frame generation > Import Lossless.dll puts its copy, always). *seen: one there that the app may not open -
// Android shows a file in Download/ to an app without All files access and then refuses it (BOTWRecomp e3ccd7a).
static bool find_dll(char *out, size_t n, char *seen, size_t sn) {
    seen[0] = 0;
    const char *e = getenv("LARECOMP_LSFG_DLL");
    if (e && *e) { snprintf(out, n, "%s", e); return is_dll(out); }
    static const char *const known[] = {"/storage/emulated/0/LSFG/Lossless.dll", "/storage/emulated/0/Download/Lossless.dll",
                                        "/storage/emulated/0/Documents/Emulation/bios/Lossless.dll",
                                        "/storage/emulated/0/Switch2/Lossless.dll", "/storage/emulated/0/Lossless.dll"};
    snprintf(out, n, "%s/Lossless.dll", rt_data_root());
    if (is_dll(out)) return true;
    for (size_t i = 0; i < sizeof known / sizeof *known; i++) {
        snprintf(out, n, "%s", known[i]);
        if (is_dll(out)) return true;
        if (!seen[0] && access(known[i], F_OK) == 0) {  // (there, but not a DLL the app could read: can it open it?)
            FILE *f = fopen(known[i], "rb");
            if (f) fclose(f);
            else snprintf(seen, sn, "%s", known[i]);
        }
    }
    return false;
}

// After Video > Frame generation > Import Lossless.dll put a copy in the app's files (ui/launcher.mm): look again
static int g_dll_reload;
void vk_fg_dll_imported(void) { __atomic_store_n(&g_dll_reload, 1, __ATOMIC_RELEASE); }

// The menu's frame generation list: is a Lossless.dll there that the app can open? (looked again at most once a second:
// the menu draws every frame)
bool vk_fg_lsfg_dll_present(void) {
    static uint64_t at;
    static bool have;
    uint64_t now = SDL_GetTicksNS();
    if (!at || now - at > 1000000000ull) {
        char p[512], seen[512];
        have = find_dll(p, sizeof p, seen, sizeof seen);
        at = now;
    }
    return have;
}

static bool load(void) {
    if (__atomic_exchange_n(&g_dll_reload, 0, __ATOMIC_ACQ_REL) && F.state < 0) F.state = 0;
    if (F.state) return F.state > 0;
    if (!device_ok()) return false;
    char path[512], seen[512];
    if (!find_dll(path, sizeof path, seen, sizeof seen)) {
        char b[700];
        if (seen[0])
            snprintf(b, sizeof b, "unavailable: Android doesn't let the app open %s (no All files access): import it with "
                     "Import Lossless.dll below, or allow TOTKDroid All files access in Android's settings", seen);
        else
            snprintf(b, sizeof b, "unavailable: no Lossless.dll (put yours in LSFG/ or Download/ on the phone's storage, "
                     "or import it with Import Lossless.dll below)");
        unavailable(b);
        return false;
    }
    uint64_t t0 = SDL_GetTicksNS();
    int r = lsfg_dll_load(path, &F.mods);
    if (r) {
        char b[256];
        snprintf(b, sizeof b, "unavailable: %s: %s", path, lsfg_status_name(r));
        unavailable(b);
        return false;
    }
    LOG("[lsfg] %d shaders from %s (%.1f ms)\n", F.mods.n, path, (SDL_GetTicksNS() - t0) / 1e6);
    F.state = 1;
    return true;
}

static void out_free(int i) {
    TexBackend *t = &F.out[i];
    if (F.out_view[i]) vkDestroyImageView(g_vk.dev, F.out_view[i], NULL);
    F.out_view[i] = VK_NULL_HANDLE;
    if (t->image) {
        vk_views_forget(t);
        vmaDestroyImage(g_vk.vma, t->image, t->mem);
    }
    memset(t, 0, sizeof *t);
}

static void teardown(void) {
    if (!F.chain && !F.out[0].image) return;
    vk_wait_ready();  // (the GPU done with the chain and the outputs)
    vkDeviceWaitIdle(g_vk.dev);
    lsfg_chain_free(F.chain);
    F.chain = NULL;
    for (int i = 0; i < LSFG_MAX_GENERATIONS; i++) out_free(i);
    lsfg_pacer_reset(&F.pacer);
    F.count = 0, F.warm_streak = 0;
}

// An output as a texture the present draws as it draws the game's (vk_present_draw: upscaling and all)
static bool out_new(VkExtent2D e, int i) {
    TexBackend *t = &F.out[i];
    memset(t, 0, sizeof *t);
    VkImageCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D,
                            .format = VK_FORMAT_R8G8B8A8_UNORM, .extent = {e.width, e.height, 1}, .mipLevels = 1,
                            .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
                            .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                     VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT};
    VmaAllocationCreateInfo ai = {.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
    if (vmaCreateImage(g_vk.vma, &ci, &ai, &t->image, &t->mem, NULL) != VK_SUCCESS) {
        LOG("[lsfg] output image %ux%u: failed\n", e.width, e.height);
        return false;
    }
    t->format = VK_FORMAT_R8G8B8A8_UNORM, t->base_type = VK_IMAGE_VIEW_TYPE_2D, t->aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    t->width = e.width, t->height = e.height, t->depth = t->layers = t->levels = t->samples = 1;
    t->gpu_owned = t->uploaded = true;
    VkImageViewCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = t->image,
                                .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = VK_FORMAT_R8G8B8A8_UNORM,
                                .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    if (vkCreateImageView(g_vk.dev, &vi, NULL, &F.out_view[i]) != VK_SUCCESS) return false;
    return true;
}

static bool rebuild(VkExtent2D e, int flow) {
    teardown();
    VkImage imgs[LSFG_MAX_GENERATIONS];
    for (int i = 0; i < LSFG_MAX_GENERATIONS; i++) {
        if (!out_new(e, i)) { teardown(); return false; }
        imgs[i] = F.out[i].image;
    }
    uint64_t t0 = SDL_GetTicksNS();
    F.chain = lsfg_chain_new(&F.mods, e, flow ? flow / 100.0f : 1.0f, imgs, F.out_view);  // (outputs put in GENERAL by it)
    if (!F.chain) { teardown(); return false; }
    F.ext = e, F.flow = flow;
    LOG("[lsfg] chain for %ux%u (flow %d%%) made in %.1f ms\n", e.width, e.height, flow ? flow : 100,
        (SDL_GetTicksNS() - t0) / 1e6);
    return true;
}

static void lsfg_idle(void) {  // (LSFG off: its chain goes back)
    if (F.chain) { teardown(); LOG("[lsfg] off\n"); }
}

// The LSFG part of the frame (vk_fg_frame): the game's image b, the generated frames gen (their count returned) and how
// long each image of this game frame should stay (*hold, s). The work is recorded into the frame's command buffer.
// Render thread.
static int lsfg_frame(TexBackend *b, double *hold, TexBackend *gen[3]) {
    *hold = 0;
    if (!fg_on()) {
        lsfg_idle();
        if (F.state >= 0) status("off");
        return 0;
    }
    if (!load()) return 0;
    uint64_t now = SDL_GetTicksNS();
    if (F.last_ns) {  // (the game's frame interval: what the generated frames share out)
        double dt = (now - F.last_ns) / 1e9;
        if (dt > 0.25) dt = 0.25;
        F.interval = F.interval > 0 ? F.interval * 0.9 + dt * 0.1 : dt;
    }
    F.last_ns = now;
    if (!b || !b->image || b->aspect != VK_IMAGE_ASPECT_COLOR_BIT || b->samples != 1) return 0;
    VkFormatProperties fp;
    vkGetPhysicalDeviceFormatProperties(g_vk.phys, b->format, &fp);
    if (!(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT)) {
        static int said;
        if (!said++) LOG("[lsfg] the game's image (format %d) can't be copied into the chain: not generating\n", (int)b->format);
        status("on, but the game's image can't be read: not generating");
        return 0;
    }
    int mult = env_int("LARECOMP_FG_MULT", g_settings.fg_mult), flow = env_int("LARECOMP_LSFG_FLOW", g_settings.lsfg_flow);
    if (mult < 2) mult = 2;
    if (mult > 4) mult = 4;
    if (flow && flow < 25) flow = 25;
    if (flow > 100) flow = 100;
    VkExtent2D e = {b->width, b->height};
    if (!F.chain || F.ext.width != e.width || F.ext.height != e.height || F.flow != flow) {
        if (!rebuild(e, flow)) { unavailable("unavailable: the frame generation chain could not be made (see [lsfg] lines)"); return 0; }
    }
    double tnow = now / 1e9;
    lsfg_plan plan = lsfg_pacer_plan(&F.pacer, LSFG_MAX_GENERATIONS, mult, 0, tnow);
    uint64_t count = F.count++;
    int warm = plan.warm && count + 1 >= 2;
    F.warm_streak = warm ? F.warm_streak + 1 : 0;
    size_t ng = warm && F.warm_streak >= 2 ? plan.generations : 0;
    if (ng > LSFG_MAX_GENERATIONS) ng = LSFG_MAX_GENERATIONS;
    // (one image a refresh: no more generated frames than the refreshes a game frame leaves room for)
    double r = refresh_s(), period = F.interval > 0 ? F.interval : 1.0 / 30;
    if (ng && r > 0) {
        double n = period / r;  // (30 fps on 120 Hz: 4; on 60 Hz: 2)
        size_t fit = n < 1.9 ? 0 : (size_t)(n + 0.1) - 1;
        if (ng > fit) ng = fit, F.pacer.issued = ng;
    }
    VkCommandBuffer cb = vk_cmd();
    vk_full_barrier(cb);  // (the game's writes to its image)
    VkImageBlit bl = {{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {{0, 0, 0}, {(int32_t)e.width, (int32_t)e.height, 1}},
                      {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {{0, 0, 0}, {(int32_t)e.width, (int32_t)e.height, 1}}};
    vkCmdBlitImage(cb, b->image, VK_IMAGE_LAYOUT_GENERAL, lsfg_chain_input(F.chain, count), VK_IMAGE_LAYOUT_GENERAL, 1, &bl,
                   VK_FILTER_NEAREST);
    if (warm) lsfg_chain_shared(F.chain, cb, count);
    for (size_t g = 0; g < ng; g++) lsfg_chain_generate(F.chain, cb, count, ng, g);
    vk_full_barrier(cb);  // (the outputs, for the window's draws after this)
    for (size_t g = 0; g < ng; g++) gen[g] = &F.out[g];
    if (ng) *hold = period / (double)(ng + 1);
    char s[96];
    if (ng) snprintf(s, sizeof s, "generating: %zu frame%s per game frame", ng, ng > 1 ? "s" : "");
    else snprintf(s, sizeof s, r > 0 && period / r < 1.9 ? "on: the display has no refresh left for a generated frame"
                                                         : "on, waiting for a steady frame rate");
    status(s);
    return (int)ng;
}
#endif

// The frames between the game's last frame and this one, by the engine chosen (above). Render thread (vk_present_now).
int vk_fg_frame(TexBackend *b, double *hold, TexBackend *gen[3]) {
    *hold = 0;
    int eng = fg_engine();
    if (eng != FG_WINFG) winfg_release();  // (the engine off: its memory goes back)
    if (eng != FG_LSFG) lsfg_idle();
    if (eng == FG_WINFG) return winfg_frame(b, hold, gen);
    if (eng == FG_LSFG) return lsfg_frame(b, hold, gen);
    status("off");
    return 0;
}
#endif
