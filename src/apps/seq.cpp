// SEQ — 16-step, 4-track groovebox: kick, snare, hat and an acid bass line
// (saw through a resonant low-pass with accent and slide). Euclidean fills
// make a track from a density; the bass line can be rolled from the scale.
//
// keys  ;./, move   enter toggle   space play/stop   [ ] tempo   ' swing
//       a-k (bass row) set note   z/x octave   v accent   l slide
//       q euclid fill (cycles)   r roll bass   m mute row   c clear row
#include "../dsp.h"
#include "../rite.h"
#include "../theme.h"
#include <stdio.h>
#include <string.h>

using namespace theme;

namespace {
constexpr int STEPS = 16, TRACKS = 4;
enum { KICK, SNARE, HAT, ACID };
const char *TRACK_NAMES[TRACKS] = {"kick", "snar", "hat", "acid"};
const int8_t MINOR[] = {0, 2, 3, 5, 7, 8, 10};

struct Step {
    bool on = false;
    bool accent = false;
    bool slide = false;
    int8_t note = 0; // semitones above the bass root (acid only)
};

static constexpr int NPAT = 4;
struct Bank {
    Step pat[TRACKS][STEPS];
    bool mute[TRACKS] = {};
    int euclid[TRACKS] = {4, 2, 8, 5};
};
struct State {
    Bank bank[NPAT];
    volatile int cur = 0; // active pattern (edit + play)
    bool song = false;    // chain A->B->C->D
    bool loaded = false;
    int bpm = 124;
    float swing = 0.08f;
    int bassRoot = 33; // A1
    volatile bool playing = true;
    volatile int step = -1;
    volatile uint32_t trigAt[TRACKS] = {};
    uint64_t sampleClock = 0, nextStepAt = 0;
    int cx = 0, cy = 0;

    // voices
    dsp::Osc kickOsc;
    dsp::Decay kickAmp, kickPitch;
    dsp::Noise noise;
    dsp::SVF snareF, hatF, acidF;
    dsp::Osc snareTone;
    dsp::Decay snareAmp, snareToneAmp, hatAmp;
    dsp::Osc acidOsc;
    dsp::Decay acidAmp, acidEnv;
    float acidHz = 55, acidTarget = 55, acidAccent = 0;
    bool acidSliding = false;
    float hatPrev = 0;
    dsp::Delay<4500> echo;
    dsp::Scope scope;
} *S = nullptr;

int samplesPerStep() { return hal::RATE * 60 / S->bpm / 4; }

void trigger(int t, const Step &st) {
    State &s = *S;
    if (s.bank[s.cur].mute[t]) return;
    s.trigAt[t] = hal::ms();
    float acc = st.accent ? 1.f : 0.72f;
    switch (t) {
    case KICK:
        s.kickAmp.trigger(acc);
        s.kickPitch.trigger(1.f);
        s.kickOsc.phase = 0;
        break;
    case SNARE:
        s.snareAmp.trigger(acc);
        s.snareToneAmp.trigger(acc * 0.7f);
        break;
    case HAT:
        s.hatAmp.time(st.accent ? 0.22f : 0.035f);
        s.hatAmp.trigger(acc * 0.8f);
        break;
    case ACID: {
        s.acidTarget = dsp::midiHz(s.bassRoot + st.note);
        if (!s.acidSliding) {
            s.acidHz = s.acidTarget;
            s.acidAmp.time(0.3f);
            s.acidAmp.trigger(1.f);
            s.acidEnv.trigger(1.f);
        }
        s.acidAccent = st.accent ? 1.f : 0.f;
        s.acidSliding = st.slide;
        break;
    }
    }
}

void render(int16_t *out, int n) {
    State &s = *S;
    for (int i = 0; i < n; ++i, ++s.sampleClock) {
        if (s.playing && s.sampleClock >= s.nextStepAt) {
            int next = (s.step + 1) % STEPS;
            s.step = next;
            if (next == 0 && s.song) s.cur = (s.cur + 1) % NPAT; // chain to next pattern
            Bank &B = s.bank[s.cur];
            for (int t = 0; t < TRACKS; ++t)
                if (B.pat[t][next].on) trigger(t, B.pat[t][next]);
            int len = samplesPerStep();
            int sw = (int)(len * s.swing);
            s.nextStepAt = s.sampleClock + (next & 1 ? len - sw : len + sw);
            if (!B.pat[ACID][next].on && !s.acidSliding) s.acidAmp.k = 0.9985f; // gate off: ~30ms release
        }
        if (!s.playing) s.nextStepAt = s.sampleClock;

        // kick: falling sine with a click
        float kp = s.kickPitch.tick();
        s.kickOsc.hz(42.f + 150.f * kp * kp);
        float kick = s.kickOsc.sine() * s.kickAmp.tick();
        // snare: band-passed noise plus a short body tone
        float sn = s.snareF.bp(s.noise.white()) * s.snareAmp.tick() * 1.6f +
                   s.snareTone.sine() * s.snareToneAmp.tick() * 0.5f;
        // hat: high-passed noise
        float nz = s.noise.white();
        float hp = nz - s.hatF.lp(nz);
        float hat = hp * s.hatAmp.tick() * 0.55f;
        // acid
        if ((s.sampleClock & 31) == 0) {
            s.acidHz += (s.acidTarget - s.acidHz) * (s.acidSliding ? 0.06f : 1.f);
            s.acidOsc.hz(s.acidHz);
            float env = s.acidEnv.v;
            float cut = 180.f + (900.f + 1800.f * s.acidAccent) * env;
            s.acidF.set(cut, 0.82f + 0.12f * s.acidAccent);
            s.acidEnv.tick();
        }
        float acid = dsp::softclip(s.acidF.lp(s.acidOsc.saw()) * 2.2f) * s.acidAmp.tick() * 0.42f;

        float dry = kick * 0.9f + sn * 0.42f + hat + acid;
        float wet = s.echo.tap(4499);
        s.echo.put(acid * 0.5f + sn * 0.12f + wet * 0.38f);
        int16_t p = dsp::toPcm((dry + wet * 0.35f) * 0.6f);
        out[i] = p;
        if ((i & 1) == 0) s.scope.put(p);
    }
}

void euclidFill(int t, int k) {
    // Bjorklund-style spread of k hits over 16 steps
    for (int i = 0; i < STEPS; ++i) {
        bool on = ((i * k) % STEPS) < k;
        S->bank[S->cur].pat[t][i].on = k > 0 && on;
        S->bank[S->cur].pat[t][i].accent = on && (i % 4 == 0);
    }
}

void rollBass() {
    dsp::Noise &r = S->noise;
    for (int i = 0; i < STEPS; ++i) {
        Step &st = S->bank[S->cur].pat[ACID][i];
        st.on = (r.next() % 100) < 62;
        st.note = MINOR[r.next() % 7] + ((r.next() % 5) == 0 ? 12 : 0);
        st.accent = (r.next() % 4) == 0;
        st.slide = (r.next() % 5) == 0;
    }
}


#ifdef GRIM_DEVICE
#include <Preferences.h>
void saveSong() {
    Preferences pr;
    if (!pr.begin("grimseq", false)) return;
    pr.putBytes("banks", S->bank, sizeof(S->bank));
    pr.putInt("bpm", S->bpm);
    pr.putInt("swing", (int)(S->swing * 1000));
    pr.putInt("root", S->bassRoot);
    pr.end();
}
void loadSong() {
    Preferences pr;
    if (!pr.begin("grimseq", true)) return;
    if (pr.getBytesLength("banks") == sizeof(S->bank)) {
        pr.getBytes("banks", S->bank, sizeof(S->bank));
        S->bpm = pr.getInt("bpm", S->bpm);
        S->swing = pr.getInt("swing", (int)(S->swing * 1000)) / 1000.f;
        S->bassRoot = pr.getInt("root", S->bassRoot);
    }
    pr.end();
}
#else
void saveSong() {}
void loadSong() {}
#endif

void enter() {
    if (!S) {
        S = new State();
        S->kickAmp.time(0.32f);
        S->kickPitch.time(0.035f);
        S->snareF.set(1900, 0.4f);
        S->snareTone.hz(185);
        S->snareAmp.time(0.13f);
        S->snareToneAmp.time(0.06f);
        S->hatF.set(3600, 0.1f);
        S->hatAmp.time(0.035f);
        S->acidAmp.time(0.3f);
        S->acidEnv.time(0.18f);
        euclidFill(KICK, 4);
        for (int i = 0; i < STEPS; ++i) S->bank[0].pat[SNARE][i].on = (i % 8) == 4;
        euclidFill(HAT, 8);
        for (int i = 0; i < STEPS; ++i)
            S->bank[0].pat[HAT][i].on = (i % 2) == 1, S->bank[0].pat[HAT][i].accent = i == 14;
        rollBass();
        loadSong(); // restore saved banks if any (device only)
    }
    hal::setAudio(render);
}
void leave() { saveSong(); }

const char PIANO[] = "awsedftgyhujk";

bool onKey(const hal::Key &k) {
    State &s = *S;
    Step &cell = s.bank[s.cur].pat[s.cy][s.cx];
    switch (k.nav) {
    case hal::N_UP: s.cy = (s.cy + TRACKS - 1) % TRACKS; return true;
    case hal::N_DOWN: s.cy = (s.cy + 1) % TRACKS; return true;
    case hal::N_LEFT: s.cx = (s.cx + STEPS - 1) % STEPS; return true;
    case hal::N_RIGHT: s.cx = (s.cx + 1) % STEPS; return true;
    case hal::N_ENTER: cell.on = !cell.on; return true;
    case hal::N_BACK: cell.on = false; return true;
    case hal::N_TAB: s.cur = (s.cur + 1) % NPAT; return true; // next pattern
    default: break;
    }
    if (s.cy == ACID && k.ch) {
        const char *p = strchr(PIANO, k.ch);
        if (p) {
            cell.note = (cell.note >= 12 ? 12 : 0) + (p - PIANO);
            cell.on = true;
            return true;
        }
    }
    switch (k.ch) {
    case ' ':
        s.playing = !s.playing;
        if (s.playing) s.step = -1;
        break;
    case '[': if (s.bpm > 60) s.bpm -= 2; break;
    case ']': if (s.bpm < 200) s.bpm += 2; break;
    case '\'': s.swing = s.swing > 0.2f ? 0.f : s.swing + 0.06f; break;
    case 'z': if (cell.note >= 12) cell.note -= 12; break;
    case 'x': if (cell.note < 12) cell.note += 12; break;
    case 'v': cell.accent = !cell.accent; break;
    case 'l': cell.slide = !cell.slide; break;
    case 'm': s.bank[s.cur].mute[s.cy] = !s.bank[s.cur].mute[s.cy]; break;
    case 'c': for (auto &st : s.bank[s.cur].pat[s.cy]) st.on = false; break;
    case 'q':
        s.bank[s.cur].euclid[s.cy] = (s.bank[s.cur].euclid[s.cy] % 9) + 1;
        euclidFill(s.cy, s.bank[s.cur].euclid[s.cy]);
        break;
    case 'r': rollBass(); break;
    case 'g': s.song = !s.song; break;                 // song/chain mode
    case 'y': {                                        // copy this pattern into the next slot
        int nx = (s.cur + 1) % NPAT;
        s.bank[nx] = s.bank[s.cur];
        s.cur = nx;
        break;
    }
    }
    return true;
}

void draw(uint32_t now) {
    State &s = *S;
    Canvas &cv = hal::canvas();
    cv.fillScreen(C_VOID);
    header("SEQ", "sequencer");

    const int gx = 30, gy = 24, cw = 13, ch = 17;
    int play = s.step;
    for (int t = 0; t < TRACKS; ++t) {
        int y = gy + t * (ch + 2);
        bool hot = now - s.trigAt[t] < 90;
        text(TRACK_NAMES[t], 3, y + 5, s.bank[s.cur].mute[t] ? C_UMBER : (hot ? C_GLOW : (t == s.cy ? C_EMBER : C_DIM)));
        for (int i = 0; i < STEPS; ++i) {
            int x = gx + i * cw;
            const Step &st = s.bank[s.cur].pat[t][i];
            uint16_t bg = (i / 4) & 1 ? C_ASH : C_DUSK;
            if (i == play && s.playing) bg = C_CLOT;
            cv.fillRect(x, y, cw - 2, ch, bg);
            if (st.on) {
                uint16_t c = s.bank[s.cur].mute[t] ? C_UMBER : (st.accent ? C_GLOW : C_RUST);
                if (i == play && s.playing && !s.bank[s.cur].mute[t]) c = C_BONE;
                if (t == ACID) {
                    int ny = y + ch - 3 - (st.note % 12);
                    cv.fillRect(x + 1, ny, cw - 4, 3, c);
                    if (st.note >= 12) cv.drawPixel(x + 5, y + 1, C_GLOW);
                    if (st.slide) cv.drawFastHLine(x + cw - 3, ny + 1, 3, C_EMBER);
                } else {
                    cv.fillRect(x + 2, y + 3, cw - 6, ch - 6, c);
                }
            }
            if (t == s.cy && i == s.cx) cv.drawRect(x - 1, y - 1, cw, ch + 2, C_GLOW);
        }
    }
    // mini scope under the grid
    int sy = 106;
    int w = s.scope.w;
    for (int i = 0; i < 100; ++i) {
        int16_t a = s.scope.s[(w + i * 2) & (dsp::Scope::N - 1)];
        int y = sy - a / 2600;
        cv.drawPixel(gx + i, y, C_BLOOD);
    }
    // pattern banks A-D row (top-right), active highlighted, song chain lit
    for (int pi = 0; pi < NPAT; ++pi) {
        int bx = 150 + pi * 15, by = 22;
        bool act = pi == s.cur;
        cv.fillRect(bx, by, 12, 10, act ? C_RUST : C_DUSK);
        if (s.song) cv.drawRect(bx, by, 12, 10, C_EMBER);
        char L[2] = {(char)('A' + pi), 0};
        text(L, bx + 3, by + 2, act ? C_GLOW : C_DIM);
    }
    if (s.song) text("SONG", 150, 34, C_EMBER);

    char b[48];
    snprintf(b, sizeof b, "%s %3dbpm sw%d", s.playing ? ">" : "#", s.bpm, (int)(s.swing * 100));
    text(b, 136, 102, s.playing ? C_EMBER : C_DIM);
    const Step &cell = s.bank[s.cur].pat[s.cy][s.cx];
    if (s.cy == ACID) {
        static const char *NN[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        int nn = s.bassRoot + cell.note;
        snprintf(b, sizeof b, "%s%d %s%s", NN[nn % 12], nn / 12 - 1, cell.accent ? "acc " : "", cell.slide ? "sld" : "");
    } else {
        snprintf(b, sizeof b, "e%d %s%s", s.bank[s.cur].euclid[s.cy], cell.accent ? "acc " : "", s.bank[s.cur].mute[s.cy] ? "muted" : "");
    }
    text(b, 136, 112, C_DIM);
    footer("tab:pat y:copy g:song r:roll q:fill");
}
} // namespace

const Rite RITE_SEQ = {"SEQ", "seq", "16 step groovebox + acid", enter, leave, onKey, draw};
