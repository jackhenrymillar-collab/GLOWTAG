#include "DevicePanel.h"
#include "../Engine/Samples.h"

namespace
{
    Icon iconFor (ModuleType t)
    {
        switch (t)
        {
            case ModuleType::Sampler:    return Icon::Sampler;
            case ModuleType::Synth:      return Icon::Synth;
            case ModuleType::Drums:      return Icon::Drums;
            case ModuleType::Filter:     return Icon::Filter;
            case ModuleType::Delay:      return Icon::Delay;
            case ModuleType::Reverb:     return Icon::Reverb;
            case ModuleType::EQ:         return Icon::EQ;
            case ModuleType::Compressor: return Icon::Compressor;
            case ModuleType::LFO:        return Icon::LFO;
            case ModuleType::Drive:      return Icon::Drive;
            case ModuleType::Input:      return Icon::Play;
            case ModuleType::Output:     return Icon::Velocity;
        }
        return Icon::Filter;
    }

    constexpr float headerHeight = 44.0f;
    constexpr float jackY = 60.0f;

    bool hasExtra (ModuleType t) { return t == ModuleType::Sampler || t == ModuleType::Drums || t == ModuleType::LFO || t == ModuleType::Compressor; }
    int extraWidth (ModuleType t) { return t == ModuleType::Sampler ? 96 : (t == ModuleType::Drums ? 104 : 26); }
}

//==============================================================================
ModuleCard::ModuleCard (AppContext& c, Patch& p, Track& t, int trackIdx, int id, int n)
    : ctx (c), patch (p), track (t), trackIndex (trackIdx), moduleId (id), numeral (n)
{
    const auto& info = moduleInfo (module().type);
    for (size_t k = 0; k < info.knobs.size(); ++k)
    {
        const auto& name = info.knobs[k].first;
        const bool bipolar = module().type == ModuleType::EQ && k < 3;
        auto* s = sliders.add (new juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox));
        s->setRange (bipolar ? -1.0 : 0.0, 1.0);
        s->setValue (module().knobValues[k], juce::dontSendNotification);
        s->setDoubleClickReturnValue (true, info.knobs[k].second);
        s->setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        s->getProperties().set ("pal", info.pal);
        s->setTooltip (name);
        s->onValueChange = [this, k, s]
        {
            const float v = (float) s->getValue();
            if (auto* m = patch.find (moduleId))
                m->knobValues[k] = v;
            ctx.engine.setKnob (trackIndex, moduleId, (int) k, v);   // straight to the running patch
        };
        addAndMakeVisible (s);
    }
    setSize ((int) info.width, (int) Patch::cardHeight);
}

// Knob value as stored in the patch (for drawing the sampler window)
#define k0(i) ((float) sliders[(i)]->getValue())

bool ModuleCard::isEndpoint() const
{
    return module().type == ModuleType::Input || module().type == ModuleType::Output;
}

std::vector<int> ModuleCard::jacks() const
{
    const auto& info = moduleInfo (module().type);
    std::vector<int> j;
    if (info.hasAudioIn)  j.push_back (Port::audioIn);
    if (info.hasAudioOut) j.push_back (Port::audioOut);
    if (info.hasModOut)   j.push_back (Port::modOut);
    for (int k = 0; k < (int) info.knobs.size(); ++k)
        j.push_back (Port::modIn (k));
    return j;
}

juce::Point<float> ModuleCard::jackPosition (int port) const
{
    const float w = (float) getWidth();
    if (port == Port::audioIn)  return { 0.0f, jackY };
    if (port == Port::audioOut) return { w, jackY };
    if (port == Port::modOut)   return { w, jackY };

    // Modulation inputs sit just left of each knob's label
    const int k = port - Port::modInBase;
    if (k >= 0 && k < sliders.size())
    {
        auto b = sliders[k]->getBounds().toFloat();
        return { b.getX() - 4.0f, b.getBottom() - 5.0f };
    }
    return {};
}

void ModuleCard::resized()
{
    const int n = sliders.size();
    if (n == 0)
        return;

    auto r = getLocalBounds().reduced (10, 0).withTrimmedTop ((int) headerHeight).withTrimmedBottom (8);
    if (hasExtra (module().type))
    {
        extraArea = r.removeFromLeft (extraWidth (module().type)).reduced (0, 4);
        r.removeFromLeft (6);
    }
    const int rows = n > 4 ? 2 : 1;
    const int cols = (n + rows - 1) / rows;
    const int cellW = r.getWidth() / cols;
    const int cellH = r.getHeight() / rows;
    const int knob = rows == 2 ? 40 : 50;

    for (int i = 0; i < n; ++i)
    {
        auto cell = juce::Rectangle<int> (r.getX() + (i % cols) * cellW, r.getY() + (i / cols) * cellH, cellW, cellH);
        cell.removeFromBottom (16);
        sliders[i]->setBounds (cell.withSizeKeepingCentre (knob, knob));
    }
}

void ModuleCard::paint (juce::Graphics& g)
{
    auto& th = Theme::get();
    const auto& info = moduleInfo (module().type);
    const auto c = th.pal (info.pal);
    auto r = getLocalBounds().toFloat();
    Glow::drawPanel (g, r, c, 12.0f, pulse);

    if (isEndpoint())
    {
        // Track in / out: title and a tall meter
        const bool in = module().type == ModuleType::Input;
        g.setColour (th.muted);
        Glow::drawSpacedText (g, in ? "TRACK" : "TRACK", r.withHeight (34.0f).translated (0.0f, 10.0f), th.label (8.0f), 1.5f,
                              juce::Justification::centred);
        g.setColour (c);
        Glow::drawSpacedText (g, in ? "IN" : "OUT", r.withHeight (24.0f).translated (0.0f, 34.0f), th.heading (15.0f), 2.0f,
                              juce::Justification::centred);
        drawMeter (g, juce::Rectangle<float> (r.getCentreX() - 4.0f, 76.0f, 8.0f, r.getHeight() - 92.0f), track.meter.load(), c);
        return;
    }

    auto head = r.reduced (10.0f, 8.0f).removeFromTop (headerHeight - 12.0f);
    Glow::drawIcon (g, iconFor (module().type), head.removeFromLeft (30.0f), c, pulse);
    head.removeFromLeft (8.0f);
    g.setColour (c.withAlpha (0.75f));
    g.setFont (th.heading (11.0f));
    g.drawText (romanNumeral (numeral), head.removeFromRight (30.0f).toNearestInt(), juce::Justification::centredRight);
    g.setColour (th.text);
    Glow::drawSpacedText (g, info.title.toUpperCase(), head, th.heading (12.5f), 1.6f, juce::Justification::centredLeft);

    // Module-specific displays
    switch (module().type)
    {
        case ModuleType::Sampler:
        {
            auto a = extraArea.toFloat();
            g.setColour (th.display);
            g.fillRoundedRectangle (a, 6.0f);
            g.setColour (dropHover ? c : th.line2);
            g.drawRoundedRectangle (a.reduced (0.5f), 6.0f, dropHover ? 1.5f : 1.0f);
            auto data = ctx.engine.resolveSample (module().sampleName);
            auto wave = a.reduced (4.0f, 14.0f);
            if (data != nullptr)
            {
                const auto& peaks = data->peaks;
                const float start = k0 (0), len = juce::jmax (0.01f, k0 (1));
                g.setColour (c.withAlpha (0.12f));
                g.fillRect (wave.getX() + wave.getWidth() * start, wave.getY(), wave.getWidth() * juce::jmin (len, 1.0f - start), wave.getHeight());
                juce::Path p;
                for (float x = 0.0f; x < wave.getWidth(); x += 1.0f)
                {
                    const float pk = peaks[(size_t) juce::jlimit (0, (int) peaks.size() - 1, (int) (x / wave.getWidth() * peaks.size()))];
                    const float h = juce::jmax (0.5f, pk * wave.getHeight() * 0.5f);
                    p.addRectangle (wave.getX() + x, wave.getCentreY() - h, 1.0f, h * 2.0f);
                }
                g.setColour (c.withAlpha (0.85f));
                g.fillPath (p);
            }
            g.setColour (th.muted);
            g.setFont (th.label (7.5f));
            g.drawText (dropHover ? "DROP FILE" : juce::File (module().sampleName).getFileNameWithoutExtension().toUpperCase(),
                        a.removeFromBottom (12.0f).toNearestInt(), juce::Justification::centred);
            g.drawText ("CLICK TO CHOOSE", a.removeFromTop (12.0f).toNearestInt(), juce::Justification::centred);
            break;
        }
        case ModuleType::Drums:
        {
            for (int p = 0; p < 16; ++p)
            {
                auto b = padBounds (p);
                const bool lit = p == litPad;
                g.setColour (lit ? c.withAlpha (0.35f) : th.display);
                g.fillRoundedRectangle (b, 3.0f);
                g.setColour (lit ? c : c.withAlpha (0.35f));
                g.drawRoundedRectangle (b.reduced (0.5f), 3.0f, 1.0f);
            }
            g.setColour (th.muted);
            g.setFont (th.label (7.0f));
            g.drawText (litPad >= 0 ? module().padNames[(size_t) litPad].isNotEmpty() ? module().padNames[(size_t) litPad].toUpperCase() : Samples::defaultPads()[(size_t) litPad].toUpperCase()
                                    : juce::String ("PADS 36 - 51"),
                        extraArea.withTrimmedTop (extraArea.getHeight() - 12), juce::Justification::centred);
            break;
        }
        case ModuleType::LFO:
        {
            // A dot riding the LFO output
            auto a = extraArea.toFloat();
            g.setColour (th.display);
            g.fillRoundedRectangle (a, 6.0f);
            g.setColour (th.line2);
            g.drawRoundedRectangle (a.reduced (0.5f), 6.0f, 1.0f);
            const float y = a.getCentreY() - shownDisplay * (a.getHeight() * 0.42f);
            g.setColour (c.withAlpha (0.25f));
            g.fillEllipse (a.getCentreX() - 7.0f, y - 7.0f, 14.0f, 14.0f);
            g.setColour (c);
            g.fillEllipse (a.getCentreX() - 3.5f, y - 3.5f, 7.0f, 7.0f);
            break;
        }
        case ModuleType::Compressor:
        {
            // Gain reduction meter, falling from the top
            auto a = extraArea.toFloat().reduced (8.0f, 0.0f);
            g.setColour (th.display);
            g.fillRoundedRectangle (a, 3.0f);
            const float gr = juce::jlimit (0.0f, 1.0f, shownDisplay / 20.0f);
            g.setColour (c.withAlpha (0.8f));
            g.fillRoundedRectangle (a.withHeight (a.getHeight() * gr), 3.0f);
            g.setColour (th.muted);
            g.setFont (th.label (7.0f));
            g.drawText ("GR", extraArea.withTrimmedTop (extraArea.getHeight() - 12).expanded (8, 0), juce::Justification::centred);
            break;
        }
        case ModuleType::Input: case ModuleType::Output: case ModuleType::Synth: case ModuleType::Filter:
        case ModuleType::Drive: case ModuleType::Delay: case ModuleType::Reverb: case ModuleType::EQ:
            break;
    }

    g.setColour (th.muted);
    for (int i = 0; i < sliders.size(); ++i)
    {
        auto b = sliders[i]->getBounds();
        Glow::drawSpacedText (g, info.knobs[(size_t) i].first.toUpperCase(),
                              juce::Rectangle<float> ((float) b.getX() - 14.0f, (float) b.getBottom() + 1.0f, (float) b.getWidth() + 28.0f, 13.0f),
                              th.label (7.5f), 1.1f, juce::Justification::centred);
    }
}

juce::Rectangle<float> ModuleCard::padBounds (int pad) const
{
    auto a = extraArea.withTrimmedBottom (12).toFloat();
    const float w = a.getWidth() / 4.0f, h = a.getHeight() / 4.0f;
    const int row = 3 - pad / 4, col = pad % 4;     // pad 0 bottom-left, like a drum machine
    return juce::Rectangle<float> (a.getX() + col * w, a.getY() + row * h, w, h).reduced (1.5f);
}

int ModuleCard::padAt (juce::Point<int> p) const
{
    if (module().type != ModuleType::Drums) return -1;
    for (int i = 0; i < 16; ++i)
        if (padBounds (i).contains (p.toFloat()))
            return i;
    return -1;
}

void ModuleCard::playPad (int pad)
{
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    ctx.engine.midiInput.addMessageToQueue (juce::MidiMessage::noteOn (1, 36 + pad, 0.9f).withTimeStamp (now));
    ctx.engine.midiInput.addMessageToQueue (juce::MidiMessage::noteOff (1, 36 + pad).withTimeStamp (now + 0.05));
    litPad = pad;
    repaint();
}

void ModuleCard::showSampleMenu (std::function<void (const juce::String&)> choose)
{
    juce::PopupMenu menu;
    juce::String lastCategory;
    int i = 1;
    for (auto& e : Samples::bank())
    {
        if (e.category != lastCategory) { menu.addSectionHeader (e.category); lastCategory = e.category; }
        menu.addItem (i++, e.name);
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [choose] (int r)
    {
        if (r > 0 && r <= (int) Samples::bank().size())
            choose (Samples::bank()[(size_t) (r - 1)].name);
    });
}

void ModuleCard::tick()
{
    if (auto* node = ctx.engine.liveNode (trackIndex, moduleId))
    {
        const float d = node->display.load();
        if (std::abs (d - shownDisplay) > 0.01f)
        {
            shownDisplay = d;
            if (hasExtra (module().type)) repaint (extraArea);
        }
    }
}

void ModuleCard::mouseUp (const juce::MouseEvent&)
{
    if (litPad >= 0) { litPad = -1; repaint (extraArea); }
}

bool ModuleCard::isInterestedInFileDrag (const juce::StringArray& files)
{
    if (module().type != ModuleType::Sampler && module().type != ModuleType::Drums) return false;
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg")) return true;
    return false;
}

void ModuleCard::filesDropped (const juce::StringArray& files, int x, int y)
{
    dropHover = false;
    if (auto* m = patch.find (moduleId))
    {
        if (m->type == ModuleType::Sampler)
            m->sampleName = files[0];
        else if (const int pad = padAt ({ x, y }); pad >= 0)
            m->padNames[(size_t) pad] = files[0];
        else
            return;
        if (onPatchChanged) onPatchChanged();
    }
    repaint();
}

void ModuleCard::mouseDown (const juce::MouseEvent& e)
{
    // Sampler waveform: click to pick a built-in sample. Drum pads: click plays, right-click picks a sample.
    if (module().type == ModuleType::Sampler && extraArea.contains (e.getPosition()))
    {
        showSampleMenu ([safe = juce::Component::SafePointer<ModuleCard> (this)] (const juce::String& name)
        {
            if (safe == nullptr) return;
            if (auto* m = safe->patch.find (safe->moduleId)) m->sampleName = name;
            if (safe->onPatchChanged) safe->onPatchChanged();
        });
        return;
    }
    if (const int pad = padAt (e.getPosition()); pad >= 0)
    {
        if (e.mods.isPopupMenu())
            showSampleMenu ([safe = juce::Component::SafePointer<ModuleCard> (this), pad] (const juce::String& name)
            {
                if (safe == nullptr) return;
                if (auto* m = safe->patch.find (safe->moduleId)) m->padNames[(size_t) pad] = name;
                if (safe->onPatchChanged) safe->onPatchChanged();
            });
        else
            playPad (pad);
        return;
    }

    if (e.mods.isPopupMenu() && ! isEndpoint())
    {
        juce::PopupMenu menu;
        menu.addItem (1, "Remove " + moduleInfo (module().type).title);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                            [safe = juce::Component::SafePointer<ModuleCard> (this)] (int result)
                            {
                                if (result == 1 && safe != nullptr && safe->onRemove)
                                    safe->onRemove();
                            });
        return;
    }
    dragStart = module().position;
    toFront (false);
}

void ModuleCard::mouseDrag (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() || litPad >= 0 || (module().type == ModuleType::Sampler && extraArea.contains (e.getMouseDownPosition())))
        return;
    if (auto* m = patch.find (moduleId))
    {
        m->position = { juce::jmax (0.0f, dragStart.x + (float) e.getDistanceFromDragStartX()),
                        juce::jlimit (0.0f, 60.0f, dragStart.y + (float) e.getDistanceFromDragStartY()) };
        setTopLeftPosition (m->position.toInt());
        if (onMoved)
            onMoved();
    }
}

//==============================================================================
CableLayer::CableLayer (AppContext& c, std::function<ModuleCard* (int)> f) : ctx (c), cardFor (std::move (f))
{
    setRepaintsOnMouseActivity (false);
}

juce::Point<float> CableLayer::jackPos (int module, int port) const
{
    if (auto* card = cardFor (module))
        return card->getPosition().toFloat() + card->jackPosition (port);
    return {};
}

juce::Colour CableLayer::colourFor (int module) const
{
    if (patch != nullptr)
        if (auto* m = patch->find (module))
            return Theme::get().pal (moduleInfo (m->type).pal);
    return Theme::get().pal (Pal::Green);
}

CableLayer::JackRef CableLayer::jackAt (juce::Point<float> p, float radius) const
{
    if (patch == nullptr)
        return {};
    JackRef best;
    float bestDist = radius;
    for (auto& m : patch->modules)
        if (auto* card = cardFor (m.id))
            for (int port : card->jacks())
            {
                const float d = jackPos (m.id, port).getDistanceFrom (p);
                if (d < bestDist)
                {
                    bestDist = d;
                    best = { m.id, port };
                }
            }
    return best;
}

juce::Path CableLayer::cablePath (juce::Point<float> a, juce::Point<float> b, bool intoKnob)
{
    // A hanging cable: leaves the jack sideways, sags a little, and arrives from below for knob inputs
    // Neighbours get a short slack loop; longer runs drape underneath the cards in between
    const float dx = std::abs (b.x - a.x);
    const float reach = juce::jmax (24.0f, dx * 0.4f);
    const float sag = dx < 90.0f ? juce::jmin (16.0f, dx * 0.3f) : (Patch::cardHeight - jackY + 18.0f) / 0.75f;
    juce::Path p;
    p.startNewSubPath (a);
    p.cubicTo (a.translated (reach, sag), intoKnob ? b.translated (0.0f, 56.0f) : b.translated (-reach, sag), b);
    return p;
}

juce::Path CableLayer::cablePath (const Cable& c) const
{
    return cablePath (jackPos (c.fromModule, c.fromPort), jackPos (c.toModule, c.toPort), Port::isModIn (c.toPort));
}

int CableLayer::cableAt (juce::Point<float> p) const
{
    if (patch == nullptr)
        return -1;
    for (int i = (int) patch->cables.size() - 1; i >= 0; --i)
    {
        juce::Point<float> nearest;
        cablePath (patch->cables[(size_t) i]).getNearestPoint (p, nearest);
        if (nearest.getDistanceFrom (p) < 5.0f)
            return i;
    }
    return -1;
}

bool CableLayer::canConnect (JackRef a, JackRef b) const
{
    if (! a.valid() || ! b.valid() || a.module == b.module)
        return false;
    if (Port::isInput (a.port) == Port::isInput (b.port))
        return false;
    return Port::isMod (a.port) == Port::isMod (b.port);
}

bool CableLayer::hitTest (int x, int y)
{
    const auto p = juce::Point<int> (x, y).toFloat();
    return dragging || jackAt (p).valid() || cableAt (p) >= 0;
}

void CableLayer::mouseMove (const juce::MouseEvent& e)
{
    const int c = jackAt (e.position).valid() ? -1 : cableAt (e.position);
    if (c != hoverCable)
    {
        hoverCable = c;
        repaint();
    }
    setMouseCursor (jackAt (e.position).valid() ? juce::MouseCursor::CrosshairCursor
                    : (c >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor));
}

void CableLayer::mouseDown (const juce::MouseEvent& e)
{
    if (patch == nullptr)
        return;

    const auto jack = jackAt (e.position);
    if (! jack.valid())
    {
        const int c = cableAt (e.position);
        if (c >= 0 && e.mods.isPopupMenu())
        {
            juce::PopupMenu menu;
            menu.addItem (1, "Remove cable");
            menu.showMenuAsync (juce::PopupMenu::Options(), [this, c] (int result)
            {
                if (result == 1 && patch != nullptr)
                {
                    patch->disconnect ((size_t) c);
                    hoverCable = -1;
                    if (onChanged) onChanged();
                }
            });
        }
        return;
    }

    dragging = true;
    dragEnd = e.position;

    if (Port::isInput (jack.port))
    {
        // Pulling a plugged-in input lifts its cable so it can be re-wired somewhere else
        const int existing = patch->cableInto (jack.module, jack.port);
        if (existing >= 0)
        {
            const auto c = patch->cables[(size_t) existing];
            patch->disconnect ((size_t) existing);
            dragFrom = { c.fromModule, c.fromPort };
            if (onChanged) onChanged();
            return;
        }
    }
    dragFrom = jack;
}

void CableLayer::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging)
        return;
    dragEnd = e.position;
    auto j = jackAt (e.position, 14.0f);
    hoverJack = canConnect (dragFrom, j) ? j : JackRef {};
    repaint();
}

void CableLayer::mouseUp (const juce::MouseEvent&)
{
    if (! dragging)
        return;
    dragging = false;
    if (patch != nullptr && hoverJack.valid())
    {
        const bool fromIsOutput = ! Port::isInput (dragFrom.port);
        const auto out = fromIsOutput ? dragFrom : hoverJack;
        const auto in  = fromIsOutput ? hoverJack : dragFrom;
        patch->connect (out.module, out.port, in.module, in.port);
    }
    dragFrom = hoverJack = {};
    if (onChanged) onChanged();
    repaint();
}

void CableLayer::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (patch == nullptr || jackAt (e.position).valid())
        return;
    const int c = cableAt (e.position);
    if (c >= 0)
    {
        patch->disconnect ((size_t) c);
        hoverCable = -1;
        if (onChanged) onChanged();
    }
}

void CableLayer::paint (juce::Graphics& g)
{
    if (patch == nullptr)
        return;

    auto& th = Theme::get();
    const bool playing = ctx.engine.isPlaying();
    const float level = ctx.selected().meter.load();
    const double phase = std::fmod (juce::Time::getMillisecondCounterHiRes() / 1400.0, 1.0);

    // Cables: glowing neon tubes; modulation cables are dashed
    for (size_t i = 0; i < patch->cables.size(); ++i)
    {
        const auto& c = patch->cables[i];
        const auto path = cablePath (c);
        const auto col = colourFor (c.fromModule);
        const bool hot = (int) i == hoverCable;

        if (c.isMod())
        {
            g.setColour (col.withAlpha (0.18f));
            g.strokePath (path, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            juce::Path dashed;
            const float pattern[] = { 5.0f, 4.0f };
            juce::PathStrokeType (1.6f).createDashedStroke (dashed, path, pattern, 2);
            g.setColour (hot ? col.brighter (0.4f) : col);
            g.fillPath (dashed);
        }
        else
        {
            Glow::strokeGlow (g, path, hot ? col.brighter (0.4f) : col, hot ? 2.2f : 1.6f, playing ? level : 0.0f);

            // Signal flowing along the cable while the track plays
            if (playing && level > 0.02f)
            {
                const float len = path.getLength();
                for (int d = 0; d < 3; ++d)
                {
                    const float t = (float) std::fmod (phase + d / 3.0, 1.0);
                    const auto p = path.getPointAlongPath (t * len);
                    g.setColour (col.brighter (0.6f).withAlpha (0.35f + 0.65f * level));
                    g.fillEllipse (p.x - 2.5f, p.y - 2.5f, 5.0f, 5.0f);
                }
            }
        }
    }

    // Cable being dragged
    if (dragging && dragFrom.valid())
    {
        const auto a = jackPos (dragFrom.module, dragFrom.port);
        const auto b = hoverJack.valid() ? jackPos (hoverJack.module, hoverJack.port) : dragEnd;
        const bool fromOut = ! Port::isInput (dragFrom.port);
        const bool intoKnob = (fromOut && hoverJack.valid() && Port::isModIn (hoverJack.port));
        const auto path = fromOut ? cablePath (a, b, intoKnob) : cablePath (b, a, Port::isModIn (dragFrom.port));
        Glow::strokeGlow (g, path, colourFor (dragFrom.module).withAlpha (0.9f), 1.6f, 1.0f);
    }

    // Jacks: rings, filled when patched, lit when a dragged cable can land on them
    for (auto& m : patch->modules)
    {
        auto* card = cardFor (m.id);
        if (card == nullptr)
            continue;
        const auto col = th.pal (moduleInfo (m.type).pal);
        for (int port : card->jacks())
        {
            const auto p = jackPos (m.id, port);
            const bool mod = Port::isMod (port);
            const float r = mod ? 4.0f : 6.0f;
            bool patched = false;
            for (auto& c : patch->cables)
                patched = patched || (c.fromModule == m.id && c.fromPort == port) || (c.toModule == m.id && c.toPort == port);
            const bool target = dragging && canConnect (dragFrom, { m.id, port });
            const bool lit = target && hoverJack.module == m.id && hoverJack.port == port;

            g.setColour (th.display);
            g.fillEllipse (p.x - r, p.y - r, r * 2.0f, r * 2.0f);
            if (lit)
            {
                g.setColour (col.withAlpha (0.35f));
                g.fillEllipse (p.x - r - 4.0f, p.y - r - 4.0f, (r + 4.0f) * 2.0f, (r + 4.0f) * 2.0f);
            }
            g.setColour (target ? col : (patched ? col : col.withAlpha (0.45f)));
            g.drawEllipse (p.x - r, p.y - r, r * 2.0f, r * 2.0f, target ? 1.6f : 1.1f);
            if (patched)
                g.fillEllipse (p.x - r * 0.45f, p.y - r * 0.45f, r * 0.9f, r * 0.9f);
        }
    }
}

//==============================================================================
void DevicePanel::Canvas::paint (juce::Graphics& g)
{
    // Faint dot grid, like marquee bulbs on a dark wall
    auto& th = Theme::get();
    g.fillAll (th.panel);
    g.setColour (th.line.withAlpha (0.55f));
    for (int y = 10; y < getHeight(); y += 18)
        for (int x = 10; x < getWidth(); x += 18)
            g.fillEllipse ((float) x - 0.9f, (float) y - 0.9f, 1.8f, 1.8f);
}

DevicePanel::DevicePanel (AppContext& c) : ctx (c)
{
    cables = std::make_unique<CableLayer> (ctx, [this] (int id) { return cardFor (id); });
    cables->onChanged = [this] { ctx.engine.rebuildTrack (ctx.selectedTrack); cables->repaint(); };

    addButton.getProperties().set ("pal", (int) Pal::Green);
    addButton.onClick = [this]
    {
        const std::pair<ModuleType, const char*> types[] = {
            { ModuleType::Sampler, "Sampler" }, { ModuleType::Synth, "Synth" }, { ModuleType::Drums, "Drum Rack" },
            { ModuleType::Filter, "Filter" }, { ModuleType::Delay, "Delay" }, { ModuleType::Reverb, "Reverb" },
            { ModuleType::EQ, "EQ" }, { ModuleType::Compressor, "Compressor" }, { ModuleType::Drive, "Drive" }, { ModuleType::LFO, "LFO" } };
        juce::PopupMenu menu;
        menu.addSectionHeader ("Add and auto-wire");
        for (int i = 0; i < 10; ++i)
            menu.addItem (i + 1, types[i].second);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addButton),
                            [this, types] (int r) { if (r > 0) addModule (types[r - 1].first); });
    };
    tidyButton.onClick = [this] { ctx.selected().patch.tidy(); rebuild(); };
    addButton.setTooltip ("Add a module; it wires itself into the chain");

    addAndMakeVisible (addButton);
    addAndMakeVisible (tidyButton);
    viewport.setViewedComponent (&canvas, false);
    viewport.setScrollBarsShown (false, true);
    viewport.setScrollBarThickness (6);
    addAndMakeVisible (viewport);
    canvas.addAndMakeVisible (*cables);

    ctx.addChangeListener (this);
    rebuild();
}

DevicePanel::~DevicePanel()
{
    ctx.removeChangeListener (this);
}

void DevicePanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    rebuild();
}

ModuleCard* DevicePanel::cardFor (int moduleId)
{
    for (auto* card : cards)
        if (card->getModuleId() == moduleId)
            return card;
    return nullptr;
}

void DevicePanel::rebuild()
{
    auto& track = ctx.selected();
    auto& patch = track.patch;
    cards.clear();

    // Numerals follow left-to-right order on the canvas
    std::vector<const Module*> byPosition;
    for (auto& m : patch.modules)
        byPosition.push_back (&m);
    std::sort (byPosition.begin(), byPosition.end(), [] (auto* a, auto* b) { return a->position.x < b->position.x; });
    auto numeralFor = [&byPosition] (int id)
    {
        int n = 0;
        for (auto* m : byPosition)
        {
            if (m->type != ModuleType::Input && m->type != ModuleType::Output)
                ++n;
            if (m->id == id)
                return n;
        }
        return 0;
    };

    for (auto& m : patch.modules)
    {
        auto* card = cards.add (new ModuleCard (ctx, patch, track, ctx.selectedTrack, m.id, numeralFor (m.id)));
        card->onPatchChanged = [this]
        {
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<DevicePanel> (this)]
            {
                if (safe != nullptr) { safe->ctx.engine.rebuildTrack (safe->ctx.selectedTrack); safe->rebuild(); }
            });
        };
        card->setTopLeftPosition (m.position.toInt());
        card->onMoved = [this] { updateCanvasSize(); cables->repaint(); };
        card->onRemove = [this, id = m.id]
        {
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<DevicePanel> (this), id]
            {
                if (safe != nullptr)
                {
                    safe->ctx.selected().patch.remove (id);
                    safe->ctx.engine.rebuildTrack (safe->ctx.selectedTrack);
                    safe->rebuild();
                }
            });
        };
        canvas.addAndMakeVisible (card);
    }

    cables->setPatch (&patch);
    cables->toFront (false);
    updateCanvasSize();
    repaint();
}

void DevicePanel::addModule (ModuleType type)
{
    auto& patch = ctx.selected().patch;
    const int id = patch.add (type);
    ctx.engine.rebuildTrack (ctx.selectedTrack);
    rebuild();
    if (auto* card = cardFor (id))
        viewport.setViewPosition (juce::jmax (0, card->getRight() + 120 - viewport.getWidth()), 0);
}

void DevicePanel::loadSample (const juce::String& nameOrPath)
{
    auto& patch = ctx.selected().patch;
    Module* sampler = nullptr;
    for (auto& m : patch.modules)
        if (m.type == ModuleType::Sampler) { sampler = &m; break; }
    if (sampler == nullptr)
        sampler = patch.find (patch.add (ModuleType::Sampler));
    sampler->sampleName = nameOrPath;
    ctx.engine.rebuildTrack (ctx.selectedTrack);
    rebuild();
}

void DevicePanel::updateCanvasSize()
{
    int right = viewport.getWidth();
    for (auto* card : cards)
        right = juce::jmax (right, card->getRight() + 24);
    canvas.setSize (right, juce::jmax (1, viewport.getHeight() - (right > viewport.getWidth() ? 6 : 0)));
    cables->setBounds (canvas.getLocalBounds());
}

void DevicePanel::resized()
{
    auto r = getLocalBounds().reduced (12, 0);
    auto head = r.removeFromTop (36).reduced (0, 6);
    tidyButton.setBounds (head.removeFromRight (70));
    head.removeFromRight (8);
    addButton.setBounds (head.removeFromRight (130));
    viewport.setBounds (r.withTrimmedBottom (8));
    updateCanvasSize();
}

void DevicePanel::tick()
{
    const float m = ctx.selected().meter.load();
    for (auto* card : cards)
    {
        card->tick();
        if (std::abs (card->pulse - m) > 0.02f)
        {
            card->pulse = m;
            card->repaint();
        }
    }
    if (ctx.engine.isPlaying() || m > 0.0f)
        cables->repaint();
}

void DevicePanel::paint (juce::Graphics& g)
{
    auto& th = Theme::get();
    g.fillAll (th.panel);
    g.setColour (th.line2);
    g.drawHorizontalLine (0, 0.0f, (float) getWidth());

    auto head = getLocalBounds().reduced (12, 0).removeFromTop (36).toFloat();
    auto& t = ctx.selected();
    g.setColour (th.muted);
    Glow::drawSpacedText (g, "DEVICES", head, th.label (9.0f), 2.0f, juce::Justification::centredLeft);
    g.setColour (th.pal (t.colourIndex));
    Glow::drawSpacedText (g, t.name.toUpperCase(), head.withTrimmedLeft (84.0f), th.heading (13.0f), 2.0f,
                          juce::Justification::centredLeft);
    g.setColour (th.muted.withAlpha (0.7f));
    g.setFont (th.body (11.0f));
    g.drawText ("Drag between jacks to patch.  Pull a plugged jack to re-wire.  Double-click a cable to remove it.",
                head.withTrimmedLeft (260.0f).withTrimmedRight (230.0f).toNearestInt(), juce::Justification::centredRight);
}
