#include "StatusBar.h"

void StatusBar::setInfo (const juce::String& left)
{
    if (left != info)
    {
        info = left;
        repaint();
    }
}

void StatusBar::paint (juce::Graphics& g)
{
    auto& th = Theme::get();
    g.fillAll (th.panel);
    g.setColour (th.line2);
    g.drawHorizontalLine (0, 0.0f, (float) getWidth());

    const auto c = th.themeColour();
    const float base = (float) getHeight() * 0.5f + 5.0f;
    const juce::Colour grey (0xff6d7077);
    Neon::paintLit (g, getWidth(), getHeight(), Neon::scratchGlow(), 0.5f, [&] (juce::Graphics& gg, bool)
    {
        Neon::bulb (gg, 18.0f, (float) getHeight() * 0.5f, c, 1.0f, 3.2f);
        Neon::label (gg, info.toLowerCase(), 32.0f, base, Neon::Align::left, grey, 13.0f);
        // Shortcuts: the key lit, what it does in grey
        const std::pair<const char*, const char*> keys[] = { { "space", "play" }, { "a - k", "play notes" }, { "z x", "octave" },
                                                             { "r", "record" }, { "1 2 3", "views" }, { "cmd k", "commands" } };
        float x = (float) getWidth() - 16.0f;
        for (int i = (int) std::size (keys) - 1; i >= 0; --i)
        {
            const juce::String what (keys[i].second), key (keys[i].first);
            x -= Neon::textWidth (what, 13.0f);
            Neon::label (gg, what, x, base, Neon::Align::left, grey, 13.0f);
            x -= Neon::textWidth (key, 14.0f) + 6.0f;
            Neon::text (gg, key, x, base, 14.0f, c, 0.6f);
            x -= 22.0f;
        }
    });
}
