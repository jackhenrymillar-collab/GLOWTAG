#include "EditorView.h"

EditorView::EditorView (AppContext& c) : ctx (c)
{
    for (auto* b : { &pencil, &cursor, &scissors, &velocity })
    {
        b->setRadioGroupId (3001);
        b->setClickingTogglesState (true);
        b->setTooltip (b->getName());
        addAndMakeVisible (*b);
    }
    pencil.setToggleState (true, juce::dontSendNotification);
}

std::vector<int> EditorView::notesForStep (const Track& t, int step) const
{
    switch (t.sound)
    {
        case Sound::Kick:  return { 36 };
        case Sound::Snare: return { 38 };
        case Sound::Clap:  return { 39 };
        case Sound::Hat:   return { 42 };
        case Sound::Chop:  { static const int n[] = { 72, 75, 79, 82 }; return { n[step % 4] }; }
        case Sound::Bass:  return { step < 8 ? 33 : 31 };
        case Sound::Keys:  return { 57, 60, 64 };
        case Sound::Silent: break;
    }
    return { 60 };
}

void EditorView::updateRange()
{
    int lo = 127, hi = 0;
    for (int s = 0; s < Project::stepsPerBar; ++s)
        for (int n : notesForStep (ctx.selected(), s))
        {
            lo = juce::jmin (lo, n);
            hi = juce::jmax (hi, n);
        }
    lowNote = juce::jlimit (0, 127 - numRows, (lo + hi) / 2 - numRows / 2);
}

void EditorView::resized()
{
    auto r = getLocalBounds();
    toolbar = r.removeFromTop (44);
    auto tools = toolbar.reduced (14, 6);
    tools.removeFromLeft (220);
    for (auto* b : { &pencil, &cursor, &scissors, &velocity })
    {
        b->setBounds (tools.removeFromLeft (44));
        tools.removeFromLeft (6);
    }

    r.reduce (14, 0);
    r.removeFromTop (20);   // beat labels
    r.removeFromBottom (12);
    velocityLane = r.removeFromBottom (90);
    r.removeFromBottom (10);
    keys = r.removeFromLeft (56);
    grid = r;
    velocityLane.removeFromLeft (56);
}

void EditorView::tick()
{
    const double ph = ctx.playheadSteps();
    if (! juce::exactlyEqual (ph, lastPlayhead))
    {
        lastPlayhead = ph;
        repaint (grid.expanded (2));
        repaint (velocityLane.expanded (2));
    }
}

void EditorView::mouseDown (const juce::MouseEvent& e)
{
    if (! grid.contains (e.getPosition()) || ! (pencil.getToggleState() || cursor.getToggleState()))
        return;

    const int step = juce::jlimit (0, Project::stepsPerBar - 1, (int) ((float) (e.x - grid.getX()) / stepWidth()));
    auto& t = ctx.selected();
    t.steps = (uint16_t) (t.steps.load() ^ (1u << step));
    repaint();
}

void EditorView::paint (juce::Graphics& g)
{
    auto& th = Theme::get();
    auto& t = ctx.selected();
    const auto c = th.pal (t.colourIndex);
    updateRange();

    g.fillAll (th.bg);

    // Toolbar
    g.setColour (th.panel);
    g.fillRect (toolbar);
    g.setColour (th.line2);
    g.drawHorizontalLine (toolbar.getBottom() - 1, 0.0f, (float) getWidth());
    auto title = toolbar.reduced (14, 0).toFloat();
    g.setColour (c);
    Glow::drawSpacedText (g, t.name.toUpperCase(), title, th.heading (14.0f), 2.5f, juce::Justification::centredLeft);
    g.setColour (th.muted);
    Glow::drawSpacedText (g, "PIANO ROLL  /  1 BAR LOOP", title, th.label (8.5f), 1.5f, juce::Justification::centredRight);

    // Keyboard
    static const bool black[] = { false, true, false, true, false, false, true, false, true, false, true, false };
    const float rh = rowHeight();
    for (int row = 0; row < numRows; ++row)
    {
        const int note = lowNote + numRows - 1 - row;
        const auto key = juce::Rectangle<float> ((float) keys.getX(), (float) grid.getY() + (float) row * rh,
                                                 (float) keys.getWidth() - 4.0f, rh);
        const bool isBlack = black[note % 12];
        g.setColour (isBlack ? th.display : th.panel2.brighter (0.04f));
        g.fillRect (key.reduced (0.0f, 0.5f));
        if (note % 12 == 0)
        {
            g.setColour (th.muted);
            g.setFont (th.label (8.0f));
            g.drawText ("C" + juce::String (note / 12 - 1), key.reduced (4.0f, 0.0f).toNearestInt(), juce::Justification::centredRight);
        }

        // Row background in the grid
        g.setColour (isBlack ? th.display.withAlpha (0.6f) : th.panel.withAlpha (0.5f));
        g.fillRect (juce::Rectangle<float> ((float) grid.getX(), key.getY(), (float) grid.getWidth(), rh).reduced (0.0f, 0.5f));
    }

    // Grid lines
    const float sw = stepWidth();
    for (int s = 0; s <= Project::stepsPerBar; ++s)
    {
        const float x = (float) grid.getX() + (float) s * sw;
        g.setColour (s % 4 == 0 ? th.line : th.line2);
        g.drawVerticalLine ((int) x, (float) grid.getY(), (float) grid.getBottom());
        if (s % 4 == 0 && s < Project::stepsPerBar)
        {
            g.setColour (th.muted);
            g.setFont (th.label (8.5f));
            g.drawText ("1." + juce::String (s / 4 + 1), juce::Rectangle<float> (x + 4.0f, (float) grid.getY() - 14.0f, 40.0f, 12.0f).toNearestInt(),
                        juce::Justification::centredLeft);
        }
    }

    // Notes
    for (int s = 0; s < Project::stepsPerBar; ++s)
    {
        if (! t.stepOn (s))
            continue;
        for (int n : notesForStep (t, s))
        {
            const int row = rowForNote (n);
            if (row < 0 || row >= numRows)
                continue;
            auto note = juce::Rectangle<float> ((float) grid.getX() + (float) s * sw, (float) grid.getY() + (float) row * rh, sw, rh).reduced (1.5f, 1.5f);
            g.setColour (c.withAlpha (0.30f));
            g.fillRoundedRectangle (note, 3.0f);
            g.setColour (c);
            g.drawRoundedRectangle (note.reduced (0.5f), 3.0f, 1.0f);
        }
    }

    // Velocity lane
    g.setColour (th.panel);
    g.fillRoundedRectangle (velocityLane.toFloat(), 8.0f);
    g.setColour (th.line2);
    g.drawRoundedRectangle (velocityLane.toFloat().reduced (0.5f), 8.0f, 1.0f);
    g.setColour (th.muted);
    Glow::drawSpacedText (g, "VELOCITY", velocityLane.toFloat().translated (-60.0f, 0.0f).withWidth (56.0f),
                          th.label (7.5f), 1.0f, juce::Justification::centred);
    auto lane = velocityLane.toFloat().reduced (0.0f, 10.0f);
    for (int s = 0; s < Project::stepsPerBar; ++s)
    {
        if (! t.stepOn (s))
            continue;
        const float vel = s % 4 == 0 ? 1.0f : 0.7f;
        const float x = (float) grid.getX() + ((float) s + 0.5f) * sw;
        const float top = lane.getBottom() - lane.getHeight() * vel;
        juce::Path stem;
        stem.startNewSubPath (x, lane.getBottom());
        stem.lineTo (x, top);
        Glow::strokeGlow (g, stem, c, 1.4f);
        g.setColour (c);
        g.fillEllipse (x - 3.0f, top - 3.0f, 6.0f, 6.0f);
    }

    // Playhead
    const double ph = ctx.playheadSteps();
    if (ph >= 0.0)
    {
        const float x = (float) grid.getX() + (float) std::fmod (ph, (double) Project::stepsPerBar) * sw;
        juce::Path line;
        line.startNewSubPath (x, (float) grid.getY());
        line.lineTo (x, (float) velocityLane.getBottom());
        Glow::strokeGlow (g, line, th.pal (Pal::Green), 1.0f, ctx.engine.getPulse());
    }
}
