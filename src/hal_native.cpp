// Desktop backend: LovyanGFX's SDL panel at 240x135 (x3), the laptop keyboard
// standing in for the Cardputer's, SDL audio out (or a WAV file).
//
// Env vars for headless previews (SDL_VIDEODRIVER=offscreen):
//   GRIM_SCRIPT="ms:action;..."  actions: shot:<file.ppm>, key:<chars>, enter,
//                                back, esc, tab, up, down, left, right, quit
//                                (times are from start; key: types each char)
//   GRIM_WAV=<file.wav>          write the audio stream to a WAV instead of SDL
#include "hal.h"
#include <lgfx/v1/platforms/sdl/Panel_sdl.hpp>

#include <SDL2/SDL.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

void grimSetup();
void grimLoop();

namespace {
struct Board : public lgfx::LGFX_Device {
    lgfx::Panel_sdl panel;
    Board() {
        auto cfg = panel.config();
        cfg.memory_width = cfg.panel_width = hal::W;
        cfg.memory_height = cfg.panel_height = hal::H;
        panel.config(cfg);
        panel.setScaling(3, 3);
        panel.setWindowTitle("daedric grimoire");
        setPanel(&panel);
    }
};
Board *lcd = nullptr;
Canvas *cv = nullptr;
std::atomic<hal::AudioFn> audioFn{nullptr};
std::atomic<hal::AudioFn> overlayFn{nullptr};
uint8_t vol = 140;

std::mutex qLock;
hal::Key queue[64];
int qHead = 0, qTail = 0;
void push(const hal::Key &k) {
    std::lock_guard<std::mutex> g(qLock);
    int n = (qTail + 1) & 63;
    if (n == qHead) return;
    queue[qTail] = k;
    qTail = n;
}

uint8_t navFor(char c) {
    switch (c) {
    case ';': return hal::N_UP;
    case '.': return hal::N_DOWN;
    case ',': return hal::N_LEFT;
    case '/': return hal::N_RIGHT;
    case '`': return hal::N_ESC;
    }
    return hal::N_NONE;
}

// --- laptop keyboard -------------------------------------------------------
// Panel_sdl pumps SDL events on the main thread; we sample the key state here
// and turn edges into Keys. Desktop arrows map to Fn+;./, so they work inside
// text editors too.
const char *SHIFTED = ")!@#$%^&*(";
char shiftChar(char c) {
    if (c >= 'a' && c <= 'z') return c - 32;
    if (c >= '0' && c <= '9') return SHIFTED[c - '0'];
    switch (c) {
    case '-': return '_';
    case '=': return '+';
    case '[': return '{';
    case ']': return '}';
    case '\\': return '|';
    case ';': return ':';
    case '\'': return '"';
    case ',': return '<';
    case '.': return '>';
    case '/': return '?';
    case '`': return '~';
    }
    return c;
}

void scanSdl() {
    static uint8_t prev[SDL_NUM_SCANCODES];
    static uint32_t heldSince[SDL_NUM_SCANCODES];
    int n = 0;
    const uint8_t *st = SDL_GetKeyboardState(&n);
    if (!st) return;
    SDL_Keymod mod = SDL_GetModState();
    bool shift = mod & KMOD_SHIFT, ctrl = mod & KMOD_CTRL, alt = mod & KMOD_ALT;
    uint32_t now = SDL_GetTicks();
    for (int sc = 0; sc < n && sc < SDL_NUM_SCANCODES; ++sc) {
        bool down = st[sc], was = prev[sc];
        prev[sc] = down;
        bool edge = down && !was;
        if (down && !edge) {
            // repeat for arrows/backspace
            if (now - heldSince[sc] > 380) {
                heldSince[sc] = now - 380 + 70;
                SDL_Keycode kc0 = SDL_GetKeyFromScancode((SDL_Scancode)sc);
                if (kc0 == SDLK_UP || kc0 == SDLK_DOWN || kc0 == SDLK_LEFT || kc0 == SDLK_RIGHT ||
                    kc0 == SDLK_BACKSPACE)
                    edge = true;
            }
            if (!edge) continue;
        }
        if (!edge) continue;
        if (!was) heldSince[sc] = now;
        SDL_Keycode kc = SDL_GetKeyFromScancode((SDL_Scancode)sc);
        hal::Key k;
        k.fn = alt; // Alt plays the Fn key
        k.ctrl = ctrl;
        switch (kc) {
        case SDLK_RETURN: k.nav = hal::N_ENTER; break;
        case SDLK_BACKSPACE: k.nav = hal::N_BACK; break;
        case SDLK_ESCAPE: k.nav = hal::N_ESC; k.ch = '`'; break;
        case SDLK_TAB: k.nav = hal::N_TAB; break;
        case SDLK_UP: k.nav = hal::N_UP; k.ch = ';'; k.fn = true; break;
        case SDLK_DOWN: k.nav = hal::N_DOWN; k.ch = '.'; k.fn = true; break;
        case SDLK_LEFT: k.nav = hal::N_LEFT; k.ch = ','; k.fn = true; break;
        case SDLK_RIGHT: k.nav = hal::N_RIGHT; k.ch = '/'; k.fn = true; break;
        case SDLK_SPACE: k.ch = ' '; break;
        default:
            if (kc >= 32 && kc < 127) {
                char c = (char)kc;
                if (shift) c = shiftChar(c);
                k.ch = c;
                if (!shift) k.nav = navFor(c);
            } else continue;
        }
        push(k);
    }
}

// --- scripted previews ---------------------------------------------------
void saveShot(const std::string &path) {
    std::vector<uint16_t> buf(hal::W * hal::H);
    lcd->readRect(0, 0, hal::W, hal::H, buf.data());
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", hal::W, hal::H);
    for (uint16_t px : buf) {
        uint16_t c = (px >> 8) | (px << 8);
        uint8_t rgb[3] = {uint8_t(((c >> 11) & 31) * 255 / 31), uint8_t(((c >> 5) & 63) * 255 / 63),
                          uint8_t((c & 31) * 255 / 31)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

void runScript() {
    const char *env = getenv("GRIM_SCRIPT");
    if (!env) return;
    std::thread([s = std::string(env)]() {
        auto t0 = std::chrono::steady_clock::now();
        size_t pos = 0;
        while (pos < s.size()) {
            size_t end = s.find(';', pos);
            std::string step = s.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
            pos = end == std::string::npos ? s.size() : end + 1;
            size_t colon = step.find(':');
            if (colon == std::string::npos) continue;
            int at = atoi(step.substr(0, colon).c_str());
            std::string act = step.substr(colon + 1);
            std::this_thread::sleep_until(t0 + std::chrono::milliseconds(at));
            hal::Key k;
            if (act.rfind("shot:", 0) == 0) { saveShot(act.substr(5)); continue; }
            if (act.rfind("key:", 0) == 0) {
                // %3B = ';' since ';' separates steps
                std::string t = act.substr(4);
                for (size_t i = 0; i < t.size(); ++i) {
                    hal::Key c;
                    if (t[i] == '%' && i + 2 < t.size()) {
                        c.ch = (char)strtol(t.substr(i + 1, 2).c_str(), nullptr, 16);
                        i += 2;
                    } else c.ch = t[i];
                    push(c);
                    std::this_thread::sleep_for(std::chrono::milliseconds(15));
                }
                continue;
            }
            if (act == "enter") k.nav = hal::N_ENTER;
            else if (act == "back") k.nav = hal::N_BACK;
            else if (act == "esc") { k.nav = hal::N_ESC; k.ch = '`'; }
            else if (act == "tab") k.nav = hal::N_TAB;
            else if (act == "up") { k.nav = hal::N_UP; k.ch = ';'; }
            else if (act == "down") { k.nav = hal::N_DOWN; k.ch = '.'; }
            else if (act == "left") { k.nav = hal::N_LEFT; k.ch = ','; }
            else if (act == "right") { k.nav = hal::N_RIGHT; k.ch = '/'; }
            else if (act == "quit") _exit(0);
            else continue;
            push(k);
        }
    }).detach();
}

// --- audio -------------------------------------------------------------------
void render(int16_t *out, int n, bool scale = true) {
    hal::AudioFn fn = audioFn.load();
    if (fn) fn(out, n);
    else memset(out, 0, n * 2);
    hal::AudioFn ofn = overlayFn.load(); // UI sfx mixed over the main voice
    if (ofn && n <= 4096) {
        int16_t ob[4096];
        ofn(ob, n);
        for (int i = 0; i < n; ++i) {
            int32_t m = out[i] + ob[i];
            out[i] = (int16_t)(m > 32767 ? 32767 : m < -32768 ? -32768 : m);
        }
    }
    if (!scale) return; // WAV dumps record what the codec would get
    for (int i = 0; i < n; ++i) out[i] = (int16_t)((int32_t)out[i] * vol / 255);
}

void sdlCallback(void *, uint8_t *stream, int len) { render((int16_t *)stream, len / 2); }

void wavWriter(std::string path) {
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) return;
    auto u32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
    fwrite("RIFF", 1, 4, f); u32(0); fwrite("WAVEfmt ", 1, 8, f);
    u32(16); u16(1); u16(1); u32(hal::RATE); u32(hal::RATE * 2); u16(2); u16(16);
    fwrite("data", 1, 4, f); u32(0);
    uint32_t total = 0;
    int16_t blk[256];
    auto t0 = std::chrono::steady_clock::now();
    for (uint64_t frames = 0;; frames += 256) {
        std::this_thread::sleep_until(t0 + std::chrono::microseconds(frames * 1000000 / hal::RATE));
        render(blk, 256, false);
        fwrite(blk, 2, 256, f);
        total += 512;
        // keep the header valid so the file is playable whenever the sim quits
        long here = ftell(f);
        fseek(f, 4, SEEK_SET); u32(36 + total);
        fseek(f, 40, SEEK_SET); u32(total);
        fseek(f, here, SEEK_SET);
        fflush(f);
    }
}

void startAudio() {
    if (const char *wav = getenv("GRIM_WAV")) {
        std::thread(wavWriter, std::string(wav)).detach();
        return;
    }
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return;
    SDL_AudioSpec want{}, have{};
    want.freq = hal::RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 512;
    want.callback = sdlCallback;
    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (dev) SDL_PauseAudioDevice(dev, 0);
}

int userFunc(bool *running) {
    runScript();
    grimSetup();
    while (*running) grimLoop();
    return 0;
}
} // namespace

namespace hal {
void begin() {
    lcd->init();
    lcd->setRotation(0);
    cv = new Canvas(lcd);
    cv->setColorDepth(16);
    cv->createSprite(W, H);
    startAudio();
}
Canvas &canvas() { return *cv; }
void present() { cv->pushSprite(0, 0); }
uint32_t ms() { return SDL_GetTicks(); }
void idle(uint32_t msec) {
    scanSdl();
    if (msec) SDL_Delay(msec);
}
bool pollKey(Key &k) {
    scanSdl();
    std::lock_guard<std::mutex> g(qLock);
    if (qHead == qTail) return false;
    k = queue[qHead];
    qHead = (qHead + 1) & 63;
    return true;
}
bool anyHeld() {
    int n = 0;
    const uint8_t *st = SDL_GetKeyboardState(&n);
    for (int i = 0; st && i < n; ++i)
        if (st[i]) return true;
    return false;
}
void setAudio(AudioFn fn) { audioFn = fn; }
void setOverlayAudio(AudioFn fn) { overlayFn = fn; }
void setVolume(uint8_t v) { vol = v; }
uint8_t volume() { return vol; }
int batteryPercent() { return 77; }
void backToLauncher() { _exit(0); }
} // namespace hal

int main(int, char **) {
    lcd = new Board();
    return lgfx::Panel_sdl::main(userFunc, 128);
}
