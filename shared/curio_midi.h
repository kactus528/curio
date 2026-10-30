#pragma once
#include <vector>
namespace curio {
// Plain MIDI note event, decoupled from JUCE so engines can be tested offline.
struct NoteEvent { int offset = 0; int note = 60; float vel = 1.0f; bool on = true; };
using NoteEvents = std::vector<NoteEvent>;
}
