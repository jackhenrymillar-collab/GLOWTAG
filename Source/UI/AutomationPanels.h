#pragma once

#include "AppContext.h"
#include "Widgets.h"

// The lane chooser: opened from a track header's "device > control" words. Devices (mixer first, then
// each device, its morph lane and, on the visualiser's track, its groups) on the left, their controls
// on the right; a lamp marks controls that already have automation. Type to search every control.
// Return picks the highlighted one, Esc or a click outside closes.
class AutomationChooser : public juce::Component
{
public:
    explicit AutomationChooser (AppContext&);

    void open (int track, juce::Rectangle<int> screenAnchor);
    void close();
    bool isOpen() const { return isVisible(); }
    bool keyUsed (const juce::KeyPress&);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    AppContext& ctx;
    int track = -1;
    std::vector<AppContext::ParamInfo> params;
    juce::StringArray groups;
    int group = 0;
    juce::String search;
    std::vector<int> listed;           // indices into params shown on the right
    int highlight = 0, scroll = 0, hoverGroup = -1, hoverItem = -1;
    juce::Rectangle<int> anchor;       // local
    Neon::GlowLayer glowLayer;

    static constexpr int rowH = 24, width = 560, height = 380;
    juce::Rectangle<float> panel() const;
    juce::Rectangle<float> groupArea() const;
    juce::Rectangle<float> listArea() const;
    int visibleRows() const { return (int) (listArea().getHeight() / (float) rowH); }
    void refilter();
    void pick (int paramIndex);
    bool automated (const AutoTarget&) const;
};

// Settings: automation sticks to clips, lanes shown, Automation Brightness, the visualiser's track
class SettingsPanel : public juce::Component
{
public:
    explicit SettingsPanel (AppContext&);

    void open();
    void close();
    bool isOpen() const { return isVisible(); }
    bool keyUsed (const juce::KeyPress&);
    std::function<void()> onChanged;   // something changed: remember it

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    AppContext& ctx;
    Neon::GlowLayer glowLayer;
    int hover = -1;
    bool draggingLever = false;

    enum Row { sticky, lanes, brightness, visTrack, numRows };
    juce::Rectangle<float> panel() const;
    juce::Rectangle<float> rowBox (int row) const;
    juce::Rectangle<float> leverTrack() const;
    juce::Rectangle<float> closeBox() const;
    void setBrightness (float x);
    void toggle (int row);
};
