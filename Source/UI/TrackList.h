#pragma once

#include "AppContext.h"
#include "Widgets.h"

// Track headers beside the arranger: colour, name, type, mute / solo and a meter.
class TrackList : public juce::Component
{
public:
    explicit TrackList (AppContext&);

    void rebuild();     // after the project's tracks were replaced
    void tick();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    AppContext& ctx;
    juce::OwnedArray<SmallToggle> muteButtons, soloButtons, seqButtons;
    int builtGeneration = -1;

    juce::Rectangle<int> rowBounds (int index) const;
};
