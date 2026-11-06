#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <memory>
#include <vector>
#include "Patch.h"

enum class TrackKind { Audio, Sampler, Midi, Automation };

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

    std::atomic<bool> muted { false };
    std::atomic<bool> soloed { false };
    std::atomic<float> volume { 0.8f }; // linear gain
    std::atomic<float> pan { 0.0f };    // -1 .. 1
    std::atomic<float> meter { 0.0f };  // peak level written by the audio thread

    int clipAt (double beat) const;     // index or -1
};

class Project
{
public:
    Project();

    static constexpr int lengthBars = 64;

    std::vector<std::unique_ptr<Track>> tracks;
    std::atomic<double> bpm { 124.0 };

    // Guards every track's clips. Hold it briefly on the message thread when changing clips.
    juce::SpinLock lock;

    bool isAudible (int trackIndex) const;
};
