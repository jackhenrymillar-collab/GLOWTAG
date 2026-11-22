#include "MixerView.h"

namespace
{
    constexpr int minStripWidth = 66;
    constexpr int maxStripWidth = 100;
    constexpr int stripGap = 6;
    constexpr int groupGap = 14;        // between tracks, buses, returns and the master
    constexpr int troughHeight = 84;
    constexpr int insertHeight = 19;
    constexpr float yellowFrom = 0.7f;  // meter zones, as a fraction of full scale
    constexpr float redFrom = 0.9f;
    constexpr float jackRadius = 7.0f;

    const juce::String arrow = juce::String::fromUTF8 ("\xe2\x86\x92 ");

    void setUpKnob (juce::Slider& k, int colour, const juce::String& tip)
    {
        k.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        k.getProperties().set ("pal", colour);
        k.getProperties().set ("console", true);
        k.setTooltip (tip);
    }

    // The same small "random" number every time for one LED, so the same LEDs are always the tired ones
    int ledHash (int slot, int column, int led)
    {
        juce::uint32 h = (juce::uint32) (slot * 7919 + column * 104729 + led * 1299709 + 17);
        h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
        return (int) (h % 997u);
    }

    void drawScrew (juce::Graphics& g, juce::Point<float> p, juce::Colour metal)
    {
        g.setColour (juce::Colour (0xff0e0f0f));
        g.fillEllipse (p.x - 3.0f, p.y - 3.0f, 6.0f, 6.0f);
        g.setColour (metal);
        g.fillEllipse (p.x - 2.0f, p.y - 2.0f, 4.0f, 4.0f);
        g.setColour (juce::Colour (0xff0e0f0f));
        g.drawLine (p.x - 1.6f, p.y - 1.0f, p.x + 1.6f, p.y + 1.0f, 0.8f);
    }
}

//==============================================================================
LampButton::LampButton() : juce::Button ("Analogue")
{
    setClickingTogglesState (true);
    setTooltip ("Analogue: console warmth. Drive the channel hard and it breaks up.");
}

void LampButton::paintButton (juce::Graphics& g, bool over, bool)
{
    auto& th = Theme::get();
    auto r = getLocalBounds().toFloat().reduced (3.0f, 2.0f);
    const bool on = getToggleState();
    const auto orange = juce::Colour (0xffff7a30), red = juce::Colour (0xffee3520);
    const auto lamp = orange.interpolatedWith (red, juce::jlimit (0.0f, 1.0f, heat));

    if (on)
    {
        // Halo, then the lit cap: bright centre falling off to a deeper edge
        g.setColour (lamp.withAlpha (0.16f + 0.10f * heat));
        g.fillRoundedRectangle (r.expanded (3.0f, 2.0f), 6.0f);
        g.setColour (lamp.withAlpha (0.30f));
        g.fillRoundedRectangle (r.expanded (1.5f, 1.0f), 5.0f);
        g.setGradientFill (juce::ColourGradient (lamp.brighter (0.55f), r.getCentreX(), r.getY() + r.getHeight() * 0.35f,
                                                 lamp.darker (0.35f), r.getX(), r.getBottom(), true));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.drawHorizontalLine ((int) r.getY() + 1, r.getX() + 3.0f, r.getRight() - 3.0f);
        g.setColour (lamp.brighter (0.3f));
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
        g.setColour (juce::Colour (0xfffff3e2));
    }
    else
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2a1a14), r.getTopLeft(), juce::Colour (0xff1a100c), r.getBottomLeft(), false));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (juce::Colour (over ? 0xff8a4a2c : 0xff5a2e1c));
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
        g.setColour (juce::Colour (over ? 0xffc27a58 : 0xff9a6248));
    }
    Glow::drawSpacedText (g, "ANALOGUE", r, th.label (7.0f), 0.9f, juce::Justification::centred);
}

//==============================================================================
ChannelStrip::ChannelStrip (AppContext& c, int s)
    : slot (s), track (c.project.slot (s)), ctx (c),
      isMaster (track.kind == TrackKind::Master), isReturn (track.kind == TrackKind::Return), isBus (track.kind == TrackKind::Bus)
{
    const int colour = track.colourIndex;

    trim.setRange (-12.0, 12.0);
    trim.setDoubleClickReturnValue (true, 0.0);
    setUpKnob (trim, colour, "Trim: level into the channel, -12 to +12 dB. Push it to drive Analogue.");
    pan.setRange (-1.0, 1.0);
    pan.setDoubleClickReturnValue (true, 0.0);
    setUpKnob (pan, colour, "Pan");
    width.setRange (0.0, 2.0);
    width.setDoubleClickReturnValue (true, 1.0);
    setUpKnob (width, colour, "Width: mono at the left, as recorded in the middle, extra wide at the right");

    const double unity = isBus ? 1.0 : 0.8;
    fader.setRange (0.0, 1.25);
    fader.setSkewFactor (0.6);
    fader.setDoubleClickReturnValue (true, unity);
    fader.getProperties().set ("pal", colour);
    fader.getProperties().set ("console", true);
    fader.setTooltip ("Volume");

    trim.setValue (track.trimDb.load(), juce::dontSendNotification);
    pan.setValue (track.pan.load(), juce::dontSendNotification);
    width.setValue (track.width.load(), juce::dontSendNotification);
    fader.setValue (track.volume.load(), juce::dontSendNotification);
    trim.onValueChange  = [this] { track.trimDb = (float) trim.getValue(); ctx.touched(); };
    pan.onValueChange   = [this] { track.pan = (float) pan.getValue(); ctx.touched(); };
    width.onValueChange = [this] { track.width = (float) width.getValue(); ctx.touched(); };
    fader.onValueChange = [this] { track.volume = (float) fader.getValue(); ctx.touched(); };

    phase.setTooltip ("Flip phase");
    mono.setTooltip ("Sum to mono");
    phase.onClick = [this] { track.phase = phase.getToggleState(); ctx.touched(); };
    mono.onClick  = [this] { track.mono = mono.getToggleState(); ctx.touched(); };
    mute.onClick  = [this] { track.muted = mute.getToggleState(); ctx.touched(); };
    solo.onClick  = [this] { track.soloed = solo.getToggleState(); ctx.touched(); };
    analogue.onClick = [this] { track.analogue = analogue.getToggleState(); ctx.touched(); };

    for (auto* comp : std::initializer_list<juce::Component*> { &trim, &pan, &width, &fader, &phase, &mono, &analogue })
        addAndMakeVisible (comp);
    addMouseListener (this, true);    // a click anywhere on the strip, knobs included, selects it
    if (! isMaster)
        addAndMakeVisible (mute);
    if (! isMaster && ! isReturn && ! isBus)
    {
        addAndMakeVisible (solo);
        // Post-fader sends to the return buses
        for (int r = 0; r < Project::numReturns; ++r)
        {
            auto* k = sends.add (new juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox));
            k->setRange (0.0, 1.0);
            k->setDoubleClickReturnValue (true, 0.0);
            setUpKnob (*k, ctx.project.returns[(size_t) r]->colourIndex, "Send to " + ctx.project.returns[(size_t) r]->name);
            k->setValue (track.send[(size_t) r].load(), juce::dontSendNotification);
            k->onValueChange = [this, k, r] { track.send[(size_t) r] = (float) k->getValue(); ctx.touched(); };
            addAndMakeVisible (k);
        }
    }
}

std::vector<ChannelStrip::Insert> ChannelStrip::inserts() const
{
    std::vector<const Module*> byPosition;
    for (auto& m : track.patch.modules)
        if (m.type != ModuleType::Input && m.type != ModuleType::Output && ! moduleInfo (m.type).utility)
            byPosition.push_back (&m);
    std::sort (byPosition.begin(), byPosition.end(), [] (auto* a, auto* b) { return a->position.x < b->position.x; });
    std::vector<Insert> list;
    for (auto* m : byPosition)
        list.push_back ({ m->id, m->type == ModuleType::Plugin && m->pluginName.isNotEmpty() ? m->pluginName : moduleInfo (m->type).title,
                          m->type == ModuleType::Compressor });
    return list;
}

void ChannelStrip::tick()
{
    phase.setToggleState (track.phase.load(), juce::dontSendNotification);
    mono.setToggleState (track.mono.load(), juce::dontSendNotification);
    mute.setToggleState (track.muted.load(), juce::dontSendNotification);
    solo.setToggleState (track.soloed.load(), juce::dontSendNotification);
    analogue.setToggleState (track.analogue.load(), juce::dontSendNotification);
    if (std::abs ((float) fader.getValue() - track.volume.load()) > 0.001f)
        fader.setValue (track.volume.load(), juce::dontSendNotification);

    const float heat = track.analogue.load() ? track.heat.load() : 0.0f;
    if (std::abs (heat - shownHeat) > 0.02f)
    {
        shownHeat = heat;
        analogue.heat = heat;
        analogue.repaint();
    }

    // Gain reduction from the first compressor in the chain, in dB
    float reduction = 0.0f;
    for (auto& ins : inserts())
        if (ins.compressor)
        {
            if (auto* node = ctx.engine.liveNode (slot, ins.moduleId))
                reduction = node->display.load();
            break;
        }

    const float l = track.meter.load();
    const bool wasHeld = clipHold > 0;
    if (l >= 1.0f) clipHold = 45;                 // an over stays lit for a second and a half
    else if (clipHold > 0) --clipHold;
    if (std::abs (l - shownLevel) > 0.005f || std::abs (reduction - shownReduction) > 0.1f || wasHeld != (clipHold > 0))
    {
        shownLevel = l;
        shownReduction = reduction;
        repaint (meterArea.getUnion (reductionArea).expanded (3));
    }
}

void ChannelStrip::mouseDown (const juce::MouseEvent& e)
{
    ctx.selectTrack (slot);
    if (e.originalComponent != this)
        return;
    if (e.mods.isPopupMenu())
    {
        showMenu();
        return;
    }
    // A device slot: jump to that device in the panel below
    if (insertArea.contains (e.getPosition()))
    {
        const auto list = inserts();
        const int index = (e.y - insertArea.getY()) / (insertHeight + 2);
        if (index >= 0 && index < numInsertSlots && index < (int) list.size())
            ctx.showDevice (slot, list[(size_t) index].moduleId);
    }
    else if (nameArea.contains (e.getPosition()) && e.getNumberOfClicks() == 2 && ! isMaster)
        rename();
}

void ChannelStrip::showMenu()
{
    juce::PopupMenu menu, out;
    auto& p = ctx.project;
    if (! isMaster)
    {
        for (int i = 0; i < p.numSlots(); ++i)
            if (p.slot (i).takesInput() && i != slot)
                out.addItem (100 + i, p.slot (i).name, p.canRoute (slot, i), p.outputSlot (slot) == i);
        menu.addSubMenu ("Output to", out);
        menu.addItem (1, "Rename...");
    }
    if (isBus)
        menu.addItem (2, "Remove bus");
    if (menu.getNumItems() == 0)
        return;
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                        [safe = juce::Component::SafePointer<ChannelStrip> (this)] (int result)
                        {
                            if (safe == nullptr || result == 0) return;
                            auto& project = safe->ctx.project;
                            if (result >= 100)
                            {
                                if (project.setOutput (safe->slot, result - 100))
                                    if (auto* parent = safe->getParentComponent()) parent->repaint();
                            }
                            else if (result == 1) safe->rename();
                            else if (result == 2 && safe->onRemoveBus) safe->onRemoveBus (safe->slot - project.busSlot (0));
                        });
}

void ChannelStrip::rename()
{
    auto* box = new juce::AlertWindow ("Rename", {}, juce::MessageBoxIconType::NoIcon);
    box->addTextEditor ("name", track.name);
    box->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    box->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    box->enterModalState (true, juce::ModalCallbackFunction::create (
        [safe = juce::Component::SafePointer<ChannelStrip> (this), box] (int result)
        {
            const auto name = box->getTextEditorContents ("name").trim();
            if (safe != nullptr && result == 1 && name.isNotEmpty())
            {
                safe->track.name = name;
                safe->ctx.clipsChanged();
            }
        }), true);
}

void ChannelStrip::resized()
{
    auto r = getLocalBounds().reduced (6, 8);
    r.removeFromTop (6);                           // colour cap
    nameArea = r.removeFromTop (20);
    r.removeFromTop (4);

    // Trim with phase and mono beside it
    auto top = r.removeFromTop (40);
    auto buttons = top.removeFromRight (22).withSizeKeepingCentre (22, 34);
    phase.setBounds (buttons.removeFromTop (16));
    mono.setBounds (buttons.removeFromBottom (16));
    trim.setBounds (top.removeFromTop (31).withSizeKeepingCentre (34, 34));
    r.removeFromTop (4);

    // Jacks, destination, mute / solo and the Analogue lamp are fixed; devices and fader share the rest
    auto jacks = r.removeFromBottom (22);
    const float jy = (float) jacks.getCentreY();
    if (hasInJack() && hasOutJack())
    {
        inJackPos = { (float) jacks.getCentreX() - 12.0f, jy };
        outJackPos = { (float) jacks.getCentreX() + 12.0f, jy };
    }
    else
        inJackPos = outJackPos = { (float) jacks.getCentreX(), jy };
    destArea = r.removeFromBottom (13);
    r.removeFromBottom (3);
    auto ms = r.removeFromBottom (20);
    if (! isMaster && ! isReturn && ! isBus)
    {
        mute.setBounds (ms.removeFromLeft (ms.getWidth() / 2).reduced (2, 0));
        solo.setBounds (ms.reduced (2, 0));
    }
    else if (! isMaster)
        mute.setBounds (ms.withSizeKeepingCentre (ms.getWidth() / 2, ms.getHeight()));
    r.removeFromBottom (4);

    const int fixed = 38 + 38 + 22 + 12;           // sends, pan and width, lamp, gaps
    const int spare = r.getHeight() - fixed;
    numInsertSlots = spare >= 8 * (insertHeight + 2) + 120 ? 8 : spare >= 6 * (insertHeight + 2) + 100 ? 6 : 4;
    insertArea = r.removeFromTop (numInsertSlots * (insertHeight + 2) - 2);
    r.removeFromTop (4);

    sendArea = r.removeFromTop (38);
    if (! sends.isEmpty())
    {
        auto s = sendArea.withTrimmedBottom (10);
        const int w = s.getWidth() / sends.size();
        for (auto* k : sends)
            k->setBounds (s.removeFromLeft (w).withSizeKeepingCentre (28, 28));
    }
    auto pw = r.removeFromTop (38).withTrimmedBottom (10);
    pan.setBounds (pw.removeFromLeft (pw.getWidth() / 2).withSizeKeepingCentre (28, 28));
    width.setBounds (pw.withSizeKeepingCentre (28, 28));
    r.removeFromTop (2);
    analogue.setBounds (r.removeFromTop (22));
    r.removeFromTop (6);

    reductionArea = r.removeFromRight (6).reduced (0, 9);
    r.removeFromRight (2);
    meterArea = r.removeFromRight (8).reduced (0, 9);
    fader.setBounds (r.withTrimmedRight (2));
}

void ChannelStrip::drawMeters (juce::Graphics& g, juce::Colour c) const
{
    auto& th = Theme::get();
    const float level = juce::jlimit (0.0f, 1.0f, shownLevel);
    const float reduction = juce::jlimit (0.0f, 1.0f, shownReduction / 18.0f);
    const auto amber = juce::Colour (0xffffb347);

    if (th.isVintage())
    {
        // LED ladders: one lamp per step, a few of them tired or dead
        auto ladder = [&] (juce::Rectangle<int> area, int column, float value, bool fromTop)
        {
            const int count = juce::jmax (4, area.getHeight() / 9);
            const float pitch = (float) area.getHeight() / (float) count;
            for (int j = 0; j < count; ++j)                      // j counts up from the bottom
            {
                const float frac = ((float) j + 0.5f) / (float) count;
                const float cy = area.getBottom() - ((float) j + 0.5f) * pitch;
                auto led = juce::Rectangle<float> (0.0f, 0.0f, 6.0f, 6.0f).withCentre ({ (float) area.getCentreX(), cy });
                const int h = ledHash (slot, column, j);
                const bool dead = h % 23 == 0, tired = h % 6 == 0;
                const bool lit = ! dead && (fromTop ? (1.0f - frac) < value : frac < value || (j == count - 1 && clipHold > 0 && column == 0));
                auto colour = column == 1 ? amber : frac >= redFrom ? th.danger : frac >= yellowFrom ? th.warn : c;

                g.setColour (juce::Colour (0xff080909));
                g.fillEllipse (led.expanded (0.8f));
                if (lit)
                {
                    if (tired) colour = colour.withMultipliedSaturation (0.45f).withMultipliedBrightness (0.55f);
                    else { g.setColour (colour.withAlpha (0.28f)); g.fillEllipse (led.expanded (2.2f)); }
                    g.setColour (colour);
                    g.fillEllipse (led);
                }
                else
                {
                    g.setColour (tired ? Theme::tinted (0xff262722, c, 0.08f) : juce::Colour (0xff1c1e1e));
                    g.fillEllipse (led);
                }
                if (tired)
                {
                    g.setColour (Theme::tinted (0xff6a6650, c, 0.25f).withAlpha (0.8f));
                    g.drawEllipse (led, 0.8f);
                }
            }
        };
        ladder (meterArea, 0, level, false);
        ladder (reductionArea, 1, reduction, true);
        return;
    }

    // Level: theme colour, yellow in the danger zone, red at the peak
    auto m = meterArea.toFloat().withSizeKeepingCentre (6.0f, (float) meterArea.getHeight());
    g.setColour (th.display);
    g.fillRoundedRectangle (m, 2.0f);
    if (level > 0.001f)
    {
        juce::Graphics::ScopedSaveState save (g);
        juce::Path clip;
        clip.addRoundedRectangle (m, 2.0f);
        g.reduceClipRegion (clip);
        const float top = m.getBottom() - m.getHeight() * level;
        const float yellowY = m.getBottom() - m.getHeight() * yellowFrom, redY = m.getBottom() - m.getHeight() * redFrom;
        g.setGradientFill (juce::ColourGradient (c.withAlpha (0.35f), m.getBottomLeft(), c, { m.getX(), yellowY }, false));
        g.fillRect (m.withTop (juce::jmax (top, yellowY)));
        if (top < yellowY) { g.setColour (th.warn);   g.fillRect (m.withTop (juce::jmax (top, redY)).withBottom (yellowY)); }
        if (top < redY)    { g.setColour (th.danger); g.fillRect (m.withTop (top).withBottom (redY)); }
    }
    if (clipHold > 0)
    {
        g.setColour (th.danger);
        g.fillRoundedRectangle (m.withHeight (4.0f), 2.0f);
    }

    // Gain reduction: amber, growing down from the top
    auto gr = reductionArea.toFloat().withSizeKeepingCentre (4.0f, (float) reductionArea.getHeight());
    g.setColour (th.display);
    g.fillRoundedRectangle (gr, 2.0f);
    if (reduction > 0.01f)
    {
        g.setColour (amber);
        g.fillRoundedRectangle (gr.withHeight (gr.getHeight() * reduction), 2.0f);
    }
}

void ChannelStrip::paint (juce::Graphics& g)
{
    auto& th = Theme::get();
    const auto c = th.pal (track.colourIndex);
    auto r = getLocalBounds().toFloat();
    const bool selected = slot == ctx.selectedTrack;
    const bool audible = ctx.project.isAudible (slot);
    const bool vintage = th.isVintage();
    const bool sums = track.takesInput();

    if (vintage)
    {
        // A worn metal channel: brushed face, screws in the corners, everything leaning to the theme colour
        g.setGradientFill (juce::ColourGradient (Theme::tinted (0xff2d2d2e, c, sums ? 0.17f : 0.10f), r.getTopLeft(),
                                                 Theme::tinted (0xff202021, c, sums ? 0.09f : 0.05f), r.getBottomLeft(), false));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (juce::Colours::white.withAlpha (0.012f));
        for (float x = r.getX() + 3.0f; x < r.getRight(); x += 4.0f)
            g.drawVerticalLine ((int) x, r.getY() + 2.0f, r.getBottom() - 2.0f);
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.drawHorizontalLine ((int) r.getY() + 1, r.getX() + 3.0f, r.getRight() - 3.0f);
        g.setColour (selected ? c.withAlpha (0.8f) : Theme::tinted (0xff46474c, c, 0.24f));
        g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
        const auto metal = Theme::tinted (0xffb4b4ae, c, 0.28f);
        for (auto p : { juce::Point<float> (r.getX() + 6.0f, r.getY() + 6.0f), { r.getRight() - 6.0f, r.getY() + 6.0f },
                        { r.getX() + 6.0f, r.getBottom() - 6.0f }, { r.getRight() - 6.0f, r.getBottom() - 6.0f } })
            drawScrew (g, p, metal);
    }
    else
    {
        g.setColour (sums ? th.panel2.interpolatedWith (c, 0.05f) : selected ? th.panel2.brighter (0.03f) : th.panel2);
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (selected ? c.withAlpha (0.7f) : sums ? th.line2.interpolatedWith (c, 0.35f) : th.line2);
        g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);
    }

    // Colour cap
    g.setColour (c.withAlpha (audible ? 1.0f : 0.4f));
    g.fillRoundedRectangle (r.getX() + 14.0f, r.getY() + 8.0f, r.getWidth() - 28.0f, vintage ? 3.0f : 2.5f, 1.5f);

    // Name: a plate, or a strip of tape on the vintage desk
    const auto shownName = isMaster ? track.name.toUpperCase() : track.name;
    if (vintage)
    {
        juce::Graphics::ScopedSaveState save (g);
        auto tape = nameArea.toFloat().reduced (1.0f, 1.5f);
        g.addTransform (juce::AffineTransform::rotation (-0.012f, tape.getCentreX(), tape.getCentreY()));
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.fillRect (tape.translated (0.0f, 1.0f));
        g.setColour (Theme::tinted (0xffe2e0d6, c, 0.38f));
        g.fillRect (tape);
        g.setColour (juce::Colour (0xff141614));
        g.setFont (isMaster ? th.heading (10.5f) : th.body (11.0f));
        g.drawFittedText (shownName, tape.toNearestInt().reduced (3, 0), juce::Justification::centred, 1);
    }
    else
    {
        g.setColour (isMaster || isReturn || isBus ? c : th.text);
        g.setFont (isMaster ? th.heading (12.0f) : th.body (12.0f));
        g.drawFittedText (shownName, nameArea.reduced (1, 0), juce::Justification::centred, 1);
    }

    // Device slots: the devices in this slot's patch, in chain order. Click one to see it below.
    const auto list = inserts();
    auto ins = insertArea.toFloat();
    for (int i = 0; i < numInsertSlots; ++i)
    {
        auto box = ins.removeFromTop ((float) insertHeight);
        ins.removeFromTop (2.0f);
        const bool filled = i < (int) list.size();
        const bool more = i == numInsertSlots - 1 && (int) list.size() > numInsertSlots;
        const bool shown = filled && ! more && ctx.shownSlot == slot && ctx.shownModule == list[(size_t) i].moduleId;
        g.setColour (filled ? (vintage ? juce::Colour (0xff111212) : th.display) : th.display.withAlpha (0.45f));
        g.fillRoundedRectangle (box, 3.0f);
        if (filled)
        {
            g.setColour (shown ? c : vintage ? Theme::tinted (0xff46474c, c, 0.24f) : th.line.brighter (0.08f));
            g.drawRoundedRectangle (box.reduced (0.5f), 3.0f, 1.0f);
            if (shown)
            {
                g.setColour (c.withAlpha (0.12f));
                g.fillRoundedRectangle (box, 3.0f);
            }
            g.setColour (shown ? c : th.text);
            g.setFont (th.label (9.5f));
            g.drawFittedText (more ? "+" + juce::String ((int) list.size() - numInsertSlots + 1) + " MORE" : list[(size_t) i].name.toUpperCase(),
                              box.toNearestInt().reduced (3, 0), juce::Justification::centred, 1, 0.75f);
        }
    }

    // Knob captions
    g.setColour (th.muted);
    g.setFont (th.label (7.0f));
    auto caption = [&] (const juce::Component& k, const juce::String& text)
    {
        auto b = k.getBounds();
        Glow::drawSpacedText (g, text, juce::Rectangle<float> ((float) b.getX() - 10.0f, (float) b.getBottom() - 1.0f, (float) b.getWidth() + 20.0f, 9.0f),
                              th.label (7.0f), 0.8f, juce::Justification::centred);
    };
    caption (trim, "TRIM");
    caption (pan, "PAN");
    caption (width, "WIDTH");
    if (! sends.isEmpty())
        for (int i = 0; i < sends.size(); ++i)
            caption (*sends[i], juce::String::charToString ((juce::juce_wchar) ('A' + i)));
    else
        Glow::drawSpacedText (g, isMaster ? "MAIN OUT" : isReturn ? "FROM SENDS" : "SUM", sendArea.toFloat(), th.label (7.5f), 1.2f, juce::Justification::centred);

    drawMeters (g, c);

    // Where it goes
    g.setColour (c.withAlpha (audible ? 0.95f : 0.5f));
    const auto dest = isMaster ? juce::String ("SPEAKERS") : arrow + ctx.project.slot (ctx.project.outputSlot (slot)).name.toUpperCase();
    g.setFont (th.label (7.5f));
    g.drawFittedText (dest, destArea, juce::Justification::centred, 1, 0.7f);
}

//==============================================================================
PatchLayer::PatchLayer (AppContext& c, MixerView& o) : ctx (c), owner (o) {}

void PatchLayer::reset()
{
    from = target = -1;
    dragging = armed = moved = false;
    repaint();
}

juce::Point<float> PatchLayer::jack (int slot, bool output) const
{
    if (auto* s = owner.strip (slot))
        return (output ? s->outJack() : s->inJack()) + s->getPosition().toFloat();
    return {};
}

int PatchLayer::outJackAt (juce::Point<float> p) const
{
    for (int i = 0; i < owner.numStrips(); ++i)
        if (owner.strip (i)->hasOutJack() && jack (i, true).getDistanceFrom (p) <= jackRadius + 4.0f)
            return i;
    return -1;
}

int PatchLayer::targetAt (juce::Point<float> p) const
{
    if (from < 0)
        return -1;
    for (int i = 0; i < owner.numStrips(); ++i)
    {
        auto* s = owner.strip (i);
        if (! s->hasInJack() || ! ctx.project.canRoute (from, i))
            continue;
        // Anywhere on the strip, or the part of the trough underneath it
        if (s->getBounds().withBottom (trough.getBottom()).toFloat().contains (p) || jack (i, false).getDistanceFrom (p) <= jackRadius + 8.0f)
            return i;
    }
    return -1;
}

bool PatchLayer::hitTest (int x, int y)
{
    if (dragging || armed)
        return true;
    return outJackAt ({ (float) x, (float) y }) >= 0;
}

void PatchLayer::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.position;
    if (armed)
    {
        const int t = e.mods.isPopupMenu() ? -1 : targetAt (p);
        if (t >= 0)
            ctx.project.setOutput (from, t);
        const int again = t < 0 && ! e.mods.isPopupMenu() ? outJackAt (p) : -1;
        reset();
        if (again < 0 || again == from)
            return;                       // plugged in, or cancelled
        // fall through: another output jack was picked up instead
    }
    from = outJackAt (p);
    if (from < 0)
        return;
    ctx.selectTrack (from);
    dragging = true;
    moved = false;
    end = p;
    target = -1;
    repaint();
}

void PatchLayer::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging)
        return;
    end = e.position;
    moved = moved || e.getDistanceFromDragStart() > 4;
    target = targetAt (end);
    repaint();
}

void PatchLayer::mouseUp (const juce::MouseEvent&)
{
    if (! dragging)
        return;
    dragging = false;
    if (moved)
    {
        if (target >= 0)
            ctx.project.setOutput (from, target);
        reset();
    }
    else
    {
        armed = true;       // a click: the next click chooses where it goes
        repaint();
    }
}

void PatchLayer::mouseMove (const juce::MouseEvent& e)
{
    if (! armed)
        return;
    end = e.position;
    target = targetAt (end);
    repaint();
}

void PatchLayer::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int j = outJackAt (e.position);
    if (j >= 0)
        ctx.project.setOutput (j, ctx.project.masterSlot());
    reset();
}

juce::Path PatchLayer::cable (juce::Point<float> a, juce::Point<float> b, float maxSag)
{
    // A cable hanging between two jacks: the further apart, the deeper it drapes into the trough
    const float sag = juce::jmin (maxSag, 40.0f + std::abs (b.x - a.x) * 0.14f);
    juce::Path p;
    p.startNewSubPath (a);
    p.cubicTo (a.translated (0.0f, sag), b.translated (0.0f, sag), b);
    return p;
}

void PatchLayer::paint (juce::Graphics& g)
{
    auto& th = Theme::get();
    auto& project = ctx.project;
    const bool vintage = th.isVintage();
    const auto accent = th.themeColour();

    // The trough
    auto t = trough.toFloat();
    g.setColour (vintage ? juce::Colour (0xff0e0f0f) : th.display.withAlpha (0.55f));
    g.fillRoundedRectangle (t, 8.0f);
    g.setColour ((vintage ? Theme::tinted (0xff1a1a1b, accent, 0.06f) : th.line2).withAlpha (0.6f));
    for (float x = t.getX() + 24.0f; x < t.getRight() - 4.0f; x += 24.0f)
        g.drawVerticalLine ((int) x, t.getY() + 3.0f, t.getBottom() - 3.0f);
    juce::Path outline;
    outline.addRoundedRectangle (t.reduced (0.5f), 8.0f);
    g.setColour (vintage ? Theme::tinted (0xff46474c, accent, 0.24f) : th.line);
    if (vintage)
        g.strokePath (outline, juce::PathStrokeType (1.0f));
    else
    {
        juce::Path dashed;
        const float pattern[] = { 4.0f, 4.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, pattern, 2);
        g.fillPath (dashed);
    }
    g.setColour (th.muted.withAlpha (0.8f));
    auto captions = t.reduced (12.0f, 5.0f).removeFromBottom (10.0f);
    Glow::drawSpacedText (g, "PATCH TROUGH", captions, th.label (7.5f), 1.4f, juce::Justification::bottomLeft);
    Glow::drawSpacedText (g, armed ? "CLICK A BUS OR THE MASTER TO PLUG IN    CLICK ANYWHERE ELSE TO CANCEL"
                                   : "DRAG AN OUTPUT JACK TO A BUS OR THE MASTER    DOUBLE-CLICK A JACK FOR THE MASTER",
                          captions, th.label (7.5f), 1.4f, juce::Justification::bottomRight);

    const float maxSag = ((float) trough.getBottom() - 20.0f - jack (0, true).y) / 0.75f;
    const bool playing = ctx.engine.isPlaying();
    const double phase = std::fmod (juce::Time::getMillisecondCounterHiRes() / 1600.0, 1.0);
    const bool patching = (dragging || armed) && from >= 0;

    // Cables: every channel to the bus or master it feeds
    for (int i = 0; i < owner.numStrips() && i < project.numSlots(); ++i)
    {
        auto* s = owner.strip (i);
        if (! s->hasOutJack() || (patching && i == from))
            continue;
        const int dest = project.outputSlot (i);
        if (dest < 0 || owner.strip (dest) == nullptr)
            continue;
        const auto col = th.pal (s->track.colourIndex);
        const float level = juce::jlimit (0.0f, 1.0f, s->track.meter.load());
        const auto path = cable (jack (i, true), jack (dest, false), maxSag);
        Glow::strokeGlow (g, path, col.withAlpha (project.isAudible (i) ? 0.9f : 0.4f), 1.5f, playing ? level : 0.0f);
        if (playing && level > 0.02f)
        {
            const float len = path.getLength();
            for (int d = 0; d < 3; ++d)
            {
                const auto p = path.getPointAlongPath ((float) std::fmod (phase + d / 3.0, 1.0) * len);
                g.setColour (col.brighter (0.6f).withAlpha (0.3f + 0.7f * level));
                g.fillEllipse (p.x - 2.2f, p.y - 2.2f, 4.4f, 4.4f);
            }
        }
    }

    // The cable in hand
    if (patching)
    {
        const auto col = th.pal (owner.strip (from)->track.colourIndex);
        const auto to = target >= 0 ? jack (target, false) : end;
        Glow::strokeGlow (g, cable (jack (from, true), to, maxSag), col, 1.7f, 1.0f);
    }

    // Jacks: a solid ring for an output, a dashed ring for an input
    for (int i = 0; i < owner.numStrips(); ++i)
    {
        auto* s = owner.strip (i);
        const auto col = th.pal (s->track.colourIndex);
        const auto rim = vintage ? Theme::tinted (0xffb4b4ae, col, 0.28f) : col;
        auto drawJack = [&] (juce::Point<float> p, bool input, bool lit)
        {
            auto ring = juce::Rectangle<float> (jackRadius * 2.0f, jackRadius * 2.0f).withCentre (p);
            if (lit)
            {
                g.setColour (col.withAlpha (0.3f));
                g.fillEllipse (ring.expanded (4.0f));
            }
            g.setColour (juce::Colour (0xff0a0b0b));
            g.fillEllipse (ring);
            g.setColour (lit ? th.text : rim);
            if (input)
            {
                juce::Path circle, dashed;
                circle.addEllipse (ring.reduced (0.5f));
                const float pattern[] = { 2.5f, 2.0f };
                juce::PathStrokeType (1.3f).createDashedStroke (dashed, circle, pattern, 2);
                g.fillPath (dashed);
            }
            else
                g.drawEllipse (ring.reduced (0.5f), 1.3f);
            g.setColour (vintage ? juce::Colour (0xff000000) : col.withAlpha (0.6f));
            g.fillEllipse (ring.reduced (vintage ? 3.5f : 4.0f));
            if (vintage)
            {
                g.setColour (Theme::tinted (0xff5a5a56, col, 0.3f));
                g.drawEllipse (ring.reduced (2.6f), 1.2f);
            }
        };
        if (s->hasInJack())
            drawJack (jack (i, false), true, patching && (target == i || (target < 0 && project.canRoute (from, i))));
        if (s->hasOutJack())
            drawJack (jack (i, true), false, patching && from == i);
    }
}

//==============================================================================
MixerView::MixerView (AppContext& c) : ctx (c), patchLayer (c, *this)
{
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (false, true);
    addAndMakeVisible (viewport);

    addBus.setTooltip ("Add a bus: a channel other channels can be patched into");
    addBus.onClick = [this]
    {
        if ((int) ctx.project.buses.size() >= Project::maxBuses)
            return;
        ctx.restructure ([this] { return ctx.project.addBus() >= 0; });
    };
    vintage.setClickingTogglesState (true);
    vintage.setTooltip ("Vintage console: the mixer as worn hardware with LED meters");
    vintage.onClick = [this] { Theme::get().setVintage (vintage.getToggleState()); };
    addAndMakeVisible (addBus);
    addAndMakeVisible (vintage);
    rebuild();
}

void MixerView::rebuild()
{
    builtGeneration = ctx.generation;
    patchLayer.reset();
    strips.clear();
    for (int i = 0; i < ctx.project.numSlots(); ++i)
    {
        auto* s = strips.add (new ChannelStrip (ctx, i));
        s->onRemoveBus = [this] (int bus)
        {
            // After this menu callback returns: removing the bus rebuilds every strip, this one included
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MixerView> (this), bus]
            {
                if (safe != nullptr)
                    safe->ctx.restructure ([safe, bus] { safe->ctx.project.removeBus (bus); return true; });
            });
        };
        content.addAndMakeVisible (s);
    }
    content.addAndMakeVisible (patchLayer);
    patchLayer.toFront (false);
    resized();
}

void MixerView::resized()
{
    auto r = getLocalBounds();
    auto head = r.removeFromTop (34).reduced (14, 6);
    vintage.setBounds (head.removeFromRight (84));
    head.removeFromRight (8);
    addBus.setBounds (head.removeFromRight (68));
    viewport.setBounds (r);

    const int h = juce::jmax (420, r.getHeight() - 14);
    const int stripHeight = h - troughHeight - 6;
    auto& p = ctx.project;
    // Fit every strip on screen when there is room, otherwise scroll sideways
    const int fitted = (r.getWidth() - 24 - 3 * groupGap - stripGap * (strips.size() - 1)) / juce::jmax (1, strips.size());
    const int stripWidth = juce::jlimit (minStripWidth, maxStripWidth, fitted);
    int x = 12;
    for (int i = 0; i < strips.size(); ++i)
    {
        if (i > 0 && (i == p.busSlot (0) || i == p.returnSlot (0) || i == p.masterSlot()))
            x += groupGap;            // a gap before the buses, the returns and the master
        strips[i]->setBounds (x, 0, stripWidth, stripHeight);
        x += stripWidth + stripGap;
    }
    content.setSize (juce::jmax (x + 6, r.getWidth()), h);
    patchLayer.setBounds (content.getLocalBounds());
    patchLayer.trough = { 12, stripHeight + 6, content.getWidth() - 24, troughHeight };
}

void MixerView::tick()
{
    if (builtGeneration != ctx.generation || strips.size() != ctx.project.numSlots())
        rebuild();
    vintage.setToggleState (Theme::get().isVintage(), juce::dontSendNotification);
    addBus.setEnabled ((int) ctx.project.buses.size() < Project::maxBuses);
    for (auto* s : strips)
        s->tick();
    if (ctx.engine.isPlaying() && ! strips.isEmpty())
        patchLayer.repaint (0, (int) strips[0]->outJack().y - 12, patchLayer.getWidth(), patchLayer.getHeight());
}

void MixerView::paint (juce::Graphics& g)
{
    auto& th = Theme::get();
    g.fillAll (th.isVintage() ? Theme::tinted (0xff161617, th.themeColour(), 0.04f) : th.bg);
    auto head = getLocalBounds().reduced (14, 0).removeFromTop (34).toFloat();
    g.setColour (th.text);
    Glow::drawSpacedText (g, "MIXER", head, th.heading (14.0f), 2.5f, juce::Justification::centredLeft);
    g.setColour (th.muted);
    auto& p = ctx.project;
    Glow::drawSpacedText (g, juce::String ((int) p.tracks.size()) + " TRACKS  /  " + juce::String ((int) p.buses.size()) + (p.buses.size() == 1 ? " BUS  /  " : " BUSES  /  ")
                              + juce::String (Project::numReturns) + " RETURNS  /  MASTER",
                          head.withTrimmedRight (176.0f), th.label (8.5f), 1.5f, juce::Justification::centredRight);
}
