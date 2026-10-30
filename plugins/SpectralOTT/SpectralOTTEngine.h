// Spectral OTT — OTT-style upward + downward compression applied per FFT bin
// ("thousands of OTTs, one per frequency"), with cross-bin gain smoothing.
#pragma once
#include "../../shared/curio_dsp.h"
#include <atomic>

namespace spectralott {
using namespace curio;

struct Params {
    float depth = 0.5f;        // 0..1 overall amount
    float balance = 0.0f;      // -1 all downward .. +1 all upward
    float time = 60.0f;        // ms attack/release
    float smoothing = 3.0f;    // bins (half-width)
    float tilt = 0.0f;         // dB/octave-ish: + gives highs more depth
    float range = 18.0f;       // dB max upward gain
    float floorDb = -70.0f;    // below this, no upward gain (keeps hiss down)
    float inGain = 0.0f, outGain = 0.0f; // dB
    float mix = 1.0f;
};

class Engine {
public:
    static constexpr int N = 2048, H = 512, kBins = N / 2 + 1, kDisplay = 96;
    std::atomic<float> display[kDisplay];   // gain dB per display band, for the UI

    void prepare(float sampleRate) {
        sr = sampleRate; fft.prepare(N);
        win.resize(N); for (int i = 0; i < N; ++i) win[(size_t)i] = std::sqrt(0.5f - 0.5f * std::cos(kTwoPi * (float)i / (float)N));
        for (int c = 0; c < 2; ++c) { inFifo[c].assign(N, 0.0f); outAcc[c].assign(2 * N, 0.0f); env[c].assign(kBins, -120.0f); }
        buf.assign(N, {}); gainDb.assign(kBins, 0.0f); smoothDb.assign(kBins, 0.0f);
        pos = 0; hopCount = 0;
        for (auto& d : display) d.store(0.0f);
    }
    int latencySamples() const { return N; }

    void process(float* const* ch, int numCh, int n, const Params& p) {
        const float inG = dbToGain(p.inGain), outG = dbToGain(p.outGain);
        for (int i = 0; i < n; ++i) {
            for (int c = 0; c < 2; ++c) {
                float x = (c < numCh ? ch[c][i] : ch[0][i]) * inG;
                inFifo[c][(size_t)pos] = x;
                dryDelay[c][(size_t)pos] = x;
            }
            float oL = outAcc[0][(size_t)pos], oR = outAcc[1][(size_t)pos];
            outAcc[0][(size_t)pos] = 0; outAcc[1][(size_t)pos] = 0;
            // dry aligned by N samples: the sample written N ago is at the same ring index
            float dL = dryOld[0][(size_t)pos], dR = dryOld[1][(size_t)pos];
            dryOld[0][(size_t)pos] = dryDelay[0][(size_t)pos]; dryOld[1][(size_t)pos] = dryDelay[1][(size_t)pos];
            ch[0][i] = safety((dL + (oL - dL) * p.mix) * outG);
            if (numCh > 1) ch[1][i] = safety((dR + (oR - dR) * p.mix) * outG);
            pos = (pos + 1) % N;
            if (++hopCount >= H) { hopCount = 0; frame(p); }
        }
    }

    // Overlap-add output lives in a ring of length N aligned with the input ring.
    // (outAcc is sized N; frame() adds into it starting at the current position.)
private:
    static float safety(float x) { float a = std::abs(x); return a < 1.5f ? x : std::copysign(1.5f + 0.5f * std::tanh((a - 1.5f) * 2.0f), x); }
    void frame(const Params& p) {
        const float frameSec = (float)H / sr;
        // fast attack (catches transients before they get boosted), set release
        const float relCoeff = std::exp(-frameSec / (0.001f * std::max(p.time, 1.0f)));
        const float attCoeff = std::exp(-frameSec / (0.001f * std::max(p.time * 0.1f, 0.5f)));
        const float down = p.depth * clampf(1.0f - p.balance, 0.0f, 1.0f);
        const float up = p.depth * clampf(1.0f + p.balance, 0.0f, 1.0f);
        const float norm = 4.0f / (float)N;    // full-scale sine ~ 0 dB
        // shared gain from summed L+R level so the stereo image holds
        for (int c = 0; c < 2; ++c) {
            for (int i = 0; i < N; ++i) buf[(size_t)i] = { inFifo[c][(size_t)((pos + i) % N)] * win[(size_t)i], 0.0f };
            fft.run(buf.data(), false);
            for (int b = 0; b < kBins; ++b) {
                float db = gainToDb(std::abs(buf[(size_t)b]) * norm);
                float& e = env[c][(size_t)b];
                const float k = db > e ? attCoeff : relCoeff;
                e = db + (e - db) * k;
            }
            spec[c].assign(buf.begin(), buf.begin() + kBins);
        }
        for (int b = 0; b < kBins; ++b) {
            float lvl = std::max(env[0][(size_t)b], env[1][(size_t)b]);
            float octave = std::log2(std::max(20.0f, (float)b * sr / (float)N) / 1000.0f);
            float tiltMul = clampf(1.0f + p.tilt * 0.1f * octave, 0.0f, 2.0f);
            const float hiThr = -24.0f, loThr = -40.0f;
            float g = 0;
            if (lvl > hiThr) g = -(lvl - hiThr) * (1.0f - 1.0f / 4.0f) * down * tiltMul;            // 4:1 downward
            else if (lvl < loThr && lvl > p.floorDb) g = std::min(p.range, (loThr - lvl) * 0.6f) * up * tiltMul; // upward
            gainDb[(size_t)b] = g;
        }
        // frequency smoothing (box filter on dB)
        const int r = (int)std::lround(p.smoothing);
        for (int b = 0; b < kBins; ++b) {
            float s = 0; int cnt = 0;
            for (int k = std::max(0, b - r); k <= std::min(kBins - 1, b + r); ++k) { s += gainDb[(size_t)k]; ++cnt; }
            smoothDb[(size_t)b] = s / (float)cnt;
        }
        for (int d = 0; d < kDisplay; ++d) {
            float f = 20.0f * std::pow(1000.0f, (float)d / (kDisplay - 1));
            int b = std::min(kBins - 1, (int)(f * (float)N / sr));
            display[d].store(smoothDb[(size_t)b]);
        }
        for (int c = 0; c < 2; ++c) {
            for (int b = 0; b < kBins; ++b) buf[(size_t)b] = spec[c][(size_t)b] * dbToGain(smoothDb[(size_t)b]);
            for (int b = kBins; b < N; ++b) buf[(size_t)b] = std::conj(buf[(size_t)(N - b)]);
            fft.run(buf.data(), true);
            // sqrt-Hann analysis * synthesis = Hann; Hann at 75% overlap sums to 2
            for (int i = 0; i < N; ++i) outAcc[c][(size_t)((pos + i) % N)] += buf[(size_t)i].real() * win[(size_t)i] * 0.5f;
        }
    }

    float sr = 48000; int pos = 0, hopCount = 0;
    FFT fft; std::vector<float> win;
    std::vector<std::complex<float>> buf, spec[2];
    std::vector<float> inFifo[2], outAcc[2], env[2], gainDb, smoothDb;
    std::vector<float> dryDelay[2] = { std::vector<float>(N, 0.0f), std::vector<float>(N, 0.0f) };
    std::vector<float> dryOld[2] = { std::vector<float>(N, 0.0f), std::vector<float>(N, 0.0f) };
};
} // namespace spectralott
