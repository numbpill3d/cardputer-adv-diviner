// PROBE — active host testing for an authorized target. Point it at an IP and
// port; it fingerprints the HTTP service, walks common paths for live status
// codes (admin panels, .git, .env, configs, APIs), and if something wants
// auth, tries a short list of default credentials. Only touches the host you
// enter. For networks you are permitted to test.
//
// keys  type ip   enter run   ,/ port field   ;/. scroll results
#include "../net.h"
#include "../rite.h"
#include "../textedit.h"
#include "../theme.h"
#include <stdio.h>
#include <string.h>

using namespace theme;

namespace {
enum Mode { M_EDIT, M_RUN };
struct State {
    TextEdit ip;
    uint16_t port = 80;
    Mode mode = M_EDIT;
    int scroll = 0;
    bool seeded = false;
} *S = nullptr;

void enter() {
    if (!S) S = new State();
    net::wifiBegin();
    if (!S->seeded) {
        // default target: the gateway (x.x.x.1 of our subnet)
        std::string ip = net::wifiIP();
        size_t dot = ip.rfind('.');
        S->ip.set(dot != std::string::npos ? (ip.substr(0, dot) + ".1").c_str() : "192.168.1.1");
        S->seeded = true;
    }
}
void leave() {}

bool onKey(const hal::Key &k) {
    State &s = *S;
    if (s.mode == M_RUN) {
        if (k.nav == hal::N_ESC) { s.mode = M_EDIT; return true; }
        if (k.nav == hal::N_UP) { if (s.scroll > 0) s.scroll--; }
        else if (k.nav == hal::N_DOWN) s.scroll++;
        else if (k.nav == hal::N_ENTER) { net::probeBegin(s.ip.buf, s.port); s.scroll = 0; }
        return true;
    }
    // EDIT
    if (k.nav == hal::N_ENTER) {
        if (net::wifiState() == net::WST_ONLINE) { net::probeBegin(s.ip.buf, s.port); s.mode = M_RUN; s.scroll = 0; }
        return true;
    }
    if (k.ch == ',') { if (s.port > 1) s.port = s.port == 443 ? 80 : s.port == 8080 ? 443 : 8080; return true; }
    if (k.ch == '/') { s.port = s.port == 80 ? 443 : s.port == 443 ? 8080 : 80; return true; }
    s.ip.key(k);
    return true;
}

void draw(uint32_t now) {
    State &s = *S;
    Canvas &cv = hal::canvas();
    cv.fillScreen(C_VOID);
    header("PROBE", "host test");

    if (net::wifiState() != net::WST_ONLINE) {
        textCentre("not linked", 120, 56, C_BLOOD);
        textCentre("join a network in LINK first", 120, 70, C_DIM);
        footer("host recon");
        return;
    }

    if (s.mode == M_EDIT) {
        textCentre("target host", 120, 30, C_BONE);
        s.ip.draw(30, 46, 180, now, C_GLOW);
        char b[24];
        snprintf(b, sizeof b, "port %d", s.port);
        textCentre(b, 120, 66, C_EMBER);
        textCentre(",/ : 80 / 443 / 8080", 120, 78, C_DIM);
        textCentre("enter: run probe", 120, 94, C_RUST);
        footer("type ip  ,/ port  enter run");
        return;
    }

    // M_RUN: results
    int pct = net::probeProgress();
    if (!net::probeDone()) {
        cv.drawRect(8, 19, 224, 4, C_CLOT);
        cv.fillRect(9, 20, 222 * pct / 100, 2, C_RUST);
    }

    auto res = net::probeResults();
    const int rows = 10, rh = 10, y0 = 26;
    int n = res.size();
    if (s.scroll > n - rows) s.scroll = n > rows ? n - rows : 0;
    if (s.scroll < 0) s.scroll = 0;
    for (int i = 0; i < rows && s.scroll + i < n; ++i) {
        const net::ProbeLine &l = res[s.scroll + i];
        int y = y0 + i * rh;
        uint16_t c = l.kind == 2 ? C_GLOW : l.kind == 1 ? C_EMBER : l.kind == 3 ? C_BLOOD : C_BONE;
        if (l.kind == 2) { cv.fillRect(0, y - 1, 240, rh, C_DUSK); diamond(4, y + 3, 2, C_GLOW); }
        char t[44] = {};
        strncpy(t, l.text.c_str(), 42);
        text(t, l.kind == 2 ? 10 : 6, y, c);
    }
    footer(net::probeDone() ? "ent rerun  ` edit  ;. scroll" : "probing...  ` edit");
}
} // namespace

const Rite RITE_PROBE = {"PROBE", "probe", "test an authorized host", enter, leave, onKey, draw};
