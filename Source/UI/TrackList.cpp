#include "TrackList.h"

namespace
{
    juce::String kindName (TrackKind k)
    {
        switch (k)
        {
            case TrackKind::Audio:      return "AUDIO";
            case TrackKind::Sampler:    return "SAMPLER";
            case TrackKind::Midi:       return "INSTRUMENT";
            case TrackKind::Automation: return "AUTOMATION";
            case TrackKind::Return:     return "RETURN";
            case TrackKind::Master:     return "MASTER";
        }
        return {};
    }
}

TrackList::TrackList (AppContext& c) : ctx (c)
{
    rebuild();
}

void TrackList::rebuild()
{
    builtGeneration = ctx.generation;
    muteButtons.clear();
    soloButtons.clear();
    seqButtons.clear();
    for (size_t i = 0; i < ctx.project.tracks.size(); ++i)
    {
        auto* track = ctx.project.tracks[i].get();

        auto* m = muteButtons.add (new SmallToggle ("M", Pal::Amber));
        m->onClick = [m, track] { track->muted = m->getToggleState(); };
        addAndMakeVisible (m);

        auto* s = soloButtons.add (new SmallToggle ("S", Pal::Gold));
        s->onClick = [s, track] { track->soloed = s->getToggleState(); };
        addAndMakeVisible (s);

        // Instrument tracks can swap their clips for the rolling sequencer rack
        auto* q = seqButtons.add (new SmallToggle ("SEQ", Pal::Sky));
        q->setTooltip ("Clips or rolling sequencer");
        q->onClick = [this, q, track, i]
        {
            track->rolling = q->getToggleState();
            ctx.selectTrack ((int) i);
            ctx.clipsChanged();
        };
        addChildComponent (q);
        q->setVisible (track->kind == TrackKind::Midi || track->kind == TrackKind::Sampler);
    }
    resized();
}

juce::Rectangle<int> TrackList::rowBounds (int index) const
{
    const int h = ctx.rowHeight();
    return { 0, Layout::rulerHeight + index * h, getWidth(), h };
}

juce::Rectangle<float> TrackList::chooserBox (int row) const
{
    auto r = rowBounds (row).toFloat();
    return { r.getX() + 18.0f, r.getBottom() - 22.0f, r.getWidth() - 18.0f - 60.0f, 18.0f };
}

juce::Rectangle<float> TrackList::restBox (int row) const
{
    auto r = rowBounds (row).toFloat();
    return { r.getRight() - 58.0f, r.getBottom() - 22.0f, 40.0f, 18.0f };
}

void TrackList::refreshShown()
{
    const int rows = ctx.numRows();
    const int gen = ctx.autoGeneration * 7919 + ctx.generation * 31 + rows + (ctx.lanesShown ? 100000 : 0);
    if (gen != shownGen || (int) shown.size() != rows)
    {
        shownGen = gen;
        shown.assign ((size_t) rows, {});
        if (ctx.lanesShown)
            for (int row = 0; row < rows; ++row)
            {
                const int track = ctx.trackOfRow (row);
                auto& s = shown[(size_t) row];
                s.target = ctx.shownTarget (track);
                s.info = ctx.describe (track, s.target);
            }
    }
    if (! ctx.lanesShown) return;
    for (int row = 0; row < rows; ++row)
    {
        const int track = ctx.trackOfRow (row);
        auto& s = shown[(size_t) row];
        const AutoLane* lane = nullptr;
        if (auto* lanes = ctx.lanesOf (track)) lane = Auto::find (*lanes, s.target);
        s.automated = lane != nullptr && ! lane->points.empty();
        s.resting = lane != nullptr && lane->overridden;
        s.value = s.target.kind == AutoTarget::morph ? (lane != nullptr ? juce::jmax (0.0f, Auto::valueAt (lane->points, ctx.playheadBeats())) : 0.0f)
                                                     : ctx.currentValue (track, s.target, s.info);
    }
}

void TrackList::resized()
{
    for (int i = 0; i < muteButtons.size(); ++i)
    {
        auto r = rowBounds (i).reduced (8, 0).withSizeKeepingCentre (getWidth() - 16, 20);
        if (ctx.lanesShown) r.setY (rowBounds (i).getY() + 6);     // the chooser takes the lower line
        r.removeFromRight (14); // meter
        soloButtons[i]->setBounds (r.removeFromRight (22));
        r.removeFromRight (4);
        muteButtons[i]->setBounds (r.removeFromRight (22));
        r.removeFromRight (6);
        seqButtons[i]->setBounds (r.removeFromRight (30));
    }
}

void TrackList::tick()
{
    if (builtGeneration != ctx.generation || muteButtons.size() != (int) ctx.project.tracks.size())
        rebuild();
    // Rows change height when lanes show or rows come and go
    if (const int layoutKey = ctx.rowHeight() * 4 + (ctx.lanesShown ? 1 : 0) + ctx.numRows() * 1000; layoutKey != shownLayout)
    {
        shownLayout = layoutKey;
        resized();
        repaint();
    }
    refreshShown();
    // Keep buttons in sync if mute / solo changed elsewhere (e.g. the mixer)
    for (int i = 0; i < muteButtons.size(); ++i)
    {
        auto& t = *ctx.project.tracks[(size_t) i];
        muteButtons[i]->setToggleState (t.muted.load(), juce::dontSendNotification);
        soloButtons[i]->setToggleState (t.soloed.load(), juce::dontSendNotification);
        seqButtons[i]->setToggleState (t.rolling.load(), juce::dontSendNotification);
    }
    if (const auto s = stateNow(); s != shownState)
    {
        shownState = s;
        repaint();
    }
}

// Everything the rows show that can change without telling us: lit meter bulbs, audible, rack, names
juce::int64 TrackList::stateNow() const
{
    juce::int64 h = (juce::int64) ctx.selectedTrack * 1000003 + (juce::int64) ctx.project.tracks.size()
                  + (juce::int64) ctx.autoGeneration * 7 + (ctx.lanesShown ? 13 : 0) + (ctx.project.visTrack ? 17 : 0);
    for (auto& s : shown)
        h = h * 31 + (juce::int64) std::lround (s.value * 200.0f) + (s.resting ? 1000 : 0) + (s.automated ? 2000 : 0);
    for (size_t i = 0; i < ctx.project.tracks.size(); ++i)
    {
        auto& t = *ctx.project.tracks[i];
        const auto r = rowBounds ((int) i).toFloat();
        const int n = juce::jmax (3, (int) ((r.getHeight() - 16.0f) / 7.0f));
        const float lv = juce::jlimit (0.0f, 1.0f, t.meter.load());
        int lit = 0;
        while (lit < n && (float) lit / (float) n < lv) ++lit;
        const juce::int64 row = lit + 64 * (ctx.project.isAudible ((int) i) ? 1 : 0) + 128 * (t.rolling.load() ? 1 : 0)
                              + 256 * (juce::int64) t.colourIndex + 65536 * (juce::int64) t.name.hashCode();
        h = h * 31 + row;
    }
    return h;
}

void TrackList::mouseDown (const juce::MouseEvent& e)
{
    if (e.y < Layout::rulerHeight) return;
    const int row = (e.y - Layout::rulerHeight) / ctx.rowHeight();
    if (row >= ctx.numRows()) return;
    const int track = ctx.trackOfRow (row);
    if (ctx.lanesShown && row < (int) shown.size())
    {
        if (shown[(size_t) row].resting && restBox (row).expanded (4.0f).contains (e.position))
        {
            if (auto* lanes = ctx.lanesOf (track))
                if (auto* lane = Auto::find (*lanes, shown[(size_t) row].target))
                {
                    const juce::SpinLock::ScopedLockType sl (ctx.project.lock);
                    lane->overridden = false;
                }
            ctx.automationChanged();
            return;
        }
        if (chooserBox (row).expanded (2.0f).contains (e.position))
        {
            if (ctx.isTrack (track)) ctx.selectTrack (track);
            if (ctx.openChooser) ctx.openChooser (track, localAreaToGlobal (chooserBox (row).toNearestInt()));
            return;
        }
    }
    if (ctx.isTrack (track))
        ctx.selectTrack (track);
}

void TrackList::paint (juce::Graphics& g)
{
    auto& th = Theme::get();
    g.fillAll (th.panel);
    g.setColour (th.line2);
    g.drawHorizontalLine (Layout::rulerHeight - 1, 0.0f, (float) getWidth());
    for (int i = 0; i < ctx.numRows(); ++i)
    {
        auto r = rowBounds (i).toFloat();
        if (i == ctx.selectedTrack && ctx.isTrack (i))
        {
            g.setColour (th.pal (ctx.project.tracks[(size_t) i]->colourIndex).withAlpha (0.06f));
            g.fillRect (r);
        }
        if (ctx.trackOfRow (i) == AppContext::visSlot)
        {
            g.setColour (th.themeColour().withAlpha (0.04f));
            g.fillRect (r);
        }
        g.setColour (th.line2.withAlpha (0.6f));
        g.drawHorizontalLine ((int) r.getBottom() - 1, 0.0f, (float) getWidth());
    }

    Neon::paintLit (g, getWidth(), getHeight(), glowLayer, 0.6f, [&] (juce::Graphics& gg, bool)
    {
        Neon::label (gg, "tracks", 14.0f, (float) Layout::rulerHeight * 0.5f + 5.0f, Neon::Align::left, juce::Colour (0xff6d7077), 13.0f);
        for (size_t i = 0; i < ctx.project.tracks.size(); ++i)
        {
            auto& t = *ctx.project.tracks[i];
            auto r = rowBounds ((int) i).toFloat();
            const auto c = th.pal (t.colourIndex);
            const bool selected = (int) i == ctx.selectedTrack;
            const bool audible = ctx.project.isAudible ((int) i);

            // A short tube in the track's colour, lit while the track can be heard
            Neon::TubeOptions o;
            o.w = 0.55f;
            o.off = ! audible;
            Neon::tube (gg, Neon::Line { { r.getX() + 8.0f, r.getY() + 9.0f }, { r.getX() + 8.0f, r.getBottom() - 9.0f } }, c, o);

            const float mid = ctx.lanesShown ? r.getY() + 16.0f : r.getCentreY();
            auto name = t.name.toLowerCase();
            const float room = r.getWidth() - 20.0f - 112.0f;
            float size = 16.0f;
            while (size > 11.0f && Neon::textWidth (name, size) > room) size -= 1.0f;
            Neon::text (gg, name, r.getX() + 20.0f, mid + 1.0f, size, c, ! audible ? 0.0f : (selected ? 1.0f : 0.5f));
            if (! ctx.lanesShown)
                Neon::label (gg, t.rolling.load() ? juce::String ("rolling seq") : kindName (t.kind).toLowerCase(), r.getX() + 21.0f, mid + 14.0f,
                             Neon::Align::left, juce::Colour (0xff6d7077), 11.0f);

            // Meter: a column of bulbs
            const float lv = juce::jlimit (0.0f, 1.0f, t.meter.load());
            const int n = juce::jmax (3, (int) ((r.getHeight() - 16.0f) / 7.0f));
            for (int k = 0; k < n; ++k)
            {
                const float y = r.getBottom() - 9.0f - (float) k * 7.0f;
                const auto col = k >= n - 1 ? juce::Colour (0xffff4d5e) : (k >= n - 2 ? juce::Colour (0xffffd23a) : c);
                Neon::bulb (gg, r.getRight() - 11.0f, y, col, (float) k / (float) n < lv ? 1.0f : 0.0f, 2.2f);
            }
        }

        // The visualiser's own track
        if (ctx.project.visTrack)
        {
            const int row = (int) ctx.project.tracks.size();
            auto r = rowBounds (row).toFloat();
            const auto c = th.themeColour();
            Neon::TubeOptions o;
            o.w = 0.55f;
            Neon::tube (gg, Neon::Line { { r.getX() + 8.0f, r.getY() + 9.0f }, { r.getX() + 8.0f, r.getBottom() - 9.0f } }, c, o);
            const float mid = ctx.lanesShown ? r.getY() + 16.0f : r.getCentreY();
            Neon::text (gg, "visualiser", r.getX() + 20.0f, mid + 1.0f, 15.0f, c, 0.6f);
            if (! ctx.lanesShown)
                Neon::label (gg, "automation", r.getX() + 21.0f, mid + 14.0f, Neon::Align::left, juce::Colour (0xff6d7077), 11.0f);
        }

        // Each lane's chooser: "device > control", its value, and the resting lamp
        if (ctx.lanesShown)
            for (int row = 0; row < (int) shown.size() && row < ctx.numRows(); ++row)
            {
                auto& s = shown[(size_t) row];
                const int track = ctx.trackOfRow (row);
                const auto c = track == AppContext::visSlot ? th.themeColour() : th.pal (ctx.project.tracks[(size_t) track]->colourIndex);
                auto box = chooserBox (row);
                const float base = box.getBottom() - 5.0f;
                const auto grey = juce::Colour (0xff8a8d94);
                auto group = s.info.group.isEmpty() ? juce::String ("choose") : s.info.group;
                auto words = group + "  >  " + (s.info.name.isEmpty() ? juce::String ("...") : s.info.name);
                float size = 11.5f;
                const float valueRoom = 40.0f;
                while (size > 9.0f && Neon::textWidth (words, size) > box.getWidth() - valueRoom) size -= 0.5f;
                Neon::label (gg, words, box.getX() + 3.0f, base, Neon::Align::left, s.automated ? c.brighter (0.2f) : grey, size);
                // A little chevron says it opens
                const float cx = box.getX() + 6.0f + Neon::textWidth (words, size);
                gg.setColour (grey);
                juce::Path chev;
                chev.startNewSubPath (cx, base - 6.0f);
                chev.lineTo (cx + 3.0f, base - 3.0f);
                chev.lineTo (cx + 6.0f, base - 6.0f);
                gg.strokePath (chev, juce::PathStrokeType (1.1f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

                // The value as it stands
                juce::String v;
                if (s.target.kind == AutoTarget::morph) v = juce::String (juce::roundToInt (s.value * 100.0f)) + "%";
                else
                {
                    const float raw = s.info.lo + s.value * (s.info.hi - s.info.lo);
                    v = s.info.stepped ? juce::String (juce::roundToInt (raw)) : juce::String (raw, std::abs (s.info.hi - s.info.lo) >= 10.0f ? 1 : 2);
                }
                Neon::label (gg, v, box.getRight() - 2.0f, base, Neon::Align::right, c.withAlpha (0.85f), 11.0f);

                if (s.resting)
                {
                    auto rb = restBox (row);
                    Neon::bulb (gg, rb.getX() + 6.0f, rb.getCentreY(), juce::Colour (0xffffb03a), 1.0f, 3.2f);
                    Neon::label (gg, "rest", rb.getX() + 13.0f, base, Neon::Align::left, juce::Colour (0xffffb03a), 11.0f);
                }
            }
    });
}
