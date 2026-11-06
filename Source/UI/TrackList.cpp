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
        }
        return {};
    }
}

TrackList::TrackList (AppContext& c) : ctx (c)
{
    for (size_t i = 0; i < ctx.project.tracks.size(); ++i)
    {
        auto* track = ctx.project.tracks[i].get();

        auto* m = muteButtons.add (new SmallToggle ("M", Pal::Amber));
        m->onClick = [m, track] { track->muted = m->getToggleState(); };
        addAndMakeVisible (m);

        auto* s = soloButtons.add (new SmallToggle ("S", Pal::Gold));
        s->onClick = [s, track] { track->soloed = s->getToggleState(); };
        addAndMakeVisible (s);
    }
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
    }
}

void TrackList::tick()
{
    // Keep buttons in sync if mute / solo changed elsewhere (e.g. the mixer)
    for (int i = 0; i < muteButtons.size(); ++i)
    {
        auto& t = *ctx.project.tracks[(size_t) i];
        muteButtons[i]->setToggleState (t.muted.load(), juce::dontSendNotification);
        soloButtons[i]->setToggleState (t.soloed.load(), juce::dontSendNotification);
    }
    repaint();
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

    auto head = juce::Rectangle<float> (12.0f, 0.0f, (float) getWidth() - 24.0f, (float) Layout::rulerHeight);
    g.setColour (th.muted);
    Glow::drawSpacedText (g, "TRACKS", head, th.label (9.0f), 2.0f, juce::Justification::centredLeft);
    g.setColour (th.line2);
    g.drawHorizontalLine (Layout::rulerHeight - 1, 0.0f, (float) getWidth());

    for (size_t i = 0; i < ctx.project.tracks.size(); ++i)
    {
        auto& t = *ctx.project.tracks[i];
        auto r = rowBounds ((int) i).toFloat();
        const auto c = th.pal (t.colourIndex);
        const bool selected = (int) i == ctx.selectedTrack;
        const bool audible = ctx.project.isAudible ((int) i);

        if (selected)
        {
            g.setColour (c.withAlpha (0.08f));
            g.fillRect (r);
        }
        g.setColour (th.line2);
        g.drawHorizontalLine ((int) r.getBottom() - 1, 0.0f, (float) getWidth());

        // Colour tab
        g.setColour (c.withAlpha (audible ? 1.0f : 0.35f));
        g.fillRoundedRectangle (r.getX() + 6.0f, r.getY() + 9.0f, 3.0f, r.getHeight() - 18.0f, 1.5f);

        auto text = r.withTrimmedLeft (18.0f).withTrimmedRight (72.0f).reduced (0.0f, 7.0f);
        g.setColour (audible ? th.text : th.muted);
        g.setFont (th.body (13.0f));
        g.drawText (t.name, text.removeFromTop (text.getHeight() * 0.55f).toNearestInt(), juce::Justification::centredLeft);
        g.setColour (th.muted.withAlpha (0.8f));
        g.setFont (th.label (8.0f));
        g.drawText (kindName (t.kind), text.toNearestInt(), juce::Justification::centredLeft);

        // Meter
        drawMeter (g, juce::Rectangle<float> (r.getRight() - 14.0f, r.getY() + 10.0f, 5.0f, r.getHeight() - 20.0f),
                   t.meter.load(), c);
    }
}
