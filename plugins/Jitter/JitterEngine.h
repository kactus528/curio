// Jitter — micro sample-and-hold instability (the "~1% at ~60 Hz" analog trick).
#pragma once
#include "../../shared/curio_dsp.h"

namespace jitter {
using namespace curio;

struct Params {
    float amount = 1.0f;     // % (0..5)
    float rate = 60.0f;      // Hz
    float smooth = 2.0f;     // ms slew on S&H steps
    float pitch = 1.0f, tone = 0.5f, pan = 0.3f; // depths 0..1
    bool unlink = true;      // independent L/R pitch
    float age = 0.0f;        // 0..1 scales all depths up and slows rate (macro)
    float mix = 1.0f;
};

class Engine {
public:
    static constexpr float kBaseMs = 12.0f;
    void prepare(float sampleRate) {
        sr = sampleRate;
        for (int c = 0; c < 2; ++c) { dl[c].prepare((int)(sr * 0.03f)); split[c].setCutoff(1800.0f, sr); }
        shP[0].prepare(sr, 11); shP[1].prepare(sr, 23); shT.prepare(sr, 37); shPan.prepare(sr, 41);
    }
    int latencySamples() const { return (int)std::lround(kBaseMs * 0.001f * sr); }

    void process(float* const* ch, int numCh, int n, const Params& p) {
        const float age = p.age;
        const float amt = p.amount * 0.01f * (1.0f + 2.0f * age);
        const float rate = std::max(0.5f, p.rate * (1.0f - 0.7f * age));
        for (auto* s : { &shP[0], &shP[1], &shT, &shPan }) s->setSlew(std::max(p.smooth, 0.05f));
        // Pitch deviation ~= delay swing * step rate, so swing = amount / rate (seconds)
        const float swing = std::min(amt * p.pitch / rate, 0.010f) * sr;
        const float base = kBaseMs * 0.001f * sr;
        for (int i = 0; i < n; ++i) {
            float mP0 = shP[0].process(rate), mP1 = p.unlink ? shP[1].process(rate) : mP0;
            float mT = shT.process(rate * 0.5f), mPan = shPan.process(rate * 0.25f);
            float toneGain = 1.0f + clampf(mT * p.tone * amt * 25.0f, -0.9f, 2.0f);
            float panAmt = clampf(mPan * p.pan * amt * 10.0f, -0.5f, 0.5f);
            for (int c = 0; c < std::min(numCh, 2); ++c) {
                float x = ch[c][i];
                dl[c].push(x);
                float y = dl[c].read(base + (c == 0 ? mP0 : mP1) * swing);
                float lo = split[c].lp(y), hi = y - lo;
                y = lo + hi * toneGain;
                y *= (c == 0 ? 1.0f - panAmt : 1.0f + panAmt);
                // dry is aligned to the same base latency
                float dry = dl[c].read(base);
                ch[c][i] = dry + (y - dry) * p.mix;
            }
        }
    }
private:
    float sr = 48000;
    DelayLine dl[2];
    OnePole split[2];
    SampleHold shP[2], shT, shPan;
};
} // namespace jitter
