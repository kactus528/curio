#include "../../shared/CurioPlugin.h"
#include "../../shared/CurioViews.h"
#include "DuckEngine.h"

using namespace curio;

static std::vector<std::string> noteNames() {
    std::vector<std::string> v { "Any note" };
    const char* n[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    for (int i = 0; i < 128; ++i) v.push_back(std::string(n[i % 12]) + std::to_string(i / 12 - 2) + " (" + std::to_string(i) + ")");
    return v;
}

class DuckProcessor : public CurioProcessor {
public:
    DuckProcessor() : CurioProcessor(
        BusesProperties().withInput("In", juce::AudioChannelSet::stereo()).withOutput("Out", juce::AudioChannelSet::stereo()),
        {
            C("aNote", "Trigger", noteNames(), 37, "Lane A (kick)"),
            P("aDepth", "Depth", 0, 100, 100, "%", "Lane A (kick)"),
            P("aAttack", "Attack", 0, 50, 2, "ms", "Lane A (kick)", 5),
            P("aHold", "Hold", 0, 200, 10, "ms", "Lane A (kick)", 30),
            P("aRelease", "Release", 5, 1000, 180, "ms", "Lane A (kick)", 150),
            P("aTension", "Curve", -100, 100, 50, "%", "Lane A (kick)"),
            P("aVel", "Vel Sens", 0, 100, 30, "%", "Lane A (kick)"),
            C("bNote", "Trigger", noteNames(), 39, "Lane B (snare)"),
            P("bDepth", "Depth", 0, 100, 50, "%", "Lane B (snare)"),
            P("bAttack", "Attack", 0, 50, 1, "ms", "Lane B (snare)", 5),
            P("bHold", "Hold", 0, 200, 0, "ms", "Lane B (snare)", 30),
            P("bRelease", "Release", 5, 1000, 120, "ms", "Lane B (snare)", 150),
            P("bTension", "Curve", -100, 100, 20, "%", "Lane B (snare)"),
            P("bVel", "Vel Sens", 0, 100, 30, "%", "Lane B (snare)"),
            C("combine", "Combine", { "Deepest", "Multiply" }, 0, "Output"),
            C("band", "Band", { "Full", "Lows only", "Highs only" }, 0, "Output"),
            P("crossover", "Crossover", 40, 2000, 150, "Hz", "Output", 200),
            P("lookahead", "Lookahead", 0, 10, 3, "ms", "Output"),
            P("smoothing", "Smoothing", 0, 5, 0.5f, "ms", "Output"),
            P("mix", "Mix", 0, 100, 100, "%", "Output"),
        },
        {
            { "Kick + Snare Pump", {} },
            { "Hard Kick Duck", { { "aDepth", 100 }, { "aRelease", 260 }, { "aTension", 80 }, { "bDepth", 0 } } },
            { "Gentle Glue", { { "aDepth", 35 }, { "aRelease", 140 }, { "bDepth", 20 } } },
            { "Bass Only Pump", { { "band", 1 }, { "crossover", 180 }, { "aDepth", 100 }, { "aRelease", 220 } } },
            { "Gated Pad Chop", { { "aNote", 0 }, { "aAttack", 0 }, { "aHold", 60 }, { "aRelease", 40 }, { "aTension", -60 }, { "bDepth", 0 } } },
            { "Swell Back", { { "aRelease", 600 }, { "aTension", 100 }, { "bDepth", 0 } } },
            { "Snap Return", { { "aRelease", 90 }, { "aTension", -80 }, { "aHold", 20 } } },
            { "Highs Breathe", { { "band", 2 }, { "crossover", 900 }, { "aDepth", 70 } } },
            { "Stacked Multiply", { { "combine", 1 }, { "aDepth", 70 }, { "bDepth", 70 } } },
            { "No Lookahead", { { "lookahead", 0 } } },
        },
        { "Duck", "MIDI-triggered sidechain: draw the pump, fire it from your drum MIDI", juce::Colour(0xffe6b25a) }) {
        notes.reserve(512);
    }

    duck::Params params() {
        duck::Params p;
        for (int l = 0; l < 2; ++l) {
            std::string k = l ? "b" : "a";
            p.lane[l].note = vi(k + "Note") - 1; p.lane[l].depth = v(k + "Depth") * 0.01f;
            p.lane[l].attack = v(k + "Attack"); p.lane[l].hold = v(k + "Hold"); p.lane[l].release = v(k + "Release");
            p.lane[l].tension = v(k + "Tension") * 0.01f; p.lane[l].velSens = v(k + "Vel") * 0.01f;
        }
        p.combine = (duck::Combine)vi("combine"); p.band = (duck::Band)vi("band"); p.crossover = v("crossover");
        p.lookahead = v("lookahead"); p.smoothing = v("smoothing"); p.mix = v("mix") * 0.01f;
        return p;
    }
    void prepareToPlay(double sr, int) override { eng.prepare((float)sr); setLatencySamples(eng.latencySamples(params())); }
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& buf, juce::MidiBuffer& midi) override {
        juce::ScopedNoDenormals nd;
        auto p = params();
        int lat = eng.latencySamples(p); if (lat != getLatencySamples()) setLatencySamples(lat);
        collectNotes(midi, notes);
        for (auto& n : notes) if (n.on) lastHit.store(n.note);
        eng.process(buf.getArrayOfWritePointers(), buf.getNumChannels(), buf.getNumSamples(), p, notes);
        meter.store(eng.meter());
    }

    std::atomic<float> meter { 1 }; std::atomic<int> lastHit { -1 };

    struct CurveView : LiveView {
        DuckProcessor& pr;
        explicit CurveView(DuckProcessor& p) : LiveView(p.info().accent), pr(p) {}
        void paint(juce::Graphics& g) override {
            paintPanel(g);
            auto p = pr.params();
            auto r = getLocalBounds().reduced(14, 12).toFloat();
            auto meterR = r.removeFromRight(18); r.removeFromRight(12);
            float maxMs = 0; for (auto& l : p.lane) maxMs = std::max(maxMs, l.attack + l.hold + l.release);
            maxMs = std::max(100.0f, maxMs * 1.1f);
            g.setColour(juce::Colour(0xff23252b));
            for (int k = 1; k < 4; ++k) g.drawHorizontalLine((int)(r.getY() + r.getHeight() * k / 4), r.getX(), r.getRight());
            const juce::Colour cols[2] = { accent, accent.withRotatedHue(0.45f) };
            for (int l = 1; l >= 0; --l) {
                juce::Path path;
                for (int x = 0; x <= (int)r.getWidth(); x += 2) {
                    float t = maxMs * (float)x / r.getWidth();
                    float gval = duck::curveGain(p.lane[l], t, 1.0f);
                    float y = r.getY() + (1.0f - gval) * r.getHeight();
                    if (x == 0) path.startNewSubPath(r.getX(), y); else path.lineTo(r.getX() + (float)x, y);
                }
                g.setColour(cols[l]); g.strokePath(path, juce::PathStrokeType(2.0f));
                g.setFont(juce::FontOptions(12.0f));
                g.drawText(l ? "B" : "A", (int)r.getRight() - 30, (int)r.getY() + 2 + l * 16, 24, 14, juce::Justification::centredRight);
            }
            g.setColour(juce::Colours::white.withAlpha(0.4f)); g.setFont(juce::FontOptions(11.0f));
            g.drawText("0 ms", r.toNearestInt().removeFromBottom(14), juce::Justification::bottomLeft);
            g.drawText(juce::String((int)maxMs) + " ms", r.toNearestInt().removeFromBottom(14), juce::Justification::bottomRight);
            float m = pr.meter.load();
            g.setColour(juce::Colour(0xff23252b)); g.fillRoundedRectangle(meterR, 3);
            g.setColour(accent); g.fillRoundedRectangle(meterR.withTop(meterR.getY() + meterR.getHeight() * (1.0f - m)), 3);
        }
    };
    std::unique_ptr<juce::Component> createCustomView() override { return std::make_unique<CurveView>(*this); }
    int customViewHeight() const override { return 150; }
private:
    duck::Engine eng; NoteEvents notes;
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DuckProcessor(); }
