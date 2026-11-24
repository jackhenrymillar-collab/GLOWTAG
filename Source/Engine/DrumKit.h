#pragma once

#include "Patch.h"
#include <array>

// Drum Rack kits: the sixteen pads (their samples or analogue voices and every per-pad control) and
// the kit-wide sound, kept as small files you can step through. Also: filling the pads from a folder
// of samples, and which range a pad plays (low, mid or high), which the pads' colours follow.
namespace DrumKit
{
    enum class Range { Low, Mid, High };

    // Low: kicks, toms, subs, deep percussion. High: hats, shakers, cymbals. Mid: everything else.
    Range rangeOfName (const juce::String& sampleName);
    Range rangeOf (const Module& rack, int pad);        // the analogue voice decides, or the sample's name
    juce::String padName (const Module& rack, int pad);   // what a pad is called: its voice, file or sample

    // A kit as data, and back onto a rack (the pad that is selected stays as it is)
    juce::var toVar (const Module& rack);
    void apply (Module& rack, const juce::var& kit);

    juce::File folder();                                // where saved kits live
    juce::String fileExtension();                      // ".glowkit"

    // Every kit you can step through: the built-in ones first, then the saved ones by name
    struct Entry { juce::String name; juce::File file; int builtIn = -1; };
    std::vector<Entry> list();
    int indexOf (const std::vector<Entry>&, const juce::String& name);   // -1 when it isn't there
    bool load (Module& rack, const Entry&);
    juce::File save (const Module& rack, const juce::String& name);       // the file written, or none

    // Up to sixteen audio files from a folder, laid onto the pads: kicks and other low sounds on the
    // bottom row first, then the mid sounds, then the high ones. Returns how many pads it filled.
    int fillFromFolder (Module& rack, const juce::File& dir);
    juce::String nextFreeName (const juce::String& base);
}
