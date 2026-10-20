#include "theme.h"
#include <stdio.h>
#include <string.h>

namespace theme {

uint16_t blend(uint16_t fg, uint16_t bg, uint8_t a) {
    if (a == 255) return fg;
    if (a == 0) return bg;
    uint32_t r = (((fg >> 11) & 31) * a + ((bg >> 11) & 31) * (255 - a)) / 255;
    uint32_t g = (((fg >> 5) & 63) * a + ((bg >> 5) & 63) * (255 - a)) / 255;
    uint32_t b = ((fg & 31) * a + (bg & 31) * (255 - a)) / 255;
    return (r << 11) | (g << 5) | b;
}

uint16_t ramp(uint8_t v) {
    static const uint16_t stops[] = {C_VOID, C_DUSK, C_CLOT, C_BLOOD, C_RUST, C_EMBER, C_GLOW, C_BONE};
    static uint16_t lut[256];
    static bool built = false;
    if (!built) {
        for (int i = 0; i < 256; ++i) {
            int seg = i * 7 / 256;
            int t = (i * 7) % 256;
            lut[i] = blend(stops[seg + 1], stops[seg], t);
        }
        built = true;
    }
    return lut[v];
}

static int glyphIndex(char c) {
    if (c >= 'a' && c <= 'z') c -= 32;
    return (c >= 'A' && c <= 'Z') ? c - 'A' : -1;
}

// Glyph pixels are blended against whatever is already in the canvas, so
// glyphs can sit on plasma, frames, anything.
static int drawGlyph(const DaedricFont &f, int idx, int x, int y, uint16_t fg) {
    Canvas &cv = hal::canvas();
    const DaedricGlyph &g = f.glyphs[idx];
    const uint8_t *d = f.data + g.offset;
    const int stride = (g.w + 3) / 4;
    static const uint8_t alpha[4] = {0, 90, 175, 255};
    for (int row = 0; row < f.h; ++row) {
        const uint8_t *line = d + row * stride;
        int py = y + row;
        if (py < 0 || py >= hal::H) continue;
        for (int col = 0; col < g.w; ++col) {
            uint8_t a = (line[col >> 2] >> (6 - 2 * (col & 3))) & 3;
            if (!a) continue;
            int px = x + col;
            if (px < 0 || px >= hal::W) continue;
            if (a == 3) cv.drawPixel(px, py, fg);
            else {
                uint16_t bg = cv.readPixel(px, py);
                cv.drawPixel(px, py, blend(fg, bg, alpha[a]));
            }
        }
    }
    return g.w;
}

int glyphWidth(const DaedricFont &f, const char *text, int spacing) {
    int w = 0;
    for (const char *p = text; *p; ++p) {
        int idx = glyphIndex(*p);
        w += (idx < 0 ? f.space : f.glyphs[idx].w) + spacing;
    }
    return w > 0 ? w - spacing : 0;
}

int glyphs(const DaedricFont &f, const char *text, int x, int y, uint16_t fg, int spacing) {
    int cx = x;
    for (const char *p = text; *p; ++p) {
        int idx = glyphIndex(*p);
        cx += (idx < 0 ? f.space : drawGlyph(f, idx, cx, y, fg)) + spacing;
    }
    return cx - x;
}

void glyphsCentre(const DaedricFont &f, const char *text, int cx, int y, uint16_t fg) {
    glyphs(f, text, cx - glyphWidth(f, text) / 2, y, fg);
}

float smoothstep(float e0, float e1, float x) {
    if (e1 <= e0) return x < e0 ? 0.f : 1.f;
    float t = (x - e0) / (e1 - e0);
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    return t * t * (3 - 2 * t);
}

// Runes fade out over the first ~60% of the reveal; the latin word fades in
// over the last ~55%, so they briefly cross-dissolve in place.
void wordDissolve(const DaedricFont &f, const char *s, int cx, int yRune, int yLatin,
                  float reveal, uint16_t runeCol, uint16_t latCol, uint16_t bg, int latSize) {
    float runeA = 1.f - smoothstep(0.0f, 0.6f, reveal);
    float latA = smoothstep(0.45f, 1.0f, reveal);
    if (runeA > 0.02f)
        glyphsCentre(f, s, cx, yRune, blend(runeCol, bg, (uint8_t)(runeA * 255)));
    if (latA > 0.02f)
        textCentre(s, cx, yLatin, blend(latCol, bg, (uint8_t)(latA * 255)), latSize);
}

void diamond(int cx, int cy, int r, uint16_t c, bool filled) {
    Canvas &cv = hal::canvas();
    if (filled) {
        cv.fillTriangle(cx - r, cy, cx, cy - r, cx + r, cy, c);
        cv.fillTriangle(cx - r, cy, cx, cy + r, cx + r, cy, c);
    } else {
        cv.drawLine(cx - r, cy, cx, cy - r, c);
        cv.drawLine(cx, cy - r, cx + r, cy, c);
        cv.drawLine(cx + r, cy, cx, cy + r, c);
        cv.drawLine(cx, cy + r, cx - r, cy, c);
    }
}

void frame(int x, int y, int w, int h, uint16_t line, uint16_t stud) {
    hal::canvas().drawRect(x, y, w, h, line);
    diamond(x, y, 2, stud);
    diamond(x + w - 1, y, 2, stud);
    diamond(x, y + h - 1, 2, stud);
    diamond(x + w - 1, y + h - 1, 2, stud);
}

void text(const char *s, int x, int y, uint16_t c, int size) {
    Canvas &cv = hal::canvas();
    cv.setFont(&fonts::Font0);
    cv.setTextSize(size);
    cv.setTextColor(c);
    cv.setTextDatum(textdatum_t::top_left);
    cv.drawString(s, x, y);
}
void textCentre(const char *s, int cx, int y, uint16_t c, int size) {
    text(s, cx - (int)strlen(s) * 6 * size / 2, y, c, size);
}
void textRight(const char *s, int rx, int y, uint16_t c, int size) {
    text(s, rx - (int)strlen(s) * 6 * size, y, c, size);
}

void header(const char *daedric, const char *latin) {
    Canvas &cv = hal::canvas();
    int w = glyphs(DAEDRIC_S, daedric, 4, 1, C_EMBER);
    text(latin, 4 + w + 6, 5, C_DIM);
    int b = hal::batteryPercent();
    if (b >= 0) {
        char buf[8];
        snprintf(buf, sizeof buf, "%d%%", b);
        textRight(buf, 214, 5, C_DIM);
        cv.drawRect(218, 4, 16, 8, C_DIM);
        cv.fillRect(234, 6, 2, 4, C_DIM);
        cv.fillRect(220, 6, 12 * (b > 100 ? 100 : b) / 100, 4, b < 20 ? C_BLOOD : C_RUST);
    }
    cv.drawFastHLine(0, 18, hal::W, C_CLOT);
    diamond(hal::W / 2, 18, 2, C_RUST);
}

void footer(const char *hint) {
    Canvas &cv = hal::canvas();
    cv.drawFastHLine(0, hal::H - 11, hal::W, C_CLOT);
    textCentre(hint, hal::W / 2, hal::H - 9, C_DIM);
}

} // namespace theme
