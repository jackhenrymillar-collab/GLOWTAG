#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Patch.h"
#include "Project.h"
#include "../DSP/Analog.h"
#include <atomic>
#include <array>
#include <memory>
#include <functional>

// The audio side of a track's patch. A Graph is built from a Patch on the message thread,
// handed to the audio thread, and processed in small sub-blocks so modulation cables move
// knobs smoothly. Knob values live in atomics so the UI can turn them while it plays.

constexpr int maxParams = 96;          // card knobs plus the expanded view's controls (the Drum Rack has the most)
constexpr int maxKnobs = maxParams;
constexpr int subBlock = 32;

// What the engine tells a graph about the song while it runs: where the playhead is, and the other
// tracks' sound from the last block (for a compressor listening to another track).
struct GraphHost
{
    double beat = 0.0, beatsPerSample = 0.0, bpm = 120.0;
    bool playing = false;
    int blockPos = 0;                                 // samples into the engine's current block
    const juce::AudioBuffer<float>* const* sideBuffers = nullptr;   // per project slot, the last block's output
    int numSide = 0;
};

// The last couple of thousand samples a node put out, for the expanded view's scopes and spectra.
// Written on the audio thread, read on the message thread; a torn read only shows as a glitch in a picture.
struct Scope
{
    static constexpr int size = 2048;
    std::array<float, size> data {};
    std::atomic<int> write { 0 };
    void push (const float* l, const float* r, int n)
    {
        int w = write.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i) { data[(size_t) w] = (l[i] + r[i]) * 0.5f; w = (w + 1) & (size - 1); }
        write.store (w, std::memory_order_release);
    }
    // The most recent `n` samples, oldest first
    void read (float* dest, int n) const
    {
        int r = (write.load (std::memory_order_acquire) - n) & (size - 1);
        for (int i = 0; i < n; ++i) { dest[i] = data[(size_t) r]; r = (r + 1) & (size - 1); }
    }
};

// A voice as the expanded view sees it: lamps ride the envelopes and playheads cross the sample
struct VoiceView
{
    std::atomic<int> pitch { -1 };                    // -1: idle
    std::atomic<float> velocity { 0.0f }, level { 0.0f }, filter { 0.0f };
    std::atomic<float> age { 0.0f }, released { -1.0f };   // seconds since the note began; when it was let go (-1 still held)
    std::atomic<float> pos { 0.0f };                  // Sampler: where in the file (0..1)
    std::atomic<int> slice { -1 };
};

class Node
{
public:
    Node (const Module& m, double sampleRate);
    virtual ~Node() = default;

    const int id;
    const ModuleType type;
    const ModuleInfo& info;

    std::array<std::atomic<float>, maxKnobs> knob;   // base values, set by the UI
    std::array<float, maxKnobs> mod {};              // added by modulation cables each sub-block

    // Knob after modulation, clamped to its range
    float k (int i) const { return juce::jlimit (lo (i), 1.0f, knob[(size_t) i].load() + mod[(size_t) i]); }

    virtual void noteOn (int pitch, float velocity, double gateSamples) { juce::ignoreUnused (pitch, velocity, gateSamples); }
    virtual void noteOff (int pitch) { juce::ignoreUnused (pitch); }
    virtual void allNotesOff() {}
    virtual void anyNoteOn() {}                      // every node hears that a note started (an LFO may restart)
    virtual bool isInstrument() const { return false; }

    // Plugin modules: the hosted processor, for its editor window
    virtual juce::AudioProcessor* hostedPlugin() { return nullptr; }
    virtual void captureState (Module&) {}

    // Process `n` (<= subBlock) samples in place
    virtual void process (float* l, float* r, int n, double bpm) = 0;

    // For modulators: the value to send down modOut after process()
    virtual float modValue() const { return 0.0f; }

    // UI readouts (written on the audio thread)
    std::atomic<float> display { 0.0f };   // LFO phase value, compressor gain reduction, voices playing...
    std::array<std::atomic<float>, 4> meter {};        // more readouts, per device (filter's moving cutoff, current step...)
    std::array<VoiceView, 16> views;                    // instruments: what each voice is doing
    Scope scope;                                        // output, for pictures
    std::atomic<float> inputLevel { 0.0f };            // peak going in, smoothed

    const GraphHost* host = nullptr;                   // set by the graph
    float raw (int i) const { return knob[(size_t) i].load(); }        // a stored value as it is (choices, widths)
    int choice (int i) const { return juce::roundToInt (knob[(size_t) i].load()); }
    bool on (int i) const { return knob[(size_t) i].load() > 0.5f; }

    std::array<float, subBlock> bufL {}, bufR {};

    // Set by the graph from the cables: notes only reach a node whose notes input is patched
    bool takesNotes = false;
    virtual class UtilityNode* asUtility() { return nullptr; }

protected:
    virtual float lo (int) const { return 0.0f; }
    const double sr;
};

// A utility (attenuator, mixer, inverter...) works on sound or on modulation, whichever it is patched
// to. The graph fills its inputs each sub-block; sound comes out in bufL / bufR, modulation in modValue().
class UtilityNode : public Node
{
public:
    using Node::Node;

    bool modMode = false;
    std::array<std::array<float, subBlock>, Port::maxSigIns> inL {}, inR {};
    std::array<float, Port::maxSigIns> inMod {};

    void clearInputs();
    void process (float* l, float* r, int n, double bpm) override;
    float modValue() const override { return modOut; }
    UtilityNode* asUtility() override { return this; }

protected:
    virtual void audio (float* l, float* r, int n) = 0;
    virtual float control (int n) = 0;
    float modOut = 0.0f;
};

class Graph
{
public:
    using SampleResolver = std::function<std::shared_ptr<const AudioData> (const juce::String& nameOrPath)>;

    Graph (const Patch&, double sampleRate, const SampleResolver&);

    void noteOn (int pitch, float velocity, double gateSamples);
    void noteOff (int pitch);
    void allNotesOff();

    // Mix `n` samples: `inL/inR` is the track's audio-clip signal, `outL/outR` receives the patch output
    void process (const float* inL, const float* inR, float* outL, float* outR, int n, double bpm);

    Node* find (int moduleId);
    bool hasInstrument() const { return instrument; }

    // Copy hosted plugins' state back into the patch, before saving
    void capturePluginState (Patch&);

    // Sequencer rack's Mod output (0..1): added to the instrument's character knob
    // (Synth cutoff, Sampler start, Drum Rack tune) each sub-block.
    std::atomic<float> externalMod { 0.0f };

    // The song position for the next process() call; it advances by itself through the call
    void setTransport (double beat, double beatsPerSample, bool playing, int blockPos)
    {
        host.beat = beat; host.beatsPerSample = beatsPerSample; host.playing = playing; host.blockPos = blockPos;
    }
    GraphHost host;

private:
    struct Link { Node* from; int fromPort; Node* to; int toPort; bool mod; };
    std::vector<std::unique_ptr<Node>> nodes;   // in processing order
    std::vector<Link> links;
    Node* input = nullptr;
    Node* output = nullptr;
    bool instrument = false;

    void processSubBlock (const float* inL, const float* inR, float* outL, float* outR, int n, double bpm);
};

std::unique_ptr<Node> createNode (const Module&, double sampleRate, const Graph::SampleResolver&);
std::unique_ptr<Node> createDeviceNode (const Module&, double sampleRate, const Graph::SampleResolver&);   // built-in instruments and effects (DeviceNodes.cpp), nullptr for others
