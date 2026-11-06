#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Look and feel: outlined pill buttons, glowing arc knobs, slim faders.
class GlowLookAndFeel : public juce::LookAndFeel_V4
{
public:
    GlowLookAndFeel();

    void refreshColours();

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& background,
                               bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float startAngle, float endAngle, juce::Slider&) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
};
