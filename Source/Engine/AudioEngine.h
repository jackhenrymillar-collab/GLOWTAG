#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include "Project.h"

// Plays the arrangement: MIDI clips trigger the built-in voices sample-accurately,
// audio clips stream their files, and incoming MIDI plays (and can record into) the
// selected track. Transport: play from anywhere, loop region, metronome.
class AudioEngine : public juce::AudioSource
{
public:
    explicit AudioEngine (Project& p) : project (p) { midiInput.reset (44100.0); }

    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
    void releaseResources() override {}
    void getNextAudioBlock (const juce::AudioSourceChannelInfo& info) override;

    // Transport (message thread)
    void play();
    void stop();                 // stop returns to where play started; stop again to go to the start
    bool isPlaying() const       { return playing.load(); }
    double getPosition() const   { return position.load(); }   // beats
    void setPosition (double beats);

    std::atomic<bool> looping { true };
    std::atomic<double> loopStart { 0.0 }, loopEnd { 16.0 * beatsPerBar };
    std::atomic<bool> metronome { false };
    std::atomic<bool> recordArmed { false };
    std::atomic<int> liveTrack { 4 };     // track that live MIDI plays and records into

    // Live MIDI from hardware inputs and the computer keyboard
    juce::MidiMessageCollector midiInput;

    // Loudness of the last kick, for the music-reactive glow (0..1)
    float getPulse() const { return pulse.load(); }

    // Notes played while recording, read back on the message thread
    struct RecordedEvent { double beat; int pitch; float velocity; bool noteOn; };
    int readRecorded (RecordedEvent* dest, int max);

private:
    struct Voice
    {
        bool active = false;
        Sound sound = Sound::Silent;
        int track = 0;
        int pitch = 60;
        float velocity = 1.0f;
        double t = 0.0;
        double phase = 0.0, phase2 = 0.0;
        float freq = 0.0f;
        float lp = 0.0f;
        float prevNoise = 0.0f;
        double gateSamples = 0.0;     // samples until release; < 0 = held (live note)
        bool releasing = false;
        float env = 0.0f;
        bool click = false;           // metronome tick
        bool accent = false;
    };

    struct Event { int offset; int track; int pitch; float velocity; double lengthBeats; bool click; bool accent; };

    void startVoice (const Event&);
    void releaseLiveNote (int track, int pitch);
    float renderVoice (Voice& v);
    void mixAudioClips (juce::AudioBuffer<float>&, int startSample, int numSamples, double startBeat, double beatsPerSample,
                        std::array<float, 32>& peaks);

    Project& project;
    double sr = 44100.0;
    std::atomic<bool> playing { false };
    std::atomic<double> position { 0.0 };
    std::atomic<double> requestedPosition { -1.0 };
    double playStartPosition = 0.0;
    std::atomic<float> pulse { 0.0f };
    std::array<Voice, 96> voices {};
    std::vector<Event> events;
    juce::MidiBuffer liveMidi;
    juce::Random random;

    juce::AbstractFifo recordFifo { 1024 };
    std::array<RecordedEvent, 1024> recordBuffer {};
    void pushRecorded (const RecordedEvent&);
};
