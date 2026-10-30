# Curio

Eight VST3 plugins built from techniques Mr. Bill teaches in his tutorials and interviews. Windows 64-bit for now.

| Plugin | Type | What it does | The tip behind it |
| --- | --- | --- | --- |
| **Jitter** | Effect | Tiny, fast random wobble on pitch, tone and pan so digital synths feel analog | ~1% sample-and-hold at ~60 Hz |
| **Repitch** | Effect | Tempo-synced delay; changing the time bends pitch like Doppler | "Delay Time Tricks" maths + Doppler tutorial |
| **Duck** | Effect (MIDI in) | Sidechain pumping fired by MIDI notes, with two drawn curves | Gatekeeper fed with doubled kick/snare MIDI |
| **Keytrack Res** | Effect (MIDI in) | A resonant filter your MIDI notes play and "ping" | Arp MIDI routed into Filterverse cutoff |
| **Comb Pluck** | Instrument | Noise bursts into a maxed comb / modal bank for plucks, bells, bars | Noise burst + comb filter at full resonance |
| **Manifest** | Effect | Random glitch events that stay locked to the grid | "Glitch Manifesting Device" |
| **Mudpie** | Effect (MIDI in) | FX jam box that always records; grab the last bars and play slices from pads | Infiltrator + Rolling Sampler "mudpie" |
| **Spectral OTT** | Effect | OTT-style up/down compression on every frequency | "A better OTT, one per frequency" |

## Install (Windows)

1. Download `Curio-Windows-VST3.zip` from the [latest Windows release](../../releases/tag/windows-latest).
2. Unzip and copy the eight `.vst3` folders into `C:\Program Files\Common Files\VST3`.
3. In Ableton: Options > Preferences > Plug-Ins, make sure "Use VST3 Plug-In System Folders" is on, then **Rescan**.
4. They show up under Plug-Ins > Andrew.

## Using the MIDI-driven effects in Ableton

Duck, Keytrack Res and Mudpie are audio effects that also listen to MIDI.

1. Put the plugin on the audio track (or a group/return).
2. Make a separate MIDI track with the notes you want to send.
3. On that MIDI track, set **MIDI To** to the audio track, and in the second box pick the plugin.
4. Set the MIDI track's monitor to **In** (or arm it) so the notes pass through.

- **Duck:** copy your kick MIDI (C1) and snare MIDI (D1) clips onto the MIDI track. Lane A ducks on C1, lane B on D1. Put Duck on a group holding everything that should pump.
- **Keytrack Res:** send it an arp or melody; the filter follows each note. No MIDI handy? Turn on **Step Seq** and it plays its own 8-step pattern.
- **Mudpie:** pads are C1 to D#2 (16 notes from MIDI 36). You can also click pads with the mouse.

## Quick notes per plugin

- **Jitter:** start with the default (1% at 60 Hz). **Age** pushes everything further and slower. Adds 12 ms latency (reported to the host, so it stays in time).
- **Repitch:** pick a note value, then automate **Division**, **Swoop** or the LFO; in Repitch mode every time change slides the pitch. **Fade** crossfades instead, **Jump** switches hard (clicky on purpose). The readout shows the current time as ms, Hz and beats.
- **Duck:** the graph shows both curves. **Curve** bends the release (positive = slow then fast). **Lookahead** starts the duck slightly before the hit. **Band** lets you pump only the lows or highs.
- **Comb Pluck:** play it like any synth. **Comb > Modal** blends the string-like comb into a bell/bar/glass bank. **Punch** is a quick pitch drop at the start of each note.
- **Manifest:** **Density** is the chance of an event on each step. Turn **Lock Pattern** on to repeat a 1/2/4-bar pattern; **Variation** lets it slowly mutate. Change **Seed** for a different pattern.
- **Mudpie:** it is always recording its own output (last 60 s). Play, sweep the macros, then hit **GRAB** to keep the last 1 to 8 bars. Slices land on the 16 pads. Drag the waveform (whole grab) or a pad (one slice) straight into Ableton, or **Save WAV** to Music\Mudpie.
- **Spectral OTT:** **Depth** is the main knob, like OTT. **Down <> Up** chooses between taming loud bins and lifting quiet ones. Adds about 43 ms latency (reported to the host).

## Building

```
cmake -B build -A x64 -DCURIO_STANDALONE=OFF
cmake --build build --config Release
```

`test/render_all.cpp` renders a test loop through every engine and fails on NaNs, silence or runaway levels (`curio_render_test`). GitHub Actions runs it and builds all plugins on every push.
