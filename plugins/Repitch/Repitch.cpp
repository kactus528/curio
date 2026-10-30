#include "../../shared/CurioPlugin.h"
#include "../../shared/CurioViews.h"
#include "RepitchEngine.h"

using namespace curio;

class RepitchProcessor : public CurioProcessor {
public:
    RepitchProcessor() : CurioProcessor(
        BusesProperties().withInput("In", juce::AudioChannelSet::stereo()).withOutput("Out", juce::AudioChannelSet::stereo()),
        {
            C("unit", "Time Unit", { "Sync", "ms", "Hz" }, 0, "Time"),
            C("division", "Division", divisionNames(), 8, "Time"),
            P("ms", "Time ms", 1, 4000, 375, "ms", "Time", 300),
            P("hz", "Time Hz", 0.25f, 1000, 4, "Hz", "Time", 20),
            C("change", "Change", { "Repitch", "Fade", "Jump" }, 0, "Time"),
            P("glide", "Glide", 1, 3000, 300, "ms", "Time", 250),
            P("feedback", "Feedback", 0, 105, 45, "%", "Echo"),
            P("tone", "Tone", 200, 20000, 6000, "Hz", "Echo", 3000),
            P("drive", "Drive", 0, 100, 0, "%", "Echo"),
            B("pingpong", "Ping-Pong", true, "Echo"),
            P("lfoRate", "LFO Rate", 0.01f, 10, 0.25f, "Hz", "Doppler", 0.5f),
            P("lfoDepth", "LFO Depth", 0, 100, 0, "%", "Doppler"),
            P("swoop", "Swoop", -2, 2, 0, "oct", "Doppler"),
            P("mix", "Mix", 0, 100, 35, "%", "Output"),
        },
        {
            { "Dotted Eighth", { { "division", 10 } } },
            { "Tape Wobble", { { "division", 8 }, { "lfoDepth", 8 }, { "lfoRate", 0.7f }, { "tone", 3500 }, { "drive", 25 } } },
            { "Doppler Pass-By", { { "division", 11 }, { "glide", 1200 }, { "lfoDepth", 60 }, { "lfoRate", 0.12f }, { "feedback", 30 } } },
            { "Riser Machine", { { "division", 5 }, { "feedback", 92 }, { "glide", 2500 }, { "mix", 50 } } },
            { "Comb Pitch", { { "unit", 2 }, { "hz", 110 }, { "feedback", 90 }, { "mix", 60 }, { "glide", 80 } } },
            { "Slapback", { { "unit", 1 }, { "ms", 95 }, { "feedback", 10 }, { "pingpong", 0 }, { "mix", 40 } } },
            { "Clicky Jumps", { { "change", 2 }, { "division", 5 }, { "feedback", 60 } } },
            { "Smooth Fades", { { "change", 1 }, { "glide", 150 }, { "division", 11 } } },
            { "Runaway", { { "feedback", 104 }, { "drive", 60 }, { "tone", 2500 }, { "division", 8 } } },
            { "Wide Triplets", { { "division", 6 }, { "feedback", 55 }, { "tone", 9000 } } },
        },
        { "Repitch", "Tempo-math delay whose time changes bend pitch like Doppler", juce::Colour(0xff5aa9e6) }) {}

    void prepareToPlay(double sr, int) override { eng.prepare((float)sr); }
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& buf, juce::MidiBuffer&) override {
        juce::ScopedNoDenormals nd;
        repitch::Params p;
        p.unit = (repitch::TimeUnit)vi("unit"); p.division = vi("division"); p.ms = v("ms"); p.hz = v("hz");
        p.change = (repitch::Change)vi("change"); p.glide = v("glide");
        p.feedback = v("feedback") * 0.01f; p.tone = v("tone"); p.drive = v("drive") * 0.01f; p.pingpong = vb("pingpong");
        p.lfoRate = v("lfoRate"); p.lfoDepth = v("lfoDepth") * 0.01f; p.swoop = v("swoop"); p.mix = v("mix") * 0.01f;
        p.bpm = (float)transport().bpm; bpm.store(p.bpm);
        eng.process(buf.getArrayOfWritePointers(), buf.getNumChannels(), buf.getNumSamples(), p);
        curMs.store(eng.currentMs());
    }

    std::atomic<float> bpm { 120 }, curMs { 0 };

    struct Readout : LiveView {
        RepitchProcessor& pr;
        explicit Readout(RepitchProcessor& p) : LiveView(p.info().accent), pr(p) {}
        void paint(juce::Graphics& g) override {
            paintPanel(g);
            float ms = pr.curMs.load(), b = pr.bpm.load();
            float beats = ms / beatsToMs(1.0f, b);
            const char* nearest = "-"; float best = 1e9f;
            for (auto& d : divisions()) { float e = std::abs(std::log2(d.beats / std::max(beats, 1e-4f))); if (e < best) { best = e; nearest = d.name; } }
            auto r = getLocalBounds().reduced(14, 8);
            auto col = [&](juce::String big, juce::String small) {
                auto c = r.removeFromLeft(r.getWidth() / 4 + 1);
                g.setColour(juce::Colours::white); g.setFont(juce::FontOptions(26.0f, juce::Font::bold)); g.drawText(big, c.removeFromTop(34), juce::Justification::centredLeft);
                g.setColour(juce::Colours::white.withAlpha(0.5f)); g.setFont(juce::FontOptions(12.0f)); g.drawText(small, c, juce::Justification::topLeft);
            };
            col(juce::String(ms, ms < 100 ? 1 : 0) + " ms", "delay time now");
            col(juce::String(1000.0f / std::max(ms, 0.01f), 2) + " Hz", "same time as a rate");
            col(juce::String(beats, 3) + " beats", juce::String("nearest note: ") + nearest + (best < 0.01f ? "" : " (off-grid)"));
            col(juce::String(b, 1) + " BPM", "ms = 60000 / BPM x beats");
        }
    };
    std::unique_ptr<juce::Component> createCustomView() override { return std::make_unique<Readout>(*this); }
    int customViewHeight() const override { return 72; }
private:
    repitch::Engine eng;
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new RepitchProcessor(); }
