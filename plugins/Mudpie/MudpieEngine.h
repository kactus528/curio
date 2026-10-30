// Mudpie — jam through an FX chain while an always-on buffer records it, then grab the
// last N bars and play the slices from MIDI pads (the Infiltrator + Rolling Sampler workflow).
#pragma once
#include "../../shared/curio_dsp.h"
#include "../../shared/curio_midi.h"
#include <atomic>

namespace mudpie {
using namespace curio;

enum class SliceMode { Grid = 0, Transient };
constexpr int kPads = 16, kFirstNote = 36, kPadVoices = 8;

struct Params {
    float chaos = 0.2f, tone = 0.5f, space = 0.2f, crunch = 0.2f; // macros 0..1
    float motion = 0.0f;  int motionDiv = 11;                      // LFO depth on Tone, sync div
    float mix = 1.0f;
    int grabBars = 2;                 // 1,2,4,8
    SliceMode sliceMode = SliceMode::Grid;
    float sensitivity = 0.5f;
    float padPitch = 0.0f;            // semitones
    bool padReverse = false;
    float padFade = 3.0f;             // ms
    bool clicky = false;
    float padGain = 0.0f;             // dB
    float bpm = 120.0f; double ppq = 0; bool playing = false;
};

struct Capture {
    std::vector<float> l, r; int length = 0;
    int sliceStart[kPads + 1] = {};
};

class Engine {
public:
    static constexpr float kRingSec = 60.0f;
    std::atomic<bool> grabRequest { false };
    std::atomic<int> generation { 0 };          // bumps after each grab
    std::atomic<int> previewPad { -1 };         // UI pad click
    std::atomic<int> padFlash[kPads];

    void prepare(float sampleRate) {
        sr = sampleRate;
        ringLen = (int)(sr * kRingSec);
        ringL.assign((size_t)ringLen, 0.0f); ringR.assign((size_t)ringLen, 0.0f); rw = 0; filled = 0;
        int capMax = (int)(sr * 34.0f);   // 8 bars at 60 bpm = 32 s
        for (auto& c : cap) { c.l.assign((size_t)capMax, 0.0f); c.r.assign((size_t)capMax, 0.0f); c.length = 0; }
        front.store(0);
        for (auto& f : filt) f.reset();
        for (auto& d : dly) d.prepare((int)(sr * 2.1f));
        hil[0].prepare(); hil[1].prepare();
        shiftPhase = 0; lfoFree = 0; crushCount = 0;
        for (auto& v : voices) v.active = false;
        for (auto& f : padFlash) f.store(0);
    }

    // UI-thread access to the current capture (read-only; swapped atomically after a grab)
    const Capture& capture() const { return cap[front.load()]; }

    void process(float* const* ch, int numCh, int n, const Params& p, const NoteEvents& ev) {
        const float bps = p.bpm / 60.0f / sr;
        // --- macro mapping ---
        const float chaos = p.chaos, crunch = p.crunch, space = p.space;
        const float res = 0.15f + 0.6f * chaos;
        const int crushFactor = 1 + (int)(chaos * chaos * 15.0f);
        const float bits = 16.0f - chaos * 11.0f, q = std::pow(2.0f, bits - 1.0f);
        const float shiftHz = chaos * chaos * 350.0f;
        const float shiftMix = clampf(chaos * 1.5f - 0.3f, 0.0f, 1.0f);
        const float delMs = beatsToMs(space < 0.5f ? 0.25f : 0.75f, p.bpm);
        const float delFb = 0.15f + 0.7f * space, delMix = space * 0.8f;
        const float drive = 1.0f + crunch * crunch * 20.0f;
        const double motionBeats = divisions()[(size_t)clampf((float)p.motionDiv, 0, (float)divisions().size() - 1)].beats;

        if (grabRequest.exchange(false)) doGrab(p);
        int pv = previewPad.exchange(-1);
        if (pv >= 0) trigger(pv, 1.0f, p);

        size_t e = 0;
        for (int i = 0; i < n; ++i) {
            while (e < ev.size() && ev[e].offset <= i) {
                const auto& m = ev[e++];
                int pad = m.note - kFirstNote;
                if (m.on && pad >= 0 && pad < kPads) trigger(pad, m.vel, p);
            }
            // motion LFO on tone
            double beat = p.playing ? p.ppq + i * (double)bps : (lfoFree += bps);
            float lfo = std::sin(kTwoPi * (float)std::fmod(beat / motionBeats, 1.0));
            float tone = clampf(p.tone + lfo * p.motion * 0.5f, 0.0f, 1.0f);
            if ((i & 31) == 0) {
                float f = tone < 0.5f ? 150.0f * std::pow(130.0f, tone * 2.0f) : 30.0f * std::pow(200.0f, (tone - 0.5f) * 2.0f);
                for (auto& fl : filt) fl.set(f, res, sr);
            }
            float in[2] = { ch[0][i], numCh > 1 ? ch[1][i] : ch[0][i] }, y[2];
            shiftPhase += shiftHz / sr; if (shiftPhase >= 1) shiftPhase -= 1;
            const float cs = std::cos(kTwoPi * shiftPhase), sn = std::sin(kTwoPi * shiftPhase);
            bool hold = (crushCount++ % crushFactor) == 0;
            for (int c = 0; c < 2; ++c) {
                float x = in[c];
                filt[c].tick(x);
                x = tone < 0.5f ? filt[c].lp : filt[c].hp;
                if (hold) crushHeld[c] = std::round(x * q) / q;
                x = crushFactor > 1 || bits < 15.5f ? crushHeld[c] : x;
                float re, im; hil[c].process(x, re, im);
                float shifted = re * cs - im * (c == 0 ? sn : -sn);    // opposite directions per side
                x = x + (shifted - x) * shiftMix;
                float d = dly[c].read(delMs * 0.001f * sr);
                dly[c].push(x + d * delFb);
                x += d * delMix;
                x = softClip(x * drive) / std::min(drive, 3.0f);
                y[c] = in[c] + (x - in[c]) * p.mix;
            }
            // record what you hear from the chain
            ringL[(size_t)rw] = y[0]; ringR[(size_t)rw] = y[1];
            rw = (rw + 1) % ringLen; filled = std::min(filled + 1, ringLen);

            // pads
            float pl = 0, pr = 0; renderPads(pl, pr, p);
            ch[0][i] = y[0] + pl; if (numCh > 1) ch[1][i] = y[1] + pr;
        }
    }

private:
    struct PadVoice { bool active = false; int pad = 0; double pos = 0; float rate = 1, vel = 1; int start = 0, end = 0; bool rev = false; };

    void doGrab(const Params& p) {
        int back = 1 - front.load();
        Capture& c = cap[back];
        int len = (int)(beatsToMs(4.0f * (float)p.grabBars, p.bpm) * 0.001f * sr);
        len = std::min({ len, filled, (int)c.l.size() });
        if (len < 64) return;
        int start = (rw - len + ringLen) % ringLen;
        for (int i = 0; i < len; ++i) { int k = (start + i) % ringLen; c.l[(size_t)i] = ringL[(size_t)k]; c.r[(size_t)i] = ringR[(size_t)k]; }
        c.length = len;
        slice(c, p);
        for (auto& v : voices) v.active = false;
        front.store(back);
        generation.fetch_add(1);
    }

    void slice(Capture& c, const Params& p) {
        for (int k = 0; k <= kPads; ++k) c.sliceStart[k] = (int)((long long)c.length * k / kPads);
        if (p.sliceMode == SliceMode::Transient) {
            // onset strength = positive jumps in a fast envelope vs a slow one
            const int hop = 256; std::vector<std::pair<float, int>> onsets;
            float fast = 0, slow = 0;
            for (int i = 0; i < c.length; i += hop) {
                float pk = 0; for (int j = i; j < std::min(c.length, i + hop); ++j) pk = std::max(pk, std::abs(c.l[(size_t)j]) + std::abs(c.r[(size_t)j]));
                float prev = fast; fast = pk; slow = slow * 0.9f + pk * 0.1f;
                float jump = fast - std::max(prev, slow);
                if (jump > 0) onsets.push_back({ jump, i });
            }
            std::sort(onsets.begin(), onsets.end(), [](auto& a, auto& b) { return a.first > b.first; });
            std::vector<int> picks; picks.push_back(0);
            float thresh = onsets.empty() ? 0 : onsets.front().first * (0.6f - 0.55f * p.sensitivity);
            int minGap = (int)(0.06f * sr);
            for (auto& o : onsets) {
                if ((int)picks.size() >= kPads || o.first < thresh) break;
                bool ok = true; for (int s : picks) if (std::abs(s - o.second) < minGap) ok = false;
                if (ok) picks.push_back(snapZero(c, o.second));
            }
            std::sort(picks.begin(), picks.end());
            // fewer hits than pads: keep splitting the longest slice so every pad plays something
            while (picks.size() >= 2 && (int)picks.size() < kPads) {
                size_t best = 0; int bestLen = -1;
                for (size_t k = 0; k < picks.size(); ++k) {
                    int e = k + 1 < picks.size() ? picks[k + 1] : c.length;
                    if (e - picks[k] > bestLen) { bestLen = e - picks[k]; best = k; }
                }
                if (bestLen < minGap * 2) break;
                picks.insert(picks.begin() + (long)best + 1, snapZero(c, picks[best] + bestLen / 2));
            }
            if (picks.size() >= 4) {
                for (int k = 0; k < kPads; ++k) c.sliceStart[k] = k < (int)picks.size() ? picks[(size_t)k] : c.length;
                c.sliceStart[kPads] = c.length;
            }
        } else {
            for (int k = 1; k < kPads; ++k) c.sliceStart[k] = snapZero(c, c.sliceStart[k]);
        }
    }
    int snapZero(const Capture& c, int at) const {
        for (int d = 0; d < 400; ++d)
            for (int s : { at + d, at - d })
                if (s > 0 && s < c.length && (c.l[(size_t)s - 1] <= 0) != (c.l[(size_t)s] <= 0)) return s;
        return at;
    }

    void trigger(int pad, float vel, const Params& p) {
        const Capture& c = cap[front.load()];
        if (c.length <= 0) return;
        int s = c.sliceStart[pad], e = c.sliceStart[pad + 1];
        if (e - s < 16) return;
        PadVoice* v = &voices[0];
        for (auto& vv : voices) if (!vv.active) { v = &vv; break; }
        v->active = true; v->pad = pad; v->start = s; v->end = e; v->rev = p.padReverse;
        v->rate = semis(p.padPitch); v->vel = vel; v->pos = v->rev ? (double)(e - s - 1) : 0.0;
        padFlash[pad].store(1);
    }

    void renderPads(float& outL, float& outR, const Params& p) {
        const Capture& c = cap[front.load()];
        const float fadeS = p.clicky ? 0.0f : std::max(1.0f, p.padFade * 0.001f * sr);
        const float g = dbToGain(p.padGain);
        for (auto& v : voices) {
            if (!v.active) continue;
            int len = v.end - v.start;
            if (v.pos < 0 || v.pos >= len - 1) { v.active = false; continue; }
            int i0 = (int)v.pos; float f = (float)(v.pos - i0);
            int k = v.start + i0;
            float l = c.l[(size_t)k] + (c.l[(size_t)k + 1] - c.l[(size_t)k]) * f;
            float r = c.r[(size_t)k] + (c.r[(size_t)k + 1] - c.r[(size_t)k]) * f;
            float played = v.rev ? (float)(len - 1 - v.pos) : (float)v.pos;
            float remain = (float)len - played;
            float env = fadeS <= 0 ? 1.0f : std::min(1.0f, std::min(played / fadeS, remain / fadeS));
            outL += l * env * v.vel * g; outR += r * env * v.vel * g;
            v.pos += v.rev ? -v.rate : v.rate;
        }
    }

    float sr = 48000;
    std::vector<float> ringL, ringR; int ringLen = 0, rw = 0, filled = 0;
    Capture cap[2]; std::atomic<int> front { 0 };
    Svf filt[2]; DelayLine dly[2]; Hilbert hil[2];
    float shiftPhase = 0, crushHeld[2] = {}; long crushCount = 0; double lfoFree = 0;
    PadVoice voices[kPadVoices];
};
} // namespace mudpie
