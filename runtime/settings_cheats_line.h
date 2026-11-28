// The cheats_on line of totkrecompandroid.ini (settings.c). Platform-free: tested on the host (tests/settings).
//
// The line must start on its own line. The 0.4.2 build of ae200ac wrote it straight after touch_layout's values, with no
// newline, so the file had "touch_layout = ...cheats_on = KEYS": the load read the whole line as touch_layout and the
// switches were lost at each start. settings_cheats_cut takes that glued form apart, so a file saved that way keeps its
// switches.
#pragma once
#include <stdio.h>
#include <string.h>

// The line, always after a newline.
static inline void settings_cheats_write(FILE *f, const char *cheats) {
    fprintf(f, "\ncheats_on = %s\n", cheats);
}

// A value (of touch_layout) with "cheats_on = " glued to its end: the value is cut before it, and the keys after it go
// to cheats (cap bytes). 1 when it was cut, 0 when there was nothing glued (cheats is left as it was).
static inline int settings_cheats_cut(char *val, char *cheats, size_t cap) {
    char *g = strstr(val, "cheats_on = ");
    if (!g) return 0;
    snprintf(cheats, cap, "%s", g + 12);   // (12 = strlen("cheats_on = "))
    *g = 0;
    return 1;
}
