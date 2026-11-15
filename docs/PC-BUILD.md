# PC build (Windows x64): status

The Android port is the only build that runs today. This file tracks the Windows build (step 1 of the plan).

## What is checked now

`tools/check_windows_compile.sh` compiles every runtime C file for `x86_64-windows-gnu` with clang (the `ziglang` pip
package), against SDL3 3.4.16, Vulkan-Headers and volk (1.4.350.1), VulkanMemoryAllocator 3.3.0 and shaderc's headers.
It needs neither the Android SDK nor the game's files. The workflow `windows-compile-check.yml` runs it on each push to
`arena/**`.

Result: 56 of 60 runtime files compile. The four that do not:

| File | Why | What it takes |
|---|---|---|
| `runtime/platform/android.c` | Android's host layer (Bionic, `android/log.h`, `mprotect` tracking) | a Windows host layer (`VirtualAlloc`, `VirtualProtect`, write watch, file mapping) |
| `runtime/nvn/vulkan/android_driver.c` | loads the Vulkan driver through adrenotools | not needed on the desktop: it loads the Vulkan loader |
| `runtime/services/hwopus.c`, `runtime/hle/movie.c` | include FFmpeg's `libavcodec` | FFmpeg headers and libraries for Windows (or the `LARECOMP_NO_FFMPEG` path) |

## Still to do, in order

1. Windows host layer (`runtime/platform/windows.c`) for the functions `runtime/platform.h` declares.
2. Desktop frontend: SDL3 window, keyboard and controller input, audio, the Vulkan surface. The Vulkan loader is linked
   directly (no volk, no `VK_NO_PROTOTYPES`).
3. A CMake target for the desktop build. The game's code is not in this repository: it is generated from the user's own
   dump (`tools/gen.py`), and that generation and its compile must run on the user's PC.
4. Link and run: the title screen, a new game, and the prologue, compared with the phone.
