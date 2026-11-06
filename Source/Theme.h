#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// The eight named colours the interface uses. In Rainbow mode they are the
// ability-card palette; in one-colour mode they become shades of the theme colour.
enum class Pal { Rose, Green, Sky, Gold, Violet, Blue, Amber, Ice };

class Theme : public juce::ChangeBroadcaster
{
public:
    static Theme& get();

    struct Choice { juce::String name; juce::Colour colour; };
    const juce::Array<Choice>& choices() const { return themeChoices; }

    int  getThemeIndex() const { return themeIndex; }
    bool isRainbow() const     { return rainbow; }
    void setThemeIndex (int index);
    void setRainbow (bool shouldBeRainbow);

    juce::Colour themeColour() const { return themeChoices[themeIndex].colour; }
    juce::Colour pal (Pal p) const;
    juce::Colour pal (int index) const { return pal ((Pal) (index % 8)); }

    // Neutral dark-grey surfaces
    const juce::Colour bg      { 0xff141416 };
    const juce::Colour panel   { 0xff1c1d20 };
    const juce::Colour panel2  { 0xff222326 };
    const juce::Colour display { 0xff121214 };
    const juce::Colour line    { 0xff36383d };
    const juce::Colour line2   { 0xff26272b };
    const juce::Colour text    { 0xffe9e9ec };
    const juce::Colour muted   { 0xff9a9ba1 };

    // Fonts: letter-spaced serif caps for headings, clean sans for controls
    juce::Font heading (float size) const;
    juce::Font body (float size) const;
    juce::Font label (float size) const;

private:
    Theme();

    juce::Array<Choice> themeChoices;
    int themeIndex = 0;
    bool rainbow = false;
};
