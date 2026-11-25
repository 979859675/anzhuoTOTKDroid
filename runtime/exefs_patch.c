// ExeFS patches: the IPS / IPS32 and .pchtxt readers (exefs_patch.h). Platform-free.
#include "exefs_patch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void exefs_writes_free(ExefsWrites *w) {
    for (int i = 0; i < w->n; i++) free(w->runs[i].bytes);
    free(w->runs);
    w->runs = NULL;
    w->n = w->cap = 0;
}

static int fail(char *err, size_t cap, const char *msg) {
    if (err && cap) snprintf(err, cap, "%s", msg);
    return -1;
}

// Appends a run of len bytes at off (copied). 0, or -1 when out of memory.
static int add_run(ExefsWrites *w, uint64_t off, const uint8_t *b, uint32_t len) {
    if (w->n == w->cap) {
        int ncap = w->cap ? w->cap * 2 : 16;
        ExefsRun *g = (ExefsRun *)realloc(w->runs, sizeof *g * (size_t)ncap);
        if (!g) return -1;
        w->runs = g;
        w->cap = ncap;
    }
    uint8_t *copy = (uint8_t *)malloc(len ? len : 1);
    if (!copy) return -1;
    if (len) memcpy(copy, b, len);
    w->runs[w->n].off = off;
    w->runs[w->n].len = len;
    w->runs[w->n].bytes = copy;
    w->n++;
    return 0;
}

static int hexval(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int exefs_bid_match(const char *name, const char *bid) {
    size_t n = strlen(name);
    if (n == 0 || n > 64 || strlen(bid) != 64) return 0;
    for (size_t i = 0; i < n; i++)
        if (hexval((unsigned char)name[i]) < 0) return 0;
    for (size_t i = 0; i < 64; i++) {
        char a = i < n ? name[i] : '0';
        if (hexval((unsigned char)a) < 0) return 0;   // (the hex digits; a is '0' past the name)
        if (hexval((unsigned char)bid[i]) < 0) return 0;
        if ((a | 0x20) != (bid[i] | 0x20)) return 0;   // (case does not matter)
    }
    return 1;
}

static uint32_t be_n(const uint8_t *p, size_t n) {
    uint32_t v = 0;
    for (size_t i = 0; i < n; i++) v = (v << 8) | p[i];
    return v;
}

int exefs_parse_ips(const uint8_t *d, size_t n, ExefsWrites *out, char *err, size_t errcap) {
    size_t width, at = 5;
    const char *end;
    if (n >= 5 && !memcmp(d, "PATCH", 5)) {
        width = 3;
        end = "EOF";
    } else if (n >= 5 && !memcmp(d, "IPS32", 5)) {
        width = 4;
        end = "EEOF";
    } else {
        return fail(err, errcap, "not an IPS or IPS32 file");
    }
    size_t elen = strlen(end);
    for (;;) {
        // the end marker, with at most 3 bytes after it (an IPS file's truncation size)
        if (at + elen <= n && !memcmp(d + at, end, elen) && at + elen + 3 >= n) return 0;
        if (at + width + 2 > n) return fail(err, errcap, "the IPS file is cut short");
        uint64_t off = be_n(d + at, width);
        uint32_t size = (uint32_t)be_n(d + at + width, 2);
        at += width + 2;
        if (off < EXEFS_NSO_HEADER) return fail(err, errcap, "a write into the NSO header");
        if (size) {
            if (at + size > n) return fail(err, errcap, "the IPS file is cut short");
            if (add_run(out, off - EXEFS_NSO_HEADER, d + at, size)) return fail(err, errcap, "out of memory");
            at += size;
        } else {   // a run: a 2-byte length, then the byte
            if (at + 3 > n) return fail(err, errcap, "the IPS file is cut short");
            uint32_t run = (uint32_t)be_n(d + at, 2);
            uint8_t v = d[at + 2];
            at += 3;
            uint8_t *body = (uint8_t *)malloc(run ? run : 1);
            if (!body) return fail(err, errcap, "out of memory");
            memset(body, v, run);
            int r = add_run(out, off - EXEFS_NSO_HEADER, body, run);
            free(body);
            if (r) return fail(err, errcap, "out of memory");
        }
    }
}

// the line's text without a // comment, its ends trimmed (in place); the start of the result
static char *clean_line(char *s, size_t len) {
    for (size_t i = 0; i + 1 < len; i++)
        if (s[i] == '/' && s[i + 1] == '/') {
            len = i;
            break;
        }
    s[len] = 0;
    while (len && (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\r')) s[--len] = 0;
    char *p = s;
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

// a token of non-blank characters at p: its end (p at its first character, or at the end of the line)
static char *token_end(char *p) {
    while (*p && *p != ' ' && *p != '\t') p++;
    return p;
}

int exefs_parse_pchtxt(const char *text, char *bid, size_t bidcap, ExefsWrites *out, char *err, size_t errcap) {
    if (bidcap) bid[0] = 0;
    int64_t shift = 0;
    int enabled = 0;
    char *copy = (char *)malloc(strlen(text) + 1);
    if (!copy) return fail(err, errcap, "out of memory");
    strcpy(copy, text);
    if (!strncmp(copy, "\xEF\xBB\xBF", 3)) memmove(copy, copy + 3, strlen(copy + 3) + 1);
    int rc = 0;
    for (char *line = copy; line && rc == 0;) {
        char *next = strchr(line, '\n');
        size_t len = next ? (size_t)(next - line) : strlen(line);
        char *following = next ? next + 1 : NULL;   // (before clean_line, which ends the line with a NUL)
        char *p = clean_line(line, len);
        line = following;
        if (!*p) continue;
        if (!strncmp(p, "@stop", 5)) break;
        if (!strncmp(p, "@nsobid-", 8)) {
            char *v = p + 8;
            while (*v == ' ' || *v == '\t') v++;
            if (bidcap) snprintf(bid, bidcap, "%s", v);
            continue;
        }
        if (!strncmp(p, "@enabled", 8)) {
            enabled = 1;
            continue;
        }
        if (!strncmp(p, "@disabled", 9)) {
            enabled = 0;
            continue;
        }
        if (!strncmp(p, "@flag", 5)) {
            char *a = p + 5;
            while (*a == ' ' || *a == '\t') a++;
            char *ae = token_end(a);
            if (ae - a == 12 && !strncmp(a, "offset_shift", 12)) {
                char *v = ae;
                while (*v == ' ' || *v == '\t') v++;
                char *ve = token_end(v);
                char tok[32];
                if (!*v || ve - v >= (long)sizeof tok) {
                    rc = fail(err, errcap, "offset_shift without a value");
                    break;
                }
                memcpy(tok, v, (size_t)(ve - v));
                tok[ve - v] = 0;
                char *endp = NULL;
                long long sh = strtoll(tok, &endp, 0);
                if (!endp || *endp) {
                    rc = fail(err, errcap, "offset_shift is not a number");
                    break;
                }
                shift = sh;
            }
            continue;
        }
        if (*p == '@' || !enabled) continue;
        // a write: hex offset, then hex bytes (or a string), then nothing else; any other line is a title or text
        char *oe = p;
        while (hexval((unsigned char)*oe) >= 0) oe++;
        if (oe == p || (*oe != ' ' && *oe != '\t')) continue;
        char *v = oe;
        while (*v == ' ' || *v == '\t') v++;
        char *ve = token_end(v);
        if (v == ve) continue;
        char *rest = ve;
        while (*rest == ' ' || *rest == '\t') rest++;
        if (*rest) continue;   // (more than the two tokens: not a write)
        if (*v == '"') {
            rc = fail(err, errcap, "a string value is not supported");
            break;
        }
        size_t vn = (size_t)(ve - v);
        int bad = vn % 2 != 0;
        for (size_t i = 0; i < vn && !bad; i++) bad = hexval((unsigned char)v[i]) < 0;
        if (bad) {
            rc = fail(err, errcap, "a value that is not hex");
            break;
        }
        if (oe - p > 16) {
            rc = fail(err, errcap, "an offset too long");
            break;
        }
        uint64_t o = 0;
        for (char *q = p; q < oe; q++) o = (o << 4) | (uint64_t)hexval((unsigned char)*q);
        int64_t off = (int64_t)o + shift - EXEFS_NSO_HEADER;
        if (off < 0) {
            rc = fail(err, errcap, "a write into the NSO header");
            break;
        }
        uint8_t buf[256];
        uint8_t *bytes = vn / 2 <= sizeof buf ? buf : (uint8_t *)malloc(vn / 2);
        if (!bytes) {
            rc = fail(err, errcap, "out of memory");
            break;
        }
        for (size_t i = 0; i < vn / 2; i++) bytes[i] = (uint8_t)(hexval((unsigned char)v[2 * i]) << 4 | hexval((unsigned char)v[2 * i + 1]));
        int r = add_run(out, (uint64_t)off, bytes, (uint32_t)(vn / 2));
        if (bytes != buf) free(bytes);
        if (r) {
            rc = fail(err, errcap, "out of memory");
            break;
        }
    }
    free(copy);
    return rc;
}
