// Cardputer ADV backend. M5Cardputer 1.1.x detects the ADV (TCA8418 keyboard,
// ES8311 codec) itself; this file only adapts it to hal.h.
#include "hal.h"
#include <M5Cardputer.h>

namespace hal {
namespace {
Canvas *cv = nullptr;
volatile AudioFn audioFn = nullptr;
uint8_t vol = 140;

constexpr int BLK = 256;
int16_t abuf[3][BLK];

volatile AudioFn overlayFn = nullptr;
int16_t obuf[BLK];

void audioTask(void *) {
    int i = 0;
    for (;;) {
        // playRaw queues at most two buffers per channel and does not copy, so
        // with three buffers the one being filled is never in flight.
        while (M5Cardputer.Speaker.isPlaying(0) >= 2) vTaskDelay(1);
        AudioFn fn = audioFn;
        if (fn) fn(abuf[i], BLK);
        else memset(abuf[i], 0, sizeof(abuf[i]));
        AudioFn ofn = overlayFn; // UI sfx, mixed on top of whatever's playing
        if (ofn) {
            ofn(obuf, BLK);
            for (int k = 0; k < BLK; ++k) {
                int32_t s = abuf[i][k] + obuf[k];
                abuf[i][k] = s > 32767 ? 32767 : s < -32768 ? -32768 : s;
            }
        }
        M5Cardputer.Speaker.playRaw(abuf[i], BLK, RATE, false, 1, 0);
        i = (i + 1) % 3;
    }
}

// Key queue with auto-repeat for arrows / backspace.
Key queue[16];
uint8_t qHead = 0, qTail = 0;
void push(const Key &k) {
    uint8_t n = (qTail + 1) & 15;
    if (n == qHead) return;
    queue[qTail] = k;
    qTail = n;
}
Key held;
bool heldValid = false;
uint32_t heldSince = 0, lastRepeat = 0;

uint8_t navFor(char c) {
    switch (c) {
    case ';': return N_UP;
    case '.': return N_DOWN;
    case ',': return N_LEFT;
    case '/': return N_RIGHT;
    case '`': return N_ESC;
    }
    return N_NONE;
}

void scanKeys() {
    M5Cardputer.update();
    auto &kb = M5Cardputer.Keyboard;
    uint32_t now = millis();
    if (kb.isChange()) {
        heldValid = false;
        if (kb.isPressed()) {
            auto st = kb.keysState();
            Key base;
            base.fn = st.fn;
            base.ctrl = st.ctrl;
            bool any = false;
            for (char c : st.word) {
                Key k = base;
                k.ch = c;
                k.nav = navFor(c);
                push(k);
                held = k;
                any = true;
            }
            if (st.enter) { Key k = base; k.nav = N_ENTER; push(k); held = k; any = true; }
            if (st.del) { Key k = base; k.nav = N_BACK; push(k); held = k; any = true; }
            if (st.tab) { Key k = base; k.nav = N_TAB; push(k); any = true; }
            if (any) { heldValid = true; heldSince = now; lastRepeat = now; }
        }
    } else if (heldValid && kb.isPressed()) {
        bool repeats = held.nav == N_UP || held.nav == N_DOWN || held.nav == N_LEFT ||
                       held.nav == N_RIGHT || held.nav == N_BACK;
        if (repeats && now - heldSince > 380 && now - lastRepeat > 70) {
            push(held);
            lastRepeat = now;
        }
    }
    // G0 (the button on top) is Esc/back, like the factory firmware: it leaves
    // a rite, and twice in the menu goes back to the Launcher.
    if (M5Cardputer.BtnA.wasPressed()) { Key k; k.nav = N_ESC; k.ch = '`'; push(k); }
}
} // namespace

void begin() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setBrightness(110);
    cv = new Canvas(&M5Cardputer.Display);
    cv->setColorDepth(16);
    cv->createSprite(W, H);
    M5Cardputer.Speaker.begin();
    M5Cardputer.Speaker.setVolume(vol);
    xTaskCreatePinnedToCore(audioTask, "grim-audio", 4096, nullptr, 5, nullptr, 0);
}

Canvas &canvas() { return *cv; }
void present() { cv->pushSprite(0, 0); }
uint32_t ms() { return millis(); }
void idle(uint32_t msec) {
    scanKeys();
    if (msec) delay(msec);
}

bool pollKey(Key &k) {
    scanKeys();
    if (qHead == qTail) return false;
    k = queue[qHead];
    qHead = (qHead + 1) & 15;
    return true;
}
bool anyHeld() { return M5Cardputer.Keyboard.isPressed(); }

void setAudio(AudioFn fn) { audioFn = fn; }
void setOverlayAudio(AudioFn fn) { overlayFn = fn; }
void setVolume(uint8_t v) {
    vol = v;
    M5Cardputer.Speaker.setVolume(v);
}
uint8_t volume() { return vol; }

int batteryPercent() { return M5Cardputer.Power.getBatteryLevel(); }
void backToLauncher() {
    audioFn = nullptr;
    delay(50);
    esp_restart();
}

} // namespace hal

void grimSetup();
void grimLoop();
void setup() { grimSetup(); }
void loop() { grimLoop(); }
