#include "MainComponent.h"

MainComponent::MainComponent()
{
    setLookAndFeel (&lookAndFeel);
    juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

    for (auto* c : std::initializer_list<juce::Component*> { &topBar, &browser, &trackList, &arranger, &devices, &mixer, &editor, &status })
        addAndMakeVisible (c);

    topBar.onModeChange = [this] (int m) { setMode ((Mode) m); };
    topBar.onPlayStop = [this] { togglePlay(); };
    topBar.onCommandPalette = [this] { showCommandPalette(); };
    arranger.onOpenEditor = [this] { setMode (Mode::Edit); };
    browser.onAddDevice = [this] (ModuleType t) { if (mode == Mode::Mix) setMode (Mode::Arrange); devices.addModule (t); };

    Theme::get().addChangeListener (this);
    ctx.addChangeListener (this);

    setWantsKeyboardFocus (true);
    setMode (Mode::Arrange);
    setSize (1680, 1040);

    // Output only for now; recording inputs arrive with the core engine step
    setAudioChannels (0, 2);
    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    stopTimer();
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

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::spaceKey)                         { togglePlay(); return true; }
    if (key == juce::KeyPress ('k', juce::ModifierKeys::commandModifier, 0)) { showCommandPalette(); return true; }
    if (key.getTextCharacter() == '1')                           { setMode (Mode::Arrange); return true; }
    if (key.getTextCharacter() == '2')                           { setMode (Mode::Mix); return true; }
    if (key.getTextCharacter() == '3')                           { setMode (Mode::Edit); return true; }
    if (key == juce::KeyPress::upKey)                            { ctx.selectTrack (ctx.selectedTrack - 1); return true; }
    if (key == juce::KeyPress::downKey)                          { ctx.selectTrack (ctx.selectedTrack + 1); return true; }
    return false;
}

void MainComponent::timerCallback()
{
    topBar.tick();
    if (arranger.isVisible()) arranger.tick();
    if (trackList.isVisible()) trackList.tick();
    if (devices.isVisible()) devices.tick();
    if (mixer.isVisible()) mixer.tick();
    if (editor.isVisible()) editor.tick();

    if (++statusCounter % 15 == 0)
    {
        juce::String info = "No audio device";
        if (auto* device = deviceManager.getCurrentAudioDevice())
            info = device->getName() + "   " + juce::String (device->getCurrentSampleRate() / 1000.0, 1) + " KHZ   "
                 + juce::String (device->getCurrentBufferSizeSamples()) + " SAMPLES   CPU "
                 + juce::String (juce::roundToInt (deviceManager.getCpuUsage() * 100.0)) + "%";
        status.setInfo (info.toUpperCase());
    }
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
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
            devices.setBounds (r.removeFromBottom (276));
            trackList.setBounds (r.removeFromLeft (210));
            arranger.setBounds (r);
            break;
        case Mode::Mix:
            mixer.setBounds (r);
            break;
        case Mode::Edit:
            devices.setBounds (r.removeFromBottom (276));
            editor.setBounds (r);
            break;
    }
}
