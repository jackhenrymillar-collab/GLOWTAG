#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Icons.h"
#include "NeonKit.h"
#include "../Theme.h"

// A button that is a sign word (its name): lit glass when on, grey glass when off. It used to carry
// an icon; the transport and the tools are words now, like every control on the device walls.
class IconButton : public juce::Button
{
public:
    IconButton (const juce::String& name, Icon i, Pal p) : juce::Button (name), icon (i), pal (p) {}

    float glow = 0.0f; // extra brightness, e.g. pulsing with the beat
    float wordSize = 0.0f; // 0: from the height

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        juce::ignoreUnused (icon);
        const auto c = Theme::get().pal (pal);
        const bool on = getToggleState();
        const auto r = getLocalBounds().toFloat();
        const float size = wordSize > 0.0f ? wordSize : juce::jlimit (12.0f, 30.0f, r.getHeight() * 0.7f);
        const auto word = getName().toLowerCase();
        Neon::paintLit (g, getWidth(), getHeight(), Neon::scratchGlow(), 0.7f, [&] (juce::Graphics& gg, bool)
        {
            Neon::wordButton (gg, word, r, size, c, on, over || down);
        });
    }

private:
    Icon icon;
    Pal pal;
};

// A small toggle word (mute, solo, seq): lit in its colour when on, grey glass when off.
class SmallToggle : public juce::Button
{
public:
    SmallToggle (const juce::String& caption, Pal p) : juce::Button (caption), pal (p)
    {
        setClickingTogglesState (true);
    }

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        const auto c = Theme::get().pal (pal);
        const bool on = getToggleState();
        const auto r = getLocalBounds().toFloat();
        const auto word = getName().toLowerCase();
        const float size = juce::jlimit (11.0f, 17.0f, r.getHeight() * 0.8f);
        Neon::paintLit (g, getWidth(), getHeight(), Neon::scratchGlow(), 0.5f, [&] (juce::Graphics& gg, bool)
        {
            Neon::wordButton (gg, word, r, size, c, on, over);
        });
    }

private:
    Pal pal;
};

// Vertical level meter: a slim bar with a glowing top.
inline void drawMeter (juce::Graphics& g, juce::Rectangle<float> r, float level, juce::Colour c)
{
    auto& th = Theme::get();
    g.setColour (th.display);
    g.fillRoundedRectangle (r, 2.0f);
    level = juce::jlimit (0.0f, 1.0f, level);
    if (level <= 0.001f)
        return;
    auto lit = r.withTop (r.getBottom() - r.getHeight() * level);
    g.setGradientFill (juce::ColourGradient (c.withAlpha (0.25f), lit.getBottomLeft(), c, lit.getTopLeft(), false));
    g.fillRoundedRectangle (lit, 2.0f);
}

// Short roman numeral, as on the ability cards.
inline juce::String romanNumeral (int n)
{
    static const char* numerals[] = { "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X",
                                       "XI", "XII", "XIII", "XIV", "XV", "XVI", "XVII", "XVIII", "XIX", "XX" };
    return n >= 1 && n <= 20 ? juce::String (numerals[n - 1]) : juce::String (n);
}
