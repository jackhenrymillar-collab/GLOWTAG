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
#include "UI/SequencerPanel.h"
#include "UI/StatusBar.h"
#include "UI/MixerView.h"
#include "UI/EditorView.h"
#include "UI/DeviceWall.h"
#include "UI/ExpandedDevice.h"
#include "UI/Visualiser.h"
#include "UI/AutomationPanels.h"

// The whole window: owns the project, the audio engine and every view.
// Also the application's menu bar model (File / View / Transport) on the Mac.
class MainComponent : public juce::AudioAppComponent,
                      public juce::MenuBarModel,
                      private juce::Timer,
                      private juce::ChangeListener
{
public:
    MainComponent();
    ~MainComponent() override;

    // Files. Each asks about unsaved changes first where that matters.
    void newProject();
    void openProject();
    void openFile (const juce::File&);
    void saveProject (bool saveAs);
    void requestQuit();              // asks to save if needed, then quits

    // MenuBarModel
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int, const juce::String&) override;
    void menuItemSelected (int, int) override;

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
    void timerCallback() override;              // housekeeping, 30 times a second: recording, MIDI, the status line
    void frame (double timestampSeconds, bool fromDisplay);   // drawing, once per screen refresh (at most 60 a second)
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void togglePlay();
    void showCommandPalette();
    void showVisualMenu();                      // the visualiser's views and what it listens to
    void visualiserViewChanged();
    void toggleLanes();                         // automation lanes over the clips, on or off
    void rememberAutomationSettings();
    int seenLaneRequests = 0;
    void addSequencerModule (SeqType);          // onto the selected track's rack, which it opens
    void loadFromWall (int deviceIndex);
    void enableMidiInputs();
    void drainRecording();
    void confirmDiscard (std::function<void()> then);   // save / don't save / cancel when the project is dirty
    void updateTitle();
    void rememberFile (const juce::File&);
    juce::File lastFile() const;
    std::unique_ptr<juce::PropertiesFile> settings;
    juce::RecentlyOpenedFilesList recent;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String shownTitle;
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
    SequencerPanel sequencer { ctx };
    MixerView mixer { ctx };
    EditorView editor { ctx };
    StatusBar status;
    DeviceWall wall;                            // hold Tab: every device as neon on a brick wall
    ExpandedDevice expanded { ctx };            // double-click a device: its controls as a neon wall
    Visualiser visualiser { ctx, *this };       // the picture that follows the song: full window, or behind everything
    AutomationChooser laneChooser { ctx };      // a track header's "device > control": what its lane moves
    SettingsPanel settingsPanel { ctx };

    Mode mode = Mode::Arrange;
    bool rackShown = false;
    int seenGeneration = 0;
    int statusCounter = 0;
    bool offlineClock = false;                  // no audio device: run the engine from the UI timer (for screenshots)
    juce::AudioBuffer<float> offlineBuffer;

    // Animation follows the display's own refresh, so moving things land on every frame evenly.
    // Declared last: it calls into every view above.
    double lastFrameTime = 0.0, lastRefresh = 0.0;   // our clock; the display's own clock
    juce::VBlankAttachment vblank { this, [this] (double t) { frame (t, true); } };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
