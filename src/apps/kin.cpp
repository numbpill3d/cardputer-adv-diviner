// KIN — a Bluetooth LE scanner. Nearby devices drift as named spirits, closer
// when their signal is stronger, fading as they go unseen. A guess at the
// maker (Apple, Google, Microsoft...) rides along from the manufacturer id.
//
// keys  ;/.  select a spirit   enter pin/unpin details
#include "../net.h"
#include "../rite.h"
#include "../theme.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

using namespace theme;

namespace {
struct Spirit {
    std::string addr;
    float x, y, tx, ty, ph;
};
struct State {
    std::vector<net::BleDev> devs;
    std::vector<Spirit> spirits;
    int sel = 0;
    uint32_t lastScan = 0;
} *S = nullptr;

const char *vendor(uint16_t c) {
    switch (c) {
    case 0x004C: return "apple";
    case 0x0075: return "samsung";
    case 0x0006: return "microsoft";
    case 0x00E0: return "google";
    case 0x0157: return "xiaomi";
    case 0x0057: return "harman";
    case 0xFFFF: return "unknown";
    default: return "";
    }
}

void enter() {
    if (!S) S = new State();
    net::bleBegin();
    S->lastScan = 0;
}
void leave() {}

bool onKey(const hal::Key &k) {
    State &s = *S;
    if (s.devs.empty()) return true;
    if (k.nav == hal::N_LEFT || k.nav == hal::N_UP) s.sel = (s.sel + s.devs.size() - 1) % s.devs.size();
    else if (k.nav == hal::N_RIGHT || k.nav == hal::N_DOWN) s.sel = (s.sel + 1) % s.devs.size();
    return true;
}

void draw(uint32_t now) {
    State &s = *S;
    Canvas &cv = hal::canvas();
    cv.fillScreen(C_VOID);
    header("KIN", "familiars");

    net::bleScan(s.devs);
    // keep a spirit per device address, give new ones a spot
    for (auto &d : s.devs) {
        bool have = false;
        for (auto &sp : s.spirits) if (sp.addr == d.addr) have = true;
        if (!have) {
            Spirit sp;
            sp.addr = d.addr;
            sp.x = sp.tx = 20 + rand() % 180;
            sp.y = sp.ty = 28 + rand() % 70;
            sp.ph = (rand() % 628) / 100.f;
            s.spirits.push_back(sp);
        }
    }
    if (s.sel >= (int)s.devs.size()) s.sel = s.devs.empty() ? 0 : s.devs.size() - 1;

    const int cx = 62, cy = 74;
    cv.fillCircle(cx, cy, 2, C_EMBER);
    cv.drawCircle(cx, cy, 50, C_DUSK);
    for (int i = 0; i < (int)s.devs.size(); ++i) {
        net::BleDev &d = s.devs[i];
        Spirit *sp = nullptr;
        for (auto &x : s.spirits) if (x.addr == d.addr) sp = &x;
        if (!sp) continue;
        float r = 12 + (-d.rssi - 40) * 0.6f;
        if (r > 48) r = 48; if (r < 10) r = 10;
        float ang = sp->ph + now * 0.0004f * (1 + (d.addr[0] & 3));
        sp->tx = cx + cosf(ang) * r;
        sp->ty = cy + sinf(ang) * r;
        sp->x += (sp->tx - sp->x) * 0.08f;
        sp->y += (sp->ty - sp->y) * 0.08f;
        bool here = i == s.sel;
        uint16_t c = here ? C_GLOW : d.connectable ? C_RUST : C_BLOOD;
        if (here) cv.drawLine(cx, cy, (int)sp->x, (int)sp->y, C_UMBER);
        cv.fillCircle((int)sp->x, (int)sp->y, here ? 3 : 2, c);
        cv.drawCircle((int)sp->x, (int)sp->y, here ? 5 : 3, blend(c, C_VOID, 120));
        if (!d.name.empty()) {
            char nm[10] = {};
            strncpy(nm, d.name.c_str(), 9);
            text(nm, (int)sp->x - strlen(nm) * 3, (int)sp->y + 6, here ? C_BONE : C_DIM);
        }
    }

    int px = 130;
    frame(px - 4, 22, 114, 96, C_CLOT, C_UMBER);
    char b[40];
    snprintf(b, sizeof b, "%d near", (int)s.devs.size());
    text(b, px, 26, C_DIM);
    if (!s.devs.empty()) {
        net::BleDev &d = s.devs[s.sel];
        text(d.name.empty() ? "(no name)" : d.name.c_str(), px, 40, C_GLOW);
        text(d.addr.c_str(), px, 52, C_BONE);
        snprintf(b, sizeof b, "%d dBm", d.rssi);
        text(b, px, 64, d.rssi > -70 ? C_EMBER : C_DIM);
        const char *v = vendor(d.company);
        if (v[0]) { snprintf(b, sizeof b, "~ %s", v); text(b, px + 52, 64, C_BLOOD); }
        text(d.connectable ? "connectable" : "beacon", px, 76, C_DIM);
        snprintf(b, sizeof b, "id %04x", d.company);
        text(b, px, 88, C_DIM);
    } else textCentre("no familiars yet", px + 53, 68, C_DIM);
    footer("bluetooth le  ;. select");
}
} // namespace

const Rite RITE_KIN = {"KIN", "kin", "bluetooth familiars", enter, leave, onKey, draw};
