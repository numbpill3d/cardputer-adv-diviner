// SENTINEL — a presence ward. Watches the Wi-Fi APs and BLE devices around
// you, learns a baseline of what's "known", then cries out (sound + flare)
// when something unknown enters the field. Flags likely drones by name/SSID
// and MAC prefix, tracks first/last seen and whether a signal is approaching.
//
// It alternates radio phases (Wi-Fi scan, then BLE scan) since the chip runs
// one stack at a time; each sighting folds into one watch list.
//
// keys  ;/.  select   enter learn-as-known   l relearn baseline
//       space arm/disarm alarm   m mute
#include "../dsp.h"
#include "../net.h"
#include "../rite.h"
#include "../theme.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

using namespace theme;

namespace {
enum Kind { K_WIFI, K_BLE };
struct Watch {
    std::string id;   // bssid or ble addr
    std::string name; // ssid or ble name
    uint8_t kind;
    int8_t rssi, rssiPrev;
    uint16_t extra; // channel (wifi) or company (ble)
    bool known, drone, secured;
    uint32_t first, last;
    bool alerted;
};

struct State {
    std::vector<Watch> watch;
    int sel = 0, top = 0;
    uint8_t mode = K_BLE; // which radio is up; switched only on a keypress
    bool learning = true, armed = true, mute = false;
    uint32_t learnUntil = 0;
    int newCount = 0, droneCount = 0;
    // alarm
    dsp::Osc alarm, sub;
    dsp::Decay chirpEnv;
    volatile int alertLevel = 0; // 0 quiet, 1 unknown, 2 drone
    volatile uint32_t alertAt = 0;
    volatile int chirpReq = 0;
} *S = nullptr;

// crude drone fingerprints
bool looksDrone(const std::string &name, const std::string &id) {
    static const char *NAMES[] = {"dji",   "mavic", "tello", "spark",  "phantom", "anafi",
                                  "parrot", "skydio", "autel", "fimi",  "drone",   "uav",
                                  "avata", "neo",    "inspire"};
    char lo[40];
    int n = 0;
    for (char c : name) { if (n < 39) lo[n++] = (c >= 'A' && c <= 'Z') ? c + 32 : c; }
    lo[n] = 0;
    for (auto *p : NAMES) if (strstr(lo, p)) return true;
    // DJI / common UAV OUIs (first 3 MAC octets, hex in the id string)
    static const char *OUI[] = {"60:60:1f", "34:d2:62", "e4:7a:2c", "48:1c:b9", "ac:9b:0a"};
    char idlo[20];
    n = 0;
    for (char c : id) { if (n < 19) idlo[n++] = (c >= 'A' && c <= 'Z') ? c + 32 : c; }
    idlo[n] = 0;
    for (auto *p : OUI) if (strncmp(idlo, p, 8) == 0) return true;
    return false;
}

void render(int16_t *out, int n) {
    State &s = *S;
    if (s.chirpReq) { s.chirpReq = 0; s.chirpEnv.trigger(0.5f); s.alarm.hz(s.alertLevel == 2 ? 1500 : 900); }
    uint32_t t = hal::ms();
    bool active = s.armed && !s.mute && (t - s.alertAt < 2500) && s.alertLevel > 0;
    for (int i = 0; i < n; ++i) {
        float x = 0;
        if (active) {
            // warbling two-tone alarm; faster/higher for drones
            float warp = sinf((t + i / 24) * (s.alertLevel == 2 ? 0.02f : 0.011f));
            s.alarm.hz((s.alertLevel == 2 ? 1300 : 760) + warp * 240);
            x = s.alarm.square() * 0.18f;
        }
        x += s.sub.sine() * s.chirpEnv.tick() * 0.2f; // ping per new sighting
        out[i] = dsp::toPcm(x);
    }
}

void enter() {
    if (!S) S = new State();
    S->sub.hz(660);
    S->chirpEnv.time(0.12f);
    S->learning = true;
    S->learnUntil = hal::ms() + 12000; // 12s baseline
    S->mode = K_BLE;
    net::bleBegin(); // one radio, no auto-cycling (switching stacks on a timer crashes)
    hal::setAudio(render);
}
void leave() {}

Watch *find(const std::string &id) {
    for (auto &w : S->watch) if (w.id == id) return &w;
    return nullptr;
}

void mergeWifi(std::vector<net::AP> &aps, uint32_t now) {
    State &s = *S;
    for (auto &a : aps) {
        char idc[20];
        snprintf(idc, sizeof idc, "%02x:%02x:%02x:%02x:%02x:%02x", a.bssid[0], a.bssid[1], a.bssid[2],
                 a.bssid[3], a.bssid[4], a.bssid[5]);
        std::string id = idc;
        Watch *w = find(id);
        bool drone = looksDrone(a.ssid, id);
        if (!w) {
            if (s.watch.size() >= 400) continue;
            Watch nw;
            nw.id = id;
            nw.name = a.ssid;
            nw.kind = K_WIFI;
            nw.rssi = nw.rssiPrev = a.rssi;
            nw.extra = a.channel;
            nw.secured = a.auth;
            nw.drone = drone;
            nw.first = nw.last = now;
            nw.known = s.learning;
            nw.alerted = false;
            if (!s.learning) {
                s.newCount++;
                if (drone) s.droneCount++;
                s.alertLevel = drone ? 2 : 1;
                s.alertAt = now;
                s.chirpReq = 1;
                nw.alerted = true;
            }
            s.watch.push_back(nw);
        } else {
            w->rssiPrev = w->rssi;
            w->rssi = a.rssi;
            w->last = now;
            w->drone = w->drone || drone;
        }
    }
}

void mergeBle(std::vector<net::BleDev> &devs, uint32_t now) {
    State &s = *S;
    for (auto &d : devs) {
        Watch *w = find(d.addr);
        bool drone = looksDrone(d.name, d.addr);
        if (!w) {
            if (s.watch.size() >= 400) continue;
            Watch nw;
            nw.id = d.addr;
            nw.name = d.name;
            nw.kind = K_BLE;
            nw.rssi = nw.rssiPrev = d.rssi;
            nw.extra = d.company;
            nw.secured = false;
            nw.drone = drone;
            nw.first = nw.last = now;
            nw.known = s.learning;
            nw.alerted = false;
            if (!s.learning) {
                s.newCount++;
                if (drone) s.droneCount++;
                s.alertLevel = drone ? 2 : 1;
                s.alertAt = now;
                s.chirpReq = 1;
                nw.alerted = true;
            }
            s.watch.push_back(nw);
        } else {
            w->rssiPrev = w->rssi;
            w->rssi = d.rssi;
            w->last = now;
        }
    }
}

bool onKey(const hal::Key &k) {
    State &s = *S;
    int n = s.watch.size();
    if (k.ch == 'l') { // relearn
        s.watch.clear();
        s.learning = true;
        s.learnUntil = hal::ms() + 12000;
        s.newCount = s.droneCount = 0;
        return true;
    }
    if (k.ch == ' ') { s.armed = !s.armed; return true; }
    if (k.ch == 'm') { s.mute = !s.mute; return true; }
    if (k.ch == 'w' || k.ch == 'b' || k.nav == hal::N_TAB) {
        // switch which radio this ward watches (a single, clean stack switch)
        uint8_t want = k.ch == 'w' ? K_WIFI : k.ch == 'b' ? K_BLE : (s.mode == K_WIFI ? K_BLE : K_WIFI);
        if (want != s.mode) {
            s.mode = want;
            s.watch.clear(); // the other band is a fresh field; relearn it
            s.learning = true;
            s.learnUntil = hal::ms() + 12000;
            s.newCount = s.droneCount = 0;
            if (s.mode == K_WIFI) net::wifiBegin();
            else net::bleBegin();
        }
        return true;
    }
    if (!n) return true;
    if (k.nav == hal::N_UP) s.sel = (s.sel + n - 1) % n;
    else if (k.nav == hal::N_DOWN) s.sel = (s.sel + 1) % n;
    else if (k.nav == hal::N_ENTER) { s.watch[s.sel].known = true; s.watch[s.sel].alerted = false; }
    return true;
}

void draw(uint32_t now) {
    State &s = *S;
    Canvas &cv = hal::canvas();
    cv.fillScreen(C_VOID);

    // single radio, no auto-cycling: the active ward scans its own stack
    if (s.mode == K_WIFI) {
        std::vector<net::AP> aps;
        if (net::wifiScan(aps) >= 0) mergeWifi(aps, now);
    } else {
        std::vector<net::BleDev> devs;
        net::bleScan(devs);
        mergeBle(devs, now);
    }
    if (s.learning && now > s.learnUntil) s.learning = false;

    // age out: nothing dropped, but mark gone (>30s)
    int online = 0, unknown = 0, drones = 0;
    for (auto &w : s.watch) {
        bool here = now - w.last < 30000;
        online += here;
        if (here && !w.known) unknown++;
        if (here && w.drone) drones++;
    }

    bool alerting = s.armed && s.alertLevel > 0 && now - s.alertAt < 2500;
    if (alerting && ((now / 200) & 1)) cv.drawRect(0, 0, 240, 135, s.alertLevel == 2 ? C_GLOW : C_BLOOD);

    const char *band = s.mode == K_WIFI ? "wifi" : "ble";
    char sub[24];
    snprintf(sub, sizeof sub, "%s %s", band, s.learning ? "learn.." : "ward");
    header("SENTINEL", sub);

    // radar on the left
    const int cx = 60, cy = 78, rmax = 52;
    for (int r = 18; r <= rmax; r += 17) cv.drawCircle(cx, cy, r, C_DUSK);
    float sweep = now * 0.003f;
    cv.drawLine(cx, cy, cx + (int)(cosf(sweep) * rmax), cy + (int)(sinf(sweep) * rmax), C_CLOT);
    cv.fillCircle(cx, cy, 2, C_EMBER);
    for (int i = 0; i < (int)s.watch.size(); ++i) {
        Watch &w = s.watch[i];
        if (now - w.last > 30000) continue;
        float ang = (w.id.size() ? (w.id[0] * 7 + w.id[w.id.size() - 1] * 13) : 0) * 0.049f;
        float r = 12 + (-w.rssi - 30) * 0.5f;
        if (r > rmax) r = rmax; if (r < 10) r = 10;
        int x = cx + (int)(cosf(ang) * r), y = cy + (int)(sinf(ang) * r);
        bool here = i == s.sel;
        uint16_t c = w.drone && !w.known ? C_GLOW : !w.known ? C_BLOOD : w.kind == K_WIFI ? C_RUST : C_DIM;
        if (w.drone && !w.known && ((now / 150) & 1)) cv.fillCircle(x, y, 4, C_GLOW);
        cv.fillCircle(x, y, here ? 3 : 2, c);
        if (here) cv.drawCircle(x, y, 5, C_EMBER);
        if (!w.known && w.rssi > w.rssiPrev + 3) { // approaching
            cv.drawLine(x, y, x + (cx - x) / 4, y + (cy - y) / 4, C_EMBER);
        }
    }

    // right: counts + selected
    int px = 126;
    char b[48];
    snprintf(b, sizeof b, "%d near", online);
    text(b, px, 22, C_BONE);
    snprintf(b, sizeof b, "%d unknown", unknown);
    text(b, px, 33, unknown ? C_BLOOD : C_DIM);
    snprintf(b, sizeof b, "%d drone?", drones);
    text(b, px, 44, drones ? C_GLOW : C_DIM);
    cv.drawFastHLine(px, 55, 112, C_CLOT);

    // list of newest unknown first? just list window
    const int rows = 5, rh = 11, y0 = 60;
    int n = s.watch.size();
    if (s.sel >= n) s.sel = n ? n - 1 : 0;
    if (s.sel < s.top) s.top = s.sel;
    if (s.sel >= s.top + rows) s.top = s.sel - rows + 1;
    for (int i = 0; i < rows && s.top + i < n; ++i) {
        Watch &w = s.watch[s.top + i];
        int y = y0 + i * rh;
        bool here = s.top + i == s.sel;
        if (here) cv.fillRect(px - 2, y - 1, 116, rh, C_DUSK);
        uint16_t c = w.drone && !w.known ? C_GLOW : !w.known ? C_BLOOD : C_DIM;
        char tag = w.drone ? '!' : w.known ? ' ' : '*';
        char nm[13] = {};
        const char *src = w.name.empty() ? w.id.c_str() : w.name.c_str();
        strncpy(nm, src, 12);
        snprintf(b, sizeof b, "%c%s", tag, nm);
        text(b, px, y, here ? C_GLOW : c);
        snprintf(b, sizeof b, "%d", w.rssi);
        textRight(b, px + 112, y, w.rssi > -60 ? C_EMBER : C_DIM);
    }

    char foot[48];
    snprintf(foot, sizeof foot, "%s%s ent:know w/b:band spc:%s", s.mute ? "MUTE " : "",
             s.armed ? "ARMED" : "idle", s.armed ? "disarm" : "arm");
    footer(foot);
}
} // namespace

const Rite RITE_SENTINEL = {"SENTINEL", "sentinel", "ward: alert on intruders", enter, leave, onKey, draw};
