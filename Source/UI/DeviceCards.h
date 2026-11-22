#pragma once

#include "DeviceViews.h"

// The rack cards in the device panel, drawn the way the expanded walls are: each device has one
// small neon piece, its name in lit lettering, then sign words and tube faders instead of knobs.
// Every card knob has a fader (its modulation jack sits at the fader's left end); a few of the
// expanded view's extra controls join them where there is room.
namespace DeviceCards
{
    // The small neon piece for a device, in a box about 64 x 46 with its top-left at (x, y)
    void art (juce::Graphics&, ModuleType, float x, float y, const juce::Colour* c, double t);
    void artScaled (juce::Graphics&, ModuleType, float x, float y, float scale, const juce::Colour* c, double t);   // the same, smaller or larger
    juce::String line (ModuleType);                  // the short description under the name
    void palette (ModuleType, juce::Colour out[3]);  // the device's Rainbow colours
    float width (ModuleType);                         // how wide its card is

    constexpr float bodyTop = 78.0f, side = 16.0f;   // where the controls start on a full-height card

    struct Card
    {
        DeviceViews::Device dev;
        DeviceViews::Built built;
        std::map<int, juce::Point<float>> modJack;   // card knob -> its modulation jack, in card space
        std::function<void()> chooseSample;          // Sampler / Crate Digger: pick a built-in sample
        std::function<void()> openPlugin;            // Plugin: open its own window
        bool animated = false;                        // has something that moves while it plays
    };

    std::unique_ptr<Card> make (AppContext&, int track, int moduleId, float width, float height);
}
