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

    loopButton.setTooltip ("Clip loop length");
    loopButton.getProperties().set ("pal", (int) Pal::Gold);
    loopButton.onClick = [this]
    {
        juce::PopupMenu menu;
        for (int bars : { 1, 2, 4, 8 })
            menu.addItem (bars, juce::String (bars) + (bars == 1 ? " bar" : " bars"), true,
                          std::abs (loopLength() - bars * beatsPerBar) < 0.001);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&loopButton), [this] (int r)
        {
            if (r <= 0 || ctx.selectedClipPtr() == nullptr)
                return;
            {
                const juce::SpinLock::ScopedLockType sl (ctx.project.lock);
                ctx.selectedClipPtr()->loopLength = r * beatsPerBar;
            }
            ctx.clipsChanged();
        });
    };
    addAndMakeVisible (loopButton);

    ctx.addChangeListener (this);
    recentre();
    updateLoopButton();
}

EditorView::~EditorView()
{
    ctx.removeChangeListener (this);
}

void EditorView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // Selection moved to another clip: forget the selected note and frame the new notes
    if (ctx.selectedTrack != shownTrack || ctx.selectedClip != shownClip)
    {
        shownTrack = ctx.selectedTrack;
        shownClip = ctx.selectedClip;
        selectedNote = -1;
        recentre();
    }
    updateLoopButton();
    repaint();
}

double EditorView::loopLength() const
{
    if (auto* clip = const_cast<AppContext&> (ctx).selectedClipPtr())
        return clip->kind == ClipKind::Audio ? clip->length : clip->loopLength;
    return beatsPerBar;
}

void EditorView::updateLoopButton()
{
    const int bars = juce::roundToInt (loopLength() / beatsPerBar);
    loopButton.setButtonText ("Loop " + juce::String (bars) + (bars == 1 ? " bar" : " bars"));
    loopButton.setVisible (ctx.selectedClipPtr() != nullptr && ctx.selectedClipPtr()->kind == ClipKind::Midi);
}

void EditorView::recentre()
{
    auto* clip = ctx.selectedClipPtr();
    if (clip == nullptr || clip->notes.empty())
    {
        lowNote = 48;
        return;
    }
    int lo = 127, hi = 0;
    for (auto& n : clip->notes)
    {
        lo = juce::jmin (lo, n.pitch);
        hi = juce::jmax (hi, n.pitch);
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
    tools.removeFromLeft (10);
    loopButton.setBounds (tools.removeFromLeft (120).reduced (0, 2));

    r.reduce (14, 0);
    r.removeFromTop (20);
    r.removeFromBottom (12);
    velocityLane = r.removeFromBottom (90);
    r.removeFromBottom (10);
    keys = r.removeFromLeft (56);
    grid = r;
    velocityLane.removeFromLeft (56);
}

juce::Rectangle<float> EditorView::noteBounds (const Note& n) const
{
    return { beatToX (n.start), pitchToY (n.pitch), juce::jmax (4.0f, (float) n.length * beatWidth()), rowHeight() };
}

int EditorView::noteAt (juce::Point<float> p, bool& onRightEdge) const
{
    auto* clip = const_cast<AppContext&> (ctx).selectedClipPtr();
    if (clip == nullptr)
        return -1;
    for (int i = (int) clip->notes.size() - 1; i >= 0; --i)
    {
        auto r = noteBounds (clip->notes[(size_t) i]);
        if (r.contains (p))
        {
            onRightEdge = p.x > r.getRight() - juce::jmin (6.0f, r.getWidth() * 0.3f);
            return i;
        }
    }
    return -1;
}

void EditorView::audition (int pitch, float vel)
{
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    ctx.engine.midiInput.addMessageToQueue (juce::MidiMessage::noteOn (1, pitch, vel).withTimeStamp (now));
    juce::Timer::callAfterDelay (160, [&engine = ctx.engine, pitch]
    {
        engine.midiInput.addMessageToQueue (juce::MidiMessage::noteOff (1, pitch).withTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001));
    });
    lastAuditioned = pitch;
}

void EditorView::tick()
{
    const double ph = ctx.playheadBeats();
    if (! juce::exactlyEqual (ph, lastPlayhead))
    {
        lastPlayhead = ph;
        repaint (grid.expanded (2));
        repaint (velocityLane.expanded (2));
    }
}

void EditorView::deleteSelectedNote()
{
    auto* clip = ctx.selectedClipPtr();
    if (clip == nullptr || selectedNote < 0 || selectedNote >= (int) clip->notes.size())
        return;
    {
        const juce::SpinLock::ScopedLockType sl (ctx.project.lock);
        clip->notes.erase (clip->notes.begin() + selectedNote);
    }
    selectedNote = -1;
    ctx.clipsChanged();
}

void EditorView::mouseMove (const juce::MouseEvent& e)
{
    bool edge = false;
    const int n = noteAt (e.position, edge);
    if (n >= 0)
        setMouseCursor (scissors.getToggleState() ? juce::MouseCursor::IBeamCursor
                        : (edge ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::DraggingHandCursor));
    else
        setMouseCursor (grid.contains (e.getPosition()) && pencil.getToggleState() ? juce::MouseCursor::CrosshairCursor
                                                                                 : juce::MouseCursor::NormalCursor);
}

void EditorView::mouseDown (const juce::MouseEvent& e)
{
    auto* clip = ctx.selectedClipPtr();
    drag = DragMode::None;
    if (clip == nullptr || clip->kind != ClipKind::Midi)
        return;

    if (velocityLane.contains (e.getPosition()))
    {
        drag = DragMode::VelocityLane;
        mouseDrag (e);
        return;
    }
    if (! grid.contains (e.getPosition()))
        return;

    bool edge = false;
    const int hit = noteAt (e.position, edge);

    if (hit >= 0)
    {
        auto& n = clip->notes[(size_t) hit];
        selectedNote = hit;

        if (e.mods.isPopupMenu())
        {
            deleteSelectedNote();
            return;
        }
        if (scissors.getToggleState())
        {
            // Split the note at the nearest grid line inside it
            const double at = std::round (xToBeat (e.position.x) / gridStep) * gridStep;
            if (at > n.start && at < n.start + n.length)
            {
                Note second = n;
                second.start = at;
                second.length = n.start + n.length - at;
                const juce::SpinLock::ScopedLockType sl (ctx.project.lock);
                clip->notes[(size_t) hit].length = at - n.start;
                clip->notes.push_back (second);
            }
            repaint();
            return;
        }
        if (velocity.getToggleState())
        {
            drag = DragMode::Velocity;
            grabVelocity = n.velocity;
            return;
        }
        drag = edge ? DragMode::Resize : DragMode::Move;
        grabOffset = xToBeat (e.position.x) - n.start;
        audition (n.pitch, n.velocity);
        repaint();
        return;
    }

    if (pencil.getToggleState())
    {
        // Draw a new note, then drag right to stretch it
        Note n;
        n.start = juce::jlimit (0.0, loopLength() - gridStep, std::floor (xToBeat (e.position.x) / gridStep) * gridStep);
        n.length = juce::jmin (lastLength, loopLength() - n.start);
        n.pitch = juce::jlimit (0, 127, yToPitch (e.position.y));
        n.velocity = 0.8f;
        {
            const juce::SpinLock::ScopedLockType sl (ctx.project.lock);
            clip->notes.push_back (n);
        }
        selectedNote = (int) clip->notes.size() - 1;
        drag = DragMode::Resize;
        audition (n.pitch);
    }
    else
    {
        selectedNote = -1;
    }
    repaint();
}

void EditorView::mouseDrag (const juce::MouseEvent& e)
{
    auto* clip = ctx.selectedClipPtr();
    if (clip == nullptr || drag == DragMode::None)
        return;

    const juce::SpinLock::ScopedLockType sl (ctx.project.lock);

    if (drag == DragMode::VelocityLane)
    {
        // Set the velocity of every note starting in the column under the mouse
        const double col = std::floor (xToBeat (e.position.x) / gridStep) * gridStep;
        const float v = juce::jlimit (0.05f, 1.0f, (float) (velocityLane.getBottom() - 10 - e.y) / (float) (velocityLane.getHeight() - 20));
        for (auto& n : clip->notes)
            if (n.start >= col - 1e-6 && n.start < col + gridStep - 1e-6)
                n.velocity = v;
        repaint();
        return;
    }

    if (selectedNote < 0 || selectedNote >= (int) clip->notes.size())
        return;
    auto& n = clip->notes[(size_t) selectedNote];
    const double loop = loopLength();

    switch (drag)
    {
        case DragMode::Move:
        {
            n.start = juce::jlimit (0.0, juce::jmax (0.0, loop - n.length),
                                    std::round ((xToBeat (e.position.x) - grabOffset) / gridStep) * gridStep);
            const int p = juce::jlimit (0, 127, yToPitch (e.position.y));
            if (p != n.pitch)
            {
                n.pitch = p;
                audition (p, n.velocity);
            }
            break;
        }
        case DragMode::Resize:
            n.length = juce::jlimit (gridStep, loop - n.start, std::round ((xToBeat (e.position.x) - n.start) / gridStep) * gridStep);
            lastLength = n.length;
            break;
        case DragMode::Velocity:
            n.velocity = juce::jlimit (0.05f, 1.0f, grabVelocity - (float) e.getDistanceFromDragStartY() / 120.0f);
            break;
        case DragMode::VelocityLane:
        case DragMode::None:
            break;
    }
    repaint();
}

void EditorView::mouseUp (const juce::MouseEvent&)
{
    if (drag != DragMode::None)
        ctx.clipsChanged();
    drag = DragMode::None;
}

void EditorView::mouseDoubleClick (const juce::MouseEvent& e)
{
    bool edge = false;
    if (noteAt (e.position, edge) >= 0 && ! scissors.getToggleState())
    {
        selectedNote = noteAt (e.position, edge);
        deleteSelectedNote();
    }
}

void EditorView::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    lowNote = juce::jlimit (0, 127 - numRows, lowNote + (w.deltaY > 0 ? 2 : -2));
    repaint();
}

void EditorView::paint (juce::Graphics& g)
{
    auto& th = Theme::get();
    auto& t = ctx.selected();
    const auto c = th.pal (t.colourIndex);
    auto* clip = ctx.selectedClipPtr();

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
    Glow::drawSpacedText (g, clip != nullptr ? clip->name.toUpperCase() : juce::String ("NO CLIP SELECTED"), title, th.label (8.5f), 1.5f,
                          juce::Justification::centredRight);

    if (clip == nullptr)
    {
        g.setColour (th.muted);
        g.setFont (th.body (14.0f));
        g.drawText ("Select a clip in Arrange to edit it, or double-click an empty lane to make one.",
                    getLocalBounds().withTrimmedTop (44), juce::Justification::centred);
        return;
    }

    if (clip->kind == ClipKind::Audio && clip->audio != nullptr)
    {
        auto area = grid.getUnion (velocityLane).toFloat();
        const auto& peaks = clip->audio->peaks;
        juce::Path wave;
        for (float x = area.getX(); x < area.getRight(); x += 1.0f)
        {
            const float p = peaks[(size_t) juce::jlimit (0, (int) peaks.size() - 1, (int) ((x - area.getX()) / area.getWidth() * peaks.size()))];
            const float h = juce::jmax (0.5f, p * area.getHeight() * 0.45f);
            wave.addRectangle (x, area.getCentreY() - h, 1.0f, h * 2.0f);
        }
        g.setColour (c.withAlpha (0.8f));
        g.fillPath (wave);
        g.setColour (th.muted);
        g.setFont (th.body (12.0f));
        g.drawText ("Audio clip. Slicing and warping arrive with the sampler in step 3.", area.toNearestInt().removeFromTop (20),
                    juce::Justification::centredLeft);
        return;
    }

    // Keyboard and row shading
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
        g.setColour (isBlack ? th.display.withAlpha (0.6f) : th.panel.withAlpha (0.5f));
        g.fillRect (juce::Rectangle<float> ((float) grid.getX(), key.getY(), (float) grid.getWidth(), rh).reduced (0.0f, 0.5f));
    }

    // Grid lines: sixteenths, beats, bars
    const double loop = loopLength();
    for (double b = 0.0; b <= loop + 1e-6; b += gridStep)
    {
        const float x = beatToX (b);
        const bool bar = std::fmod (b, beatsPerBar) < 1e-6, beat = std::fmod (b, 1.0) < 1e-6;
        if (! beat && beatWidth() < 40.0f)
            continue;
        g.setColour (bar ? th.line.brighter (0.1f) : (beat ? th.line : th.line2));
        g.drawVerticalLine ((int) x, (float) grid.getY(), (float) grid.getBottom());
        if (beat && b < loop)
        {
            g.setColour (th.muted);
            g.setFont (th.label (8.5f));
            g.drawText (juce::String ((int) (b / beatsPerBar) + 1) + "." + juce::String ((int) std::fmod (b, beatsPerBar) + 1),
                        juce::Rectangle<float> (x + 4.0f, (float) grid.getY() - 15.0f, 40.0f, 12.0f).toNearestInt(),
                        juce::Justification::centredLeft);
        }
    }

    // Notes, brighter when louder
    for (size_t i = 0; i < clip->notes.size(); ++i)
    {
        const auto& n = clip->notes[i];
        if (n.pitch < lowNote || n.pitch >= lowNote + numRows || n.start >= loop)
            continue;
        auto r = noteBounds (n).reduced (1.0f, 1.5f);
        const bool sel = (int) i == selectedNote;
        g.setColour (c.withAlpha (0.15f + 0.35f * n.velocity));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (sel ? th.text : c);
        g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, sel ? 1.5f : 1.0f);
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
    for (size_t i = 0; i < clip->notes.size(); ++i)
    {
        const auto& n = clip->notes[i];
        if (n.start >= loop)
            continue;
        const float x = beatToX (n.start) + 4.0f;
        const float top = lane.getBottom() - lane.getHeight() * n.velocity;
        juce::Path stem;
        stem.startNewSubPath (x, lane.getBottom());
        stem.lineTo (x, top);
        Glow::strokeGlow (g, stem, (int) i == selectedNote ? th.text : c, 1.4f);
        g.setColour ((int) i == selectedNote ? th.text : c);
        g.fillEllipse (x - 3.0f, top - 3.0f, 6.0f, 6.0f);
    }

    // Playhead, when it is inside this clip
    const double ph = ctx.playheadBeats();
    if (ph >= clip->start && ph < clip->end())
    {
        const float x = beatToX (std::fmod (ph - clip->start, loop));
        juce::Path line;
        line.startNewSubPath (x, (float) grid.getY());
        line.lineTo (x, (float) velocityLane.getBottom());
        Glow::strokeGlow (g, line, th.pal (Pal::Green), 1.0f, ctx.engine.getPulse());
    }
}
