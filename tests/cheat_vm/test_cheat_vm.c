// Host tests for runtime/cheat_vm.c: the parser, the classifier and the VM on a fake memory.
// Build and run: tests/cheat_vm/run.sh
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cheat_vm.h"

static int failures, checks;
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

// ---- fake memory: main at MAIN and heap at HEAP, SIZE bytes each; everything else is unmapped
#define MAIN 0x100000ull
#define HEAP 0x200000ull
#define SIZE 0x10000ull
static uint8_t mem[2 * SIZE];

static uint8_t *at(uint64_t a) { return a >= HEAP ? mem + SIZE + (a - HEAP) : mem + (a - MAIN); }
static int mapped(uint64_t a, int w) {
    return (a >= MAIN && a + (uint64_t)w <= MAIN + SIZE) || (a >= HEAP && a + (uint64_t)w <= HEAP + SIZE);
}

static uint64_t fake_region(void *ctx, int m) {
    (void)ctx;
    return m == 0 ? MAIN : m == 1 ? HEAP : 0;
}
static int fake_read(void *ctx, uint64_t a, int w, uint64_t *v) {
    (void)ctx;
    if (!mapped(a, w)) return 0;
    uint64_t x = 0;
    memcpy(&x, at(a), (size_t)w);
    *v = x;
    return 1;
}
static void fake_write(void *ctx, uint64_t a, int w, uint64_t v) {
    (void)ctx;
    memcpy(at(a), &v, (size_t)w);
}
static const CheatMem fake = {NULL, fake_region, fake_read, fake_write};

static uint64_t peek(uint64_t a, int w) {
    uint64_t v = 0;
    fake_read(NULL, a, w, &v);
    return v;
}
static void reset_mem(void) { memset(mem, 0, sizeof mem); }

// ---- parser

static void test_parse(void) {
    char text[] =
        "[The Legend of Zelda: Tears of the Kingdom v1.4.3 TID=0100F2C0115B6000 BID=277178B7DBA1B6D4]\r\n"
        "\r\n"
        "[Player]\n"
        "[A code cheat]\n"
        "040A0000 XXXXXXXX YYYYYYYY\n"
        "[A data cheat (Hold ZL+X)]\n"
        "80000104\n"
        "580F0000 00000010\n"
        "[Player]\n"
        "{Master}\n"
        "20000000\n";
    CheatRec *recs = NULL;
    int n = cheat_parse(text, &recs);
    CHECK(n == 5, "5 cheats (the title line is not one), got %d", n);
    if (n != 5) return;
    CHECK(!strcmp(recs[0].name, "Player") && recs[0].n == 0, "heading Player has no words");
    CHECK(!strcmp(recs[1].name, "A code cheat") && recs[1].n == 1 && recs[1].words[0] == 0x040A0000u,
          "code cheat: only the 8-digit word 040A0000 (XXXXXXXX is not one), got %d", recs[1].n);
    CHECK(!strcmp(recs[2].name, "A data cheat (Hold ZL+X)"), "name keeps its spaces and brackets-free text");
    CHECK(recs[2].n == 3 && recs[2].words[0] == 0x80000104u && recs[2].words[2] == 0x00000010u,
          "data cheat words, got %d", recs[2].n);
    CHECK(recs[0].dup == 1 && recs[3].dup == 2, "a repeated name is numbered (dup 2)");
    CHECK(recs[4].master == 1 && !strcmp(recs[4].name, "Master") && recs[4].n == 1, "master cheat");
    cheat_parse_free(recs, n);
}

static void test_parse_title_bom(void) {
    // the file as the game reads it: the loader strips the BOM first; the title line then ends the list of nothing
    char text[] = "[The Legend TID=0100F2C0115B6000]\n20000000\n[Speed]\n20000000\n";
    CheatRec *recs = NULL;
    int n = cheat_parse(text, &recs);
    CHECK(n == 1 && !strcmp(recs[0].name, "Speed"), "title line not a cheat, got %d", n);
    cheat_parse_free(recs, n);
}

static void test_parse_repeats(void) {
    char text[] = "[Speed]\n20000000\n[Speed]\n20000000\n[Speed]\n20000000\n";
    CheatRec *recs = NULL;
    int n = cheat_parse(text, &recs);
    CHECK(n == 3, "3 cheats, got %d", n);
    if (n == 3) CHECK(recs[0].dup == 1 && recs[1].dup == 2 && recs[2].dup == 3, "dup numbering");
    cheat_parse_free(recs, n);
}

// ---- classifier

static void test_classify(void) {
    char note[160] = "";
    const uint64_t limit = 0x1000;   // main's code: offsets below this
    uint32_t store_code[] = {0x04000000, 0x00000100, 0x12345678};          // main + 0x100: code
    uint32_t store_main_data[] = {0x04000000, 0x00002000, 0x12345678};     // main + 0x2000: data (past the code)
    uint32_t store_heap[] = {0x04000000 | (1u << 20), 0x00000100, 0x1};    // heap: data
    uint32_t end_only[] = {0x20000000};
    uint32_t unsupported[] = {0xC4000000, 0x00000000};                     // C4: not run
    CHECK(cheat_classify(store_code, 3, limit, note, sizeof note) == CHEAT_CODE, "store into code is CODE");
    // the register field (R, the 4th nibble of 0TMR00AA): a cheat that writes no registers keeps it 0, so the write
    // is still at main + offset (the cheats' 040A0000 stores: "Inf. Battery" and the rest of the code cheats)
    uint32_t store_code_r[] = {0x040A0000, 0x00000100, 0x12345678};
    CHECK(cheat_classify(store_code_r, 3, limit, note, sizeof note) == CHEAT_CODE, "a code store with register field A is CODE");
    CHECK(cheat_code_stores(store_code_r, 3, limit, NULL, 0) == 1, "and it is one code store");
    CHECK(cheat_classify(store_main_data, 3, limit, note, sizeof note) == CHEAT_DATA, "store into main data is DATA");
    CHECK(cheat_classify(store_heap, 3, limit, note, sizeof note) == CHEAT_DATA, "store into heap is DATA");
    CHECK(cheat_classify(end_only, 1, limit, note, sizeof note) == CHEAT_HEADING, "an end alone is a heading");
    CHECK(cheat_classify(unsupported, 2, limit, note, sizeof note) == CHEAT_UNSUPPORTED, "C4 is unsupported");
    CHECK(strstr(note, "not supported") != NULL, "the note says why, got '%s'", note);
    uint32_t cut[] = {0x04000000, 0x00000100};   // a store cut short
    CHECK(cheat_classify(cut, 2, limit, note, sizeof note) == CHEAT_UNSUPPORTED, "cut short is unsupported");

    // code writes: whole instructions only, with stores and one button condition around them
    uint32_t code_w8[] = {0x08000000, 0x00000100, 0x11111111, 0x22222222};   // 8 bytes into code: two instructions
    CHECK(cheat_classify(code_w8, 4, limit, note, sizeof note) == CHEAT_CODE, "an 8-byte code store is CODE");
    uint32_t code_w2[] = {0x02000000, 0x00000100, 0x1234};                  // 2 bytes: not an instruction
    CHECK(cheat_classify(code_w2, 3, limit, note, sizeof note) == CHEAT_UNSUPPORTED, "a 2-byte code store is unsupported");
    CHECK(strstr(note, "whole instruction") != NULL, "the note says why, got '%s'", note);
    uint32_t code_odd[] = {0x04000000, 0x00000102, 0x1};                    // not 4-aligned
    CHECK(cheat_classify(code_odd, 3, limit, note, sizeof note) == CHEAT_UNSUPPORTED, "an unaligned code store is unsupported");
    uint32_t code_mixed[] = {0x04000000, 0x00000100, 0x1, 0x14000000, 0x00000200, 0x2};   // plus a conditional
    CHECK(cheat_classify(code_mixed, 6, limit, note, sizeof note) == CHEAT_UNSUPPORTED, "code writes with another opcode are unsupported");
    CHECK(strstr(note, "other opcodes") != NULL, "the note says why, got '%s'", note);
    uint32_t code_key[] = {0x80000002, 0x04000000, 0x00000100, 0x1, 0x21000000, 0x04000000, 0x00000100, 0x2, 0x20000000};
    CHECK(cheat_classify(code_key, 9, limit, note, sizeof note) == CHEAT_CODE, "a button condition around code writes is CODE");
    CHECK(cheat_classify(store_main_data, 3, limit, note, sizeof note) == CHEAT_DATA, "a data store is still DATA");

    CheatStore st[4];
    CHECK(cheat_code_stores(code_key, 9, limit, st, 4) == 2, "two code stores in the key cheat");
    CHECK(st[0].off == 0x100 && st[0].width == 4 && st[0].value == 0x1, "the first store: offset, width, value");
    CHECK(st[1].off == 0x100 && st[1].value == 0x2, "the else store");
    CHECK(cheat_code_stores(code_w8, 4, limit, st, 4) == 1 && st[0].width == 8 && st[0].value == 0x1111111122222222ull,
          "an 8-byte store's value is its two words");
    CHECK(cheat_code_stores(store_main_data, 3, limit, st, 4) == 0, "a data store is not a code store");
}

// ---- VM

static void test_store(void) {
    reset_mem();
    uint64_t statics[256] = {0};
    // 0TMR00AA AAAAAAAA V: width 4, region 1 (heap), A = 0x40
    uint32_t w[] = {0x04000000 | (1u << 20), 0x40, 0xCAFEBABE};
    cheat_vm_run(w, 3, &fake, 0, statics);
    CHECK(peek(HEAP + 0x40, 4) == 0xCAFEBABEu, "store into heap");
    CHECK(peek(HEAP + 0x44, 4) == 0, "only the stored bytes change");
}

static void test_button_block(void) {
    uint64_t statics[256] = {0};
    // 8<mask>: the block runs while every button of the mask is held; 20000000 ends it
    uint32_t cond[] = {0x80000101, 0x04000000 | (1u << 20), 0x44, 0x11111111, 0x20000000};   // mask A|ZL
    reset_mem();
    cheat_vm_run(cond, 5, &fake, 0x101, statics);
    CHECK(peek(HEAP + 0x44, 4) == 0x11111111, "all held: the block runs");
    reset_mem();
    cheat_vm_run(cond, 5, &fake, 0x001, statics);
    CHECK(peek(HEAP + 0x44, 4) == 0, "only A held: the block is skipped");
    reset_mem();
    cheat_vm_run(cond, 5, &fake, 0x1101, statics);   // A, ZL and another button
    CHECK(peek(HEAP + 0x44, 4) == 0x11111111, "more buttons than the mask: still runs");
}

static void test_loop_and_statics(void) {
    reset_mem();
    uint64_t statics[256] = {0};
    // loop 3 times: r1 += 1 (7T0RC000 V, T = 8, R = 1, C = 0 add), then static[0x80] = r1 (C3)
    uint32_t w[] = {
        0x30000000, 3,       // loop start: r0 = 3 (300R0000 V)
        0x78010000, 1,       // r1 = r1 + 1 (width 8)
        0xC3000801,          // static 0x80 = r1
        0x31000000,          // loop end (310R0000): r0 -= 1, back while r0 != 0
    };
    cheat_vm_run(w, 7, &fake, 0, statics);
    CHECK(statics[0x80] == 3, "loop ran 3 times, static = %llu", (unsigned long long)statics[0x80]);
}

static void test_unmapped(void) {
    reset_mem();
    uint64_t statics[256] = {0};
    // region 2 (alias) is unmapped here: the store does nothing, the run goes on
    uint32_t far[] = {0x04000000 | (2u << 20), 0x00000000, 0x77777777, 0x04000000, 0x00000008, 0x55555555};
    cheat_vm_run(far, 6, &fake, 0, statics);
    CHECK(peek(MAIN, 4) == 0, "unmapped store does nothing");
    CHECK(peek(MAIN + 8, 4) == 0x55555555u, "the next opcode still runs");
}

// 5T..: r0 = [heap + 0] (a pointer). 6T0RIor0 V: store V at r0 (width 4). The pointer lands in the heap.
static void test_pointer_chain(void) {
    reset_mem();
    uint64_t statics[256] = {0};
    uint64_t target = HEAP + 0x80;
    memcpy(at(HEAP), &target, 8);
    uint32_t w[] = {
        0x58100000, 0x00000000,       // r0 = [region 1 + 0], 8 bytes (5, T = 8, M = 1, R = 0)
        0x64000000, 0x00000000, 0x5A5A5A5A,   // [r0] = 0x5A5A5A5A, 4 bytes (6, T = 4, R = 0)
    };
    cheat_vm_run(w, 5, &fake, 0, statics);
    CHECK(peek(HEAP + 0x80, 4) == 0x5A5A5A5Au, "the chain stored through the pointer");
}

// A store that changes nothing is not written (its own counter reads the same value).
static void test_unchanged_store(void) {
    reset_mem();
    uint64_t statics[256] = {0};
    uint32_t w[] = {0x04000000 | (1u << 20), 0x10, 0x00000000};
    cheat_vm_run(w, 3, &fake, 0, statics);
    CHECK(peek(HEAP + 0x10, 4) == 0, "zero stored over zero");
}

int main(void) {
    test_parse();
    test_parse_title_bom();
    test_parse_repeats();
    test_classify();
    test_store();
    test_button_block();
    test_loop_and_statics();
    test_unmapped();
    test_pointer_chain();
    test_unchanged_store();
    printf("cheat_vm: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
