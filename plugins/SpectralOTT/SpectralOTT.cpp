#include "../../shared/CurioPlugin.h"
#include "../../shared/CurioViews.h"
#include "SpectralOTTEngine.h"

using namespace curio;

class SpectralOTTProcessor : public CurioProcessor {
public:
    SpectralOTTProcessor() : CurioProcessor(
        BusesProperties().withInput("In", juce::AudioChannelSet::stereo()).withOutput("Out", juce::AudioChannelSet::stereo()),
        {
            P("depth", "Depth", 0, 100, 50, "%", "Compression"),
            P("balance", "Down <> Up", -100, 100, 0, "%", "Compression"),
            P("time", "Time", 5, 500, 60, "ms", "Compression", 60),
            P("range", "Max Lift", 0, 30, 18, "dB", "Compression"),
            P("smoothing", "Freq Smooth", 0, 16, 3, "bins", "Shape"),
            P("tilt", "Tilt", -10, 10, 0, "", "Shape"),
            P("floor", "Noise Floor", -100, -40, -70, "dB", "Shape"),
            P("inGain", "In", -24, 24, 0, "dB", "Output"),
            P("outGain", "Out", -24, 24, 0, "dB", "Output"),
            P("mix", "Mix", 0, 100, 100, "%", "Output"),
        },
        {
            { "Polish", {} },
            { "Full Smash", { { "depth", 100 }, { "mix", 100 } } },
            { "Parallel Shine", { { "depth", 80 }, { "mix", 35 } } },
            { "Lift the Details", { { "balance", 70 }, { "depth", 60 }, { "range", 12 } } },
            { "Tame Harshness", { { "balance", -80 }, { "depth", 70 }, { "tilt", 6 } } },
            { "Drum Bus Glue", { { "depth", 45 }, { "time", 25 }, { "smoothing", 6 }, { "mix", 70 } } },
            { "Vocal Air", { { "depth", 55 }, { "tilt", 7 }, { "balance", 30 } } },
            { "Bass Even-out", { { "depth", 60 }, { "tilt", -6 }, { "time", 120 } } },
            { "Robot Grit", { { "depth", 100 }, { "smoothing", 0 }, { "time", 8 } } },
            { "Pad Bloom", { { "balance", 90 }, { "depth", 80 }, { "time", 250 }, { "range", 24 } } },
        },
        { "Spectral OTT", "Up + down compression on every frequency at once", juce::Colour(0xffe05a9c) }) {}

    void prepareToPlay(double sr, int) override { eng.prepare((float)sr); setLatencySamples(eng.latencySamples()); }
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& buf, juce::MidiBuffer&) override {
        juce::ScopedNoDenormals nd;
        spectralott::Params p;
        p.depth = v("depth") * 0.01f; p.balance = v("balance") * 0.01f; p.time = v("time"); p.range = v("range");
        p.smoothing = v("smoothing"); p.tilt = v("tilt"); p.floorDb = v("floor");
        p.inGain = v("inGain"); p.outGain = v("outGain"); p.mix = v("mix") * 0.01f;
        eng.process(buf.getArrayOfWritePointers(), buf.getNumChannels(), buf.getNumSamples(), p);
    }

    spectralott::Engine eng;

    struct GainView : LiveView {
        SpectralOTTProcessor& pr; float shown[spectralott::Engine::kDisplay] = {};
        explicit GainView(SpectralOTTProcessor& p) : LiveView(p.info().accent), pr(p) {}
        void tick() override { for (int i = 0; i < spectralott::Engine::kDisplay; ++i) shown[i] += (pr.eng.display[i].load() - shown[i]) * 0.4f; }
        void paint(juce::Graphics& g) override {
            paintPanel(g);
            auto r = getLocalBounds().reduced(14, 10).toFloat();
            const float range = 24.0f;
            auto yOf = [&](float db) { return r.getCentreY() - clampf(db, -range, range) / range * r.getHeight() * 0.5f; };
            g.setColour(juce::Colour(0xff2a2d34)); g.drawHorizontalLine((int)r.getCentreY(), r.getX(), r.getRight());
            g.setColour(juce::Colours::white.withAlpha(0.35f)); g.setFont(juce::FontOptions(10.5f));
            g.drawText("+24 dB lift", r.toNearestInt().removeFromTop(12), juce::Justification::topLeft);
            g.drawText("-24 dB cut", r.toNearestInt().removeFromBottom(12), juce::Justification::bottomLeft);
            g.drawText("20 Hz  ->  20 kHz", r.toNearestInt().removeFromBottom(12), juce::Justification::bottomRight);
            const int n = spectralott::Engine::kDisplay; const float bw = r.getWidth() / n;
            for (int i = 0; i < n; ++i) {
                float db = shown[i], y = yOf(db), x = r.getX() + i * bw;
                g.setColour(db >= 0 ? accent : accent.withRotatedHue(0.5f));
                g.fillRect(juce::Rectangle<float>(x, std::min(y, r.getCentreY()), bw - 1, std::abs(y - r.getCentreY()) + 1));
            }
        }
    };
    std::unique_ptr<juce::Component> createCustomView() override { return std::make_unique<GainView>(*this); }
    int customViewHeight() const override { return 120; }
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SpectralOTTProcessor(); }
