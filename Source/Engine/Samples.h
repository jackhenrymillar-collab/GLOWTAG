#pragma once

#include "Project.h"

// Built-in sample bank, synthesised at startup so the app makes sound with no files on disk.
// Also used as the Drum Rack's default pads and as the browser's Samples list.
namespace Samples
{
    struct Entry { juce::String name, category; std::shared_ptr<const AudioData> data; };

    const std::vector<Entry>& bank();
    std::shared_ptr<const AudioData> find (const juce::String& name);

    // Sixteen default pad sounds for the Drum Rack, pads 0..15 = MIDI 36..51
    const std::array<juce::String, 16>& defaultPads();
}
