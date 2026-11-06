#pragma once

#include "AppContext.h"
#include "Widgets.h"

// Thin bar along the bottom: audio status on the left, shortcuts on the right.
class StatusBar : public juce::Component
{
public:
    void setInfo (const juce::String& left);
    void paint (juce::Graphics&) override;

private:
    juce::String info { "Starting audio" };
};
