// Smoke host: loads each built .vst3 the way a DAW would, runs audio + MIDI through
// every preset, opens and closes the editor, and fails on crashes, NaNs or silence.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <cstdio>
#if JUCE_WINDOWS
 #define NOMINMAX
 #define WIN32_LEAN_AND_MEAN
 #include <windows.h>
static LONG WINAPI crashFilter(EXCEPTION_POINTERS* e) {
    std::printf("CRASH  exception 0x%08lx at %p\n", e->ExceptionRecord->ExceptionCode, e->ExceptionRecord->ExceptionAddress);
    std::fflush(stdout);
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

static void step(const char* what, const juce::String& name) { std::printf("  .. %s: %s\n", name.toRawUTF8(), what); std::fflush(stdout); }

static int testPlugin(juce::VST3PluginFormat& fmt, const juce::String& path) {
    const auto shortName = juce::File(path).getFileNameWithoutExtension();
    step("scanning", shortName);
    if (!juce::File(path).exists()) { std::printf("FAIL  %s: file missing\n", path.toRawUTF8()); return 1; }
    juce::OwnedArray<juce::PluginDescription> types;
    fmt.findAllTypesForFile(types, path);
    if (types.isEmpty()) { std::printf("FAIL  %s: no plugin found in file\n", path.toRawUTF8()); return 1; }
    const auto& desc = *types[0];
    juce::String err;
    step("instantiating", shortName);
    auto inst = fmt.createInstanceFromDescription(desc, 48000.0, 512, err);
    if (!inst) { std::printf("FAIL  %s: could not instantiate (%s)\n", desc.name.toRawUTF8(), err.toRawUTF8()); return 1; }

    step("preparing", shortName);
    inst->enableAllBuses();
    const int ins = inst->getTotalNumInputChannels(), outs = inst->getTotalNumOutputChannels();
    inst->setPlayConfigDetails(ins, outs, 48000.0, 512);
    inst->prepareToPlay(48000.0, 512);
    const int chans = std::max({ ins, outs, 2 });
    juce::AudioBuffer<float> buf(chans, 512);
    juce::Random rng(7);
    int bad = 0; double worstRms = 1e9, bestRms = 0;
    const int programs = std::max(1, inst->getNumPrograms());
    long sample = 0;
    for (int prog = 0; prog < programs; ++prog) {
        if (prog == 0) step("processing presets", shortName);
        inst->setCurrentProgram(prog);
        double sum = 0; long count = 0;
        for (int b = 0; b < 200; ++b) {   // ~2.1 s per preset
            buf.clear();
            for (int i = 0; i < 512; ++i, ++sample) {
                float t = (float)sample / 48000.0f, beat = std::fmod(t, 0.5f);
                float x = 0.3f * std::sin(juce::MathConstants<float>::twoPi * 220.0f * t)
                        + 0.5f * std::sin(juce::MathConstants<float>::twoPi * 55.0f * beat) * std::exp(-beat * 10.0f)
                        + 0.1f * (rng.nextFloat() * 2 - 1);
                for (int c = 0; c < ins; ++c) buf.setSample(c, i, x);
            }
            juce::MidiBuffer midi;
            if (b % 12 == 0) {
                midi.addEvent(juce::MidiMessage::noteOn(1, 36, (juce::uint8)110), 0);
                midi.addEvent(juce::MidiMessage::noteOn(1, 48 + (b / 12) % 12, (juce::uint8)100), 64);
            }
            if (b % 12 == 6) { midi.addEvent(juce::MidiMessage::noteOff(1, 36), 0); midi.addEvent(juce::MidiMessage::noteOff(1, 48 + (b / 12) % 12), 0); }
            inst->processBlock(buf, midi);
            for (int c = 0; c < outs; ++c)
                for (int i = 0; i < 512; ++i) {
                    float y = buf.getSample(c, i);
                    if (!std::isfinite(y) || std::abs(y) > 8.0f) ++bad; else { sum += (double)y * y; ++count; }
                }
        }
        double rms = std::sqrt(sum / std::max(1L, count));
        worstRms = std::min(worstRms, rms); bestRms = std::max(bestRms, rms);
        if (rms < 1.0e-4) std::printf("      silent preset %d (%s)\n", prog, inst->getProgramName(prog).toRawUTF8());
    }
    bool editorOk = true;
    step("opening editor", shortName);
    if (inst->hasEditor()) {
        std::unique_ptr<juce::AudioProcessorEditor> ed(inst->createEditor());
        editorOk = ed != nullptr && ed->getWidth() > 100 && ed->getHeight() > 100;
        if (ed) std::printf("      editor %d x %d\n", ed->getWidth(), ed->getHeight());
    }
    step("release + state round-trip", shortName);
    inst->releaseResources();
    // state round-trip
    juce::MemoryBlock state; inst->getStateInformation(state); inst->setStateInformation(state.getData(), (int)state.getSize());

    const bool ok = bad == 0 && worstRms > 1.0e-4 && editorOk;
    std::fflush(stdout);
    std::printf("%s  %-12s presets %d  latency %d  rms min %.4f max %.4f  bad samples %d  editor %s\n",
        ok ? "PASS" : "FAIL", desc.name.toRawUTF8(), programs, inst->getLatencySamples(), worstRms, bestRms, bad, editorOk ? "ok" : "BROKEN");
    return ok ? 0 : 1;
}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("smoke host: %d plugins\n", argc - 1);
   #if JUCE_WINDOWS
    SetUnhandledExceptionFilter(crashFilter);
   #endif
    std::printf("  init juce\n");
    juce::ScopedJuceInitialiser_GUI init;
    std::printf("  create vst3 format\n");
    juce::VST3PluginFormat fmt;
    std::printf("  ready\n");
    int failures = 0;
    for (int i = 1; i < argc; ++i) {
        std::printf("  arg %d: %s\n", i, argv[i]);
        auto path = juce::File::getCurrentWorkingDirectory().getChildFile(juce::String(argv[i])).getFullPathName();
        failures += testPlugin(fmt, path);
    }
    std::printf("\n%d of %d plugins failed\n", failures, argc - 1);
    return failures ? 1 : 0;
}
