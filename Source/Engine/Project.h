#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <memory>
#include <vector>
#include "Patch.h"
#include "Sequencer.h"

enum class TrackKind { Audio, Sampler, Midi, Automation, Return, Master, Bus };

// Times are in beats (quarter notes). The project is in 4/4.
constexpr double beatsPerBar = 4.0;

struct Note
{
    double start = 0.0;     // beats from the start of the clip's loop
    double length = 0.25;   // beats
    int pitch = 60;
    float velocity = 0.8f;
};

// A decoded audio file plus a small overview for drawing its waveform
struct AudioData
{
    juce::AudioBuffer<float> buffer;
    double sampleRate = 44100.0;
    juce::String fileName;
    std::vector<float> peaks;  // 0..1, evenly spaced across the file
    std::vector<std::pair<float, float>> onsets;   // hits: position 0..1 in the file, strength 0..1 (the Sampler's slicer)

    void findOnsets();         // fills `onsets` from the buffer

    double lengthSeconds() const { return buffer.getNumSamples() / sampleRate; }
    static std::shared_ptr<const AudioData> load (const juce::File&);
};

enum class ClipKind { Midi, Audio };

struct Clip
{
    double start = 0.0;       // beats on the timeline
    double length = 16.0;     // beats
    double loopLength = 4.0;  // MIDI content repeats every loopLength beats
    juce::String name;
    ClipKind kind = ClipKind::Midi;
    std::vector<Note> notes;
    std::shared_ptr<const AudioData> audio;
    juce::String audioPath;   // the file the audio came from, for saving

    double end() const { return start + length; }
};

// One track. Mixer fields are atomics so the UI can change them live.
// Clips are edited on the message thread while holding Project::lock; the audio thread reads them under it.
struct Track
{
    juce::String name;
    TrackKind kind = TrackKind::Audio;
    int colourIndex = 0;           // index into the theme palette (see Pal)
    std::vector<Clip> clips;
    Patch patch;                   // modular device patch; the engine builds a Graph from it
    SeqRack seq;                   // Eurorack-style rolling sequencer (message thread only)
    std::atomic<bool> rolling { false };   // true: the rack plays this track instead of its clips

    std::atomic<bool> muted { false };
    std::atomic<bool> soloed { false };
    std::atomic<float> volume { 0.8f }; // linear gain
    std::atomic<float> pan { 0.0f };    // -1 .. 1
    std::atomic<float> meter { 0.0f };  // peak level written by the audio thread
    std::array<std::atomic<float>, 2> send { 0.0f, 0.0f };   // post-fader sends to Return A / B

    // Console channel, after the device patch and before the fader
    std::atomic<int> output { -1 };         // where the channel goes: a bus index, or -1 for the master
    std::atomic<float> trimDb { 0.0f };     // level into the channel, -12 .. +12 dB
    std::atomic<bool> phase { false };      // flip polarity
    std::atomic<bool> mono { false };       // sum to mono
    std::atomic<float> width { 1.0f };      // stereo width: 0 mono, 1 as recorded, 2 extra wide
    std::atomic<bool> analogue { false };   // console warmth; breaks up when driven hard
    std::atomic<float> heat { 0.0f };       // how hard the Analogue stage is being driven, 0..1 (audio thread)

    int clipAt (double beat) const;     // index or -1
    bool isBus() const { return kind == TrackKind::Return || kind == TrackKind::Master || kind == TrackKind::Bus; }
    bool takesInput() const { return kind == TrackKind::Bus || kind == TrackKind::Master; }   // can be patched into

    juce::var toVar() const;
    void fromVar (const juce::var&);
};

// The song: its tracks, any number of mix buses, two return buses (A and B) fed by the track sends,
// and the master. Every track, bus and return can be patched into a bus or straight into the master.
// The engine addresses all of them by slot: tracks first, then buses, Return A, Return B, Master.
class Project
{
public:
    Project();

    static constexpr int lengthBars = 64;
    static constexpr int numReturns = 2;
    static constexpr int maxBuses = 12;

    std::vector<std::unique_ptr<Track>> tracks;
    std::vector<std::unique_ptr<Track>> buses;
    std::array<std::unique_ptr<Track>, numReturns> returns;
    std::unique_ptr<Track> master;
    std::atomic<double> bpm { 124.0 };

    // Where it lives on disk, and whether it has changed since
    juce::File file;
    bool dirty = false;

    // Guards every track's clips. Hold it briefly on the message thread when changing clips.
    juce::SpinLock lock;

    int numSlots() const { return (int) tracks.size() + (int) buses.size() + numReturns + 1; }
    int busSlot (int b) const { return (int) tracks.size() + b; }
    int returnSlot (int r) const { return (int) tracks.size() + (int) buses.size() + r; }
    int masterSlot() const { return (int) tracks.size() + (int) buses.size() + numReturns; }
    int slotOf (const Track*) const;     // -1 when it is not in this project

    // Routing. A channel feeds one bus or the master; buses may feed other buses but never in a loop.
    int outputSlot (int slotIndex) const;                 // the slot this one feeds (the master feeds nothing: -1)
    bool canRoute (int fromSlot, int toSlot) const;
    bool setOutput (int fromSlot, int toSlot);            // false when it would make a loop or is not allowed
    // Adding and removing buses changes the slot layout: do it through AppContext::restructure
    int addBus (const juce::String& name = {});           // returns the bus index, or -1 when full
    void removeBus (int busIndex);                        // whatever fed it moves on to where it was going
    Track& slot (int index);
    const Track& slot (int index) const;

    bool isAudible (int slotIndex) const;

    // Start again: the demo song, or a bare song with a few empty instrument tracks
    void makeDemo();
    void makeEmpty();

    // Save and load (JSON). Loading replaces every track; tell the engine and the views afterwards.
    juce::var toVar() const;
    bool fromVar (const juce::var&);
    bool saveTo (const juce::File&);
    bool loadFrom (const juce::File&);
    static juce::String fileExtension() { return ".glowtag"; }

private:
    void makeBuses();
    void clear();
};
