#include "../../shared/CurioPlugin.h"
#include "../../shared/CurioViews.h"
#include "MudpieEngine.h"

using namespace curio;

class MudpieProcessor : public CurioProcessor {
public:
    MudpieProcessor() : CurioProcessor(
        BusesProperties().withInput("In", juce::AudioChannelSet::stereo()).withOutput("Out", juce::AudioChannelSet::stereo()),
        {
            P("chaos", "Chaos", 0, 100, 20, "%", "Macros"),
            P("tone", "Tone", 0, 100, 50, "%", "Macros"),
            P("space", "Space", 0, 100, 20, "%", "Macros"),
            P("crunch", "Crunch", 0, 100, 20, "%", "Macros"),
            P("motion", "Motion", 0, 100, 0, "%", "Macros"),
            C("motionDiv", "Motion Rate", divisionNames(), 11, "Macros"),
            P("mix", "FX Mix", 0, 100, 100, "%", "Macros"),
            C("grabBars", "Grab", { "1 bar", "2 bars", "4 bars", "8 bars" }, 1, "Capture"),
            C("sliceMode", "Slicing", { "Grid", "Transient" }, 0, "Capture"),
            P("sensitivity", "Sensitivity", 0, 100, 50, "%", "Capture"),
            P("padPitch", "Pad Pitch", -24, 24, 0, "st", "Pads", 0, 1),
            B("padReverse", "Reverse", false, "Pads"),
            P("padFade", "Fade", 0, 50, 3, "ms", "Pads", 8),
            B("clicky", "Clicky", false, "Pads"),
            P("padGain", "Pad Gain", -24, 12, 0, "dB", "Pads"),
        },
        {
            { "Clean Recorder", { { "chaos", 0 }, { "space", 0 }, { "crunch", 0 }, { "mix", 0 } } },
            { "Mudpie Starter", {} },
            { "Crushed Haze", { { "chaos", 65 }, { "space", 55 }, { "crunch", 35 }, { "tone", 40 } } },
            { "Shifty Delay", { { "chaos", 45 }, { "space", 75 }, { "motion", 30 } } },
            { "Filter Sweeper", { { "motion", 80 }, { "motionDiv", 16 }, { "chaos", 30 } } },
            { "Blown Out", { { "crunch", 90 }, { "chaos", 50 }, { "tone", 60 } } },
            { "Thin Highpass", { { "tone", 80 }, { "space", 40 } } },
            { "Transient Chops", { { "sliceMode", 1 }, { "sensitivity", 70 }, { "chaos", 30 } } },
            { "Reverse Pads", { { "padReverse", 1 }, { "padFade", 10 }, { "space", 35 } } },
            { "Octave Down Chops", { { "padPitch", -12 }, { "chaos", 40 }, { "clicky", 1 } } },
        },
        { "Mudpie", "Jam into it, grab the last bars, play the best bits from pads", juce::Colour(0xffc98a4b), 820 }) {
        notes.reserve(512);
    }

    void prepareToPlay(double sr, int) override { eng.prepare((float)sr); sampleRate.store(sr); }
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& buf, juce::MidiBuffer& midi) override {
        juce::ScopedNoDenormals nd;
        mudpie::Params p;
        p.chaos = v("chaos") * 0.01f; p.tone = v("tone") * 0.01f; p.space = v("space") * 0.01f; p.crunch = v("crunch") * 0.01f;
        p.motion = v("motion") * 0.01f; p.motionDiv = vi("motionDiv"); p.mix = v("mix") * 0.01f;
        p.grabBars = 1 << vi("grabBars"); p.sliceMode = (mudpie::SliceMode)vi("sliceMode"); p.sensitivity = v("sensitivity") * 0.01f;
        p.padPitch = v("padPitch"); p.padReverse = vb("padReverse"); p.padFade = v("padFade"); p.clicky = vb("clicky"); p.padGain = v("padGain");
        auto t = transport(); p.bpm = (float)t.bpm; p.ppq = t.ppq; p.playing = t.playing;
        collectNotes(midi, notes);
        eng.process(buf.getArrayOfWritePointers(), buf.getNumChannels(), buf.getNumSamples(), p, notes);
    }

    mudpie::Engine eng;
    std::atomic<double> sampleRate { 48000 };

    // Copy of the current capture for the UI (display + export)
    struct Snapshot { std::vector<float> l, r; int length = 0; int slices[mudpie::kPads + 1] = {}; int gen = -1; };
    void refresh(Snapshot& s) {
        int gen = eng.generation.load();
        if (gen == s.gen) return;
        const auto& c = eng.capture();
        s.length = c.length; s.l.assign(c.l.begin(), c.l.begin() + c.length); s.r.assign(c.r.begin(), c.r.begin() + c.length);
        std::copy(std::begin(c.sliceStart), std::end(c.sliceStart), std::begin(s.slices)); s.gen = gen;
    }
    juce::File writeWav(const Snapshot& s, int from, int to, const juce::String& name, bool keep) {
        auto dir = keep ? juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile("Mudpie")
                        : juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("Mudpie");
        dir.createDirectory();
        auto file = dir.getNonexistentChildFile(name + "_" + juce::Time::getCurrentTime().formatted("%H%M%S"), ".wav");
        juce::AudioBuffer<float> b(2, std::max(1, to - from));
        for (int i = 0; i < to - from; ++i) { b.setSample(0, i, s.l[(size_t)(from + i)]); b.setSample(1, i, s.r[(size_t)(from + i)]); }
        juce::WavAudioFormat wav;
        if (auto os = std::unique_ptr<juce::FileOutputStream>(file.createOutputStream())) {
            if (auto* w = wav.createWriterFor(os.get(), sampleRate.load(), 2, 24, {}, 0)) {
                os.release();
                std::unique_ptr<juce::AudioFormatWriter> writer(w);
                writer->writeFromAudioSampleBuffer(b, 0, b.getNumSamples());
            }
        }
        return file;
    }

    struct View : LiveView {
        MudpieProcessor& pr; Snapshot snap; float flash[mudpie::kPads] = {};
        juce::TextButton grab { "GRAB" }, save { "Save WAV" };
        juce::Rectangle<int> waveArea, padArea[mudpie::kPads];
        int dragPad = -2; bool dragged = false;

        explicit View(MudpieProcessor& p) : LiveView(p.info().accent), pr(p) {
            addAndMakeVisible(grab); addAndMakeVisible(save);
            grab.setColour(juce::TextButton::buttonColourId, accent.darker(0.2f));
            grab.onClick = [this] { pr.eng.grabRequest.store(true); };
            save.onClick = [this] {
                if (snap.length <= 0) return;
                auto f = pr.writeWav(snap, 0, snap.length, "mudpie", true);
                save.setButtonText("Saved"); f.revealToUser();
            };
        }
        void tick() override {
            pr.refresh(snap);
            for (int i = 0; i < mudpie::kPads; ++i) { flash[i] *= 0.8f; if (pr.eng.padFlash[i].exchange(0)) flash[i] = 1.0f; }
        }
        void resized() override {
            auto r = getLocalBounds().reduced(10);
            auto side = r.removeFromLeft(110);
            grab.setBounds(side.removeFromTop(56)); side.removeFromTop(8); save.setBounds(side.removeFromTop(30));
            r.removeFromLeft(10);
            waveArea = r.removeFromTop(78); r.removeFromTop(8);
            int pw = r.getWidth() / 8, ph = (r.getHeight() - 6) / 2;
            for (int i = 0; i < mudpie::kPads; ++i)
                padArea[i] = juce::Rectangle<int>(r.getX() + (i % 8) * pw, r.getY() + (i / 8) * (ph + 6), pw - 6, ph);
        }
        void paint(juce::Graphics& g) override {
            paintPanel(g);
            auto w = waveArea.toFloat();
            g.setColour(juce::Colour(0xff1a1c21)); g.fillRoundedRectangle(w, 4);
            if (snap.length > 0) {
                g.setColour(accent.withAlpha(0.85f));
                const int cols = (int)w.getWidth();
                for (int x = 0; x < cols; ++x) {
                    int a = (int)((long long)snap.length * x / cols), b = (int)((long long)snap.length * (x + 1) / cols);
                    float pk = 0; for (int i = a; i < b; i += 4) pk = std::max(pk, std::abs(snap.l[(size_t)i]) + std::abs(snap.r[(size_t)i]));
                    float h = std::min(1.0f, pk * 0.5f) * w.getHeight() * 0.48f;
                    g.drawVerticalLine((int)w.getX() + x, w.getCentreY() - h, w.getCentreY() + h);
                }
                g.setColour(juce::Colours::white.withAlpha(0.35f));
                for (int k = 1; k < mudpie::kPads; ++k) g.drawVerticalLine((int)(w.getX() + w.getWidth() * snap.slices[k] / (float)snap.length), w.getY(), w.getBottom());
                g.setColour(juce::Colours::white.withAlpha(0.45f)); g.setFont(juce::FontOptions(11.0f));
                g.drawText(juce::String(snap.length / pr.sampleRate.load(), 2) + " s  -  drag the waveform out, or a pad for one slice", waveArea.reduced(6, 2), juce::Justification::bottomLeft);
            } else {
                g.setColour(juce::Colours::white.withAlpha(0.45f)); g.setFont(juce::FontOptions(13.0f));
                g.drawText("Always recording. Play and sweep the macros, then hit GRAB.", waveArea, juce::Justification::centred);
            }
            const char* nn[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            for (int i = 0; i < mudpie::kPads; ++i) {
                auto pa = padArea[i].toFloat();
                g.setColour(juce::Colour(0xff23252b).interpolatedWith(accent, flash[i])); g.fillRoundedRectangle(pa, 4);
                g.setColour(flash[i] > 0.5f ? juce::Colours::black : juce::Colours::white.withAlpha(0.7f)); g.setFont(juce::FontOptions(11.5f));
                int note = mudpie::kFirstNote + i;
                g.drawText(juce::String(i + 1) + "  " + nn[note % 12] + juce::String(note / 12 - 2), pa, juce::Justification::centred);
            }
        }
        int padAt(juce::Point<int> p) const { for (int i = 0; i < mudpie::kPads; ++i) if (padArea[i].contains(p)) return i; return waveArea.contains(p) ? -1 : -2; }
        void mouseDown(const juce::MouseEvent& e) override {
            dragPad = padAt(e.getPosition()); dragged = false;
            if (dragPad >= 0) pr.eng.previewPad.store(dragPad);
        }
        void mouseDrag(const juce::MouseEvent& e) override {
            if (dragged || dragPad < -1 || snap.length <= 0 || e.getDistanceFromDragStart() < 8) return;
            dragged = true;
            int from = dragPad >= 0 ? snap.slices[dragPad] : 0, to = dragPad >= 0 ? snap.slices[dragPad + 1] : snap.length;
            if (to - from < 16) return;
            auto f = pr.writeWav(snap, from, to, dragPad >= 0 ? "mudpie_slice" + juce::String(dragPad + 1) : juce::String("mudpie"), false);
            juce::DragAndDropContainer::performExternalDragDropOfFiles({ f.getFullPathName() }, false, this);
        }
    };
    std::unique_ptr<juce::Component> createCustomView() override { return std::make_unique<View>(*this); }
    int customViewHeight() const override { return 190; }
private:
    NoteEvents notes;
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MudpieProcessor(); }
