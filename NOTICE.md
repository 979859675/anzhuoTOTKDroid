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
- **Ported from PC TotK Recomp (MIT):** the Atmosphère cheat VM and cheat-file parser, `runtime/cheat_vm.c` and
  `runtime/cheat_vm.h`, are ported from [valentderah/totk-pc-recomp](https://github.com/valentderah/totk-pc-recomp)
  (`runtime/cheats.c` there, MIT). Its MIT License notice: Copyright (c) 2026 Valiantsin Dzerakh (the full text is in that repository's LICENSE). `runtime/cheats.c`
  and `runtime/cheats.h` here are TOTKDroid's glue to the phone's memory, and are MIT as well. See [docs/CHEATS.md](docs/CHEATS.md).
- **Ported from PC TotK Recomp (MIT):** the game's resource size table (RESTBL), `runtime/services/restbl.c` and
  `runtime/services/restbl.h`, with its host test `tests/restbl/`, are ported from
  [valentderah/totk-pc-recomp](https://github.com/valentderah/totk-pc-recomp) (`runtime/restbl.c` and `runtime/restbl.h`
  there, MIT). Same MIT License notice as above. The table's merge into `runtime/services/fs.c` is TOTKDroid's own.
- **Ported from PC TotK Recomp (MIT):** the ExeFS patch formats (IPS, IPS32 and .pchtxt of the mods' exefs folders, and
  the build id rule), `runtime/exefs_patch.c` and `runtime/exefs_patch.h`, with its host test `tests/exefs/`, are ported
  from [valentderah/totk-pc-recomp](https://github.com/valentderah/totk-pc-recomp) (`tools/exefs_patches.py` there, MIT).
  Same MIT License notice as above. Their use in `runtime/cheats.c` (the writes into the game's code at boot) is TOTKDroid's own.
- **Third-party code** keeps its own license, listed in [README.md](README.md#credits).
- **Ideas and layouts from [totk-android](https://github.com/AurelioB/totk-android)** (AurelioB, MIT): the HD rumble
  mixing in `runtime/hle/input.c` (two bands to one strength, the silence threshold, the 250 ms stale value) follows its
  `runtime/rumble_mix.h` and `runtime/hid_feedback.c`. The 3D reverb and the auxiliary buffer in
  `runtime/services/audio_reverb3d.inc` are adapted from its `games/totk/runtime/audio_reverb.c` and
  `audio_mixer.c` (Copyright (c) 2026 MacNativeGames contributors, MIT), whose reverb network is based on
  Ryujinx.Audio's Reverb3d (Copyright (c) Ryujinx Team and Contributors, MIT). The software keyboard in
  `runtime/hle/swkbd.c` (the argument layout, the UTF-16 / UTF-8 answer, the disabled keys and the cancel result)
  follows its `games/totk/runtime/swkbd_host.c` (Copyright (c) 2026 MacNativeGames contributors, MIT). The optional
  rendering reductions in `runtime/native.c` (shadow map size, the game's FXAA, the render far plane) use the sites
  its `games/totk/runtime/game_patches_v143.c` found (same copyright, MIT).
- **From [Savage Wind Recomp](https://github.com/Revivedx/SavageWindRecomp)** (MIT, Copyright (c) 2026 Revivedx and the
  Savage Wind Recomp contributors; Copyright (c) 2026 Peiikai), its `mali-fixes` branch (StonedEdge): the low bits of
  16 / 8-bit integer colour targets on Mali (`rt_kind_key` in `runtime/nvn/vulkan/exec.c`, the narrow outputs in
  `runtime/nvn/maxwell/translate.c`; its 9950b9e, through BOTWRecompAndroid 86ac04d), the pass barriers held to
  fragment work (`frag_barrier_on`, its 8defa9b), the colour targets a pass doesn't draw into left out of it
  (`det_tab`, its 087f2c5), and the GPU's load and clock through devfreq in `runtime/ui/perf_overlay.mm` (its 9dadb24).
