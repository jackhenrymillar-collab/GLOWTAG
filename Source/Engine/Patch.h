#pragma once

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <vector>

// A track's modular device patch: modules joined by audio, note and modulation cables.
// New modules wire themselves in automatically; any cable can be re-patched by hand, as long as
// both ends carry the same kind of signal.

enum class ModuleType { Input, Output, Sampler, Synth, Drums, Filter, Drive, Delay, Reverb, EQ, Compressor, LFO, Plugin,
                        Attenuator, Mixer, Inverter, Crossfade, Slew,
                        GasTube, CrateDigger, StringWall, GhostTag, Crowd, Transformer, DrumSeq };
constexpr int numModuleTypes = 25;

// What a jack carries. Notes are the track's MIDI: pitch, velocity, gate and the rest, from clips, the
// keyboard and the sequencer rack. A utility's signal jacks are `Any`: they take on whatever they are
// patched to, sound or modulation, and the whole utility then works on that.
enum class Signal { Audio, Notes, Mod, Any };

// Jacks on a module
namespace Port
{
    constexpr int audioIn  = 0;
    constexpr int audioOut = 1;
    constexpr int modOut   = 2;
    constexpr int notesIn  = 3;
    constexpr int notesOut = 4;
    constexpr int auxOut   = 5;                     // a second sound output (the Drum Rack's pads set to their own out)
    constexpr int modInBase = 10;                     // modInBase + k = modulation input for knob k
    constexpr int sigInBase = 40;                     // sigInBase + i = a utility's signal input i
    constexpr int sigOut    = 60;                     // a utility's signal output
    constexpr int maxSigIns = 4;
    inline int modIn (int knob)          { return modInBase + knob; }
    inline int sigIn (int index)         { return sigInBase + index; }
    inline bool isModIn (int port)       { return port >= modInBase && port < sigInBase; }
    inline bool isSigIn (int port)       { return port >= sigInBase && port < sigOut; }
    inline bool isInput (int port)       { return port == audioIn || port == notesIn || isModIn (port) || isSigIn (port); }

    // What the jack carries by its own nature (`Any` for a utility's signal jacks)
    inline Signal kind (int port)
    {
        if (port == audioIn || port == audioOut || port == auxOut) return Signal::Audio;
        if (port == notesIn || port == notesOut) return Signal::Notes;
        if (port == modOut || isModIn (port))    return Signal::Mod;
        return Signal::Any;
    }
}

struct ModuleInfo
{
    juce::String title;
    int pal;                                          // Pal index
    bool hasAudioIn, hasAudioOut, hasModOut;
    std::vector<std::pair<juce::String, float>> knobs; // name, default value
    float width;                                      // card width in pixels
    bool hasNotesIn = false, hasNotesOut = false;
    int sigIns = 0;                                   // utilities: signal inputs (they all have one signal output)
    bool utility = false;                             // a half-height module on the utility row
    bool hasAuxOut = false;                           // a second sound output (Port::auxOut)

    // Controls only the expanded view shows, stored after the knobs. `fresh` is the value on a new
    // module; `legacy` is what a project saved before the control existed gets, chosen so it sounds
    // exactly as it did.
    struct Extra { juce::String name; float fresh, legacy; };
    std::vector<Extra> extras;

    int numParams() const                             { return (int) (knobs.size() + extras.size()); }
    float defaultValue (int param, bool legacy = false) const;
    juce::String paramName (int param) const;

    bool isInstrument() const { return hasNotesIn && ! hasAudioIn; }
    bool isEffect() const     { return hasAudioIn && hasAudioOut; }
    bool hasPort (int port) const;
    std::vector<int> ports() const;                   // every jack, inputs and outputs
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
    juce::String kitName;                             // Drum Rack: the kit it was last saved as or loaded from
    juce::String pluginId, pluginName;                // Plugin: AU / VST3 identifier and display name
    juce::MemoryBlock pluginState;                    // Plugin: its saved state
};

struct Cable
{
    int fromModule = 0, fromPort = 0;
    int toModule = 0, toPort = 0;
};

class Patch
{
public:
    Patch();

    std::vector<Module> modules;
    std::vector<Cable> cables;

    static constexpr int inputId = 1, outputId = 2;
    static constexpr float cardHeight = 250.0f, gap = 34.0f, top = 14.0f;
    // Utilities sit on a second row under the devices, much shorter
    static constexpr float utilityHeight = 100.0f, utilityTop = top + cardHeight + 22.0f, utilityGap = 16.0f;
    static constexpr float rowsBottom = utilityTop + utilityHeight;
    static float rowY (ModuleType t)      { return moduleInfo (t).utility ? utilityTop : top; }
    static float heightOf (ModuleType t)  { return moduleInfo (t).utility ? utilityHeight : cardHeight; }

    Module* find (int id);
    const Module* find (int id) const;

    // Add a module and wire it in. An effect goes at the end of the chain, just before the output.
    // An instrument takes its notes from the track input and becomes the start of the chain (a second
    // one is layered with the first through a Mixer). An LFO patches into the first free knob of the
    // last module in the chain. Utilities land on the utility row unpatched. `asInstrument` is for
    // plugins, which can be either an instrument or an effect.
    int add (ModuleType, bool asInstrument = false);
    int addAt (ModuleType, juce::Point<float>, bool asInstrument = false);

    // Make sure the track's own sound is heard: if the track input's sound jack is loose, patch it into
    // the start of the chain, mixed with any instrument already there. True if it changed anything.
    bool patchTrackSound();
    bool upgraded = false;                            // loaded from a file saved before note cables

    // Remove a module, joining its neighbours back up so the sound keeps flowing.
    void remove (int id);

    // What a jack carries here. A utility's signal jacks follow whatever the utility is patched to,
    // through any other utilities; `Any` while nothing decides it.
    Signal kindOf (int module, int port) const;
    Signal kindOf (const Cable& c) const              { return kindOf (c.fromModule, c.fromPort); }

    // Whether an output may be patched to an input: both jacks exist and carry the same kind of
    // signal. So notes only go to instruments, sound never goes into a knob, and so on.
    bool canConnect (int fromModule, int fromPort, int toModule, int toPort) const;

    // Connect two jacks, if canConnect. An input takes one cable, so anything already in it is replaced.
    bool connect (int fromModule, int fromPort, int toModule, int toPort);
    void disconnect (size_t cableIndex);
    int cableInto (int module, int port) const;       // index or -1

    // Lay modules out left to right in signal order
    void tidy();

    juce::Point<float> nextFreePosition (bool utilityRow = false) const;

    juce::var toVar() const;
    void fromVar (const juce::var&);

private:
    int nextId = 3;
    int lastInChain() const;                          // module feeding the output
    Signal settledKind (int utilityModule) const;
    void wireInstrument (int id);
    void joinChain (int fromModule, int fromPort, bool replaceTrackSound);
    void upgrade();                                   // bring a patch saved before note cables up to date
    void moveDrumSteps (const std::vector<int>& oldRacks);
    void placeBefore (int id, int before);   // racks saved with their own step rows get a sequencer
public:
    // Put a Drum Rack Sequencer in front of a Drum Rack: the notes that went into the rack now go through
    // the sequencer, which adds its steps. Returns the sequencer's id (an existing one if there is one).
    int addSequencerFor (int drumRack);
};
