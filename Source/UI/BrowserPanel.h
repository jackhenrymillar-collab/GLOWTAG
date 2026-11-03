#pragma once

#include "AppContext.h"
#include "Widgets.h"

// Left-hand browser: Samples / Devices / Presets tabs.
// Devices shows the instrument and effect cards with their line-art icons.
class BrowserPanel : public juce::Component
{
public:
    explicit BrowserPanel (AppContext&);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;

    struct Item { juce::String name, detail; Icon icon; Pal pal; };

private:
    AppContext& ctx;
    juce::TextButton tabs[3];
    int tab = 1;
    int hovered = -1, chosen = -1;
    juce::Rectangle<int> listArea;

    const std::vector<Item>& items() const;
    juce::Rectangle<int> itemBounds (int index) const;
    int itemAt (juce::Point<int>) const;
};
