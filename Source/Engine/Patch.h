#pragma once

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <vector>

// A track's modular device patch: modules joined by audio and modulation cables.
// New modules wire themselves in automatically; any cable can be re-patched by hand.

enum class ModuleType { Input, Output, Sampler, Synth, Drums, Filter, Drive, Delay, Reverb, EQ, Compressor, LFO };
constexpr int numModuleTypes = 12;

// Jacks on a module
namespace Port
{
    constexpr int audioIn  = 0;
    constexpr int audioOut = 1;
    constexpr int modOut   = 2;
    constexpr int modInBase = 10;                     // modInBase + k = modulation input for knob k
    inline int modIn (int knob)          { return modInBase + knob; }
    inline bool isModIn (int port)       { return port >= modInBase; }
    inline bool isInput (int port)       { return port == audioIn || isModIn (port); }
    inline bool isMod (int port)         { return port == modOut || isModIn (port); }
}

struct ModuleInfo
{
    juce::String title;
    int pal;                                          // Pal index
    bool hasAudioIn, hasAudioOut, hasModOut;
    std::vector<std::pair<juce::String, float>> knobs; // name, default value
    float width;                                      // card width in pixels
};

const ModuleInfo& moduleInfo (ModuleType);

struct Module
{
    int id = 0;
    ModuleType type = ModuleType::Filter;
    juce::Point<float> position;                      // top-left on the patch canvas
    std::vector<float> knobValues;
    juce::String sampleName;                          // Sampler: built-in sample name, or a file path
    std::array<juce::String, 16> padNames;            // Drum Rack: sample per pad
};

struct Cable
{
    int fromModule = 0, fromPort = 0;
    int toModule = 0, toPort = 0;
    bool isMod() const { return Port::isMod (fromPort); }
};

class Patch
{
public:
    Patch();

    std::vector<Module> modules;
    std::vector<Cable> cables;

    static constexpr int inputId = 1, outputId = 2;
    static constexpr float cardHeight = 172.0f, gap = 34.0f, top = 14.0f;

    Module* find (int id);
    const Module* find (int id) const;

    // Add a module and wire it in: audio modules go at the end of the chain (just before
    // the output); an LFO patches into the first free knob of the last module in the chain.
    int add (ModuleType);
    int addAt (ModuleType, juce::Point<float>);

    // Remove a module, joining its neighbours back up so the sound keeps flowing.
    void remove (int id);

    // Connect two jacks. An input takes one cable, so anything already in it is replaced.
    bool connect (int fromModule, int fromPort, int toModule, int toPort);
    void disconnect (size_t cableIndex);
    int cableInto (int module, int port) const;       // index or -1

    // Lay modules out left to right in signal order
    void tidy();

    juce::Point<float> nextFreePosition() const;

private:
    int nextId = 3;
    int lastInChain() const;                          // module feeding the output
};
