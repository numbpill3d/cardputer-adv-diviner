// VISIONS — pixel visions in the blood ramp: plasma, an Oblivion-gate
// tunnel, ash-fire, Daedric glyph rain, and a heptagram kaleidoscope.
// Each one hums a little; s toggles sound.
//
// keys  1-5 / tab / enter  next vision   ,/ speed   space pause   s sound
#include "../daedric_glyphs.h"
#include "../dsp.h"
#include "../rite.h"
#include "../theme.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

using namespace theme;

namespace {
constexpr int VW = 120, VH = 58, OY = 19; // half-res field, drawn 2x2
const char *NAMES[] = {"plasma", "gate", "ashfire", "rain", "kaleido"};
constexpr int N_VIS = 5;

struct State {
    int vis = 0;
    float t = 0, speed = 1.f;
    bool paused = false, sound = true;
    uint8_t ang[VH][VW], dist[VH][VW]; // tunnel tables
    bool tablesBuilt = false;
    uint8_t fire[VH + 1][VW];
    struct Drop {
        float y, v;
        char g;
        uint8_t len;
    } drops[24];
    // sound
    dsp::Noise rng;
    dsp::SVF windF;
    dsp::Osc lfo, tone, tone2;
    volatile float level = 0.5f;
    volatile int which = 0;
} *S = nullptr;

int8_t sin8(uint8_t a) { // -127..127
    static int8_t t[256];
    static bool built = false;
    if (!built) {
        for (int i = 0; i < 256; ++i) t[i] = (int8_t)(127 * sinf(i * 6.2831853f / 256));
        built = true;
    }
    return t[a];
}

void cell(int x, int y, uint16_t c) { hal::canvas().fillRect(x * 2, OY + y * 2, 2, 2, c); }

void plasma() {
    int t = (int)(S->t * 40);
    for (int y = 0; y < VH; ++y)
        for (int x = 0; x < VW; ++x) {
            int v = sin8(x * 3 + t) + sin8(y * 5 - t * 2 / 3) + sin8((x + y) * 2 + t / 2) +
                    sin8((int)(sqrtf((x - 60) * (x - 60) + (y - 29) * (y - 29) * 4) * 4) - t);
            uint8_t u = (uint8_t)((v + 508) * 255 / 1016);
            cell(x, y, ramp((uint8_t)(sin8(u + t / 3) + 128)));
        }
}

void buildTunnel() {
    for (int y = 0; y < VH; ++y)
        for (int x = 0; x < VW; ++x) {
            float dx = x - VW / 2 + 0.5f, dy = (y - VH / 2 + 0.5f) * 1.1f;
            float d = sqrtf(dx * dx + dy * dy) + 0.5f;
            S->dist[y][x] = (uint8_t)((int)(900.f / d) & 255);
            S->ang[y][x] = (uint8_t)(128 * atan2f(dy, dx) / 3.14159f);
        }
    S->tablesBuilt = true;
}

void gate() {
    if (!S->tablesBuilt) buildTunnel();
    int t = (int)(S->t * 60);
    for (int y = 0; y < VH; ++y)
        for (int x = 0; x < VW; ++x) {
            uint8_t d = S->dist[y][x], a = S->ang[y][x];
            uint8_t u = d + t, v = a * 2 + t / 4;
            int pat = ((u >> 4) ^ (v >> 4)) & 1 ? 150 : 40;
            pat += sin8(u * 2 + v) / 3;
            int dx = x - VW / 2, dy = y - VH / 2;
            int fade = dx * dx + dy * dy * 4; // darker in the throat
            int k = fade > 2400 ? 255 : fade * 255 / 2400;
            int val = pat * k / 255;
            cell(x, y, ramp((uint8_t)(val < 0 ? 0 : val > 255 ? 255 : val)));
        }
    // the gate's rim
    hal::canvas().drawEllipse(120, OY + 58, 112, 56, C_BLOOD);
}

void ashfire() {
    auto &f = S->fire;
    for (int x = 0; x < VW; ++x) f[VH][x] = (S->rng.next() % 100) < 55 ? 255 : 60;
    for (int y = 0; y < VH; ++y)
        for (int x = 0; x < VW; ++x) {
            int r = S->rng.next() % 3;
            int sx = x + r - 1;
            if (sx < 0) sx = 0;
            if (sx >= VW) sx = VW - 1;
            int y2 = y + 2 > VH ? VH : y + 2;
            int v = (f[y + 1][sx] * 2 + f[y + 1][x] + f[y2][x]) / 4;
            v -= 3 + (S->rng.next() & 3);
            f[y][x] = v < 0 ? 0 : v;
        }
    for (int y = 0; y < VH; ++y)
        for (int x = 0; x < VW; ++x) cell(x, y, ramp(f[y][x]));
}

void glyphRain() {
    Canvas &cv = hal::canvas();
    cv.fillRect(0, OY, 240, VH * 2, C_VOID);
    const int lanes = 24, lw = 10;
    for (int i = 0; i < lanes; ++i) {
        auto &d = S->drops[i];
        if (d.v == 0 || d.y > 140) {
            d.y = -(float)(S->rng.next() % 80);
            d.v = 0.6f + (S->rng.next() % 100) / 70.f;
            d.len = 3 + S->rng.next() % 5;
        }
        if (!S->paused) d.y += d.v * S->speed;
        for (int j = 0; j < d.len; ++j) {
            int y = (int)d.y - j * 16;
            if (y < OY - 14 || y > 134) continue;
            char g[2] = {(char)('A' + ((i * 7 + j * 3 + (int)(d.y / 16)) % 26)), 0};
            uint16_t c = j == 0 ? C_GLOW : blend(C_RUST, C_VOID, 255 - j * 255 / d.len);
            glyphs(DAEDRIC_S, g, i * lw, y, c);
        }
    }
}

void kaleido() {
    Canvas &cv = hal::canvas();
    cv.fillRect(0, OY, 240, VH * 2, C_VOID);
    float t = S->t;
    const int cx = 120, cy = OY + 58;
    for (int ring = 0; ring < 5; ++ring) {
        float r = 12 + ring * 11 + 4 * sinf(t * 1.3f + ring);
        float rot = t * (ring & 1 ? -0.4f : 0.3f) + ring * 0.4f;
        int step = 2 + ring % 2;
        int px[7], py[7];
        for (int i = 0; i < 7; ++i) {
            float a = rot + i * 6.2831853f / 7;
            px[i] = cx + (int)(cosf(a) * r * 1.6f);
            py[i] = cy + (int)(sinf(a) * r);
        }
        uint16_t c = ramp((uint8_t)(90 + ring * 30 + 40 * sinf(t + ring)));
        for (int i = 0; i < 7; ++i) cv.drawLine(px[i], py[i], px[(i + step) % 7], py[(i + step) % 7], c);
        for (int i = 0; i < 7; ++i) diamond(px[i], py[i], 1 + (ring == 4), C_GLOW);
    }
    // seven mirrored sparks
    for (int i = 0; i < 7; ++i) {
        float a = t * 0.9f + i * 6.2831853f / 7;
        float r = 30 + 22 * sinf(t * 2.1f);
        cv.fillCircle(cx + (int)(cosf(a) * r * 1.6f), cy + (int)(sinf(a) * r), 2, C_BONE);
    }
}

void render(int16_t *out, int n) {
    State &s = *S;
    for (int i = 0; i < n; ++i) {
        float x = 0;
        if (s.sound) {
            float l = s.lfo.sine();
            if ((i & 31) == 0) s.windF.set(250.f + 700.f * (0.5f + 0.5f * l) * (1 + s.which * 0.3f), 0.9f);
            x = s.windF.bp(s.rng.white()) * 0.16f + (s.tone.sine() + s.tone2.sine()) * 0.03f * (1.2f + l);
        }
        out[i] = dsp::toPcm(x);
    }
}

void retone() {
    static const float ROOTS[N_VIS] = {55.f, 41.2f, 36.7f, 61.7f, 49.f};
    S->tone.hz(ROOTS[S->vis]);
    S->tone2.hz(ROOTS[S->vis] * 1.498f);
    S->which = S->vis;
}

void enter() {
    if (!S) S = new State();
    S->lfo.hz(0.07f);
    retone();
    hal::setAudio(render);
}
void leave() {}

bool onKey(const hal::Key &k) {
    State &s = *S;
    if (k.ch >= '1' && k.ch < '1' + N_VIS) s.vis = k.ch - '1';
    else if (k.nav == hal::N_TAB || k.nav == hal::N_ENTER || k.nav == hal::N_DOWN) s.vis = (s.vis + 1) % N_VIS;
    else if (k.nav == hal::N_UP) s.vis = (s.vis + N_VIS - 1) % N_VIS;
    else if (k.nav == hal::N_RIGHT) s.speed = fminf(4.f, s.speed + 0.25f);
    else if (k.nav == hal::N_LEFT) s.speed = fmaxf(0.25f, s.speed - 0.25f);
    else if (k.ch == ' ') s.paused = !s.paused;
    else if (k.ch == 's') s.sound = !s.sound;
    retone();
    return true;
}

void draw(uint32_t) {
    State &s = *S;
    Canvas &cv = hal::canvas();
    if (!s.paused) s.t += 0.033f * s.speed;
    switch (s.vis) {
    case 0: plasma(); break;
    case 1: gate(); break;
    case 2: ashfire(); break;
    case 3: glyphRain(); break;
    case 4: kaleido(); break;
    }
    cv.fillRect(0, 0, 240, OY, C_VOID);
    header("VISIONS", "visions");
    textRight(NAMES[s.vis], 178, 5, C_DIM);
}
} // namespace

const Rite RITE_VISIONS = {"VISIONS", "visions", "plasma, gate, fire, glyph rain", enter, leave, onKey, draw};
