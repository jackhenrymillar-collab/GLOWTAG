#include "Theme.h"

Theme& Theme::get()
{
    static Theme instance;
    return instance;
}

Theme::Theme()
{
    themeChoices.add ({ "Green",  juce::Colour (0xff4ade80) });
    themeChoices.add ({ "Sky",    juce::Colour (0xff4cc9f0) });
    themeChoices.add ({ "Blue",   juce::Colour (0xff8fa2ff) });
    themeChoices.add ({ "Violet", juce::Colour (0xffa78bfa) });
    themeChoices.add ({ "Rose",   juce::Colour (0xffff6b8f) });
    themeChoices.add ({ "Amber",  juce::Colour (0xffffa94d) });
    themeChoices.add ({ "Gold",   juce::Colour (0xffffd27a) });
}

void Theme::setThemeIndex (int index)
{
    themeIndex = juce::jlimit (0, themeChoices.size() - 1, index);
    rainbow = false;
    sendChangeMessage();
}

void Theme::setRainbow (bool shouldBeRainbow)
{
    rainbow = shouldBeRainbow;
    sendChangeMessage();
}

void Theme::setVintage (bool shouldBeVintage)
{
    vintage = shouldBeVintage;
    sendChangeMessage();
}

juce::Colour Theme::pal (Pal p) const
{
    if (rainbow)
    {
        switch (p)
        {
            case Pal::Rose:   return juce::Colour (0xffff6b8f);
            case Pal::Green:  return juce::Colour (0xff4ade80);
            case Pal::Sky:    return juce::Colour (0xff4cc9f0);
            case Pal::Gold:   return juce::Colour (0xffffd27a);
            case Pal::Violet: return juce::Colour (0xffa78bfa);
            case Pal::Blue:   return juce::Colour (0xff8fa2ff);
            case Pal::Amber:  return juce::Colour (0xffffa94d);
            case Pal::Ice:    return juce::Colour (0xffbfeaff);
        }
    }

    // One-colour mode: shades of the theme colour so items stay distinguishable
    auto th = themeColour();
    auto towardsWhite = [th] (float keep) { return th.interpolatedWith (juce::Colours::white, 1.0f - keep); };
    auto towardsBlack = [th] (float keep) { return th.interpolatedWith (juce::Colours::black, 1.0f - keep); };

    switch (p)
    {
        case Pal::Green:  return th;
        case Pal::Amber:  return towardsWhite (0.82f);
        case Pal::Sky:    return towardsBlack (0.88f);
        case Pal::Violet: return towardsWhite (0.65f);
        case Pal::Blue:   return towardsBlack (0.76f);
        case Pal::Gold:   return towardsWhite (0.55f);
        case Pal::Rose:   return towardsWhite (0.92f);
        case Pal::Ice:    return towardsWhite (0.40f);
    }
    return th;
}

juce::Font Theme::heading (float size) const
{
   #if JUCE_MAC
    return juce::Font (juce::FontOptions ("Baskerville", size, juce::Font::bold));
   #else
    return juce::Font (juce::FontOptions (juce::Font::getDefaultSerifFontName(), size, juce::Font::bold));
   #endif
}

juce::Font Theme::body (float size) const
{
   #if JUCE_MAC
    return juce::Font (juce::FontOptions ("Georgia", size, juce::Font::plain));
   #else
    return juce::Font (juce::FontOptions (juce::Font::getDefaultSerifFontName(), size, juce::Font::plain));
   #endif
}

juce::Font Theme::label (float size) const
{
   #if JUCE_MAC
    return juce::Font (juce::FontOptions ("Avenir Next", size, juce::Font::bold));
   #else
    return juce::Font (juce::FontOptions (juce::Font::getDefaultSansSerifFontName(), size, juce::Font::bold));
   #endif
}
