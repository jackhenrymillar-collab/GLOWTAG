#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
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

constexpr int maxKnobs = 12;
constexpr int subBlock = 32;

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
    virtual bool isInstrument() const { return false; }

    // Process `n` (<= subBlock) samples in place
    virtual void process (float* l, float* r, int n, double bpm) = 0;

    // For modulators: the value to send down modOut after process()
    virtual float modValue() const { return 0.0f; }

    // UI readouts (written on the audio thread)
    std::atomic<float> display { 0.0f };   // LFO phase value, compressor gain reduction, voices playing...

    std::array<float, subBlock> bufL {}, bufR {};

protected:
    virtual float lo (int) const { return 0.0f; }
    const double sr;
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

    // Sequencer rack's Mod output (0..1): added to the instrument's character knob
    // (Synth cutoff, Sampler start, Drum Rack tune) each sub-block.
    std::atomic<float> externalMod { 0.0f };

private:
    struct Link { Node* from; int fromPort; Node* to; int toPort; };
    std::vector<std::unique_ptr<Node>> nodes;   // in processing order
    std::vector<Link> links;
    Node* input = nullptr;
    Node* output = nullptr;
    bool instrument = false;

    void processSubBlock (const float* inL, const float* inR, float* outL, float* outR, int n, double bpm);
};

std::unique_ptr<Node> createNode (const Module&, double sampleRate, const Graph::SampleResolver&);
