#pragma once

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

// The Eurorack-style rolling sequencer. A MIDI track can swap its arranged clips for a rack of
// modules (a clock, a 16-step sequencer, Euclidean and Turing-machine pattern makers, a quantiser,
// a probability gate and slow "circadian" modulators) joined by patch cables, like a hardware
// sequencer in a modular case. The rack plays the track's instrument live for as long as the
// transport (or its own free-running clock) is rolling.
//
// Signals between modules are control voltages measured once per audio sub-block:
//   pitch   : semitones from middle C (C3 = 60), so 1.0 = one semitone, 12.0 = one octave
//   gate    : 1 while high, 0 while low; a trigger is a gate held high for one tick
//   cv      : 0..1 (or -1..1 for bipolar modulators)

enum class SeqType { Clock, Steps, Euclid, Turing, Burst, Chance, Quantise, Mutate, Circadian, Tide, Out };
constexpr int numSeqTypes = 11;

struct SeqPortInfo
{
    juce::String name;
    bool input;         // left of the signal flow: a jack that takes one cable
    bool gate;          // gate / trigger (drawn as a smaller ring) rather than a continuous cv
};

struct SeqKnobInfo
{
    juce::String name;
    float defaultValue;                       // 0..1
    juce::StringArray choices;                // non-empty: a stepped switch between these positions
};

struct SeqInfo
{
    juce::String title;
    int pal;                                  // Pal index
    float width;                              // module width in pixels
    std::vector<SeqKnobInfo> knobs;
    std::vector<SeqPortInfo> ports;           // inputs and outputs, addressed by index
    juce::String blurb;

    int numInputs() const;
    int firstOutput() const { return numInputs(); }
    int portIndex (const juce::String& name) const;
};

const SeqInfo& seqInfo (SeqType);

constexpr int seqSteps = 16;

struct SeqModule
{
    int id = 0;
    SeqType type = SeqType::Steps;
    std::vector<float> knobs;                 // 0..1, one per SeqInfo::knobs entry
    std::array<int, seqSteps> stepPitch {};   // Steps: semitones, -12..12
    std::array<bool, seqSteps> stepOn {};     // Steps: gate on / rest
};

struct SeqCable
{
    int fromModule = 0, fromPort = 0;         // fromPort is an output index on fromModule
    int toModule = 0, toPort = 0;             // toPort is an input index on toModule
};

// Knob values decoded for the engine and the UI
namespace SeqKnob
{
    inline int choice (float v, int numChoices) { return juce::jlimit (0, numChoices - 1, (int) std::lround (v * (float) (numChoices - 1))); }
    inline float fromChoice (int index, int numChoices) { return numChoices > 1 ? (float) index / (float) (numChoices - 1) : 0.0f; }
    inline int count (float v, int lo, int hi) { return juce::jlimit (lo, hi, lo + (int) std::lround (v * (float) (hi - lo))); }
}

// A track's sequencer rack: modules in rail order with the cables between them.
// Edited on the message thread; the engine builds a SeqRuntime from it.
class SeqRack
{
public:
    SeqRack();

    std::vector<SeqModule> modules;           // drawn left to right in this order
    std::vector<SeqCable> cables;

    static constexpr int clockId = 1, outId = 2;

    SeqModule* find (int id);
    const SeqModule* find (int id) const;
    int indexOf (int id) const;

    // Add a module in front of Out and wire it in sensibly: clocked modules take the clock,
    // pattern makers drive the gate, cv processors slip into the pitch path, modulators land
    // on the first free cv input.
    int add (SeqType);

    // Remove a module, joining its neighbours back up.
    void remove (int id);

    bool connect (int fromModule, int fromPort, int toModule, int toPort);
    void disconnect (size_t cableIndex);
    int cableInto (int module, int port) const;        // index or -1
    std::vector<int> cablesFrom (int module, int port) const;
    void move (int id, int newIndex);

    // Scale definitions shared by the Quantise module and its keyboard display
    struct Scale { const char* name; std::array<bool, 12> notes; };
    static const std::vector<Scale>& scales();
    static const char* noteName (int semitone);

private:
    int nextId = 3;
    void insertInline (int id, int inPort, int outPort, int targetModule, int targetPort);
};

//==============================================================================
// Audio-thread side. Built from a SeqRack on the message thread, swapped in by the engine.

struct SeqTick
{
    double beat;          // song position at the start of the tick, or -1 while the transport is stopped
    double beatsPerTick;  // tick length in beats (at the current tempo)
    double seconds;       // wall clock, seconds since midnight local time
    bool reset;           // the transport just started or jumped
};

class SeqNode
{
public:
    SeqNode (const SeqModule&);
    virtual ~SeqNode() = default;

    const int id;
    const SeqType type;
    const SeqInfo& info;

    std::array<std::atomic<float>, 8> knob;             // 0..1, set by the UI
    std::array<std::atomic<int>, seqSteps> stepPitch;   // Steps
    std::array<std::atomic<bool>, seqSteps> stepOn;

    std::vector<float> in;      // input values this tick (0 when nothing is plugged in)
    std::vector<bool> plugged;  // whether an input has a cable
    std::vector<float> out;     // output values after process()

    // UI readouts written on the audio thread: current step, LEDs, phase...
    std::array<std::atomic<float>, 4> display;

    virtual void process (const SeqTick&) = 0;
    virtual void reset() {}

protected:
    float k (int i) const { return knob[(size_t) i].load(); }
    int choice (int i) const { return SeqKnob::choice (k (i), info.knobs[(size_t) i].choices.size()); }
    bool rose (int input);      // input went high since the last tick
    std::vector<bool> wasHigh;
};

class SeqRuntime
{
public:
    explicit SeqRuntime (const SeqRack&);

    // Note events the Out module produces. `on` false is a note off.
    using NoteSink = std::function<void (int pitch, float velocity, bool on)>;

    void process (const SeqTick&, const NoteSink&);
    void allNotesOff (const NoteSink&);
    SeqNode* find (int moduleId);

    // Out module's Mod input, for the track's instrument (0..1)
    std::atomic<float> modOut { 0.0f };

private:
    struct Link { SeqNode* from; int fromPort; SeqNode* to; int toPort; };
    std::vector<std::unique_ptr<SeqNode>> nodes;    // in processing order
    std::vector<Link> links;
    SeqNode* outNode = nullptr;

    // Out state
    bool gateHigh = false;
    std::array<int, 4> held { -1, -1, -1, -1 };
    int nextVoice = 0;
    int lastPitch = 60;
    int sustainTicks = 0;
};

std::unique_ptr<SeqNode> createSeqNode (const SeqModule&);
