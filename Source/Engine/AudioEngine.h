#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include "Project.h"
#include "Graph.h"
#include <map>

// Plays the arrangement through each track's modular patch: MIDI clips and live MIDI drive the
// instruments in the patch, audio clips enter at Track In, and the patch output goes to the mixer.
// Transport: play from anywhere, loop region, metronome, recording of live notes.
class AudioEngine : public juce::AudioSource
{
public:
    explicit AudioEngine (Project& p);

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

    // Patches (message thread). Call rebuildTrack after changing a track's modules or cables;
    // knob turns go straight to the running graph through setKnob.
    void rebuildTrack (int track);
    void rebuildAll();
    void setKnob (int track, int moduleId, int knob, float value);
    Node* liveNode (int track, int moduleId);          // for UI readouts; may be null

    // Sequencer racks (message thread). rebuildSequencer after changing modules or cables;
    // knobs and steps go straight to the running rack.
    void rebuildSequencer (int track);
    void setSeqKnob (int track, int moduleId, int knob, float value);
    void setSeqStep (int track, int moduleId, int step, int pitch, bool on);
    SeqNode* liveSeqNode (int track, int moduleId);    // for UI readouts; may be null
    std::shared_ptr<const AudioData> resolveSample (const juce::String& nameOrPath);

    // Loudness of the last kick-ish hit, for the music-reactive glow (0..1)
    float getPulse() const { return pulse.load(); }

    // Notes played while recording, read back on the message thread
    struct RecordedEvent { double beat; int pitch; float velocity; bool noteOn; };
    int readRecorded (RecordedEvent* dest, int max);

private:
    struct TrackSlot
    {
        juce::SpinLock lock;
        std::shared_ptr<Graph> current;     // used by the audio thread
        std::shared_ptr<Graph> pending;     // built on the message thread, picked up at the next block
        std::shared_ptr<Graph> retired;     // dropped by the audio thread, freed on the message thread
        std::shared_ptr<Graph> ui;          // the message thread's handle for knob changes
        std::shared_ptr<SeqRuntime> seq, seqPending, seqRetired, seqUi;
        bool seqWasActive = false;
        juce::AudioBuffer<float> input, output;
    };

    struct Event { int offset; int track; int pitch; float velocity; double lengthBeats; bool click; bool accent; };
    struct Click { bool active = false; double phase = 0.0; float t = 0.0f; bool accent = false; };

    void renderAudioClips (TrackSlot&, Track&, int offset, int numSamples, double startBeat, double beatsPerSample);
    void pushRecorded (const RecordedEvent&);

    Project& project;
    double sr = 44100.0;
    int blockSize = 512;
    std::vector<std::unique_ptr<TrackSlot>> slots;
    std::map<juce::String, std::shared_ptr<const AudioData>> fileCache;

    std::atomic<bool> playing { false };
    std::atomic<double> position { 0.0 };
    std::atomic<double> requestedPosition { -1.0 };
    double playStartPosition = 0.0;
    std::atomic<float> pulse { 0.0f };
    std::vector<Event> events;
    struct Segment { int startSample; double startBeat; };
    std::vector<Segment> segments;      // where the loop wrapped inside this block
    bool wasPlaying = false;
    void runSequencers (int numSamples, double beatsPerSample, bool isPlaying, bool jumped);
    std::array<Click, 4> clicks;
    juce::MidiBuffer liveMidi;

    juce::AbstractFifo recordFifo { 1024 };
    std::array<RecordedEvent, 1024> recordBuffer {};
};
