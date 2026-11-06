#pragma once

#include "AppContext.h"
#include "Widgets.h"

// Left-hand browser: Samples / Devices / Sequencers tabs.
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
    void mouseDoubleClick (const juce::MouseEvent&) override;

    // Double-clicking a device adds it to the selected track, wired in automatically
    std::function<void (ModuleType)> onAddDevice;
    // Double-clicking a sample loads it into the selected track's sampler
    std::function<void (const juce::String& sampleName)> onLoadSample;
    // Double-clicking a sequencer module adds it to the selected track's rack
    std::function<void (SeqType)> onAddSequencer;

    struct Item { juce::String name, detail; Icon icon; Pal pal; };

private:
    AppContext& ctx;
    juce::TextButton tabs[3];
    int tab = 1;
    int hovered = -1, chosen = -1;
    juce::Rectangle<int> listArea;

    const std::vector<Item>& items() const;
    mutable std::vector<Item> sampleItems;
    juce::Rectangle<int> itemBounds (int index) const;
    int itemAt (juce::Point<int>) const;
};
