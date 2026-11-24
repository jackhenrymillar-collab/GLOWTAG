#include "ArrangerView.h"

namespace
{
    constexpr float pointRadius = 3.4f;
    // Lanes go with a clip to another track only where that track has the same control
    bool laneFits (const Track& to, const Track& from, const AutoTarget& t)
    {
        if (t.kind == AutoTarget::mixer) return true;
        if (t.kind != AutoTarget::knob && t.kind != AutoTarget::morph) return false;
        const auto* a = to.patch.find (t.module);
        const auto* b = from.patch.find (t.module);
        return a != nullptr && b != nullptr && a->type == b->type;
    }
}

ArrangerView::ArrangerView (AppContext& c) : ctx (c) {}

void ArrangerView::resized()
{
    if (pixelsPerBeat <= 0.0 && getWidth() > 0)
        pixelsPerBeat = getWidth() / (16.0 * beatsPerBar);
    ctx.arrangeHeight = getHeight();
}

double ArrangerView::fineSnap (double beat, bool free) const
{
    if (free) return juce::jmax (0.0, beat);
    const double g = pixelsPerBeat >= 24.0 ? 0.25 : pixelsPerBeat >= 10.0 ? 0.5 : 1.0;
    return juce::jmax (0.0, std::round (beat / g) * g);
}

int ArrangerView::rowAtY (int y) const
{
    if (y < Layout::rulerHeight)
        return -1;
    const int i = (y - Layout::rulerHeight) / rowH();
    return i < ctx.numRows() ? i : -1;
}

int ArrangerView::trackAtY (int y) const
{
    const int r = rowAtY (y);
    return r >= 0 && r < (int) ctx.project.tracks.size() ? r : -1;
}

juce::Rectangle<float> ArrangerView::clipBounds (int track, const Clip& clip) const
{
    const float y = (float) rowTop (track);
    return juce::Rectangle<float> (beatToX (clip.start), y, (float) (clip.length * pixelsPerBeat), (float) rowH())
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

// With lanes showing, a clip is held by its name strip (and its right edge for resizing)
bool ArrangerView::onClipStrip (juce::Point<int> p, int& track, int& clip, bool& edge) const
{
    if (! hitClip (p, track, clip, edge)) return false;
    const auto r = clipBounds (track, ctx.project.tracks[(size_t) track]->clips[(size_t) clip]);
    return (float) p.y <= r.getY() + 14.0f || edge;
}

juce::Colour ArrangerView::laneColour (int track) const
{
    auto& th = Theme::get();
    if (track == AppContext::visSlot) return th.themeColour();
    return th.pal (ctx.project.tracks[(size_t) track]->colourIndex);
}

AutoLane* ArrangerView::shownLane (int track)
{
    auto* lanes = ctx.lanesOf (track);
    return lanes != nullptr ? Auto::find (*lanes, ctx.shownTarget (track)) : nullptr;
}

AutoLane& ArrangerView::makeShownLane (int track)
{
    auto* lanes = ctx.lanesOf (track);
    jassert (lanes != nullptr);
    const auto target = ctx.shownTarget (track);
    const auto info = ctx.describe (track, target);
    const juce::SpinLock::ScopedLockType sl (ctx.project.lock);
    auto& lane = Auto::get (*lanes, target);
    if (lane.name.isEmpty())
    {
        lane.name = (info.group + "  " + info.name).trim();
        if (info.hi > info.lo) { lane.lo = info.lo; lane.hi = info.hi; lane.stepped = info.stepped; }
    }
    return lane;
}

int ArrangerView::pointAt (int row, juce::Point<float> p) const
{
    auto* lane = const_cast<ArrangerView*> (this)->shownLane (ctx.trackOfRow (row));
    if (lane == nullptr) return -1;
    int best = -1;
    float bestD = 8.0f;
    for (int i = 0; i < (int) lane->points.size(); ++i)
    {
        const auto& q = lane->points[(size_t) i];
        const float d = p.getDistanceFrom ({ beatToX (q.beat), valueToY (row, q.value) });
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

juce::Point<float> ArrangerView::curveHandle (int row, const std::vector<AutoPoint>& pts, int seg) const
{
    const auto& a = pts[(size_t) seg];
    const auto& b = pts[(size_t) seg + 1];
    const float v = a.value + (b.value - a.value) * Auto::shape (0.5f, a.curve);
    return { beatToX ((a.beat + b.beat) * 0.5), valueToY (row, v) };
}

int ArrangerView::curveHandleAt (int row, juce::Point<float> p) const
{
    auto* lane = const_cast<ArrangerView*> (this)->shownLane (ctx.trackOfRow (row));
    if (lane == nullptr) return -1;
    for (int i = 0; i + 1 < (int) lane->points.size(); ++i)
    {
        const auto& a = lane->points[(size_t) i];
        const auto& b = lane->points[(size_t) i + 1];
        if (std::abs (a.value - b.value) < 0.01f || beatToX (b.beat) - beatToX (a.beat) < 18.0f) continue;
        if (p.getDistanceFrom (curveHandle (row, lane->points, i)) < 7.0f) return i;
    }
    return -1;
}

juce::Rectangle<float> ArrangerView::selectionBox (int row) const
{
    if (selection.size() < 2 || ctx.rowOfTrack (selTrack) != row) return {};
    auto* lane = const_cast<ArrangerView*> (this)->shownLane (selTrack);
    if (lane == nullptr || lane->target != selTarget) return {};
    float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
    for (int i : selection)
    {
        if (i < 0 || i >= (int) lane->points.size()) continue;
        const auto& q = lane->points[(size_t) i];
        x0 = juce::jmin (x0, beatToX (q.beat)); x1 = juce::jmax (x1, beatToX (q.beat));
        y0 = juce::jmin (y0, valueToY (row, q.value)); y1 = juce::jmax (y1, valueToY (row, q.value));
    }
    if (x1 < x0) return {};
    return juce::Rectangle<float>::leftTopRightBottom (x0, y0, x1, y1).expanded (7.0f, 5.0f);
}

ArrangerView::BoxPart ArrangerView::boxPartAt (int row, juce::Point<float> p) const
{
    const auto box = selectionBox (row);
    if (box.isEmpty() || ! box.expanded (4.0f).contains (p)) return BoxPart::None;
    const bool nearTop = std::abs (p.y - box.getY()) < 5.0f;
    const bool nearL = std::abs (p.x - box.getX()) < 6.0f, nearR = std::abs (p.x - box.getRight()) < 6.0f;
    if (nearTop && nearL) return BoxPart::TopLeft;
    if (nearTop && nearR) return BoxPart::TopRight;
    if (nearTop) return BoxPart::Top;
    if (nearL) return BoxPart::Left;
    if (nearR) return BoxPart::Right;
    return box.contains (p) ? BoxPart::Move : BoxPart::None;
}

void ArrangerView::clearPointSelection()
{
    selection.clear();
    selTrack = -100;
    repaint();
}

void ArrangerView::tick()
{
    if (shownAutoGeneration != ctx.autoGeneration || shownLanes != ctx.lanesShown)
    {
        shownAutoGeneration = ctx.autoGeneration;
        shownLanes = ctx.lanesShown;
        if (! shownLanes) selection.clear();
        repaint();
    }

    const double ph = ctx.playheadBeats();
    if (! juce::exactlyEqual (ph, lastPlayhead))
    {
        lastPlayhead = ph;

        // Page along with the playhead while playing
        const double visible = getWidth() / pixelsPerBeat;
        const double startBefore = viewStart;
        if (ctx.engine.isPlaying() && drag == DragMode::None && (ph > viewStart + visible * 0.96 || ph < viewStart))
            viewStart = juce::jmax (0.0, ph - visible * 0.04);
        if (! juce::exactlyEqual (startBefore, viewStart))
            repaint();
        else
        {
            // Only the thin strips the playhead leaves and enters are drawn again, not every clip
            const int x = juce::roundToInt (beatToX (ph));
            if (std::abs (x - shownPlayheadX) <= 40)
                repaint (juce::jmin (x, shownPlayheadX) - 9, 0, std::abs (x - shownPlayheadX) + 19, getHeight());
            else
            {
                repaint (shownPlayheadX - 9, 0, 19, getHeight());
                repaint (x - 9, 0, 19, getHeight());
            }
        }
        shownPlayheadX = juce::roundToInt (beatToX (ph));
    }
    else if (ctx.engine.isPlaying() && ! juce::approximatelyEqual (shownPulse, ctx.engine.getPulse()))
        repaint (shownPlayheadX - 9, 0, 19, getHeight());
    shownPulse = ctx.engine.getPulse();

    // A rolling rack's lane shows its running step lights
    if (ctx.engine.isPlaying())
        for (int i = 0; i < (int) ctx.project.tracks.size(); ++i)
            if (ctx.project.tracks[(size_t) i]->rolling.load())
                repaint (0, rowTop (i), getWidth(), rowH());
}

void ArrangerView::mouseExit (const juce::MouseEvent&)
{
    if (hoverRow >= 0) { hoverRow = -1; repaint(); }
}

void ArrangerView::mouseMove (const juce::MouseEvent& e)
{
    const int row = rowAtY (e.y);
    if (lanesOn() && row != hoverRow)
    {
        hoverRow = row;
        repaint();
    }
    int t, c;
    bool edge = false;
    if (e.y < Layout::rulerHeight)
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    else if (lanesOn() && row >= 0)
    {
        const auto part = boxPartAt (row, e.position);
        if (part == BoxPart::Top) setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        else if (part == BoxPart::TopLeft) setMouseCursor (juce::MouseCursor::TopLeftCornerResizeCursor);
        else if (part == BoxPart::TopRight) setMouseCursor (juce::MouseCursor::TopRightCornerResizeCursor);
        else if (part == BoxPart::Left || part == BoxPart::Right) setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
        else if (pointAt (row, e.position) >= 0) setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        else if (curveHandleAt (row, e.position) >= 0) setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        else if (part == BoxPart::Move) setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        else if (onClipStrip (e.getPosition(), t, c, edge)) setMouseCursor (edge ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::DraggingHandCursor);
        else setMouseCursor (juce::MouseCursor::CrosshairCursor);
    }
    else if (hitClip (e.getPosition(), t, c, edge))
        setMouseCursor (edge ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::DraggingHandCursor);
    else
        setMouseCursor (juce::MouseCursor::NormalCursor);
}

//==============================================================================
// Carrying automation with a clip

void ArrangerView::startCarry (int track, int clip, bool copy)
{
    carry = {};
    if (! ctx.stickToClips) return;
    auto& t = *ctx.project.tracks[(size_t) track];
    const auto& c = t.clips[(size_t) clip];
    carry.active = true;
    carry.copy = copy;
    carry.fromTrack = track;
    carry.a0 = c.start;
    carry.b0 = c.end();
    const juce::SpinLock::ScopedLockType sl (ctx.project.lock);
    carry.fromSnap = t.lanes;
    carry.fromBase = t.lanes;
    carry.carried = t.lanes;
    for (auto& l : carry.carried) l.points = Auto::slice (l.points, carry.a0, carry.b0);
    if (! copy)
        for (auto& l : carry.fromBase) Auto::removeRange (l.points, carry.a0, carry.b0 - 0.0005);
}

void ArrangerView::updateCarry (int track, double start)
{
    if (! carry.active) return;
    auto& from = *ctx.project.tracks[(size_t) carry.fromTrack];
    const double len = carry.b0 - carry.a0;
    const juce::SpinLock::ScopedLockType sl (ctx.project.lock);
    if (track == carry.fromTrack && std::abs (start - carry.a0) < 1.0e-9 && ! carry.copy)
    {
        from.lanes = carry.fromSnap;          // back where it started: exactly as it was
        for (auto& [ti, snapLanes] : carry.destSnaps) ctx.project.tracks[(size_t) ti]->lanes = snapLanes;
        return;
    }
    from.lanes = carry.fromBase;
    for (auto& [ti, snapLanes] : carry.destSnaps)
        if (ti != track) ctx.project.tracks[(size_t) ti]->lanes = snapLanes;
    auto& to = *ctx.project.tracks[(size_t) track];
    if (track != carry.fromTrack)
    {
        if (carry.destSnaps.count (track) == 0) carry.destSnaps[track] = to.lanes;
        to.lanes = carry.destSnaps[track];
    }
    for (auto& c : carry.carried)
    {
        if (c.points.empty() || (track != carry.fromTrack && ! laneFits (to, from, c.target))) continue;
        auto& lane = Auto::get (to.lanes, c.target);
        if (lane.name.isEmpty()) { lane.name = c.name; lane.lo = c.lo; lane.hi = c.hi; lane.stepped = c.stepped; lane.snaps = c.snaps; lane.morphStepped = c.morphStepped; }
        Auto::paste (lane.points, c.points, start, len);
    }
}

//==============================================================================

void ArrangerView::mouseDown (const juce::MouseEvent& e)
{
    drag = DragMode::None;
    dragStartPos = e.position;

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

    const int row = rowAtY (e.y);
    int t, c;
    bool edge = false;

    if (lanesOn() && row >= 0)
    {
        const int track = ctx.trackOfRow (row);
        if (ctx.isTrack (track)) ctx.selectTrack (track);
        if (e.mods.isPopupMenu())
        {
            if (! onClipStrip (e.getPosition(), t, c, edge))
            {
                showLaneMenu (track, e.getPosition());
                return;
            }
        }
        else
        {
            // The selection box's edges and inside
            if (const auto part = boxPartAt (row, e.position); part != BoxPart::None && pointAt (row, e.position) < 0)
            {
                auto* lane = shownLane (track);
                if (lane != nullptr)
                {
                    drag = DragMode::Box;
                    boxPart = part;
                    dragFrom = lane->points;
                    dragSel = selection;
                    return;
                }
            }
            // A point: drag it (Alt-click a point to remove it)
            if (const int p = pointAt (row, e.position); p >= 0)
            {
                auto* lane = shownLane (track);
                if (e.mods.isAltDown())
                {
                    { const juce::SpinLock::ScopedLockType sl (ctx.project.lock); lane->points.erase (lane->points.begin() + p); }
                    clearPointSelection();
                    lanesEdited();
                    return;
                }
                const bool inSel = selTrack == track && std::find (selection.begin(), selection.end(), p) != selection.end();
                if (inSel && selection.size() > 1)
                {
                    drag = DragMode::Box;
                    boxPart = BoxPart::Move;
                    dragFrom = lane->points;
                    dragSel = selection;
                    return;
                }
                selTrack = track; selTarget = lane->target; selection = { p };
                drag = DragMode::Point;
                dragPoint = p;
                dragFrom = lane->points;
                repaint();
                return;
            }
            // The ring between two points: bend that stretch
            if (const int s = curveHandleAt (row, e.position); s >= 0)
            {
                drag = DragMode::Curve;
                dragSegment = s;
                dragFrom = shownLane (track)->points;
                return;
            }
        }
        if (onClipStrip (e.getPosition(), t, c, edge))
        {
            // fall through to the clip handling below
        }
        else if (! e.mods.isPopupMenu())
        {
            // Empty lane: drag a box to select points
            selection.clear();
            selTrack = track;
            selTarget = ctx.shownTarget (track);
            drag = DragMode::Band;
            band = { e.position, e.position };
            if (ctx.isTrack (track))
                if (hitClip (e.getPosition(), t, c, edge)) ctx.selectClip (t, c);
            repaint();
            return;
        }
        else
            return;
    }

    if (hitClip (e.getPosition(), t, c, edge))
    {
        ctx.selectClip (t, c);

        if (e.mods.isPopupMenu())
        {
            showClipMenu (t, c);
            return;
        }

        // Alt-drag copies the clip and drags the copy
        const bool copy = e.mods.isAltDown() && ! edge;
        if (copy)
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
        if (drag == DragMode::MoveClip) startCarry (t, c, copy);
        return;
    }

    if (row >= 0 && ctx.isTrack (ctx.trackOfRow (row)))
    {
        ctx.selectClip (ctx.trackOfRow (row), -1);
        repaint();
    }
}

void ArrangerView::applyBox (juce::Point<float> to, bool free)
{
    auto* lane = shownLane (selTrack);
    if (lane == nullptr || dragSel.empty()) return;
    const int row = ctx.rowOfTrack (selTrack);
    double b0 = 1e9, b1 = -1e9;
    float v0 = 1.0f, v1 = 0.0f;
    for (int i : dragSel)
    {
        const auto& q = dragFrom[(size_t) i];
        b0 = juce::jmin (b0, q.beat); b1 = juce::jmax (b1, q.beat);
        v0 = juce::jmin (v0, q.value); v1 = juce::jmax (v1, q.value);
    }
    const double dBeat = (to.x - dragStartPos.x) / pixelsPerBeat;
    const float dVal = yToValue (row, to.y) - yToValue (row, dragStartPos.y);
    const float span = juce::jmax (0.02f, v1 - v0);
    const float factor = juce::jmax (0.0f, (span + dVal) / span);

    struct Tagged { AutoPoint p; bool sel; };
    std::vector<Tagged> all;
    for (size_t i = 0; i < dragFrom.size(); ++i)
        all.push_back ({ dragFrom[i], std::find (dragSel.begin(), dragSel.end(), (int) i) != dragSel.end() });
    for (auto& tg : all)
    {
        if (! tg.sel) continue;
        auto& q = tg.p;
        const double rel = b1 > b0 ? (q.beat - b0) / (b1 - b0) : 0.0;
        switch (boxPart)
        {
            case BoxPart::Move:
            {
                const double shift = fineSnap (b0 + dBeat, free) - b0;
                q.beat = juce::jmax (0.0, q.beat + shift);
                q.value = juce::jlimit (0.0f, 1.0f, q.value + dVal);
                break;
            }
            case BoxPart::Top:      q.value = juce::jlimit (0.0f, 1.0f, v0 + (q.value - v0) * factor); break;
            case BoxPart::TopLeft:  q.value = juce::jlimit (0.0f, 1.0f, v0 + (q.value - v0) * (1.0f + (factor - 1.0f) * (float) (1.0 - rel))); break;
            case BoxPart::TopRight: q.value = juce::jlimit (0.0f, 1.0f, v0 + (q.value - v0) * (1.0f + (factor - 1.0f) * (float) rel)); break;
            case BoxPart::Left:
            {
                const double nb0 = juce::jmin (b1 - 0.0625, fineSnap (b0 + dBeat, free));
                q.beat = b1 - (b1 - q.beat) * (b1 - nb0) / juce::jmax (1.0e-6, b1 - b0);
                break;
            }
            case BoxPart::Right:
            {
                const double nb1 = juce::jmax (b0 + 0.0625, fineSnap (b1 + dBeat, free));
                q.beat = b0 + (q.beat - b0) * (nb1 - b0) / juce::jmax (1.0e-6, b1 - b0);
                break;
            }
            case BoxPart::None: break;
        }
    }
    std::stable_sort (all.begin(), all.end(), [] (const Tagged& a, const Tagged& b) { return a.p.beat < b.p.beat; });
    std::vector<AutoPoint> pts;
    selection.clear();
    for (size_t i = 0; i < all.size(); ++i)
    {
        pts.push_back (all[i].p);
        if (all[i].sel) selection.push_back ((int) i);
    }
    const juce::SpinLock::ScopedLockType sl (ctx.project.lock);
    lane->points = pts;
}

void ArrangerView::mouseDrag (const juce::MouseEvent& e)
{
    const bool free = e.mods.isShiftDown();
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
            {
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
            }
            updateCarry (dragTrack, newStart);
            break;
        }

        case DragMode::ResizeClip:
        {
            const juce::SpinLock::ScopedLockType sl (ctx.project.lock);
            auto& clip = ctx.project.tracks[(size_t) dragTrack]->clips[(size_t) dragClip];
            clip.length = juce::jmax (snap(), snapBeat (xToBeat ((float) e.x)) - clip.start);
            break;
        }

        case DragMode::Point:
        {
            auto* lane = shownLane (selTrack);
            if (lane == nullptr || dragPoint < 0 || dragPoint >= (int) lane->points.size()) break;
            const int row = ctx.rowOfTrack (selTrack);
            const auto& orig = dragFrom[(size_t) dragPoint];
            const double lo = dragPoint > 0 ? dragFrom[(size_t) dragPoint - 1].beat : 0.0;
            const double hi = dragPoint + 1 < (int) dragFrom.size() ? dragFrom[(size_t) dragPoint + 1].beat : 1.0e9;
            const double b = juce::jlimit (lo, hi, fineSnap (orig.beat + (e.position.x - dragStartPos.x) / pixelsPerBeat, free));
            const float v = e.mods.isCommandDown() ? orig.value + (yToValue (row, e.position.y) - yToValue (row, dragStartPos.y)) * 0.2f
                                                   : yToValue (row, e.position.y);
            const juce::SpinLock::ScopedLockType sl (ctx.project.lock);
            lane->points[(size_t) dragPoint].beat = b;
            lane->points[(size_t) dragPoint].value = juce::jlimit (0.0f, 1.0f, lane->stepped && lane->hi > lane->lo ? lane->norm (lane->raw (v)) : v);
            break;
        }

        case DragMode::Curve:
        {
            auto* lane = shownLane (ctx.trackOfRow (rowAtY ((int) dragStartPos.y)));
            if (lane == nullptr || dragSegment < 0 || dragSegment + 1 >= (int) lane->points.size()) break;
            const int row = rowAtY ((int) dragStartPos.y);
            const auto& a = lane->points[(size_t) dragSegment];
            const auto& b = lane->points[(size_t) dragSegment + 1];
            if (std::abs (b.value - a.value) < 0.01f) break;
            const float target = yToValue (row, e.position.y);
            const float mid = (target - a.value) / (b.value - a.value);
            const juce::SpinLock::ScopedLockType sl (ctx.project.lock);
            lane->points[(size_t) dragSegment].curve = Auto::curveFor (mid);
            break;
        }

        case DragMode::Band:
        {
            band = juce::Rectangle<float> (dragStartPos, e.position);
            selection.clear();
            if (auto* lane = shownLane (selTrack))
            {
                const int row = ctx.rowOfTrack (selTrack);
                const auto area = band.withTop (juce::jmin (band.getY(), laneTop (row))).withBottom (juce::jmax (band.getBottom(), laneBottom (row)));
                for (int i = 0; i < (int) lane->points.size(); ++i)
                    if (area.contains (beatToX (lane->points[(size_t) i].beat), valueToY (row, lane->points[(size_t) i].value)))
                        selection.push_back (i);
            }
            break;
        }

        case DragMode::Box:
            applyBox (e.position, free);
            break;

        case DragMode::None:
            break;
    }
    repaint();
}

void ArrangerView::mouseUp (const juce::MouseEvent&)
{
    if (drag == DragMode::MoveClip || drag == DragMode::ResizeClip)
    {
        if (carry.active) ++ctx.autoGeneration;
        ctx.clipsChanged();
    }
    if (drag == DragMode::Point || drag == DragMode::Curve || drag == DragMode::Box)
    {
        if (auto* lane = shownLane (selTrack); lane != nullptr && drag != DragMode::Curve) { const juce::SpinLock::ScopedLockType sl (ctx.project.lock); Auto::sort (lane->points); }
        lanesEdited();
    }
    if (drag == DragMode::Band) band = {};
    carry = {};
    drag = DragMode::None;
    repaint();
}

void ArrangerView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.y < Layout::rulerHeight)
        return;

    const int row = rowAtY (e.y);
    int t, c;
    bool edge;

    if (lanesOn() && row >= 0 && ! onClipStrip (e.getPosition(), t, c, edge))
    {
        const int track = ctx.trackOfRow (row);
        // On a point: remove it.  On a ring: straighten.  Anywhere else on the lane: a new point.
        if (const int p = pointAt (row, e.position); p >= 0)
        {
            auto* lane = shownLane (track);
            { const juce::SpinLock::ScopedLockType sl (ctx.project.lock); lane->points.erase (lane->points.begin() + p); }
            clearPointSelection();
        }
        else if (const int s = curveHandleAt (row, e.position); s >= 0)
        {
            const juce::SpinLock::ScopedLockType sl (ctx.project.lock);
            shownLane (track)->points[(size_t) s].curve = 0.0f;
        }
        else
        {
            auto& lane = makeShownLane (track);
            float v = yToValue (row, e.position.y);
            if (lane.stepped && lane.hi > lane.lo) v = lane.norm (lane.raw (v));
            const juce::SpinLock::ScopedLockType sl (ctx.project.lock);
            const int i = Auto::add (lane.points, fineSnap (xToBeat (e.position.x), e.mods.isShiftDown()), v);
            selTrack = track; selTarget = lane.target; selection = { i };
        }
        lanesEdited();
        return;
    }

    if (hitClip (e.getPosition(), t, c, edge))
    {
        if (onOpenEditor)
            onOpenEditor();
        return;
    }

    // Empty lane: make a new one-bar MIDI clip
    t = trackAtY (e.y);
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
    const int t = trackAtY (y) >= 0 ? trackAtY (y) : juce::jmin (ctx.selectedTrack, (int) ctx.project.tracks.size() - 1);
    double start = juce::jmax (0.0, snapBeat (xToBeat ((float) x)));

    for (auto& f : files)
    {
        auto data = AudioData::load (juce::File (f));
        if (data == nullptr)
            continue;
        Clip clip;
        clip.kind = ClipKind::Audio;
        clip.audio = data;
        clip.audioPath = f;
        clip.name = data->fileName;
        clip.start = start;
        clip.length = juce::jmax (0.25, data->lengthSeconds() * ctx.project.bpm.load() / 60.0);
        clip.loopLength = clip.length;
        {
            const juce::SpinLock::ScopedLockType sl (ctx.project.lock);
            ctx.project.tracks[(size_t) t]->clips.push_back (clip);
        }
        // On a track that only played an instrument, the track's own sound may not be patched in yet
        if (ctx.project.tracks[(size_t) t]->patch.patchTrackSound())
            ctx.engine.rebuildTrack (t);
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
        if (ctx.stickToClips)
            Auto::clipCopied (ctx.selected().lanes, clip->start, clip->end(), copy.start);
        ctx.selected().clips.push_back (copy);
    }
    if (ctx.stickToClips) ++ctx.autoGeneration;
    ctx.selectClip (ctx.selectedTrack, (int) ctx.selected().clips.size() - 1);
}

void ArrangerView::copySelectedClip()
{
    auto* clip = ctx.selectedClipPtr();
    if (clip == nullptr || ! ctx.selectedIsTrack())
        return;
    auto& cb = ctx.clipBoard;
    cb = {};
    cb.full = true;
    cb.clip = *clip;
    cb.fromTrack = ctx.selectedTrack;
    const juce::SpinLock::ScopedLockType sl (ctx.project.lock);
    for (auto& l : ctx.selected().lanes)
    {
        auto part = l;
        part.points = Auto::slice (l.points, clip->start, clip->end());
        part.ghost.clear();
        if (! part.points.empty()) cb.lanes.push_back (part);
    }
}

void ArrangerView::cutSelectedClip()
{
    copySelectedClip();
    deleteSelectedClip();
}

void ArrangerView::pasteClip()
{
    auto& cb = ctx.clipBoard;
    if (! cb.full || ! ctx.selectedIsTrack())
        return;
    const int track = ctx.selectedTrack;
    auto& to = *ctx.project.tracks[(size_t) track];
    Clip clip = cb.clip;
    clip.start = juce::jmax (0.0, std::floor (ctx.playheadBeats() / snap()) * snap());
    const Track* from = cb.fromTrack >= 0 && cb.fromTrack < (int) ctx.project.tracks.size() ? ctx.project.tracks[(size_t) cb.fromTrack].get() : nullptr;
    {
        const juce::SpinLock::ScopedLockType sl (ctx.project.lock);
        to.clips.push_back (clip);
        if (ctx.stickToClips)
            for (auto& c : cb.lanes)
            {
                if (from != nullptr && from != &to && ! laneFits (to, *from, c.target)) continue;
                auto& lane = Auto::get (to.lanes, c.target);
                if (lane.name.isEmpty()) { lane.name = c.name; lane.lo = c.lo; lane.hi = c.hi; lane.stepped = c.stepped; lane.snaps = c.snaps; lane.morphStepped = c.morphStepped; }
                Auto::paste (lane.points, c.points, clip.start, clip.length);
            }
    }
    // Clips that come from another track and play notes keep sounding: an audio clip needs the track's own sound
    if (clip.kind == ClipKind::Audio && to.patch.patchTrackSound())
        ctx.engine.rebuildTrack (track);
    ++ctx.autoGeneration;
    ctx.selectClip (track, (int) to.clips.size() - 1);
}

bool ArrangerView::deleteSelectedPoints()
{
    if (! lanesOn() || selection.empty()) return false;
    auto* lane = shownLane (selTrack);
    if (lane == nullptr || lane->target != selTarget) { clearPointSelection(); return false; }
    auto sel = selection;
    std::sort (sel.rbegin(), sel.rend());
    {
        const juce::SpinLock::ScopedLockType sl (ctx.project.lock);
        for (int i : sel)
            if (i >= 0 && i < (int) lane->points.size()) lane->points.erase (lane->points.begin() + i);
    }
    clearPointSelection();
    lanesEdited();
    return true;
}

void ArrangerView::showClipMenu (int track, int clip)
{
    juce::PopupMenu menu;
    menu.addItem (1, "Open in editor");
    menu.addItem (2, "Duplicate");
    menu.addItem (4, "Copy");
    menu.addItem (5, "Cut");
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
        else if (r == 4) copySelectedClip();
        else if (r == 5) cutSelectedClip();
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

void ArrangerView::showLaneMenu (int track, juce::Point<int> at)
{
    const auto target = ctx.shownTarget (track);
    auto* lane = shownLane (track);
    const bool has = lane != nullptr && ! lane->points.empty();
    juce::PopupMenu m;
    m.addItem (1, "Choose what this lane moves...");
    m.addSeparator();
    m.addItem (2, "Back to automation", lane != nullptr && lane->overridden);
    m.addItem (3, "Swap with the ghost take", lane != nullptr && ! lane->ghost.empty());
    m.addItem (4, "Tidy lane (fewer points, same shape)", has && lane->points.size() > 2);
    m.addItem (5, "Straighten every curve", has);
    m.addItem (6, "Clear lane", has);
    if (target.kind == AutoTarget::morph)
    {
        m.addSeparator();
        m.addSectionHeader ("Morph: the lane slides between snapshots");
        const int n = lane != nullptr ? (int) lane->snaps.size() : 0;
        m.addItem (10, "Store the device as snapshot A", true, n > 0);
        m.addItem (11, "Store the device as snapshot B", true, n > 1);
        m.addItem (12, "Store the device as snapshot C", true, n > 2);
        m.addItem (13, "Forget the snapshots", n > 0);
    }
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (juce::Rectangle<int> (1, 1).withPosition (localPointToGlobal (at))),
                     [this, track, at] (int r)
    {
        if (r == 0) return;
        if (r == 1)
        {
            if (ctx.openChooser) ctx.openChooser (track, juce::Rectangle<int> (1, 1).withPosition (localPointToGlobal (at)));
            return;
        }
        auto* lane = shownLane (track);
        if (r >= 10 && r <= 12)
        {
            // Snapshot every control the device has
            auto& l = makeShownLane (track);
            MorphSnap snap;
            std::vector<int> stepped;
            if (auto* m = ctx.project.slot (track).patch.find (l.target.module))
                for (auto& p : ctx.paramsOf (track))
                    if (p.target.kind == AutoTarget::knob && p.target.module == l.target.module && p.target.param < (int) m->knobValues.size())
                    {
                        snap.values.push_back ({ p.target.param, m->knobValues[(size_t) p.target.param] });
                        if (p.stepped) stepped.push_back (p.target.param);
                    }
            const juce::SpinLock::ScopedLockType sl (ctx.project.lock);
            const size_t idx = (size_t) (r - 10);
            while (l.snaps.size() <= idx) l.snaps.push_back (snap);
            l.snaps[idx] = snap;
            l.morphStepped = stepped;
            l.name = ctx.describe (track, l.target).group + "  morph";
            if (l.points.empty() && l.snaps.size() >= 2)
            {
                // Start with a gentle sweep across the loop so the morph is heard straight away
                Auto::add (l.points, ctx.engine.loopStart.load(), 0.0f);
                Auto::add (l.points, ctx.engine.loopEnd.load(), 1.0f);
            }
        }
        else if (lane != nullptr)
        {
            const juce::SpinLock::ScopedLockType sl (ctx.project.lock);
            if (r == 2) lane->overridden = false;
            else if (r == 3) std::swap (lane->points, lane->ghost);
            else if (r == 4) Auto::thin (lane->points, 0.0, 1.0e9, 0.006f);
            else if (r == 5) for (auto& p : lane->points) p.curve = 0.0f;
            else if (r == 6) { lane->ghost = lane->points; lane->points.clear(); }
            else if (r == 13) lane->snaps.clear();
        }
        clearPointSelection();
        lanesEdited();
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


void ArrangerView::drawLane (juce::Graphics& g, int row, float w)
{
    const int track = ctx.trackOfRow (row);
    const auto target = ctx.shownTarget (track);
    auto* lane = shownLane (track);
    const auto c = laneColour (track);
    const float bright = juce::jlimit (0.05f, 1.0f, ctx.autoBrightness);
    const float top = laneTop (row), bottom = laneBottom (row);
    const bool near = row == hoverRow || track == ctx.selectedTrack || (selTrack == track && ! selection.empty());
    const double ph = ctx.playheadBeats();

    // A lane with no points: a dashed line where the control stands
    if (lane == nullptr || lane->points.empty())
    {
        const auto info = ctx.describe (track, target);
        const float y = valueToY (row, ctx.currentValue (track, target, info));
        juce::Path flat;
        flat.startNewSubPath (0.0f, y);
        flat.lineTo (w, y);
        juce::Path dashed;
        const float dashes[] = { 4.0f, 6.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, flat, dashes, 2);
        g.setColour (c.withAlpha (0.35f * bright + 0.1f));
        g.fillPath (dashed);
        if (near)
        {
            g.setColour (juce::Colour (0xff6d7077));
            g.setFont (Theme::get().label (8.5f));
            g.drawText ("DOUBLE-CLICK TO ADD A POINT", juce::Rectangle<float> (8.0f, y - 14.0f, 240.0f, 12.0f).toNearestInt(), juce::Justification::centredLeft);
        }
        return;
    }

    const auto& pts = lane->points;
    const bool resting = lane->overridden;
    auto pathOf = [&] (const std::vector<AutoPoint>& p)
    {
        juce::Path path;
        path.startNewSubPath (juce::jmin (0.0f, beatToX (p.front().beat)), valueToY (row, p.front().value));
        for (size_t i = 0; i < p.size(); ++i)
        {
            const float x = beatToX (p[i].beat), y = valueToY (row, p[i].value);
            path.lineTo (x, y);
            if (i + 1 < p.size())
            {
                const auto& n = p[i + 1];
                const float x1 = beatToX (n.beat);
                if (std::abs (p[i].curve) > 1.0e-4f && std::abs (n.value - p[i].value) > 1.0e-4f && x1 > 0.0f && x < w)
                {
                    const int steps = juce::jlimit (4, 48, (int) ((x1 - x) / 4.0f));
                    for (int k = 1; k < steps; ++k)
                    {
                        const float u = (float) k / (float) steps;
                        path.lineTo (x + (x1 - x) * u, valueToY (row, p[i].value + (n.value - p[i].value) * Auto::shape (u, p[i].curve)));
                    }
                }
            }
        }
        path.lineTo (juce::jmax (w, beatToX (p.back().beat)), valueToY (row, p.back().value));
        return path;
    };

    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (juce::Rectangle<float> (0.0f, top - 6.0f, w, bottom - top + 12.0f).toNearestInt());

    // The ghost of the last take, faint and dashed
    if (! lane->ghost.empty() && near)
    {
        juce::Path dashed;
        const float dashes[] = { 3.0f, 5.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, pathOf (lane->ghost), dashes, 2);
        g.setColour (c.withAlpha (0.22f));
        g.fillPath (dashed);
    }

    const auto line = pathOf (pts);
    // Soft light under the tube
    auto fill = line;
    fill.lineTo (juce::jmax (w, beatToX (pts.back().beat)), bottom);
    fill.lineTo (juce::jmin (0.0f, beatToX (pts.front().beat)), bottom);
    fill.closeSubPath();
    g.setColour (c.withAlpha ((resting ? 0.03f : 0.075f) * bright));
    g.fillPath (fill);

    if (resting)
    {
        juce::Path dashed;
        const float dashes[] = { 5.0f, 4.0f };
        juce::PathStrokeType (1.2f).createDashedStroke (dashed, line, dashes, 2);
        g.setColour (juce::Colour (0xff8b8d94).withAlpha (0.7f));
        g.fillPath (dashed);
    }
    else
        Glow::strokeGlow (g, line, c.interpolatedWith (juce::Colours::white, 0.15f).withAlpha (0.35f + 0.65f * bright), 1.3f, bright);

    // Bulbs on the points, the selected ones ringed
    for (int i = 0; i < (int) pts.size(); ++i)
    {
        const float x = beatToX (pts[(size_t) i].beat);
        if (x < -10.0f || x > w + 10.0f) continue;
        const float y = valueToY (row, pts[(size_t) i].value);
        const bool sel = selTrack == track && std::find (selection.begin(), selection.end(), i) != selection.end();
        const auto bc = resting ? juce::Colour (0xff8b8d94) : c;
        g.setColour (bc.withAlpha (0.22f * bright));
        g.fillEllipse (x - 6.0f, y - 6.0f, 12.0f, 12.0f);
        g.setColour (bc.interpolatedWith (juce::Colours::white, 0.45f).withAlpha (0.5f + 0.5f * bright));
        g.fillEllipse (x - pointRadius, y - pointRadius, pointRadius * 2.0f, pointRadius * 2.0f);
        if (sel)
        {
            g.setColour (juce::Colours::white.withAlpha (0.9f));
            g.drawEllipse (x - 5.5f, y - 5.5f, 11.0f, 11.0f, 1.2f);
        }
    }

    // The rings between points that bend each stretch
    if (near && ! resting)
        for (int i = 0; i + 1 < (int) pts.size(); ++i)
        {
            if (std::abs (pts[(size_t) i].value - pts[(size_t) i + 1].value) < 0.01f || beatToX (pts[(size_t) i + 1].beat) - beatToX (pts[(size_t) i].beat) < 18.0f) continue;
            const auto h = curveHandle (row, pts, i);
            if (h.x < -6.0f || h.x > w + 6.0f) continue;
            g.setColour (Theme::get().bg.withAlpha (0.9f));
            g.fillEllipse (h.x - 3.2f, h.y - 3.2f, 6.4f, 6.4f);
            g.setColour (c.withAlpha (0.55f + 0.4f * bright));
            g.drawEllipse (h.x - 3.2f, h.y - 3.2f, 6.4f, 6.4f, 1.0f);
        }

    // A lamp riding the lane at the playhead
    if (! resting)
    {
        const float x = beatToX (ph);
        if (x >= 0.0f && x <= w)
        {
            const float y = valueToY (row, Auto::valueAt (pts, ph));
            g.setColour (c.withAlpha (0.3f * bright));
            g.fillEllipse (x - 7.0f, y - 7.0f, 14.0f, 14.0f);
            g.setColour (c.interpolatedWith (juce::Colours::white, 0.6f));
            g.fillEllipse (x - 3.0f, y - 3.0f, 6.0f, 6.0f);
        }
    }

    // The box around selected points, with its handles
    if (const auto box = selectionBox (row); ! box.isEmpty())
    {
        g.setColour (c.withAlpha (0.08f));
        g.fillRoundedRectangle (box, 3.0f);
        g.setColour (c.withAlpha (0.7f));
        g.drawRoundedRectangle (box, 3.0f, 1.0f);
        g.setColour (c.interpolatedWith (juce::Colours::white, 0.4f));
        g.fillRect (box.getCentreX() - 8.0f, box.getY() - 1.5f, 16.0f, 3.0f);                 // scale
        for (auto x : { box.getX(), box.getRight() })
        {
            g.fillEllipse (x - 3.0f, box.getY() - 3.0f, 6.0f, 6.0f);                          // tilt
            g.fillRect (x - 1.5f, box.getCentreY() - 6.0f, 3.0f, 12.0f);                       // squeeze
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
    const int rh = rowH();
    const bool lanes = lanesOn();

    g.fillAll (th.bg);

    // Ruler
    auto ruler = juce::Rectangle<float> (0.0f, 0.0f, w, (float) Layout::rulerHeight);
    g.setColour (th.panel);
    g.fillRect (ruler);

    // Loop region on the lower strip
    {
        const bool on = ctx.engine.looping.load();
        const float x1 = beatToX (ctx.engine.loopStart.load()), x2 = beatToX (ctx.engine.loopEnd.load());
        auto loopBand = juce::Rectangle<float> (x1, ruler.getBottom() - 9.0f, x2 - x1, 7.0f);
        g.setColour (loopCol.withAlpha (on ? 0.22f : 0.08f));
        g.fillRoundedRectangle (loopBand, 3.0f);
        g.setColour (loopCol.withAlpha (on ? 0.9f : 0.3f));
        g.drawRoundedRectangle (loopBand, 3.0f, 1.0f);
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
    const int numRows = ctx.numRows();
    for (int i = 0; i < numRows; ++i)
    {
        const float y = (float) rowTop (i);
        if (ctx.trackOfRow (i) == ctx.selectedTrack && i < numTracks)
        {
            g.setColour (th.pal (ctx.selected().colourIndex).withAlpha (0.05f));
            g.fillRect (0.0f, y, w, (float) rh);
        }
        if (i >= numTracks)
        {
            // The visualiser's own track has no clips, only its lane
            g.setColour (th.themeColour().withAlpha (0.035f));
            g.fillRect (0.0f, y, w, (float) rh);
        }
        g.setColour (th.line2);
        g.drawHorizontalLine ((int) y + rh - 1, 0.0f, w);
    }

    // Clips: translucent fill, thin outline, name, and their notes or waveform. Under the lanes they dim.
    const float under = lanes ? 0.55f : 1.0f;
    for (int i = 0; i < numTracks; ++i)
    {
        auto& t = *ctx.project.tracks[(size_t) i];
        const auto c = th.pal (t.colourIndex);
        const bool rolling = t.rolling.load();
        const float alpha = (ctx.project.isAudible (i) ? 1.0f : 0.4f) * (rolling ? 0.3f : 1.0f);

        if (rolling)
        {
            // The rack plays live: a banner across the lane with the running step lights
            const float y = (float) rowTop (i);
            auto rb = juce::Rectangle<float> (0.0f, y + 4.0f, w, (float) rh - 9.0f);
            g.setColour (c.withAlpha (0.06f));
            g.fillRect (rb);
            g.setColour (c.withAlpha (0.35f));
            g.drawHorizontalLine ((int) rb.getY(), 0.0f, w);
            g.drawHorizontalLine ((int) rb.getBottom(), 0.0f, w);
            int pos = -1, length = seqSteps;
            for (auto& m : t.seq.modules)
                if (m.type == SeqType::Steps)
                    if (auto* n = ctx.engine.liveSeqNode (i, m.id)) { pos = (int) n->display[0].load(); length = SeqKnob::count (n->knob[4].load(), 1, seqSteps); break; }
            g.setColour (th.text.withAlpha (0.85f));
            Glow::drawSpacedText (g, juce::String::fromUTF8 ("ROLLING  \xc2\xb7  ") + t.name.toUpperCase() + juce::String::fromUTF8 ("  \xc2\xb7  ")
                                     + juce::String (length) + juce::String::fromUTF8 (" STEP  \xc2\xb7  \xe2\x88\x9e"),
                                  rb.withTrimmedLeft (12.0f), th.label (8.5f), 1.6f, juce::Justification::centredLeft);
            for (int s = 0; s < length; ++s)
            {
                const float x = rb.getRight() - 24.0f - (float) (length - 1 - s) * 9.0f;
                const bool lit = s == pos && ctx.engine.isPlaying();
                g.setColour (lit ? c : c.withAlpha (0.3f));
                g.fillEllipse (x - (lit ? 3.0f : 2.0f), rb.getCentreY() - (lit ? 3.0f : 2.0f), lit ? 6.0f : 4.0f, lit ? 6.0f : 4.0f);
            }
            g.setColour (th.muted.withAlpha (0.7f));
            g.setFont (th.label (7.5f));
            g.drawText ("LIVE FROM RACK", rb.withTrimmedRight (12.0f + 9.0f * (float) length + 20.0f).toNearestInt(), juce::Justification::centredRight);
        }

        for (size_t ci = 0; ci < t.clips.size(); ++ci)
        {
            const auto& clip = t.clips[ci];
            auto r = clipBounds (i, clip);
            if (r.getRight() < 0.0f || r.getX() > w)
                continue;
            const bool selected = i == ctx.selectedTrack && (int) ci == ctx.selectedClip;

            g.setColour (c.withAlpha ((selected ? 0.26f : 0.16f) * alpha * (lanes ? 0.8f : 1.0f)));
            g.fillRoundedRectangle (r, 5.0f);
            g.setColour (c.withAlpha ((selected ? 1.0f : 0.7f) * alpha * (lanes ? 0.8f : 1.0f)));
            g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, selected ? 1.6f : 1.0f);
            if (lanes)
            {
                // The name strip is the handle while lanes show
                g.setColour (c.withAlpha (0.14f * alpha));
                g.fillRoundedRectangle (r.withHeight (14.0f), 5.0f);
            }

            g.setColour (th.text.withAlpha (0.9f * alpha));
            g.setFont (th.label (9.0f));
            g.drawText (clip.name.toUpperCase(), r.reduced (7.0f, 2.0f).removeFromTop (12.0f).toNearestInt(),
                        juce::Justification::centredLeft, true);

            drawClipContent (g, clip, r, c, alpha * under, t.kind == TrackKind::Automation && ! lanes);
        }
    }

    // Automation lanes over the clips
    if (lanes)
        for (int i = 0; i < numRows; ++i)
            drawLane (g, i, w);
    if (drag == DragMode::Band && ! band.isEmpty())
    {
        g.setColour (th.themeColour().withAlpha (0.08f));
        g.fillRect (band);
        g.setColour (th.themeColour().withAlpha (0.6f));
        g.drawRect (band, 1.0f);
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
