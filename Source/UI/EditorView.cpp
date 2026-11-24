#include "EditorView.h"

EditorView::EditorView (AppContext& c) : ctx (c)
{
    for (auto* b : { &pencil, &cursor, &scissors, &velocity })
    {
        b->setRadioGroupId (3001);
        b->setClickingTogglesState (true);
        addAndMakeVisible (*b);
    }
    pencil.setToggleState (true, juce::dontSendNotification);

    snapB.setClickingTogglesState (true);
    snapB.setToggleState (true, juce::dontSendNotification);
    snapB.onClick = [this] { snapOn = snapB.getToggleState(); say (snapOn ? "snap on" : "snap off"); repaint(); };

    auto hasNotes = [this] { auto* clip = midiClip(); return clip != nullptr && ! clip->notes.empty(); };
    auto hasSelection = [this] { return midiClip() != nullptr && ! selection.empty(); };
    quantiseB.enabled = hasNotes;
    allB.enabled = hasNotes;
    duplicateB.enabled = hasSelection;
    octUpB.enabled = hasSelection;
    octDownB.enabled = hasSelection;
    gridB.enabled = shorterB.enabled = longerB.enabled = [this] { return midiClip() != nullptr; };

    quantiseB.onClick = [this] { quantise(); };
    shorterB.onClick = [this] { changeLength (-1); };
    longerB.onClick = [this] { changeLength (1); };
    duplicateB.onClick = [this] { duplicateSelection(); };
    octUpB.onClick = [this] { transposeSelection (12); };
    octDownB.onClick = [this] { transposeSelection (-12); };
    allB.onClick = [this] { selectAll(); };
    gridB.onClick = [this]
    {
        juce::PopupMenu menu;
        for (int i = NoteEdit::numGrids - 1; i >= 0; --i)
            menu.addItem (i + 1, NoteEdit::gridName (i), true, i == gridIndex);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&gridB), [this] (int r)
        {
            if (r > 0) { gridIndex = r - 1; updateButtons(); repaint(); }
        });
    };

    for (auto* b : { &quantiseB, &gridB, &snapB, &shorterB, &longerB, &duplicateB, &octUpB, &octDownB, &allB })
        addAndMakeVisible (*b);

    ctx.addChangeListener (this);
    recentre();
    updateButtons();
}

EditorView::~EditorView()
{
    ctx.removeChangeListener (this);
}

void EditorView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // Selection moved to another clip: forget the selected notes and frame the new notes
    if (ctx.selectedTrack != shownTrack || ctx.selectedClip != shownClip)
    {
        shownTrack = ctx.selectedTrack;
        shownClip = ctx.selectedClip;
        selection.clear();
        recentre();
    }
    else if (auto* clip = midiClip())
    {
        // Notes may have gone (undo, recording): keep only picks that still exist
        selection.erase (std::remove_if (selection.begin(), selection.end(),
                                         [n = (int) clip->notes.size()] (int i) { return i >= n; }), selection.end());
    }
    else
        selection.clear();
    updateButtons();
    repaint();
}

Clip* EditorView::midiClip() const
{
    auto* clip = const_cast<AppContext&> (ctx).selectedClipPtr();
    return clip != nullptr && clip->kind == ClipKind::Midi ? clip : nullptr;
}

double EditorView::loopLength() const
{
    if (auto* clip = const_cast<AppContext&> (ctx).selectedClipPtr())
        return clip->kind == ClipKind::Audio ? clip->length : clip->loopLength;
    return beatsPerBar;
}

void EditorView::select (std::vector<int> s)
{
    std::sort (s.begin(), s.end());
    s.erase (std::unique (s.begin(), s.end()), s.end());
    selection = std::move (s);
    updateButtons();
}

void EditorView::say (const juce::String& s)
{
    flash = s;
    flashTime = Neon::now();
    repaint (toolbar);
}

void EditorView::edited()
{
    updateButtons();
    ctx.clipsChanged();
    repaint();
}

void EditorView::updateButtons()
{
    const bool midi = midiClip() != nullptr;
    for (auto* b : { &pencil, &cursor, &scissors, &velocity, &quantiseB, &gridB, &snapB, &shorterB, &longerB,
                     &duplicateB, &octUpB, &octDownB, &allB })
    {
        b->setVisible (midi);
        b->repaint();
    }
    gridB.word = NoteEdit::gridName (gridIndex);
    snapB.setToggleState (snapOn, juce::dontSendNotification);
}

void EditorView::recentre()
{
    auto* clip = ctx.selectedClipPtr();
    if (clip == nullptr || clip->notes.empty())
    {
        lowNote = juce::jlimit (0, 127 - numRows, 60 - numRows / 2);
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
    toolbar = r.removeFromTop (70);

    // The buttons, left to right after the track name. Words drop off (icons stay) when there is no room.
    auto tools = toolbar.reduced (14, 5);
    tools.removeFromLeft (200);
    const float wordSize = 15.0f;
    RollButton* all[] = { &pencil, &cursor, &scissors, &velocity, &quantiseB, &gridB, &snapB, &shorterB, &longerB,
                          &duplicateB, &octUpB, &octDownB, &allB };
    const int gap = 4, groupGap = 22, readoutW = 64;
    auto layout = [&] (bool words, bool place)
    {
        auto row = tools;
        int used = 0;
        for (auto* b : all)
        {
            b->showWord = words;
            b->setSize (b->getWidth(), row.getHeight());
            const int w = b->idealWidth (wordSize);
            if (b == &quantiseB || b == &shorterB || b == &duplicateB) { row.removeFromLeft (groupGap); used += groupGap; }
            if (place) b->setBounds (row.removeFromLeft (w)); else row.removeFromLeft (w);
            used += w + gap;
            row.removeFromLeft (gap);
            if (b == &shorterB)
            {
                if (place) lengthReadout = row.removeFromLeft (readoutW); else row.removeFromLeft (readoutW);
                used += readoutW;
            }
        }
        return used;
    };
    const bool words = layout (true, false) <= tools.getWidth();
    layout (words, true);

    r.reduce (14, 0);
    r.removeFromTop (20);
    r.removeFromBottom (12);
    velocityLane = r.removeFromBottom (90);
    r.removeFromBottom (10);
    keys = r.removeFromLeft (64);
    grid = r;
    velocityLane.removeFromLeft (64);

    // Rows a little taller than before (they were about 19 px), so notes are easier to pick
    const int rowsBefore = numRows;
    numRows = juce::jlimit (12, 60, (int) std::floor ((float) grid.getHeight() / targetRowHeight));
    if (numRows != rowsBefore) recentre();
    lowNote = juce::jlimit (0, 127 - numRows, lowNote);
}

juce::Rectangle<float> EditorView::noteBounds (const Note& n) const
{
    return { beatToX (n.start), pitchToY (n.pitch), juce::jmax (4.0f, (float) n.length * beatWidth()), rowHeight() };
}

int EditorView::noteAt (juce::Point<float> p, bool& onRightEdge) const
{
    auto* clip = midiClip();
    if (clip == nullptr)
        return -1;
    for (int i = (int) clip->notes.size() - 1; i >= 0; --i)
    {
        const auto& n = clip->notes[(size_t) i];
        if (n.pitch < lowNote || n.pitch >= lowNote + numRows || n.start >= loopLength())
            continue;
        auto r = noteBounds (n);
        if (r.contains (p))
        {
            onRightEdge = p.x > r.getRight() - juce::jmin (7.0f, r.getWidth() * 0.3f);
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
    if (flash.isNotEmpty() && Neon::now() - flashTime > 1.6)
    {
        flash.clear();
        repaint (toolbar);
    }
    const double ph = ctx.playheadBeats();
    if (! juce::exactlyEqual (ph, lastPlayhead))
    {
        lastPlayhead = ph;
        // Only the strips the playhead leaves and enters
        int x = -1000;
        if (auto* clip = ctx.selectedClipPtr(); clip != nullptr && ph >= clip->start && ph < clip->end())
            x = juce::roundToInt (beatToX (std::fmod (ph - clip->start, loopLength())));
        if (x != shownPlayheadX)
        {
            const int top = grid.getY() - 2, height = velocityLane.getBottom() + 2 - top;
            if (shownPlayheadX > -1000) repaint (shownPlayheadX - 9, top, 19, height);
            if (x > -1000)              repaint (x - 9, top, 19, height);
            shownPlayheadX = x;
        }
    }
}

//==============================================================================
// Actions

void EditorView::deleteSelectedNote()
{
    auto* clip = midiClip();
    if (clip == nullptr || selection.empty())
        return;
    {
        const juce::SpinLock::ScopedLockType sl (ctx.project.lock);
        NoteEdit::erase (clip->notes, selection);
    }
    selection.clear();
    edited();
}

void EditorView::quantise()
{
    auto* clip = midiClip();
    if (clip == nullptr || clip->notes.empty())
        return;
    {
        const juce::SpinLock::ScopedLockType sl (ctx.project.lock);
        NoteEdit::quantise (clip->notes, selection, gridStep(), clip->loopLength);
    }
    say ("quantised " + juce::String (selection.empty() ? "all" : juce::String ((int) selection.size())) + " to " + NoteEdit::gridName (gridIndex));
    edited();
}

void EditorView::changeGrid (int direction)
{
    gridIndex = direction > 0 ? NoteEdit::finer (gridIndex) : NoteEdit::coarser (gridIndex);
    say ("grid " + NoteEdit::gridName (gridIndex));
    updateButtons();
    repaint();
}

void EditorView::changeLength (int direction)
{
    auto* clip = midiClip();
    if (clip == nullptr)
        return;
    {
        const juce::SpinLock::ScopedLockType sl (ctx.project.lock);
        const double before = clip->loopLength;
        clip->loopLength = NoteEdit::changedLength (before, direction);
        // A clip that plays its loop once grows and shrinks with it in the arrangement
        if (std::abs (clip->length - before) < 1e-6)
            clip->length = clip->loopLength;
    }
    say ("clip " + NoteEdit::lengthName (clip->loopLength));
    edited();
}

bool EditorView::duplicateSelection()
{
    auto* clip = midiClip();
    if (clip == nullptr || selection.empty())
        return false;
    std::vector<int> copies;
    {
        const juce::SpinLock::ScopedLockType sl (ctx.project.lock);
        const double before = clip->loopLength;
        copies = NoteEdit::duplicate (clip->notes, selection, gridStep(), clip->loopLength);
        if (clip->loopLength > before && std::abs (clip->length - before) < 1e-6)
            clip->length = clip->loopLength;
    }
    select (copies);
    say ("duplicated " + juce::String ((int) copies.size()));
    edited();
    return true;
}

void EditorView::transposeSelection (int semis)
{
    auto* clip = midiClip();
    if (clip == nullptr || selection.empty())
        return;
    bool moved;
    {
        const juce::SpinLock::ScopedLockType sl (ctx.project.lock);
        moved = NoteEdit::transpose (clip->notes, selection, semis);
    }
    if (! moved)
    {
        say ("out of range");
        return;
    }
    // Follow the notes if they left the visible keys
    int lo = 127, hi = 0;
    for (int i : selection) { lo = juce::jmin (lo, clip->notes[(size_t) i].pitch); hi = juce::jmax (hi, clip->notes[(size_t) i].pitch); }
    if (lo < lowNote || hi >= lowNote + numRows)
        lowNote = juce::jlimit (0, 127 - numRows, (lo + hi) / 2 - numRows / 2);
    audition (clip->notes[(size_t) selection.front()].pitch, clip->notes[(size_t) selection.front()].velocity);
    say (semis > 0 ? "octave up" : "octave down");
    edited();
}

void EditorView::selectAll()
{
    if (auto* clip = midiClip())
    {
        select (NoteEdit::all (clip->notes));
        say ("all " + juce::String ((int) selection.size()) + " notes");
        repaint();
    }
}

void EditorView::toggleSnap()
{
    snapOn = ! snapOn;
    updateButtons();
    say (snapOn ? "snap on" : "snap off");
    repaint();
}

bool EditorView::keyUsed (const juce::KeyPress& key)
{
    if (midiClip() == nullptr)
        return false;
    const auto mods = key.getModifiers();
    const int code = key.getKeyCode();
    const auto lower = code < 256 ? juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) code) : (juce::juce_wchar) 0;
    const bool cmd = mods.isCommandDown() && ! mods.isAltDown() && ! mods.isCtrlDown();

    // Alt+S: snap on and off (Option+S types "ß" on a Mac keyboard, so take either)
    if (mods.isAltDown() && ! mods.isCommandDown() && (lower == 's' || key.getTextCharacter() == 0xdf))
    {
        toggleSnap();
        return true;
    }
    // Q on its own quantises too (it plays no note)
    if (! mods.isAnyModifierKeyDown() && lower == 'q') { quantise(); return true; }
    if (! cmd)
        return false;

    if (code == juce::KeyPress::upKey)             { changeGrid (1); return true; }
    if (code == juce::KeyPress::downKey)           { changeGrid (-1); return true; }
    if (code == juce::KeyPress::rightKey)          { changeLength (1); return true; }
    if (code == juce::KeyPress::leftKey)           { changeLength (-1); return true; }
    if (lower == 'a' && ! mods.isShiftDown())      { selectAll(); return true; }
    if (lower == 'd' && ! mods.isShiftDown())      return duplicateSelection();   // nothing picked: the clip duplicates as before
    if (code == '=' || code == '+')                { transposeSelection (12); return true; }
    if (code == '-' || code == '_')                { transposeSelection (-12); return true; }
    return false;
}

//==============================================================================
// Pointer

void EditorView::mouseMove (const juce::MouseEvent& e)
{
    bool edge = false;
    const int n = noteAt (e.position, edge);
    if (n >= 0)
        setMouseCursor (scissors.getToggleState() ? juce::MouseCursor::IBeamCursor
                        : (edge ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::DraggingHandCursor));
    else if (keys.contains (e.getPosition()))
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    else
        setMouseCursor (grid.contains (e.getPosition()) && pencil.getToggleState() && ! e.mods.isShiftDown()
                            ? juce::MouseCursor::CrosshairCursor : juce::MouseCursor::NormalCursor);
}

void EditorView::mouseDown (const juce::MouseEvent& e)
{
    auto* clip = midiClip();
    drag = DragMode::None;
    if (clip == nullptr)
        return;

    if (velocityLane.contains (e.getPosition()))
    {
        drag = DragMode::VelocityLane;
        mouseDrag (e);
        return;
    }

    // A piano key: hear it and pick every note on it (Shift adds them)
    if (keys.contains (e.getPosition()) && e.position.y >= (float) grid.getY() && e.position.y < (float) grid.getBottom())
    {
        const int p = juce::jlimit (0, 127, yToPitch (e.position.y));
        audition (p);
        std::vector<int> s = e.mods.isShiftDown() ? selection : std::vector<int>();
        for (size_t i = 0; i < clip->notes.size(); ++i)
            if (clip->notes[i].pitch == p) s.push_back ((int) i);
        select (s);
        repaint();
        return;
    }
    if (! grid.contains (e.getPosition()))
        return;

    bool edge = false;
    const int hit = noteAt (e.position, edge);
    const double step = gridStep();

    if (hit >= 0)
    {
        auto& n = clip->notes[(size_t) hit];

        if (e.mods.isPopupMenu())
        {
            if (! isSelected (hit)) select ({ hit });
            deleteSelectedNote();
            return;
        }
        // Shift-click adds or takes away a note; a plain click on a picked note keeps the group
        if (e.mods.isShiftDown())
        {
            auto s = selection;
            if (isSelected (hit)) s.erase (std::find (s.begin(), s.end(), hit));
            else s.push_back (hit);
            select (s);
            repaint();
            return;
        }
        if (! isSelected (hit))
            select ({ hit });

        if (scissors.getToggleState())
        {
            // Split the note at the nearest grid line inside it (anywhere when snap is off)
            const double at = NoteEdit::snap (xToBeat (e.position.x), step, snapOn);
            if (at > n.start + 1e-6 && at < n.start + n.length - 1e-6)
            {
                Note second = n;
                second.start = at;
                second.length = n.start + n.length - at;
                const juce::SpinLock::ScopedLockType sl (ctx.project.lock);
                clip->notes[(size_t) hit].length = at - n.start;
                clip->notes.push_back (second);
            }
            edited();
            return;
        }

        anchor = hit;
        dragFrom.clear();
        for (int i : selection) dragFrom.push_back (clip->notes[(size_t) i]);
        if (velocity.getToggleState())
        {
            drag = DragMode::Velocity;
            return;
        }
        drag = edge ? DragMode::Resize : DragMode::Move;
        grabOffset = xToBeat (e.position.x) - n.start;
        audition (n.pitch, n.velocity);
        repaint();
        return;
    }

    // Empty space: a selection box with the Select tool, or Shift with any tool
    if (cursor.getToggleState() || e.mods.isShiftDown() || ! pencil.getToggleState())
    {
        drag = DragMode::Box;
        boxStartSelection = e.mods.isShiftDown() ? selection : std::vector<int>();
        box = { e.position, e.position };
        select (boxStartSelection);
        repaint();
        return;
    }

    // Pencil: draw a new note, then drag right to stretch it
    const double loop = loopLength();
    Note n;
    const double at = snapOn ? std::floor (xToBeat (e.position.x) / step) * step : xToBeat (e.position.x);
    n.start = juce::jlimit (0.0, juce::jmax (0.0, loop - (snapOn ? step : NoteEdit::freeMinLength)), at);
    n.length = juce::jmax (snapOn ? step : NoteEdit::freeMinLength, juce::jmin (lastLength, loop - n.start));
    n.pitch = juce::jlimit (0, 127, yToPitch (e.position.y));
    n.velocity = 0.8f;
    {
        const juce::SpinLock::ScopedLockType sl (ctx.project.lock);
        clip->notes.push_back (n);
    }
    select ({ (int) clip->notes.size() - 1 });
    anchor = selection.front();
    dragFrom = { n };
    drag = DragMode::Resize;
    audition (n.pitch);
    repaint();
}

void EditorView::mouseDrag (const juce::MouseEvent& e)
{
    auto* clip = midiClip();
    if (clip == nullptr || drag == DragMode::None)
        return;

    if (drag == DragMode::Box)
    {
        box = juce::Rectangle<float> (e.mouseDownPosition, e.position).getIntersection (grid.toFloat());
        auto s = boxStartSelection;
        const auto inside = NoteEdit::inBox (clip->notes, xToBeat (box.getX()), xToBeat (box.getRight()),
                                             yToPitch (box.getBottom() - 0.5f), yToPitch (box.getY() + 0.5f));
        for (int i : inside)
            if (clip->notes[(size_t) i].start < loopLength()) s.push_back (i);
        select (s);
        repaint();
        return;
    }

    const juce::SpinLock::ScopedLockType sl (ctx.project.lock);
    const double step = gridStep();

    if (drag == DragMode::VelocityLane)
    {
        // Set the velocity of every note starting in the column under the mouse (only picked ones, if any are)
        const double col = std::floor (xToBeat (e.position.x) / step) * step;
        const float v = juce::jlimit (0.05f, 1.0f, (float) (velocityLane.getBottom() - 10 - e.y) / (float) (velocityLane.getHeight() - 20));
        for (size_t i = 0; i < clip->notes.size(); ++i)
        {
            auto& n = clip->notes[i];
            if (n.start >= col - 1e-6 && n.start < col + step - 1e-6 && (selection.empty() || isSelected ((int) i)))
                n.velocity = v;
        }
        repaint();
        return;
    }

    if (dragFrom.size() != selection.size() || selection.empty() || selection.back() >= (int) clip->notes.size())
        return;
    const auto anchorPos = std::find (selection.begin(), selection.end(), anchor);
    if (anchorPos == selection.end())
        return;
    const auto& a = dragFrom[(size_t) (anchorPos - selection.begin())];
    const double loop = loopLength();

    switch (drag)
    {
        case DragMode::Move:
        {
            // Every picked note moves by the anchor's step, kept inside the loop and the keyboard
            double dBeat = NoteEdit::snap (xToBeat (e.position.x) - grabOffset, step, snapOn) - a.start;
            int dPitch = juce::jlimit (0, 127, yToPitch (e.position.y)) - a.pitch;
            for (auto& f : dragFrom)
            {
                dBeat = juce::jlimit (-f.start, juce::jmax (0.0, loop - f.length) - f.start, dBeat);
                dPitch = juce::jlimit (-f.pitch, 127 - f.pitch, dPitch);
            }
            const int before = clip->notes[(size_t) anchor].pitch;
            for (size_t k = 0; k < selection.size(); ++k)
            {
                auto& n = clip->notes[(size_t) selection[k]];
                n.start = dragFrom[k].start + dBeat;
                n.pitch = dragFrom[k].pitch + dPitch;
            }
            if (clip->notes[(size_t) anchor].pitch != before)
                audition (clip->notes[(size_t) anchor].pitch, clip->notes[(size_t) anchor].velocity);
            break;
        }
        case DragMode::Resize:
        {
            const double minLen = snapOn ? step : NoteEdit::freeMinLength;
            const double newLen = juce::jmax (minLen, NoteEdit::snap (xToBeat (e.position.x) - a.start, step, snapOn));
            const double dLen = newLen - a.length;
            for (size_t k = 0; k < selection.size(); ++k)
            {
                auto& n = clip->notes[(size_t) selection[k]];
                n.length = juce::jlimit (minLen, juce::jmax (minLen, loop - n.start), dragFrom[k].length + dLen);
            }
            lastLength = clip->notes[(size_t) anchor].length;
            break;
        }
        case DragMode::Velocity:
        {
            const float dv = - (float) e.getDistanceFromDragStartY() / 120.0f;
            for (size_t k = 0; k < selection.size(); ++k)
                clip->notes[(size_t) selection[k]].velocity = juce::jlimit (0.05f, 1.0f, dragFrom[k].velocity + dv);
            break;
        }
        case DragMode::VelocityLane:
        case DragMode::Box:
        case DragMode::None:
            break;
    }
    repaint();
}

void EditorView::mouseUp (const juce::MouseEvent&)
{
    if (drag == DragMode::Box)
    {
        box = {};
        repaint();
    }
    else if (drag != DragMode::None)
        edited();
    drag = DragMode::None;
}

void EditorView::mouseDoubleClick (const juce::MouseEvent& e)
{
    bool edge = false;
    const int hit = noteAt (e.position, edge);
    if (hit >= 0 && ! scissors.getToggleState() && ! e.mods.isShiftDown())
    {
        select ({ hit });
        deleteSelectedNote();
    }
}

void EditorView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    // Alt + scroll: the picked notes get louder or softer
    if (e.mods.isAltDown() && ! selection.empty())
    {
        if (auto* clip = midiClip())
        {
            const float d = std::abs (w.deltaY) >= std::abs (w.deltaX) ? w.deltaY : w.deltaX;
            if (std::abs (d) < 1e-4f)
                return;
            {
                const juce::SpinLock::ScopedLockType sl (ctx.project.lock);
                NoteEdit::nudgeVelocity (clip->notes, selection, juce::jlimit (-0.1f, 0.1f, d * (w.isReversed ? -0.25f : 0.25f)));
            }
            float avg = 0.0f;
            for (int i : selection) avg += clip->notes[(size_t) i].velocity;
            say ("velocity " + juce::String (juce::roundToInt (avg / (float) selection.size() * 127.0f)));
            ctx.project.dirty = true;
            repaint();
            return;
        }
    }
    lowNote = juce::jlimit (0, 127 - numRows, lowNote + (w.deltaY > 0 ? 2 : -2));
    repaint();
}

//==============================================================================

void EditorView::paint (juce::Graphics& g)
{
    auto& th = Theme::get();
    auto& t = ctx.selected();
    const auto c = th.pal (t.colourIndex);
    auto* clip = ctx.selectedClipPtr();

    g.fillAll (th.bg);

    // Toolbar: the track's name, then the buttons, the clip length between shorter and longer,
    // and on the right the clip's name (or a word saying what just happened)
    g.setColour (th.panel);
    g.fillRect (toolbar);
    g.setColour (th.line2);
    g.drawHorizontalLine (toolbar.getBottom() - 1, 0.0f, (float) getWidth());
    auto title = toolbar.reduced (14, 0).toFloat();
    const bool flashing = flash.isNotEmpty();
    Neon::paintLit (g, getWidth(), toolbar.getBottom(), Neon::scratchGlow(), 0.7f, [&] (juce::Graphics& gg, bool)
    {
        Neon::text (gg, t.name.toLowerCase(), title.getX(), title.getCentreY() + 7.0f, 21.0f, c, 0.9f);
        if (midiClip() != nullptr && ! lengthReadout.isEmpty())
            Neon::text (gg, NoteEdit::lengthName (loopLength()), (float) lengthReadout.getCentreX(), title.getCentreY() + 5.0f,
                        16.0f, th.pal (Pal::Sky), 0.75f, Neon::Align::centre);
        const auto right = flashing ? flash : (clip != nullptr ? clip->name.toLowerCase() : juce::String ("no clip selected"));
        Neon::label (gg, right, title.getRight(), title.getCentreY() + 4.0f, Neon::Align::right,
                     flashing ? th.themeColour() : juce::Colour (0xff6d7077), 12.0f);
    });

    if (clip == nullptr)
    {
        g.setColour (th.muted);
        g.setFont (th.body (14.0f));
        g.drawText ("Select a clip in Arrange to edit it, or double-click an empty lane to make one.",
                    getLocalBounds().withTrimmedTop (toolbar.getHeight()), juce::Justification::centred);
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

    // Keyboard and row shading. Keys of picked notes light up.
    static const bool black[] = { false, true, false, true, false, false, true, false, true, false, true, false };
    const float rh = rowHeight();
    std::vector<bool> pickedPitch (128, false);
    for (int i : selection)
        if (i < (int) clip->notes.size()) pickedPitch[(size_t) clip->notes[(size_t) i].pitch] = true;
    for (int row = 0; row < numRows; ++row)
    {
        const int note = lowNote + numRows - 1 - row;
        const auto key = juce::Rectangle<float> ((float) keys.getX(), (float) grid.getY() + (float) row * rh,
                                                 (float) keys.getWidth() - 4.0f, rh);
        const bool isBlack = black[note % 12];
        g.setColour (isBlack ? th.display : th.panel2.brighter (0.04f));
        g.fillRoundedRectangle (key.reduced (0.0f, 0.75f), 2.5f);
        if (pickedPitch[(size_t) note])
        {
            g.setColour (c.withAlpha (0.35f));
            g.fillRoundedRectangle (key.reduced (0.0f, 0.75f).removeFromRight (6.0f), 2.0f);
        }
        if (note % 12 == 0 || rh >= 22.0f)
        {
            static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            g.setColour (note % 12 == 0 ? th.muted : th.muted.withAlpha (0.45f));
            g.setFont (th.label (note % 12 == 0 ? 9.5f : 8.0f));
            g.drawText (juce::String (names[note % 12]) + juce::String (note / 12 - 1), key.reduced (9.0f, 0.0f).toNearestInt(),
                        juce::Justification::centredRight);
        }
        g.setColour (isBlack ? th.display.withAlpha (0.6f) : th.panel.withAlpha (0.5f));
        g.fillRect (juce::Rectangle<float> ((float) grid.getX(), key.getY(), (float) grid.getWidth(), rh).reduced (0.0f, 0.5f));
    }

    // Grid lines at the chosen size (thinned out when they would crowd), with beats and bars stronger
    const double loop = loopLength();
    const double step = gridStep();
    const bool fine = (float) step * beatWidth() >= 7.0f;
    for (double b = 0.0; b <= loop + 1e-6; b += juce::jmin (step, 1.0))
    {
        const float x = beatToX (b);
        const bool bar = std::fmod (b, beatsPerBar) < 1e-6, beat = std::fmod (b, 1.0) < 1e-6;
        const bool onGrid = std::abs (std::fmod (b + 1e-9, step)) < 1e-6;
        if (! beat && ! fine)
            continue;
        g.setColour (bar ? th.line.brighter (0.1f) : (beat ? (onGrid ? th.line : th.line.withAlpha (0.5f)) : th.line2));
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
    if (! snapOn)
    {
        g.setColour (th.pal (Pal::Amber).withAlpha (0.8f));
        g.setFont (th.label (8.5f));
        g.drawText ("SNAP OFF", juce::Rectangle<int> (grid.getRight() - 80, grid.getY() - 15, 80, 12), juce::Justification::centredRight);
    }

    // Notes, brighter when louder. Picked notes glow white-edged.
    for (size_t i = 0; i < clip->notes.size(); ++i)
    {
        const auto& n = clip->notes[i];
        if (n.pitch < lowNote || n.pitch >= lowNote + numRows || n.start >= loop)
            continue;
        auto r = noteBounds (n).reduced (1.0f, 1.5f);
        const bool sel = isSelected ((int) i);
        g.setColour (c.withAlpha ((sel ? 0.3f : 0.15f) + 0.35f * n.velocity));
        g.fillRoundedRectangle (r, 3.0f);
        if (sel)
        {
            juce::Path outline;
            outline.addRoundedRectangle (r.reduced (0.5f), 3.0f);
            Glow::strokeGlow (g, outline, Neon::mix (c, juce::Colours::white, 0.55f), 1.4f, 0.4f);
        }
        else
        {
            g.setColour (c);
            g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
        }
    }

    // The selection box, a dashed neon outline
    if (drag == DragMode::Box && ! box.isEmpty())
    {
        g.setColour (th.pal (Pal::Sky).withAlpha (0.06f));
        g.fillRoundedRectangle (box, 3.0f);
        juce::Path p, dashed;
        p.addRoundedRectangle (box, 3.0f);
        const float pattern[] = { 5.0f, 4.0f };
        juce::PathStrokeType (1.2f).createDashedStroke (dashed, p, pattern, 2);
        g.setColour (th.pal (Pal::Sky).withAlpha (0.9f));
        g.fillPath (dashed);
    }

    // Velocity lane
    g.setColour (th.panel);
    g.fillRoundedRectangle (velocityLane.toFloat(), 8.0f);
    g.setColour (th.line2);
    g.drawRoundedRectangle (velocityLane.toFloat().reduced (0.5f), 8.0f, 1.0f);
    g.setColour (th.muted);
    Glow::drawSpacedText (g, "VELOCITY", velocityLane.toFloat().translated (-68.0f, 0.0f).withWidth (64.0f),
                          th.label (7.5f), 1.0f, juce::Justification::centred);
    auto lane = velocityLane.toFloat().reduced (0.0f, 10.0f);
    for (size_t i = 0; i < clip->notes.size(); ++i)
    {
        const auto& n = clip->notes[i];
        if (n.start >= loop)
            continue;
        const bool sel = isSelected ((int) i);
        const float x = beatToX (n.start) + 4.0f;
        const float top = lane.getBottom() - lane.getHeight() * n.velocity;
        juce::Path stem;
        stem.startNewSubPath (x, lane.getBottom());
        stem.lineTo (x, top);
        Glow::strokeGlow (g, stem, sel ? th.text : c, 1.4f);
        g.setColour (sel ? th.text : c);
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
