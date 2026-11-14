// Atmosphere's cheat VM (dmnt's opcodes 0-A and C0-C3) and the cheat file's parser (cheats.c runs them in the game).
// Platform-free: the game's memory and the buttons come in through CheatMem and the keys argument, so this file can be
// tested on the host (tests/cheat_vm). Ported from valentderah/totk-pc-recomp's runtime/cheats.c (MIT), which follows
// stratosphere's dmnt (CheatVirtualMachine). The format is described in docs/CHEATS.md.
#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CHEAT_HEADING,      // no opcodes: a title or a group label
    CHEAT_DATA,         // writes data only: the VM runs it every presented frame while it is on
    CHEAT_MASTER,       // {name} with data writes: always on
    CHEAT_CODE,         // writes the game's instructions (main's code): not supported yet
    CHEAT_UNSUPPORTED,  // an opcode the VM does not run; the note says which
} CheatKind;

// The game's memory as the VM sees it. region(m): the base of region M (0 main, 1 heap, 2 alias, 3 aslr; 0 if none).
// read: 1 when the guest may read width bytes at a (the value into *v), else 0. write: stores width bytes at a (only
// called for addresses read() accepted; a write the game may not make is refused by the implementation).
typedef struct CheatMem {
    void *ctx;
    uint64_t (*region)(void *ctx, int m);
    int (*read)(void *ctx, uint64_t a, int width, uint64_t *v);
    void (*write)(void *ctx, uint64_t a, int width, uint64_t v);
} CheatMem;

// FNV-1a 32 of n bytes: a cheat's key (of "<mod folder>/<cheat name>") and the content hash of its words.
uint32_t cheat_fnv(const char *s, size_t n);

// The kind of a cheat's opcodes: CHEAT_HEADING when it has none (or only an end), CHEAT_CODE when a write goes to
// main's offset below code_limit (main's code, with its padding), CHEAT_UNSUPPORTED with note set when an opcode is
// not run, else CHEAT_DATA. note (notecap bytes) is written for CHEAT_UNSUPPORTED only.
CheatKind cheat_classify(const uint32_t *w, int n, uint64_t code_limit, char *note, size_t notecap);

// Runs one cheat's words once (every presented frame while it is on). keys: the buttons held (HidNpadButton bits).
// statics: the 256 static registers (C3), shared by all cheats and kept between runs, as dmnt keeps them.
void cheat_vm_run(const uint32_t *w, int n, const CheatMem *mem, uint64_t keys, uint64_t statics[256]);

// One cheat of a file, as parsed. words is malloc'd (the caller frees it, cheat_parse_free frees the array's).
typedef struct CheatRec {
    char *name;         // as written, UTF-8, trimmed (malloc'd)
    int master;         // written {name}
    int dup;            // 1 for the first cheat of its name in the file, 2 for the next one, ...
    uint32_t *words;
    int n;
} CheatRec;

// Parses an Atmosphere cheat file's text (modified in place). Returns the number of cheats; *out is malloc'd.
// Title lines (containing "TID=") are left out. 8-digit hex words after a [name] or {name} line are its opcodes.
int cheat_parse(char *text, CheatRec **out);
void cheat_parse_free(CheatRec *recs, int n);

#ifdef __cplusplus
}
#endif
