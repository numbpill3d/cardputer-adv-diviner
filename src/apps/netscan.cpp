// NET — map the local network. Sweeps your /24 for live hosts, then probes
// common ports, and lists what it finds with a guess at each service. Works
// on the network you joined in LINK; only touches your own subnet.
//
// keys  ;/.  scroll   enter rescan
#include "../net.h"
#include "../rite.h"
#include "../theme.h"
#include <stdio.h>
#include <string.h>

using namespace theme;

namespace {
struct State {
    std::vector<net::Host> hosts;
    int sel = 0, top = 0;
    bool running = false;
    bool started = false;
} *S = nullptr;

const char *svc(uint16_t p) {
    switch (p) {
    case 22: return "ssh";
    case 53: return "dns";
    case 80: return "http";
    case 443: return "https";
    case 445: return "smb";
    case 1965: return "gemini";
    case 3389: return "rdp";
    case 8080: return "http-alt";
    default: return "?";
    }
}

void startScan() {
    net::wifiBegin();
    if (net::wifiState() == net::WST_ONLINE) {
        net::lanBegin();
        S->running = true;
        S->started = true;
    }
}
void enter() {
    if (!S) S = new State();
    S->started = false;
    startScan();
}
void leave() {}

bool onKey(const hal::Key &k) {
    State &s = *S;
    if (k.nav == hal::N_ENTER) { s.started = false; startScan(); return true; }
    int n = s.hosts.size();
    if (!n) return true;
    if (k.nav == hal::N_UP) s.sel = (s.sel + n - 1) % n;
    else if (k.nav == hal::N_DOWN) s.sel = (s.sel + 1) % n;
    return true;
}

void draw(uint32_t now) {
    State &s = *S;
    Canvas &cv = hal::canvas();
    cv.fillScreen(C_VOID);
    header("NET", "net rites");

    if (net::wifiState() != net::WST_ONLINE) {
        textCentre("not linked", 120, 56, C_BLOOD);
        textCentre("join a network in LINK first", 120, 70, C_DIM);
        footer("ent retry");
        return;
    }
    if (!s.started) { net::lanBegin(); s.running = true; s.started = true; }
    s.hosts = net::lanHosts();
    int pct = net::lanProgress();
    if (pct >= 100) s.running = false;

    char b[48];
    snprintf(b, sizeof b, "%d hosts", (int)s.hosts.size());
    textRight(b, 178, 5, C_DIM);

    if (s.running || pct < 100) {
        char ip[40];
        snprintf(ip, sizeof ip, "%s/24  %d%%", net::wifiIP().c_str(), pct);
        text(ip, 8, 20, C_DIM);
        cv.drawRect(100, 20, 132, 5, C_CLOT);
        cv.fillRect(101, 21, 130 * pct / 100, 3, C_RUST);
    }

    const int rows = 8, rh = 11, y0 = 28;
    int n = s.hosts.size();
    if (s.sel >= n) s.sel = n ? n - 1 : 0;
    if (s.sel < s.top) s.top = s.sel;
    if (s.sel >= s.top + rows) s.top = s.sel - rows + 1;
    for (int i = 0; i < rows && s.top + i < n; ++i) {
        const net::Host &h = s.hosts[s.top + i];
        int y = y0 + i * rh;
        bool here = s.top + i == s.sel;
        if (here) cv.fillRect(0, y - 1, 240, rh, C_DUSK);
        diamond(6, y + 4, 2, h.ports.empty() ? C_DIM : C_EMBER);
        text(h.ip.c_str(), 14, y, here ? C_GLOW : C_BONE);
        int x = 92;
        if (!h.name.empty()) {
            char nm[11] = {};
            strncpy(nm, h.name.c_str(), 10);
            text(nm, x, y, C_DIM);
            x += strlen(nm) * 6 + 4;
        }
        for (size_t p = 0; p < h.ports.size() && x < 232; ++p) {
            const char *sv = svc(h.ports[p]);
            int w = strlen(sv) * 6 + 4;
            if (x + w > 234) { text("+", x, y, C_RUST); break; }
            cv.drawRect(x, y - 1, w, 9, C_CLOT);
            text(sv, x + 2, y, here ? C_EMBER : C_RUST);
            x += w + 3;
        }
    }
    if (!n && !s.running) textCentre("no hosts found", 120, 70, C_DIM);

    footer("ent rescan  ;. scroll");
}
} // namespace

const Rite RITE_NET = {"NET", "net", "map the local net", enter, leave, onKey, draw};
