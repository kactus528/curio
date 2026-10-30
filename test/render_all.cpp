// Offline test: run every Curio engine on a synthetic drum + chord loop (or MIDI for the
// synth), write WAVs, and fail on NaN/Inf, silence, or runaway levels.
#include "../plugins/Jitter/JitterEngine.h"
#include "../plugins/Repitch/RepitchEngine.h"
#include "../plugins/Duck/DuckEngine.h"
#include "../plugins/KeytrackRes/KeytrackEngine.h"
#include "../plugins/CombPluck/CombPluckEngine.h"
#include "../plugins/Manifest/ManifestEngine.h"
#include "../plugins/Mudpie/MudpieEngine.h"
#include "../plugins/SpectralOTT/SpectralOTTEngine.h"
#include <cstdio>
#include <functional>
#include <string>

using namespace curio;
static const int SR = 48000, SECS = 8, N = SR * SECS, BLOCK = 256;
static const float BPM = 120.0f;

static void writeWav(const std::string& path, const std::vector<float>& L, const std::vector<float>& R) {
    FILE* f = std::fopen(path.c_str(), "wb"); if (!f) return;
    const int n = (int)L.size(); const uint32_t dataBytes = (uint32_t)n * 4;
    auto w32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); }; auto w16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f); w32(36 + dataBytes); std::fwrite("WAVEfmt ", 1, 8, f);
    w32(16); w16(1); w16(2); w32(SR); w32(SR * 4); w16(4); w16(16); std::fwrite("data", 1, 4, f); w32(dataBytes);
    for (int i = 0; i < n; ++i) { int16_t a = (int16_t)clampf(L[(size_t)i] * 32767.0f, -32768, 32767), b = (int16_t)clampf(R[(size_t)i] * 32767.0f, -32768, 32767); std::fwrite(&a, 2, 1, f); std::fwrite(&b, 2, 1, f); }
    std::fclose(f);
}

static void makeLoop(std::vector<float>& L, std::vector<float>& R) {
    L.assign(N, 0); R.assign(N, 0); Rng rng; const float beat = SR * 60.0f / BPM;
    const float chord[4] = { 220.0f, 261.63f, 329.63f, 392.0f };
    for (int i = 0; i < N; ++i) {
        float t = (float)i / SR, pb = std::fmod((float)i, beat) / SR, p8 = std::fmod((float)i, beat * 0.5f) / SR;
        float kick = std::sin(kTwoPi * (50 + 120 * std::exp(-pb * 30)) * pb) * std::exp(-pb * 8);
        float hat = rng.bi() * std::exp(-p8 * 60) * 0.2f;
        float snare = ((int)(i / beat) % 2) ? rng.bi() * std::exp(-pb * 18) * 0.35f : 0.0f;
        float pad = 0; for (float fq : chord) pad += std::sin(kTwoPi * fq * t) * 0.06f;
        L[(size_t)i] = 0.55f * kick + hat + snare + pad; R[(size_t)i] = 0.55f * kick + 0.8f * hat + snare + pad;
    }
}

// MIDI: kick note 36 every beat, snare 38 on 2 & 4, arp notes on 1/8s
static NoteEvents eventsFor(int start, int n, bool arp) {
    NoteEvents ev; const int beat = (int)(SR * 60.0f / BPM), eighth = beat / 2;
    const int arpNotes[8] = { 48, 55, 60, 63, 67, 60, 55, 51 };
    for (int i = start; i < start + n; ++i) {
        if (i % beat == 0) ev.push_back({ i - start, 36, 1.0f, true });
        if (i % (beat * 2) == beat) ev.push_back({ i - start, 38, 0.8f, true });
        if (arp && i % eighth == 0) ev.push_back({ i - start, arpNotes[(i / eighth) % 8], 0.9f, true });
    }
    std::stable_sort(ev.begin(), ev.end(), [](auto& a, auto& b) { return a.offset < b.offset; });
    return ev;
}

static int failures = 0;
static void check(const std::string& name, const std::vector<float>& L, const std::vector<float>& R, float minRms, float maxPeak) {
    int bad = 0; double sum = 0; float pk = 0;
    for (size_t i = 0; i < L.size(); ++i) {
        if (!std::isfinite(L[i]) || !std::isfinite(R[i])) { ++bad; continue; }
        sum += L[i] * L[i] + R[i] * R[i]; pk = std::max(pk, std::max(std::abs(L[i]), std::abs(R[i])));
    }
    float rms = (float)std::sqrt(sum / (2.0 * (double)L.size()));
    bool ok = bad == 0 && rms > minRms && pk < maxPeak;
    if (!ok) ++failures;
    std::printf("%-14s %s  rms %.3f (%.1f dB)  peak %.3f  nan %d\n", name.c_str(), ok ? "OK  " : "FAIL", rms, gainToDb(rms), pk, bad);
    writeWav("out_" + name + ".wav", L, R);
}

template <typename F>
static void run(std::vector<float>& L, std::vector<float>& R, F&& block) {
    float* ch[2];
    for (int s = 0; s < N; s += BLOCK) { int m = std::min(BLOCK, N - s); ch[0] = L.data() + s; ch[1] = R.data() + s; block(ch, m, s); }
}

int main() {
    std::vector<float> dryL, dryR; makeLoop(dryL, dryR);
    check("dry", dryL, dryR, 0.01f, 2.0f);

    { auto L = dryL, R = dryR; jitter::Engine e; e.prepare(SR); jitter::Params p; p.amount = 2; p.age = 0.5f;
      run(L, R, [&](float** ch, int m, int) { e.process(ch, 2, m, p); }); check("jitter", L, R, 0.05f, 2.0f); }

    { auto L = dryL, R = dryR; repitch::Engine e; e.prepare(SR); repitch::Params p; p.bpm = BPM; p.feedback = 0.7f; p.lfoDepth = 0.3f;
      run(L, R, [&](float** ch, int m, int s) { p.swoop = s > N / 2 ? -1.0f : 0.0f; e.process(ch, 2, m, p); }); check("repitch", L, R, 0.05f, 4.0f); }

    { auto L = dryL, R = dryR; duck::Engine e; e.prepare(SR); duck::Params p; p.lane[1].note = 38; p.lane[1].depth = 0.6f; p.band = duck::Band::LowOnly;
      run(L, R, [&](float** ch, int m, int s) { e.process(ch, 2, m, p, eventsFor(s, m, false)); }); check("duck", L, R, 0.03f, 2.0f); }

    { auto L = dryL, R = dryR; keytrack::Engine e; e.prepare(SR); keytrack::Params p; p.bpm = BPM;
      run(L, R, [&](float** ch, int m, int s) { e.process(ch, 2, m, p, eventsFor(s, m, true)); }); check("keytrack", L, R, 0.02f, 2.0f);
      auto L2 = dryL, R2 = dryR; keytrack::Engine e2; e2.prepare(SR); p.seqOn = true; p.type = keytrack::Type::Comb;
      run(L2, R2, [&](float** ch, int m, int) { e2.process(ch, 2, m, p, {}); }); check("keytrack_comb", L2, R2, 0.02f, 2.0f); }

    for (int mode = 0; mode < 2; ++mode) {
        std::vector<float> L(N, 0.0f), R(N, 0.0f); combpluck::Engine e; e.prepare(SR); combpluck::Params p;
        p.blend = mode ? 0.8f : 0.0f; p.modal = combpluck::Modal::Bell; p.exciter = mode ? combpluck::Exciter::Bubble : combpluck::Exciter::White;
        run(L, R, [&](float** ch, int m, int s) { auto ev = eventsFor(s, m, true); ev.erase(std::remove_if(ev.begin(), ev.end(), [](auto& x) { return x.note < 40; }), ev.end()); e.process(ch, 2, m, p, ev); });
        check(mode ? "combpluck_modal" : "combpluck", L, R, 0.02f, 1.01f);
    }

    { auto L = dryL, R = dryR; manifest::Engine e; e.prepare(SR); manifest::Params p; p.bpm = BPM; p.density = 0.5f; p.playing = true;
      int fired = 0, last = -1;
      run(L, R, [&](float** ch, int m, int s) { p.ppq = s / (SR * 60.0 / BPM); e.process(ch, 2, m, p); if (e.currentEvent() >= 0 && e.currentEvent() != last) ++fired; last = e.currentEvent(); });
      check("manifest", L, R, 0.03f, 2.5f); std::printf("               events fired: %d\n", fired); if (fired < 5) ++failures; }

    { auto L = dryL, R = dryR; mudpie::Engine e; e.prepare(SR); mudpie::Params p; p.bpm = BPM; p.chaos = 0.6f; p.space = 0.5f; p.crunch = 0.5f; p.motion = 0.5f;
      p.sliceMode = mudpie::SliceMode::Transient;
      run(L, R, [&](float** ch, int m, int s) {
          if (s >= SR * 4 && s - BLOCK < SR * 4) e.grabRequest.store(true);
          NoteEvents ev; if (s > SR * 5 && (s / BLOCK) % 20 == 0) ev.push_back({ 0, 36 + (s / BLOCK / 20) % 16, 1.0f, true });
          e.process(ch, 2, m, p, ev); });
      check("mudpie", L, R, 0.03f, 4.0f);
      auto& c = e.capture(); std::printf("               capture %d samples, gen %d, slices:", c.length, e.generation.load());
      for (int k = 0; k <= mudpie::kPads; ++k) std::printf(" %d", c.sliceStart[k] / 480);
      std::printf(" (x10ms)\n");
      if (c.length < SR) ++failures; }

    { auto L = dryL, R = dryR; spectralott::Engine e; e.prepare(SR); spectralott::Params p; p.depth = 0.0f; p.mix = 1.0f;
      run(L, R, [&](float** ch, int m, int) { e.process(ch, 2, m, p); });
      // with depth 0 the output must equal the input delayed by N
      double err = 0; int lat = e.latencySamples();
      for (int i = lat + 4096; i < N; ++i) err = std::max(err, (double)std::abs(L[(size_t)i] - dryL[(size_t)(i - lat)]));
      std::printf("spectral null  max err vs delayed input: %.5f\n", err); if (err > 0.01) ++failures;
      auto L2 = dryL, R2 = dryR; spectralott::Engine e2; e2.prepare(SR); p.depth = 1.0f;
      run(L2, R2, [&](float** ch, int m, int) { e2.process(ch, 2, m, p); }); check("spectralott", L2, R2, 0.03f, 2.5f); }

    std::printf("\n%s (%d failures)\n", failures ? "SOME TESTS FAILED" : "ALL PASSED", failures);
    return failures ? 1 : 0;
}
