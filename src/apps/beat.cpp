// BEAT — bytebeat livecoder. Type an expression in t (the sample counter),
// press enter, and it plays: each value's low byte is the waveform, at 8 kHz
// by default. Errors keep the old incantation playing and point at the fault.
//
// grammar  C integer expressions on unsigned 32-bit t:
//          + - * / % & | ^ << >> < > <= >= == != ~ ! ?: ( ), decimal and 0x hex
// keys     type to edit   enter cast   tab next preset   fn+,/ cursor
//          fn+;. rate 8k/11k/16k/22k   fn+backspace clear   fn+r restart t
#include "../dsp.h"
#include "../rite.h"
#include "../theme.h"
#include <stdio.h>
#include <string.h>

using namespace theme;

namespace {
enum Op : uint8_t {
    O_NUM, O_T, O_ADD, O_SUB, O_MUL, O_DIV, O_MOD, O_AND, O_OR, O_XOR, O_SHL, O_SHR,
    O_LT, O_GT, O_LE, O_GE, O_EQ, O_NE, O_NEG, O_INV, O_NOT, O_TERN,
};
constexpr int MAX_CODE = 96, MAX_SRC = 120;
struct Ins {
    uint8_t op;
    uint32_t v;
};
struct Program {
    Ins code[MAX_CODE];
    int n = 0;
};

inline uint32_t run(const Program &p, uint32_t t) {
    uint32_t st[32];
    int sp = 0;
    for (int i = 0; i < p.n; ++i) {
        const Ins &in = p.code[i];
        switch (in.op) {
        case O_NUM: st[sp++] = in.v; continue;
        case O_T: st[sp++] = t; continue;
        case O_NEG: st[sp - 1] = -st[sp - 1]; continue;
        case O_INV: st[sp - 1] = ~st[sp - 1]; continue;
        case O_NOT: st[sp - 1] = !st[sp - 1]; continue;
        case O_TERN: {
            uint32_t c = st[sp - 3], a = st[sp - 2], b = st[sp - 1];
            sp -= 2;
            st[sp - 1] = c ? a : b;
            continue;
        }
        default: break;
        }
        uint32_t b = st[--sp], a = st[sp - 1], r = 0;
        switch (in.op) {
        case O_ADD: r = a + b; break;
        case O_SUB: r = a - b; break;
        case O_MUL: r = a * b; break;
        case O_DIV: r = b ? a / b : 0; break;
        case O_MOD: r = b ? a % b : 0; break;
        case O_AND: r = a & b; break;
        case O_OR: r = a | b; break;
        case O_XOR: r = a ^ b; break;
        case O_SHL: r = a << (b & 31); break;
        case O_SHR: r = a >> (b & 31); break;
        case O_LT: r = a < b; break;
        case O_GT: r = a > b; break;
        case O_LE: r = a <= b; break;
        case O_GE: r = a >= b; break;
        case O_EQ: r = a == b; break;
        case O_NE: r = a != b; break;
        }
        st[sp - 1] = r;
    }
    return sp ? st[sp - 1] : 0;
}

// ---- compiler: recursive descent straight to RPN -------------------------
struct Compiler {
    const char *src;
    int pos = 0;
    Program *out;
    const char *err = nullptr;
    int errPos = 0;
    int depth = 0, maxDepth = 0;

    void fail(const char *m) {
        if (!err) {
            err = m;
            errPos = pos;
        }
    }
    void ws() {
        while (src[pos] == ' ') ++pos;
    }
    bool eat(const char *s) {
        ws();
        int n = strlen(s);
        if (strncmp(src + pos, s, n) == 0) {
            // don't let '<' eat the start of '<<' or '<='
            if (n == 1 && (s[0] == '<' || s[0] == '>') && (src[pos + 1] == s[0] || src[pos + 1] == '='))
                return false;
            if (n == 1 && (s[0] == '&' || s[0] == '|') && src[pos + 1] == s[0]) return false;
            if (n == 1 && s[0] == '!' && src[pos + 1] == '=') return false;
            pos += n;
            return true;
        }
        return false;
    }
    void emit(uint8_t op, uint32_t v = 0) {
        if (out->n >= MAX_CODE) return fail("too long");
        out->code[out->n++] = {op, v};
        if (op == O_NUM || op == O_T) {
            if (++depth > maxDepth) maxDepth = depth;
        } else if (op == O_TERN) depth -= 2;
        else if (op != O_NEG && op != O_INV && op != O_NOT) depth -= 1;
    }
    void primary() {
        ws();
        char c = src[pos];
        if (c == '(') {
            ++pos;
            ternary();
            if (!eat(")")) fail("missing )");
        } else if (c == 't') {
            ++pos;
            emit(O_T);
        } else if (c >= '0' && c <= '9') {
            uint32_t v = 0;
            if (c == '0' && (src[pos + 1] == 'x' || src[pos + 1] == 'X')) {
                pos += 2;
                bool any = false;
                for (;;) {
                    char h = src[pos];
                    int d = h >= '0' && h <= '9' ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10 : h >= 'A' && h <= 'F' ? h - 'A' + 10 : -1;
                    if (d < 0) break;
                    v = v * 16 + d;
                    ++pos;
                    any = true;
                }
                if (!any) fail("bad hex");
            } else {
                while (src[pos] >= '0' && src[pos] <= '9') v = v * 10 + (src[pos++] - '0');
            }
            emit(O_NUM, v);
        } else if (c == '-') {
            ++pos;
            primary();
            emit(O_NEG);
        } else if (c == '~') {
            ++pos;
            primary();
            emit(O_INV);
        } else if (c == '!') {
            ++pos;
            primary();
            emit(O_NOT);
        } else if (c == '+') {
            ++pos;
            primary();
        } else fail(c ? "unexpected symbol" : "unfinished");
    }
    void mul() {
        primary();
        for (;;) {
            if (eat("*")) { primary(); emit(O_MUL); }
            else if (eat("/")) { primary(); emit(O_DIV); }
            else if (eat("%")) { primary(); emit(O_MOD); }
            else return;
        }
    }
    void add() {
        mul();
        for (;;) {
            if (eat("+")) { mul(); emit(O_ADD); }
            else if (eat("-")) { mul(); emit(O_SUB); }
            else return;
        }
    }
    void shift() {
        add();
        for (;;) {
            if (eat("<<")) { add(); emit(O_SHL); }
            else if (eat(">>")) { add(); emit(O_SHR); }
            else return;
        }
    }
    void rel() {
        shift();
        for (;;) {
            if (eat("<=")) { shift(); emit(O_LE); }
            else if (eat(">=")) { shift(); emit(O_GE); }
            else if (eat("<")) { shift(); emit(O_LT); }
            else if (eat(">")) { shift(); emit(O_GT); }
            else return;
        }
    }
    void eq() {
        rel();
        for (;;) {
            if (eat("==")) { rel(); emit(O_EQ); }
            else if (eat("!=")) { rel(); emit(O_NE); }
            else return;
        }
    }
    void band() { eq(); while (eat("&")) { eq(); emit(O_AND); } }
    void bxor() { band(); while (eat("^")) { band(); emit(O_XOR); } }
    void bor() { bxor(); while (eat("|")) { bxor(); emit(O_OR); } }
    void ternary() {
        bor();
        if (eat("?")) {
            ternary();
            if (!eat(":")) return fail("missing :");
            ternary();
            emit(O_TERN);
        }
    }
    bool compile() {
        out->n = 0;
        ternary();
        ws();
        if (!err && src[pos]) fail("unexpected symbol");
        if (!err && maxDepth > 30) fail("too deep");
        return !err;
    }
};

const char *const PRESETS[] = {
    "t*(t>>5|t>>8)",
    "t*(42&t>>10)",
    "(t*5&t>>7)|(t*3&t>>10)",
    "t*((t>>12|t>>8)&63&t>>4)",
    "(t>>6|t|t>>(t>>16))*10+((t>>11)&7)",
    "t*(0xCA98>>(t>>9&14)&15)|t>>8",
    "(t>>7|t|t>>6)*10+4*(t&t>>13|t>>6)",
    "t>>4|t*(t>>11&3?7:5)&t>>3",
    "((t&4096)?((t*(t^t%255)|(t>>4))>>1):(t>>3)|((t&8192)?t<<2:t))",
};
constexpr int N_PRESETS = sizeof(PRESETS) / sizeof(PRESETS[0]);
const int RATES[] = {8000, 11025, 16000, 22050};
const char *RATE_NAMES[] = {"8k", "11k", "16k", "22k"};

struct State {
    char src[MAX_SRC + 1] = {};
    int len = 0, cur = 0;
    int preset = 0;
    Program progs[2];
    volatile Program *live = nullptr;
    Program view; // UI-side copy for the picture
    const char *err = nullptr;
    int errPos = 0;
    uint32_t castAt = 0;
    int rate = 0;
    volatile uint32_t t = 0;
    uint32_t frac = 0; // 16.16 step through t
    float dc = 0;
    dsp::Scope scope;
    int flip = 0;
} *S = nullptr;

void render(int16_t *out, int n) {
    State &s = *S;
    const Program *p = (const Program *)s.live;
    uint32_t step = (uint32_t)((uint64_t)RATES[s.rate] * 65536 / hal::RATE);
    for (int i = 0; i < n; ++i) {
        uint32_t v = p ? run(*p, s.t) & 255 : 128;
        float x = (v - 128.f) / 128.f;
        s.dc += (x - s.dc) * 0.0005f; // keep the speaker centred
        int16_t pcm = (int16_t)((x - s.dc) * 15000.f);
        out[i] = pcm;
        if ((i & 1) == 0) s.scope.put(pcm);
        s.frac += step;
        s.t += s.frac >> 16;
        s.frac &= 0xFFFF;
    }
}

void load(const char *text) {
    State &s = *S;
    strncpy(s.src, text, MAX_SRC);
    s.src[MAX_SRC] = 0;
    s.len = s.cur = strlen(s.src);
}

void cast() {
    State &s = *S;
    Program &dst = s.progs[s.flip ^ 1];
    Compiler c{s.src, 0, &dst};
    if (c.compile()) {
        s.flip ^= 1;
        s.live = &dst;
        s.view = dst;
        s.err = nullptr;
    } else {
        s.err = c.err;
        s.errPos = c.errPos;
    }
    s.castAt = hal::ms();
}

void enter() {
    if (!S) {
        S = new State();
        load(PRESETS[0]);
        cast();
    }
    hal::setAudio(render);
}
void leave() {}

bool onKey(const hal::Key &k) {
    State &s = *S;
    if (k.fn) {
        switch (k.nav) {
        case hal::N_LEFT: if (s.cur > 0) --s.cur; return true;
        case hal::N_RIGHT: if (s.cur < s.len) ++s.cur; return true;
        case hal::N_UP: s.rate = (s.rate + 1) % 4; return true;
        case hal::N_DOWN: s.rate = (s.rate + 3) % 4; return true;
        case hal::N_BACK: s.len = s.cur = 0; s.src[0] = 0; return true;
        default: break;
        }
        if (k.ch == 'r') { s.t = 0; return true; }
        return true;
    }
    switch (k.nav) {
    case hal::N_ENTER: cast(); return true;
    case hal::N_TAB:
        s.preset = (s.preset + 1) % N_PRESETS;
        load(PRESETS[s.preset]);
        cast();
        return true;
    case hal::N_BACK:
        if (s.cur > 0) {
            memmove(s.src + s.cur - 1, s.src + s.cur, s.len - s.cur + 1);
            --s.cur;
            --s.len;
        }
        return true;
    default: break;
    }
    if (k.ch >= 32 && k.ch < 127 && s.len < MAX_SRC) {
        memmove(s.src + s.cur + 1, s.src + s.cur, s.len - s.cur + 1);
        s.src[s.cur++] = k.ch;
        ++s.len;
    }
    return true;
}

void draw(uint32_t now) {
    State &s = *S;
    Canvas &cv = hal::canvas();
    cv.fillScreen(C_VOID);
    header("BEAT", "bytebeat");
    char b[48];
    snprintf(b, sizeof b, "%s t=%lu", RATE_NAMES[s.rate], (unsigned long)s.t);
    textRight(b, 178, 5, C_DIM);

    // the incantation's picture: columns of 256 samples, rows down each column
    const int px = 0, py = 20, pw = 240, ph = 54, cell = 2;
    uint32_t tNow = s.t;
    uint32_t colBase = (tNow >> 8);
    for (int c = 0; c < pw / cell; ++c) {
        uint32_t tc = (colBase - (pw / cell - 1) + c) << 8;
        for (int r = 0; r < ph / cell; ++r) {
            uint32_t v = s.view.n ? run(s.view, tc + r * 256 / (ph / cell)) & 255 : 0;
            cv.fillRect(px + c * cell, py + r * cell, cell, cell, ramp(v));
        }
    }
    cv.drawFastHLine(0, py + ph, 240, C_CLOT);

    // scope strip
    int w = s.scope.w;
    int ly = 0;
    for (int i = 0; i < 240; ++i) {
        int16_t a = s.scope.s[(w + i) & (dsp::Scope::N - 1)];
        int y = 84 - a / 1200;
        if (i) cv.drawLine(i - 1, ly, i, y, C_RUST);
        ly = y;
    }

    // editor: 3 lines of 38 chars
    frame(2, 92, 236, 31, s.err ? C_BLOOD : C_CLOT, s.err ? C_RUST : C_UMBER);
    const int COLS = 38;
    for (int line = 0; line < 3; ++line) {
        int start = line * COLS;
        if (start > s.len) break;
        char seg[COLS + 1];
        int n = s.len - start < COLS ? s.len - start : COLS;
        memcpy(seg, s.src + start, n);
        seg[n] = 0;
        text(seg, 6, 95 + line * 9, C_BONE);
    }
    if ((now / 350) & 1) {
        int cl = s.cur / COLS, cc = s.cur % COLS;
        if (cl < 3) cv.fillRect(6 + cc * 6, 95 + cl * 9 + 8, 5, 1, C_GLOW);
    }
    if (s.err) {
        int el = s.errPos / COLS, ec = s.errPos % COLS;
        if (el < 3) cv.drawRect(5 + ec * 6, 94 + el * 9, 7, 10, C_BLOOD);
        snprintf(b, sizeof b, "! %s", s.err);
        cv.fillRect(0, 124, 240, 11, C_VOID);
        text(b, 4, 126, C_RUST);
    } else if (now - s.castAt < 600) {
        cv.fillRect(0, 124, 240, 11, C_VOID);
        textCentre("- cast -", 120, 126, C_GLOW);
    } else {
        cv.fillRect(0, 124, 240, 11, C_VOID);
        textCentre("ent cast  tab preset  fn+;. rate", 120, 126, C_DIM);
    }
}
} // namespace

const Rite RITE_BEAT = {"BEAT", "beat", "bytebeat livecoder", enter, leave, onKey, draw};
