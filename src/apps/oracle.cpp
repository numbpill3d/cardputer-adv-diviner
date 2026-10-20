// ORACLE — draw three runes. Each Daedric letter carries an omen; the three
// read as past / present / wired. Enter casts (with a chime per rune), and
// the oracle types out a line of its own between readings.
//
// keys  enter cast   space next utterance
#include "../daedric_glyphs.h"
#include "../dsp.h"
#include "../rite.h"
#include "../theme.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

using namespace theme;

namespace {
// One omen per letter, A..Z. Written for this grimoire.
const char *const OMENS[26] = {
    "the ash remembers",  "a door left ajar",   "static in the blood", "the wheel turns",
    "an ember waits",     "a name unspoken",    "the signal returns",  "hollow mountain",
    "a mirror of ash",    "the long night",     "keys without locks",  "a voice on the wire",
    "the moon breaks",    "nothing is lost",    "the dreamer wakes",   "a debt of salt",
    "the quiet protocol", "red stars falling",  "seven pointed",       "the tower hums",
    "unbound vessel",     "the last packet",    "a choir of relays",   "crossed wires",
    "the shrine is lit",  "zero, and again",
};
const char *const UTTERANCES[] = {
    "no one is truly alone in the wired.",
    "the shrine keeps what the hand forgets.",
    "every signal is a prayer to someone.",
    "ash is only fire that has finished speaking.",
    "the wheel has seven spokes. count again.",
    "what you flash, you become.",
    "present day. present time.",
    "a vessel waits in every partition.",
    "listen past the noise floor.",
    "the red moon does not set. it waits.",
};
constexpr int N_UTT = sizeof(UTTERANCES) / sizeof(UTTERANCES[0]);
const char *POSITIONS[3] = {"past", "present", "wired"};

struct State {
    int runes[3] = {-1, -1, -1};
    uint32_t castAt = 0, uttAt = 0;
    int utt = 0;
    dsp::Noise rng;
    // chimes
    dsp::Osc bell[3], bellB[3];
    dsp::Decay env[3];
    volatile int pendingChime = -1;
    float drone = 0;
    dsp::Osc low;
    dsp::Delay<6000> dl;
} *S = nullptr;

void render(int16_t *out, int n) {
    State &s = *S;
    if (s.pendingChime >= 0) {
        int i = s.pendingChime;
        s.pendingChime = -1;
        static const float HZ[3] = {659.3f, 987.8f, 1318.5f};
        float base = HZ[i] * (1.f + (s.runes[i] % 5) * 0.0595f);
        s.bell[i].hz(base);
        s.bellB[i].hz(base * 2.76f); // inharmonic partial: bell, not beep
        s.env[i].trigger(0.6f);
    }
    for (int k = 0; k < n; ++k) {
        float x = s.low.sine() * 0.05f;
        for (int i = 0; i < 3; ++i) {
            float e = s.env[i].tick();
            x += (s.bell[i].sine() + s.bellB[i].sine() * 0.35f) * e * 0.3f;
        }
        float wet = s.dl.tap(5999);
        s.dl.put(x + wet * 0.45f);
        out[k] = dsp::toPcm(x + wet * 0.4f);
    }
}

void cast() {
    State &s = *S;
    for (int i = 0; i < 3; ++i) {
        int r;
        do r = s.rng.next() % 26;
        while ((i > 0 && r == s.runes[0]) || (i > 1 && r == s.runes[1]));
        s.runes[i] = r;
    }
    s.castAt = hal::ms();
}

void enter() {
    if (!S) {
        S = new State();
        S->rng.s ^= hal::ms() * 2654435761u;
        S->utt = S->rng.next() % N_UTT;
    }
    for (auto &e : S->env) e.time(1.4f);
    S->low.hz(55);
    S->uttAt = hal::ms();
    hal::setAudio(render);
}
void leave() {}

bool onKey(const hal::Key &k) {
    if (k.nav == hal::N_ENTER) cast();
    else if (k.ch == ' ') {
        S->utt = (S->utt + 1) % N_UTT;
        S->uttAt = hal::ms();
    }
    return true;
}

void draw(uint32_t now) {
    State &s = *S;
    Canvas &cv = hal::canvas();
    cv.fillScreen(C_VOID);
    header("ORACLE", "oracle");

    uint32_t since = now - s.castAt;
    for (int i = 0; i < 3; ++i) {
        int x = 8 + i * 77, y = 22, w = 70, h = 58;
        bool revealed = s.runes[i] >= 0 && since > (uint32_t)(i * 450);
        cv.fillRect(x, y, w, h, revealed ? C_DUSK : C_ASH);
        frame(x, y, w, h, revealed ? C_RUST : C_CLOT, revealed ? C_EMBER : C_UMBER);
        textCentre(POSITIONS[i], x + w / 2, y + 4, C_DIM);
        if (revealed) {
            // fire the chime once as each rune turns
            static uint32_t chimed[3];
            if (chimed[i] != s.castAt) {
                chimed[i] = s.castAt;
                s.pendingChime = i;
            }
            char g[2] = {(char)('A' + s.runes[i]), 0};
            uint32_t age = since - i * 450;
            uint16_t c = age < 300 ? C_BONE : C_EMBER;
            glyphsCentre(DAEDRIC_L, g, x + w / 2 + 1, y + 13, C_CLOT);
            glyphsCentre(DAEDRIC_L, g, x + w / 2, y + 12, c);
            char l[2] = {(char)('a' + s.runes[i]), 0};
            textCentre(l, x + w / 2, y + 48, C_DIM);
        } else {
            float p = now * 0.004f + i;
            diamond(x + w / 2, y + 32, 6 + (int)(2 * sinf(p)), C_CLOT, false);
        }
    }
    if (s.runes[0] >= 0) {
        // the omens, in order, as they are revealed
        for (int i = 0; i < 3; ++i) {
            if (since <= (uint32_t)(i * 450)) break;
            char line[48];
            snprintf(line, sizeof line, "%-7s %s", POSITIONS[i], OMENS[s.runes[i]]);
            text(line, 30, 83 + i * 9, i == 2 ? C_GLOW : C_BONE);
        }
    } else {
        textCentre("press enter to cast the runes", 120, 92, C_DIM);
    }
    // typed utterance
    const char *u = UTTERANCES[s.utt];
    int chars = (now - s.uttAt) / 45;
    int len = strlen(u);
    char buf[64];
    snprintf(buf, sizeof buf, "%.*s%s", chars < len ? chars : len, u, (now / 300) & 1 ? "_" : " ");
    cv.drawFastHLine(0, 111, 240, C_CLOT);
    textCentre(buf, 120, 115, C_EMBER);
    textCentre("ent cast  spc speak", 120, 126, C_DIM);
}
} // namespace

const Rite RITE_ORACLE = {"ORACLE", "oracle", "cast three runes", enter, leave, onKey, draw};
