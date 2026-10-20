// Tiny fixed-rate DSP kit for the rites: table oscillators, noise, envelopes,
// a Chamberlin state-variable filter and a feedback delay. Everything runs
// per sample on the audio thread at hal::RATE, so keep it cheap.
#pragma once
#include "hal.h"
#include <math.h>
#include <stdint.h>

namespace dsp {

constexpr float PI2 = 6.2831853f;

// 1024-entry sine, built on first use.
inline const float *sineTable() {
    static float t[1025];
    static bool built = false;
    if (!built) {
        for (int i = 0; i <= 1024; ++i) t[i] = sinf(PI2 * i / 1024.f);
        built = true;
    }
    return t;
}

inline uint32_t hzToInc(float hz) { return (uint32_t)(hz * (4294967296.0 / hal::RATE)); }
inline float midiHz(float note) { return 440.f * powf(2.f, (note - 69.f) / 12.f); }

struct Osc {
    uint32_t phase = 0, inc = 0;
    void hz(float f) { inc = hzToInc(f); }
    inline float sine() {
        const float *t = sineTable();
        uint32_t i = phase >> 22;
        float frac = (phase & 0x3FFFFF) * (1.f / 4194304.f);
        float v = t[i] + (t[i + 1] - t[i]) * frac;
        phase += inc;
        return v;
    }
    inline float saw() {
        float v = (int32_t)phase * (1.f / 2147483648.f);
        phase += inc;
        return v;
    }
    inline float square(uint32_t pw = 0x80000000u) {
        float v = phase < pw ? 1.f : -1.f;
        phase += inc;
        return v;
    }
    inline float tri() {
        float s = (int32_t)phase * (1.f / 2147483648.f);
        phase += inc;
        return 2.f * fabsf(s) - 1.f;
    }
};

struct Noise {
    uint32_t s = 0x9E3779B9u;
    inline uint32_t next() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
    inline float white() { return (int32_t)next() * (1.f / 2147483648.f); }
};

// Exponential decay envelope; trigger() then tick() per sample.
struct Decay {
    float v = 0, k = 0.999f;
    void time(float sec) { k = expf(-1.f / (sec * hal::RATE)); }
    void trigger(float level = 1.f) { v = level; }
    inline float tick() { return v *= k; }
};

// Chamberlin SVF. f = 2 sin(pi fc / fs); stable for fc < fs/6.
struct SVF {
    float low = 0, band = 0, f = 0.1f, q = 1.f;
    void set(float hz, float res) { // res 0..1
        if (hz > hal::RATE / 6.5f) hz = hal::RATE / 6.5f;
        f = 2.f * sinf(3.14159265f * hz / hal::RATE);
        q = 2.f - 1.9f * res;
        // Chamberlin is only stable while f^2 + 2qf < 4
        float fmax = 0.95f * (sqrtf(q * q + 4.f) - q);
        if (f > fmax) f = fmax;
    }
    inline float lp(float in) {
        low += f * band;
        float high = in - low - q * band;
        band += f * high;
        return low;
    }
    inline float bp(float in) {
        lp(in);
        return band;
    }
};

template <int N> struct Delay {
    float buf[N] = {};
    int w = 0;
    inline float tap(int d) const { return buf[(w - d + N) % N]; }
    inline void put(float v) {
        buf[w] = v;
        w = (w + 1) % N;
    }
};

inline float softclip(float x) {
    if (x > 1.5f) return 1.f;
    if (x < -1.5f) return -1.f;
    return x - x * x * x * (4.f / 27.f);
}

inline int16_t toPcm(float x) {
    if (!(x == x)) x = 0; // NaN guard: never hand the codec garbage
    x = softclip(x);
    return (int16_t)(x * 30000.f);
}

// A scope ring the rites fill from the audio thread and draw from the UI.
struct Scope {
    static constexpr int N = 256;
    int16_t s[N];
    volatile int w = 0;
    inline void put(int16_t v) {
        s[w] = v;
        w = (w + 1) & (N - 1);
    }
};

} // namespace dsp
