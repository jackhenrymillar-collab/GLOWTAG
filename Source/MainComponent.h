#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
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

    enum class Mode { Arrange, Mix, Edit };
    void setMode (Mode);

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void togglePlay();
    void showCommandPalette();

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
