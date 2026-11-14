// Win-FG frame generation for TOTKDroid (MIT): the compute chain of The412Banner/win-fg (MIT, LICENSE.md here), made
// for the phone's own Vulkan present. winfg.cpp is the engine; framegen.c calls it (vk_fg_frame, engine "Win-FG").
// Per game frame: winfg_input (the game's image is copied in), winfg_record (the shared motion and C1/C2 passes once,
// then one synthesized frame per generation). Ring of WINFG_RING slots: a slot's previous work is waited for before
// it is reused (winfg_begin), so no descriptor or buffer is written while the GPU still reads it.
#pragma once
#include <volk.h>

#define WINFG_RING 3
#define WINFG_MAX_GEN 3

#ifdef __cplusplus
extern "C" {
#endif

// What the engine needs from the host (vk.h's submissions, which the C++ file doesn't include): the number the pending
// submission will get, the last completed one, and a wait for one. The engine waits only for submitted numbers.
typedef struct WinfgSync {
    uint64_t (*pending)(void);
    uint64_t (*completed)(void);
    void (*wait)(uint64_t seq);
} WinfgSync;

typedef struct WinfgOpts {
    int model;          // 3 = symmetric optical flow, 4 = bidirectional (the default)
    int perf;           // 0 quality, 1 balanced (default), 2 performance: the finest solved flow level
    int gm;             // C1 global-motion pre-warp: 0 auto (default), 1 on, 2 off
    int fr;             // C2 TV-L1 flow regularization: 0 auto (default), 1 on, 2 off
    int fr_iters;       // C2 iterations (4 by default; 0 = off)
    float flow_scale;   // scales the solved flow (1 by default)
} WinfgOpts;

// Pipelines and the sampler: once per process. False when the GPU can't run them (said in the log).
bool winfg_init(VkDevice dev, VkPhysicalDevice phys, const WinfgSync *sync);
// Makes everything for this size and these options, writing into outs[0..nout) (the generated frames' images, storage
// views, RGBA8). Caller: the device idle (vk_wait_ready, vkDeviceWaitIdle) before it is called again.
bool winfg_build(VkExtent2D e, const WinfgOpts *o, const VkImageView *outs, int nout);
void winfg_free(void);       // the size's resources; the caller has made the device idle
bool winfg_built(void);

// Before a game frame's work: waits for the ring slot's previous submission (the slot is reused).
void winfg_begin(uint64_t count);
// The image the game's frame is copied into for this frame (GENERAL; the caller blits into it).
VkImage winfg_input(VkCommandBuffer cb, uint64_t count);
// Records the shared passes and ngen (0..WINFG_MAX_GEN) generated frames; frame g is at alpha (g+1)/(ngen+1) between the
// previous game frame (input of count-1) and this one. ngen 0 still runs the shared passes: the motion estimate (C1's
// history) advances per recorded frame, so record every frame from the second on. Call after vk_cmd(): the slot's
// submission is the pending one.
void winfg_record(VkCommandBuffer cb, uint64_t count, int ngen);

#ifdef __cplusplus
}
#endif
