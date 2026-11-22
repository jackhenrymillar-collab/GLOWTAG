#include "MixerView.h"

namespace
{
    constexpr int minStripWidth = 74;
    constexpr int maxStripWidth = 100;
    constexpr int stripGap = 6;
    constexpr int numInsertSlots = 8;   // insert boxes shown per strip
}

ChannelStrip::ChannelStrip (AppContext& c, int s)
    : slot (s), ctx (c), track (c.project.slot (s)),
      isMaster (track.kind == TrackKind::Master), isReturn (track.kind == TrackKind::Return)
{
    const int colour = track.colourIndex;
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

    pan.setValue (track.pan.load(), juce::dontSendNotification);
    fader.setValue (track.volume.load(), juce::dontSendNotification);
    pan.onValueChange   = [this] { track.pan = (float) pan.getValue(); ctx.touched(); };
    fader.onValueChange = [this] { track.volume = (float) fader.getValue(); ctx.touched(); };
    mute.onClick = [this] { track.muted = mute.getToggleState(); ctx.touched(); };
    solo.onClick = [this] { track.soloed = solo.getToggleState(); ctx.touched(); };

    addAndMakeVisible (pan);
    addAndMakeVisible (fader);
    addMouseListener (this, true);    // a click anywhere on the strip, knobs included, selects it
    if (! isMaster)
        addAndMakeVisible (mute);
    if (! isMaster && ! isReturn)
    {
        addAndMakeVisible (solo);
        // Post-fader sends to the return buses
        for (int r = 0; r < Project::numReturns; ++r)
        {
            auto* k = sends.add (new juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox));
            k->setRange (0.0, 1.0);
            k->setDoubleClickReturnValue (true, 0.0);
            k->setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
            k->getProperties().set ("pal", ctx.project.returns[(size_t) r]->colourIndex);
            k->setTooltip ("Send to " + ctx.project.returns[(size_t) r]->name);
            k->setValue (track.send[(size_t) r].load(), juce::dontSendNotification);
            k->onValueChange = [this, k, r] { track.send[(size_t) r] = (float) k->getValue(); ctx.touched(); };
            addAndMakeVisible (k);
        }
    }
}

juce::StringArray ChannelStrip::insertNames() const
{
    juce::StringArray names;
    std::vector<const Module*> byPosition;
    for (auto& m : track.patch.modules)
        if (m.type != ModuleType::Input && m.type != ModuleType::Output)
            byPosition.push_back (&m);
    std::sort (byPosition.begin(), byPosition.end(), [] (auto* a, auto* b) { return a->position.x < b->position.x; });
    for (auto* m : byPosition)
        names.add (m->type == ModuleType::Plugin && m->pluginName.isNotEmpty() ? m->pluginName : moduleInfo (m->type).title);
    return names;
}

void ChannelStrip::tick()
{
    mute.setToggleState (track.muted.load(), juce::dontSendNotification);
    solo.setToggleState (track.soloed.load(), juce::dontSendNotification);
    if (std::abs ((float) fader.getValue() - track.volume.load()) > 0.001f)
        fader.setValue (track.volume.load(), juce::dontSendNotification);
    const float l = track.meter.load();
    if (std::abs (l - shownLevel) > 0.005f)
    {
        shownLevel = l;
        repaint (meterArea.expanded (2));
    }
}

void ChannelStrip::mouseDown (const juce::MouseEvent&)
{
    ctx.selectTrack (slot);
}

void ChannelStrip::resized()
{
    auto r = getLocalBounds().reduced (8, 10);
    r.removeFromTop (8);                          // colour cap
    insertArea = r.removeFromTop (numInsertSlots * 18 - 2);
    r.removeFromTop (6);
    sendArea = r.removeFromTop (30);
    if (! sends.isEmpty())
    {
        auto s = sendArea;
        const int w = s.getWidth() / sends.size();
        for (auto* k : sends)
            k->setBounds (s.removeFromLeft (w).withSizeKeepingCentre (26, 26));
    }
    r.removeFromTop (4);
    pan.setBounds (r.removeFromTop (40).withSizeKeepingCentre (36, 36));
    r.removeFromTop (4);
    r.removeFromBottom (24);                      // name
    auto buttons = r.removeFromBottom (22);
    if (! isMaster && ! isReturn)
    {
        mute.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2).reduced (2, 0));
        solo.setBounds (buttons.reduced (2, 0));
    }
    else if (isReturn)
        mute.setBounds (buttons.withSizeKeepingCentre (buttons.getWidth() / 2, buttons.getHeight()));
    r.removeFromBottom (8);
    meterArea = r.removeFromRight (10).reduced (0, 9);
    fader.setBounds (r.withTrimmedRight (2));
}

void ChannelStrip::paint (juce::Graphics& g)
{
    auto& th = Theme::get();
    const auto c = th.pal (track.colourIndex);
    auto r = getLocalBounds().toFloat();
    const bool selected = slot == ctx.selectedTrack;
    const bool audible = ctx.project.isAudible (slot);

    g.setColour (selected ? th.panel2.brighter (0.03f) : th.panel2);
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (selected ? c.withAlpha (0.7f) : th.line2);
    g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);

    // Colour cap
    g.setColour (c.withAlpha (audible ? 1.0f : 0.4f));
    g.fillRoundedRectangle (r.getX() + 14.0f, r.getY() + 8.0f, r.getWidth() - 28.0f, 3.0f, 1.5f);

    // Insert slots: the devices in this slot's patch, in chain order
    const auto inserts = insertNames();
    auto ins = insertArea.toFloat();
    for (int i = 0; i < numInsertSlots; ++i)
    {
        auto box = ins.removeFromTop (16.0f);
        ins.removeFromTop (2.0f);
        const bool filled = i < inserts.size();
        const bool more = i == numInsertSlots - 1 && inserts.size() > numInsertSlots;
        g.setColour (th.display);
        g.fillRoundedRectangle (box, 3.0f);
        g.setColour (filled ? c.withAlpha (0.45f) : th.line2);
        g.drawRoundedRectangle (box.reduced (0.5f), 3.0f, 1.0f);
        if (filled)
        {
            g.setColour (th.text.withAlpha (0.8f));
            g.setFont (th.label (7.5f));
            g.drawFittedText (more ? "+" + juce::String (inserts.size() - numInsertSlots + 1) + " MORE" : inserts[i].toUpperCase(),
                              box.toNearestInt().reduced (3, 0), juce::Justification::centred, 1);
        }
    }

    // Send labels
    g.setColour (th.muted);
    g.setFont (th.label (7.0f));
    if (! sends.isEmpty())
    {
        for (int i = 0; i < sends.size(); ++i)
        {
            auto b = sends[i]->getBounds();
            g.drawText (juce::String::charToString ((juce::juce_wchar) ('A' + i)),
                        juce::Rectangle<int> (b.getX() - 6, b.getBottom() - 2, b.getWidth() + 12, 10), juce::Justification::centred);
        }
    }
    else
    {
        Glow::drawSpacedText (g, isMaster ? "MAIN OUT" : "FROM SENDS", sendArea.toFloat(), th.label (7.5f), 1.2f, juce::Justification::centred);
    }

    drawMeter (g, meterArea.toFloat(), shownLevel, c);

    // Name plate
    auto plate = r.removeFromBottom (32.0f).reduced (6.0f, 6.0f);
    g.setColour (th.display);
    g.fillRoundedRectangle (plate, 5.0f);
    g.setColour (isMaster || isReturn ? c : th.text);
    g.setFont (isMaster ? th.heading (12.0f) : th.body (11.5f));
    g.drawFittedText (isMaster ? track.name.toUpperCase() : track.name, plate.toNearestInt().reduced (3, 0), juce::Justification::centred, 1);
}

MixerView::MixerView (AppContext& c) : ctx (c)
{
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (false, true);
    addAndMakeVisible (viewport);
    rebuild();
}

void MixerView::rebuild()
{
    builtGeneration = ctx.generation;
    strips.clear();
    for (int i = 0; i < ctx.project.numSlots(); ++i)
        content.addAndMakeVisible (strips.add (new ChannelStrip (ctx, i)));
    resized();
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
            x += 18;              // gap before the returns and master
        strips[i]->setBounds (x, 0, stripWidth, h);
        x += stripWidth + stripGap;
    }
    content.setSize (juce::jmax (x + 6, r.getWidth()), h);
}

void MixerView::tick()
{
    if (builtGeneration != ctx.generation || strips.size() != ctx.project.numSlots())
        rebuild();
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
    Glow::drawSpacedText (g, juce::String ((int) ctx.project.tracks.size()) + " TRACKS  /  " + juce::String (Project::numReturns)
                              + " RETURNS  /  MASTER     SENDS ARE POST-FADER",
                          head, th.label (8.5f), 1.5f, juce::Justification::centredRight);
}
