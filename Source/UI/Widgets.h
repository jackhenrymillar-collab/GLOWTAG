#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Icons.h"
#include "../Theme.h"

// Pill button carrying one of the glowing line-art icons (transport, tools).
class IconButton : public juce::Button
{
public:
    IconButton (const juce::String& name, Icon i, Pal p) : juce::Button (name), icon (i), pal (p) {}

    float glow = 0.0f; // extra brightness, e.g. pulsing with the beat

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto& th = Theme::get();
        const auto c = th.pal (pal);
        const bool on = getToggleState();
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        const float radius = juce::jmin (r.getHeight() * 0.5f, 10.0f);

        g.setColour (on ? c.withAlpha (0.14f) : (over || down ? th.panel2.brighter (0.06f) : th.panel2));
        g.fillRoundedRectangle (r, radius);
        g.setColour (on ? c : c.withAlpha (over ? 0.75f : 0.45f));
        g.drawRoundedRectangle (r, radius, 1.0f);

        const float s = r.getHeight() - 8.0f;
        Glow::drawIcon (g, icon, r.withSizeKeepingCentre (s, s), on ? c : c.withMultipliedAlpha (0.85f),
                        on ? 1.0f : glow);
    }

private:
    Icon icon;
    Pal pal;
};

// Small square toggle used for Mute / Solo.
class SmallToggle : public juce::Button
{
public:
    SmallToggle (const juce::String& caption, Pal p) : juce::Button (caption), pal (p)
    {
        setClickingTogglesState (true);
    }

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        auto& th = Theme::get();
        const auto c = th.pal (pal);
        const bool on = getToggleState();
        auto r = getLocalBounds().toFloat().reduced (0.5f);

        g.setColour (on ? c.withAlpha (0.22f) : (over ? th.panel2.brighter (0.06f) : th.panel2));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (on ? c : th.line);
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
        g.setColour (on ? c : th.muted);
        g.setFont (th.label (10.0f));
        g.drawText (getName(), getLocalBounds(), juce::Justification::centred);
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
