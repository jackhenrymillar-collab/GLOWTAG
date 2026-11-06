#include "MainComponent.h"

MainComponent::MainComponent()
{
    setLookAndFeel (&lookAndFeel);
    juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

    for (auto* c : std::initializer_list<juce::Component*> { &topBar, &browser, &trackList, &arranger, &devices, &mixer, &editor, &status })
        addAndMakeVisible (c);
    addChildComponent (sequencer);

    topBar.onModeChange = [this] (int m) { setMode ((Mode) m); };
    topBar.onPlayStop = [this] { togglePlay(); };
    topBar.onCommandPalette = [this] { showCommandPalette(); };
    arranger.onOpenEditor = [this] { setMode (Mode::Edit); };
    browser.onAddDevice = [this] (ModuleType t) { if (mode == Mode::Mix) setMode (Mode::Arrange); devices.addModule (t); };
    browser.onLoadSample = [this] (const juce::String& name) { if (mode == Mode::Mix) setMode (Mode::Arrange); devices.loadSample (name); };
    browser.onAddSequencer = [this] (SeqType t)
    {
        // Adding a sequencer module switches the track to its rack
        if (mode == Mode::Mix) setMode (Mode::Arrange);
        ctx.selected().rolling = true;
        ctx.clipsChanged();
        sequencer.addModule (t);
    };

    Theme::get().addChangeListener (this);
    ctx.addChangeListener (this);

    setWantsKeyboardFocus (true);
    setMode (Mode::Arrange);
    setSize (1680, 1040);

    // Audio out, plus every MIDI input (keyboards, pads) feeding the selected track
    setAudioChannels (0, 2);
    deviceManager.addMidiInputDeviceCallback ({}, &engine.midiInput);
    enableMidiInputs();
    engine.liveTrack = ctx.selectedTrack;

    stopKeyboardFocus (*this);
    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    stopTimer();
    deviceManager.removeMidiInputDeviceCallback ({}, &engine.midiInput);
    shutdownAudio();
    Theme::get().removeChangeListener (this);
    ctx.removeChangeListener (this);
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void MainComponent::prepareToPlay (int samplesPerBlockExpected, double sampleRate)
{
    engine.prepareToPlay (samplesPerBlockExpected, sampleRate);
}

void MainComponent::getNextAudioBlock (const juce::AudioSourceChannelInfo& info)
{
    engine.getNextAudioBlock (info);
}

void MainComponent::releaseResources()
{
    engine.releaseResources();
}

void MainComponent::setMode (Mode m)
{
    mode = m;
    topBar.setMode ((int) m);
    trackList.setVisible (m == Mode::Arrange);
    arranger.setVisible (m == Mode::Arrange);
    mixer.setVisible (m == Mode::Mix);
    editor.setVisible (m == Mode::Edit);
    devices.setVisible (m != Mode::Mix);
    sequencer.setVisible (rackShown && m != Mode::Mix);
    resized();
    repaint();
}

void MainComponent::togglePlay()
{
    if (engine.isPlaying())
        engine.stop();
    else
        engine.play();
}

void MainComponent::showCommandPalette()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("Transport");
    menu.addItem (1, engine.isPlaying() ? "Stop" : "Play");
    menu.addSectionHeader ("Views");
    menu.addItem (2, "Arrange", true, mode == Mode::Arrange);
    menu.addItem (3, "Mix", true, mode == Mode::Mix);
    menu.addItem (4, "Edit", true, mode == Mode::Edit);
    menu.addSectionHeader ("Theme");
    const auto& choices = Theme::get().choices();
    for (int i = 0; i < choices.size(); ++i)
        menu.addItem (100 + i, choices[i].name,
                      true, ! Theme::get().isRainbow() && Theme::get().getThemeIndex() == i);
    menu.addItem (99, "Rainbow", true, Theme::get().isRainbow());

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&topBar).withMinimumWidth (240),
                        [this] (int result)
                        {
                            if (result == 1) togglePlay();
                            else if (result >= 2 && result <= 4) setMode ((Mode) (result - 2));
                            else if (result == 99) Theme::get().setRainbow (! Theme::get().isRainbow());
                            else if (result >= 100) Theme::get().setThemeIndex (result - 100);
                        });
}

// Children never take keyboard focus, so Space, note keys and shortcuts always reach the window
void MainComponent::stopKeyboardFocus (juce::Component& c)
{
    for (auto* child : c.getChildren())
    {
        child->setWantsKeyboardFocus (false);
        child->setMouseClickGrabsKeyboardFocus (false);
        stopKeyboardFocus (*child);
    }
}

int MainComponent::keyToSemitone (juce::juce_wchar c)
{
    static const juce::String keys ("awsedftgyhujkolp");
    return keys.indexOfChar (juce::CharacterFunctions::toLowerCase (c));
}

void MainComponent::sendLiveNote (int pitch, bool on)
{
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    engine.midiInput.addMessageToQueue ((on ? juce::MidiMessage::noteOn (1, pitch, (juce::uint8) 100)
                                            : juce::MidiMessage::noteOff (1, pitch)).withTimeStamp (now));
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    if (key == juce::KeyPress::spaceKey)                         { togglePlay(); return true; }
    if (key == juce::KeyPress ('k', juce::ModifierKeys::commandModifier, 0)) { showCommandPalette(); return true; }
    if (key == juce::KeyPress ('d', juce::ModifierKeys::commandModifier, 0)) { arranger.duplicateSelectedClip(); return true; }
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        if (mode == Mode::Edit) editor.deleteSelectedNote();
        else arranger.deleteSelectedClip();
        return true;
    }
    if (key == juce::KeyPress::upKey)                            { ctx.selectTrack (ctx.selectedTrack - 1); return true; }
    if (key == juce::KeyPress::downKey)                          { ctx.selectTrack (ctx.selectedTrack + 1); return true; }
    if (key == juce::KeyPress::homeKey || key == juce::KeyPress::returnKey) { engine.setPosition (0.0); return true; }

    if (mods.isCommandDown() || mods.isCtrlDown() || mods.isAltDown())
        return false;

    const auto ch = key.getTextCharacter();
    if (ch == '1') { setMode (Mode::Arrange); return true; }
    if (ch == '2') { setMode (Mode::Mix); return true; }
    if (ch == '3') { setMode (Mode::Edit); return true; }
    if (ch == 'z' || ch == 'Z') { keyboardOctave = juce::jmax (1, keyboardOctave - 1); return true; }
    if (ch == 'x' || ch == 'X') { keyboardOctave = juce::jmin (8, keyboardOctave + 1); return true; }
    if (ch == 'r' || ch == 'R') { engine.recordArmed = ! engine.recordArmed.load(); return true; }

    // Note keys: play on press, ignore auto-repeat
    const int semi = keyToSemitone (ch);
    if (semi >= 0)
    {
        const auto lower = juce::CharacterFunctions::toLowerCase (ch);
        if (heldKeys.find (lower) == heldKeys.end())
        {
            const int pitch = juce::jlimit (0, 127, keyboardOctave * 12 + semi);
            heldKeys[lower] = pitch;
            sendLiveNote (pitch, true);
        }
        return true;
    }
    return false;
}

bool MainComponent::keyStateChanged (bool isKeyDown)
{
    if (isKeyDown)
        return false;

    // A key went up: release any note whose key is no longer held
    bool used = false;
    for (auto it = heldKeys.begin(); it != heldKeys.end();)
    {
        const auto c = it->first;
        const bool down = juce::KeyPress::isKeyCurrentlyDown ((int) c)
                       || juce::KeyPress::isKeyCurrentlyDown ((int) juce::CharacterFunctions::toUpperCase (c));
        if (! down)
        {
            sendLiveNote (it->second, false);
            it = heldKeys.erase (it);
            used = true;
        }
        else
            ++it;
    }
    return used;
}

void MainComponent::enableMidiInputs()
{
    const auto midiDevices = juce::MidiInput::getAvailableDevices();
    for (auto& d : midiDevices)
        if (! deviceManager.isMidiInputDeviceEnabled (d.identifier))
            deviceManager.setMidiInputDeviceEnabled (d.identifier, true);
    midiInputCount = midiDevices.size();
}

void MainComponent::addRecordedNote (int pitch, double start, double length, float velocity)
{
    auto& track = *project.tracks[(size_t) juce::jlimit (0, (int) project.tracks.size() - 1, engine.liveTrack.load())];
    if (length <= 0.0)
        length = 0.25;

    const juce::SpinLock::ScopedLockType sl (project.lock);

    int idx = track.clipAt (start);
    if (idx < 0)
    {
        // New take: a one-bar clip that grows as you play
        Clip clip;
        clip.start = std::floor (start / beatsPerBar) * beatsPerBar;
        clip.length = clip.loopLength = beatsPerBar;
        clip.name = "Take " + juce::String (++takeCount);
        track.clips.push_back (clip);
        idx = (int) track.clips.size() - 1;
    }

    auto& clip = track.clips[(size_t) idx];
    double rel = start - clip.start;
    const bool looped = clip.loopLength < clip.length - 1e-6;
    if (looped)
        rel = std::fmod (rel, clip.loopLength);
    else if (rel + length > clip.length)
    {
        clip.length = std::ceil ((rel + length) / beatsPerBar) * beatsPerBar;
        clip.loopLength = clip.length;
    }
    clip.notes.push_back ({ rel, juce::jmin (length, clip.loopLength - rel), pitch, juce::jmax (0.05f, velocity) });
}

void MainComponent::drainRecording()
{
    AudioEngine::RecordedEvent events[256];
    const int n = engine.readRecorded (events, 256);
    bool changed = false;

    for (int i = 0; i < n; ++i)
    {
        const auto& e = events[i];
        if (e.noteOn)
            recordingHeld[e.pitch] = { e.beat, e.velocity };
        else if (auto it = recordingHeld.find (e.pitch); it != recordingHeld.end())
        {
            double length = e.beat - it->second.first;
            if (length < 0.0)   // the loop wrapped while the key was held
                length = engine.loopEnd.load() - it->second.first;
            addRecordedNote (e.pitch, it->second.first, length, it->second.second);
            recordingHeld.erase (it);
            changed = true;
        }
    }

    // Stopped with keys still down: close those notes
    if (! engine.isPlaying() && ! recordingHeld.empty())
    {
        for (auto& [pitch, held] : recordingHeld)
            addRecordedNote (pitch, held.first, 0.5, held.second);
        recordingHeld.clear();
        changed = true;
    }

    if (changed)
        ctx.clipsChanged();
}

void MainComponent::timerCallback()
{
    topBar.tick();
    if (arranger.isVisible()) arranger.tick();
    if (trackList.isVisible()) trackList.tick();
    if (devices.isVisible()) devices.tick();
    if (sequencer.isVisible()) sequencer.tick();
    if (mixer.isVisible()) mixer.tick();
    if (editor.isVisible()) editor.tick();

    drainRecording();

    if (statusCounter % 60 == 0)
    {
        enableMidiInputs();
        stopKeyboardFocus (*this);   // views add controls as you work
        if (! hasKeyboardFocus (true) && isShowing())
            grabKeyboardFocus();
    }

    if (++statusCounter % 15 == 0)
    {
        juce::String info = "No audio device";
        if (auto* device = deviceManager.getCurrentAudioDevice())
            info = device->getName() + "   " + juce::String (device->getCurrentSampleRate() / 1000.0, 1) + " KHZ   "
                 + juce::String (device->getCurrentBufferSizeSamples()) + " SAMPLES   CPU "
                 + juce::String (juce::roundToInt (deviceManager.getCpuUsage() * 100.0)) + "%";
        info << "   MIDI IN " << midiInputCount << "   KEYS OCTAVE " << (keyboardOctave - 1);
        status.setInfo (info.toUpperCase());
    }
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // The sequencer rack appears under the arrangement when the selected track is rolling
    const bool wantRack = ctx.selected().rolling.load();
    if (wantRack != rackShown)
    {
        rackShown = wantRack;
        sequencer.setVisible (rackShown && mode != Mode::Mix);
        resized();
    }

    // Theme or selection changed: refresh colours and redraw everything
    lookAndFeel.refreshColours();
    sendLookAndFeelChange();
    repaint();
    for (auto* c : getChildren())
        c->repaint();
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (Theme::get().bg);
}

void MainComponent::resized()
{
    auto r = getLocalBounds();
    topBar.setBounds (r.removeFromTop (56));
    status.setBounds (r.removeFromBottom (26));
    browser.setBounds (r.removeFromLeft (260));

    switch (mode)
    {
        case Mode::Arrange:
            devices.setBounds (r.removeFromBottom (rackShown ? 240 : 276));
            if (rackShown) sequencer.setBounds (r.removeFromBottom (SequencerPanel::preferredHeight));
            trackList.setBounds (r.removeFromLeft (210));
            arranger.setBounds (r);
            break;
        case Mode::Mix:
            mixer.setBounds (r);
            break;
        case Mode::Edit:
            devices.setBounds (r.removeFromBottom (rackShown ? 240 : 276));
            if (rackShown) sequencer.setBounds (r.removeFromBottom (SequencerPanel::preferredHeight));
            editor.setBounds (r);
            break;
    }
}
