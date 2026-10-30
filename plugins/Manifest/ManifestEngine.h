// Manifest — random-but-on-grid glitch events (the "Glitch Manifesting Device" idea):
// every grid step rolls dice; a hit fires a short tempo-locked event, then back to dry.
#pragma once
#include "../../shared/curio_dsp.h"

namespace manifest {
using namespace curio;

enum Ev { Stutter = 0, Gate, Reverse, Crush, Throw, PitchDrop, TapeStop, Silence, kNumEvents };
inline const char* eventName(int e) {
    static const char* n[kNumEvents] = { "Stutter", "Gate", "Reverse", "Crush", "Throw", "Pitch Drop", "Tape Stop", "Silence" };
    return n[e];
}

struct Params {
    float density = 0.3f;     // chance per step
    int grid = 5;             // curio::divisions index (default 1/16)
    int maxLen = 4;           // steps
    float weight[kNumEvents] = { 1, 0.6f, 0.5f, 0.5f, 0.4f, 0.3f, 0.3f, 0.2f };
    int seed = 1;
    bool lock = false;
    int patternBars = 1;      // 1, 2, 4
    float variation = 0.0f;   // chance a locked step re-rolls each loop
    float tail = 0.5f;        // reverb tail level
    float mix = 1.0f;
    float bpm = 120.0f; double ppq = 0; bool playing = false;
};

class Engine {
public:
    static constexpr float kHistSec = 6.0f;
    void prepare(float sampleRate) {
        sr = sampleRate;
        histLen = nextPow2((int)(sr * kHistSec)); histMask = histLen - 1;
        for (auto& h : hist) h.assign((size_t)histLen, 0.0f);
        w = 0; verb.prepare(sr); active = -1; lastStep = -1; freeBeat = 0; xf = 0; crushHold[0] = crushHold[1] = 0; crushCount = 0;
    }
    int currentEvent() const { return active; }

    void process(float* const* ch, int numCh, int n, const Params& p) {
        const double stepBeats = divisions()[(size_t)clampf((float)p.grid, 0, (float)divisions().size() - 1)].beats;
        const double bps = p.bpm / 60.0 / sr;
        const float stepSamples = (float)(stepBeats / bps);
        const float xfInc = 1.0f / (0.002f * sr);
        for (int i = 0; i < n; ++i) {
            double beat = p.playing ? p.ppq + i * bps : (freeBeat += bps);
            long step = (long)std::floor(beat / stepBeats + 1e-9);
            if (step != lastStep) { lastStep = step; onStep(step, p, stepBeats, stepSamples); }

            float inL = ch[0][i], inR = numCh > 1 ? ch[1][i] : inL;
            hist[0][(size_t)w] = inL; hist[1][(size_t)w] = inR;

            float eL = inL, eR = inR, sendL = 0, sendR = 0;
            if (active >= 0) {
                const float t = (float)evT;
                switch (active) {
                    case Stutter: { int s = std::max(1, sliceLen); int idx = evStart + ((int)evT % s); eL = h(0, idx); eR = h(1, idx); break; }
                    case Gate: { float ph = std::fmod(t / std::max(1.0f, gateLen), 1.0f); float g = ph < 0.5f ? 1.0f : 0.0f; eL *= g; eR *= g; break; }
                    case Reverse: { int idx = evStart - 1 - (int)evT; eL = h(0, idx); eR = h(1, idx); break; }
                    case Crush: {
                        if (crushCount++ % crushFactor == 0) { crushHold[0] = inL; crushHold[1] = inR; }
                        const float q = 12.0f; eL = std::round(crushHold[0] * q) / q; eR = std::round(crushHold[1] * q) / q; break; }
                    case Throw: sendL = inL; sendR = inR; break;
                    case PitchDrop: { readPos += 0.5; eL = hInterp(0, evStart + readPos); eR = hInterp(1, evStart + readPos); break; }
                    case TapeStop: { float r = std::max(0.0f, 1.0f - t / std::max(1.0f, evDur)); readPos += r * r; eL = hInterp(0, evStart + readPos); eR = hInterp(1, evStart + readPos); break; }
                    case Silence: eL = eR = 0; break;
                }
                ++evT;
                if (evT >= evDur) active = -1;
            }
            // 2 ms crossfade in/out of events
            float target = active >= 0 ? 1.0f : 0.0f;
            xf += (target > xf ? xfInc : -xfInc); xf = clampf(xf, 0.0f, 1.0f);
            if (active < 0 && xf > 0) { eL = lastEL; eR = lastER; }  // hold last event sample during fade-out
            else { lastEL = eL; lastER = eR; }
            float oL = inL + (eL - inL) * xf, oR = inR + (eR - inR) * xf;

            float vL, vR; verb.process(sendL * 4.0f, sendR * 4.0f, 0.85f, 0.3f, vL, vR);
            oL += vL * p.tail; oR += vR * p.tail;
            ch[0][i] = inL + (oL - inL) * p.mix;
            if (numCh > 1) ch[1][i] = inR + (oR - inR) * p.mix;
            w = (w + 1) & histMask;
        }
    }
private:
    float h(int c, int idx) const { return hist[c][(size_t)(idx & histMask)]; }
    float hInterp(int c, double pos) const { int i0 = (int)std::floor(pos); float f = (float)(pos - i0); return h(c, i0) + (h(c, i0 + 1) - h(c, i0)) * f; }

    void onStep(long step, const Params& p, double stepBeats, float stepSamples) {
        if (active >= 0) return;   // let the running event finish
        long key = step;
        if (p.lock) {
            long patSteps = std::max(1L, (long)std::lround(p.patternBars * 4.0 / stepBeats));
            long loop = (long)std::floor((double)step / (double)patSteps);
            key = ((step % patSteps) + patSteps) % patSteps;
            if (p.variation > 0 && hashUni((uint32_t)p.seed, (uint32_t)key, (uint32_t)loop + 777u) < p.variation) key += 100000 + loop * 1000;
        }
        const uint32_t s = (uint32_t)p.seed, k = (uint32_t)(key & 0x7fffffff);
        if (hashUni(s, k, 1) >= p.density) return;
        float total = 0; for (float wv : p.weight) total += std::max(0.0f, wv);
        if (total <= 0) return;
        float r = hashUni(s, k, 2) * total; int ev = 0;
        for (; ev < kNumEvents - 1; ++ev) { r -= std::max(0.0f, p.weight[ev]); if (r < 0) break; }
        int len = 1 + (int)(hashUni(s, k, 3) * (float)std::max(1, p.maxLen)); len = std::min(len, std::max(1, p.maxLen));
        active = ev; evT = 0; evDur = stepSamples * (float)len; evStart = w; readPos = 0;
        sliceLen = (int)(stepSamples / (float)(1 << (int)(hashUni(s, k, 4) * 3.0f)));   // 1, 1/2 or 1/4 step
        gateLen = stepSamples / (hashUni(s, k, 5) < 0.5f ? 2.0f : 4.0f);
        crushFactor = 2 + (int)(hashUni(s, k, 6) * 10.0f); crushCount = 0;
    }

    float sr = 48000;
    std::vector<float> hist[2]; int histLen = 0, histMask = 0, w = 0;
    int active = -1, evStart = 0, sliceLen = 1, crushFactor = 4, crushCount = 0;
    float evT = 0, evDur = 0, gateLen = 1, xf = 0, lastEL = 0, lastER = 0, crushHold[2] = {};
    double readPos = 0, freeBeat = 0; long lastStep = -1;
    MiniVerb verb;
};
} // namespace manifest
