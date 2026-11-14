// Atmosphere's cheat VM and cheat file parser (cheat_vm.h). Ported from valentderah/totk-pc-recomp runtime/cheats.c
// (MIT): the same opcodes, the same reading of each one as stratosphere's dmnt does it (opcodes 0-A and C0-C3; C4 and
// the 0xF extensions are not run).
#include "cheat_vm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { OPS_PER_RUN = 1 << 20 };

uint32_t cheat_fnv(const char *s, size_t n) {
    uint32_t h = 0x811C9DC5u;
    for (size_t i = 0; i < n; i++) h = (h ^ (uint8_t)s[i]) * 0x01000193u;
    return h;
}

// ---- opcodes

// Words of the opcode at w (left words remain), 0 when it is cut short.
static int op_len(const uint32_t *w, int left) {
    uint32_t f = w[0];
    int t = f >> 28, width = (f >> 24) & 0xF, n;
    switch (t) {
    case 0: case 1: n = width == 8 ? 4 : 3; break;
    case 2: case 8: n = 1; break;
    case 3: n = width == 0 ? 2 : 1; break;
    case 4: case 6: n = 3; break;
    case 5: case 7: n = 2; break;
    case 9: n = 1 + (((f >> 8) & 0xF) ? (width == 8 ? 2 : 1) : 0); break;
    case 0xA: n = ((f >> 8) & 0xF) == 2 || ((f >> 8) & 0xF) == 4 || ((f >> 8) & 0xF) == 5 ? 2 : 1; break;
    case 0xC:   // C0TcSX..: an address word for operand types 0 and 2, the value (two words at width 8) for 4
        if (((f >> 24) & 0xF) != 0) n = 1;
        else if (((f >> 8) & 0xF) == 0 || ((f >> 8) & 0xF) == 2) n = 2;
        else n = ((f >> 8) & 0xF) == 4 ? (((f >> 20) & 0xF) == 8 ? 3 : 2) : 1;
        break;
    default: n = 1; break;
    }
    return n <= left ? n : 0;
}

// 1 when the VM does not run the opcode at w (its note in buf), else 0.
static int op_unsupported(const uint32_t *w, char *buf, size_t n) {
    uint32_t f = w[0];
    int t = f >> 28, width = (f >> 24) & 0xF;
    int ext = f >> 24;   // 0xC0-0xC3 run; C0's width is at bits 20-23 and its operand type (at most 5) at bits 8-11
    if (ext == 0xC0) t = -1, width = (f >> 20) & 0xF;
    if ((t > 0xA && (ext < 0xC1 || ext > 0xC3)) || (t == -1 && ((f >> 8) & 0xF) > 5) ||
        (t == 5 && ((f >> 12) & 0xF) > 1) || (t == 0xA && ((f >> 8) & 0xF) > 5)) {
        snprintf(buf, n, "opcode %08X is not supported", f);
        return 1;
    }
    if ((t <= 1 || t == 5 || t == 6 || t == 7 || t == 9 || t == 0xA) && width != 1 && width != 2 && width != 4 &&
        width != 8) {   // C0 too (t -1)
        snprintf(buf, n, "opcode %08X has a bad width", f);
        return 1;
    }
    return 0;
}

// A store opcode into main at a constant offset (0TMR00AA AAAAAAAA ..: M 0, R 0): 1 with its offset in *off, else 0.
static int main_store(const uint32_t *w, uint64_t *off) {
    uint32_t f = w[0];
    if (f >> 28 != 0 || ((f >> 20) & 0xF) != 0 || ((f >> 16) & 0xF) != 0) return 0;
    *off = ((uint64_t)(f & 0xFF) << 32) | w[1];
    return 1;
}

CheatKind cheat_classify(const uint32_t *w, int n, uint64_t code_limit, char *note, size_t notecap) {
    int ops = 0, ends = 0, code = 0, other = 0;
    for (int i = 0; i < n;) {
        int len = op_len(w + i, n - i);
        if (!len) {
            snprintf(note, notecap, "opcode %08X is cut short", w[i]);
            return CHEAT_UNSUPPORTED;
        }
        uint32_t f = w[i];
        ops++;
        ends += f >> 28 == 2;
        uint64_t off;
        if (main_store(w + i, &off) && off < code_limit) {   // a write into main's code
            int width = (f >> 24) & 0xF;
            if ((width != 4 && width != 8) || (off & 3)) {
                snprintf(note, notecap, "a write into the game's code at offset %llx is not a whole instruction",
                         (unsigned long long)off);
                return CHEAT_UNSUPPORTED;
            }
            code = 1;
        } else if (f >> 28 != 0 && f >> 28 != 2 && f >> 28 != 8) {   // (a store, an end or else, a button condition)
            other = 1;
        }
        if (op_unsupported(w + i, note, notecap)) return CHEAT_UNSUPPORTED;
        i += len;
    }
    if (code) {
        if (other) {
            snprintf(note, notecap, "code writes come with other opcodes");
            return CHEAT_UNSUPPORTED;
        }
        return CHEAT_CODE;
    }
    return ops == ends && ops <= 1 ? CHEAT_HEADING : CHEAT_DATA;
}

int cheat_code_stores(const uint32_t *w, int n, uint64_t code_limit, CheatStore *out, int cap) {
    int k = 0;
    for (int i = 0; i < n;) {
        int len = op_len(w + i, n - i);
        if (!len) break;
        uint64_t off;
        if (main_store(w + i, &off) && off < code_limit) {
            int width = (w[i] >> 24) & 0xF;
            if (out && k < cap) {
                out[k].off = off;
                out[k].width = width;
                out[k].value = width == 8 ? (uint64_t)w[i + 2] << 32 | w[i + 3] : w[i + 2];
            }
            k++;
        }
        i += len;
    }
    return k;
}

// ---- guest memory (through CheatMem)

static uint64_t region(const CheatMem *m, int k) { return m->region(m->ctx, k); }

static int rd(const CheatMem *m, uint64_t a, int width, uint64_t *v) {
    if (!a) return 0;
    return m->read(m->ctx, a, width, v);
}

// A store that changes nothing is not made (as the cheat VM of dmnt writes only when the value differs).
static void wr(const CheatMem *m, uint64_t a, int width, uint64_t v) {
    uint64_t now;
    if (!a || !rd(m, a, width, &now)) return;
    if (!memcmp(&now, &v, (size_t)width)) return;
    m->write(m->ctx, a, width, v);
}

static uint64_t masked(int width, uint64_t v) { return width >= 8 ? v : v & ((1ull << (8 * width)) - 1); }

static uint64_t arith(int op, uint64_t a, uint64_t b) {
    switch (op) {
    case 0: return a + b;
    case 1: return a - b;
    case 2: return a * b;
    case 3: return b < 64 ? a << b : 0;
    case 4: return b < 64 ? a >> b : 0;
    case 5: return a & b;
    case 6: return a | b;
    case 7: return ~a;
    case 8: return a ^ b;
    default: return a;   // 9: move
    }
}

static int compare(int cond, uint64_t a, uint64_t b) {
    switch (cond) {
    case 1: return a > b;
    case 2: return a >= b;
    case 3: return a < b;
    case 4: return a <= b;
    case 5: return a == b;
    case 6: return a != b;
    default: return 0;
    }
}

// From i (after a failed condition, or an else reached while running), the opcode to continue at: after the
// matching else (else_ok) or end. Conditions 1, 8 and C0 open a block, as in dmnt (CheatVirtualMachine::
// SkipConditionalBlock), which steps over whole opcodes so that an operand word reading 20000000 ends nothing.
static int skip(const uint32_t *w, int n, int i, int else_ok) {
    int depth = 0;
    while (i < n) {
        int len = op_len(w + i, n - i);
        if (!len) return n;
        uint32_t f = w[i];
        int t = f >> 28;
        i += len;
        if (t == 1 || t == 8 || f >> 24 == 0xC0) depth++;
        else if (t == 2 && ((f >> 24) & 0xF) == 1) {   // else; any other 2x000000 ends, as dmnt decodes is_else
            if (depth == 0 && else_ok) return i;
        } else if (t == 2 && depth-- == 0) {
            return i;
        }
    }
    return n;
}

static uint64_t imm64(const uint32_t *w) { return (uint64_t)w[0] << 32 | w[1]; }

// C0TcSX..: if register S <cond c> operand X, all cut to width T; the operand as dmnt reads it
// (CheatVirtualMachine::DecodeNextOpcode and Execute, BeginRegisterConditionalBlock). X: 0 region M + a, 1 region M +
// register r, 2 register R + a, 3 register R + register r, 4 the value V, 5 register R. M, R at bits 4-7, r and the
// top bits of a at bits 0-3. A read the guest cannot make compares as 0: dmnt ignores the read's result and compares
// the zero it started with.
static int reg_cond(const CheatMem *m, const uint32_t *w, const uint64_t *r) {
    uint32_t f = w[0];
    int width = (f >> 20) & 0xF, cond = (f >> 16) & 0xF, a4 = (f >> 4) & 0xF, a0 = f & 0xF;
    uint64_t src = masked(width, r[(f >> 12) & 0xF]), b = 0, addr;
    switch ((f >> 8) & 0xF) {   // w[1] exists for types 0, 2 and 4 only (op_len)
    case 0: addr = region(m, a4) + (((uint64_t)a0 << 32) | w[1]); break;
    case 1: addr = region(m, a4) + r[a0]; break;
    case 2: addr = r[a4] + (((uint64_t)a0 << 32) | w[1]); break;
    case 3: addr = r[a4] + r[a0]; break;
    case 4: return compare(cond, src, width == 8 ? imm64(w + 1) : masked(width, w[1]));
    default: return compare(cond, src, masked(width, r[a4]));
    }
    if (!rd(m, addr, width, &b)) b = 0;
    return compare(cond, src, b);
}

void cheat_vm_run(const uint32_t *w, int n, const CheatMem *m, uint64_t keys, uint64_t statics[256]) {
    uint64_t r[16] = {0}, saved[16] = {0};
    int loop[16] = {0};
    int budget = OPS_PER_RUN;
    for (int i = 0; i < n && budget-- > 0;) {
        int len = op_len(w + i, n - i);
        if (!len) return;
        uint32_t f = w[i];
        int t = f >> 28, width = (f >> 24) & 0xF, x = (f >> 20) & 0xF, y = (f >> 16) & 0xF, z = (f >> 12) & 0xF;
        uint64_t rel = ((uint64_t)(f & 0xFF) << 32) | (len > 1 ? w[i + 1] : 0), v;
        switch (t) {
        case 0:   // 0TMR00AA AAAAAAAA V..: store a value at region M + A + register R
            wr(m, region(m, x) + rel + r[y], width, width == 8 ? imm64(w + i + 2) : w[i + 2]);
            break;
        case 1:   // 1TMC00AA AAAAAAAA V..: if (region M + A) <cond C> V
            if (!rd(m, region(m, x) + rel, width, &v) ||
                !compare(y, v, width == 8 ? imm64(w + i + 2) : w[i + 2])) {
                i = skip(w, n, i + len, 1);
                continue;
            }
            break;
        case 2:   // 20000000 end, 21000000 else: an else reached while running skips to the end
            if (width == 1) {
                i = skip(w, n, i + len, 0);
                continue;
            }
            break;
        case 3:   // 300R0000 VVVVVVVV loop start, 310R0000 loop end
            if (width == 0) {
                r[y] = w[i + 1];
                loop[y] = i + len;
            } else if (r[y] && --r[y]) {
                i = loop[y];
                continue;
            }
            break;
        case 4:   // 400R0000 V V: register R = V
            r[y] = imm64(w + i + 1);
            break;
        case 5:   // 5TMRI0AA AAAAAAAA: R = (I ? R : region M) + A, read
            if (!rd(m, z ? r[y] + rel : region(m, x) + rel, width, &v)) return;   // a broken pointer chain: nothing more
            r[y] = v;
            break;
        case 6: {   // 6T0RIor0 V V: store V at R (+ register r); I: R += width
            uint64_t a = r[y] + (((f >> 8) & 0xF) ? r[(f >> 4) & 0xF] : 0);
            wr(m, a, width, imm64(w + i + 1));
            if (z) r[y] += (uint64_t)width;
            break;
        }
        case 7:   // 7T0RC000 VVVVVVVV: R = R <op C> V
            r[y] = masked(width, arith(z, r[y], w[i + 1]));
            break;
        case 8:   // 8kkkkkkk: if the buttons are held
            if ((keys & (f & 0x0FFFFFFF)) != (f & 0x0FFFFFFF)) {
                i = skip(w, n, i + len, 1);
                continue;
            }
            break;
        case 9: {   // 9TCRS0s0 / 9TCRS100 V..: R = S <op C> (s or V)
            uint64_t b = (f >> 8) & 0xF ? (width == 8 ? imm64(w + i + 1) : w[i + 1]) : r[(f >> 4) & 0xF];
            r[y] = masked(width, arith(x, r[z], b));
            break;
        }
        case 0xA: {   // ATSRIOxa (aaaaaaaa): store register S at an address of type O
            int o = (f >> 8) & 0xF, mm = (f >> 4) & 0xF;
            uint64_t imm = ((uint64_t)(f & 0xF) << 32) | (len > 1 ? w[i + 1] : 0), a;
            switch (o) {
            case 0: a = r[y]; break;
            case 1: a = r[y] + r[mm]; break;
            case 2: a = r[y] + imm; break;
            case 3: a = region(m, mm) + r[y]; break;
            case 4: a = region(m, mm) + imm; break;
            default: a = region(m, mm) + r[y] + imm; break;
            }
            wr(m, a, width, r[x]);
            if (z) r[y] += (uint64_t)width;
            break;
        }
        case 0xC:   // C1-C3 as dmnt's Execute runs SaveRestoreRegister(Mask) and ReadWriteStaticRegister
            switch (f >> 24) {
            case 0xC0:
                if (!reg_cond(m, w + i, r)) {
                    i = skip(w, n, i + len, 1);
                    continue;
                }
                break;
            case 0xC1: {   // C10D0Sx0: x 0 register D = saved S, 1 saved D = register S, 2 saved D = 0, 3 register D = 0
                int d = (f >> 16) & 0xF, s = (f >> 8) & 0xF, op = (f >> 4) & 0xF;
                if (op == 1) saved[d] = r[s];
                else if (op == 2) saved[d] = 0;
                else if (op == 3) r[d] = 0;
                else r[d] = saved[s];   // dmnt restores for any other x too
                break;
            }
            case 0xC2: {   // C2x0XXXX: for each register in the mask X, x as C1 with D = S = that register
                int op = (f >> 20) & 0xF;
                for (int k = 0; k < 16; k++) {
                    if (!(f >> k & 1)) continue;
                    if (op == 1) saved[k] = r[k];
                    else if (op == 2) saved[k] = 0;
                    else if (op == 3) r[k] = 0;
                    else r[k] = saved[k];
                }
                break;
            }
            case 0xC3: {   // C3000XXx: static register XX < 0x80 into register x, else register x into it
                int s = (f >> 4) & 0xFF;
                if (s < 0x80) r[f & 0xF] = statics[s];
                else statics[s] = r[f & 0xF];
                break;
            }
            default:
                return;
            }
            break;
        default:
            return;
        }
        i += len;
    }
}

// ---- the file

static int hex_word(const char *tok, uint32_t *v) {
    if (strlen(tok) != 8) return 0;
    uint32_t x = 0;
    for (int k = 0; k < 8; k++) {
        char c = tok[k];
        int d = c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
        if (d < 0) return 0;
        x = x << 4 | (uint32_t)d;
    }
    *v = x;
    return 1;
}

static void add_word(CheatRec *c, uint32_t v, int *cap) {
    if (c->n == *cap) {
        *cap = *cap ? *cap * 2 : 16;
        uint32_t *w = (uint32_t *)realloc(c->words, sizeof *w * (size_t)*cap);
        if (!w) return;   // (out of memory: the cheat loses a word; the file is not trusted past it anyway)
        c->words = w;
    }
    c->words[c->n++] = v;
}

void cheat_parse_free(CheatRec *recs, int n) {
    for (int i = 0; i < n; i++) free(recs[i].name), free(recs[i].words);
    free(recs);
}

int cheat_parse(char *s, CheatRec **out) {
    CheatRec *v = NULL;
    int n = 0, cap = 0, wcap = 0, title = 0;
    *out = NULL;
    for (char *line = s; line && *line;) {
        char *end = strchr(line, '\n'), *next = end ? end + 1 : NULL;
        if (end) *end = 0;
        while (*line == ' ' || *line == '\t') line++;
        size_t len = strlen(line);
        while (len && (line[len - 1] == '\r' || line[len - 1] == ' ' || line[len - 1] == '\t')) line[--len] = 0;
        if (len >= 2 && (line[0] == '[' || line[0] == '{') && (line[len - 1] == ']' || line[len - 1] == '}')) {
            line[len - 1] = 0;
            char *name = line + 1;
            while (*name == ' ' || *name == '\t') name++;
            size_t k = strlen(name);
            while (k && (name[k - 1] == ' ' || name[k - 1] == '\t')) name[--k] = 0;
            if (strstr(name, "TID=")) {   // the file's title line: not a cheat, and the words after it belong to none
                title = 1;
                line = next;
                continue;
            }
            title = 0;
            if (n == cap) {
                cap = cap ? cap * 2 : 32;
                CheatRec *grown = (CheatRec *)realloc(v, sizeof *v * (size_t)cap);
                if (!grown) break;
                v = grown;
            }
            CheatRec *c = &v[n++];
            memset(c, 0, sizeof *c);
            c->name = strdup(name);
            c->master = line[0] == '{';
            c->dup = 1;
            for (int j = 0; j < n - 1; j++) c->dup += !strcmp(v[j].name, name);
            wcap = 0;
        } else if (n && !title) {
            char *p = line;
            while (*p) {
                while (*p == ' ' || *p == '\t') p++;
                if (!*p) break;
                char *tok = p;
                while (*p && *p != ' ' && *p != '\t') p++;
                char save = *p;
                *p = 0;
                uint32_t word;
                if (hex_word(tok, &word)) add_word(&v[n - 1], word, &wcap);
                *p = save;
                if (save) p++;
            }
        }
        line = next;
    }
    *out = v;
    return n;
}
