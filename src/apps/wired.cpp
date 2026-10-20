// WIRED — ASCII 3D. A character-cell renderer (40x14 cells of the 6x8 font)
// with a z-buffer and luminance ramp: torus, a navi sphere wrapped in text,
// wireframe solids, and a tunnel. A quiet data-hum rides along.
//
// keys  1-4 / tab  form    ;. tilt   ,/ spin speed   space pause
//       m mono/ember   s sound on/off
#include "../dsp.h"
#include "../rite.h"
#include "../theme.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

using namespace theme;

namespace {
constexpr int COLS = 40, ROWS = 12, OY = 21;
const char LUM[] = ".,-~:;=!*#$@";
constexpr int NLUM = sizeof(LUM) - 1;
const char *FORMS[] = {"torus", "navi", "solids", "tunnel"};
const char NAVI_TEXT[] = "LETALLTHECHILDRENOFTHEWIREDBECONNECTED";

struct State {
    int form = 0;
    float a = 0, b = 0, tilt = 0.6f, speed = 1.f;
    bool paused = false, mono = false, sound = true;
    char ch[ROWS][COLS];
    uint8_t lum[ROWS][COLS];
    float zb[ROWS][COLS];
    int solid = 0;
    uint32_t solidAt = 0;
    // sound
    dsp::Osc hum, hum2, blip;
    dsp::Decay blipEnv;
    dsp::Noise rng;
    dsp::SVF hissF;
    volatile float motion = 0;
} *S = nullptr;

void clear() {
    memset(S->ch, ' ', sizeof(S->ch));
    memset(S->lum, 0, sizeof(S->lum));
    for (auto &r : S->zb)
        for (float &z : r) z = 0;
}

inline void plot(int x, int y, float ooz, float L, char override = 0) {
    if (x < 0 || x >= COLS || y < 0 || y >= ROWS) return;
    if (ooz <= S->zb[y][x]) return;
    S->zb[y][x] = ooz;
    if (L < 0) L = 0;
    if (L > 1) L = 1;
    S->ch[y][x] = override ? override : LUM[(int)(L * (NLUM - 1))];
    S->lum[y][x] = (uint8_t)(70 + L * 185);
}

// the classic: a torus of R1=1, R2=2 seen from K2=5
void torus() {
    float cA = cosf(S->a), sA = sinf(S->a), cB = cosf(S->b), sB = sinf(S->b);
    const float K2 = 5, K1 = 17;
    for (float th = 0; th < 6.283f; th += 0.1f) {
        float ct = cosf(th), st = sinf(th);
        for (float ph = 0; ph < 6.283f; ph += 0.04f) {
            float cp = cosf(ph), sp = sinf(ph);
            float cx = 2 + ct, cy = st;
            float x = cx * (cB * cp + sA * sB * sp) - cy * cA * sB;
            float y = cx * (sB * cp - sA * cB * sp) + cy * cA * cB;
            float z = K2 + cA * cx * sp + cy * sA;
            float ooz = 1 / z;
            int xp = (int)(COLS / 2 + K1 * 1.33f * ooz * x);
            int yp = (int)(ROWS / 2 - K1 * 0.42f * ooz * y);
            float L = cp * ct * sB - cA * ct * sp - sA * st + cB * (cA * st - ct * sA * sp);
            if (L > 0) plot(xp, yp, ooz, L * 0.7f);
        }
    }
}

// a sphere with the Wired's motto wrapped around it, lit from the upper left
void navi() {
    const float R = 1.f, K2 = 3.2f, K1 = 15.f;
    float spin = S->a, tl = S->tilt * 0.5f;
    float ct = cosf(tl), st = sinf(tl);
    const int TL = sizeof(NAVI_TEXT) - 1;
    for (int la = 0; la < 34; ++la) {
        float lat = -1.5708f + la * 3.14159f / 33;
        for (int lo = 0; lo < 110; ++lo) {
            float lon = lo * 6.2831853f / 110 + spin;
            float x = R * cosf(lat) * cosf(lon), y = R * sinf(lat), z = R * cosf(lat) * sinf(lon);
            float y2 = y * ct - z * st, z2 = y * st + z * ct;
            float zz = K2 + z2;
            float ooz = 1 / zz;
            int xp = (int)(COLS / 2 + K1 * 1.33f * ooz * x);
            int yp = (int)(ROWS / 2 - K1 * 0.98f * ooz * y2);
            float L = (-x * 0.5f + y2 * 0.6f - z2 * 0.62f);
            if (z2 > 0.15f) continue; // back face
            char c = (lo % 6 == 0 || la % 6 == 0) ? NAVI_TEXT[(lo / 2 + la * 3) % TL] : 0;
            plot(xp, yp, ooz, 0.25f + L * 0.75f, c);
        }
    }
}

struct V3 {
    float x, y, z;
};
void project(V3 p, int &xp, int &yp, float &ooz) {
    float cA = cosf(S->a), sA = sinf(S->a), cB = cosf(S->tilt), sB = sinf(S->tilt);
    float x = p.x * cA + p.z * sA, z = -p.x * sA + p.z * cA;
    float y = p.y * cB - z * sB;
    z = p.y * sB + z * cB;
    ooz = 1.f / (z + 4.f);
    xp = (int)(COLS / 2 + 21.f * 1.33f * ooz * x);
    yp = (int)(ROWS / 2 - 21.f * 1.0f * ooz * y); // 6x8 cells: rows = cols * 0.75
}
void edge(V3 a, V3 b) {
    int x0, y0, x1, y1;
    float z0, z1;
    project(a, x0, y0, z0);
    project(b, x1, y1, z1);
    int n = abs(x1 - x0) > abs(y1 - y0) ? abs(x1 - x0) : abs(y1 - y0);
    if (n == 0) n = 1;
    for (int i = 0; i <= n; ++i) {
        float t = (float)i / n;
        float ooz = z0 + (z1 - z0) * t;
        float L = (ooz - 0.17f) * 5.f;
        int x = x0 + (int)roundf((x1 - x0) * t), y = y0 + (int)roundf((y1 - y0) * t);
        float dx = x1 - x0, dy = (y1 - y0) * 2.f;
        char c = fabsf(dx) > 2 * fabsf(dy) ? '-' : fabsf(dy) > 2 * fabsf(dx) ? '|' : (dx * dy > 0 ? '\\' : '/');
        plot(x, y, ooz, L, c);
    }
    int xp, yp;
    float o;
    project(a, xp, yp, o);
    plot(xp, yp, o + 0.001f, 1.f, '@');
}
void solids() {
    if (hal::ms() - S->solidAt > 7000) {
        S->solid = (S->solid + 1) % 3;
        S->solidAt = hal::ms();
    }
    if (S->solid == 0) { // heptagram prism: the {7/3} star at two depths
        V3 top[7], bot[7];
        for (int i = 0; i < 7; ++i) {
            float t = i * 6.2831853f / 7;
            top[i] = {cosf(t) * 1.4f, 0.55f, sinf(t) * 1.4f};
            bot[i] = {cosf(t) * 1.4f, -0.55f, sinf(t) * 1.4f};
        }
        for (int i = 0; i < 7; ++i) {
            edge(top[i], top[(i + 3) % 7]);
            edge(bot[i], bot[(i + 3) % 7]);
            edge(top[i], bot[i]);
        }
    } else if (S->solid == 1) { // icosahedron
        const float p = 1.618f * 0.75f, q = 0.75f;
        V3 v[12] = {{-q, p, 0}, {q, p, 0}, {-q, -p, 0}, {q, -p, 0}, {0, -q, p}, {0, q, p},
                    {0, -q, -p}, {0, q, -p}, {p, 0, -q}, {p, 0, q}, {-p, 0, -q}, {-p, 0, q}};
        for (int i = 0; i < 12; ++i)
            for (int j = i + 1; j < 12; ++j) {
                float dx = v[i].x - v[j].x, dy = v[i].y - v[j].y, dz = v[i].z - v[j].z;
                if (fabsf(dx * dx + dy * dy + dz * dz - 4 * q * q) < 0.05f) edge(v[i], v[j]);
            }
    } else { // tesseract shadow: two cubes, inner one breathing
        float s1 = 1.1f, s2 = 0.45f + 0.15f * sinf(S->b * 2);
        V3 o[8], in[8];
        for (int i = 0; i < 8; ++i) {
            float x = i & 1 ? 1 : -1, y = i & 2 ? 1 : -1, z = i & 4 ? 1 : -1;
            o[i] = {x * s1, y * s1, z * s1};
            in[i] = {x * s2, y * s2, z * s2};
        }
        for (int i = 0; i < 8; ++i)
            for (int bit = 1; bit < 8; bit <<= 1)
                if (!(i & bit)) {
                    edge(o[i], o[i | bit]);
                    edge(in[i], in[i | bit]);
                }
        for (int i = 0; i < 8; ++i) edge(o[i], in[i]);
    }
}

void tunnel() {
    float t = S->b * 2.f;
    for (int y = 0; y < ROWS; ++y)
        for (int x = 0; x < COLS; ++x) {
            float dx = (x - COLS / 2 + 0.5f) * 0.75f, dy = (y - ROWS / 2 + 0.5f) * 1.6f;
            dx += sinf(t * 0.7f) * 3.f;
            float d = sqrtf(dx * dx + dy * dy) + 0.001f;
            float ang = atan2f(dy, dx) + S->a * 0.5f;
            float depth = 18.f / d + t * 2.f;
            int ring = (int)depth, spoke = (int)((ang + 3.1416f) * 7 / 6.2832f * 2);
            float L = fminf(1.f, d / 14.f);
            bool on = ((ring + spoke) & 1);
            char c = on ? LUM[(int)(L * (NLUM - 1))] : (ring % 3 == 0 ? '.' : ' ');
            if (c == ' ') continue;
            S->ch[y][x] = c;
            S->lum[y][x] = (uint8_t)(40 + L * 210 * (on ? 1.f : 0.5f));
        }
}

void render(int16_t *out, int n) {
    State &s = *S;
    for (int i = 0; i < n; ++i) {
        float x = 0;
        if (s.sound) {
            x = s.hum.sine() * 0.08f + s.hum2.sine() * 0.04f;
            if (s.rng.next() % 2600 == 0) {
                s.blip.hz(400.f + (s.rng.next() % 12) * 180.f);
                s.blipEnv.trigger(0.25f + s.motion * 0.4f);
            }
            x += s.blip.square(0x60000000u) * s.blipEnv.tick() * 0.25f;
            x += s.hissF.bp(s.rng.white()) * 0.02f;
        }
        out[i] = dsp::toPcm(x);
    }
}

void enter() {
    if (!S) S = new State();
    S->hum.hz(55);
    S->hum2.hz(82.6f);
    S->blipEnv.time(0.04f);
    S->hissF.set(5200, 0.3f);
    S->solidAt = hal::ms();
    hal::setAudio(render);
}
void leave() {}

bool onKey(const hal::Key &k) {
    State &s = *S;
    if (k.ch >= '1' && k.ch <= '4') s.form = k.ch - '1';
    else if (k.nav == hal::N_TAB || k.nav == hal::N_ENTER) s.form = (s.form + 1) % 4;
    else if (k.nav == hal::N_UP) s.tilt += 0.15f;
    else if (k.nav == hal::N_DOWN) s.tilt -= 0.15f;
    else if (k.nav == hal::N_RIGHT) s.speed = fminf(4.f, s.speed + 0.25f);
    else if (k.nav == hal::N_LEFT) s.speed = fmaxf(-4.f, s.speed - 0.25f);
    else if (k.ch == ' ') s.paused = !s.paused;
    else if (k.ch == 'm') s.mono = !s.mono;
    else if (k.ch == 's') s.sound = !s.sound;
    return true;
}

void draw(uint32_t now) {
    State &s = *S;
    Canvas &cv = hal::canvas();
    cv.fillScreen(C_VOID);
    header("WIRED", "wired");
    textRight(FORMS[s.form], 178, 5, C_DIM);
    if (!s.paused) {
        s.a += 0.045f * s.speed;
        s.b += 0.021f * s.speed;
    }
    s.motion = fabsf(s.speed) / 4.f;
    clear();
    switch (s.form) {
    case 0: torus(); break;
    case 1: navi(); break;
    case 2: solids(); break;
    case 3: tunnel(); break;
    }
    cv.setFont(&fonts::Font0);
    cv.setTextSize(1);
    for (int y = 0; y < ROWS; ++y)
        for (int x = 0; x < COLS; ++x) {
            char c = s.ch[y][x];
            if (c == ' ') continue;
            uint16_t col = s.mono ? blend(C_BONE, C_VOID, s.lum[y][x]) : ramp(s.lum[y][x]);
            cv.setTextColor(col);
            cv.drawChar(c, x * 6, OY + y * 8);
        }
    // scanline flicker, CRT-ish
    int sl = (now / 16) % 120;
    cv.drawFastHLine(0, OY + sl, 240, C_DUSK);
    footer("1-4 form ;. tilt ,/ spin s snd m mono");
}
} // namespace

const Rite RITE_WIRED = {"WIRED", "wired", "ascii 3d: torus, navi, solids", enter, leave, onKey, draw};
