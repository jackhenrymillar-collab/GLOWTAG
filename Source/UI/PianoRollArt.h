#pragma once

#include "NeonKit.h"
#include "../Theme.h"

// The piano roll's buttons: each is a small neon line drawing with its word beside it.
// Toggles (the tools, snap) are lit when on and bare glass when off. Actions are lit when
// they can do something (most need selected notes) and bare glass when they can't.
enum class RollArt { Draw, Select, Split, Velocity, Quantise, Grid, Snap, Shorter, Longer,
                     Duplicate, OctaveUp, OctaveDown, SelectAll };

namespace RollArtDraw
{
    // Draws the art in a square box. `lit` 0 is bare glass, 1 fully lit.
    void draw (juce::Graphics&, RollArt, juce::Rectangle<float> box, juce::Colour, float lit);
}

class RollButton : public juce::Button
{
public:
    RollButton (const juce::String& name, RollArt a, Pal p, const juce::String& tip)
        : juce::Button (name), art (a), pal (p)
    {
        setTooltip (tip);
    }

    std::function<bool()> enabled;       // actions: lit only when this says yes
    bool showWord = true;
    juce::String word;                   // shown instead of the name when set (the grid size)

    int idealWidth (float wordSize) const;
    void paintButton (juce::Graphics&, bool over, bool down) override;

private:
    RollArt art;
    Pal pal;
};
