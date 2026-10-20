// DRONE — generative drone box. Five slow voices hold a chord from the chosen
// scale and drift to new degrees on their own; a resonant low-pass breathes
// over them, wind and bells come and go, all through a dark feedback delay.
//
// keys  a w s e d f t g y h u j k : root note (piano row, a = C)
//       1-6 scale   z/x octave   ;/. brightness   ,// drift
//       n wind   b bells   space freeze   r reseed
#include "../dsp.h"
#include "../rite.h"
#include "../theme.h"
#include <stdio.h>
#include <string.h>

using namespace theme;

namespace {
struct Scale {
    const char *name;
    int8_t steps[7];
    int n;
};
const Scale SCALES[] = {
    {"phrygian", {0, 1, 3, 5, 7, 8, 10}, 7}, {"aeolian", {0, 2, 3, 5, 7, 8, 10}, 7},
    {"dorian", {0, 2, 3, 5, 7, 9, 10}, 7},   {"locrian", {0, 1, 3, 5, 6, 8, 10}, 7},
    {"hijaz", {0, 1, 4, 5, 7, 8, 10}, 7},    {"fifths", {0, 7, 0, 7, 0, 7, 0}, 2},
};
constexpr int N_SCALES = sizeof(SCALES) / sizeof(SCALES[0]);
const char *NOTE_NAMES[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

constexpr int NV = 5;
struct Voice {
    dsp::Osc a, b, sub;
    float hz = 110, target = 110;
    float amp = 0, ampTarget = 0.5f;
    float swellRate = 0.0002f;
    int degree = 0, octave = 0;
};

struct State {
    int root = 38;          // D2
    int scale = 0;
    float bright = 0.45f;   // 0..1
    float drift = 0.5f;     // 0..1
    bool wind = true, bells = true, freeze = false;
    Voice v[NV];
    dsp::SVF filt, windF, bellF;
    dsp::Osc lfo, lfo2;
    dsp::Noise rng;
    dsp::Osc bell;
    dsp::Decay bellEnv;
    float bellHz = 880;
    dsp::Delay<9000> dl;
    float dlLp = 0;
    uint32_t ctrl = 0, nextMove = 0;
    float cutoffNow = 600;
    dsp::Scope scope;
    volatile int lastMoved = -1;
    volatile uint32_t movedAt = 0;
} *S = nullptr;

float noteHz(int degree, int octave) {
    const Scale &sc = SCALES[S->scale];
    int d = ((degree % sc.n) + sc.n) % sc.n;
    int o = octave + (degree < 0 ? -1 : degree / sc.n);
    return dsp::midiHz(S->root + sc.steps[d] + 12 * o);
}

void retune(int i, bool snap) {
    Voice &v = S->v[i];
    v.target = noteHz(v.degree, v.octave);
    if (snap) v.hz = v.target;
}

void seed() {
    // root, fifth, octave-ish spread, then two wanderers higher up
    const int deg[NV] = {0, 4, 0, 2, 6};
    const int oct[NV] = {0, 0, 1, 1, 1};
    for (int i = 0; i < NV; ++i) {
        Voice &v = S->v[i];
        v.degree = deg[i] + (S->rng.next() % 3 == 0 ? 2 : 0);
        v.octave = oct[i];
        v.ampTarget = 0.25f + (S->rng.next() % 100) / 160.f;
        v.swellRate = 0.00008f + (S->rng.next() % 100) * 0.000003f;
        retune(i, true);
    }
    S->v[0].degree = 0;
    S->v[0].octave = 0;
    retune(0, true);
}

void moveVoice() {
    // pick a voice (never the root drone) and step it a little through the scale
    int i = 1 + S->rng.next() % (NV - 1);
    Voice &v = S->v[i];
    int step = (int)(S->rng.next() % 5) - 2;
    if (step == 0) step = 1;
    v.degree += step;
    if (v.degree > 9) v.degree -= 7;
    if (v.degree < -2) v.degree += 7;
    v.ampTarget = 0.15f + (S->rng.next() % 100) / 140.f;
    retune(i, false);
    S->lastMoved = i;
    S->movedAt = hal::ms();
}

void render(int16_t *out, int n) {
    State &s = *S;
    for (int i = 0; i < n; ++i) {
        if ((s.ctrl++ & 63) == 0) {
            // control rate: glides, swells, filter LFO, wandering, bells
            for (int k = 0; k < NV; ++k) {
                Voice &v = s.v[k];
                v.hz += (v.target - v.hz) * 0.0045f;
                v.a.hz(v.hz * 1.0031f);
                v.b.hz(v.hz * 0.9968f);
                v.sub.hz(v.hz * 0.5f);
                v.amp += (v.ampTarget - v.amp) * v.swellRate * 64;
                if (fabsf(v.amp - v.ampTarget) < 0.01f) v.ampTarget = 0.1f + (s.rng.next() % 100) / 120.f;
            }
            float l = s.lfo.sine();
            float l2 = s.lfo2.sine();
            float cut = 120.f + s.bright * s.bright * 2600.f * (1.f + 0.45f * l + 0.2f * l2);
            s.cutoffNow = cut;
            s.filt.set(cut, 0.35f + 0.25f * s.bright);
            s.windF.set(300.f + 900.f * (0.5f + 0.5f * l2), 0.85f);
            if (!s.freeze && s.ctrl > s.nextMove) {
                moveVoice();
                uint32_t gap = (uint32_t)((1.f - s.drift) * 9.f * hal::RATE) + hal::RATE / 2;
                s.nextMove = s.ctrl + gap / 2 + s.rng.next() % gap;
            }
            if (s.bells && s.rng.next() % (int)(hal::RATE / 64 * (3.5f - s.drift * 2.f)) == 0) {
                const Scale &sc = SCALES[s.scale];
                int d = s.rng.next() % sc.n;
                s.bellHz = dsp::midiHz(s.root + sc.steps[d] + 36 + (s.rng.next() & 1) * 12);
                s.bell.hz(s.bellHz);
                s.bellF.set(s.bellHz, 0.98f);
                s.bellEnv.trigger(0.5f);
            }
        }
        float mix = 0;
        for (int k = 0; k < NV; ++k) {
            Voice &v = s.v[k];
            float x = (v.a.saw() + v.b.saw()) * 0.5f + v.sub.sine() * 0.6f;
            mix += x * v.amp;
        }
        mix = s.filt.lp(mix * 0.32f);
        if (s.wind) mix += s.windF.bp(s.rng.white()) * 0.09f;
        float bl = s.bell.sine() * s.bellEnv.tick();
        mix += bl * 0.18f;
        // dark delay: two taps, low-passed feedback
        float wet = s.dl.tap(8999) * 0.55f + s.dl.tap(5311) * 0.3f;
        s.dlLp += (wet - s.dlLp) * 0.18f;
        s.dl.put(mix + s.dlLp * 0.62f);
        float o = mix * 0.8f + s.dlLp * 0.5f;
        int16_t p = dsp::toPcm(o * 0.9f);
        out[i] = p;
        if ((i & 3) == 0) s.scope.put(p);
    }
}

void enter() {
    if (!S) S = new State();
    S->lfo.hz(0.045f);
    S->lfo2.hz(0.013f);
    S->bellEnv.time(1.6f);
    S->bellF.set(880, 0.9f);
    seed();
    hal::setAudio(render);
}
void leave() {}

const char PIANO[] = "awsedftgyhujk";

bool onKey(const hal::Key &k) {
    State &s = *S;
    if (k.ch) {
        const char *p = strchr(PIANO, k.ch);
        if (p && k.ch) {
            int semis = p - PIANO;
            s.root = (s.root / 12) * 12 + semis;
            for (int i = 0; i < NV; ++i) retune(i, false);
            return true;
        }
        if (k.ch >= '1' && k.ch < '1' + N_SCALES) {
            s.scale = k.ch - '1';
            for (int i = 0; i < NV; ++i) retune(i, false);
            return true;
        }
    }
    switch (k.nav) {
    case hal::N_UP: s.bright = fminf(1.f, s.bright + 0.05f); return true;
    case hal::N_DOWN: s.bright = fmaxf(0.f, s.bright - 0.05f); return true;
    case hal::N_RIGHT: s.drift = fminf(1.f, s.drift + 0.1f); return true;
    case hal::N_LEFT: s.drift = fmaxf(0.f, s.drift - 0.1f); return true;
    default: break;
    }
    switch (k.ch) {
    case 'z': if (s.root >= 24) s.root -= 12; for (int i = 0; i < NV; ++i) retune(i, false); break;
    case 'x': if (s.root < 60) s.root += 12; for (int i = 0; i < NV; ++i) retune(i, false); break;
    case 'n': s.wind = !s.wind; break;
    case 'b': s.bells = !s.bells; break;
    case ' ': s.freeze = !s.freeze; break;
    case 'r': seed(); break;
    }
    return true;
}

void draw(uint32_t now) {
    State &s = *S;
    Canvas &cv = hal::canvas();
    cv.fillScreen(C_VOID);
    header("DRONE", "drone");

    // mandala: each voice is a ring of 7 nodes; radius from pitch, glow from amp
    const int cx = 70, cy = 75;
    float rot = now * 0.00012f;
    cv.drawCircle(cx, cy, 52, C_DUSK);
    for (int k = 0; k < NV; ++k) {
        const Voice &v = s.v[k];
        float r = 14 + log2f(v.hz / 30.f) * 9.f;
        if (r > 50) r = 50;
        uint8_t a = (uint8_t)fminf(255.f, 60 + v.amp * 260.f);
        uint16_t c = blend(k == s.lastMoved && now - s.movedAt < 1500 ? C_GLOW : C_EMBER, C_VOID, a);
        int nodes = 7;
        float ph = rot * (k & 1 ? -1.f : 1.f) * (1 + k * 0.3f);
        int px0 = 0, py0 = 0, fx = 0, fy = 0;
        for (int j = 0; j <= nodes; ++j) {
            float t = ph + j * 6.2831853f / nodes;
            int px = cx + (int)(cosf(t) * r), py = cy + (int)(sinf(t) * r);
            if (j) cv.drawLine(px0, py0, px, py, blend(C_BLOOD, C_VOID, a));
            else { fx = px; fy = py; }
            px0 = px;
            py0 = py;
            if (j < nodes) cv.fillCircle(px, py, 1, c);
        }
        (void)fx; (void)fy;
    }
    // polar scope in the middle
    int w = s.scope.w;
    int lx = 0, ly = 0;
    for (int i = 0; i <= 64; ++i) {
        int16_t v = s.scope.s[(w + i * 4) & (dsp::Scope::N - 1)];
        float r = 10 + v / 2400.f;
        float t = i * 6.2831853f / 64;
        int px = cx + (int)(cosf(t) * r), py = cy + (int)(sinf(t) * r);
        if (i) cv.drawLine(lx, ly, px, py, C_RUST);
        lx = px;
        ly = py;
    }

    // panel
    char b[40];
    int px = 132;
    frame(px - 4, 24, 110, 92, C_CLOT, C_UMBER);
    snprintf(b, sizeof b, "%s%d", NOTE_NAMES[s.root % 12], s.root / 12 - 1);
    text("root", px, 29, C_DIM);
    text(b, px + 40, 26, C_GLOW, 2);
    text("mode", px, 46, C_DIM);
    text(SCALES[s.scale].name, px + 40, 46, C_BONE);
    auto bar = [&](const char *label, float v, int y) {
        text(label, px, y, C_DIM);
        cv.drawRect(px + 40, y + 1, 60, 6, C_CLOT);
        cv.fillRect(px + 41, y + 2, (int)(58 * v), 4, C_RUST);
    };
    bar("light", s.bright, 58);
    bar("drift", s.drift, 70);
    snprintf(b, sizeof b, "%s %s %s", s.wind ? "WIND" : "wind", s.bells ? "BELL" : "bell",
             s.freeze ? "HOLD" : "flow");
    text(b, px, 84, s.freeze ? C_GLOW : C_EMBER);
    snprintf(b, sizeof b, "lp %4dhz", (int)s.cutoffNow);
    text(b, px, 96, C_DIM);
    footer("a-k root 1-6 mode ;. ,/ spc hold");
}
} // namespace

const Rite RITE_DRONE = {"DRONE", "drone", "a generative drone box", enter, leave, onKey, draw};
