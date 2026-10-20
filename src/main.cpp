// Daedric Grimoire shell: splash, rite carousel, volume, and the frame loop
// that drives whichever rite is open.
#include "hal.h"
#include "rite.h"
#include "uisfx.h"
#include "theme.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

using namespace theme;

namespace {
const Rite *const RITES[] = {&RITE_DRONE,  &RITE_SEQ,    &RITE_BEAT,
                             &RITE_WIRED,  &RITE_VISIONS, &RITE_ORACLE,
                             &RITE_LINK,   &RITE_SCRY,   &RITE_LISTEN,
                             &RITE_KIN,    &RITE_NET,    &RITE_GEMINI, &RITE_IR,
                             &RITE_SENTINEL, &RITE_PULSE, &RITE_PROBE};
constexpr int N_RITES = sizeof(RITES) / sizeof(RITES[0]);
constexpr uint32_t FRAME_MS = 33;

enum Mode { SPLASH, MENU, RITE };
Mode mode = SPLASH;
int sel = 0;
float slide = 0; // animated carousel position, eases toward sel
const Rite *active = nullptr;
uint32_t t0 = 0, volShownAt = 0, escArmedAt = 0;

struct Mote {
    float x, y, vy;
    uint8_t life;
} motes[18];

void motesInit() {
    for (auto &m : motes) {
        m.x = rand() % hal::W;
        m.y = rand() % hal::H;
        m.vy = 0.15f + (rand() % 100) / 220.f;
        m.life = rand() % 255;
    }
}
void motesDraw() {
    Canvas &cv = hal::canvas();
    for (auto &m : motes) {
        m.y -= m.vy;
        m.life += 3;
        if (m.y < 0) {
            m.y = hal::H;
            m.x = rand() % hal::W;
        }
        uint8_t a = (uint8_t)(128 + 127 * sinf(m.life * 0.0245f));
        cv.drawPixel((int)m.x, (int)m.y, blend(C_EMBER, C_VOID, a));
    }
}

void heptagram(int cx, int cy, int r, float a, uint16_t c) {
    Canvas &cv = hal::canvas();
    int px[7], py[7];
    for (int i = 0; i < 7; ++i) {
        float t = a + i * 6.2831853f / 7;
        px[i] = cx + (int)(cosf(t) * r);
        py[i] = cy + (int)(sinf(t) * r);
    }
    for (int i = 0; i < 7; ++i) cv.drawLine(px[i], py[i], px[(i + 3) % 7], py[(i + 3) % 7], c);
    for (int i = 0; i < 7; ++i) diamond(px[i], py[i], 1, C_GLOW);
}

/* ------------------------------------------------------------ splash */
const char *const LINES[] = {"present day.", "present time.", "the wired remembers.", "let us begin."};

void drawSplash(uint32_t now) {
    Canvas &cv = hal::canvas();
    uint32_t t = now - t0;
    cv.fillScreen(C_VOID);
    motesDraw();
    int cx = 62, cy = 67;
    cv.drawCircle(cx, cy, 50, C_CLOT);
    cv.drawCircle(cx, cy, 31, C_CLOT);
    const char *ring = "THEGRIMOIREOFTHEWIRED";
    int n = strlen(ring);
    for (int i = 0; i < n; ++i) {
        float a = -1.5708f + i * 6.2831853f / n + t * 0.00025f;
        char g[2] = {ring[i], 0};
        bool lit = ((t / 140) % n) == (uint32_t)i;
        int gx = cx + (int)(cosf(a) * 40.5f) - 4, gy = cy + (int)(sinf(a) * 40.5f) - 8;
        glyphs(DAEDRIC_S, g, gx, gy, lit ? C_GLOW : (i & 1 ? C_BLOOD : C_RUST));
    }
    heptagram(cx, cy, 28, t * 0.0012f, C_BLOOD);
    cv.fillCircle(cx, cy, 13, C_DUSK);
    glyphsCentre(DAEDRIC_M, "G", cx + 1, cy - 10, C_CLOT);
    glyphsCentre(DAEDRIC_M, "G", cx, cy - 11, C_EMBER);

    // title decodes letter by letter
    const char *title = "GRIMOIRE";
    int shown = t / 120;
    char buf[16] = {0};
    for (int i = 0; i < 8; ++i) buf[i] = i < shown ? title[i] : 'A' + (rand() % 26);
    glyphsCentre(DAEDRIC_M, buf, 178, 26, shown >= 8 ? C_EMBER : C_RUST);
    textCentre("G R I M O I R E", 178, 54, C_DIM);
    cv.drawFastHLine(122, 66, 112, C_CLOT);
    diamond(178, 66, 2, C_RUST);

    int li = (t / 900) % 4;
    int chars = (t % 900) / 40;
    char line[32];
    snprintf(line, sizeof line, "%.*s%s", chars, LINES[li], (t / 300) & 1 ? "_" : " ");
    text(line, 124, 76, C_BONE);
    textCentre("sound & sight", 178, 98, C_DIM);
    int bar = t * 104 / 2600;
    cv.drawRect(126, 112, 106, 5, C_CLOT);
    cv.fillRect(127, 113, bar > 104 ? 104 : bar, 3, C_BLOOD);
}

/* ------------------------------------------------------------ menu */
void card(const Rite &r, int x, int y, int w, int h, bool centre) {
    Canvas &cv = hal::canvas();
    cv.fillRect(x, y, w, h, centre ? C_DUSK : C_ASH);
    frame(x, y, w, h, centre ? C_RUST : C_CLOT, centre ? C_EMBER : C_UMBER);
    if (centre) {
        textCentre("- rite -", x + w / 2, y + 5, C_DIM);
        const DaedricFont &f = glyphWidth(DAEDRIC_L, r.glyph) <= w - 10 ? DAEDRIC_L : DAEDRIC_M;
        glyphsCentre(f, r.glyph, x + w / 2 + 1, y + 18 + (f.h == DAEDRIC_L.h ? 0 : 5), C_CLOT);
        glyphsCentre(f, r.glyph, x + w / 2, y + 17 + (f.h == DAEDRIC_L.h ? 0 : 5), C_EMBER);
        int lw = strlen(r.name) * 12;
        textCentre(r.name, x + w / 2, y + h - 22, C_GLOW, lw <= w - 6 ? 2 : 1);
    } else {
        glyphsCentre(DAEDRIC_S, r.glyph, x + w / 2, y + 16, C_BLOOD);
        textCentre(r.name, x + w / 2, y + h - 14, C_DIM);
    }
}

void drawMenu(uint32_t now) {
    Canvas &cv = hal::canvas();
    cv.fillScreen(C_VOID);
    motesDraw();
    header("GRIMOIRE", "grimoire");

    slide += (sel - slide) * 0.35f;
    if (fabsf(sel - slide) < 0.01f) slide = sel;
    // cards at spacing 82px around the centre; wraps
    for (int d = -2; d <= 2; ++d) {
        int i = ((int)roundf(slide) + d + N_RITES * 4) % N_RITES;
        float off = (roundf(slide) + d) - slide;
        bool centre = d == 0 && fabsf(off) < 0.5f;
        int cw = centre ? 104 : 60, ch = centre ? 86 : 62;
        int x = (int)(hal::W / 2 + off * 86) - cw / 2;
        int y = centre ? 23 : 35;
        if (x > hal::W || x + cw < 0) continue;
        if (!centre) card(*RITES[i], x, y, cw, ch, false);
    }
    {
        int i = ((int)roundf(slide) % N_RITES + N_RITES) % N_RITES;
        float off = roundf(slide) - slide;
        card(*RITES[i], (int)(hal::W / 2 + off * 86) - 52, 23, 104, 86, true);
    }
    // arrows
    uint16_t ac = ((now / 400) & 1) ? C_RUST : C_BLOOD;
    cv.fillTriangle(4, 66, 10, 60, 10, 72, ac);
    cv.fillTriangle(235, 66, 229, 60, 229, 72, ac);

    textCentre(RITES[sel]->desc, hal::W / 2, 112, C_BONE);
    for (int i = 0; i < N_RITES; ++i) diamond(hal::W / 2 - (N_RITES - 1) * 5 + i * 10, 127, 2, i == sel ? C_EMBER : C_CLOT, i == sel);

    if (now - volShownAt < 1200) {
        char b[24];
        snprintf(b, sizeof b, "vol %d", hal::volume() * 100 / 255);
        cv.fillRect(84, 52, 72, 26, C_DUSK);
        frame(84, 52, 72, 26, C_RUST, C_EMBER);
        textCentre(b, 120, 56, C_GLOW);
        cv.drawRect(92, 67, 56, 5, C_CLOT);
        cv.fillRect(93, 68, hal::volume() * 54 / 255, 3, C_EMBER);
    }
    if (escArmedAt && now - escArmedAt < 1500) {
        cv.fillRect(40, 50, 160, 30, C_DUSK);
        frame(40, 50, 160, 30, C_RUST, C_EMBER);
        textCentre("again: to the shrine", 120, 56, C_GLOW);
        textCentre("(back to the launcher)", 120, 67, C_DIM);
    } else escArmedAt = 0;
}

void open(int i) {
    uisfx::trigger(uisfx::OPEN);
    active = RITES[i];
    mode = RITE;
    if (active->enter) active->enter();
}
void close() {
    if (active && active->leave) active->leave();
    hal::setAudio(nullptr);
    uisfx::trigger(uisfx::BACK);
    active = nullptr;
    mode = MENU;
}

bool volumeKey(const hal::Key &k) {
    if (k.ch == '-' || k.ch == '_' || k.ch == '=' || k.ch == '+') {
        int v = hal::volume() + ((k.ch == '-' || k.ch == '_') ? -16 : 16);
        hal::setVolume(v < 0 ? 0 : v > 255 ? 255 : v);
        volShownAt = hal::ms();
        return true;
    }
    return false;
}
} // namespace

void grimSetup() {
    hal::begin();
    hal::setOverlayAudio(uisfx::render); // UI blips layer over everything
    srand(hal::ms() ^ 0x5A17);
    motesInit();
    t0 = hal::ms();
}

void grimLoop() {
    uint32_t start = hal::ms();
    hal::Key k;
    while (hal::pollKey(k)) {
        if (mode == SPLASH) {
            mode = MENU;
            uisfx::trigger(uisfx::SELECT);
            continue;
        }
        if (mode == MENU) {
            if (volumeKey(k)) continue;
            if (k.nav == hal::N_ESC) {
                if (escArmedAt) hal::backToLauncher();
                escArmedAt = hal::ms();
                uisfx::trigger(uisfx::BACK);
                continue;
            }
            escArmedAt = 0;
            if (k.nav == hal::N_LEFT || k.nav == hal::N_UP) { sel = (sel + N_RITES - 1) % N_RITES; uisfx::trigger(uisfx::MOVE); }
            else if (k.nav == hal::N_RIGHT || k.nav == hal::N_DOWN) { sel = (sel + 1) % N_RITES; uisfx::trigger(uisfx::MOVE); }
            else if (k.nav == hal::N_ENTER) open(sel);
            else if (k.ch >= '1' && k.ch < '1' + N_RITES) open(sel = k.ch - '1');
            // keep the carousel from spinning the long way round on wrap
            if (sel - slide > N_RITES / 2.f) slide += N_RITES;
            if (slide - sel > N_RITES / 2.f) slide -= N_RITES;
            continue;
        }
        // in a rite: Esc always leaves; Fn+-/= is volume everywhere
        if (k.nav == hal::N_ESC && !k.fn) {
            close();
            continue;
        }
        if (k.fn && volumeKey(k)) continue;
        bool wasNav = k.nav == hal::N_UP || k.nav == hal::N_DOWN || k.nav == hal::N_LEFT ||
                      k.nav == hal::N_RIGHT;
        if (!active->key(k)) close();
        else if (wasNav) uisfx::trigger(uisfx::MOVE); // click on list/grid moves in any rite
    }

    uint32_t now = hal::ms();
    if (mode == SPLASH) {
        drawSplash(now);
        if (now - t0 > 2600) mode = MENU;
    } else if (mode == MENU) {
        drawMenu(now);
    } else {
        active->frame(now);
        if (now - volShownAt < 900) {
            Canvas &cv = hal::canvas();
            cv.fillRect(180, 2, 58, 13, C_DUSK);
            cv.drawRect(180, 2, 58, 13, C_RUST);
            cv.fillRect(182, 7, hal::volume() * 54 / 255, 3, C_EMBER);
        }
    }
    hal::present();
    uint32_t spent = hal::ms() - start;
    hal::idle(spent < FRAME_MS ? FRAME_MS - spent : 1);
}
