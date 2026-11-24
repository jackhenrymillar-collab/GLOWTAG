#pragma once

#include "AppContext.h"
#include "Widgets.h"

// Track headers beside the arranger: colour, name, type, mute / solo and a meter.
// With automation shown, each header also names the control its lane moves ("device > control", click
// to choose another), its value, and a lamp when the lane rests because the control was moved by hand
// (click the lamp to go back to automation). The visualiser's track sits under the song's tracks.
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
    int shownLayout = -1;
    Neon::GlowLayer glowLayer;
    juce::int64 shownState = 0;     // what the last paint showed: repainted only when it changes
    juce::int64 stateNow() const;

    juce::Rectangle<int> rowBounds (int index) const;
    juce::Rectangle<float> chooserBox (int row) const;   // "device > control" words
    juce::Rectangle<float> restBox (int row) const;      // the resting lamp

    // What each header's lane shows, refreshed when lanes change
    struct Shown { AutoTarget target; AppContext::ParamInfo info; bool resting = false, automated = false; float value = 0.0f; };
    std::vector<Shown> shown;
    int shownGen = -1;
    void refreshShown();
};
