#include "MixerView.h"

namespace
{
    constexpr int minStripWidth = 70;
    constexpr int maxStripWidth = 96;
    constexpr int stripGap = 6;
}

ChannelStrip::ChannelStrip (AppContext& c, Track* t, const juce::String& n, int colour, bool master)
    : ctx (c), track (t), name (n), colourIndex (colour), isMaster (master)
{
    pan.setRange (-1.0, 1.0);
    pan.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    pan.setDoubleClickReturnValue (true, 0.0);
    pan.getProperties().set ("pal", colour);
    pan.setTooltip ("Pan");

    fader.setRange (0.0, 1.25);
    fader.setSkewFactor (0.6);
    fader.setDoubleClickReturnValue (true, 0.8);
    fader.getProperties().set ("pal", colour);
    fader.setTooltip ("Volume");

    if (track != nullptr)
    {
        pan.setValue (track->pan.load(), juce::dontSendNotification);
        fader.setValue (track->volume.load(), juce::dontSendNotification);
        pan.onValueChange   = [this] { track->pan = (float) pan.getValue(); };
        fader.onValueChange = [this] { track->volume = (float) fader.getValue(); };
        mute.onClick = [this] { track->muted = mute.getToggleState(); };
        solo.onClick = [this] { track->soloed = solo.getToggleState(); };
    }
    else
    {
        fader.setValue (0.8, juce::dontSendNotification);
    }

    addAndMakeVisible (pan);
    addAndMakeVisible (fader);
    if (! isMaster)
    {
        addAndMakeVisible (mute);
        addAndMakeVisible (solo);
    }
}

float ChannelStrip::level() const
{
    if (track != nullptr)
        return track->meter.load() * (float) fader.getValue();

    float peak = 0.0f;
    for (auto& t : ctx.project.tracks)
        peak = juce::jmax (peak, t->meter.load() * t->volume.load());
    return peak * (float) fader.getValue();
}

void ChannelStrip::tick()
{
    if (track != nullptr)
    {
        mute.setToggleState (track->muted.load(), juce::dontSendNotification);
        solo.setToggleState (track->soloed.load(), juce::dontSendNotification);
    }
    const float l = level();
    if (std::abs (l - shownLevel) > 0.005f)
    {
        shownLevel = l;
        repaint (meterArea.expanded (2));
    }
}

void ChannelStrip::mouseDown (const juce::MouseEvent&)
{
    if (trackIndex >= 0)
        ctx.selectTrack (trackIndex);
}

void ChannelStrip::resized()
{
    auto r = getLocalBounds().reduced (8, 10);
    r.removeFromTop (8);                          // colour cap
    insertArea = r.removeFromTop (54);
    r.removeFromTop (6);
    pan.setBounds (r.removeFromTop (44).withSizeKeepingCentre (40, 40));
    r.removeFromTop (6);
    r.removeFromBottom (24);                      // name
    auto buttons = r.removeFromBottom (22);
    if (! isMaster)
    {
        mute.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2).reduced (2, 0));
        solo.setBounds (buttons.reduced (2, 0));
    }
    r.removeFromBottom (8);
    meterArea = r.removeFromRight (10).reduced (0, 9);
    fader.setBounds (r.withTrimmedRight (2));
}

void ChannelStrip::paint (juce::Graphics& g)
{
    auto& th = Theme::get();
    const auto c = th.pal (colourIndex);
    auto r = getLocalBounds().toFloat();
    const bool selected = trackIndex >= 0 && trackIndex == ctx.selectedTrack;

    g.setColour (selected ? th.panel2.brighter (0.03f) : th.panel2);
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (selected ? c.withAlpha (0.7f) : th.line2);
    g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);

    // Colour cap
    g.setColour (c);
    g.fillRoundedRectangle (r.getX() + 14.0f, r.getY() + 8.0f, r.getWidth() - 28.0f, 3.0f, 1.5f);

    // Insert slots
    const char* inserts[] = { "EQ", isMaster ? "LIMIT" : "COMP", "" };
    auto ins = insertArea.toFloat();
    for (int i = 0; i < 3; ++i)
    {
        auto slot = ins.removeFromTop (16.0f);
        ins.removeFromTop (2.0f);
        g.setColour (th.display);
        g.fillRoundedRectangle (slot, 3.0f);
        g.setColour (inserts[i][0] != 0 ? c.withAlpha (0.45f) : th.line2);
        g.drawRoundedRectangle (slot.reduced (0.5f), 3.0f, 1.0f);
        if (inserts[i][0] != 0)
        {
            g.setColour (th.text.withAlpha (0.8f));
            g.setFont (th.label (8.0f));
            g.drawText (inserts[i], slot.toNearestInt(), juce::Justification::centred);
        }
    }

    drawMeter (g, meterArea.toFloat(), shownLevel, c);

    // Name plate
    auto plate = r.removeFromBottom (32.0f).reduced (6.0f, 6.0f);
    g.setColour (th.display);
    g.fillRoundedRectangle (plate, 5.0f);
    g.setColour (isMaster ? c : th.text);
    g.setFont (isMaster ? th.heading (12.0f) : th.body (11.5f));
    g.drawFittedText (isMaster ? name.toUpperCase() : name, plate.toNearestInt().reduced (3, 0), juce::Justification::centred, 1);
}

MixerView::MixerView (AppContext& c) : ctx (c)
{
    for (size_t i = 0; i < ctx.project.tracks.size(); ++i)
    {
        auto& t = *ctx.project.tracks[i];
        auto* s = strips.add (new ChannelStrip (ctx, &t, t.name, t.colourIndex, false));
        s->trackIndex = (int) i;
    }
    strips.add (new ChannelStrip (ctx, nullptr, "Drum Bus", (int) Pal::Amber, false));
    strips.add (new ChannelStrip (ctx, nullptr, "FX Bus", (int) Pal::Sky, false));
    strips.add (new ChannelStrip (ctx, nullptr, "Master", (int) Pal::Green, true));

    for (auto* s : strips)
        content.addAndMakeVisible (s);

    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (false, true);
    addAndMakeVisible (viewport);
}

void MixerView::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (34);
    viewport.setBounds (r);

    const int h = juce::jmax (300, r.getHeight() - 14);
    const int numTracks = (int) ctx.project.tracks.size();
    // Fit every strip on screen when there is room, otherwise scroll sideways
    const int fitted = (r.getWidth() - 24 - 18 - stripGap * (strips.size() - 1)) / juce::jmax (1, strips.size());
    const int stripWidth = juce::jlimit (minStripWidth, maxStripWidth, fitted);
    int x = 12;
    for (int i = 0; i < strips.size(); ++i)
    {
        if (i == numTracks)
            x += 18;              // gap before buses and master
        strips[i]->setBounds (x, 0, stripWidth, h);
        x += stripWidth + stripGap;
    }
    content.setSize (juce::jmax (x + 6, r.getWidth()), h);
}

void MixerView::tick()
{
    for (auto* s : strips)
        s->tick();
}

void MixerView::paint (juce::Graphics& g)
{
    auto& th = Theme::get();
    g.fillAll (th.bg);
    auto head = getLocalBounds().reduced (14, 0).removeFromTop (34).toFloat();
    g.setColour (th.text);
    Glow::drawSpacedText (g, "MIXER", head, th.heading (14.0f), 2.5f, juce::Justification::centredLeft);
    g.setColour (th.muted);
    Glow::drawSpacedText (g, juce::String ((int) ctx.project.tracks.size()) + " TRACKS  /  2 BUSES  /  MASTER",
                          head, th.label (8.5f), 1.5f, juce::Justification::centredRight);
}
