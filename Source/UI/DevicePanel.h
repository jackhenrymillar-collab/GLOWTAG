#pragma once

#include "AppContext.h"
#include "Widgets.h"

// One device in the chain: framed card with an icon, title, numeral and knobs.
class DeviceCard : public juce::Component
{
public:
    DeviceCard (const juce::String& title, Icon, Pal, int numeral,
                std::initializer_list<std::pair<const char*, double>> knobs);

    void paint (juce::Graphics&) override;
    void resized() override;
    float pulse = 0.0f;

private:
    juce::String title;
    Icon icon;
    Pal pal;
    int numeral;
    juce::OwnedArray<juce::Slider> sliders;
    juce::StringArray knobNames;
};

// Bottom panel showing the selected track's device chain.
class DevicePanel : public juce::Component
{
public:
    explicit DevicePanel (AppContext&);

    void tick();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    AppContext& ctx;
    juce::OwnedArray<DeviceCard> cards;
};
