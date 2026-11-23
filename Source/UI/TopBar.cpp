#include "TopBar.h"

TopBar::TopBar (AppContext& c) : ctx (c)
{
    const char* names[] = { "Arrange", "Mix", "Edit" };
    for (int i = 0; i < 3; ++i)
    {
        auto& b = modeButtons[i];
        b.setButtonText (names[i]);
        b.setRadioGroupId (1001);
        b.setClickingTogglesState (true);
        b.getProperties().set ("pal", (int) Pal::Green);
        b.onClick = [this, i] { if (modeButtons[i].getToggleState() && onModeChange) onModeChange (i); };
        addAndMakeVisible (b);
    }
    modeButtons[0].setToggleState (true, juce::dontSendNotification);

    playButton.onClick = [this] { if (onPlayStop) onPlayStop(); };
    playButton.setTooltip ("Play / stop (Space)");
    stopButton.onClick = [this] { ctx.engine.stop(); };
    stopButton.setTooltip ("Stop. Press again to go back to the start");
    recButton.setClickingTogglesState (true);
    recButton.setTooltip ("Record: play notes on a MIDI keyboard (or the A-K keys) while playing");
    recButton.onClick = [this] { ctx.engine.recordArmed = recButton.getToggleState(); };
    loopButton.setClickingTogglesState (true);
    loopButton.setTooltip ("Loop the region marked on the ruler");
    loopButton.onClick = [this] { ctx.engine.looping = loopButton.getToggleState(); };
    metroButton.setClickingTogglesState (true);
    metroButton.setTooltip ("Metronome");
    metroButton.onClick = [this] { ctx.engine.metronome = metroButton.getToggleState(); };
    for (auto* b : { &playButton, &stopButton, &recButton, &loopButton, &metroButton })
        addAndMakeVisible (*b);
    playButton.wordSize = stopButton.wordSize = recButton.wordSize = 28.0f;
    loopButton.wordSize = metroButton.wordSize = 19.0f;
    for (auto& b : modeButtons) b.getProperties().set ("wordSize", 20.0f);
    rainbowButton.getProperties().set ("wordSize", 18.0f);
    commandButton.getProperties().set ("wordSize", 18.0f);

    rainbowButton.setClickingTogglesState (true);
    rainbowButton.getProperties().set ("pal", (int) Pal::Gold);
    rainbowButton.onClick = [this] { Theme::get().setRainbow (rainbowButton.getToggleState()); };
    addAndMakeVisible (rainbowButton);

    commandButton.setTooltip ("Command palette");
    commandButton.onClick = [this] { if (onCommandPalette) onCommandPalette(); };
    addAndMakeVisible (commandButton);
}

void TopBar::setMode (int mode)
{
    modeButtons[juce::jlimit (0, 2, mode)].setToggleState (true, juce::dontSendNotification);
}

void TopBar::tick()
{
    playButton.setToggleState (ctx.engine.isPlaying(), juce::dontSendNotification);
    recButton.setToggleState (ctx.engine.recordArmed.load(), juce::dontSendNotification);
    loopButton.setToggleState (ctx.engine.looping.load(), juce::dontSendNotification);
    metroButton.setToggleState (ctx.engine.metronome.load(), juce::dontSendNotification);
    rainbowButton.setToggleState (Theme::get().isRainbow(), juce::dontSendNotification);

    // Only what changed is drawn again
    const float rec = ctx.engine.recordArmed.load() && ctx.engine.isPlaying() ? 1.0f : 0.0f;
    if (! juce::approximatelyEqual (recButton.glow, rec)) { recButton.glow = rec; recButton.repaint(); }
    const float pulse = ctx.engine.getPulse();
    if (std::abs (playButton.glow - pulse) > 0.01f || (pulse == 0.0f && playButton.glow != 0.0f)) { playButton.glow = pulse; playButton.repaint(); }

    // The readouts change when the shown bar, beat, time (to the hundredth) or tempo does
    const double beats = ctx.playheadBeats();
    const double secs = beats * 60.0 / ctx.project.bpm.load();
    const auto shown = (juce::int64) std::floor (beats * 4.0) * 7919 + (juce::int64) std::floor (secs * 100.0) * 104729
                     + (juce::int64) std::round (ctx.project.bpm.load() * 10.0);
    if (shown != shownReadout)
    {
        shownReadout = shown;
        repaint (lcdArea);
    }
}

juce::Rectangle<float> TopBar::swatchBounds (int index) const
{
    return { (float) swatchArea.getX() + (float) index * 22.0f, (float) swatchArea.getCentreY() - 8.0f, 16.0f, 16.0f };
}

void TopBar::resized()
{
    auto r = getLocalBounds().reduced (14, 6);

    logoArea = r.removeFromLeft (170);
    r.removeFromLeft (6);
    const int modeW[] = { 92, 52, 54 };
    for (int i = 0; i < 3; ++i)
        modeButtons[i].setBounds (r.removeFromLeft (modeW[i]));

    commandButton.setBounds (r.removeFromRight (66));
    r.removeFromRight (6);
    rainbowButton.setBounds (r.removeFromRight (84));
    r.removeFromRight (6);
    swatchArea = r.removeFromRight (Theme::get().choices().size() * 22);

    // Transport words and the readouts sit together in the middle
    const int transportW = 72 + 76 + 58 + 22 + 380 + 16 + 56 + 62;
    auto mid = r.withSizeKeepingCentre (juce::jmin (transportW, r.getWidth()), r.getHeight());
    playButton.setBounds (mid.removeFromLeft (72));
    stopButton.setBounds (mid.removeFromLeft (76));
    recButton.setBounds (mid.removeFromLeft (58));
    mid.removeFromLeft (22);
    metroButton.setBounds (mid.removeFromRight (62));
    loopButton.setBounds (mid.removeFromRight (56));
    mid.removeFromRight (16);
    lcdArea = mid;
    tempoArea = lcdArea.withWidth (90);
}

void TopBar::paint (juce::Graphics& g)
{
    auto& th = Theme::get();
    g.fillAll (th.panel);
    g.setColour (th.line2);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

    const auto c = th.themeColour();
    const double beats = ctx.playheadBeats();
    const int bar = (int) (beats / beatsPerBar) + 1;
    const int beat = (int) std::fmod (beats, beatsPerBar) + 1;
    const int sixteenth = (int) std::fmod (beats * 4.0, 4.0) + 1;
    const double secs = beats * 60.0 / ctx.project.bpm.load();
    struct Cell { juce::String value, caption; float width; };
    const Cell cells[] = {
        { juce::String (ctx.project.bpm.load(), 1), "bpm", 90.0f },
        { "4/4", "sig", 60.0f },
        { juce::String (bar).paddedLeft ('0', 3) + "." + juce::String (beat) + "." + juce::String (sixteenth), "bar", 112.0f },
        { juce::String ((int) secs / 60).paddedLeft ('0', 2) + ":" + juce::String (std::fmod (secs, 60.0), 2).paddedLeft ('0', 5), "time", 110.0f },
    };

    Neon::paintLit (g, getWidth(), getHeight(), glowLayer, 0.8f, [&] (juce::Graphics& gg, bool)
    {
        // The logo, a lit tag in the theme colour
        Neon::text (gg, "glowtag", (float) logoArea.getX() + 4.0f, (float) logoArea.getCentreY() + 10.0f, 32.0f, c, 1.0f);

        // The readouts: numbers in neon, a small word under each
        float x = (float) lcdArea.getX();
        const float base = (float) lcdArea.getCentreY() + 4.0f;
        for (auto& cell : cells)
        {
            Neon::text (gg, cell.value, x, base, 21.0f, c, 0.8f);
            Neon::label (gg, cell.caption, x + 1.0f, base + 13.0f, Neon::Align::left, juce::Colour (0xff6d7077), 11.0f);
            x += cell.width;
        }

        // Theme colours as bulbs; the chosen one lit
        const auto& choices = th.choices();
        for (int i = 0; i < choices.size(); ++i)
        {
            auto s = swatchBounds (i);
            const bool on = ! th.isRainbow() && i == th.getThemeIndex();
            Neon::bulb (gg, s.getCentreX(), s.getCentreY(), choices[i].colour, on ? 1.0f : 0.3f, on ? 6.0f : 4.6f);
        }
    });
}

void TopBar::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < Theme::get().choices().size(); ++i)
        if (swatchBounds (i).expanded (2.0f).contains (e.position))
        {
            Theme::get().setThemeIndex (i);
            return;
        }

    dragStartBpm = ctx.project.bpm.load();
}

void TopBar::mouseDrag (const juce::MouseEvent& e)
{
    // Drag the tempo up or down
    if (tempoArea.contains (e.getMouseDownPosition()))
    {
        const double bpm = juce::jlimit (40.0, 240.0, dragStartBpm - e.getDistanceFromDragStartY() * 0.25);
        ctx.project.bpm = std::round (bpm * 10.0) / 10.0;
        ctx.touched();
        repaint (lcdArea);
    }
}
