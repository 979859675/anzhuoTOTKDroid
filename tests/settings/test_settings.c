// Host test of runtime/settings_cheats_line.h: the cheats_on line of totkrecompandroid.ini. It must be written on its
// own line, and a file saved by ae200ac (the line glued to touch_layout's values) must still give its switches back.
// Each line is read the way settings_load reads one (sscanf, then the touch_layout branch).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "settings_cheats_line.h"

static int checks, failures;

#define CHECK(cond, ...)                                       \
    do {                                                       \
        checks++;                                              \
        if (!(cond)) {                                         \
            failures++;                                        \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
            fprintf(stderr, __VA_ARGS__);                      \
            fprintf(stderr, "\n");                             \
        }                                                      \
    } while (0)

// Reads a settings text the way settings_load reads it: returns the cheats_on value it ends with.
static void load_text(const char *text, char *cheats, size_t cap) {
    FILE *f = fmemopen((void *)text, strlen(text), "r");
    char line[1200];
    cheats[0] = 0;
    while (fgets(line, sizeof line, f)) {
        char key[64], val[1100] = "";
        if (sscanf(line, " %63[^= ] = %1099[^\r\n]", key, val) < 1) continue;
        val[strcspn(val, "\r\n")] = 0;
        if (!strcmp(key, "cheats_on")) snprintf(cheats, cap, "%s", val);
        else if (!strcmp(key, "touch_layout")) settings_cheats_cut(val, cheats, cap);
    }
    fclose(f);
}

// What settings_save writes around the two lines: touch_layout's values, then the cheats_on line, then the next key.
static void save_text(char *out, size_t cap, const char *cheats) {
    FILE *f = fmemopen(out, cap, "w");
    fprintf(f, "touch_layout = 0.1000,0.2000,0.3000,0.4000");
    settings_cheats_write(f, cheats);
    fprintf(f, "\ntouch_scheme = 0\n");
    fclose(f);
    out[cap - 1] = 0;
}

int main(void) {
    char buf[2048], cheats[1024];

    // 1. The line is written on its own line, even right after touch_layout's values.
    save_text(buf, sizeof buf, "84F3A2B1|1C2D3E4F");
    CHECK(strstr(buf, "0.4000\ncheats_on = 84F3A2B1|1C2D3E4F\n") != NULL, "cheats_on not on its own line: %s", buf);

    // 2. A file saved this way reads back its switches.
    load_text(buf, cheats, sizeof cheats);
    CHECK(!strcmp(cheats, "84F3A2B1|1C2D3E4F"), "round trip: got \"%s\"", cheats);

    // 3. No switches: the line is "cheats_on = " and reads back as empty.
    save_text(buf, sizeof buf, "");
    load_text(buf, cheats, sizeof cheats);
    CHECK(!strcmp(cheats, ""), "empty round trip: got \"%s\"", cheats);

    // 4. The ae200ac file: "touch_layout = ...cheats_on = KEYS" on one line, as the game wrote it.
    const char *glued =
        "touch_layout = 0.1000,0.2000,0.3000,0.4000cheats_on = 84F3A2B1|1C2D3E4F\n"
        "\ntouch_scheme = 0\n";
    load_text(glued, cheats, sizeof cheats);
    CHECK(!strcmp(cheats, "84F3A2B1|1C2D3E4F"), "glued line: got \"%s\"", cheats);

    // 5. The glued line with no switches.
    const char *glued_empty = "touch_layout = 0.1000,0.2000cheats_on = \n\ntouch_scheme = 0\n";
    load_text(glued_empty, cheats, sizeof cheats);
    CHECK(!strcmp(cheats, ""), "glued empty: got \"%s\"", cheats);

    // 6. The cut leaves the touch_layout value alone (its numbers parse as before).
    char val[1100] = "0.1000,0.2000cheats_on = 84F3";
    char keys[1024] = "unchanged";
    CHECK(settings_cheats_cut(val, keys, sizeof keys) == 1, "cut not reported");
    CHECK(!strcmp(val, "0.1000,0.2000") && !strcmp(keys, "84F3"), "cut: val \"%s\", keys \"%s\"", val, keys);

    // 7. Nothing glued: no cut, and the switches are left as they were.
    char val2[1100] = "0.1000,0.2000";
    char keys2[1024] = "kept";
    CHECK(settings_cheats_cut(val2, keys2, sizeof keys2) == 0, "cut reported with nothing glued");
    CHECK(!strcmp(val2, "0.1000,0.2000") && !strcmp(keys2, "kept"), "no-cut changed the values");

    printf("settings: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
