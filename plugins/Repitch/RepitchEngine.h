// Repitch — tempo-math delay where time changes are heard as Doppler pitch swoops.
#pragma once
#include "../../shared/curio_dsp.h"

namespace repitch {
using namespace curio;

enum class TimeUnit { Sync = 0, Ms, Hz };
enum class Change { Repitch = 0, Fade, Jump };

struct Params {
    TimeUnit unit = TimeUnit::Sync;
    int division = 8;          // index into curio::divisions()
    float ms = 375.0f, hz = 4.0f;
    Change change = Change::Repitch;
    float glide = 300.0f;      // ms
    float feedback = 0.45f, tone = 6000.0f, drive = 0.0f;
    bool pingpong = true;
    float lfoRate = 0.25f;     // Hz
    float lfoDepth = 0.0f;     // 0..1 (fraction of time)
    float swoop = 0.0f;        // -1..1 manual Doppler push (time multiplier in octaves)
    float mix = 0.35f;
    float bpm = 120.0f;
};

inline float targetMs(const Params& p) {
    switch (p.unit) {
        case TimeUnit::Sync: return beatsToMs(divisions()[(size_t)clampf((float)p.division, 0, (float)divisions().size() - 1)].beats, p.bpm);
        case TimeUnit::Ms: return p.ms;
        case TimeUnit::Hz: return 1000.0f / std::max(p.hz, 0.25f);
    }
    return p.ms;
}

class Engine {
public:
    void prepare(float sampleRate) {
        sr = sampleRate;
        for (auto& d : dl) d.prepare((int)(sr * 17.0f));
        for (auto& f : lp) f.reset();
        curMs = prevMs = fadeTo = 0; fadePos = 1; lfoPhase = 0;
    }
    float currentMs() const { return curMs; }

    void process(float* const* ch, int numCh, int n, const Params& p) {
        const float tgt = clampf(targetMs(p) * std::pow(2.0f, p.swoop), 1.0f, 16000.0f);
        if (curMs <= 0) { curMs = tgt; fadeTo = tgt; prevMs = tgt; }
        const float glideCoeff = std::exp(-1.0f / (0.001f * std::max(p.glide, 1.0f) * sr));
        const float fadeInc = 1.0f / (0.001f * std::max(p.glide, 5.0f) * sr);
        for (auto& f : lp) f.set(p.tone, 0.1f, sr);
        const float drv = 1.0f + p.drive * 6.0f, fb = clampf(p.feedback, 0.0f, 1.05f);

        for (int i = 0; i < n; ++i) {
            lfoPhase += p.lfoRate / sr; if (lfoPhase >= 1) lfoPhase -= 1;
            const float lfo = 1.0f + p.lfoDepth * 0.5f * std::sin(kTwoPi * lfoPhase);
            const float want = tgt * lfo;
            float readA, readB = 0, wB = 0;
            switch (p.change) {
                case Change::Repitch: curMs = want + (curMs - want) * glideCoeff; readA = curMs; break;
                case Change::Jump: curMs = want; readA = curMs; break;
                case Change::Fade:
                default:
                    if (fadePos >= 1.0f && std::abs(want - fadeTo) > 0.5f) { prevMs = fadeTo; fadeTo = want; fadePos = 0; }
                    if (fadePos < 1.0f) { fadePos = std::min(1.0f, fadePos + fadeInc); }
                    curMs = fadeTo; readA = fadeTo; readB = prevMs; wB = 1.0f - fadePos; break;
            }
            float inL = ch[0][i], inR = numCh > 1 ? ch[1][i] : inL;
            float y[2];
            for (int c = 0; c < 2; ++c) {
                float a = dl[c].read(readA * 0.001f * sr);
                if (wB > 0) a = a * (1.0f - wB) + dl[c].read(readB * 0.001f * sr) * wB;
                y[c] = a;
            }
            float f0, f1;
            lp[0].tick(y[0]); lp[1].tick(y[1]);
            f0 = softClip(lp[0].lp * drv) / std::max(1.0f, drv * 0.7f);
            f1 = softClip(lp[1].lp * drv) / std::max(1.0f, drv * 0.7f);
            if (p.pingpong) {
                dl[0].push(0.5f * (inL + inR) + f1 * fb);
                dl[1].push(f0 * fb);
            } else {
                dl[0].push(inL + f0 * fb);
                dl[1].push(inR + f1 * fb);
            }
            ch[0][i] = inL + y[0] * p.mix;   // dry always passes, echoes added at Mix level
            if (numCh > 1) ch[1][i] = inR + y[1] * p.mix;
        }
    }
private:
    float sr = 48000, curMs = 0, prevMs = 0, fadeTo = 0, fadePos = 1, lfoPhase = 0;
    DelayLine dl[2];
    Svf lp[2];
};
} // namespace repitch
