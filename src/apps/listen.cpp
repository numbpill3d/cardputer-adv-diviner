// LISTEN — receive-only channel monitor. The radio hops (or holds) across the
// 2.4GHz channels and counts frames; busy channels drone, quiet ones hiss. A
// bar spectrum shows management vs data traffic. Never transmits.
//
// keys  ;/.  move channel   enter hop/hold   space mute
#include "../dsp.h"
#include "../net.h"
#include "../rite.h"
#include "../theme.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

using namespace theme;

namespace {
struct State {
    uint32_t mgmt[14] = {}, data[14] = {};
    float ema[14] = {};
    int ch = 1, rssi = -90;
    bool hop = true, mute = false;
    uint32_t lastPoll = 0, lastHop = 0;
    dsp::Noise rng;
    dsp::SVF f;
    dsp::Osc drone;
    volatile float level = 0, pitch = 60;
} *S = nullptr;

void render(int16_t *out, int n) {
    State &s = *S;
    for (int i = 0; i < n; ++i) {
        if (s.mute) { out[i] = 0; continue; }
        if ((i & 63) == 0) {
            s.f.set(300.f + s.level * 2600.f, 0.8f);
            s.drone.hz(s.pitch);
        }
        float hiss = s.f.bp(s.rng.white()) * (0.03f + s.level * 0.18f);
        float tone = s.drone.sine() * s.level * 0.12f;
        out[i] = dsp::toPcm(hiss + tone);
    }
}

void enter() {
    if (!S) S = new State();
    S->f.set(1200, 0.8f);
    net::snifferBegin();
    net::snifferSetChannel(S->ch);
    hal::setAudio(render);
}
void leave() {}

bool onKey(const hal::Key &k) {
    State &s = *S;
    if (k.nav == hal::N_LEFT || k.nav == hal::N_UP) { s.ch = s.ch <= 1 ? 13 : s.ch - 1; s.hop = false; net::snifferSetChannel(s.ch); }
    else if (k.nav == hal::N_RIGHT || k.nav == hal::N_DOWN) { s.ch = s.ch >= 13 ? 1 : s.ch + 1; s.hop = false; net::snifferSetChannel(s.ch); }
    else if (k.nav == hal::N_ENTER) s.hop = !s.hop;
    else if (k.ch == ' ') s.mute = !s.mute;
    return true;
}

void draw(uint32_t now) {
    State &s = *S;
    Canvas &cv = hal::canvas();
    cv.fillScreen(C_VOID);
    header("LISTEN", "channels");

    if (s.hop && now - s.lastHop > 350) {
        s.ch = s.ch >= 13 ? 1 : s.ch + 1;
        net::snifferSetChannel(s.ch);
        s.lastHop = now;
    }
    if (now - s.lastPoll > 200) {
        uint32_t m[14], d[14];
        int r;
        net::snifferPoll(m, d, r);
        s.rssi = r;
        float total = 0;
        for (int c = 1; c <= 13; ++c) {
            s.mgmt[c] = m[c];
            s.data[c] = d[c];
            float v = m[c] + d[c];
            s.ema[c] = s.ema[c] * 0.6f + v * 0.4f;
            if (c == s.ch) total = v;
        }
        s.level = total > 40 ? 1.f : total / 40.f;
        s.pitch = 45 + s.ch * 7;
        s.lastPoll = now;
    }

    // spectrum: 13 channels across the width
    const int x0 = 10, bw = 16, base = 112, maxh = 78;
    float peak = 1;
    for (int c = 1; c <= 13; ++c) peak = fmaxf(peak, s.ema[c]);
    for (int c = 1; c <= 13; ++c) {
        int x = x0 + (c - 1) * bw;
        int hM = (int)(maxh * fminf(1.f, s.mgmt[c] / peak));
        int hD = (int)(maxh * fminf(1.f, (s.mgmt[c] + s.data[c]) / peak));
        bool here = c == s.ch;
        cv.fillRect(x, base - hD, bw - 3, hD, here ? C_BLOOD : C_CLOT);
        cv.fillRect(x, base - hM, bw - 3, hM, here ? C_EMBER : C_RUST);
        char n[4];
        snprintf(n, sizeof n, "%d", c);
        text(n, x + (c < 10 ? 2 : 0), base + 3, here ? C_GLOW : C_DIM);
        if (here) cv.drawRect(x - 1, 22, bw - 1, base - 22, C_UMBER);
    }
    cv.drawFastHLine(x0, base, bw * 13, C_DUSK);

    char b[48];
    snprintf(b, sizeof b, "ch %d  %s", s.ch, s.hop ? "hopping" : "held");
    text(b, 8, 22, C_BONE);
    snprintf(b, sizeof b, "%d dBm", s.rssi);
    textRight(b, 232, 22, s.rssi > -70 ? C_EMBER : C_DIM);
    snprintf(b, sizeof b, "mgmt %lu  data %lu", (unsigned long)s.mgmt[s.ch], (unsigned long)s.data[s.ch]);
    textCentre(b, 120, 30, C_DIM);
    footer(s.mute ? "MUTED spc  ent hop/hold ;. ch" : "spc mute  ent hop/hold  ;. ch");
}
} // namespace

const Rite RITE_LISTEN = {"LISTEN", "listen", "hear the channels", enter, leave, onKey, draw};
