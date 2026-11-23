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
    return { 0, Layout::rulerHeight + index * Layout::rowHeight, getWidth(), Layout::rowHeight };
}

void TrackList::resized()
{
    for (int i = 0; i < muteButtons.size(); ++i)
    {
        auto r = rowBounds (i).reduced (8, 0).withSizeKeepingCentre (getWidth() - 16, 20);
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
    juce::int64 h = (juce::int64) ctx.selectedTrack * 1000003 + (juce::int64) ctx.project.tracks.size();
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
    const int i = (e.y - Layout::rulerHeight) / Layout::rowHeight;
    if (e.y >= Layout::rulerHeight && i < (int) ctx.project.tracks.size())
        ctx.selectTrack (i);
}

void TrackList::paint (juce::Graphics& g)
{
    auto& th = Theme::get();
    g.fillAll (th.panel);
    g.setColour (th.line2);
    g.drawHorizontalLine (Layout::rulerHeight - 1, 0.0f, (float) getWidth());
    for (size_t i = 0; i < ctx.project.tracks.size(); ++i)
    {
        auto r = rowBounds ((int) i).toFloat();
        if ((int) i == ctx.selectedTrack)
        {
            g.setColour (th.pal (ctx.project.tracks[i]->colourIndex).withAlpha (0.06f));
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

            const float mid = r.getCentreY();
            auto name = t.name.toLowerCase();
            const float room = r.getWidth() - 20.0f - 112.0f;
            float size = 16.0f;
            while (size > 11.0f && Neon::textWidth (name, size) > room) size -= 1.0f;
            Neon::text (gg, name, r.getX() + 20.0f, mid + 1.0f, size, c, ! audible ? 0.0f : (selected ? 1.0f : 0.5f));
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
    });
}
