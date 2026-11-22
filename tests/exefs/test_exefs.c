// Host tests of runtime/exefs_patch.c (the ExeFS patch readers). Synthetic files only: no game or mod files.
#include <stdio.h>
#include <string.h>

#include "exefs_patch.h"

static int checks, failures;
#define CHECK(c)                                                              \
    do {                                                                      \
        checks++;                                                             \
        if (!(c)) {                                                           \
            failures++;                                                       \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);               \
        }                                                                     \
    } while (0)

static const char *BID = "277178B7DBA1B6D4949A84778D4ABC58B31B34F5000000000000000000000000";

static int run_is(const ExefsWrites *w, int i, uint64_t off, const uint8_t *b, uint32_t len) {
    if (i >= w->n || w->runs[i].off != off || w->runs[i].len != len) return 0;
    return !memcmp(w->runs[i].bytes, b, len);
}

static void test_bid(void) {
    CHECK(exefs_bid_match("277178B7DBA1B6D4949A84778D4ABC58B31B34F5", BID));
    CHECK(exefs_bid_match("277178b7dba1b6d4949a84778d4abc58b31b34f5", BID));          // case does not matter
    CHECK(exefs_bid_match("277178B7DBA1B6D4949A84778D4ABC58B31B34F5000000000000000000000000", BID));
    CHECK(!exefs_bid_match("277178B7DBA1B6D4949A84778D4ABC58B31B34F4", BID));          // one digit off
    CHECK(!exefs_bid_match("277178B7DBA1B6D4", BID));                                  // the cheat files' 8 bytes: not a full name
    CHECK(!exefs_bid_match("", BID));
    CHECK(!exefs_bid_match("277178B7DBA1B6D4949A84778D4ABC58B31B34F5G", BID));        // not hex
    CHECK(!exefs_bid_match("277178B7DBA1B6D4949A84778D4ABC58B31B34F5000000000000000000000000001", BID));   // too long
}

// IPS32: an 4-byte write at 0x1100 (file offset: module offset 0x1000), a run of 3 bytes 0x90 at 0x2000, then EEOF
static void test_ips32(void) {
    const uint8_t f[] = {'I', 'P', 'S', '3', '2',
                         0x00, 0x00, 0x13, 0x00, 0x00, 0x04, 0xDE, 0xAD, 0xBE, 0xEF,   // off 0x1300, 4 bytes
                         0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x03, 0x90,   // off 0x2000, run of 3 x 0x90
                         'E', 'E', 'O', 'F'};
    ExefsWrites w = {0};
    char err[80] = "";
    CHECK(exefs_parse_ips(f, sizeof f, &w, err, sizeof err) == 0);
    CHECK(w.n == 2);
    const uint8_t a[] = {0xDE, 0xAD, 0xBE, 0xEF}, b[] = {0x90, 0x90, 0x90};
    CHECK(run_is(&w, 0, 0x1200, a, 4));   // the file offset less the NSO header
    CHECK(run_is(&w, 1, 0x1F00, b, 3));
    exefs_writes_free(&w);
    CHECK(w.n == 0 && w.runs == NULL);
}

static void test_ips(void) {   // the 3-byte form: PATCH ... EOF
    const uint8_t f[] = {'P', 'A', 'T', 'C', 'H', 0x00, 0x02, 0x00, 0x00, 0x02, 0xAB, 0xCD, 'E', 'O', 'F'};
    ExefsWrites w = {0};
    CHECK(exefs_parse_ips(f, sizeof f, &w, NULL, 0) == 0);
    const uint8_t a[] = {0xAB, 0xCD};
    CHECK(w.n == 1 && run_is(&w, 0, 0x100, a, 2));   // file 0x200 is module 0x100
    exefs_writes_free(&w);
}

static void test_ips_errors(void) {
    ExefsWrites w = {0};
    char err[80] = "";
    const uint8_t bad[] = {'X', 'P', 'S', '3', '2', 'E', 'E', 'O', 'F'};
    CHECK(exefs_parse_ips(bad, sizeof bad, &w, err, sizeof err) == -1);
    CHECK(strstr(err, "not an IPS") != NULL);
    const uint8_t cut[] = {'I', 'P', 'S', '3', '2', 0x00, 0x00, 0x13, 0x00, 0x04, 0xDE};   // 4 bytes promised, 1 there
    CHECK(exefs_parse_ips(cut, sizeof cut, &w, err, sizeof err) == -1);
    CHECK(strstr(err, "cut short") != NULL);
    const uint8_t hdr[] = {'I', 'P', 'S', '3', '2', 0x00, 0x00, 0x00, 0x50, 0x00, 0x01, 0xFF, 'E', 'E', 'O', 'F'};   // off 0x50
    CHECK(exefs_parse_ips(hdr, sizeof hdr, &w, err, sizeof err) == -1);
    CHECK(strstr(err, "NSO header") != NULL);
    CHECK(w.n == 0);
}

static void test_pchtxt(void) {
    const char *t =
        "@nsobid-277178B7DBA1B6D4949A84778D4ABC58B31B34F5\n"
        "# a title line, then text: not writes\n"
        "00000111 22222222\n"            // before @enabled: not a write
        "@flag offset_shift 0x100\n"
        "@enabled\n"
        "009700A8 D6268914  // -> cave\n"
        "02BB9C00 1C1B00F9  //   cave\n"
        "@disabled\n"
        "00000010 00000000\n"            // off: not a write
        "@enabled\n"
        "00000200 AABB\r\n"              // CRLF line ends
        "@stop\n"
        "00000300 CC\n";                 // after @stop: not read
    char bid[80] = "";
    ExefsWrites w = {0};
    char err[80] = "";
    CHECK(exefs_parse_pchtxt(t, bid, sizeof bid, &w, err, sizeof err) == 0);
    CHECK(strcmp(bid, "277178B7DBA1B6D4949A84778D4ABC58B31B34F5") == 0);
    CHECK(w.n == 3);
    const uint8_t c1[] = {0xD6, 0x26, 0x89, 0x14}, c2[] = {0x1C, 0x1B, 0x00, 0xF9}, c3[] = {0xAA, 0xBB};
    CHECK(run_is(&w, 0, 0x9700A8, c1, 4));   // the offset_shift (0x100) and the header (-0x100) cancel
    CHECK(run_is(&w, 1, 0x2BB9C00, c2, 4));
    CHECK(run_is(&w, 2, 0x200, c3, 2));
    exefs_writes_free(&w);
}

static void test_pchtxt_bom_and_no_shift(void) {
    const char *t = "\xEF\xBB\xBF@nsobid-277178B7DBA1B6D4949A84778D4ABC58B31B34F5\n@enabled\n00000150 01\n";
    char bid[80] = "";
    ExefsWrites w = {0};
    CHECK(exefs_parse_pchtxt(t, bid, sizeof bid, &w, NULL, 0) == 0);
    const uint8_t b[] = {0x01};
    CHECK(strcmp(bid, "277178B7DBA1B6D4949A84778D4ABC58B31B34F5") == 0 && w.n == 1 && run_is(&w, 0, 0x50, b, 1));   // no shift: 0x150 - 0x100
    exefs_writes_free(&w);
}

static void test_pchtxt_errors(void) {
    ExefsWrites w = {0};
    char err[80] = "";
    CHECK(exefs_parse_pchtxt("@enabled\n00000200 \"abc\"\n", NULL, 0, &w, err, sizeof err) == -1);
    CHECK(strstr(err, "string") != NULL);
    CHECK(exefs_parse_pchtxt("@enabled\n00000200 ABC\n", NULL, 0, &w, err, sizeof err) == -1);
    CHECK(strstr(err, "not hex") != NULL);
    CHECK(exefs_parse_pchtxt("@flag offset_shift 0xZZ\n", NULL, 0, &w, err, sizeof err) == -1);
    CHECK(exefs_parse_pchtxt("@enabled\n00000050 00\n", NULL, 0, &w, err, sizeof err) == -1);
    CHECK(strstr(err, "NSO header") != NULL);
    CHECK(w.n == 0);
}

int main(void) {
    test_bid();
    test_ips32();
    test_ips();
    test_ips_errors();
    test_pchtxt();
    test_pchtxt_bom_and_no_shift();
    test_pchtxt_errors();
    printf("exefs: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
