// Comb Pluck — noise bursts into a maxed-resonance comb (and a modal bank) for
// physical-modelling plucks, plus a short pitch envelope for "punch".
#pragma once
#include "../../shared/curio_dsp.h"
#include "../../shared/curio_midi.h"

namespace combpluck {
using namespace curio;

enum class Exciter { White = 0, Pink, Bubble, Click };
enum class Modal { String = 0, Bar, Glass, Bell, Vocal };

struct Params {
    Exciter exciter = Exciter::White;
    float burst = 6.0f;          // ms
    float brightness = 8000.0f;  // Hz, exciter LP
    float decay = 1.2f;          // s (T60)
    float damping = 0.35f;       // 0..1 loop low-pass
    float blend = 0.0f;          // 0 comb .. 1 modal
    Modal modal = Modal::String;
    float inharm = 0.0f;         // 0..1 stretches modal ratios
    float punch = 7.0f;          // semitones
    float punchTime = 25.0f;     // ms
    float drift = 4.0f;          // cents of S&H wobble
    float spread = 0.5f;         // stereo spread
    float velBright = 0.6f;
    float gain = 0.0f;           // dB
    float tune = 0.0f;           // semitones
};

inline const float* modalRatios(Modal m) {
    static const float r[5][8] = {
        { 1, 2, 3, 4, 5, 6, 7, 8 },
        { 1, 2.756f, 5.404f, 8.933f, 13.34f, 18.64f, 24.81f, 31.87f },
        { 1, 2.32f, 4.25f, 6.63f, 9.38f, 12.5f, 15.9f, 19.6f },
        { 0.5f, 1, 1.183f, 1.506f, 2.0f, 2.514f, 2.662f, 3.011f },
        { 1, 2, 3, 4.1f, 5.2f, 6.9f, 8.3f, 9.8f } };
    return r[(int)m];
}

class Voice {
public:
    int note = -1; bool active = false; long age = 0;
    void prepare(float sampleRate, uint32_t seed) { sr = sampleRate; comb.prepare((int)(sr / 15.0f)); rng.seed(seed); sh.prepare(sr, seed * 7 + 3); }
    void start(int n, float vel, const Params& p, float panPos) {
        note = n; active = true; age = 0; this->vel = vel; t = 0; level = 0; quiet = 0; pan = panPos;
        comb.clear(); loopLp = 0; punchEnv = 1.0f; pinkB[0] = pinkB[1] = pinkB[2] = 0; bubPhase = 0; bubFreq = 300; bubT = 0;
        for (auto& m : modes) m.reset();
        float bright = p.brightness * (1.0f - p.velBright + p.velBright * vel);
        exLp.setCutoff(bright, sr);
        baseHz = mtof((float)n + p.tune);
        const float* r = modalRatios(p.modal);
        for (int k = 0; k < 8; ++k) {
            float ratio = r[k] * (1.0f + p.inharm * 0.02f * (float)(k * k));
            float f = baseHz * ratio;
            modeOn[k] = f < sr * 0.45f;
            float q = kPi * f * std::max(0.05f, p.decay) / 6.9f;     // T60 -> Q
            if (modeOn[k]) modes[k].setQ(f, std::min(q, 4000.0f), sr);
            modeGain[k] = 1.0f / (1.0f + 0.35f * (float)k);
        }
    }
    void release() { /* plucks ring out naturally */ }

    void render(float* L, float* R, int n, const Params& p) {
        if (!active) return;
        const float burstS = std::max(0.3f, p.burst) * 0.001f * sr;
        const float fb = std::pow(10.0f, -3.0f / (std::max(0.05f, p.decay) * baseHz)); // per-period T60
        const float dampA = 0.05f + 0.9f * p.damping;
        const float gl = std::sqrt(0.5f * (1.0f - pan)), gr = std::sqrt(0.5f * (1.0f + pan));
        const float punchK = std::exp(-1.0f / (0.001f * std::max(1.0f, p.punchTime) * sr));
        for (int i = 0; i < n; ++i, ++age) {
            // exciter
            float ex = 0;
            if (t < burstS) {
                float envA = std::min(1.0f, t / (0.0005f * sr + 1)), envD = 1.0f - t / burstS;
                float w = rng.bi();
                switch (p.exciter) {
                    case Exciter::White: ex = w; break;
                    case Exciter::Pink: pinkB[0] = 0.997f * pinkB[0] + 0.029591f * w; pinkB[1] = 0.985f * pinkB[1] + 0.032534f * w; pinkB[2] = 0.95f * pinkB[2] + 0.048056f * w; ex = (pinkB[0] + pinkB[1] + pinkB[2]) * 3.0f; break;
                    case Exciter::Bubble:
                        bubT += 1; if (bubT > sr * 0.002f) { bubT = 0; bubFreq = 200 + 2400 * rng.uni(); }
                        bubFreq *= 1.0004f; bubPhase += bubFreq / sr; ex = std::sin(kTwoPi * bubPhase) * 1.2f; break;
                    case Exciter::Click: ex = t < 2 ? 1.0f : w * 0.2f * std::exp(-t / (0.0003f * sr)); break;
                }
                ex = exLp.lp(ex) * envA * envD * vel;
            }
            t += 1.0f;
            // pitch: punch envelope + drift wobble
            float wob = sh.process(18.0f) * p.drift;
            float hz = baseHz * semis(punchEnv * p.punch + wob * 0.01f);
            punchEnv *= punchK;
            // comb resonator
            float y = 0;
            if (p.blend < 0.999f) {
                float d = sr / clampf(hz, 20.0f, sr * 0.45f);
                float r = comb.read(d);
                loopLp = r + (loopLp - r) * dampA;
                float in = ex + loopLp * fb;
                comb.push(in);
                y += r * (1.0f - p.blend) * 2.5f;
            }
            if (p.blend > 0.001f) {
                float m = 0;
                for (int k = 0; k < 8; ++k) if (modeOn[k]) { modes[k].tick(ex); m += modes[k].bp * modeGain[k]; }
                y += m * p.blend * 0.5f;
            }
            float a = std::abs(y); level = std::max(a, level * 0.9995f);
            L[i] += y * gl; R[i] += y * gr;
        }
        if (t > burstS && level < 1.0e-4f) { if (++quiet > 4) active = false; } else quiet = 0;
    }
private:
    float sr = 48000, vel = 1, t = 0, level = 0, pan = 0, baseHz = 220, loopLp = 0, punchEnv = 0;
    float pinkB[3] = {}, bubPhase = 0, bubFreq = 300, bubT = 0;
    int quiet = 0;
    DelayLine comb; Rng rng; OnePole exLp; SampleHold sh;
    Svf modes[8]; bool modeOn[8] = {}; float modeGain[8] = {};
};

class Engine {
public:
    static constexpr int kVoices = 16;
    void prepare(float sampleRate) {
        sr = sampleRate;
        for (int i = 0; i < kVoices; ++i) voices[i].prepare(sr, (uint32_t)(i * 97 + 13));
        tmpL.assign(4096, 0); tmpR.assign(4096, 0); counter = 0;
    }
    void process(float* const* ch, int numCh, int n, const Params& p, const NoteEvents& ev) {
        if ((int)tmpL.size() < n) { tmpL.resize((size_t)n); tmpR.resize((size_t)n); }
        std::fill(tmpL.begin(), tmpL.begin() + n, 0.0f); std::fill(tmpR.begin(), tmpR.begin() + n, 0.0f);
        int pos = 0; size_t e = 0;
        while (pos < n) {
            int next = n;
            while (e < ev.size() && ev[e].offset <= pos) { handle(ev[e++], p); }
            if (e < ev.size()) next = std::min(n, ev[e].offset);
            for (auto& v : voices) v.render(tmpL.data() + pos, tmpR.data() + pos, next - pos, p);
            pos = next;
        }
        const float g = dbToGain(p.gain) * 0.6f;
        for (int i = 0; i < n; ++i) {
            float l = softClip(tmpL[(size_t)i] * g), r = softClip(tmpR[(size_t)i] * g);
            ch[0][i] = l; if (numCh > 1) ch[1][i] = r;
        }
    }
private:
    void handle(const NoteEvent& m, const Params& p) {
        if (!m.on) return;
        Voice* best = nullptr;
        for (auto& v : voices) if (!v.active) { best = &v; break; }
        if (!best) { best = &voices[0]; for (auto& v : voices) if (v.age > best->age) best = &v; }
        float pan = ((counter++ % 2) ? 1.0f : -1.0f) * p.spread * (0.4f + 0.6f * ((m.note % 7) / 6.0f));
        best->start(m.note, m.vel, p, pan);
    }
    float sr = 48000; long counter = 0;
    Voice voices[kVoices];
    std::vector<float> tmpL, tmpR;
};
} // namespace combpluck
