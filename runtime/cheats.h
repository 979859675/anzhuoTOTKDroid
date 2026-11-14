// Cheats of the mods' Atmosphere cheat files (cheats.c): mods/<mod>/cheats/<build id>.txt of each mod that is on
// (the Mods page: a cheats folder of its own, or atmosphere/contents/0100F2C0115B6000/cheats inside it). The data
// cheats run in the cheat VM (cheat_vm.h) every presented frame while they are on, switched on the Mods page. Cheats
// that write the game's code are listed but not run yet (docs/CHEATS.md).
#pragma once
#include <stdint.h>

#include "cheat_vm.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char name[160];   // as shown: UTF-8, the menu font's \u2116 (numero sign) read as "No"
    char mod[96];     // the mod's folder name
    char note[200];   // why it is not run (CHEAT_UNSUPPORTED), else ""
    CheatKind kind;
    int on;           // switched on (data cheats; always 1 for a master cheat)
} CheatInfo;

// The game is starting (after loader_load): reads the enabled mods' cheat files for this build and starts the VM.
// exefs: the game's exefs folder (main's NSO header gives the build id and the code's size).
void cheats_boot(const char *exefs);
// The Mods page's list: reads the files again while the game is not running (the cheats of a session are the ones it
// started with, as the mods are). Cheap: a few small files.
void cheats_refresh(void);
int cheats_count(void);
// Copies cheat i into out. 0 when there is none.
int cheats_get(int i, CheatInfo *out);
// Switches a data cheat on or off at once (the caller saves the settings: settings_save). 0 when it cannot be switched.
int cheats_set(int i, int on);
// Every presented frame (nvn/objects.c nvnQueuePresentTexture): the data cheats that are on, once the game is running.
void cheats_frame(void);

#ifdef __cplusplus
}
#endif
