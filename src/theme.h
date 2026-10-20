// Daedric / Lain skin shared by every rite: palette (from ~/daedric-login,
// same as the Launcher fork), Daedric glyph text, studded frames, chrome.
#pragma once
#include "daedric_glyphs.h"
#include "hal.h"

// RGB565
#define C_VOID 0x0020
#define C_ASH 0x0840
#define C_DUSK 0x1820
#define C_CLOT 0x3861
#define C_BLOOD 0x78E2
#define C_RUST 0xAA05
#define C_EMBER 0xC307
#define C_GLOW 0xE4CD
#define C_BONE 0xB531
#define C_DIM 0x6A47
#define C_UMBER 0x3923
#define C_STONE 0x39A6

namespace theme {

uint16_t blend(uint16_t fg, uint16_t bg, uint8_t a); // a 0..255
// 0..255 -> void..clot..blood..rust..ember..glow..bone ramp
uint16_t ramp(uint8_t v);

int glyphWidth(const DaedricFont &f, const char *text, int spacing = 2);
int glyphs(const DaedricFont &f, const char *text, int x, int y, uint16_t fg, int spacing = 2);
void glyphsCentre(const DaedricFont &f, const char *text, int cx, int y, uint16_t fg);

// Reveal animation: the same word drawn as Daedric runes dissolving into latin
// letters as `reveal` runs 0..1 (0 = pure runes, 1 = pure english). Both are
// centred on cx; alpha blends toward `bg`. Use for selected menu/list items.
void wordDissolve(const DaedricFont &f, const char *s, int cx, int yRune, int yLatin,
                  float reveal, uint16_t runeCol, uint16_t latCol, uint16_t bg, int latSize = 1);
float smoothstep(float e0, float e1, float x);

void diamond(int cx, int cy, int r, uint16_t c, bool filled = true);
void frame(int x, int y, int w, int h, uint16_t line, uint16_t stud);

// Small built-in 6x8 font helpers (transparent background).
void text(const char *s, int x, int y, uint16_t c, int size = 1);
void textCentre(const char *s, int cx, int y, uint16_t c, int size = 1);
void textRight(const char *s, int rx, int y, uint16_t c, int size = 1);

// Top bar: Daedric name + latin name, battery; bottom hint line.
void header(const char *daedric, const char *latin);
void footer(const char *hint);

} // namespace theme
