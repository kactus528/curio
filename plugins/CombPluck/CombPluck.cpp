#include "../../shared/CurioPlugin.h"
#include "CombPluckEngine.h"

using namespace curio;

class CombPluckProcessor : public CurioProcessor {
public:
    CombPluckProcessor() : CurioProcessor(
        BusesProperties().withOutput("Out", juce::AudioChannelSet::stereo()),
        {
            C("exciter", "Exciter", { "White", "Pink", "Bubble", "Click" }, 0, "Exciter"),
            P("burst", "Burst", 0.3f, 50, 6, "ms", "Exciter", 6),
            P("brightness", "Brightness", 200, 18000, 8000, "Hz", "Exciter", 3000),
            P("velBright", "Vel > Bright", 0, 100, 60, "%", "Exciter"),
            P("decay", "Decay", 0.05f, 10, 1.2f, "s", "Resonator", 1.0f),
            P("damping", "Damping", 0, 100, 35, "%", "Resonator"),
            P("blend", "Comb > Modal", 0, 100, 0, "%", "Resonator"),
            C("modal", "Modal Shape", { "String", "Bar", "Glass", "Bell", "Vocal" }, 0, "Resonator"),
            P("inharm", "Inharmonic", 0, 100, 0, "%", "Resonator"),
            P("punch", "Punch", 0, 24, 7, "st", "Punch & Life"),
            P("punchTime", "Punch Time", 1, 200, 25, "ms", "Punch & Life", 30),
            P("drift", "Drift", 0, 30, 4, "ct", "Punch & Life"),
            P("spread", "Spread", 0, 100, 50, "%", "Output"),
            P("tune", "Tune", -24, 24, 0, "st", "Output"),
            P("gain", "Gain", -24, 12, 0, "dB", "Output"),
        },
        {
            { "Noise Pluck", {} },
            { "Nylon-ish", { { "exciter", 1 }, { "brightness", 4000 }, { "damping", 55 }, { "decay", 2.0f }, { "punch", 2 } } },
            { "Harp Glass", { { "blend", 70 }, { "modal", 2 }, { "decay", 3.5f }, { "brightness", 12000 } } },
            { "Bell Tower", { { "blend", 100 }, { "modal", 3 }, { "decay", 6 }, { "punch", 0 }, { "exciter", 3 } } },
            { "Marimba Bar", { { "blend", 85 }, { "modal", 1 }, { "decay", 0.9f }, { "burst", 3 }, { "exciter", 3 } } },
            { "Bubble Drops", { { "exciter", 2 }, { "burst", 18 }, { "decay", 0.8f }, { "punch", 12 }, { "punchTime", 40 } } },
            { "Tight Perc", { { "decay", 0.15f }, { "punch", 18 }, { "punchTime", 12 }, { "burst", 2 } } },
            { "Dark Kalimba", { { "blend", 40 }, { "modal", 1 }, { "damping", 75 }, { "brightness", 1800 }, { "decay", 1.6f } } },
            { "Vocal Ping", { { "blend", 100 }, { "modal", 4 }, { "decay", 1.5f }, { "inharm", 20 } } },
            { "Detuned Strings", { { "drift", 18 }, { "decay", 4 }, { "spread", 100 }, { "damping", 25 } } },
            { "Metal Scrape", { { "exciter", 0 }, { "burst", 40 }, { "blend", 60 }, { "modal", 2 }, { "inharm", 80 }, { "decay", 2.5f } } },
        },
        { "Comb Pluck", "Noise bursts into a maxed comb: physical-modelling plucks", juce::Colour(0xffe07b6a) }) {
        notes.reserve(512);
    }

    void prepareToPlay(double sr, int) override { eng.prepare((float)sr); }
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& buf, juce::MidiBuffer& midi) override {
        juce::ScopedNoDenormals nd;
        combpluck::Params p;
        p.exciter = (combpluck::Exciter)vi("exciter"); p.burst = v("burst"); p.brightness = v("brightness"); p.velBright = v("velBright") * 0.01f;
        p.decay = v("decay"); p.damping = v("damping") * 0.01f; p.blend = v("blend") * 0.01f; p.modal = (combpluck::Modal)vi("modal");
        p.inharm = v("inharm") * 0.01f; p.punch = v("punch"); p.punchTime = v("punchTime"); p.drift = v("drift");
        p.spread = v("spread") * 0.01f; p.tune = v("tune"); p.gain = v("gain");
        collectNotes(midi, notes);
        eng.process(buf.getArrayOfWritePointers(), buf.getNumChannels(), buf.getNumSamples(), p, notes);
    }
    double getTailLengthSeconds() const override { return 10.0; }
private:
    combpluck::Engine eng; NoteEvents notes;
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new CombPluckProcessor(); }
