#include "../../shared/CurioPlugin.h"
#include "../../shared/CurioViews.h"
#include "ManifestEngine.h"

using namespace curio;

static const char* kWeightIds[manifest::kNumEvents] = { "wStutter", "wGate", "wReverse", "wCrush", "wThrow", "wPitch", "wTape", "wSilence" };

class ManifestProcessor : public CurioProcessor {
public:
    ManifestProcessor() : CurioProcessor(
        BusesProperties().withInput("In", juce::AudioChannelSet::stereo()).withOutput("Out", juce::AudioChannelSet::stereo()),
        {
            P("density", "Density", 0, 100, 30, "%", "Dice"),
            C("grid", "Grid", divisionNames(), 5, "Dice"),
            P("maxLen", "Max Length", 1, 8, 4, "steps", "Dice", 0, 1),
            P("seed", "Seed", 1, 999, 1, "", "Dice", 0, 1),
            B("lock", "Lock Pattern", false, "Pattern"),
            C("bars", "Pattern", { "1 bar", "2 bars", "4 bars" }, 0, "Pattern"),
            P("variation", "Variation", 0, 100, 0, "%", "Pattern"),
            P("wStutter", "Stutter", 0, 100, 100, "%", "Event weights"),
            P("wGate", "Gate", 0, 100, 60, "%", "Event weights"),
            P("wReverse", "Reverse", 0, 100, 50, "%", "Event weights"),
            P("wCrush", "Crush", 0, 100, 50, "%", "Event weights"),
            P("wThrow", "Verb Throw", 0, 100, 40, "%", "Event weights"),
            P("wPitch", "Pitch Drop", 0, 100, 30, "%", "Event weights"),
            P("wTape", "Tape Stop", 0, 100, 30, "%", "Event weights"),
            P("wSilence", "Silence", 0, 100, 20, "%", "Event weights"),
            P("tail", "Verb Tail", 0, 100, 50, "%", "Output"),
            P("mix", "Mix", 0, 100, 100, "%", "Output"),
        },
        {
            { "Sprinkle", {} },
            { "Subtle Edits", { { "density", 12 }, { "maxLen", 2 } } },
            { "Stutter Heaven", { { "density", 45 }, { "wStutter", 100 }, { "wGate", 20 }, { "wReverse", 10 }, { "wCrush", 0 }, { "wThrow", 0 }, { "wPitch", 0 }, { "wTape", 0 }, { "wSilence", 0 } } },
            { "Locked Fill Bar", { { "lock", 1 }, { "density", 40 }, { "bars", 0 }, { "seed", 7 } } },
            { "Evolving 4 Bars", { { "lock", 1 }, { "bars", 2 }, { "variation", 20 }, { "density", 30 } } },
            { "Throws Only", { { "wStutter", 0 }, { "wGate", 0 }, { "wReverse", 0 }, { "wCrush", 0 }, { "wThrow", 100 }, { "wPitch", 0 }, { "wTape", 0 }, { "wSilence", 0 }, { "density", 20 }, { "tail", 80 } } },
            { "Tape Wreck", { { "wTape", 100 }, { "wPitch", 80 }, { "wStutter", 20 }, { "density", 25 }, { "grid", 8 } } },
            { "Vocal Chopper", { { "grid", 2 }, { "density", 35 }, { "wReverse", 80 }, { "wGate", 80 }, { "maxLen", 3 } } },
            { "32nd Machine Gun", { { "grid", 2 }, { "density", 50 }, { "wStutter", 100 }, { "maxLen", 6 } } },
            { "Holes", { { "wSilence", 100 }, { "wGate", 60 }, { "wStutter", 0 }, { "wReverse", 0 }, { "wCrush", 0 }, { "wThrow", 0 }, { "wPitch", 0 }, { "wTape", 0 }, { "density", 25 } } },
        },
        { "Manifest", "Dice on every step: random glitches that stay on the grid", juce::Colour(0xffd0d35a) }) {}

    void prepareToPlay(double sr, int) override { eng.prepare((float)sr); }
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& buf, juce::MidiBuffer&) override {
        juce::ScopedNoDenormals nd;
        manifest::Params p;
        p.density = v("density") * 0.01f; p.grid = vi("grid"); p.maxLen = vi("maxLen"); p.seed = vi("seed");
        p.lock = vb("lock"); p.patternBars = 1 << vi("bars"); p.variation = v("variation") * 0.01f;
        for (int i = 0; i < manifest::kNumEvents; ++i) p.weight[i] = v(kWeightIds[i]) * 0.01f;
        p.tail = v("tail") * 0.01f; p.mix = v("mix") * 0.01f;
        auto t = transport(); p.bpm = (float)t.bpm; p.ppq = t.ppq; p.playing = t.playing;
        eng.process(buf.getArrayOfWritePointers(), buf.getNumChannels(), buf.getNumSamples(), p);
        int e = eng.currentEvent(); if (e >= 0) { lastEvent.store(e); flash.store(6); }
    }

    std::atomic<int> lastEvent { -1 }, flash { 0 };

    struct EventView : LiveView {
        ManifestProcessor& pr; float glow[manifest::kNumEvents] = {};
        explicit EventView(ManifestProcessor& p) : LiveView(p.info().accent), pr(p) {}
        void tick() override {
            for (auto& gl : glow) gl *= 0.85f;
            if (pr.flash.load() > 0) { pr.flash.fetch_sub(1); int e = pr.lastEvent.load(); if (e >= 0) glow[e] = 1.0f; }
        }
        void paint(juce::Graphics& g) override {
            paintPanel(g);
            auto r = getLocalBounds().reduced(10, 10);
            int w = r.getWidth() / manifest::kNumEvents;
            for (int i = 0; i < manifest::kNumEvents; ++i) {
                auto c = r.removeFromLeft(w).reduced(4, 0).toFloat();
                g.setColour(juce::Colour(0xff23252b).interpolatedWith(accent, glow[i])); g.fillRoundedRectangle(c, 5);
                g.setColour(glow[i] > 0.5f ? juce::Colours::black : juce::Colours::white.withAlpha(0.7f));
                g.setFont(juce::FontOptions(12.5f, juce::Font::bold)); g.drawText(manifest::eventName(i), c, juce::Justification::centred);
            }
        }
    };
    std::unique_ptr<juce::Component> createCustomView() override { return std::make_unique<EventView>(*this); }
    int customViewHeight() const override { return 54; }
private:
    manifest::Engine eng;
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ManifestProcessor(); }
