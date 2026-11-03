#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <array>
#include <vector>

// Built-in sounds the demo engine can play until the real instruments land.
enum class Sound { Kick, Snare, Hat, Clap, Chop, Bass, Keys, Silent };

enum class TrackKind { Audio, Sampler, Midi, Automation };

struct Clip
{
    int startBar = 0;
    int lengthBars = 4;
    juce::String name;
};

// One track. Fields the audio thread reads are atomics so the UI can change them live.
struct Track
{
    juce::String name;
    TrackKind kind = TrackKind::Audio;
    int colourIndex = 0;           // index into the theme palette (see Pal)
    Sound sound = Sound::Silent;
    std::vector<Clip> clips;

    std::atomic<uint16_t> steps { 0 }; // 16-step pattern, bit n = step n
    std::atomic<bool> muted { false };
    std::atomic<bool> soloed { false };
    std::atomic<float> volume { 0.8f }; // linear gain
    std::atomic<float> pan { 0.0f };    // -1 .. 1
    std::atomic<float> meter { 0.0f };  // peak level written by the audio thread

    bool stepOn (int step) const { return (steps.load() >> step) & 1u; }
};

class Project
{
public:
    Project();

    static constexpr int numBars = 16;
    static constexpr int stepsPerBar = 16;

    std::vector<std::unique_ptr<Track>> tracks;
    std::atomic<double> bpm { 124.0 };

    bool isAudible (int trackIndex) const;

    static uint16_t patternFromString (const char* sixteenChars);
};
