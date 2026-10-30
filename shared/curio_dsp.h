// Curio shared DSP — plain C++17, no JUCE, so engines can be unit-rendered offline.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <complex>
#include <vector>

namespace curio {

constexpr float kPi = 3.14159265358979f;
constexpr float kTwoPi = 6.28318530717959f;

inline float dbToGain(float db) { return std::pow(10.0f, db * 0.05f); }
inline float gainToDb(float g) { return 20.0f * std::log10(std::max(g, 1.0e-9f)); }
inline float mtof(float note) { return 440.0f * std::pow(2.0f, (note - 69.0f) / 12.0f); }
inline float semis(float s) { return std::pow(2.0f, s / 12.0f); }
inline float clampf(float v, float lo, float hi) { return std::min(hi, std::max(lo, v)); }
inline float softClip(float x) { return std::tanh(x); }
inline int nextPow2(int n) { int p = 1; while (p < n) p <<= 1; return p; }

// ---- Random -------------------------------------------------------------
struct Rng {
    uint32_t s = 0x9E3779B9u;
    void seed(uint32_t v) { s = v ? v : 0x9E3779B9u; }
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni() { return (next() >> 8) * (1.0f / 16777216.0f); }   // 0..1
    float bi() { return uni() * 2.0f - 1.0f; }                       // -1..1
};

// Stateless hash -> 0..1, used for deterministic per-step decisions.
inline uint32_t hash32(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x;
}
inline float hashUni(uint32_t a, uint32_t b, uint32_t c = 0) {
    return (hash32(a * 0x9E3779B1u ^ hash32(b + 0x632BE5ABu) ^ hash32(c * 0x85EBCA77u + 1u)) >> 8) * (1.0f / 16777216.0f);
}

// ---- Parameter smoothing -----------------------------------------------
struct Smooth {
    float v = 0, coeff = 0.999f;
    void setTime(float ms, float sr) { coeff = ms <= 0 ? 0.0f : std::exp(-1.0f / (0.001f * ms * sr)); }
    void reset(float x) { v = x; }
    float process(float target) { v = target + (v - target) * coeff; return v; }
};

// ---- Sample-and-hold random modulator with slew ------------------------
// Output -1..1. Steps at `rateHz`, glides with `slewMs`.
struct SampleHold {
    Rng rng; float phase = 0, target = 0, out = 0, slewCoeff = 0, sr = 48000;
    void prepare(float sampleRate, uint32_t seed) { sr = sampleRate; rng.seed(seed); target = rng.bi(); out = target; phase = 0; }
    void setSlew(float ms) { slewCoeff = ms <= 0.01f ? 0.0f : std::exp(-1.0f / (0.001f * ms * sr)); }
    float process(float rateHz) {
        phase += rateHz / sr;
        if (phase >= 1.0f) { phase -= std::floor(phase); target = rng.bi(); }
        out = target + (out - target) * slewCoeff;
        return out;
    }
};

// ---- Fractional delay line (cubic Hermite) ------------------------------
struct DelayLine {
    std::vector<float> buf; int mask = 0, w = 0;
    void prepare(int maxSamples) { int n = nextPow2(maxSamples + 4); buf.assign((size_t)n, 0.0f); mask = n - 1; w = 0; }
    void clear() { std::fill(buf.begin(), buf.end(), 0.0f); }
    void push(float x) { buf[(size_t)w] = x; w = (w + 1) & mask; }
    float at(int i) const { return buf[(size_t)(i & mask)]; }
    // delay in samples (>= 1) measured back from the last written sample
    float read(float d) const {
        d = std::max(1.0f, std::min(d, (float)mask - 3.0f));
        float pos = (float)w - d; int i = (int)std::floor(pos); float f = pos - (float)i;
        float y0 = at(i - 1), y1 = at(i), y2 = at(i + 1), y3 = at(i + 2);
        float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }
};

// ---- TPT state-variable filter (Zavalishin) ------------------------------
struct Svf {
    float ic1 = 0, ic2 = 0, g = 0, k = 1, a1 = 0, a2 = 0, a3 = 0;
    float lp = 0, bp = 0, hp = 0;
    void reset() { ic1 = ic2 = 0; }
    // res 0..1 (1 = near self-oscillation)
    void set(float cutoff, float res, float sr) {
        cutoff = clampf(cutoff, 10.0f, sr * 0.49f);
        g = std::tan(kPi * cutoff / sr);
        k = 2.0f - 1.98f * clampf(res, 0.0f, 1.0f);
        a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    void setQ(float cutoff, float q, float sr) {
        cutoff = clampf(cutoff, 10.0f, sr * 0.49f);
        g = std::tan(kPi * cutoff / sr); k = 1.0f / std::max(q, 0.05f);
        a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    void tick(float x) {
        float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2;
        lp = v2; bp = v1; hp = x - k * v1 - v2;
        if (!std::isfinite(ic1) || !std::isfinite(ic2)) { ic1 = ic2 = 0; lp = bp = hp = 0; }
    }
};

// 4th-order Linkwitz-Riley split (two cascaded Butterworth SVFs per side)
struct LR4 {
    Svf l1, l2, h1, h2;
    void set(float f, float sr) { for (Svf* s : { &l1, &l2, &h1, &h2 }) s->setQ(f, 0.7071f, sr); }
    void process(float x, float& lo, float& hi) {
        l1.tick(x); l2.tick(l1.lp); lo = l2.lp;
        h1.tick(x); h2.tick(h1.hp); hi = h2.hp;
    }
};

struct OnePole {
    float z = 0, a = 0;
    void setCutoff(float f, float sr) { a = std::exp(-kTwoPi * clampf(f, 1.0f, sr * 0.49f) / sr); }
    float lp(float x) { z = x + (z - x) * a; return z; }
};

// ---- Tempo math (the "Delay Time Tricks" formulas) -----------------------
// ms = 60000 / bpm * beats ; Hz = bpm / 60 / beats
inline float beatsToMs(float beats, float bpm) { return 60000.0f / std::max(bpm, 1.0f) * beats; }
inline float beatsToHz(float beats, float bpm) { return std::max(bpm, 1.0f) / 60.0f / std::max(beats, 1.0e-4f); }

// Note divisions shared by several plugins (in quarter-note beats)
struct Division { const char* name; float beats; };
inline const std::vector<Division>& divisions() {
    static const std::vector<Division> d = {
        { "1/64", 1.0f / 16 }, { "1/32T", 1.0f / 12 }, { "1/32", 1.0f / 8 }, { "1/16T", 1.0f / 6 }, { "1/32D", 3.0f / 16 },
        { "1/16", 0.25f }, { "1/8T", 1.0f / 3 }, { "1/16D", 0.375f }, { "1/8", 0.5f }, { "1/4T", 2.0f / 3 },
        { "1/8D", 0.75f }, { "1/4", 1.0f }, { "1/2T", 4.0f / 3 }, { "1/4D", 1.5f }, { "1/2", 2.0f },
        { "1/2D", 3.0f }, { "1 bar", 4.0f }, { "2 bars", 8.0f }, { "4 bars", 16.0f } };
    return d;
}

// ---- Small Freeverb-style reverb (for throws and tails) ------------------
struct MiniVerb {
    struct Comb { std::vector<float> b; int i = 0; float s = 0;
        float tick(float x, float fb, float damp) { float y = b[(size_t)i]; s = y + (s - y) * damp; b[(size_t)i] = x + s * fb; if (++i >= (int)b.size()) i = 0; return y; } };
    struct AP { std::vector<float> b; int i = 0;
        float tick(float x) { float y = b[(size_t)i]; float o = -x + y; b[(size_t)i] = x + y * 0.5f; if (++i >= (int)b.size()) i = 0; return o; } };
    Comb c[2][4]; AP a[2][2];
    void prepare(float sr) {
        const int cl[4] = { 1116, 1188, 1277, 1356 }, al[2] = { 556, 441 };
        float k = sr / 44100.0f;
        for (int ch = 0; ch < 2; ++ch) {
            for (int j = 0; j < 4; ++j) { c[ch][j].b.assign((size_t)((cl[j] + ch * 23) * k), 0.0f); c[ch][j].i = 0; c[ch][j].s = 0; }
            for (int j = 0; j < 2; ++j) { a[ch][j].b.assign((size_t)((al[j] + ch * 23) * k), 0.0f); a[ch][j].i = 0; }
        }
    }
    void process(float inL, float inR, float size, float damp, float& outL, float& outR) {
        float fb = 0.7f + 0.28f * size, in[2] = { inL, inR }, o[2];
        for (int ch = 0; ch < 2; ++ch) {
            float s = 0; for (auto& cc : c[ch]) s += cc.tick(in[ch] * 0.015f, fb, damp);
            for (auto& aa : a[ch]) s = aa.tick(s);
            o[ch] = s;
        }
        outL = o[0]; outR = o[1];
    }
};

// ---- Radix-2 complex FFT (in-place) --------------------------------------
struct FFT {
    int n = 0; std::vector<std::complex<float>> tw; std::vector<int> rev;
    void prepare(int size) {
        n = size; tw.resize((size_t)n / 2); rev.resize((size_t)n);
        for (int i = 0; i < n / 2; ++i) tw[(size_t)i] = std::polar(1.0f, -kTwoPi * (float)i / (float)n);
        int bits = 0; while ((1 << bits) < n) ++bits;
        for (int i = 0; i < n; ++i) { int r = 0; for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= 1 << (bits - 1 - b); rev[(size_t)i] = r; }
    }
    void run(std::complex<float>* x, bool inverse) const {
        for (int i = 0; i < n; ++i) if (i < rev[(size_t)i]) std::swap(x[i], x[rev[(size_t)i]]);
        for (int len = 2; len <= n; len <<= 1) {
            int step = n / len;
            for (int i = 0; i < n; i += len)
                for (int j = 0; j < len / 2; ++j) {
                    auto w = tw[(size_t)(j * step)]; if (inverse) w = std::conj(w);
                    auto u = x[i + j], v = x[i + j + len / 2] * w;
                    x[i + j] = u + v; x[i + j + len / 2] = u - v;
                }
        }
        if (inverse) { float s = 1.0f / (float)n; for (int i = 0; i < n; ++i) x[i] *= s; }
    }
};

// ---- Hilbert pair (two allpass chains, ~90° apart over audio band) -------
struct Hilbert {
    struct AP1 { float a = 0, x1 = 0, y1 = 0; float tick(float x) { float y = a * (x - y1) + x1; x1 = x; y1 = y; return y; } };
    AP1 re[4], im[4]; float imDelay = 0;
    void prepare() {
        const double c1[4] = { 0.6923878, 0.9360654322959, 0.9882295226860, 0.9987488452737 };
        const double c2[4] = { 0.4021921162426, 0.8561710882420, 0.9722909545651, 0.9952884791278 };
        for (int i = 0; i < 4; ++i) { re[i] = {}; im[i] = {}; re[i].a = (float)(c1[i] * c1[i]); im[i].a = (float)(c2[i] * c2[i]); }
    }
    void process(float x, float& i, float& q) {
        float a = x, b = x; for (auto& r : re) a = r.tick(a); for (auto& m : im) b = m.tick(b);
        i = a; q = imDelay; imDelay = b;
    }
};

} // namespace curio
