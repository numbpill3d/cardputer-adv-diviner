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
#ifdef GRIM_DEVICE
#include <esp_system.h>
#endif

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

struct TonePreset {
    const char *name;
    uint8_t shape; // 0 saw, 1 sine, 2 triangle, 3 square
    float detune;
    float sub;
    float cutoff;
    float resonance;
    float lfo;
    float lfo2;
    float delay;
    float bellTime;
};
const TonePreset TONES[] = {
    {"warm",   0, 0.0031f, 0.60f, 1.00f, 0.35f, 0.045f, 0.013f, 0.62f, 1.60f},
    {"hollow", 1, 0.0014f, 0.95f, 0.72f, 0.58f, 0.027f, 0.009f, 0.48f, 2.20f},
    {"glass",  2, 0.0060f, 0.25f, 1.35f, 0.22f, 0.071f, 0.021f, 0.68f, 1.10f},
    {"pulse",  3, 0.0022f, 0.12f, 0.82f, 0.70f, 0.038f, 0.017f, 0.42f, 1.35f},
    {"abyss",  0, 0.0018f, 1.05f, 0.48f, 0.76f, 0.018f, 0.006f, 0.78f, 2.80f},
};
constexpr int N_TONES = sizeof(TONES) / sizeof(TONES[0]);

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
    int tone = 0;
    float bright = 0.45f;   // 0..1
    float drift = 0.5f;     // 0..1
    bool wind = true, bells = true, freeze = false;
    bool safeMode = false;
    float windAmp = 1;      // eases toward the wind toggle
    Voice v[NV];
    dsp::SVF filt, windF, bellF;
    dsp::Osc lfo, lfo2;
    dsp::Noise rng;
    dsp::Osc bell;
    dsp::Decay bellEnv;
    float bellHz = 880;
    // Keep the feedback buffer below 20 KB on the PSRAM-less Cardputer. The
    // old 9000-sample delay used 36 KB by itself and could exhaust/fragment
    // heap after visiting radio-heavy rites before opening DRONE.
    dsp::Delay<4096> dl;
    float dlLp = 0;
    uint32_t ctrl = 0, nextMove = 0;
    float cutoffNow = 600;
    // Small visual meters shared from the audio callback. They deliberately
    // lag the signal: the display should breathe with the sound, not jitter at
    // the 24 kHz sample rate.
    volatile float visualEnergy = 0;
    volatile float visualWind = 0;
    volatile float visualBell = 0;
    volatile float visualDelay = 0;
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

const TonePreset &tonePreset() { return TONES[S->tone]; }

void retune(int i, bool snap) {
    Voice &v = S->v[i];
    v.target = noteHz(v.degree, v.octave);
    if (snap) v.hz = v.target;
}

void seed(bool snap) {
    // root, fifth, octave-ish spread, then two wanderers higher up
    const int deg[NV] = {0, 4, 0, 2, 6};
    const int oct[NV] = {0, 0, 1, 1, 1};
    for (int i = 0; i < NV; ++i) {
        Voice &v = S->v[i];
        v.degree = deg[i] + (S->rng.next() % 3 == 0 ? 2 : 0);
        v.octave = oct[i];
        v.ampTarget = 0.25f + (S->rng.next() % 100) / 160.f;
        v.swellRate = 0.00008f + (S->rng.next() % 100) * 0.000003f;
        retune(i, snap);
    }
    S->v[0].degree = 0;
    S->v[0].octave = 0;
    retune(0, snap);
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
    const TonePreset &tp = tonePreset();
    for (int i = 0; i < n; ++i) {
        if ((s.ctrl++ & 63) == 0) {
            // control rate: glides, swells, filter LFO, wandering, bells
            for (int k = 0; k < NV; ++k) {
                Voice &v = s.v[k];
                v.hz += (v.target - v.hz) * 0.0045f;
                v.a.hz(v.hz * (1.f + tp.detune));
                v.b.hz(v.hz * (1.f - tp.detune));
                v.sub.hz(v.hz * 0.5f);
                v.amp += (v.ampTarget - v.amp) * v.swellRate * 64;
                if (fabsf(v.amp - v.ampTarget) < 0.01f) v.ampTarget = 0.1f + (s.rng.next() % 100) / 120.f;
            }
            float l = s.lfo.sine();
            float l2 = s.lfo2.sine();
            float cut = 120.f + s.bright * s.bright * 2600.f * tp.cutoff * (1.f + 0.45f * l + 0.2f * l2);
            s.cutoffNow = cut;
            s.filt.set(cut, tp.resonance + 0.25f * s.bright);
            s.windF.set(300.f + 900.f * (0.5f + 0.5f * l2), 0.85f);
            s.windAmp += ((s.wind ? 1.f : 0.f) - s.windAmp) * 0.006f;
            if (!s.freeze && s.ctrl > s.nextMove) {
                moveVoice();
                uint32_t gap = (uint32_t)((1.f - s.drift) * 9.f * hal::RATE) + hal::RATE / 2;
                s.nextMove = s.ctrl + gap / 2 + s.rng.next() % gap;
            }
            if (s.bells && !s.safeMode && s.rng.next() % (int)(hal::RATE / 64 * (3.5f - s.drift * 2.f)) == 0) {
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
            float a, b;
            switch (tp.shape) {
            case 1: a = v.a.sine(); b = v.b.sine(); break;
            case 2: a = v.a.tri(); b = v.b.tri(); break;
            case 3: a = v.a.square(); b = v.b.square(); break;
            default: a = v.a.saw(); b = v.b.saw(); break;
            }
            float x = (a + b) * 0.5f + v.sub.sine() * tp.sub;
            mix += x * v.amp;
        }
        mix = s.filt.lp(mix * 0.32f);
        if (s.windAmp > 0.001f) mix += s.windF.bp(s.rng.white()) * 0.09f * s.windAmp;
        float bl = s.bell.sine() * s.bellEnv.tick();
        mix += bl * 0.18f;
        // dark delay: two taps, low-passed feedback
        float wet = s.safeMode ? 0.f : s.dl.tap(4095) * (tp.delay * 0.89f) + s.dl.tap(3072) * (tp.delay * 0.48f);
        s.dlLp += (wet - s.dlLp) * 0.18f;
        s.dl.put(mix + s.dlLp * tp.delay);
        float o = mix * 0.8f + s.dlLp * 0.5f;
        s.visualEnergy += (fabsf(o) - s.visualEnergy) * 0.0035f;
        s.visualWind += (fabsf(s.windAmp > 0.001f ? s.windF.band : 0.f) - s.visualWind) * 0.008f;
        s.visualBell = s.bellEnv.v;
        s.visualDelay += (fabsf(s.dlLp) - s.visualDelay) * 0.006f;
        int16_t p = dsp::toPcm(o * 0.9f);
        out[i] = p;
        if ((i & 3) == 0) s.scope.put(p);
    }
}

void enter() {
    if (!S) {
        S = new State();
        if (!S) {
            // Do not let an exhausted heap turn opening DRONE into a reboot.
            hal::setAudio(nullptr);
            return;
        }
    }
    S->lfo.hz(TONES[S->tone].lfo);
    S->lfo2.hz(TONES[S->tone].lfo2);
    S->bellEnv.time(TONES[S->tone].bellTime);
    S->bellF.set(880, 0.9f);
#ifdef GRIM_DEVICE
    // Radio rites can leave the PSRAM-less S3 heap fragmented. Safe mode keeps
    // the core drone alive while dropping bells and feedback if headroom is low.
    S->safeMode = esp_get_free_heap_size() < 60000;
#else
    S->safeMode = false;
#endif
    seed(true);
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
    case 'p':
        s.tone = (s.tone + 1) % N_TONES;
        s.lfo.hz(TONES[s.tone].lfo);
        s.lfo2.hz(TONES[s.tone].lfo2);
        s.bellEnv.time(TONES[s.tone].bellTime);
        break;
    case 'q': s.safeMode = !s.safeMode; break;
    case ' ': s.freeze = !s.freeze; break;
    case 'r': seed(false); break;
    }
    return true;
}

void draw(uint32_t now) {
    State &s = *S;
    Canvas &cv = hal::canvas();
    cv.fillScreen(C_VOID);
    header("DRONE", "drone");

    // The left side stays close to the original mandala: each voice is a
    // geometric ring of seven nodes. Pitch sets its radius, amplitude sets its
    // glow, and drift only gives the figure a gentle orbital motion.
    const int cx = 70, cy = 74;
    const float t = now * 0.001f;
    float energy = fminf(1.f, s.visualEnergy * 3.2f);
    float cutoff = fminf(1.f, s.cutoffNow / 2800.f);
    float breath = 1.f + 0.08f * sinf(t * 1.4f) + 0.14f * energy;
    int shell = (int)((25.f + cutoff * 24.f + energy * 6.f) * breath);

    // Filter cutoff is the creature's breathing aperture. Delay feedback and
    // bells leave geometric echoes instead of turning the image into noise.
    cv.drawCircle(cx, cy, shell + 5, blend(C_BLOOD, C_VOID, (uint8_t)(35 + cutoff * 55)));
    cv.drawCircle(cx, cy, shell, blend(C_DUSK, C_VOID, (uint8_t)(100 + cutoff * 80)));
    if (s.visualDelay > 0.015f) {
        for (int e = 1; e <= 3; ++e) {
            int ghost = shell + e * 7 + (int)(s.visualDelay * e * 9.f);
            uint8_t echoA = (uint8_t)fminf(130.f, 24.f + s.visualDelay * (180.f - e * 35.f));
            cv.drawCircle(cx, cy, ghost, blend(C_RUST, C_VOID, echoA));
        }
    }

    for (int k = 0; k < NV; ++k) {
        const Voice &v = s.v[k];
        float pitch = fminf(1.f, fmaxf(0.f, log2f(v.hz / 45.f) / 4.8f));
        float amp = fminf(1.f, v.amp * 0.95f);
        float orbit = 13.f + pitch * 29.f + amp * 5.f;
        float phase = t * ((k & 1) ? -0.10f : 0.12f) * (0.65f + s.drift) + k * 1.2566f;
        uint8_t alpha = (uint8_t)fminf(255.f, 70.f + amp * 170.f + energy * 45.f);
        bool moved = k == s.lastMoved && now - s.movedAt < 1500;
        uint16_t body = blend(moved ? C_GLOW : C_EMBER, C_VOID, alpha);
        uint16_t trail = blend(k == 0 ? C_RUST : C_BLOOD, C_VOID, alpha / 2 + 20);

        int nodes = 7;
        int px0 = 0, py0 = 0;
        for (int j = 0; j <= nodes; ++j) {
            float a = phase + j * 6.2831853f / nodes;
            float rr = orbit * (1.f + 0.025f * sinf(t * 1.7f + k));
            int x = cx + (int)(cosf(a) * rr);
            int y = cy + (int)(sinf(a) * rr * 0.72f);
            if (j) cv.drawLine(px0, py0, x, y, trail);
            px0 = x;
            py0 = y;
            if (j < nodes) {
                int glow = amp > 0.6f ? 2 : 1;
                cv.fillCircle(x, y, glow + 2, blend(C_RUST, C_VOID, alpha / 3));
                cv.fillCircle(x, y, glow, body);
            }
        }
    }

    // A small polar waveform keeps the center tied to the actual signal.
    int w = s.scope.w;
    int lx = 0, ly = 0;
    for (int i = 0; i <= 64; ++i) {
        int16_t sample = s.scope.s[(w + i * 4) & (dsp::Scope::N - 1)];
        float radius = 6.f + sample / 4200.f * (5.f + energy * 7.f);
        float a = i * 6.2831853f / 64.f + t * 0.08f;
        int x = cx + (int)(cosf(a) * radius);
        int y = cy + (int)(sinf(a) * radius);
        if (i) cv.drawLine(lx, ly, x, y, C_GLOW);
        lx = x;
        ly = y;
    }
    if (s.visualBell > 0.02f) {
        float ring = 10.f + (1.f - s.visualBell) * 38.f;
        uint8_t ringAlpha = (uint8_t)fminf(220.f, s.visualBell * 300.f);
        cv.drawCircle(cx, cy, (int)ring, blend(C_GLOW, C_VOID, ringAlpha));
        cv.drawCircle(cx, cy, (int)(ring + 3), blend(C_RUST, C_VOID, ringAlpha / 2));
    }

    // panel
    char b[40];
    int px = 132;
    frame(px - 4, 24, 110, 92, C_CLOT, C_UMBER);
    snprintf(b, sizeof b, "%s%d", NOTE_NAMES[s.root % 12], s.root / 12 - 1);
    text("root", px, 29, C_DIM);
    text(b, px + 40, 26, C_GLOW, 2);
    text("tone", px, 46, C_DIM);
    text(TONES[s.tone].name, px + 40, 46, C_BONE);
    text("mode", px, 58, C_DIM);
    text(SCALES[s.scale].name, px + 40, 58, C_BONE);
    auto bar = [&](const char *label, float v, int y) {
        text(label, px, y, C_DIM);
        cv.drawRect(px + 40, y + 1, 60, 6, C_CLOT);
        cv.fillRect(px + 41, y + 2, (int)(58 * v), 4, C_RUST);
    };
    bar("light", s.bright, 70);
    bar("drift", s.drift, 82);
    snprintf(b, sizeof b, "%s %s %s", s.safeMode ? "SAFE" : (s.wind ? "WIND" : "wind"), s.bells ? "BELL" : "bell",
             s.freeze ? "HOLD" : "flow");
    text(b, px, 96, s.safeMode ? C_GLOW : (s.freeze ? C_GLOW : C_EMBER));
    snprintf(b, sizeof b, "lp %4dhz", (int)s.cutoffNow);
    text(b, px, 108, C_DIM);
    footer("a-k root 1-6 mode p tone ;. ,/ spc hold");
}
} // namespace

const Rite RITE_DRONE = {"DRONE", "drone", "a generative drone box", enter, leave, onKey, draw};
