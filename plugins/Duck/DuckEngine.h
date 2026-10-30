// Duck — MIDI-triggered sidechain envelopes (the "double the kick/snare MIDI into a
// ducking group" trick, in one device). Two lanes, each with its own curve.
#pragma once
#include "../../shared/curio_dsp.h"
#include "../../shared/curio_midi.h"

namespace duck {
using namespace curio;

struct Lane {
    int note = 36;            // trigger note (-1 = any note)
    float depth = 1.0f;       // 0..1 (1 = full silence at the bottom)
    float attack = 2.0f;      // ms to reach the bottom
    float hold = 10.0f;       // ms at the bottom
    float release = 180.0f;   // ms back to unity
    float tension = 0.5f;     // -1..1 release curve shape (neg = fast-then-slow, pos = slow-then-fast)
    float velSens = 0.5f;     // 0..1
};

enum class Band { Full = 0, LowOnly, HighOnly };
enum class Combine { Deepest = 0, Multiply };

struct Params {
    Lane lane[2];
    Combine combine = Combine::Deepest;
    Band band = Band::Full;
    float crossover = 150.0f;
    float lookahead = 3.0f;   // ms
    float smoothing = 0.5f;   // ms
    float mix = 1.0f;
};

// Gain of a lane curve at time t (ms) after a trigger, 1 = unity. Shared with the UI.
inline float curveGain(const Lane& l, float tMs, float vel) {
    const float depth = l.depth * (1.0f - l.velSens + l.velSens * vel);
    const float floorG = 1.0f - clampf(depth, 0.0f, 1.0f);
    if (tMs < 0) return 1.0f;
    if (tMs < l.attack) { float a = tMs / std::max(l.attack, 0.01f); return 1.0f + (floorG - 1.0f) * a; }
    tMs -= l.attack;
    if (tMs < l.hold) return floorG;
    tMs -= l.hold;
    if (tMs >= l.release) return 1.0f;
    float r = tMs / std::max(l.release, 0.01f);
    float e = l.tension >= 0 ? 1.0f + l.tension * 3.0f : 1.0f / (1.0f - l.tension * 3.0f);
    float shaped = std::pow(r, e);
    return floorG + (1.0f - floorG) * shaped;
}

class Engine {
public:
    void prepare(float sampleRate) {
        sr = sampleRate;
        for (auto& d : delay) d.prepare((int)(sr * 0.02f));
        for (auto& v : st) v = {};
        for (auto& s : smooth) s.reset(1.0f);
    }
    int latencySamples(const Params& p) const { return (int)std::lround(p.lookahead * 0.001f * sr); }
    float meter() const { return lastGain; }

    void process(float* const* ch, int numCh, int n, const Params& p, const NoteEvents& ev) {
        for (auto& x : xo) x.set(p.crossover, sr);
        for (auto& s : smooth) s.setTime(p.smoothing, sr);
        const float la = (float)latencySamples(p);
        size_t e = 0;
        for (int i = 0; i < n; ++i) {
            while (e < ev.size() && ev[e].offset <= i) {
                const auto& m = ev[e++];
                if (!m.on) continue;
                for (int l = 0; l < 2; ++l)
                    if (p.lane[l].note < 0 || p.lane[l].note == m.note) {
                        // restart from where the curve is now so retriggers never click
                        st[l].active = true; st[l].t = 0; st[l].vel = m.vel;
                    }
            }
            float g[2];
            for (int l = 0; l < 2; ++l) {
                if (!st[l].active) { g[l] = 1.0f; continue; }
                g[l] = curveGain(p.lane[l], st[l].t, st[l].vel);
                st[l].t += 1000.0f / sr;
                const auto& L = p.lane[l];
                if (st[l].t > L.attack + L.hold + L.release) st[l].active = false;
            }
            float gain = p.combine == Combine::Deepest ? std::min(g[0], g[1]) : g[0] * g[1];
            gain = smooth[0].process(gain);
            gain = 1.0f + (gain - 1.0f) * p.mix;
            lastGain = gain;
            for (int c = 0; c < std::min(numCh, 2); ++c) {
                float x = ch[c][i];
                delay[c].push(x);
                float y = la >= 1.0f ? delay[c].read(la) : x;
                if (p.band == Band::Full) y *= gain;
                else {
                    float lo, hi; xo[c].process(y, lo, hi);
                    y = p.band == Band::LowOnly ? lo * gain + hi : lo + hi * gain;
                }
                ch[c][i] = y;
            }
        }
    }
private:
    struct State { bool active = false; float t = 0, vel = 1; };
    float sr = 48000, lastGain = 1;
    State st[2];
    DelayLine delay[2];
    LR4 xo[2];
    Smooth smooth[1];
};
} // namespace duck
