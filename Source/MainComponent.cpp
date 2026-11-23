#include "MainComponent.h"
#include "Engine/PluginHost.h"

namespace
{
    enum MenuIds { menuNew = 1, menuOpen, menuSave, menuSaveAs, menuClearRecent, menuArrange, menuMix, menuEdit, menuPlay, menuRecord,
                   menuMetronome, menuPalette, menuWall, menuRecentBase = 100 };
}

MainComponent::MainComponent()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "GLOWTAG";
    options.filenameSuffix = "settings";
    options.folderName = AppData::dir().getFullPathName();
    options.osxLibrarySubFolder = "Application Support";
    settings = std::make_unique<juce::PropertiesFile> (AppData::dir().getChildFile ("settings.xml"), options);
    recent.setMaxNumberOfItems (10);
    recent.restoreFromString (settings->getValue ("recent"));

    setLookAndFeel (&lookAndFeel);
    juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

    for (auto* c : std::initializer_list<juce::Component*> { &topBar, &browser, &trackList, &arranger, &devices, &mixer, &editor, &status })
        addAndMakeVisible (c);
    addChildComponent (sequencer);

    // Every view paints all of its own area, so nothing behind it needs painting first
    setOpaque (true);
    for (auto* c : std::initializer_list<juce::Component*> { &topBar, &browser, &trackList, &arranger, &devices, &mixer, &editor, &status,
                                                              &sequencer, &wall, &expanded })
        c->setOpaque (true);

    topBar.onModeChange = [this] (int m) { setMode ((Mode) m); };
    topBar.onPlayStop = [this] { togglePlay(); };
    topBar.onCommandPalette = [this] { showCommandPalette(); };
    arranger.onOpenEditor = [this] { setMode (Mode::Edit); };
    browser.onAddDevice = [this] (ModuleType t) { devices.addModule (t); };
    browser.onLoadSample = [this] (const juce::String& name) { devices.loadSample (name); };
    browser.onAddSequencer = [this] (SeqType t) { addSequencerModule (t); };
    browser.onAddPlugin = [this] (const juce::PluginDescription& d) { devices.addPlugin (d); };

    // The device wall covers the whole window while Tab is held
    addChildComponent (wall);
    devices.onFullScreenChanged = [this] { resized(); };
    wall.onLoad = [this] (int index) { loadFromWall (index); };
    wall.targetName = [this] { return ctx.selected().name; };
    wall.onClosed = [this] { settings->setValue ("deviceWall", wall.state()); };
    wall.onReleaseFocus = [this] { grabKeyboardFocus(); };
    wall.restoreState (settings->getValue ("deviceWall"));

    // A device's expanded view covers the window until Esc
    addChildComponent (expanded);
    expanded.lookFromVar (juce::JSON::parse (settings->getValue ("expandedView")));
    devices.onOpenExpanded = [this] (int track, int moduleId) { expanded.open (track, moduleId); };
    expanded.onClose = [this] { devices.refresh(); grabKeyboardFocus(); };
    expanded.onLookChanged = [this] { settings->setValue ("expandedView", juce::JSON::toString (expanded.lookToVar(), true)); };
    expanded.onPatchChanged = [this] (int track) { engine.rebuildTrack (track); };

    // Look: theme colour, Rainbow and the vintage console come back as they were left
    Theme::get().setThemeIndex (settings->getIntValue ("theme", 0));
    Theme::get().setRainbow (settings->getBoolValue ("rainbow", false));
    Theme::get().setVintage (settings->getBoolValue ("vintageConsole", false));
    ctx.beforeRestructure = [this] { devices.closePluginWindows(); };
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

   #if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu (this);
   #endif

    // Carry on from the last project when it is still there
    if (const auto last = lastFile(); last.existsAsFile())
        openFile (last);

    stopKeyboardFocus (*this);
    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    stopTimer();
   #if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu (nullptr);
   #endif
    devices.closePluginWindows();
    settings->setValue ("theme", Theme::get().getThemeIndex());
    settings->setValue ("rainbow", Theme::get().isRainbow());
    settings->setValue ("vintageConsole", Theme::get().isVintage());
    settings->setValue ("recent", recent.toString());
    settings->setValue ("deviceWall", wall.state());
    settings->saveIfNeeded();
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
    devices.setVisible (true);
    sequencer.setVisible (rackShown && m != Mode::Mix);
    resized();
    repaint();
}

void MainComponent::addSequencerModule (SeqType t)
{
    // Adding a sequencer module switches the track to its rack
    if (mode == Mode::Mix) setMode (Mode::Arrange);
    if (! ctx.selectedIsTrack()) ctx.selectTrack (0);
    ctx.selected().rolling = true;
    ctx.clipsChanged();
    sequencer.addModule (t);
}

void MainComponent::loadFromWall (int deviceIndex)
{
    const auto& d = Wall::device (deviceIndex);
    if (d.sequencer) addSequencerModule (d.seq);      // Clock and Out are in every rack already: this opens it
    else             devices.addModule (d.module);
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
    menu.addSectionHeader ("File");
    menu.addItem (10, "New project");
    menu.addItem (11, "Open project...");
    menu.addItem (12, "Save");
    menu.addItem (13, "Save as...");
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
                            else if (result == 10) newProject();
                            else if (result == 11) openProject();
                            else if (result == 12) saveProject (false);
                            else if (result == 13) saveProject (true);
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
        if (dynamic_cast<DeviceWall*> (child) != nullptr)
            continue;                                   // it has a title box you type into
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

    // Tab shows the device wall for as long as it is held (a quick tap leaves it up)
    if (key.isKeyCode (juce::KeyPress::tabKey) && ! mods.isCommandDown() && ! mods.isCtrlDown() && ! mods.isAltDown())
    {
        wall.tabPressed();
        return true;
    }
    if (wall.isOpen())
    {
        if (wall.keyUsed (key))
            return true;
        // Behind the wall only play / stop and the Cmd shortcuts still work
        if (key != juce::KeyPress::spaceKey && ! mods.isCommandDown())
            return true;
    }

    if (key == juce::KeyPress::escapeKey && devices.isFullScreen())  { devices.setFullScreen (false); return true; }
    if (key == juce::KeyPress::spaceKey)                         { togglePlay(); return true; }
    if (key == juce::KeyPress ('k', juce::ModifierKeys::commandModifier, 0)) { showCommandPalette(); return true; }
    if (key == juce::KeyPress ('n', juce::ModifierKeys::commandModifier, 0)) { newProject(); return true; }
    if (key == juce::KeyPress ('o', juce::ModifierKeys::commandModifier, 0)) { openProject(); return true; }
    if (key == juce::KeyPress ('s', juce::ModifierKeys::commandModifier, 0)) { saveProject (false); return true; }
    if (key == juce::KeyPress ('s', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)) { saveProject (true); return true; }
    if (key == juce::KeyPress ('S', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)) { saveProject (true); return true; }
    if (key == juce::KeyPress ('d', juce::ModifierKeys::commandModifier, 0)) { arranger.duplicateSelectedClip(); return true; }
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        if (mode == Mode::Edit) editor.deleteSelectedNote();
        else arranger.deleteSelectedClip();
        return true;
    }
    if (key == juce::KeyPress::upKey)                            { ctx.selectTrack (ctx.selectedTrack - 1); return true; }
    if (key == juce::KeyPress::downKey)                          { ctx.selectTrack (juce::jmin (ctx.selectedTrack + 1, mode == Mode::Mix ? project.numSlots() - 1 : (int) project.tracks.size() - 1)); return true; }
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

    if (! juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::tabKey))
        wall.tabReleased();

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
    // With GLOWTAG_OFFLINE_CLOCK set and no sound card, the engine is run from here so meters and the
    // playhead still move. It makes no sound; it exists for checking the interface on a headless machine.
    if (statusCounter == 0 && ! offlineClock && deviceManager.getCurrentAudioDevice() == nullptr
        && juce::SystemStats::getEnvironmentVariable ("GLOWTAG_OFFLINE_CLOCK", {}).isNotEmpty())
    {
        offlineClock = true;
        offlineBuffer.setSize (2, 512);
        engine.prepareToPlay (512, 44100.0);
    }
    if (offlineClock)
        for (int i = 0; i < 3; ++i)
        {
            juce::AudioSourceChannelInfo info (&offlineBuffer, 0, 512);
            engine.getNextAudioBlock (info);
        }

    // GLOWTAG_SHOW_DEVICE=Synth (with the offline clock) opens that device's expanded view on start,
    // and GLOWTAG_SNAPSHOT=file.png saves the window a moment later: for checking the walls headless.
    if (offlineClock && statusCounter == 20)
    {
        const auto want = juce::SystemStats::getEnvironmentVariable ("GLOWTAG_SHOW_DEVICE", {});
        for (int t = 0; want.isNotEmpty() && t < (int) project.tracks.size() && ! expanded.isOpen(); ++t)
            for (auto& m : project.tracks[(size_t) t]->patch.modules)
                if (moduleInfo (m.type).title.equalsIgnoreCase (want)) { expanded.open (t, m.id); break; }
        for (int ti = 0; want.isNotEmpty() && ! expanded.isOpen() && ti < numModuleTypes; ++ti)
            if (moduleInfo ((ModuleType) ti).title.equalsIgnoreCase (want))
            {
                const int track = ctx.selectedTrack;
                const int id = project.slot (track).patch.add ((ModuleType) ti);
                engine.rebuildTrack (track);
                expanded.open (track, id);
            }
        // GLOWTAG_MODE=1 or 2 shows the mixer or the editor
        if (const auto modeVar = juce::SystemStats::getEnvironmentVariable ("GLOWTAG_MODE", {}); modeVar.isNotEmpty())
            setMode ((Mode) juce::jlimit (0, 2, modeVar.getIntValue()));
        // GLOWTAG_CARDS="Gas Tube,Crowd" swaps the selected track's devices for those, to check the cards
        const auto cards = juce::StringArray::fromTokens (juce::SystemStats::getEnvironmentVariable ("GLOWTAG_CARDS", {}), ",", {});
        if (! cards.isEmpty())
        {
            auto& patch = project.slot (ctx.selectedTrack).patch;
            for (int i = (int) patch.modules.size(); --i >= 0;)
                if (patch.modules[(size_t) i].id != Patch::inputId && patch.modules[(size_t) i].id != Patch::outputId)
                    patch.remove (patch.modules[(size_t) i].id);
            for (auto& name : cards)
                for (int ti = 0; ti < numModuleTypes; ++ti)
                    if (moduleInfo ((ModuleType) ti).title.equalsIgnoreCase (name.trim()))
                        patch.add ((ModuleType) ti);
            patch.tidy();
            engine.rebuildTrack (ctx.selectedTrack);
            devices.refresh();
            engine.midiInput.addMessageToQueue (juce::MidiMessage::noteOn (1, 48, 0.9f).withTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001));
            engine.play();
        }
        if (expanded.isOpen()) { engine.midiInput.addMessageToQueue (juce::MidiMessage::noteOn (1, 48, 0.9f).withTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001)); engine.play(); }
    }
    if (offlineClock && statusCounter == 50)
        if (const auto shot = juce::SystemStats::getEnvironmentVariable ("GLOWTAG_SNAPSHOT", {}); shot.isNotEmpty())
        {
            juce::File f (shot);
            f.deleteFile();
            juce::FileOutputStream out (f);
            juce::PNGImageFormat().writeImageToStream (createComponentSnapshot (getLocalBounds(), true, 1.0f), out);
            out.flush();
            juce::JUCEApplication::quit();
        }

    // Without a screen refresh to follow (a hidden window, or no display), the views still keep up
    if (const auto now = juce::Time::getMillisecondCounterHiRes() * 0.001; now - lastFrameTime > 0.25)
        frame (now, false);

    drainRecording();

    if (statusCounter % 60 == 0)
    {
        enableMidiInputs();
        stopKeyboardFocus (*this);   // views add controls as you work
        if (! hasKeyboardFocus (true) && isShowing())
            grabKeyboardFocus();
    }

    if (statusCounter % 10 == 0)
        updateTitle();

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

void MainComponent::frame (double timestampSeconds, bool fromDisplay)
{
    // A 120 Hz screen is followed every other refresh: 60 even frames a second look smooth and cost half.
    // The display's own refresh time is used, so a late or early callback never drops a frame.
    static int refreshes = 0, fallbacks = 0;     // for GLOWTAG_FRAME_STATS
    if (fromDisplay) ++refreshes; else ++fallbacks;
    if (fromDisplay)
    {
        if (timestampSeconds - lastRefresh < 0.75 / 60.0 && timestampSeconds >= lastRefresh)
            return;
        lastRefresh = timestampSeconds;
    }
    const double t = juce::Time::getMillisecondCounterHiRes() * 0.001;
    Neon::setFrameTime (t - lastFrameTime);
    lastFrameTime = t;

    // GLOWTAG_FRAME_STATS=1 prints how many frames a second the views really get, for checking smoothness
    static const bool stats = juce::SystemStats::getEnvironmentVariable ("GLOWTAG_FRAME_STATS", {}).isNotEmpty();
    if (stats)
    {
        static int frames = 0;
        static double since = t;
        ++frames;
        if (t - since >= 2.0)
        {
            std::fprintf (stderr, "frames per second: %.1f  (screen refreshes %d, timer fallbacks %d, last refresh time %.4f)\n",
                          frames / (t - since), refreshes, fallbacks, timestampSeconds);
            frames = 0; refreshes = 0; fallbacks = 0;
            since = t;
        }
    }

    // A full-window view hides everything else, so only it moves
    if (expanded.isOpen()) { expanded.tick(); return; }
    if (wall.isOpen()) { wall.tick (engine.getPulse()); return; }

    topBar.tick();
    if (arranger.isVisible()) arranger.tick();
    if (trackList.isVisible()) trackList.tick();
    if (devices.isVisible()) devices.tick();
    if (sequencer.isVisible()) sequencer.tick();
    if (mixer.isVisible()) mixer.tick();
    if (editor.isVisible()) editor.tick();
    wall.warm();
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (seenGeneration != ctx.generation)
    {
        // A new song: the track list and mixer strips point at tracks that no longer exist
        seenGeneration = ctx.generation;
        trackList.rebuild();
        mixer.rebuild();
    }
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
    wall.themeChanged();
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
    wall.setBounds (r);
    expanded.setBounds (r);
    topBar.setBounds (r.removeFromTop (56));
    status.setBounds (r.removeFromBottom (26));
    browser.setBounds (r.removeFromLeft (260));

    // The device panel has two rows now; on a short window it gives some of that back
    const int rackHeight = rackShown && mode != Mode::Mix ? (int) SequencerPanel::preferredHeight : 0;
    const int deviceHeight = juce::jlimit (240, (int) DevicePanel::preferredHeight, r.getHeight() - rackHeight - 220);
    const auto whole = getLocalBounds();

    switch (mode)
    {
        case Mode::Arrange:
            devices.setBounds (r.removeFromBottom (deviceHeight));
            if (rackShown) sequencer.setBounds (r.removeFromBottom (SequencerPanel::preferredHeight));
            trackList.setBounds (r.removeFromLeft (210));
            arranger.setBounds (r);
            break;
        case Mode::Mix:
            devices.setBounds (r.removeFromBottom (deviceHeight));
            mixer.setBounds (r);
            break;
        case Mode::Edit:
            devices.setBounds (r.removeFromBottom (deviceHeight));
            if (rackShown) sequencer.setBounds (r.removeFromBottom (SequencerPanel::preferredHeight));
            editor.setBounds (r);
            break;
    }

    // Full screen: the patch covers the whole window (the device wall still opens over it)
    if (devices.isFullScreen())
    {
        devices.setBounds (whole);
        devices.toFront (false);
        if (wall.isOpen()) wall.toFront (false);
    }
}


//==============================================================================
// Files

void MainComponent::updateTitle()
{
    juce::String title = "GLOWTAG";
    title << "  —  " << (project.file == juce::File() ? juce::String ("Untitled") : project.file.getFileNameWithoutExtension());
    if (project.dirty)
        title << " •";
    title = juce::String::fromUTF8 (title.toRawUTF8());
    if (title != shownTitle)
    {
        shownTitle = title;
        if (auto* w = dynamic_cast<juce::DocumentWindow*> (getTopLevelComponent()))
            w->setName (title);
    }
}

juce::File MainComponent::lastFile() const
{
    return juce::File (settings->getValue ("lastProject"));
}

void MainComponent::rememberFile (const juce::File& f)
{
    recent.addFile (f);
    settings->setValue ("recent", recent.toString());
    settings->setValue ("lastProject", f.getFullPathName());
    settings->saveIfNeeded();
   #if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu (this);   // refresh Open Recent
   #endif
}

void MainComponent::confirmDiscard (std::function<void()> then)
{
    if (! project.dirty)
    {
        then();
        return;
    }
    const auto name = project.file == juce::File() ? juce::String ("this project") : project.file.getFileNameWithoutExtension();
    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (juce::MessageBoxIconType::QuestionIcon)
                                      .withTitle ("Save changes?")
                                      .withMessage ("Save the changes to " + name + " first?")
                                      .withButton ("Save")
                                      .withButton ("Don't save")
                                      .withButton ("Cancel")
                                      .withAssociatedComponent (this),
                                  [this, then] (int result)
                                  {
                                      if (result == 1)
                                      {
                                          // Save, then carry on once it has a file
                                          if (project.file == juce::File())
                                          {
                                              saveProject (true);
                                              juce::Timer::callAfterDelay (300, [safe = juce::Component::SafePointer<MainComponent> (this), then]
                                              {
                                                  if (safe != nullptr && ! safe->project.dirty) then();
                                              });
                                          }
                                          else
                                          {
                                              saveProject (false);
                                              if (! project.dirty) then();
                                          }
                                      }
                                      else if (result == 2)
                                          then();
                                  });
}

void MainComponent::newProject()
{
    confirmDiscard ([this]
    {
        devices.closePluginWindows();
        ctx.replaceProject ([this] { project.makeEmpty(); return true; });
        settings->setValue ("lastProject", juce::String());
        setMode (Mode::Arrange);
        updateTitle();
    });
}

void MainComponent::openProject()
{
    confirmDiscard ([this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Open project", lastFile().getParentDirectory(), "*" + Project::fileExtension());
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  if (fc.getResult().existsAsFile())
                                      openFile (fc.getResult());
                              });
    });
}

void MainComponent::openFile (const juce::File& f)
{
    devices.closePluginWindows();
    const bool ok = ctx.replaceProject ([this, f]
    {
        if (project.loadFrom (f))
            return true;
        project.makeEmpty();   // the file was not a project: start clean rather than half-loaded
        return false;
    });
    if (ok)
        rememberFile (f);
    else
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Couldn't open",
                                                f.getFileName() + " isn't a GLOWTAG project file.");
    setMode (Mode::Arrange);
    updateTitle();
}

void MainComponent::saveProject (bool saveAs)
{
    if (! saveAs && project.file != juce::File())
    {
        engine.capturePluginStates();
        if (! project.saveTo (project.file))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Couldn't save",
                                                    "GLOWTAG couldn't write " + project.file.getFullPathName());
        else
            rememberFile (project.file);
        updateTitle();
        return;
    }

    auto folder = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    if (! folder.isDirectory())
        folder = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    auto start = project.file != juce::File() ? project.file : folder.getChildFile ("Untitled" + Project::fileExtension());
    chooser = std::make_unique<juce::FileChooser> ("Save project", start, "*" + Project::fileExtension());
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (f == juce::File()) return;
                              if (! f.hasFileExtension (Project::fileExtension()))
                                  f = f.withFileExtension (Project::fileExtension());
                              engine.capturePluginStates();
                              if (project.saveTo (f))
                                  rememberFile (f);
                              else
                                  juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Couldn't save",
                                                                          "GLOWTAG couldn't write " + f.getFullPathName());
                              updateTitle();
                          });
}

void MainComponent::requestQuit()
{
    confirmDiscard ([] { juce::JUCEApplication::getInstance()->quit(); });
}

//==============================================================================
// Menu bar

juce::StringArray MainComponent::getMenuBarNames()
{
    return { "File", "View", "Transport" };
}

juce::PopupMenu MainComponent::getMenuForIndex (int index, const juce::String&)
{
    juce::PopupMenu m;
    auto item = [] (int id, const juce::String& text, const juce::String& shortcut, bool ticked = false)
    {
        juce::PopupMenu::Item i (text);
        i.itemID = id;
        i.shortcutKeyDescription = shortcut;
        i.isTicked = ticked;
        return i;
    };
    if (index == 0)
    {
        m.addItem (item (menuNew, "New Project", "Cmd+N"));
        m.addItem (item (menuOpen, "Open...", "Cmd+O"));
        juce::PopupMenu recentMenu;
        recent.createPopupMenuItems (recentMenu, menuRecentBase, true, true);
        recentMenu.addSeparator();
        recentMenu.addItem (menuClearRecent, "Clear Menu", recent.getNumFiles() > 0);
        m.addSubMenu ("Open Recent", recentMenu);
        m.addSeparator();
        m.addItem (item (menuSave, "Save", "Cmd+S"));
        m.addItem (item (menuSaveAs, "Save As...", "Shift+Cmd+S"));
    }
    else if (index == 1)
    {
        m.addItem (item (menuArrange, "Arrange", "1", mode == Mode::Arrange));
        m.addItem (item (menuMix, "Mix", "2", mode == Mode::Mix));
        m.addItem (item (menuEdit, "Edit", "3", mode == Mode::Edit));
        m.addSeparator();
        m.addItem (item (menuPalette, "Command Palette", "Cmd+K"));
        m.addItem (item (menuWall, "Device Wall", "Tab", wall.isOpen()));
    }
    else
    {
        m.addItem (item (menuPlay, engine.isPlaying() ? "Stop" : "Play", "Space"));
        m.addItem (item (menuRecord, "Record Arm", "R", engine.recordArmed.load()));
        m.addItem (item (menuMetronome, "Metronome", "", engine.metronome.load()));
    }
    return m;
}

void MainComponent::menuItemSelected (int id, int)
{
    switch (id)
    {
        case menuNew:       newProject(); break;
        case menuOpen:      openProject(); break;
        case menuSave:      saveProject (false); break;
        case menuSaveAs:    saveProject (true); break;
        case menuClearRecent: recent.clear(); rememberFile (project.file); break;
        case menuArrange:   setMode (Mode::Arrange); break;
        case menuMix:       setMode (Mode::Mix); break;
        case menuEdit:      setMode (Mode::Edit); break;
        case menuPlay:      togglePlay(); break;
        case menuRecord:    engine.recordArmed = ! engine.recordArmed.load(); break;
        case menuMetronome: engine.metronome = ! engine.metronome.load(); break;
        case menuPalette:   showCommandPalette(); break;
        case menuWall:      if (wall.isOpen()) wall.close(); else wall.open (true); break;
        default:
            if (id >= menuRecentBase)
            {
                const auto f = recent.getFile (id - menuRecentBase);
                if (f.existsAsFile())
                    confirmDiscard ([this, f] { openFile (f); });
            }
            break;
    }
}
