#include "../../shared/CurioPlugin.h"
#include "../../shared/CurioViews.h"
#include "KeytrackEngine.h"

using namespace curio;

class KeytrackProcessor : public CurioProcessor {
public:
    KeytrackProcessor() : CurioProcessor(
        BusesProperties().withInput("In", juce::AudioChannelSet::stereo()).withOutput("Out", juce::AudioChannelSet::stereo()),
        {
            C("type", "Type", { "Low-pass", "Band-pass", "High-pass", "Comb" }, 1, "Filter"),
            P("base", "Base Cutoff", 40, 16000, 800, "Hz", "Filter", 800),
            P("resonance", "Resonance", 0, 100, 85, "%", "Filter"),
            P("drive", "Drive", 0, 100, 20, "%", "Filter"),
            P("keyOffset", "Key Offset", -24, 36, 12, "st", "Keys", 0, 1),
            P("keytrack", "Key Track", 0, 100, 100, "%", "Keys"),
            P("glide", "Glide", 0, 500, 15, "ms", "Keys", 60),
            P("envAmount", "Ping", 0, 48, 24, "st", "Ping"),
            P("envDecay", "Ping Decay", 2, 1000, 60, "ms", "Ping", 100),
            P("lrOffset", "L/R Offset", 0, 30, 8, "%", "Ping"),
            B("seqOn", "Step Seq (no MIDI)", false, "Fallback sequencer"),
            C("seqDiv", "Rate", divisionNames(), 5, "Fallback sequencer"),
            P("seqRoot", "Root", 24, 84, 48, "note", "Fallback sequencer", 0, 1),
            P("s1", "Step 1", -24, 24, 0, "st", "Steps", 0, 1), P("s2", "Step 2", -24, 24, 12, "st", "Steps", 0, 1),
            P("s3", "Step 3", -24, 24, 7, "st", "Steps", 0, 1), P("s4", "Step 4", -24, 24, 3, "st", "Steps", 0, 1),
            P("s5", "Step 5", -24, 24, 0, "st", "Steps", 0, 1), P("s6", "Step 6", -24, 24, 10, "st", "Steps", 0, 1),
            P("s7", "Step 7", -24, 24, 7, "st", "Steps", 0, 1), P("s8", "Step 8", -24, 24, 15, "st", "Steps", 0, 1),
            P("mix", "Mix", 0, 100, 100, "%", "Output"),
        },
        {
            { "Resonant Arp Peaks", {} },
            { "Liquid Bandpass", { { "envAmount", 12 }, { "envDecay", 140 }, { "glide", 60 }, { "resonance", 92 } } },
            { "Stabby LP", { { "type", 0 }, { "keyOffset", 24 }, { "envAmount", 36 }, { "envDecay", 35 }, { "resonance", 70 } } },
            { "Tuned Comb Drums", { { "type", 3 }, { "keyOffset", 0 }, { "resonance", 90 }, { "envAmount", 0 } } },
            { "Noise to Melody", { { "resonance", 98 }, { "envAmount", 5 }, { "keytrack", 100 }, { "drive", 40 } } },
            { "Wide Ping", { { "lrOffset", 25 }, { "envAmount", 30 }, { "envDecay", 90 } } },
            { "Seq: Minor Run", { { "seqOn", 1 }, { "seqDiv", 5 } } },
            { "Seq: Comb Octaves", { { "seqOn", 1 }, { "type", 3 }, { "seqDiv", 8 }, { "s2", 12 }, { "s3", 0 }, { "s4", 24 }, { "s5", 12 }, { "s6", 0 }, { "s7", 19 }, { "s8", 7 } } },
            { "Gentle Keytrack", { { "type", 0 }, { "keytrack", 50 }, { "resonance", 40 }, { "envAmount", 6 } } },
            { "Screamer", { { "resonance", 100 }, { "drive", 85 }, { "envAmount", 40 }, { "envDecay", 25 } } },
        },
        { "Keytrack Res", "A resonant filter your MIDI plays: every note retunes and pings it", juce::Colour(0xffb57be0) }) {
        notes.reserve(512);
    }

    void prepareToPlay(double sr, int) override { eng.prepare((float)sr); }
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& buf, juce::MidiBuffer& midi) override {
        juce::ScopedNoDenormals nd;
        keytrack::Params p;
        p.type = (keytrack::Type)vi("type"); p.baseCutoff = v("base"); p.resonance = v("resonance") * 0.01f; p.drive = v("drive") * 0.01f;
        p.keyOffset = v("keyOffset"); p.keytrack = v("keytrack") * 0.01f; p.glide = v("glide");
        p.envAmount = v("envAmount"); p.envDecay = v("envDecay"); p.lrOffset = v("lrOffset") * 0.01f; p.mix = v("mix") * 0.01f;
        p.seqOn = vb("seqOn"); p.seqDiv = vi("seqDiv"); p.seqRoot = v("seqRoot");
        const char* ids[8] = { "s1", "s2", "s3", "s4", "s5", "s6", "s7", "s8" };
        for (int i = 0; i < 8; ++i) p.seqSteps[i] = v(ids[i]);
        auto t = transport(); p.bpm = (float)t.bpm; p.ppq = t.ppq; p.playing = t.playing;
        collectNotes(midi, notes);
        eng.process(buf.getArrayOfWritePointers(), buf.getNumChannels(), buf.getNumSamples(), p, notes);
    }
private:
    keytrack::Engine eng; NoteEvents notes;
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new KeytrackProcessor(); }
