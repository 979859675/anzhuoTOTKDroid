// ExeFS patches of the mods (cheats.c applies them to main): an IPS or IPS32 file named <build id>.ips, or a .pchtxt
// (IPSwitch) with its @nsobid line, in a mod's exefs folder (or atmosphere/exefs_patches/<name>/). Their offsets are as
// emulators read them: an IPS offset counts the NSO header (0x100 bytes) before main's image; a pchtxt offset adds its
// @flag offset_shift first, then the header is taken off. The result is an offset in main's image, as the cheats use.
// Ported from valentderah/totk-pc-recomp's tools/exefs_patches.py (MIT): the formats and the build id rule. Platform-free:
// tested on the host (tests/exefs).
#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EXEFS_NSO_HEADER 0x100

// One run of bytes written into main's image at off.
typedef struct {
    uint64_t off;       // from main's base (the NSO header already taken off)
    uint32_t len;
    uint8_t *bytes;     // malloc'd
} ExefsRun;

// The writes of one file, in file order (a later run over an earlier one wins).
typedef struct {
    ExefsRun *runs;
    int n, cap;
} ExefsWrites;

void exefs_writes_free(ExefsWrites *w);

// Whether name (hex digits, any case, of 1 to 64 digits; trailing zeros may be left out) is main's build id (bid, hex of
// its 32 bytes, 64 digits). Atmosphere and the emulators match names this way.
int exefs_bid_match(const char *name, const char *bid);

// An IPS (\"PATCH\", 3-byte offsets, EOF) or IPS32 (\"IPS32\", 4-byte offsets, EEOF) file. 0 when it is read whole; -1 with
// err (errcap bytes) when it is neither, cut short, or writes into the NSO header.
int exefs_parse_ips(const uint8_t *d, size_t n, ExefsWrites *out, char *err, size_t errcap);

// A .pchtxt file (text, UTF-8, a BOM allowed). bid gets its @nsobid value (bidcap bytes; "" when it has none). The
// lines between @enabled and @disabled are the writes (none before @enabled); @stop ends the file. A write is an
// offset (hex) and hex bytes, in byte order. A string value, or a value that is not hex, is an error (-1, err). 0
// when the file is read whole.
int exefs_parse_pchtxt(const char *text, char *bid, size_t bidcap, ExefsWrites *out, char *err, size_t errcap);

#ifdef __cplusplus
}
#endif
