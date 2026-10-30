// Curio shared JUCE wrapper: parameter table -> APVTS, factory presets, and a generic
// grouped-knob editor with an optional plugin-specific view on top.
#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "curio_midi.h"

namespace curio {

// MidiBuffer -> plain note events (vector capacity reserved by the caller, so no
// allocation on the audio thread in normal use)
inline void collectNotes(const juce::MidiBuffer& midi, NoteEvents& out) {
    out.clear();
    for (const auto meta : midi) {
        const auto m = meta.getMessage();
        if (m.isNoteOn()) out.push_back({ meta.samplePosition, m.getNoteNumber(), m.getFloatVelocity(), true });
        else if (m.isNoteOff()) out.push_back({ meta.samplePosition, m.getNoteNumber(), 0.0f, false });
    }
}

struct ParamSpec {
    std::string id, name, unit, group;
    float min = 0, max = 1, def = 0, centre = 0, step = 0;   // centre != 0 -> skewed range
    std::vector<std::string> choices;                         // non-empty -> choice param
    bool isBool = false;
};

inline ParamSpec P(std::string id, std::string name, float mn, float mx, float def, std::string unit, std::string group, float centre = 0, float step = 0) {
    ParamSpec s; s.id = id; s.name = name; s.min = mn; s.max = mx; s.def = def; s.unit = unit; s.group = group; s.centre = centre; s.step = step; return s;
}
inline ParamSpec C(std::string id, std::string name, std::vector<std::string> choices, int def, std::string group) {
    ParamSpec s; s.id = id; s.name = name; s.choices = std::move(choices); s.def = (float)def; s.group = group; return s;
}
inline ParamSpec B(std::string id, std::string name, bool def, std::string group) {
    ParamSpec s; s.id = id; s.name = name; s.isBool = true; s.def = def ? 1.0f : 0.0f; s.group = group; return s;
}

struct Preset { std::string name; std::vector<std::pair<std::string, float>> values; };

struct PluginInfo { std::string title, tagline; juce::Colour accent; int width = 780; };

class CurioProcessor : public juce::AudioProcessor {
public:
    CurioProcessor(const BusesProperties& buses, std::vector<ParamSpec> specs, std::vector<Preset> presets, PluginInfo info)
        : juce::AudioProcessor(buses), specs_(std::move(specs)), presets_(std::move(presets)), info_(std::move(info)),
          apvts(*this, nullptr, "STATE", makeLayout(specs_)) {
        for (auto& s : specs_) raw_[s.id] = apvts.getRawParameterValue(s.id);
        if (!presets_.empty()) applyPreset(0);
    }

    // Fast parameter read in real units (choice -> index, bool -> 0/1)
    float v(const std::string& id) const { auto it = raw_.find(id); return it == raw_.end() ? 0.0f : it->second->load(); }
    int vi(const std::string& id) const { return (int)std::lround(v(id)); }
    bool vb(const std::string& id) const { return v(id) > 0.5f; }

    const std::vector<ParamSpec>& specs() const { return specs_; }
    const std::vector<Preset>& presets() const { return presets_; }
    const PluginInfo& info() const { return info_; }

    void applyPreset(int idx) {
        if (idx < 0 || idx >= (int)presets_.size()) return;
        current_ = idx;
        for (auto& s : specs_) setReal(s.id, s.def);
        for (auto& kv : presets_[(size_t)idx].values) setReal(kv.first, kv.second);
    }
    void setReal(const std::string& id, float value) {
        if (auto* p = apvts.getParameter(id)) p->setValueNotifyingHost(p->convertTo0to1(value));
    }

    // Plugin-specific view shown above the knobs (nullptr = none)
    virtual std::unique_ptr<juce::Component> createCustomView() { return nullptr; }
    virtual int customViewHeight() const { return 0; }

    // --- AudioProcessor boilerplate ---
    const juce::String getName() const override { return info_.title; }
    bool acceptsMidi() const override { return JucePlugin_WantsMidiInput != 0; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }
    int getNumPrograms() override { return std::max(1, (int)presets_.size()); }
    int getCurrentProgram() override { return current_; }
    void setCurrentProgram(int i) override { applyPreset(i); }
    const juce::String getProgramName(int i) override { return i < (int)presets_.size() ? juce::String(presets_[(size_t)i].name) : juce::String("Init"); }
    void changeProgramName(int, const juce::String&) override {}
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;

    bool isBusesLayoutSupported(const BusesLayout& l) const override {
        auto out = l.getMainOutputChannelSet();
        if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
        if (l.inputBuses.size() > 0 && !l.getMainInputChannelSet().isDisabled() && l.getMainInputChannelSet() != out) return false;
        return true;
    }

    void getStateInformation(juce::MemoryBlock& dest) override {
        auto state = apvts.copyState();
        state.setProperty("preset", current_, nullptr);
        saveExtraState(state);
        if (auto xml = state.createXml()) copyXmlToBinary(*xml, dest);
    }
    void setStateInformation(const void* data, int size) override {
        if (auto xml = getXmlFromBinary(data, size)) {
            auto tree = juce::ValueTree::fromXml(*xml);
            if (tree.hasType(apvts.state.getType())) {
                current_ = (int)tree.getProperty("preset", 0);
                loadExtraState(tree);
                apvts.replaceState(tree);
            }
        }
    }
    virtual void saveExtraState(juce::ValueTree&) {}
    virtual void loadExtraState(const juce::ValueTree&) {}

    // Host transport helper
    struct Transport { double bpm = 120, ppq = 0; bool playing = false; };
    Transport transport() {
        Transport t;
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition()) {
                if (auto b = pos->getBpm()) t.bpm = *b;
                if (auto q = pos->getPpqPosition()) t.ppq = *q;
                t.playing = pos->getIsPlaying();
            }
        if (!(t.bpm > 1.0)) t.bpm = 120.0;
        return t;
    }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout(const std::vector<ParamSpec>& specs) {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;
        for (auto& s : specs) {
            juce::ParameterID pid { s.id, 1 };
            if (!s.choices.empty()) {
                juce::StringArray arr; for (auto& c : s.choices) arr.add(c);
                layout.add(std::make_unique<juce::AudioParameterChoice>(pid, s.name, arr, (int)s.def));
            } else if (s.isBool) {
                layout.add(std::make_unique<juce::AudioParameterBool>(pid, s.name, s.def > 0.5f));
            } else {
                juce::NormalisableRange<float> r(s.min, s.max, s.step);
                if (s.centre > s.min && s.centre < s.max) r.setSkewForCentre(s.centre);
                auto unit = s.unit;
                auto attrs = juce::AudioParameterFloatAttributes().withLabel(unit)
                    .withStringFromValueFunction([unit](float val, int) {
                        float a = std::abs(val);
                        int dec = a >= 100 ? 0 : a >= 10 ? 1 : 2;
                        if (unit == "Hz" && a >= 1000) return juce::String(val / 1000.0f, 2) + " kHz";
                        return juce::String(val, dec) + (unit.empty() ? "" : " " + unit);
                    });
                layout.add(std::make_unique<juce::AudioParameterFloat>(pid, s.name, r, s.def, attrs));
            }
        }
        return layout;
    }

    // NOTE: declaration order matters. specs_/presets_/info_ must be constructed
    // before apvts, because apvts is built from specs_ in the constructor.
    std::vector<ParamSpec> specs_;
    std::vector<Preset> presets_;
    PluginInfo info_;
    std::map<std::string, std::atomic<float>*> raw_;
    int current_ = 0;

public:
    juce::AudioProcessorValueTreeState apvts;  // public for attachments
};

// ------------------------------------------------------------------------
class CurioLook : public juce::LookAndFeel_V4 {
public:
    juce::Colour accent;
    explicit CurioLook(juce::Colour a) : accent(a) {
        setColour(juce::ResizableWindow::backgroundColourId, juce::Colour(0xff15161a));
        setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff23252b));
        setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff34373f));
        setColour(juce::ComboBox::textColourId, juce::Colours::white.withAlpha(0.9f));
        setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff23252b));
        setColour(juce::PopupMenu::highlightedBackgroundColourId, a.withAlpha(0.5f));
        setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a2d34));
        setColour(juce::TextButton::buttonOnColourId, a);
        setColour(juce::ToggleButton::tickColourId, a);
        setColour(juce::Slider::textBoxTextColourId, juce::Colours::white.withAlpha(0.8f));
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    }
    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override {
        auto b = juce::Rectangle<float>((float)x, (float)y, (float)w, (float)h).reduced(6);
        float r = std::min(b.getWidth(), b.getHeight()) * 0.5f; auto c = b.getCentre();
        juce::Path track; track.addCentredArc(c.x, c.y, r, r, 0, a0, a1, true);
        g.setColour(juce::Colour(0xff2c2f37)); g.strokePath(track, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        float ang = a0 + pos * (a1 - a0);
        juce::Path val; val.addCentredArc(c.x, c.y, r, r, 0, a0, ang, true);
        g.setColour(accent); g.strokePath(val, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(juce::Colour(0xff23252b)); g.fillEllipse(c.x - r + 6, c.y - r + 6, (r - 6) * 2, (r - 6) * 2);
        juce::Path p; p.addRoundedRectangle(-1.5f, -(r - 8), 3.0f, r * 0.45f, 1.5f);
        g.setColour(juce::Colours::white.withAlpha(0.85f)); g.fillPath(p, juce::AffineTransform::rotation(ang).translated(c));
    }
};

// ------------------------------------------------------------------------
class CurioEditor : public juce::AudioProcessorEditor {
public:
    explicit CurioEditor(CurioProcessor& p) : juce::AudioProcessorEditor(p), proc(p), look(p.info().accent) {
        setLookAndFeel(&look);
        for (int i = 0; i < (int)p.presets().size(); ++i) presetBox.addItem(p.presets()[(size_t)i].name, i + 1);
        presetBox.setSelectedId(p.getCurrentProgram() + 1, juce::dontSendNotification);
        presetBox.onChange = [this] { proc.applyPreset(presetBox.getSelectedId() - 1); };
        addAndMakeVisible(presetBox);
        for (auto* b : { &prevBtn, &nextBtn }) addAndMakeVisible(b);
        prevBtn.onClick = [this] { step(-1); }; nextBtn.onClick = [this] { step(1); };

        custom = p.createCustomView();
        if (custom) addAndMakeVisible(*custom);

        // Build groups in first-seen order
        for (auto& s : p.specs()) {
            Group* g = nullptr;
            for (auto& gg : groups) if (gg->name == s.group) g = gg.get();
            if (!g) { groups.push_back(std::make_unique<Group>()); g = groups.back().get(); g->name = s.group; }
            auto ctl = std::make_unique<Control>(); ctl->spec = s;
            if (!s.choices.empty()) {
                ctl->combo = std::make_unique<juce::ComboBox>();
                juce::StringArray arr; for (auto& c : s.choices) arr.add(c);
                ctl->combo->addItemList(arr, 1); addAndMakeVisible(*ctl->combo);
                ctl->comboAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.apvts, s.id, *ctl->combo);
            } else if (s.isBool) {
                ctl->toggle = std::make_unique<juce::TextButton>(s.name); ctl->toggle->setClickingTogglesState(true);
                addAndMakeVisible(*ctl->toggle);
                ctl->toggleAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts, s.id, *ctl->toggle);
            } else {
                ctl->slider = std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
                ctl->slider->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 78, 16);
                addAndMakeVisible(*ctl->slider);
                ctl->sliderAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts, s.id, *ctl->slider);
            }
            g->controls.push_back(std::move(ctl));
        }
        setSize(p.info().width, layout(true));
        startTimerHz(4);
    }
    ~CurioEditor() override { setLookAndFeel(nullptr); }

    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour(0xff15161a));
        auto top = getLocalBounds().removeFromTop(56).toFloat();
        g.setColour(juce::Colour(0xff1d1f25)); g.fillRect(top);
        g.setColour(proc.info().accent); g.fillRect(top.removeFromBottom(2));
        g.setColour(juce::Colours::white); g.setFont(juce::FontOptions(24.0f, juce::Font::bold));
        g.drawText(proc.info().title, 16, 6, 300, 28, juce::Justification::centredLeft);
        g.setColour(juce::Colours::white.withAlpha(0.5f)); g.setFont(juce::FontOptions(12.5f));
        g.drawText(proc.info().tagline, 16, 32, 420, 18, juce::Justification::centredLeft);
        for (auto& gr : groups) {
            g.setColour(juce::Colour(0xff1d1f25)); g.fillRoundedRectangle(gr->bounds.toFloat(), 6);
            g.setColour(proc.info().accent.withAlpha(0.85f)); g.setFont(juce::FontOptions(11.5f, juce::Font::bold));
            g.drawText(juce::String(gr->name).toUpperCase(), gr->bounds.getX() + 10, gr->bounds.getY() + 4, gr->bounds.getWidth() - 20, 16, juce::Justification::centredLeft);
            g.setColour(juce::Colours::white.withAlpha(0.65f)); g.setFont(juce::FontOptions(11.5f));
            for (auto& c : gr->controls) if (c->slider || c->combo)
                g.drawText(c->spec.name, c->labelArea, juce::Justification::centred);
        }
    }
    void resized() override { layout(false); }

private:
    struct Control {
        ParamSpec spec;
        std::unique_ptr<juce::Slider> slider; std::unique_ptr<juce::ComboBox> combo; std::unique_ptr<juce::TextButton> toggle;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAtt;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAtt;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> toggleAtt;
        juce::Rectangle<int> labelArea;
    };
    struct Group { std::string name; std::vector<std::unique_ptr<Control>> controls; juce::Rectangle<int> bounds; };

    void step(int d) {
        int n = (int)proc.presets().size(); if (n == 0) return;
        int i = (proc.getCurrentProgram() + d + n) % n; proc.applyPreset(i); presetBox.setSelectedId(i + 1, juce::dontSendNotification);
    }
    void timerCallback() { if (presetBox.getSelectedId() != proc.getCurrentProgram() + 1) presetBox.setSelectedId(proc.getCurrentProgram() + 1, juce::dontSendNotification); }
    struct T : juce::Timer { std::function<void()> f; void timerCallback() override { f(); } } timer;
    void startTimerHz(int hz) { timer.f = [this] { timerCallback(); }; timer.startTimerHz(hz); }

    int layout(bool measureOnly) {
        const int W = proc.info().width, pad = 10, cw = 86, ch = 96;
        if (!measureOnly) {
            presetBox.setBounds(W - 300, 14, 220, 28);
            prevBtn.setBounds(W - 76, 14, 28, 28); nextBtn.setBounds(W - 44, 14, 28, 28);
        }
        int y = 56 + pad;
        if (custom) { if (!measureOnly) custom->setBounds(pad, y, W - 2 * pad, proc.customViewHeight()); y += proc.customViewHeight() + pad; }
        int x = pad, rowH = 0;
        for (auto& gr : groups) {
            int n = (int)gr->controls.size();
            int perRow = std::max(1, (W - 2 * pad - 12) / cw);
            int cols = std::min(n, perRow), rows = (n + perRow - 1) / perRow;
            int gw = cols * cw + 12, gh = 24 + rows * ch + 6;
            if (x + gw > W - pad) { x = pad; y += rowH + pad; rowH = 0; }
            gr->bounds = { x, y, gw, gh };
            if (!measureOnly)
                for (int i = 0; i < n; ++i) {
                    auto& c = gr->controls[(size_t)i];
                    juce::Rectangle<int> cell(x + 6 + (i % perRow) * cw, y + 22 + (i / perRow) * ch, cw, ch);
                    if (c->slider) { c->labelArea = cell.removeFromTop(14); c->slider->setBounds(cell.reduced(2, 0)); }
                    else if (c->combo) { c->labelArea = cell.removeFromTop(14); c->combo->setBounds(cell.withSizeKeepingCentre(cw - 8, 24).translated(0, -14)); }
                    else c->toggle->setBounds(cell.withSizeKeepingCentre(cw - 8, 28));
                }
            x += gw + pad; rowH = std::max(rowH, gh);
        }
        return y + rowH + pad;
    }

    CurioProcessor& proc;
    CurioLook look;
    juce::ComboBox presetBox;
    juce::TextButton prevBtn { "<" }, nextBtn { ">" };
    std::unique_ptr<juce::Component> custom;
    std::vector<std::unique_ptr<Group>> groups;
};

inline juce::AudioProcessorEditor* CurioProcessor::createEditor() { return new CurioEditor(*this); }

} // namespace curio
