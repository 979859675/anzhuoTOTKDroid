# Licenses of TOTKDroid

- **TOTKDroid's own code is MIT** ([LICENSE](LICENSE)), as are LAS Recomp's, LASRecompAndroid's and
  BOTWRecompAndroid's, which it builds on.
- **Except one folder, GPL-3.0-or-later:** [`runtime/nvn/vulkan/lsfg/`](runtime/nvn/vulkan/lsfg/LICENSE.md) holds Lossless
  Scaling frame generation (LSFG). It is a C port of [Eden](https://git.eden-emu.dev/eden-emu/eden)'s, which is based on
  [lsfg-vk](https://github.com/PancakeTAS/lsfg-vk), taken from Swap2-Native.
  - Files: `lsfg.h`, `lsfg_chain.c`, `lsfg_dll.c`, `lsfg_pacer.c`, plus TOTKDroid's adapter `lsfg_host.h`. All carry
    `SPDX-License-Identifier: GPL-3.0-or-later`, with [COPYING](runtime/nvn/vulkan/lsfg/COPYING).
  - The glue that calls it, `runtime/nvn/vulkan/framegen.c`, is MIT.
- **An APK built with LSFG is GPL-3.0-or-later as a whole.** This is the default build (CMake `TOTK_LSFG`, Gradle
  property `totkLsfg`). It is a combined work of the MIT files and the GPL folder. Whoever passes such an APK on must
  offer its complete source: this repository at the commit of the release.
  - `./gradlew assembleRelease -PtotkLsfg=false` builds an APK without the folder, MIT as a whole. In that APK the setting
    says "not in this build".
- **Win-FG frame generation (MIT):** [`runtime/nvn/vulkan/winfg/`](runtime/nvn/vulkan/winfg/LICENSE.md) is the compute chain
  of [The412Banner/win-fg](https://github.com/The412Banner/win-fg) (commit `fbc69ed`), MIT, built into every APK. It has no
  proprietary files or weights. Its model-3 shaders (`of3_*`) adapt AMD's FidelityFX SDK FSR3 Optical Flow (MIT):
  [NOTICE_FIDELITYFX_OPTICALFLOW.md](runtime/nvn/vulkan/winfg/NOTICE_FIDELITYFX_OPTICALFLOW.md). Its glue is in
  `framegen.c`, MIT. Win-FG and LSFG are separate engines; the menu picks one.
- **Lossless Scaling's shaders are never included.** The app reads them at run time from the player's own copy of
  `Lossless.dll` (Lossless Scaling, proprietary, sold on Steam). The DLL is not part of this repository or of any APK.
  The app looks for it in `LSFG/`, `Download/`, `Documents/Emulation/bios/`, `Switch2/` and the root of the phone's
  storage, and in the app's data folder.
- **Ported from PC TotK Recomp (MIT):** the Atmosphère cheat VM and cheat-file parser, `runtime/cheat_vm.c` and
  `runtime/cheat_vm.h`, are ported from [valentderah/totk-pc-recomp](https://github.com/valentderah/totk-pc-recomp)
  (`runtime/cheats.c` there, MIT). Its MIT License notice: Copyright (c) 2026 Valiantsin Dzerakh (the full text is in that repository's LICENSE). `runtime/cheats.c`
  and `runtime/cheats.h` here are TOTKDroid's glue to the phone's memory, and are MIT as well. See [docs/CHEATS.md](docs/CHEATS.md).
- **Third-party code** keeps its own license, listed in [README.md](README.md#credits).
