#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <map>
#include "Engine/Project.h"
#include "Engine/AudioEngine.h"
#include "UI/AppContext.h"
#include "UI/GlowLookAndFeel.h"
#include "UI/TopBar.h"
#include "UI/BrowserPanel.h"
#include "UI/TrackList.h"
#include "UI/ArrangerView.h"
#include "UI/DevicePanel.h"
#include "UI/StatusBar.h"
#include "UI/MixerView.h"
#include "UI/EditorView.h"

// The whole window: owns the project, the audio engine and every view.
class MainComponent : public juce::AudioAppComponent,
                      private juce::Timer,
                      private juce::ChangeListener
{
public:
    MainComponent();
    ~MainComponent() override;

    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock (const juce::AudioSourceChannelInfo&) override;
    void releaseResources() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;

    enum class Mode { Arrange, Mix, Edit };
    void setMode (Mode);

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void togglePlay();
    void showCommandPalette();
    void enableMidiInputs();
    void drainRecording();
    void addRecordedNote (int pitch, double start, double length, float velocity);
    void sendLiveNote (int pitch, bool on);
    static void stopKeyboardFocus (juce::Component&);

    // Computer keyboard as a piano: A W S E D F T G Y H U J K O L P
    static int keyToSemitone (juce::juce_wchar);
    std::map<juce::juce_wchar, int> heldKeys;    // key -> note playing
    int keyboardOctave = 5;                      // C5 = MIDI 60 on the A key

    std::map<int, std::pair<double, float>> recordingHeld;   // pitch -> start beat, velocity
    int takeCount = 0;
    int midiInputCount = 0;

    Project project;
    AudioEngine engine { project };
    AppContext ctx { project, engine };
    GlowLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this, 600 };

    TopBar topBar { ctx };
    BrowserPanel browser { ctx };
    TrackList trackList { ctx };
    ArrangerView arranger { ctx };
    DevicePanel devices { ctx };
    MixerView mixer { ctx };
    EditorView editor { ctx };
    StatusBar status;

    Mode mode = Mode::Arrange;
    int statusCounter = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
