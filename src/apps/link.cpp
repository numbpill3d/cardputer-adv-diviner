// LINK — the Wi-Fi rite. Scans, shows networks as glyph-marked bars, connects
// (asking for a password when needed), remembers them, and shows the live
// link. The other networked rites rely on the connection made here.
//
// keys  ;/.  move   enter connect/select   f forget   r rescan
//       (password prompt) type, enter join, ` cancel
#include "../net.h"
#include "../rite.h"
#include "../textedit.h"
#include "../theme.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

using namespace theme;

namespace {
enum Phase { LIST, ASKPASS };
struct State {
    std::vector<net::AP> aps;
    int sel = 0, top = 0;
    Phase phase = LIST;
    TextEdit pass;
    char joining[33] = "";
    uint32_t lastScan = 0;
    bool scanning = true;
    bool capAsked = false; // ran the captive-portal check for this link yet
    bool capTried = false; // user pressed 'p' to accept at least once
} *S = nullptr;

void enter() {
    if (!S) S = new State();
    net::wifiBegin();
    S->scanning = true;
    S->lastScan = 0;
}
void leave() { /* keep the radio up so net/gemini stay connected */ }

bool onKey(const hal::Key &k) {
    State &s = *S;
    if (s.phase == ASKPASS) {
        if (k.nav == hal::N_ESC) { s.phase = LIST; return true; }
        if (k.nav == hal::N_ENTER) {
            net::wifiConnect(s.joining, s.pass.buf);
            s.phase = LIST;
            return true;
        }
        s.pass.key(k);
        return true;
    }
    if (k.ch == 'r') { net::wifiScan(s.aps); s.scanning = true; return true; }
    if (k.ch == 'p') { // accept a captive portal (only meaningful once linked)
        if (net::wifiState() == net::WST_ONLINE) { net::captiveAccept(); s.capTried = true; }
        return true;
    }

    if (s.aps.empty()) return true;
    if (k.nav == hal::N_UP) s.sel = (s.sel + s.aps.size() - 1) % s.aps.size();
    else if (k.nav == hal::N_DOWN) s.sel = (s.sel + 1) % s.aps.size();
    else if (k.ch == 'f') { net::wifiForget(s.aps[s.sel].ssid.c_str()); net::wifiScan(s.aps); }
    else if (k.nav == hal::N_ENTER) {
        const net::AP &a = s.aps[s.sel];
        snprintf(s.joining, sizeof s.joining, "%s", a.ssid.c_str());
        if (a.auth == 0 || a.known) {
            net::wifiConnect(a.ssid.c_str(), a.known ? "" : "");
        } else {
            s.pass.clear();
            s.pass.mask = true;
            s.phase = ASKPASS;
        }
    }
    return true;
}

void bars(int rssi, int x, int y, uint16_t c) {
    Canvas &cv = hal::canvas();
    int n = rssi > -55 ? 4 : rssi > -67 ? 3 : rssi > -78 ? 2 : 1;
    for (int i = 0; i < 4; ++i)
        cv.fillRect(x + i * 4, y + 6 - i * 2, 3, 2 + i * 2, i < n ? c : C_UMBER);
}

void draw(uint32_t now) {
    State &s = *S;
    Canvas &cv = hal::canvas();
    cv.fillScreen(C_VOID);
    header("LINK", "wifi");

    net::WifiState ws = net::wifiState();
    const char *st = ws == net::WST_ONLINE ? "online" : ws == net::WST_CONNECTING ? "linking.." :
                     ws == net::WST_FAILED ? "offline" : "...";
    char line[64];
    if (ws == net::WST_ONLINE)
        snprintf(line, sizeof line, "%s  %s", net::wifiSSID().c_str(), net::wifiIP().c_str());
    else
        snprintf(line, sizeof line, "%s", st);
    textRight(line, 178, 5, ws == net::WST_ONLINE ? C_GLOW : C_DIM);

    // once a link is up, probe for a captive portal (walled garden) once
    if (ws == net::WST_ONLINE) { if (!s.capAsked) { net::captiveCheck(); s.capAsked = true; } }
    else { s.capAsked = false; s.capTried = false; }
    net::PortalState ps = net::captiveState();

    if (s.phase == ASKPASS) {
        cv.fillRect(14, 40, 212, 54, C_DUSK);
        frame(14, 40, 212, 54, C_RUST, C_EMBER);
        char t[48];
        snprintf(t, sizeof t, "key for %s", s.joining);
        textCentre(t, 120, 46, C_BONE);
        s.pass.draw(24, 60, 192, now);
        textCentre("enter: join   ` : cancel", 120, 80, C_DIM);
        return;
    }

    // rescan periodically until we have a list
    if (now - s.lastScan > 1500 || s.scanning) {
        int r = net::wifiScan(s.aps);
        s.scanning = r < 0;
        if (r >= 0) s.lastScan = now;
    }
    if (s.aps.empty()) {
        textCentre(s.scanning ? "scrying the aether..." : "no networks", 120, 60, C_DIM);
        if (s.scanning) {
            float a = now * 0.006f;
            diamond(120 + (int)(cosf(a) * 14), 74 + (int)(sinf(a) * 8), 2, C_RUST);
        } else {
            // the S3 is 2.4GHz-only: the usual reason a phone hotspot is missing
            textCentre("phone hotspot? set it to 2.4GHz", 120, 76, C_UMBER);
        }
    }

    const int rows = 7, rh = 12, y0 = 22;
    if (s.sel < s.top) s.top = s.sel;
    if (s.sel >= s.top + rows) s.top = s.sel - rows + 1;
    for (int i = 0; i < rows && s.top + i < (int)s.aps.size(); ++i) {
        const net::AP &a = s.aps[s.top + i];
        int y = y0 + i * rh;
        bool here = s.top + i == s.sel;
        if (here) cv.fillRect(0, y - 1, 240, rh, C_DUSK);
        bars(a.rssi, 3, y + 1, here ? C_EMBER : C_RUST);
        char g[2] = {(char)('A' + (a.bssid[5] % 26)), 0};
        glyphs(DAEDRIC_S, g, 22, y - 2, a.auth ? (here ? C_EMBER : C_BLOOD) : C_DIM);
        const char *nm = a.ssid.empty() ? "<hidden>" : a.ssid.c_str();
        text(nm, 36, y, here ? C_GLOW : C_BONE);
        if (a.known) text("*", 196, y, C_EMBER);
        char ch[8];
        snprintf(ch, sizeof ch, "c%d", a.channel);
        textRight(ch, 236, y, C_DIM);
        if (a.auth == 0) text("o", 205, y, C_DIM);
    }

    // captive-portal status takes over the hint line once a link is up
    const char *foot = "ent join  f forget  r rescan";
    if (ws == net::WST_ONLINE) {
        if (ps == net::PORTAL_CAPTIVE) {
            // once p has been tried and we're still walled in, the portal wants
            // a real browser login (form/JS) the device can't drive — say so
            foot = s.capTried ? "needs a browser login" : "captive portal!  p: accept";
            std::string u = net::captiveURL();
            if (!u.empty()) textCentre(u.c_str(), 120, hal::H - 22, C_EMBER);
            else if (s.capTried) textCentre("open it on your phone", 120, hal::H - 22, C_DIM);
        } else if (ps == net::PORTAL_CHECKING) foot = "checking the aether...";
        else if (ps == net::PORTAL_JOINING) foot = "opening the portal...";
        else if (ps == net::PORTAL_ONLINE) foot = "the aether is open";
        else if (ps == net::PORTAL_FAILED) foot = "walled off  p: retry";
    }
    footer(foot);
}
} // namespace

const Rite RITE_LINK = {"LINK", "wifi", "join the aether", enter, leave, onKey, draw};
