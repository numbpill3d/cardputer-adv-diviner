// Network seam: Wi-Fi, BLE scan, channel sniffing and Gemini, behind one
// interface so the rites compile and preview on the desktop (net_native.cpp
// returns canned data) and run for real on the Cardputer (net_device.cpp:
// Arduino WiFi, Bluedroid BLE, esp_wifi promiscuous mode, WiFiClientSecure).
//
// Only one radio stack is ever up at a time: a rite calls wifiBegin() or
// bleBegin() in enter() and radioStop() in leave(), so TLS and BLE never
// fight for heap on the PSRAM-less S3.
#pragma once
#include <stdint.h>
#include <string>
#include <vector>

namespace net {

// ---- Wi-Fi ---------------------------------------------------------------
enum WifiState { WST_OFF, WST_SCANNING, WST_CONNECTING, WST_ONLINE, WST_FAILED };

struct AP {
    std::string ssid;
    uint8_t bssid[6];
    int8_t rssi;
    uint8_t channel;
    uint8_t auth; // 0 open, else secured
    bool known;   // we have saved credentials
};

void wifiBegin();            // bring the radio up (STA), load saved creds
void radioStop();            // tear down whatever radio is up
WifiState wifiState();
int wifiScan(std::vector<AP> &out); // async: returns count, -1 while running; call again
void wifiConnect(const char *ssid, const char *pass);
void wifiForget(const char *ssid);
std::string wifiIP();
std::string wifiSSID();
int wifiRSSI();
// Saved networks (SSIDs only; passwords live in NVS, never returned).
std::vector<std::string> wifiSaved();

// ---- channel traffic (listen) -------------------------------------------
// Puts the radio in receive-only promiscuous mode and counts frames per
// 2.4GHz channel. counts[0..13] are packets since the last poll; mgmt/data
// split is approximate. Never transmits.
void snifferBegin();
void snifferSetChannel(int ch);
void snifferPoll(uint32_t mgmt[14], uint32_t data[14], int &rssiLast);

// ---- BLE (familiars / kin) ----------------------------------------------
struct BleDev {
    std::string name;
    std::string addr;
    int8_t rssi;
    uint16_t company; // manufacturer id, 0xFFFF unknown
    bool connectable;
    uint32_t lastSeen; // ms
};
void bleBegin();
int bleScan(std::vector<BleDev> &out); // -1 while a scan is running

// ---- LAN scan (net rites) -----------------------------------------------
struct Host {
    std::string ip;
    std::string name;
    std::vector<uint16_t> ports;
    bool up;
};
// Sweep the local /24 for live hosts, then common ports. Progress 0..100 via
// lanProgress(); results via lanHosts(). Runs on a worker task.
void lanBegin();
int lanProgress();
std::vector<Host> lanHosts();

// ---- CSI motion sense (pulse) -------------------------------------------
// Reads Wi-Fi channel-state info from the connected link and turns its
// variation into a motion signal: bodies moving through the radio field
// perturb the subcarriers. Needs an active connection (join one in LINK).
void csiBegin();
void csiEnd();
// motion: 0..1 smoothed; bins: per-subcarrier amplitude (0..1) for a waterfall
// (up to nbins written); returns the number of bins, or -1 if no data yet.
int csiRead(float &motion, float *bins, int nbins);

// ---- host probe (active HTTP recon / default creds) ----------------------
struct ProbeLine {
    std::string text;
    uint8_t kind; // 0 info, 1 hit (interesting), 2 vuln (e.g. default creds), 3 error
};
void probeBegin(const char *ip, uint16_t port); // HTTP enum + default-cred check
int probeProgress();
std::vector<ProbeLine> probeResults();
bool probeDone();

// ---- Gemini (gemtext) ---------------------------------------------------
// One request at a time on a worker task; poll gemState()/gemBody().
enum GemState { GEM_IDLE, GEM_FETCHING, GEM_DONE, GEM_INPUT, GEM_ERROR };
void gemGet(const char *url);
void gemStop();
GemState gemState();
int gemStatus();            // the 2-digit Gemini status, e.g. 20, 31, 51
std::string gemMeta();      // mime/meta or redirect/input prompt
std::string gemBody();      // the gemtext (for status 2x)
std::string gemURL();       // the URL the body belongs to (after redirects)
int gemProgress();          // bytes received, for the spinner

} // namespace net
