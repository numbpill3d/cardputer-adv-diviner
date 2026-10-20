// PULSE — a motion/presence sense built on Wi-Fi CSI. Bodies moving through
// the radio field between you and the AP disturb the subcarriers; PULSE reads
// that disturbance as motion. Needs an active link (join one in LINK first).
// Experimental: CSI is noisy and depends on the AP and the room.
//
// keys  ;/.  sensitivity   l set still-baseline   space mute   m mono
#include "../dsp.h"
#include "../net.h"
#include "../rite.h"
#include "../theme.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

using namespace theme;

namespace {
constexpr int WF_W = 116, WF_H = 70, NB = 52;
struct State {
    uint8_t wf[WF_H][WF_W]; // waterfall ring (column = time)
    int col = 0;
    float bins[NB] = {};
    float motion = 0, baseline = 0.06f, sens = 1.f;
    bool mute = false, mono = false, online = false;
    uint32_t lastCol = 0;
    dsp::Osc tick, hum;
    dsp::Decay tickEnv;
    dsp::Noise rng;
    volatile float level = 0;
    volatile int tickReq = 0;
    float tickAccum = 0;
} *S = nullptr;

void render(int16_t *out, int n) {
    State &s = *S;
    if (s.tickReq) { s.tickReq = 0; s.tick.hz(1800); s.tickEnv.trigger(0.4f); }
    for (int i = 0; i < n; ++i) {
        if (s.mute) { out[i] = 0; continue; }
        float x = s.hum.sine() * 0.015f * (0.5f + s.level);
        x += s.tick.sine() * s.tickEnv.tick() * 0.3f;
        out[i] = dsp::toPcm(x);
    }
}

void enter() {
    if (!S) S = new State();
    S->hum.hz(70);
    S->tickEnv.time(0.03f);
    net::wifiBegin();
    hal::setAudio(render);
}
void leave() { net::csiEnd(); }

bool onKey(const hal::Key &k) {
    State &s = *S;
    if (k.nav == hal::N_UP || k.nav == hal::N_RIGHT) s.sens = fminf(4.f, s.sens + 0.25f);
    else if (k.nav == hal::N_DOWN || k.nav == hal::N_LEFT) s.sens = fmaxf(0.25f, s.sens - 0.25f);
    else if (k.ch == 'l') s.baseline = s.motion; // mark "this is still"
    else if (k.ch == ' ') s.mute = !s.mute;
    else if (k.ch == 'm') s.mono = !s.mono;
    return true;
}

void draw(uint32_t now) {
    State &s = *S;
    Canvas &cv = hal::canvas();
    cv.fillScreen(C_VOID);
    header("PULSE", "csi sense");

    if (net::wifiState() != net::WST_ONLINE) {
        s.online = false;
        textCentre("no link", 120, 56, C_BLOOD);
        textCentre("join a network in LINK;", 120, 70, C_DIM);
        textCentre("csi needs an active connection", 120, 80, C_DIM);
        footer("csi motion sense");
        return;
    }
    if (!s.online) { net::csiBegin(); s.online = true; }

    float m;
    int nb = net::csiRead(m, s.bins, NB);
    float rel = (m - s.baseline) * s.sens;
    if (rel < 0) rel = 0;
    if (rel > 1) rel = 1;
    s.motion = m;
    s.level = rel;

    // geiger-style ticks: faster with more motion
    s.tickAccum += rel * rel * 0.5f;
    if (s.tickAccum >= 1.f) { s.tickAccum = 0; s.tickReq = 1; }

    // scroll waterfall one column per ~60ms
    if (nb > 0 && now - s.lastCol > 60) {
        s.lastCol = now;
        for (int y = 0; y < WF_H; ++y) {
            int bi = y * nb / WF_H;
            float v = s.bins[bi];
            if (v < 0) v = 0; if (v > 1) v = 1;
            s.wf[y][s.col] = (uint8_t)(v * 255);
        }
        s.col = (s.col + 1) % WF_W;
    }
    // draw waterfall (left)
    const int wx = 6, wy = 22;
    for (int x = 0; x < WF_W; ++x) {
        int sx = (s.col + x) % WF_W;
        for (int y = 0; y < WF_H; ++y) {
            uint8_t v = s.wf[y][sx];
            uint16_t c = s.mono ? blend(C_BONE, C_VOID, v) : ramp(v);
            cv.drawPixel(wx + x, wy + y, c);
        }
    }
    frame(wx - 1, wy - 1, WF_W + 2, WF_H + 2, C_CLOT, C_UMBER);

    // motion meter (right)
    int px = 130;
    const char *state = rel < 0.12f ? "still" : rel < 0.4f ? "stirring" : rel < 0.7f ? "MOTION" : "PRESENCE";
    uint16_t sc = rel < 0.12f ? C_DIM : rel < 0.4f ? C_RUST : rel < 0.7f ? C_EMBER : C_GLOW;
    textCentre(state, px + 52, 26, sc, rel >= 0.4f ? 2 : 1);

    // vertical bar
    int bx = px + 6, by = 44, bh = 64;
    cv.drawRect(bx, by, 14, bh, C_CLOT);
    int fill = (int)(bh * rel);
    for (int i = 0; i < fill; ++i) cv.fillRect(bx + 1, by + bh - 1 - i, 12, 1, ramp((uint8_t)(i * 255 / bh)));

    // a pulsing ring that reacts to motion
    int rx = px + 70, ry = 76;
    float pr = 8 + rel * 22 + 2 * sinf(now * 0.01f);
    for (int r = (int)pr; r > 2; r -= 4) cv.drawCircle(rx, ry, r, blend(sc, C_VOID, 80 + r * 3));
    cv.fillCircle(rx, ry, 3, sc);

    char b[40];
    snprintf(b, sizeof b, "sens %.1fx", s.sens);
    text(b, px, 112, C_DIM);
    snprintf(b, sizeof b, "raw %.2f", s.motion);
    textRight(b, 236, 112, C_DIM);
    footer(s.mute ? "MUTE m  ;. sens  l baseline" : "spc mute  ;. sens  l baseline");
}
} // namespace

const Rite RITE_PULSE = {"PULSE", "pulse", "csi motion / presence", enter, leave, onKey, draw};
