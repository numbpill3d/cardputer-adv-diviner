// BANISH — an infrared remote. Sends power/mute/volume codes across the common
// TV protocols (NEC, Sony SIRC, RC5, Samsung, Panasonic), plus a "banish all"
// sweep that fires every power code in turn, TV-B-Gone style, to quiet a room.
//
// The ADV's IR LED pin isn't documented; GRIM_IR_PIN defaults to 44 (the
// original Cardputer's). If beaming does nothing, that's the thing to change.
//
// keys  ;/.  move   enter send   b banish all   ,/ protocol
#include "../rite.h"
#include "../theme.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

#ifndef GRIM_IR_PIN
#define GRIM_IR_PIN 44
#endif

#ifdef GRIM_DEVICE
#define SEND_PWM_BY_TIMER
#define IR_SEND_PIN GRIM_IR_PIN
#include <IRremote.hpp>
#endif

using namespace theme;

namespace {
enum Proto { P_NEC, P_SONY, P_RC5, P_SAMSUNG, P_PANA, N_PROTO };
const char *PROTO_NAMES[N_PROTO] = {"NEC", "Sony", "RC5", "Samsung", "Panason"};

struct Code {
    const char *name;
    uint8_t proto;
    uint16_t addr;
    uint16_t cmd;
};
// A small, generic bank. Power codes feed the "banish all" sweep.
const Code CODES[] = {
    {"TV power", P_NEC, 0x04, 0x08},     {"TV power", P_SAMSUNG, 0x0707, 0x02},
    {"TV power", P_SONY, 0x01, 0x15},    {"TV power", P_RC5, 0x00, 0x0C},
    {"TV power", P_PANA, 0x4004, 0x0100},{"volume +", P_NEC, 0x04, 0x02},
    {"volume -", P_NEC, 0x04, 0x03},     {"mute", P_NEC, 0x04, 0x09},
    {"input", P_NEC, 0x04, 0x0B},        {"vol+ sony", P_SONY, 0x01, 0x12},
    {"vol- sony", P_SONY, 0x01, 0x13},   {"power samsung", P_SAMSUNG, 0x0707, 0x99},
};
constexpr int N_CODES = sizeof(CODES) / sizeof(CODES[0]);

struct State {
    int sel = 0;
    uint32_t beamAt = 0;
    const char *beaming = nullptr;
    int banish = -1; // index into power sweep, -1 idle
    uint32_t banishAt = 0;
} *S = nullptr;

void sendCode(const Code &c) {
#ifdef GRIM_DEVICE
    switch (c.proto) {
    case P_NEC: IrSender.sendNEC(c.addr, c.cmd, 1); break;
    case P_SONY: IrSender.sendSony(c.addr, c.cmd, 1, 12); break;
    case P_RC5: IrSender.sendRC5(c.addr, c.cmd, 1); break;
    case P_SAMSUNG: IrSender.sendSamsung(c.addr, c.cmd, 1); break;
    case P_PANA: IrSender.sendPanasonic(c.addr, c.cmd, 1); break;
    }
#endif
    S->beamAt = hal::ms();
    S->beaming = c.name;
}

void enter() {
    if (!S) S = new State();
#ifdef GRIM_DEVICE
    IrSender.begin(GRIM_IR_PIN);
#endif
}
void leave() { S->banish = -1; }

bool onKey(const hal::Key &k) {
    State &s = *S;
    if (k.nav == hal::N_UP || k.nav == hal::N_LEFT) s.sel = (s.sel + N_CODES - 1) % N_CODES;
    else if (k.nav == hal::N_DOWN || k.nav == hal::N_RIGHT) s.sel = (s.sel + 1) % N_CODES;
    else if (k.nav == hal::N_ENTER) sendCode(CODES[s.sel]);
    else if (k.ch == 'b') { s.banish = 0; s.banishAt = 0; }
    return true;
}

void draw(uint32_t now) {
    State &s = *S;
    Canvas &cv = hal::canvas();
    cv.fillScreen(C_VOID);
    header("BANISH", "infrared");

    // banish-all sweep: fire each power code in turn
    if (s.banish >= 0) {
        if (now - s.banishAt > 220) {
            // advance to the next power code
            int found = -1, count = 0;
            for (int i = 0; i < N_CODES; ++i)
                if (CODES[i].cmd, strstr(CODES[i].name, "power")) {
                    if (count == s.banish) { found = i; break; }
                    count++;
                }
            if (found < 0) s.banish = -1;
            else { sendCode(CODES[found]); s.banish++; s.banishAt = now; }
        }
    }

    const int rows = 9, rh = 10, y0 = 24;
    int top = s.sel < rows ? 0 : s.sel - rows + 1;
    for (int i = 0; i < rows && top + i < N_CODES; ++i) {
        const Code &c = CODES[top + i];
        int y = y0 + i * rh;
        bool here = top + i == s.sel;
        if (here) cv.fillRect(0, y - 1, 150, rh, C_DUSK);
        text(c.name, 10, y, here ? C_GLOW : C_BONE);
        textRight(PROTO_NAMES[c.proto], 148, y, here ? C_EMBER : C_DIM);
    }

    // the emitter: a diamond that flares when beaming
    int cx = 196, cy = 64;
    bool beaming = now - s.beamAt < 160 || s.banish >= 0;
    for (int r = 40; r > 6; r -= 7) {
        uint16_t c = beaming ? blend(C_EMBER, C_VOID, 40 + (40 - r) * 5) : C_DUSK;
        diamond(cx, cy, r, c, false);
    }
    diamond(cx, cy, 6, beaming ? C_GLOW : C_CLOT);
    if (beaming) {
        float a = now * 0.02f;
        for (int i = 0; i < 6; ++i) {
            float t = a + i * 1.047f;
            cv.drawLine(cx, cy, cx + (int)(cosf(t) * 44), cy + (int)(sinf(t) * 44), C_RUST);
        }
    }
    char b[32];
    if (s.banish >= 0) textCentre("BANISHING", cx, 96, C_GLOW);
    else if (s.beaming && now - s.beamAt < 600) { snprintf(b, sizeof b, "sent: %s", s.beaming); textCentre(b, cx, 96, C_EMBER); }
    else textCentre("point at a tv", cx, 96, C_DIM);

    footer("ent send  b banish all  ;. move");
}
} // namespace

const Rite RITE_IR = {"BANISH", "banish", "infrared remote", enter, leave, onKey, draw};
