#pragma once

#include "AppContext.h"
#include "Widgets.h"

// One channel strip: inserts (the slot's devices), send A / B, pan, fader with meter, mute / solo and name.
// Tracks, the two return buses and the master are all strips; clicking one selects its slot so the
// device panel below shows its patch.
class ChannelStrip : public juce::Component
{
public:
    ChannelStrip (AppContext&, int slot);

    void tick();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    const int slot;

private:
    AppContext& ctx;
    Track& track;
    const bool isMaster, isReturn;
    float shownLevel = 0.0f;

    juce::Slider pan { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::Slider fader { juce::Slider::LinearVertical, juce::Slider::NoTextBox };
    juce::OwnedArray<juce::Slider> sends;
    SmallToggle mute { "M", Pal::Amber }, solo { "S", Pal::Gold };
    juce::Rectangle<int> meterArea, insertArea, sendArea;

    juce::StringArray insertNames() const;
};

// Logic-style mixer, in the GLOWTAG theme: tracks, then Return A, Return B and Master.
class MixerView : public juce::Component
{
public:
    explicit MixerView (AppContext&);

    void rebuild();     // after the project's tracks were replaced
    void tick();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    AppContext& ctx;
    juce::Viewport viewport;
    juce::Component content;
    juce::OwnedArray<ChannelStrip> strips;
    int builtGeneration = -1;
};
