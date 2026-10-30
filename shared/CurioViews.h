// Small shared UI pieces for custom views.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "curio_dsp.h"
#include <string>
#include <vector>

namespace curio {

inline std::vector<std::string> divisionNames() {
    std::vector<std::string> v; for (auto& d : divisions()) v.push_back(d.name); return v;
}

// A dark rounded panel that repaints on a timer.
class LiveView : public juce::Component, private juce::Timer {
public:
    explicit LiveView(juce::Colour a, int hz = 30) : accent(a) { startTimerHz(hz); }
    ~LiveView() override { stopTimer(); }
protected:
    juce::Colour accent;
    void paintPanel(juce::Graphics& g) {
        g.setColour(juce::Colour(0xff101115)); g.fillRoundedRectangle(getLocalBounds().toFloat(), 6);
        g.setColour(juce::Colour(0xff2a2d34)); g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 6, 1);
    }
private:
    void timerCallback() override { tick(); repaint(); }
    virtual void tick() {}
};

} // namespace curio
