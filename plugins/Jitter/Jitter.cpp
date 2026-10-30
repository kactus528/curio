#include "../../shared/CurioPlugin.h"
#include "JitterEngine.h"

using namespace curio;

class JitterProcessor : public CurioProcessor {
public:
    JitterProcessor() : CurioProcessor(
        BusesProperties().withInput("In", juce::AudioChannelSet::stereo()).withOutput("Out", juce::AudioChannelSet::stereo()),
        {
            P("amount", "Amount", 0, 5, 1, "%", "Instability", 1.0f),
            P("rate", "Rate", 5, 200, 60, "Hz", "Instability", 40),
            P("smooth", "Smooth", 0, 20, 2, "ms", "Instability", 3),
            P("age", "Age", 0, 100, 0, "%", "Instability"),
            P("pitch", "Pitch", 0, 100, 100, "%", "Targets"),
            P("tone", "Tone", 0, 100, 50, "%", "Targets"),
            P("pan", "Pan", 0, 100, 30, "%", "Targets"),
            B("unlink", "L/R Unlink", true, "Targets"),
            P("mix", "Mix", 0, 100, 100, "%", "Output"),
        },
        {
            { "Analog 1% @ 60 Hz", {} },
            { "Barely There", { { "amount", 0.4f }, { "tone", 20 }, { "pan", 10 } } },
            { "Warm Poly", { { "amount", 1.5f }, { "rate", 30 }, { "smooth", 6 }, { "pan", 45 } } },
            { "Old Oscillators", { { "amount", 2.5f }, { "rate", 12 }, { "smooth", 12 }, { "age", 30 } } },
            { "Unstable PSU", { { "amount", 3.5f }, { "rate", 120 }, { "smooth", 0.5f }, { "tone", 80 } } },
            { "Seasick", { { "amount", 5 }, { "rate", 6 }, { "smooth", 20 }, { "age", 60 }, { "pan", 70 } } },
            { "Tone Only", { { "pitch", 0 }, { "tone", 100 }, { "amount", 3 } } },
            { "Wide Shimmer", { { "amount", 1.2f }, { "rate", 90 }, { "pan", 100 }, { "smooth", 4 } } },
            { "Tape Age", { { "amount", 2 }, { "age", 100 }, { "smooth", 15 }, { "rate", 40 } } },
            { "Mono Wobble", { { "unlink", 0 }, { "pan", 0 }, { "amount", 2.5f }, { "rate", 25 } } },
        },
        { "Jitter", "Micro sample-and-hold instability: digital in, analog out", juce::Colour(0xff4fc3a1) }) {}

    void prepareToPlay(double sr, int) override { eng.prepare((float)sr); setLatencySamples(eng.latencySamples()); }
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& buf, juce::MidiBuffer&) override {
        juce::ScopedNoDenormals nd;
        jitter::Params p;
        p.amount = v("amount"); p.rate = v("rate"); p.smooth = v("smooth"); p.age = v("age") * 0.01f;
        p.pitch = v("pitch") * 0.01f; p.tone = v("tone") * 0.01f; p.pan = v("pan") * 0.01f; p.unlink = vb("unlink"); p.mix = v("mix") * 0.01f;
        eng.process(buf.getArrayOfWritePointers(), buf.getNumChannels(), buf.getNumSamples(), p);
    }
private:
    jitter::Engine eng;
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new JitterProcessor(); }
