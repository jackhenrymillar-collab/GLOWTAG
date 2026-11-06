#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "Project.h"

// Plays the project's step patterns with simple built-in drum and synth voices.
// This is the foundation the real sampler, instruments and effects plug into later.
class AudioEngine : public juce::AudioSource
{
public:
    explicit AudioEngine (Project& p) : project (p) {}

    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
    void releaseResources() override {}
    void getNextAudioBlock (const juce::AudioSourceChannelInfo& info) override;

    void play();
    void stop();
    bool isPlaying() const { return playing.load(); }

    // Position in 16th-note steps since play was pressed (-1 when stopped)
    int64_t getStepPosition() const { return stepPosition.load(); }
    double getSecondsPlayed() const;

    // Overall loudness of the last kick, for the music-reactive glow (0..1)
    float getPulse() const { return pulse.load(); }

private:
    struct Voice
    {
        bool active = false;
        Sound sound = Sound::Silent;
        int track = 0;
        double t = 0.0;
        double phase = 0.0;
        float freq = 0.0f;
        float lp = 0.0f;     // one-pole filter state
        float prevNoise = 0.0f;
    };

    void triggerStep (int step);
    void startVoice (Sound s, int track, float freq);
    float renderVoice (Voice& v);

    Project& project;
    double sr = 44100.0;
    std::atomic<bool> playing { false };
    std::atomic<int64_t> stepPosition { -1 };
    std::atomic<float> pulse { 0.0f };
    double samplesUntilNextStep = 0.0;
    int64_t samplesPlayed = 0;
    std::array<Voice, 64> voices {};
    juce::Random random;
};
