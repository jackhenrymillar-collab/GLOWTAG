#include "ArrangerView.h"

ArrangerView::ArrangerView (AppContext& c) : ctx (c) {}

void ArrangerView::resized()
{
    if (pixelsPerBeat <= 0.0 && getWidth() > 0)
        pixelsPerBeat = getWidth() / (16.0 * beatsPerBar);
}

int ArrangerView::trackAtY (int y) const
{
    if (y < Layout::rulerHeight)
        return -1;
    const int i = (y - Layout::rulerHeight) / Layout::rowHeight;
    return i < (int) ctx.project.tracks.size() ? i : -1;
}

juce::Rectangle<float> ArrangerView::clipBounds (int track, const Clip& clip) const
{
    const float y = (float) (Layout::rulerHeight + track * Layout::rowHeight);
    return juce::Rectangle<float> (beatToX (clip.start), y, (float) (clip.length * pixelsPerBeat), (float) Layout::rowHeight)
               .reduced (1.5f, 4.0f);
}

bool ArrangerView::hitClip (juce::Point<int> p, int& track, int& clip, bool& onRightEdge) const
{
    track = trackAtY (p.y);
    if (track < 0)
        return false;
    auto& clips = ctx.project.tracks[(size_t) track]->clips;
    for (int i = (int) clips.size() - 1; i >= 0; --i)
    {
        auto r = clipBounds (track, clips[(size_t) i]);
        if (r.contains (p.toFloat()))
        {
            clip = i;
            onRightEdge = p.x > r.getRight() - 7.0f;
            return true;
        }
    }
    return false;
}

void ArrangerView::tick()
{
    const double ph = ctx.playheadBeats();
    if (! juce::exactlyEqual (ph, lastPlayhead))
    {
        lastPlayhead = ph;

        // Page along with the playhead while playing
        const double visible = getWidth() / pixelsPerBeat;
        if (ctx.engine.isPlaying() && drag == DragMode::None && (ph > viewStart + visible * 0.96 || ph < viewStart))
            viewStart = juce::jmax (0.0, ph - visible * 0.04);
        repaint();
    }
}

void ArrangerView::mouseMove (const juce::MouseEvent& e)
{
    int t, c;
    bool edge = false;
    if (e.y < Layout::rulerHeight)
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    else if (hitClip (e.getPosition(), t, c, edge))
        setMouseCursor (edge ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::DraggingHandCursor);
    else
        setMouseCursor (juce::MouseCursor::NormalCursor);
}

void ArrangerView::mouseDown (const juce::MouseEvent& e)
{
    drag = DragMode::None;

    // Ruler: the upper part moves the playhead, the lower strip sets the loop
    if (e.y < Layout::rulerHeight)
    {
        if (e.y >= Layout::rulerHeight - 10)
        {
            drag = DragMode::Loop;
            loopAnchor = juce::jmax (0.0, std::round (xToBeat ((float) e.x) / beatsPerBar) * beatsPerBar);
        }
        else
        {
            drag = DragMode::Position;
            ctx.engine.setPosition (juce::jmax (0.0, snapBeat (xToBeat ((float) e.x))));
        }
        repaint();
        return;
    }

    int t, c;
    bool edge = false;
    if (hitClip (e.getPosition(), t, c, edge))
    {
        ctx.selectClip (t, c);

        if (e.mods.isPopupMenu())
        {
            showClipMenu (t, c);
            return;
        }

        // Alt-drag copies the clip and drags the copy
        if (e.mods.isAltDown() && ! edge)
        {
            const juce::SpinLock::ScopedLockType sl (ctx.project.lock);
            auto& clips = ctx.project.tracks[(size_t) t]->clips;
            clips.push_back (clips[(size_t) c]);
            c = (int) clips.size() - 1;
            ctx.selectedClip = c;
        }

        dragTrack = t;
        dragClip = c;
        dragGrabOffset = xToBeat ((float) e.x) - ctx.project.tracks[(size_t) t]->clips[(size_t) c].start;
        drag = edge ? DragMode::ResizeClip : DragMode::MoveClip;
        return;
    }

    if (t >= 0)
    {
        ctx.selectClip (t, -1);
        repaint();
    }
}

void ArrangerView::mouseDrag (const juce::MouseEvent& e)
{
    switch (drag)
    {
        case DragMode::Position:
            ctx.engine.setPosition (juce::jmax (0.0, snapBeat (xToBeat ((float) e.x))));
            break;

        case DragMode::Loop:
        {
            const double b = juce::jmax (0.0, std::round (xToBeat ((float) e.x) / beatsPerBar) * beatsPerBar);
            if (std::abs (b - loopAnchor) >= beatsPerBar)
            {
                ctx.engine.loopStart = juce::jmin (b, loopAnchor);
                ctx.engine.loopEnd = juce::jmax (b, loopAnchor);
                ctx.engine.looping = true;
            }
            break;
        }

        case DragMode::MoveClip:
        {
            const int newTrack = juce::jmax (0, trackAtY (juce::jmax (Layout::rulerHeight, e.y)));
            const double newStart = juce::jmax (0.0, snapBeat (xToBeat ((float) e.x) - dragGrabOffset));
            const juce::SpinLock::ScopedLockType sl (ctx.project.lock);
            auto& from = ctx.project.tracks[(size_t) dragTrack]->clips;
            if (dragClip >= (int) from.size())
                break;
            from[(size_t) dragClip].start = newStart;
            if (newTrack != dragTrack && trackAtY (e.y) >= 0)
            {
                // Hop to another lane
                auto& to = ctx.project.tracks[(size_t) newTrack]->clips;
                to.push_back (from[(size_t) dragClip]);
                from.erase (from.begin() + dragClip);
                dragTrack = newTrack;
                dragClip = (int) to.size() - 1;
                ctx.selectedTrack = dragTrack;
                ctx.selectedClip = dragClip;
                ctx.engine.liveTrack = dragTrack;
            }
            break;
        }

        case DragMode::ResizeClip:
        {
            const juce::SpinLock::ScopedLockType sl (ctx.project.lock);
            auto& clip = ctx.project.tracks[(size_t) dragTrack]->clips[(size_t) dragClip];
            clip.length = juce::jmax (snap(), snapBeat (xToBeat ((float) e.x)) - clip.start);
            break;
        }

        case DragMode::None:
            break;
    }
    repaint();
}

void ArrangerView::mouseUp (const juce::MouseEvent&)
{
    if (drag == DragMode::MoveClip || drag == DragMode::ResizeClip)
        ctx.clipsChanged();
    drag = DragMode::None;
}

void ArrangerView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.y < Layout::rulerHeight)
        return;

    int t, c;
    bool edge;
    if (hitClip (e.getPosition(), t, c, edge))
    {
        if (onOpenEditor)
            onOpenEditor();
        return;
    }

    // Empty lane: make a new one-bar MIDI clip
    if (t >= 0)
    {
        Clip clip;
        clip.start = std::floor (xToBeat ((float) e.x) / beatsPerBar) * beatsPerBar;
        clip.length = clip.loopLength = beatsPerBar;
        clip.name = "New clip";
        {
            const juce::SpinLock::ScopedLockType sl (ctx.project.lock);
            ctx.project.tracks[(size_t) t]->clips.push_back (clip);
        }
        ctx.selectClip (t, (int) ctx.project.tracks[(size_t) t]->clips.size() - 1);
    }
}

void ArrangerView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        // Zoom around the mouse
        const double anchor = xToBeat ((float) e.x);
        pixelsPerBeat = juce::jlimit (3.0, 200.0, pixelsPerBeat * std::pow (1.6, (double) w.deltaY * 2.0));
        viewStart = juce::jmax (0.0, anchor - e.x / pixelsPerBeat);
    }
    else
    {
        const float delta = std::abs (w.deltaX) > std::abs (w.deltaY) ? w.deltaX : w.deltaY;
        const double maxStart = juce::jmax (0.0, Project::lengthBars * beatsPerBar - getWidth() / pixelsPerBeat);
        viewStart = juce::jlimit (0.0, maxStart, viewStart - delta * 400.0 / pixelsPerBeat);
    }
    repaint();
}

bool ArrangerView::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg"))
            return true;
    return false;
}

void ArrangerView::filesDropped (const juce::StringArray& files, int x, int y)
{
    const int t = trackAtY (y) >= 0 ? trackAtY (y) : ctx.selectedTrack;
    double start = juce::jmax (0.0, snapBeat (xToBeat ((float) x)));

    for (auto& f : files)
    {
        auto data = AudioData::load (juce::File (f));
        if (data == nullptr)
            continue;
        Clip clip;
        clip.kind = ClipKind::Audio;
        clip.audio = data;
        clip.name = data->fileName;
        clip.start = start;
        clip.length = juce::jmax (0.25, data->lengthSeconds() * ctx.project.bpm.load() / 60.0);
        clip.loopLength = clip.length;
        {
            const juce::SpinLock::ScopedLockType sl (ctx.project.lock);
            ctx.project.tracks[(size_t) t]->clips.push_back (clip);
        }
        start = clip.end();
        ctx.selectClip (t, (int) ctx.project.tracks[(size_t) t]->clips.size() - 1);
    }
}

void ArrangerView::deleteSelectedClip()
{
    if (ctx.selectedClipPtr() == nullptr)
        return;
    {
        const juce::SpinLock::ScopedLockType sl (ctx.project.lock);
        auto& clips = ctx.selected().clips;
        clips.erase (clips.begin() + ctx.selectedClip);
    }
    ctx.selectClip (ctx.selectedTrack, -1);
}

void ArrangerView::duplicateSelectedClip()
{
    auto* clip = ctx.selectedClipPtr();
    if (clip == nullptr)
        return;
    Clip copy = *clip;
    copy.start = clip->end();
    {
        const juce::SpinLock::ScopedLockType sl (ctx.project.lock);
        ctx.selected().clips.push_back (copy);
    }
    ctx.selectClip (ctx.selectedTrack, (int) ctx.selected().clips.size() - 1);
}

void ArrangerView::showClipMenu (int track, int clip)
{
    juce::PopupMenu menu;
    menu.addItem (1, "Open in editor");
    menu.addItem (2, "Duplicate");
    menu.addItem (3, "Delete");
    if (ctx.project.tracks[(size_t) track]->clips[(size_t) clip].kind == ClipKind::Midi)
    {
        juce::PopupMenu loop;
        for (int bars : { 1, 2, 4, 8 })
            loop.addItem (10 + bars, juce::String (bars) + (bars == 1 ? " bar" : " bars"));
        menu.addSubMenu ("Loop length", loop);
    }
    menu.showMenuAsync (juce::PopupMenu::Options(), [this, track, clip] (int r)
    {
        if (r == 1 && onOpenEditor) onOpenEditor();
        else if (r == 2) duplicateSelectedClip();
        else if (r == 3) deleteSelectedClip();
        else if (r > 10)
        {
            {
                const juce::SpinLock::ScopedLockType sl (ctx.project.lock);
                auto& clips = ctx.project.tracks[(size_t) track]->clips;
                if (clip < (int) clips.size())
                    clips[(size_t) clip].loopLength = (r - 10) * beatsPerBar;
            }
            ctx.clipsChanged();
        }
    });
}

void ArrangerView::drawClipContent (juce::Graphics& g, const Clip& clip, juce::Rectangle<float> r, juce::Colour c, float alpha, bool automation) const
{
    auto body = r.withTrimmedTop (14.0f).reduced (3.0f, 3.0f);
    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (r.toNearestInt());

    if (clip.kind == ClipKind::Audio && clip.audio != nullptr)
    {
        // Waveform from the file's overview
        const auto& peaks = clip.audio->peaks;
        const double secPerBeat = 60.0 / ctx.project.bpm.load();
        const float mid = body.getCentreY();
        juce::Path wave;
        for (float x = body.getX(); x < body.getRight(); x += 1.0f)
        {
            const double sec = (xToBeat (x) - clip.start) * secPerBeat;
            if (sec < 0.0 || sec >= clip.audio->lengthSeconds())
                continue;
            const float p = peaks[(size_t) juce::jlimit (0, (int) peaks.size() - 1, (int) (sec / clip.audio->lengthSeconds() * peaks.size()))];
            const float h = juce::jmax (0.5f, p * body.getHeight() * 0.5f);
            wave.addRectangle (x, mid - h, 1.0f, h * 2.0f);
        }
        g.setColour (c.withAlpha (0.8f * alpha));
        g.fillPath (wave);
        return;
    }

    if (clip.notes.empty() || clip.loopLength <= 0.0)
    {
        if (! automation)
            return;
        // Automation-style ramp for FX lanes
        juce::Path ramp;
        ramp.startNewSubPath (r.getX() + 4.0f, r.getBottom() - 4.0f);
        ramp.lineTo (r.getRight() - 4.0f, r.getY() + 16.0f);
        Glow::strokeGlow (g, ramp, c.withAlpha (0.8f * alpha), 1.0f);
        return;
    }

    int lo = 127, hi = 0;
    for (auto& n : clip.notes)
    {
        lo = juce::jmin (lo, n.pitch);
        hi = juce::jmax (hi, n.pitch);
    }
    const float range = (float) juce::jmax (6, hi - lo + 1);
    const float rowH = body.getHeight() / range;

    g.setColour (c.withAlpha (0.85f * alpha));
    for (double rep = 0.0; rep < clip.length; rep += clip.loopLength)
    {
        // Faint line where the loop repeats
        if (rep > 0.0)
        {
            g.setColour (c.withAlpha (0.25f * alpha));
            g.drawVerticalLine ((int) beatToX (clip.start + rep), body.getY(), body.getBottom());
            g.setColour (c.withAlpha (0.85f * alpha));
        }
        for (auto& n : clip.notes)
        {
            if (n.start >= clip.loopLength || rep + n.start >= clip.length)
                continue;
            const float x = beatToX (clip.start + rep + n.start);
            const float w = juce::jmax (1.5f, (float) (juce::jmin (n.length, clip.length - rep - n.start) * pixelsPerBeat) - 1.0f);
            const float y = body.getBottom() - (float) (n.pitch - lo + 1) * rowH;
            g.fillRoundedRectangle (x, y, w, juce::jmax (2.0f, rowH - 1.0f), 1.0f);
        }
    }
}

void ArrangerView::paint (juce::Graphics& g)
{
    auto& th = Theme::get();
    const float w = (float) getWidth();
    const float h = (float) getHeight();
    const auto accent = th.pal (Pal::Green);
    const auto loopCol = th.pal (Pal::Gold);
    const double firstBeat = std::floor (viewStart);
    const double lastBeat = xToBeat (w);

    g.fillAll (th.bg);

    // Ruler
    auto ruler = juce::Rectangle<float> (0.0f, 0.0f, w, (float) Layout::rulerHeight);
    g.setColour (th.panel);
    g.fillRect (ruler);

    // Loop region on the lower strip
    {
        const bool on = ctx.engine.looping.load();
        const float x1 = beatToX (ctx.engine.loopStart.load()), x2 = beatToX (ctx.engine.loopEnd.load());
        auto band = juce::Rectangle<float> (x1, ruler.getBottom() - 9.0f, x2 - x1, 7.0f);
        g.setColour (loopCol.withAlpha (on ? 0.22f : 0.08f));
        g.fillRoundedRectangle (band, 3.0f);
        g.setColour (loopCol.withAlpha (on ? 0.9f : 0.3f));
        g.drawRoundedRectangle (band, 3.0f, 1.0f);
        if (on)
        {
            g.setColour (loopCol.withAlpha (0.04f));
            g.fillRect (x1, ruler.getBottom(), x2 - x1, h);
        }
    }

    const int barStep = pixelsPerBeat * beatsPerBar >= 40.0 ? 1 : (pixelsPerBeat * beatsPerBar >= 20.0 ? 2 : 4);
    for (double beat = firstBeat; beat <= lastBeat; beat += 1.0)
    {
        const float x = beatToX (beat);
        const bool isBar = std::fmod (beat, beatsPerBar) < 0.001;
        if (isBar)
        {
            const int bar = (int) (beat / beatsPerBar);
            if (bar % barStep == 0)
            {
                g.setColour (th.muted);
                g.setFont (th.label (9.5f));
                g.drawText (juce::String (bar + 1), juce::Rectangle<float> (x + 4.0f, 2.0f, 30.0f, 14.0f).toNearestInt(),
                            juce::Justification::centredLeft);
                g.setColour (th.line);
                g.drawVerticalLine ((int) x, 3.0f, ruler.getBottom() - 10.0f);
            }
            g.setColour (th.line);
            g.drawVerticalLine ((int) x, ruler.getBottom(), h);
        }
        else if (pixelsPerBeat >= 10.0)
        {
            g.setColour (th.line2.withAlpha (0.6f));
            g.drawVerticalLine ((int) x, ruler.getBottom(), h);
        }
    }
    g.setColour (th.line2);
    g.drawHorizontalLine (Layout::rulerHeight - 1, 0.0f, w);

    // Lanes
    const int numTracks = (int) ctx.project.tracks.size();
    for (int i = 0; i < numTracks; ++i)
    {
        const float y = (float) (Layout::rulerHeight + i * Layout::rowHeight);
        if (i == ctx.selectedTrack)
        {
            g.setColour (th.pal (ctx.selected().colourIndex).withAlpha (0.05f));
            g.fillRect (0.0f, y, w, (float) Layout::rowHeight);
        }
        g.setColour (th.line2);
        g.drawHorizontalLine ((int) y + Layout::rowHeight - 1, 0.0f, w);
    }

    // Clips: translucent fill, thin outline, name, and their notes or waveform
    for (int i = 0; i < numTracks; ++i)
    {
        auto& t = *ctx.project.tracks[(size_t) i];
        const auto c = th.pal (t.colourIndex);
        const bool rolling = t.rolling.load();
        const float alpha = (ctx.project.isAudible (i) ? 1.0f : 0.4f) * (rolling ? 0.3f : 1.0f);

        if (rolling)
        {
            // The rack plays live: a banner across the lane with the running step lights
            const float y = (float) (Layout::rulerHeight + i * Layout::rowHeight);
            auto band = juce::Rectangle<float> (0.0f, y + 4.0f, w, (float) Layout::rowHeight - 9.0f);
            g.setColour (c.withAlpha (0.06f));
            g.fillRect (band);
            g.setColour (c.withAlpha (0.35f));
            g.drawHorizontalLine ((int) band.getY(), 0.0f, w);
            g.drawHorizontalLine ((int) band.getBottom(), 0.0f, w);
            int pos = -1, length = seqSteps;
            for (auto& m : t.seq.modules)
                if (m.type == SeqType::Steps)
                    if (auto* n = ctx.engine.liveSeqNode (i, m.id)) { pos = (int) n->display[0].load(); length = SeqKnob::count (n->knob[4].load(), 1, seqSteps); break; }
            g.setColour (th.text.withAlpha (0.85f));
            Glow::drawSpacedText (g, juce::String::fromUTF8 ("ROLLING  \xc2\xb7  ") + t.name.toUpperCase() + juce::String::fromUTF8 ("  \xc2\xb7  ")
                                     + juce::String (length) + juce::String::fromUTF8 (" STEP  \xc2\xb7  \xe2\x88\x9e"),
                                  band.withTrimmedLeft (12.0f), th.label (8.5f), 1.6f, juce::Justification::centredLeft);
            for (int s = 0; s < length; ++s)
            {
                const float x = band.getRight() - 24.0f - (float) (length - 1 - s) * 9.0f;
                const bool lit = s == pos && ctx.engine.isPlaying();
                g.setColour (lit ? c : c.withAlpha (0.3f));
                g.fillEllipse (x - (lit ? 3.0f : 2.0f), band.getCentreY() - (lit ? 3.0f : 2.0f), lit ? 6.0f : 4.0f, lit ? 6.0f : 4.0f);
            }
            g.setColour (th.muted.withAlpha (0.7f));
            g.setFont (th.label (7.5f));
            g.drawText ("LIVE FROM RACK", band.withTrimmedRight (12.0f + 9.0f * (float) length + 20.0f).toNearestInt(), juce::Justification::centredRight);
        }

        for (size_t ci = 0; ci < t.clips.size(); ++ci)
        {
            const auto& clip = t.clips[ci];
            auto r = clipBounds (i, clip);
            if (r.getRight() < 0.0f || r.getX() > w)
                continue;
            const bool selected = i == ctx.selectedTrack && (int) ci == ctx.selectedClip;

            g.setColour (c.withAlpha ((selected ? 0.26f : 0.16f) * alpha));
            g.fillRoundedRectangle (r, 5.0f);
            g.setColour (c.withAlpha ((selected ? 1.0f : 0.7f) * alpha));
            g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, selected ? 1.6f : 1.0f);

            g.setColour (th.text.withAlpha (0.9f * alpha));
            g.setFont (th.label (9.0f));
            g.drawText (clip.name.toUpperCase(), r.reduced (7.0f, 2.0f).removeFromTop (12.0f).toNearestInt(),
                        juce::Justification::centredLeft, true);

            drawClipContent (g, clip, r, c, alpha, t.kind == TrackKind::Automation);
        }
    }

    // Playhead
    const double ph = ctx.playheadBeats();
    const float x = beatToX (ph);
    if (x >= 0.0f && x <= w)
    {
        juce::Path line;
        line.startNewSubPath (x, 0.0f);
        line.lineTo (x, h);
        Glow::strokeGlow (g, line, accent, 1.0f, ctx.engine.getPulse());
        juce::Path cap;
        cap.addTriangle (x - 5.0f, 0.0f, x + 5.0f, 0.0f, x, 7.0f);
        g.setColour (accent);
        g.fillPath (cap);
    }
}
