// Hardware seam: everything above this file is the same on the Cardputer ADV
// (hal_device.cpp: M5Cardputer/M5Unified, ES8311 codec, TCA8418 keyboard) and
// on the desktop (hal_native.cpp: LovyanGFX SDL panel, SDL audio or a WAV dump).
#pragma once
#include <stdint.h>

#ifdef GRIM_DEVICE
#include <M5GFX.h>
using Canvas = M5Canvas;
#else
#include <LovyanGFX.hpp>
using Canvas = LGFX_Sprite;
#endif

namespace hal {

constexpr int W = 240, H = 135;
constexpr int RATE = 24000; // audio sample rate, mono int16

// Navigation codes. On the Cardputer the arrow keys are ; . , / (printed on
// the caps); a Key carries both the character and the arrow meaning, and each
// screen decides which one it wants (text editors only take arrows with Fn).
enum Nav : uint8_t { N_NONE, N_UP, N_DOWN, N_LEFT, N_RIGHT, N_ENTER, N_BACK, N_ESC, N_TAB };

struct Key {
    char ch = 0;      // printable character after shift, 0 if none
    uint8_t nav = 0;  // Nav
    bool fn = false;  // Fn held
    bool ctrl = false;
};

typedef void (*AudioFn)(int16_t *out, int n);

void begin();
Canvas &canvas(); // 240x135 back buffer; draw here, then present()
void present();
uint32_t ms();
void idle(uint32_t msec);

bool pollKey(Key &k);  // one queued key press (with auto-repeat for arrows)
bool anyHeld();

void setAudio(AudioFn fn);        // main voice (rite/menu); nullptr = silence
void setOverlayAudio(AudioFn fn); // mixed on top of the main voice (UI sfx)
void setVolume(uint8_t v); // 0..255
uint8_t volume();

int batteryPercent(); // -1 if unknown
void backToLauncher(); // restart; the Launcher bootscreen comes up first

} // namespace hal
