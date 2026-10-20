// SCRY — a Wi-Fi scanner drawn as a constellation. Each network is a glyph
// star placed by channel (angle) and signal (radius: strong = close in).
// Secured networks burn red, open ones glow. Select one to read it.
//
// keys  ;/.  cycle selection   enter details   r rescan
#include "../dsp.h"
#include "../net.h"
#include "../rite.h"
#include "../theme.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

using namespace theme;

namespace {
struct State {
    std::vector<net::AP> aps;
    int sel = 0;
    uint32_t lastScan = 0;
    bool scanning = true;
    bool detail = false;
    dsp::Noise rng;
    dsp::SVF f;
    dsp::Osc ping;
    dsp::Decay pingEnv;
    volatile int pingReq = 0;
} *S = nullptr;

void render(int16_t *out, int n) {
    State &s = *S;
    if (s.pingReq) { s.pingReq = 0; s.ping.hz(1200); s.pingEnv.trigger(0.3f); }
    for (int i = 0; i < n; ++i) {
        float x = s.f.bp(s.rng.white()) * 0.015f;        // faint aether hiss
        x += s.ping.sine() * s.pingEnv.tick() * 0.25f;   // sonar ping on rescan
        out[i] = dsp::toPcm(x);
    }
}

void enter() {
    if (!S) S = new State();
    S->f.set(2200, 0.7f);
    S->pingEnv.time(0.25f);
    net::wifiBegin();
    S->scanning = true;
    S->lastScan = 0;
    hal::setAudio(render);
}
void leave() {}

bool onKey(const hal::Key &k) {
    State &s = *S;
    if (s.aps.empty()) { if (k.ch == 'r') s.scanning = true; return true; }
    if (k.nav == hal::N_LEFT || k.nav == hal::N_UP) s.sel = (s.sel + s.aps.size() - 1) % s.aps.size();
    else if (k.nav == hal::N_RIGHT || k.nav == hal::N_DOWN) s.sel = (s.sel + 1) % s.aps.size();
    else if (k.nav == hal::N_ENTER) s.detail = !s.detail;
    else if (k.ch == 'r') { s.scanning = true; s.pingReq = 1; }
    return true;
}

void draw(uint32_t now) {
    State &s = *S;
    Canvas &cv = hal::canvas();
    cv.fillScreen(C_VOID);
    header("SCRY", "wifi scry");

    if (now - s.lastScan > 2500 || s.scanning) {
        int r = net::wifiScan(s.aps);
        if (r >= 0 && s.scanning) s.pingReq = 1;
        s.scanning = r < 0;
        if (r >= 0) { s.lastScan = now; if (s.sel >= (int)s.aps.size()) s.sel = 0; }
    }

    const int cx = 70, cy = 78, rmax = 52;
    // concentric signal rings
    for (int r = 18; r <= rmax; r += 17) cv.drawCircle(cx, cy, r, C_DUSK);
    cv.fillCircle(cx, cy, 2, C_EMBER); // us

    for (int i = 0; i < (int)s.aps.size(); ++i) {
        const net::AP &a = s.aps[i];
        float ang = -1.5708f + (a.channel % 14) * 6.2831853f / 14 + a.rssi * 0.004f;
        float r = 14 + (-a.rssi - 30) * 0.5f; // -30dBm=center, weaker=outer
        if (r > rmax) r = rmax;
        if (r < 10) r = 10;
        int x = cx + (int)(cosf(ang) * r), y = cy + (int)(sinf(ang) * r);
        bool here = i == s.sel;
        uint16_t c = here ? C_GLOW : a.auth ? C_BLOOD : C_RUST;
        if (here) {
            float p = 3 + 1.5f * sinf(now * 0.008f);
            cv.drawCircle(x, y, (int)p + 3, C_EMBER);
            cv.drawLine(cx, cy, x, y, C_UMBER);
        }
        char g[2] = {(char)('A' + (a.bssid[5] % 26)), 0};
        glyphs(DAEDRIC_S, g, x - 4, y - 8, c);
        if (a.known) diamond(x, y + 8, 1, C_EMBER);
    }
    if (s.aps.empty()) textCentre(s.scanning ? "scrying..." : "empty aether", cx, cy, C_DIM);

    // side panel
    int px = 132;
    frame(px - 4, 22, 112, 96, C_CLOT, C_UMBER);
    char b[40];
    snprintf(b, sizeof b, "%d seen", (int)s.aps.size());
    text(b, px, 26, C_DIM);
    if (!s.aps.empty()) {
        const net::AP &a = s.aps[s.sel];
        const char *nm = a.ssid.empty() ? "<hidden>" : a.ssid.c_str();
        // wrap ssid to 2 lines of ~14
        char l1[16] = {}, l2[16] = {};
        strncpy(l1, nm, 14);
        if (strlen(nm) > 14) strncpy(l2, nm + 14, 14);
        text(l1, px, 40, C_GLOW);
        if (l2[0]) text(l2, px, 49, C_GLOW);
        snprintf(b, sizeof b, "ch %d", a.channel);
        text(b, px, 62, C_BONE);
        snprintf(b, sizeof b, "%d dBm", a.rssi);
        text(b, px + 52, 62, a.rssi > -67 ? C_EMBER : C_DIM);
        text(a.auth ? "secured" : "open", px, 74, a.auth ? C_BLOOD : C_RUST);
        if (a.known) text("remembered", px, 84, C_EMBER);
        snprintf(b, sizeof b, "%02x:%02x:%02x", a.bssid[3], a.bssid[4], a.bssid[5]);
        text(b, px, 98, C_DIM);
    }
    footer("ent detail  r rescan  ;. select");
}
} // namespace

const Rite RITE_SCRY = {"SCRY", "scry", "wifi constellation", enter, leave, onKey, draw};
