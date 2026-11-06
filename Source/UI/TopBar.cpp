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
    recButton.glow = ctx.engine.recordArmed.load() && ctx.engine.isPlaying() ? 1.0f : 0.0f;
    recButton.repaint();
    playButton.glow = ctx.engine.getPulse();
    rainbowButton.setToggleState (Theme::get().isRainbow(), juce::dontSendNotification);
    playButton.repaint();
    repaint (lcdArea);
    repaint (logoArea);
}

juce::Rectangle<float> TopBar::swatchBounds (int index) const
{
    return { (float) swatchArea.getX() + (float) index * 22.0f, (float) swatchArea.getCentreY() - 8.0f, 16.0f, 16.0f };
}

void TopBar::resized()
{
    auto r = getLocalBounds().reduced (14, 10);

    logoArea = r.removeFromLeft (200);
    r.removeFromLeft (8);
    for (auto& b : modeButtons)
    {
        b.setBounds (r.removeFromLeft (84).reduced (2, 2));
    }

    commandButton.setBounds (r.removeFromRight (64).reduced (2));
    r.removeFromRight (8);
    rainbowButton.setBounds (r.removeFromRight (92).reduced (2));
    r.removeFromRight (6);
    swatchArea = r.removeFromRight (Theme::get().choices().size() * 22);

    // Transport and LCD sit together in the middle
    const int transportW = 3 * 50 + 12 + 340 + 12 + 2 * 46;
    auto mid = r.withSizeKeepingCentre (juce::jmin (transportW, r.getWidth()), r.getHeight());
    playButton.setBounds (mid.removeFromLeft (50).reduced (3, 0));
    stopButton.setBounds (mid.removeFromLeft (50).reduced (3, 0));
    recButton.setBounds (mid.removeFromLeft (50).reduced (3, 0));
    mid.removeFromLeft (12);
    metroButton.setBounds (mid.removeFromRight (46).reduced (3, 2));
    loopButton.setBounds (mid.removeFromRight (46).reduced (3, 2));
    mid.removeFromRight (12);
    lcdArea = mid;
    tempoArea = lcdArea.withWidth (100);
}

void TopBar::paint (juce::Graphics& g)
{
    auto& th = Theme::get();
    g.fillAll (th.panel);
    g.setColour (th.line2);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

    // Logo: serif caps with a soft neon halo that breathes with the kick
    {
        const auto c = th.pal (Pal::Green);
        const float pulse = ctx.engine.getPulse();
        auto font = th.heading (22.0f);
        auto lr = logoArea.toFloat();
        g.setColour (c.withAlpha (0.18f + 0.25f * pulse));
        Glow::drawSpacedText (g, "GLOWTAG", lr.translated (0.0f, 1.0f), font, 4.0f, juce::Justification::centredLeft);
        g.setColour (c);
        Glow::drawSpacedText (g, "GLOWTAG", lr, font, 4.0f, juce::Justification::centredLeft);
    }

    // LCD
    {
        auto r = lcdArea.toFloat();
        g.setColour (th.display);
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (th.line);
        g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);

        const auto c = th.pal (Pal::Green);
        const double beats = ctx.playheadBeats();
        const int bar = (int) (beats / beatsPerBar) + 1;
        const int beat = (int) std::fmod (beats, beatsPerBar) + 1;
        const int sixteenth = (int) std::fmod (beats * 4.0, 4.0) + 1;
        const double secs = beats * 60.0 / ctx.project.bpm.load();

        struct Cell { juce::String value, caption; };
        const Cell cells[] = {
            { juce::String (ctx.project.bpm.load(), 1), "BPM" },
            { "4/4", "SIG" },
            { juce::String (bar).paddedLeft ('0', 3) + "." + juce::String (beat) + "." + juce::String (sixteenth), "BAR" },
            { juce::String ((int) secs / 60).paddedLeft ('0', 2) + ":" + juce::String (std::fmod (secs, 60.0), 2).paddedLeft ('0', 5), "TIME" },
        };
        const float widths[] = { 100.0f, 60.0f, 100.0f, 80.0f };

        float x = r.getX();
        for (int i = 0; i < 4; ++i)
        {
            auto cell = juce::Rectangle<float> (x, r.getY(), widths[i], r.getHeight());
            if (i > 0)
            {
                g.setColour (th.line2);
                g.drawVerticalLine ((int) x, r.getY() + 7.0f, r.getBottom() - 7.0f);
            }
            g.setColour (i == 0 || i == 2 ? c : th.text);
            g.setFont (th.label (15.0f));
            g.drawText (cells[i].value, cell.withTrimmedBottom (10.0f).toNearestInt(), juce::Justification::centred);
            g.setColour (th.muted);
            Glow::drawSpacedText (g, cells[i].caption, cell.withTrimmedTop (cell.getHeight() - 14.0f).translated (0.0f, -3.0f),
                                  th.label (7.5f), 1.5f, juce::Justification::centred);
            x += widths[i];
        }
    }

    // Theme swatches
    const auto& choices = th.choices();
    for (int i = 0; i < choices.size(); ++i)
    {
        auto s = swatchBounds (i);
        g.setColour (choices[i].colour.withAlpha (0.85f));
        g.fillEllipse (s.reduced (3.0f));
        if (! th.isRainbow() && i == th.getThemeIndex())
        {
            g.setColour (choices[i].colour);
            g.drawEllipse (s.expanded (1.5f), 1.2f);
        }
    }
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
        repaint (lcdArea);
    }
}
