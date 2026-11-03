#pragma once

#include "AppContext.h"
#include "Widgets.h"

// One channel strip: inserts, pan, fader with meter, mute / solo and name.
class ChannelStrip : public juce::Component
{
public:
    ChannelStrip (AppContext&, Track* track, const juce::String& name, int colourIndex, bool isMaster);

    void tick();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    int trackIndex = -1;

private:
    AppContext& ctx;
    Track* track;
    juce::String name;
    int colourIndex;
    bool isMaster;
    float shownLevel = 0.0f;

    juce::Slider pan { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::Slider fader { juce::Slider::LinearVertical, juce::Slider::NoTextBox };
    SmallToggle mute { "M", Pal::Amber }, solo { "S", Pal::Gold };
    juce::Rectangle<int> meterArea, insertArea;

    float level() const;
};

// Logic-style mixer, in the GLOWTAG theme.
class MixerView : public juce::Component
{
public:
    explicit MixerView (AppContext&);

    void tick();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    AppContext& ctx;
    juce::Viewport viewport;
    juce::Component content;
    juce::OwnedArray<ChannelStrip> strips;
};
