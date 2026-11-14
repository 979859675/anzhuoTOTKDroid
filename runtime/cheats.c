// Cheats of the mods' Atmosphere cheat files (cheats.h). Each mod that is on may hold <build id>.txt in its cheats
// folder (mods.c mods_cheats_dir), the build id the first 8 bytes of main's NSO build id, as Atmosphere and the
// emulators name it; the file name's case does not matter. A cheat's switch is in the settings (cheats_on: the keys of
// the cheats that are on, FNV-1a 32 of "<mod folder name>/<cheat name>", as the PC port keys them), so two mods' cheats
// of one name keep their own switches.
//
// The game runs its own ARM64 code here (runtime/native.c), in this process at its own addresses, so a cheat's
// memory is ordinary memory: the VM (cheat_vm.c) reads and writes it through guest_ok, which asks the kernel's memory
// regions (kernel_region_of) for the permissions. Writes go through plat_write_prepare, which keeps the GPU copies of
// the page up to date (the write tracking). Main's code is never written here: cheats that change it are listed as
// CHEAT_CODE and not run yet.
//
// The VM runs in the present path (nvn/objects.c nvnQueuePresentTexture -> cheats_frame), every presented frame while
// the cheat is on, after the game's first 60 frames (as the PC port). The list is read at boot and, while the game is
// not running, again for the Mods page; while it runs, a cheat's switch changes at once but the mods do not (they apply
// at the next start, as the Mods page says).
#include "cheats.h"

#ifdef __ANDROID__
#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "hle.h"
#include "kernel.h"
#include "mods.h"
#include "platform.h"
#include "rt.h"
#include "settings.h"

// the Switch's buttons held now (hle/input.c; 0 while the menu is open)
uint64_t input_buttons_now(void);

typedef struct {
    CheatInfo info;
    char id[9];          // its settings key: 8 hex digits
    uint32_t *words;     // the opcodes of a data or master cheat (the others keep none)
    int n;
} Cheat;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;   // g_list, g_count, g_vm: the present thread runs the VM
static Cheat *g_list;
static int g_count;
static volatile int g_running;        // the game runs (cheats_boot .. the in-place reboot)
static volatile int g_vm;             // a data or master cheat is on
static uint64_t g_frames;             // presented frames since the boot (the VM waits for the game to start)
static uint64_t g_statics[256];       // C3's static registers, shared by the cheats (dmnt keeps them too)
static char g_exefs[1024];            // the exefs folder the game booted from
static uint64_t g_code_limit;         // main's code: the offsets below it (its text segment, page-aligned)

// ---- the settings' list of switched cheats (cheats_on: keys separated by '|')

static int list_has(const char *list, const char *key) {
    size_t n = strlen(key);
    for (const char *s = list; *s;) {
        const char *e = strchr(s, '|');
        size_t l = e ? (size_t)(e - s) : strlen(s);
        if (l == n && !strncmp(s, key, n)) return 1;
        if (!e) break;
        s = e + 1;
    }
    return 0;
}

// key on or off in the list (the others as they were)
static void list_set(char *list, size_t cap, const char *key, int on) {
    char out[1024] = "";
    size_t k = 0;
    for (const char *s = list; *s;) {
        const char *e = strchr(s, '|');
        size_t l = e ? (size_t)(e - s) : strlen(s);
        if (l && !(l == strlen(key) && !strncmp(s, key, l)) && k + l + 2 < sizeof out) {
            k += (size_t)snprintf(out + k, sizeof out - k, "%s%.*s", k ? "|" : "", (int)l, s);
        }
        if (!e) break;
        s = e + 1;
    }
    if (on && k + strlen(key) + 2 < sizeof out) snprintf(out + k, sizeof out - k, "%s%s", k ? "|" : "", key);
    snprintf(list, cap, "%s", out);
}

// ---- the files

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

// main's NSO header: the build id (bytes 0x40-0x47) and the text segment's end (its memory offset and size)
static int read_main_header(const char *exefs, uint8_t head[0x48]) {
    char path[1200];
    snprintf(path, sizeof path, "%s/main", exefs);
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    size_t n = fread(head, 1, 0x48, f);
    fclose(f);
    return n == 0x48 && !memcmp(head, "NSO0", 4);
}

// the file named want in dir, its name in any case, into out; 0 when there is none
static int find_file(const char *dir, const char *want, char *out, size_t cap) {
    DIR *d = opendir(dir);
    if (!d) return 0;
    int found = 0;
    for (struct dirent *e; !found && (e = readdir(d));)
        if (e->d_name[0] != '.' && !strcasecmp(e->d_name, want)) found = snprintf(out, cap, "%s/%s", dir, e->d_name) < (int)cap;
    closedir(d);
    return found;
}

// the whole file as text (malloc'd, NUL-terminated; a UTF-8 BOM dropped); NULL when it can't be read
static char *read_text(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    size_t cap = 4096, n = 0;
    char *s = (char *)malloc(cap);
    while (s && (n += fread(s + n, 1, cap - n - 1, f)) == cap - 1) {
        cap *= 2;
        char *g = (char *)realloc(s, cap);
        if (!g) { free(s); s = NULL; break; }
        s = g;
    }
    fclose(f);
    if (!s) return NULL;
    s[n] = 0;
    if (n >= 3 && !memcmp(s, "\xEF\xBB\xBF", 3)) memmove(s, s + 3, n - 2);
    return s;
}

// The menu's font has no numero sign (U+2116), which cheat names often start with: they show "No" instead.
static void plain_name(char *s) {
    for (char *p; (p = strstr(s, "\xE2\x84\x96")) != NULL;) {
        p[0] = 'N', p[1] = 'o';
        memmove(p + 2, p + 3, strlen(p + 3) + 1);
    }
}

// Reads the enabled mods' cheats for this build into a new list, then replaces the running one.
static void scan(const char *exefs) {
    uint8_t head[0x48];
    Cheat *list = NULL;
    int n = 0, cap = 0;
    uint64_t code_limit = 0;
    char file[24] = "";
    if (!read_main_header(exefs, head)) {
        LOG("[cheats] no main module in %s: no cheats\n", exefs);
    } else {
        char bid[17];
        for (int i = 0; i < 8; i++) snprintf(bid + 2 * i, 3, "%02X", head[0x40 + i]);
        snprintf(file, sizeof file, "%s.txt", bid);
        code_limit = ((uint64_t)le32(head + 0x14) + le32(head + 0x18) + 0xFFF) & ~0xFFFull;
        ModInfo mods[MODS_MAX];
        int nm = mods_list(mods, MODS_MAX), files = 0;
        for (int k = 0; k < nm; k++) {
            if (!mods[k].on) continue;
            char dir[1200], path[1400];
            if (!mods_cheats_dir(mods[k].name, dir, sizeof dir) || !find_file(dir, file, path, sizeof path)) continue;
            char *text = read_text(path);
            if (!text) {
                LOG("[cheats] %s: cannot read %s\n", mods[k].name, path);
                continue;
            }
            CheatRec *recs = NULL;
            int nr = cheat_parse(text, &recs);
            free(text);
            files++;
            for (int j = 0; j < nr; j++) {
                CheatRec *r = &recs[j];
                if (n == cap) {
                    int ncap = cap ? cap * 2 : 64;
                    Cheat *grown = (Cheat *)realloc(list, sizeof *list * (size_t)ncap);
                    if (!grown) break;
                    list = grown, cap = ncap;
                }
                Cheat *c = &list[n];
                memset(c, 0, sizeof *c);
                snprintf(c->info.mod, sizeof c->info.mod, "%s", mods[k].name);
                char named[300];
                if (r->dup > 1) snprintf(named, sizeof named, "%s/%s#%d", mods[k].name, r->name, r->dup);
                else snprintf(named, sizeof named, "%s/%s", mods[k].name, r->name);
                snprintf(c->id, sizeof c->id, "%08X", cheat_fnv(named, strlen(named)));
                snprintf(c->info.name, sizeof c->info.name, "%s", r->name);
                plain_name(c->info.name);
                CheatKind kind = cheat_classify(r->words, r->n, code_limit, c->info.note, sizeof c->info.note);
                if (r->master && kind == CHEAT_DATA) kind = CHEAT_MASTER;
                c->info.kind = kind;
                c->info.on = kind == CHEAT_MASTER || (kind == CHEAT_DATA && list_has(g_settings.cheats_on, c->id));
                if (kind == CHEAT_DATA || kind == CHEAT_MASTER) {
                    c->words = r->words;   // (moved: cheat_parse_free leaves them out)
                    c->n = r->n;
                    r->words = NULL;
                }
                n++;
            }
            cheat_parse_free(recs, nr);
        }
        LOG("[cheats] %s in %d mods (%d cheats)\n", file, files, n);
    }
    int vm = 0;
    for (int i = 0; i < n; i++) vm |= list[i].info.on && (list[i].info.kind == CHEAT_DATA || list[i].info.kind == CHEAT_MASTER);
    pthread_mutex_lock(&g_lock);
    Cheat *old = g_list;
    int old_n = g_count;
    g_list = list;
    g_count = n;
    g_vm = vm;
    g_code_limit = code_limit;
    pthread_mutex_unlock(&g_lock);
    for (int i = 0; i < old_n; i++) free(old[i].words);
    free(old);
}

// ---- guest memory: main's image, and the guest's regions the kernel knows

static uint64_t main_base(void) { return g_nmodules > 0 ? g_modules[0].base : 0; }

static int in_main(uint64_t a, uint64_t n) {
    uint64_t b = main_base();
    return b && a >= b && a + n <= b + g_modules[0].size;
}

// n bytes at a touch main's code (its text segment with the padding up to the data)?
static int in_code(uint64_t a, uint64_t n) {
    uint64_t b = main_base();
    return b && a < b + g_code_limit && a + n > b;
}

// need: 1 read, 2 write. Main's image is read and written as the game's own memory (the loader maps it so).
static int guest_ok(uint64_t a, uint64_t n, uint32_t need) {
    if (in_main(a, n)) return 1;
    uint64_t base, size;
    uint32_t perm;
    if (!kernel_region_of(a, &base, &size, &perm)) return 0;
    return (perm & need) == need && a + n <= base + size;
}

static uint64_t mem_region(void *ctx, int m) {
    (void)ctx;
    switch (m) {
    case 0: return main_base();
    case 1: return HEAP_BASE;
    case 2: return ALIAS_BASE;
    default: return 0;   // (no ASLR region in this runtime)
    }
}

static int mem_read(void *ctx, uint64_t a, int width, uint64_t *v) {
    (void)ctx;
    if (!a || !guest_ok(a, (uint64_t)width, 1)) return 0;
    uint64_t x = 0;
    memcpy(&x, (const void *)(uintptr_t)a, (size_t)width);
    *v = x;
    return 1;
}

static void mem_write(void *ctx, uint64_t a, int width, uint64_t v) {
    (void)ctx;
    if (in_code(a, (uint64_t)width)) {   // not run: the code is the game's own (cheats that change it are CHEAT_CODE)
        static int warned;
        if (!warned++) LOG("[cheats] a data cheat writes the game's code at %llx: skipped\n", (unsigned long long)a);
        return;
    }
    if (!guest_ok(a, (uint64_t)width, 2)) return;
    plat_write_prepare(a, (uint64_t)width);
    memcpy((void *)(uintptr_t)a, &v, (size_t)width);
}

static const CheatMem g_mem = {NULL, mem_region, mem_read, mem_write};

// ---- the interface

// the Mods page's list (the game not running) reads the default folder: the data root's, where the game is set up
static const char *default_exefs(void) { return "extracted/exefs"; }

void cheats_boot(const char *exefs) {
    snprintf(g_exefs, sizeof g_exefs, "%s", exefs);
    memset(g_statics, 0, sizeof g_statics);
    g_frames = 0;
    scan(g_exefs);
    g_running = 1;
}

void cheats_refresh(void) {
    if (g_running) return;
    scan(g_exefs[0] ? g_exefs : default_exefs());
}

int cheats_count(void) {
    pthread_mutex_lock(&g_lock);
    int n = g_count;
    pthread_mutex_unlock(&g_lock);
    return n;
}

int cheats_get(int i, CheatInfo *out) {
    pthread_mutex_lock(&g_lock);
    int ok = i >= 0 && i < g_count;
    if (ok) *out = g_list[i].info;
    pthread_mutex_unlock(&g_lock);
    return ok;
}

int cheats_set(int i, int on) {
    pthread_mutex_lock(&g_lock);
    int ok = i >= 0 && i < g_count && g_list[i].info.kind == CHEAT_DATA;
    if (ok) {
        Cheat *c = &g_list[i];
        c->info.on = on != 0;
        list_set(g_settings.cheats_on, sizeof g_settings.cheats_on, c->id, on != 0);
        int vm = 0;
        for (int k = 0; k < g_count; k++)
            vm |= g_list[k].info.on && (g_list[k].info.kind == CHEAT_DATA || g_list[k].info.kind == CHEAT_MASTER);
        g_vm = vm;
        LOG("[cheats] %s/%s %s\n", c->info.mod, c->info.name, on ? "on" : "off");
    }
    pthread_mutex_unlock(&g_lock);
    return ok;
}

void cheats_frame(void) {
    if (!g_running || !g_vm) return;
    if (++g_frames < 60) return;   // the game polls its input and starts before this (as the PC port)
    uint64_t keys = input_buttons_now();
    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < g_count; i++) {
        Cheat *c = &g_list[i];
        if (c->info.on && (c->info.kind == CHEAT_DATA || c->info.kind == CHEAT_MASTER))
            cheat_vm_run(c->words, c->n, &g_mem, keys, g_statics);
    }
    pthread_mutex_unlock(&g_lock);
}

// the in-place reboot (main.c): the game's memory goes; its cheats stop with it, and the next boot reads them again
RT_ON_RESET(cheats_reset) {
    g_running = 0;
    g_vm = 0;
}

#else   // (the runtime is Android's; elsewhere there are no cheats)
void cheats_boot(const char *exefs) { (void)exefs; }
void cheats_refresh(void) {}
int cheats_count(void) { return 0; }
int cheats_get(int i, CheatInfo *out) { (void)i, (void)out; return 0; }
int cheats_set(int i, int on) { (void)i, (void)on; return 0; }
void cheats_frame(void) {}
#endif
