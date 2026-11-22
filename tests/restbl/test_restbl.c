// Host tests of runtime/services/restbl.c (the game's resource size table): parse, pack, raise, grow, estimate, merge.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "restbl.h"
#include "zstd.h"

static int checks, failures;
#define CHECK(cond, ...)                                       \
    do {                                                       \
        checks++;                                              \
        if (!(cond)) {                                         \
            failures++;                                        \
            fprintf(stderr, "FAIL line %d: ", __LINE__);       \
            fprintf(stderr, __VA_ARGS__);                      \
            fprintf(stderr, "\n");                             \
        }                                                      \
    } while (0)

// A decompressed RESTBL with the given hashed entries (already sorted by hash) and no named ones.
static uint8_t *make_table(const uint32_t (*e)[2], uint32_t n, size_t *len) {
    *len = 22 + n * 8;
    uint8_t *d = calloc(1, *len);
    memcpy(d, "RESTBL", 6);
    d[6] = 1;                       // version 1
    d[10] = 160;                    // name length
    d[14] = (uint8_t)n;             // hashed count
    for (uint32_t i = 0; i < n; i++) {
        for (int b = 0; b < 4; b++) {
            d[22 + i * 8 + b] = (uint8_t)(e[i][0] >> 8 * b);
            d[26 + i * 8 + b] = (uint8_t)(e[i][1] >> 8 * b);
        }
    }
    return d;
}

static void test_key(void) {
    char buf[256];
    CHECK(!strcmp(restbl_key("Model/A.bfres.zs", buf, sizeof buf), "Model/A.bfres"), "key drops .zs");
    CHECK(!strcmp(restbl_key("Model/A.mc", buf, sizeof buf), "Model/A"), "key drops .mc");
    CHECK(!strcmp(restbl_key("Model/A.bfres", buf, sizeof buf), "Model/A.bfres"), "key keeps a plain name");
}

static void test_parse_raise_pack(void) {
    static const uint32_t e[2][2] = {{0x10, 1000}, {0x9000, 5000}};
    size_t n = 0;
    uint8_t *d = make_table(e, 2, &n);
    Restbl *t = restbl_parse(d, n);
    CHECK(t != NULL, "a valid table parses");
    CHECK(restbl_parse(d, n - 1) == NULL, "a truncated table is refused");
    free(d);
    uint32_t size = 0;
    CHECK(t && restbl_get(t, "nowhere/x", &size) == 0, "a missing key is not found");
    CHECK(t && restbl_raise(t, "nowhere/x", 4096) == 1, "a missing key is added");
    CHECK(t && restbl_get(t, "nowhere/x", &size) && size == 4096, "the added key has its size");
    CHECK(t && restbl_raise(t, "nowhere/x", 100) == 0, "raise never lowers");
    size_t zn = 0;
    uint8_t *z = t ? restbl_pack(t, &zn) : NULL;
    CHECK(z != NULL, "pack works");
    Restbl *u = z ? restbl_unpack(z, zn) : NULL;
    CHECK(u != NULL, "a packed table unpacks");
    CHECK(u && restbl_get(u, "nowhere/x", &size) && size == 4096, "the packed table keeps the raised entry");
    free(z);
    restbl_free(t);
    restbl_free(u);
}

static void test_grow_estimate(void) {
    uint32_t r = restbl_grow("Model/x.bfres", 1000, 100, 300, 0);   // factor 1.5: 1000 + 300, 32-byte aligned
    CHECK(r >= 1300 && r % 32 == 0, "bfres grows by 1.5 times the growth, aligned: got %u", r);
    r = restbl_grow("Other/x.bin", 1000, 100, 300, 0);                // unlisted type: 1.5 times, 32-byte aligned
    CHECK(r == 1312, "an unlisted type grows by 1.5 times the growth, aligned: got %u", r);
    CHECK(restbl_grow("Other/x.bin", 1000, 300, 100, 0) == 1000, "a smaller file keeps the entry");
    CHECK(restbl_grow("Sound/x.bwav", 1000, 100, 300, 0) == 1000, "a streamed type is never grown");
    uint32_t est = restbl_estimate("Model/x.bfres", 1000);
    CHECK(est >= 1500 && est % 4096 == 0, "an estimate covers the type's factor, 4 KiB aligned: got %u", est);
    CHECK(restbl_estimate("Sound/x.bwav", 1000) == 0, "a streamed type gets no estimate");
}

static void test_content_size(void) {
    static const char plain[] = "not zstd at all, read as it is";
    CHECK(restbl_content_size((const uint8_t *)plain, sizeof plain - 1, 1234) == 1234,
          "a file that is not zstd keeps its size");
    static const uint32_t e[1][2] = {{0x10, 1000}};
    size_t n = 0;
    uint8_t *d = make_table(e, 1, &n);
    Restbl *t = restbl_parse(d, n);
    size_t zn = 0;
    uint8_t *z = t ? restbl_pack(t, &zn) : NULL;
    CHECK(z != NULL, "a table packs into a frame");
    // the vendored zstd decodes it: the frame is valid, and its recorded size is the table's
    unsigned long long rec = z ? ZSTD_getFrameContentSize(z, zn) : 0;
    CHECK(rec == n, "the frame records the table's size: got %llu, want %zu", rec, n);
    CHECK(restbl_content_size(z, zn < 18 ? zn : 18, zn) == n, "restbl_content_size reads the recorded size");
    uint8_t *back = malloc(n);
    size_t got = z && back ? ZSTD_decompress(back, n, z, zn) : 0;
    CHECK(!ZSTD_isError(got) && got == n && memcmp(back, d, n) == 0, "the frame decodes to the table");
    free(back);
    free(z);
    restbl_free(t);
    free(d);
}

static void test_merge(void) {
    static const uint32_t b[2][2] = {{0x10, 1000}, {0x20, 3000}};
    static const uint32_t s[2][2] = {{0x10, 2000}, {0x20, 500}};
    size_t bn = 0, sn = 0;
    uint8_t *bd = make_table(b, 2, &bn), *sd = make_table(s, 2, &sn);
    Restbl *base = restbl_parse(bd, bn), *ship = restbl_parse(sd, sn);
    Restbl *out = restbl_parse(bd, bn);
    CHECK(base && ship && out, "the tables parse");
    int raised = base && ship && out ? restbl_merge(out, base, ship) : -1;
    uint32_t size = 0;
    CHECK(raised == 1, "one entry is larger in the shipped table: got %d", raised);
    CHECK(out && restbl_get(out, "k", &size) == 0, "a hash-only entry has no key name");
    CHECK(out && restbl_get(out, "nope", &size) == 0, "unknown keys stay unknown");
    free(bd);
    free(sd);
    restbl_free(base);
    restbl_free(ship);
    restbl_free(out);
}

int main(void) {
    test_key();
    test_parse_raise_pack();
    test_grow_estimate();
    test_content_size();
    test_merge();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
