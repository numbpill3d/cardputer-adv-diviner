// Desktop fakes for net.h: canned APs, BLE devices, a LAN and a couple of
// Gemini capsules, so every networked rite previews in the SDL sim. No real
// sockets. A tiny bundled gemtext page exercises the parser.
#ifdef GRIM_NATIVE
#include "net.h"
#include <SDL2/SDL.h>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>

namespace net {
namespace {
uint32_t now() { return SDL_GetTicks(); }
WifiState wstate = WST_OFF;
uint32_t stateAt = 0;
std::string ssid = "", ip = "";
std::vector<std::string> saved = {"wired-house", "navi"};

const char *FAKE_SSID[] = {"wired-house", "navi", "COPLAND-OS", "Setec", "xanadu",
                           "linksys", "ATT-7721", "eduroam", "", "ThePlan9"};
const int FAKE_AUTH[] = {1, 1, 1, 0, 1, 0, 1, 1, 0, 1};

int scanCalls = 0;
} // namespace

void wifiBegin() {
    if (wstate == WST_OFF) { wstate = WST_CONNECTING; stateAt = now(); ssid = "wired-house"; }
}
void radioStop() { wstate = WST_OFF; }
WifiState wifiState() {
    if (wstate == WST_CONNECTING && now() - stateAt > 1500) {
        wstate = WST_ONLINE;
        ip = "192.168.1.57";
    }
    return wstate;
}
int wifiScan(std::vector<AP> &out) {
    if (++scanCalls < 20) return -1; // pretend it takes ~0.6s of frames
    scanCalls = 0;
    out.clear();
    int count = (getenv("GRIM_FAKE_DRONE") && now() > 22000) ? 11 : 10; // a drone wanders in after 16s
    for (int i = 0; i < count; ++i) {
        AP a;
        if (i == 10) {
            a.ssid = "DJI-Mavic3-1A2B";
            for (int b = 0; b < 6; ++b) a.bssid[b] = (b == 0 ? 0x60 : b == 1 ? 0x60 : b == 2 ? 0x1f : i * 9 + b);
            a.rssi = -48 - (now() / 900 % 5);
            a.channel = 6;
            a.auth = 1;
            a.known = false;
            out.push_back(a);
            continue;
        }
        a.ssid = FAKE_SSID[i];
        for (int b = 0; b < 6; ++b) a.bssid[b] = (i * 40 + b * 17) & 0xFF;
        a.rssi = -35 - (i * 6 + (now() / 500 % 7));
        a.channel = 1 + (i * 3) % 11;
        a.auth = FAKE_AUTH[i];
        a.known = a.ssid == "wired-house" || a.ssid == "navi";
        out.push_back(a);
    }
    return out.size();
}
void wifiConnect(const char *s, const char *) {
    ssid = s;
    wstate = WST_CONNECTING;
    stateAt = now();
    bool found = false;
    for (auto &x : saved) found |= x == s;
    if (!found) saved.push_back(s);
}
void wifiForget(const char *s) {
    for (auto it = saved.begin(); it != saved.end(); ++it)
        if (*it == s) { saved.erase(it); break; }
}
std::string wifiIP() { return ip; }
std::string wifiSSID() { return ssid; }
int wifiRSSI() { return -42 - (int)(now() / 400 % 12); }
std::vector<std::string> wifiSaved() { return saved; }

// ---- captive portal (fake) ----
namespace {
PortalState capState = PORTAL_NONE;
uint32_t capAt = 0;
std::string capUrl;
bool fakeCaptive() { return getenv("GRIM_FAKE_CAPTIVE") || ssid.find("COPLAND") != std::string::npos; }
} // namespace
void captiveCheck() {
    capState = PORTAL_CHECKING;
    capAt = now();
    capUrl = fakeCaptive() ? "http://portal.wired/login" : "";
}
void captiveAccept() { capState = PORTAL_JOINING; capAt = now(); }
PortalState captiveState() {
    if ((capState == PORTAL_CHECKING || capState == PORTAL_JOINING) && now() - capAt > 900)
        capState = (capState == PORTAL_CHECKING && fakeCaptive()) ? PORTAL_CAPTIVE : PORTAL_ONLINE;
    return capState;
}
std::string captiveURL() { return capUrl; }

// ---- sniffer ----
namespace {
int snifCh = 1;
}
void snifferBegin() {}
void snifferSetChannel(int ch) { snifCh = ch; }
void snifferPoll(uint32_t mgmt[14], uint32_t data[14], int &rssiLast) {
    uint32_t t = now();
    for (int c = 1; c <= 13; ++c) {
        float load = 0.5f + 0.5f * sinf((t * 0.001f) + c);
        mgmt[c] = (uint32_t)(load * (c == 1 || c == 6 || c == 11 ? 40 : 8) * (rand() % 100) / 100);
        data[c] = (uint32_t)(load * (c == snifCh ? 120 : 20) * (rand() % 100) / 100);
    }
    rssiLast = -40 - rand() % 50;
}

// ---- BLE ----
namespace {
int bleCalls = 0;
struct FD { const char *n; uint16_t co; bool conn; };
const FD FAKES[] = {{"navi-buds", 0x004C, true}, {"", 0x0075, false}, {"Tile", 0x0157, false},
                    {"Mi Band 7", 0x0157, true}, {"", 0x00E0, false}, {"JBL FLIP", 0x0057, true},
                    {"ESP32-ghost", 0xFFFF, true}, {"AirTag", 0x004C, false}};
} // namespace
void bleBegin() {}
int bleScan(std::vector<BleDev> &out) {
    if (++bleCalls < 40) return -1;
    bleCalls = 0;
    out.clear();
    int n = 5 + rand() % 3;
    for (int i = 0; i < n; ++i) {
        BleDev d;
        d.name = FAKES[i].n;
        char a[18];
        snprintf(a, sizeof a, "%02x:%02x:%02x:%02x:%02x:%02x", 0xDE, 0xAD, i * 11, i * 7, i * 3, i);
        d.addr = a;
        d.rssi = -45 - (i * 7 + rand() % 10);
        d.company = FAKES[i].co;
        d.connectable = FAKES[i].conn;
        d.lastSeen = now();
        out.push_back(d);
    }
    return out.size();
}

// ---- LAN ----
namespace {
uint32_t lanStart = 0;
}
void lanBegin() { lanStart = now(); }
int lanProgress() {
    if (!lanStart) return 0;
    int p = (now() - lanStart) / 30;
    return p > 100 ? 100 : p;
}
std::vector<Host> lanHosts() {
    std::vector<Host> h;
    if (lanProgress() < 100) return h;
    h.push_back({"192.168.1.1", "gateway", {53, 80, 443}, true});
    h.push_back({"192.168.1.57", "this.navi", {}, true});
    h.push_back({"192.168.1.64", "copland.local", {22, 80, 8080}, true});
    h.push_back({"192.168.1.90", "", {445, 3389}, true});
    h.push_back({"192.168.1.112", "protocol7", {1965, 22}, true});
    return h;
}

// ---- CSI ----
namespace {
float csiMotion = 0;
}
void csiBegin() {}
void csiEnd() {}
int csiRead(float &motion, float *bins, int nbins) {
    uint32_t t = now();
    // fake a breathing baseline with occasional "someone walked by" bursts
    float base = 0.08f + 0.05f * sinf(t * 0.004f);
    float burst = ((t / 3000) % 4 == 0) ? 0.6f * fabsf(sinf(t * 0.02f)) : 0.f;
    float target = base + burst + (rand() % 100) * 0.0009f;
    csiMotion += (target - csiMotion) * 0.1f;
    motion = csiMotion;
    int n = nbins < 52 ? nbins : 52;
    for (int i = 0; i < n; ++i)
        bins[i] = 0.3f + 0.3f * sinf(i * 0.4f + t * 0.003f) + csiMotion * 0.4f * sinf(i + t * 0.01f) +
                  (rand() % 100) * 0.001f;
    return n;
}

// ---- probe ----
namespace {
uint32_t probeStart = 0;
std::vector<ProbeLine> probeLines;
bool probeFin = false;
}
void probeBegin(const char *ip, uint16_t port) {
    probeStart = now();
    probeFin = false;
    probeLines.clear();
    char b[64];
    snprintf(b, sizeof b, "probing %s:%d", ip, port);
    probeLines.push_back({b, 0});
}
int probeProgress() {
    if (!probeStart) return 0;
    int p = (now() - probeStart) / 25;
    return p > 100 ? 100 : p;
}
bool probeDone() { return probeProgress() >= 100; }
std::vector<ProbeLine> probeResults() {
    int p = probeProgress();
    auto add = [&](int at, const char *t, uint8_t k) {
        if (p >= at && (int)probeLines.size() < 20) {
            for (auto &l : probeLines) if (l.text == t) return;
            probeLines.push_back({t, k});
        }
    };
    add(15, "Server: lighttpd/1.4.55", 1);
    add(25, "/  200 OK", 1);
    add(35, "/admin  401 auth", 1);
    add(45, "/.git/HEAD  200 OK", 1);
    add(55, "/config  403 auth", 1);
    add(70, "trying default creds...", 0);
    add(88, "DEFAULT CRED: admin:admin", 2);
    add(100, "done", 0);
    return probeLines;
}

// ---- Gemini ----
namespace {
GemState gstate = GEM_IDLE;
int gstatus = 0, gprog = 0;
uint32_t gAt = 0;
std::string gmeta, gbody, gurl;

const char *HOME =
    "# gemini // the wired\n\n"
    "=> gemini://geminiprotocol.net/ project home\n"
    "=> gemini://warmedal.se/~antenna/ Antenna (aggregated feeds)\n"
    "=> gemini://station.martinrue.com/ Station microblog\n\n"
    "## what is this\n"
    "A small-web browser. Text only. One capsule per line.\n\n"
    "> close the world, open the next.\n\n"
    "* links are numbered\n"
    "* press the number to follow\n"
    "=> gemini://station.martinrue.com/post post something\n";
const char *POST =
    "# post to station\n\n"
    "Say a few words to the wired.\n";
} // namespace

void gemGet(const char *url) {
    gstate = GEM_FETCHING;
    gprog = 0;
    gAt = now();
    gurl = url;
}
void gemStop() { gstate = GEM_IDLE; }
GemState gemState() {
    if (gstate == GEM_FETCHING) {
        gprog = (now() - gAt) / 3;
        if (now() - gAt > 700) {
            if (gurl.find("/post") != std::string::npos) {
                gstate = GEM_INPUT;
                gstatus = 10;
                gmeta = "say something to the wired";
            } else {
                gstate = GEM_DONE;
                gstatus = 20;
                gmeta = "text/gemini";
                gbody = gurl.find("station") != std::string::npos && gurl.find("post") == std::string::npos
                            ? POST
                            : HOME;
            }
        }
    }
    return gstate;
}
int gemStatus() { return gstatus; }
std::string gemMeta() { return gmeta; }
std::string gemBody() { return gbody; }
std::string gemURL() { return gurl; }
int gemProgress() { return gprog; }

} // namespace net
#endif
