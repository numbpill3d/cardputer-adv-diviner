// A "rite" is one applet in the grimoire. The shell (main.cpp) owns the menu
// and calls into the active rite every frame; a rite draws its whole screen
// into hal::canvas() and may install an audio callback with hal::setAudio().
#pragma once
#include "hal.h"

struct Rite {
    const char *glyph; // Daedric name (A-Z only, rendered with the glyph font)
    const char *name;  // latin name
    const char *desc;  // one line for the menu
    void (*enter)();
    void (*leave)();
    bool (*key)(const hal::Key &k); // false = leave the rite
    void (*frame)(uint32_t now);
};

extern const Rite RITE_DRONE;
extern const Rite RITE_SEQ;
extern const Rite RITE_BEAT;
extern const Rite RITE_WIRED;
extern const Rite RITE_VISIONS;
extern const Rite RITE_ORACLE;
extern const Rite RITE_LINK;
extern const Rite RITE_SCRY;
extern const Rite RITE_LISTEN;
extern const Rite RITE_KIN;
extern const Rite RITE_NET;
extern const Rite RITE_GEMINI;
extern const Rite RITE_IR;
extern const Rite RITE_SENTINEL;
extern const Rite RITE_PULSE;
extern const Rite RITE_PROBE;
