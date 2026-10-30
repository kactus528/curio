// Keytrack Res — a resonant filter played by MIDI: each note retunes and re-pings it
// (the Filterverse "MIDI into cutoff" arp trick), with an L/R cutoff offset for width.
#pragma once
#include "../../shared/curio_dsp.h"
#include "../../shared/curio_midi.h"

namespace keytrack {
using namespace curio;

enum class Type { LowPass = 0, BandPass, HighPass, Comb };

struct Params {
    Type type = Type::BandPass;
    float baseCutoff = 800.0f;   // Hz when no note / keytrack 0
    float keyOffset = 12.0f;     // semitones added to the played note
    float keytrack = 1.0f;       // 0..1
    float resonance = 0.85f;
    float envAmount = 24.0f;     // semitones of "ping" on each note
    float envDecay = 60.0f;      // ms
    float glide = 15.0f;         // ms
    float lrOffset = 0.08f;      // 0..0.3 fraction of cutoff
    float drive = 0.2f;
    float mix = 1.0f;
    bool seqOn = false;          // fallback when no MIDI
    int seqDiv = 5;              // curio::divisions index
    float seqSteps[8] = { 0, 12, 7, 3, 0, 10, 7, 15 };
    float seqRoot = 48.0f;
    float bpm = 120.0f; double ppq = 0; bool playing = false;
};

class Engine {
public:
    void prepare(float sampleRate) {
        sr = sampleRate;
        for (auto& f : svf) f.reset();
        for (auto& c : comb) c.prepare((int)(sr * 0.06f));
        glideNote.reset(60.0f); env = 0; held = 0; freeRun = 0; lastStep = -1;
    }

    void process(float* const* ch, int numCh, int n, const Params& p, const NoteEvents& ev) {
        glideNote.setTime(std::max(p.glide, 0.01f), sr);
        const float envCoeff = std::exp(-1.0f / (0.001f * std::max(p.envDecay, 1.0f) * sr));
        const float drv = 1.0f + p.drive * 8.0f;
        const double stepBeats = divisions()[(size_t)clampf((float)p.seqDiv, 0, (float)divisions().size() - 1)].beats;
        const double beatsPerSample = p.bpm / 60.0 / sr;
        size_t e = 0;
        for (int i = 0; i < n; ++i) {
            while (e < ev.size() && ev[e].offset <= i) {
                const auto& m = ev[e++];
                if (m.on) { targetNote = (float)m.note; env = m.vel; ++held; }
                else held = std::max(0, held - 1);
            }
            if (p.seqOn) {
                double beat = p.playing ? p.ppq + i * beatsPerSample : (freeRun += beatsPerSample);
                long step = (long)std::floor(beat / stepBeats);
                if (step != lastStep) { lastStep = step; targetNote = p.seqRoot + p.seqSteps[((step % 8) + 8) % 8]; env = 1.0f; }
            }
            float note = glideNote.process(targetNote) + p.keyOffset;
            float keyHz = mtof(note);
            float baseHz = p.baseCutoff;
            // log-interpolate between fixed cutoff and the note
            float hz = std::exp(std::log(baseHz) + (std::log(keyHz) - std::log(baseHz)) * p.keytrack);
            hz *= semis(env * p.envAmount);
            env *= envCoeff;

            if ((i & 15) == 0) {
                for (int c = 0; c < 2; ++c) {
                    float f = hz * (c == 0 ? 1.0f - p.lrOffset : 1.0f + p.lrOffset);
                    svf[c].set(f, p.resonance, sr);
                    combHz[c] = f;
                }
            }
            for (int c = 0; c < std::min(numCh, 2); ++c) {
                float x = ch[c][i] * drv, y;
                if (p.type == Type::Comb) {
                    float d = sr / clampf(combHz[c], 20.0f, sr * 0.45f);
                    float fb = 0.5f + 0.495f * p.resonance;
                    y = x * (1.0f - fb) + comb[c].read(d) * fb;
                    comb[c].push(softClip(y));
                } else {
                    svf[c].tick(x);
                    y = p.type == Type::LowPass ? svf[c].lp : p.type == Type::HighPass ? svf[c].hp : svf[c].bp * (2.0f - 1.98f * p.resonance + 0.3f);
                }
                y = softClip(y) / std::max(1.0f, drv * 0.6f);
                ch[c][i] = ch[c][i] + (y - ch[c][i]) * p.mix;
            }
        }
    }
private:
    float sr = 48000, targetNote = 60, env = 0, combHz[2] = { 440, 440 };
    int held = 0; long lastStep = -1; double freeRun = 0;
    Svf svf[2];
    DelayLine comb[2];
    Smooth glideNote;
};
} // namespace keytrack
