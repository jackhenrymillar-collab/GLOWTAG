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
            case ModuleType::Plugin:     return Icon::Plugin;
            case ModuleType::Input:      return Icon::Play;
            case ModuleType::Output:     return Icon::Velocity;
            case ModuleType::Attenuator: case ModuleType::Mixer: case ModuleType::Inverter:
            case ModuleType::Crossfade:  case ModuleType::Slew:
                break;                   // utilities draw their own small glyph
        }
        return Icon::Filter;
    }

    constexpr float headerHeight = 44.0f;
    constexpr float jackY = 60.0f;
    constexpr float notesJackDrop = 26.0f;      // a second jack on the same edge sits this far below the first

    bool hasExtra (ModuleType t) { return t == ModuleType::Sampler || t == ModuleType::Drums || t == ModuleType::LFO || t == ModuleType::Compressor || t == ModuleType::Plugin; }
    int extraWidth (ModuleType t) { return t == ModuleType::Sampler ? 96 : (t == ModuleType::Drums ? 104 : (t == ModuleType::Plugin ? 110 : 26)); }
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
        s->setTooltip (info.utility && module().type == ModuleType::Mixer ? "Level of input " + name : name);
        s->onValueChange = [this, k, s]
        {
            const float v = (float) s->getValue();
            if (auto* m = patch.find (moduleId))
                m->knobValues[k] = v;
            ctx.engine.setKnob (trackIndex, moduleId, (int) k, v);   // straight to the running patch
            ctx.touched();
        };
        addAndMakeVisible (s);
    }
    setSize ((int) info.width, (int) Patch::heightOf (module().type));
}

// Knob value as stored in the patch (for drawing the sampler window)
#define k0(i) ((float) sliders[(i)]->getValue())

bool ModuleCard::isEndpoint() const
{
    return module().type == ModuleType::Input || module().type == ModuleType::Output;
}

bool ModuleCard::isUtility() const
{
    return moduleInfo (module().type).utility;
}

std::vector<int> ModuleCard::jacks() const
{
    return moduleInfo (module().type).ports();
}

juce::Point<float> ModuleCard::jackPosition (int port) const
{
    const float w = (float) getWidth(), h = (float) getHeight();
    const auto& info = moduleInfo (module().type);
    if (info.utility)
    {
        // Signal inputs down the left edge, the output half way down the right
        if (port == Port::sigOut)
            return { w, h * 0.5f };
        if (Port::isSigIn (port))
        {
            const float spacing = info.sigIns > 2 ? 15.0f : 22.0f;
            return { 0.0f, h * 0.5f + ((float) (port - Port::sigInBase) - (float) (info.sigIns - 1) * 0.5f) * spacing };
        }
    }
    if (port == Port::audioIn)  return { 0.0f, jackY };
    if (port == Port::notesIn)  return { 0.0f, info.hasAudioIn ? jackY + notesJackDrop : jackY };
    if (port == Port::audioOut) return { w, jackY };
    if (port == Port::notesOut) return { w, info.hasAudioOut ? jackY + notesJackDrop : jackY };
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

    if (isUtility())
    {
        // One row of small knobs under the title
        auto u = getLocalBounds().reduced (14, 0).withTrimmedTop (24).withTrimmedBottom (18);
        const int cell = u.getWidth() / n;
        for (int i = 0; i < n; ++i)
            sliders[i]->setBounds (juce::Rectangle<int> (u.getX() + i * cell, u.getY(), cell, u.getHeight()).withSizeKeepingCentre (32, 32));
        return;
    }

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

// A utility: half height, a small title, what it is working on, and a glyph or a row of small knobs
void ModuleCard::paintUtility (juce::Graphics& g)
{
    auto& th = Theme::get();
    const auto& info = moduleInfo (module().type);
    const auto c = th.pal (info.pal);
    auto r = getLocalBounds().toFloat();
    Glow::drawPanel (g, r, c, 8.0f, pulse);

    g.setColour (th.text);
    Glow::drawSpacedText (g, info.title.toUpperCase(), juce::Rectangle<float> (11.0f, 6.0f, r.getWidth() - 22.0f, 14.0f), th.heading (9.5f), 1.3f,
                          juce::Justification::centredLeft);

    // What it has settled on, with a lamp that follows the signal going through
    const auto kind = patch.kindOf (moduleId, Port::sigOut);
    const float lamp = juce::jlimit (0.0f, 1.0f, shownDisplay * 1.6f);
    g.setColour (c.withAlpha (0.25f + 0.75f * lamp));
    g.fillEllipse (r.getRight() - 15.0f, 10.0f, 5.0f, 5.0f);
    g.setColour (th.muted);
    g.setFont (th.label (6.5f));
    g.drawText (kind == Signal::Audio ? "SOUND" : (kind == Signal::Mod ? "MOD" : "FREE"),
                juce::Rectangle<float> (11.0f, r.getBottom() - 15.0f, r.getWidth() - 22.0f, 10.0f).toNearestInt(),
                sliders.isEmpty() ? juce::Justification::centred : juce::Justification::centredRight);

    if (module().type == ModuleType::Inverter)
    {
        // A wave and the same wave turned over
        const float cx = r.getCentreX(), cy = r.getCentreY() + 3.0f;
        juce::Path up, down;
        up.startNewSubPath (cx - 20.0f, cy);
        up.quadraticTo (cx - 10.0f, cy - 22.0f, cx, cy);
        up.quadraticTo (cx + 10.0f, cy + 22.0f, cx + 20.0f, cy);
        down.startNewSubPath (cx - 20.0f, cy);
        down.quadraticTo (cx - 10.0f, cy + 22.0f, cx, cy);
        down.quadraticTo (cx + 10.0f, cy - 22.0f, cx + 20.0f, cy);
        g.setColour (c.withAlpha (0.3f));
        g.strokePath (up, juce::PathStrokeType (1.2f));
        Glow::strokeGlow (g, down, c, 1.5f, pulse);
    }

    g.setColour (th.muted);
    for (int i = 0; i < sliders.size(); ++i)
    {
        auto b = sliders[i]->getBounds();
        const auto name = module().type == ModuleType::Mixer ? "IN " + info.knobs[(size_t) i].first : info.knobs[(size_t) i].first.toUpperCase();
        Glow::drawSpacedText (g, name, juce::Rectangle<float> ((float) b.getX() - 10.0f, (float) b.getBottom(), (float) b.getWidth() + 20.0f, 11.0f),
                              th.label (6.5f), 1.0f, sliders.size() == 1 ? juce::Justification::centredLeft : juce::Justification::centred);
    }
}

void ModuleCard::paint (juce::Graphics& g)
{
    if (isUtility())
    {
        paintUtility (g);
        return;
    }

    auto& th = Theme::get();
    const auto& info = moduleInfo (module().type);
    const auto c = th.pal (info.pal);
    auto r = getLocalBounds().toFloat();
    Glow::drawPanel (g, r, c, 12.0f, pulse);
    if (flash > 0.01f)
    {
        g.setColour (c.withAlpha (0.10f * flash));
        g.fillRoundedRectangle (r, 12.0f);
        g.setColour (c.withAlpha (juce::jmin (1.0f, flash)));
        g.drawRoundedRectangle (r.reduced (1.0f), 12.0f, 2.0f);
    }

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
        if (in)
        {
            // Two outputs: the track's own sound, and its notes
            g.setColour (th.muted);
            g.setFont (th.label (6.5f));
            g.drawText ("SOUND", juce::Rectangle<float> (0.0f, jackY - 6.0f, r.getWidth() - 10.0f, 12.0f).toNearestInt(), juce::Justification::centredRight);
            g.drawText ("NOTES", juce::Rectangle<float> (0.0f, jackY + notesJackDrop - 6.0f, r.getWidth() - 10.0f, 12.0f).toNearestInt(), juce::Justification::centredRight);
        }
        const float meterTop = in ? 104.0f : 76.0f;
        drawMeter (g, juce::Rectangle<float> (r.getCentreX() - 4.0f, meterTop, 8.0f, r.getHeight() - meterTop - 16.0f), track.meter.load(), c);
        return;
    }

    auto head = r.reduced (10.0f, 8.0f).removeFromTop (headerHeight - 12.0f);
    Glow::drawIcon (g, iconFor (module().type), head.removeFromLeft (30.0f), c, pulse);
    head.removeFromLeft (8.0f);
    g.setColour (c.withAlpha (0.75f));
    g.setFont (th.heading (11.0f));
    g.drawText (romanNumeral (numeral), head.removeFromRight (30.0f).toNearestInt(), juce::Justification::centredRight);
    g.setColour (th.text);
    if (module().type == ModuleType::Plugin)
    {
        g.setFont (th.heading (12.5f));
        g.drawFittedText (module().pluginName.isNotEmpty() ? module().pluginName : info.title, head.toNearestInt(), juce::Justification::centredLeft, 1);
    }
    else
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
        case ModuleType::Plugin:
        {
            // Plugin plate: format, state and an invitation to open its window
            auto a = extraArea.toFloat();
            auto* node = ctx.engine.liveNode (trackIndex, moduleId);
            const bool loaded = node != nullptr && node->hostedPlugin() != nullptr;
            g.setColour (th.display);
            g.fillRoundedRectangle (a, 6.0f);
            g.setColour (loaded ? c.withAlpha (0.5f) : th.pal ((int) Pal::Amber).withAlpha (0.6f));
            g.drawRoundedRectangle (a.reduced (0.5f), 6.0f, 1.0f);
            auto inner = a.reduced (6.0f, 8.0f);
            g.setColour (loaded ? c : th.pal ((int) Pal::Amber));
            auto* instance = loaded ? dynamic_cast<juce::AudioPluginInstance*> (node->hostedPlugin()) : nullptr;
            Glow::drawSpacedText (g, instance != nullptr ? instance->getPluginDescription().pluginFormatName.toUpperCase()
                                                         : juce::String (loaded ? "PLUGIN" : "NOT FOUND"),
                                  inner.removeFromTop (16.0f), th.heading (11.0f), 1.5f, juce::Justification::centred);
            g.setColour (th.muted);
            g.setFont (th.label (7.0f));
            g.drawFittedText (loaded ? "DOUBLE-CLICK\nTO OPEN" : "SCAN PLUGINS\nIN THE BROWSER", inner.toNearestInt(), juce::Justification::centred, 2);
            // Output level
            auto m = a.reduced (8.0f, 0.0f).removeFromBottom (5.0f).translated (0.0f, -6.0f);
            g.setColour (th.line2);
            g.fillRoundedRectangle (m, 2.0f);
            g.setColour (c.withAlpha (0.85f));
            g.fillRoundedRectangle (m.withWidth (m.getWidth() * juce::jlimit (0.0f, 1.0f, shownDisplay)), 2.0f);
            break;
        }
        case ModuleType::Input: case ModuleType::Output: case ModuleType::Synth: case ModuleType::Filter:
        case ModuleType::Drive: case ModuleType::Delay: case ModuleType::Reverb: case ModuleType::EQ:
        case ModuleType::Attenuator: case ModuleType::Mixer: case ModuleType::Inverter:
        case ModuleType::Crossfade: case ModuleType::Slew:
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
            else if (isUtility()) repaint (getWidth() - 20, 6, 14, 14);      // its lamp
        }
    }
}

void ModuleCard::mouseUp (const juce::MouseEvent&)
{
    if (litPad >= 0) { litPad = -1; repaint (extraArea); }
}

void ModuleCard::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) return;
    if (module().type == ModuleType::Plugin && onOpenPlugin)
        onOpenPlugin();
    // Empty space on a built-in device opens its expanded view (the pads and the waveform keep their clicks)
    else if (! isEndpoint() && ! isUtility() && onOpenExpanded
             && ! ((module().type == ModuleType::Drums || module().type == ModuleType::Sampler) && extraArea.contains (e.getPosition())))
        onOpenExpanded();
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
        if (module().type == ModuleType::Plugin)
            menu.addItem (2, "Open " + module().pluginName);
        menu.addItem (1, "Remove " + (module().type == ModuleType::Plugin ? module().pluginName : moduleInfo (module().type).title));
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                            [safe = juce::Component::SafePointer<ModuleCard> (this)] (int result)
                            {
                                if (result == 1 && safe != nullptr && safe->onRemove)
                                    safe->onRemove();
                                else if (result == 2 && safe != nullptr && safe->onOpenPlugin)
                                    safe->onOpenPlugin();
                            });
        return;
    }
    dragStart = module().position;
    if (onRaise) onRaise();
}

void ModuleCard::mouseDrag (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() || litPad >= 0 || (module().type == ModuleType::Sampler && extraArea.contains (e.getMouseDownPosition())))
        return;
    if (auto* m = patch.find (moduleId))
    {
        // Cards slide along their row
        m->position = { juce::jmax (0.0f, dragStart.x + (float) e.getDistanceFromDragStartX()), Patch::rowY (m->type) };
        setTopLeftPosition (juce::roundToInt (m->position.x), getY());
        if (onMoved)
            onMoved();
    }
}

//==============================================================================
PluginWindow::PluginWindow (const juce::String& title, std::shared_ptr<Graph> g, juce::AudioProcessor& processor, int s, int m)
    : juce::DocumentWindow (title, Theme::get().panel, juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton),
      slot (s), moduleId (m), graph (std::move (g))
{
    setUsingNativeTitleBar (true);
    juce::AudioProcessorEditor* editor = processor.hasEditor() ? processor.createEditorIfNeeded() : nullptr;
    if (editor == nullptr)
        editor = new juce::GenericAudioProcessorEditor (processor);
    setContentOwned (editor, true);
    setResizable (editor->isResizable(), false);
    centreWithSize (getWidth(), getHeight());
    setVisible (true);
    toFront (true);
}

PluginWindow::~PluginWindow()
{
    // The editor must go before the processor it edits: release it while the graph is still alive
    clearContentComponent();
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

juce::Path CableLayer::cablePath (juce::Point<float> a, juce::Point<float> b, bool intoKnob) const
{
    // A hanging cable: leaves the jack sideways, sags a little, and arrives from below for knob inputs.
    // Neighbours get a short slack loop; longer runs drape underneath the cards in between. A cable
    // between the device row and the utility row just curves across the gap.
    const float split = rowOffset + Patch::utilityTop - 6.0f;
    const bool lowA = a.y > split, lowB = b.y > split;
    const float dx = std::abs (b.x - a.x);
    const float reach = juce::jmax (24.0f, dx * 0.4f);
    if (lowA != lowB)
    {
        // Between the rows: out sideways, along the gap between the two rows, then into the jack.
        // Each cable takes its own lane in the gap so two of them do not lie on top of each other.
        const float lane = std::fmod (a.x * 0.37f + b.x * 0.11f, 10.0f) - 5.0f;
        const float gapY = rowOffset + Patch::top + Patch::cardHeight + 11.0f + lane;
        const float outX = a.x + 14.0f;
        juce::Path line;
        line.startNewSubPath (a);
        line.lineTo (outX, a.y);
        line.lineTo (outX, gapY);
        if (intoKnob)
            line.lineTo (b.x, gapY);
        else
        {
            const float inX = juce::jmax (3.0f, b.x - 14.0f);
            line.lineTo (inX, gapY);
            line.lineTo (inX, b.y);
        }
        line.lineTo (b);
        return line.createPathWithRoundedCorners (11.0f);
    }
    float sag;
    if (dx < 90.0f)          sag = juce::jmin (16.0f, dx * 0.3f);
    else if (lowA)           sag = (Patch::utilityHeight * 0.5f + 12.0f) / 0.75f;
    else                     sag = (Patch::cardHeight - jackY + 18.0f) / 0.75f;
    juce::Path p;
    p.startNewSubPath (a);
    p.cubicTo (a.translated (reach, sag), intoKnob ? b.translated (0.0f, lowB ? 30.0f : 56.0f) : b.translated (-reach, sag), b);
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
    if (patch == nullptr || ! a.valid() || ! b.valid())
        return false;
    // Either end may be the one being dragged: the patch wants output first
    return Port::isInput (a.port) ? patch->canConnect (b.module, b.port, a.module, a.port)
                                  : patch->canConnect (a.module, a.port, b.module, b.port);
}

namespace
{
    const char* kindName (Signal k)
    {
        switch (k)
        {
            case Signal::Audio: return "sound";
            case Signal::Notes: return "notes";
            case Signal::Mod:   return "modulation";
            case Signal::Any:   break;
        }
        return "sound or modulation";
    }
}

juce::String CableLayer::describe (JackRef j) const
{
    if (patch == nullptr || ! j.valid())
        return {};
    const auto* m = patch->find (j.module);
    if (m == nullptr)
        return {};
    const auto& info = moduleInfo (m->type);
    const auto title = m->type == ModuleType::Plugin && m->pluginName.isNotEmpty() ? m->pluginName : info.title;
    const auto kind = patch->kindOf (j.module, j.port);

    if (j.port == Port::notesOut) return "Track notes out: pitch, velocity and gate from clips, the keyboard and the rack. Patch it to an instrument.";
    if (j.port == Port::notesIn)  return title + " notes in: takes notes from the track input only.";
    if (Port::isModIn (j.port))   return title + " " + info.knobs[(size_t) (j.port - Port::modInBase)].first + ": takes modulation (an LFO, or a utility working on one).";
    if (j.port == Port::modOut)   return title + " out: modulation, for any knob.";
    if (Port::isSigIn (j.port) || j.port == Port::sigOut)
        return title + (j.port == Port::sigOut ? " out: " : " in: ")
               + (kind == Signal::Any ? juce::String ("takes sound or modulation, and works on whichever it is given.")
                                      : "working on " + juce::String (kindName (kind)) + ".");
    return title + (Port::isInput (j.port) ? " in: " : " out: ") + kindName (kind) + ".";
}

juce::String CableLayer::whyNot (JackRef from, JackRef to) const
{
    if (patch == nullptr || ! from.valid() || ! to.valid() || canConnect (from, to))
        return {};
    if (from.module == to.module)
        return "A module can't be patched to itself.";
    if (Port::isInput (from.port) == Port::isInput (to.port))
        return Port::isInput (to.port) ? "Two inputs: a cable runs from an output to an input." : "Two outputs: a cable runs from an output to an input.";
    const auto a = patch->kindOf (from.module, from.port), b = patch->kindOf (to.module, to.port);
    const auto out = Port::isInput (from.port) ? b : a, in = Port::isInput (from.port) ? a : b;
    if (in == Signal::Notes)
        return "An instrument only takes notes, from the track's notes output.";
    if (out == Signal::Notes)
        return "Notes only go to an instrument's notes input.";
    return juce::String ("That carries ") + kindName (out) + ", and this jack takes " + kindName (in) + ".";
}

void CableLayer::hint (const juce::String& text)
{
    if (text == lastHint)
        return;
    lastHint = text;
    if (onHint) onHint (text);
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
    const auto jack = jackAt (e.position);
    setMouseCursor (jack.valid() ? juce::MouseCursor::CrosshairCursor
                    : (c >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor));
    hint (describe (jack));
}

void CableLayer::mouseExit (const juce::MouseEvent&)
{
    hint ({});
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
    hint (hoverJack.valid() ? describe (j) : whyNot (dragFrom, j));
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
    hint ({});
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
        const auto kind = patch->kindOf (c);

        if (kind == Signal::Notes)
        {
            // Notes: a string of bulbs
            g.setColour (col.withAlpha (0.16f));
            g.strokePath (path, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            const float len = path.getLength();
            const int bulbs = juce::jmax (2, juce::roundToInt (len / 7.0f));
            const auto lamp = hot ? col.brighter (0.4f) : col;
            const int running = playing && level > 0.02f ? (int) (phase * bulbs) : -1;
            for (int b = 0; b <= bulbs; ++b)
            {
                const auto p = path.getPointAlongPath (len * (float) b / (float) bulbs);
                const bool lit = running >= 0 && ((b - running) % 6 + 6) % 6 == 0;
                g.setColour (lit ? lamp.brighter (0.7f) : lamp.withAlpha (0.85f));
                const float rad = lit ? 2.1f : 1.5f;
                g.fillEllipse (p.x - rad, p.y - rad, rad * 2.0f, rad * 2.0f);
            }
        }
        else if (kind == Signal::Mod)
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
            const auto kind = patch->kindOf (m.id, port);
            // Sound: a large ring. Modulation: a small one. Notes: a square. A free utility jack: in between.
            const bool square = kind == Signal::Notes;
            const float r = kind == Signal::Mod ? 4.0f : (kind == Signal::Any ? 5.0f : (square ? 5.5f : 6.0f));
            const float corner = square ? 1.8f : r;
            bool patched = false;
            for (auto& c : patch->cables)
                patched = patched || (c.fromModule == m.id && c.fromPort == port) || (c.toModule == m.id && c.toPort == port);
            const bool source = dragging && dragFrom.module == m.id && dragFrom.port == port;
            const bool target = dragging && canConnect (dragFrom, { m.id, port });
            const bool lit = target && hoverJack.module == m.id && hoverJack.port == port;
            // While a cable is in the hand, jacks it can't go to fade back
            const float fade = dragging && ! target && ! source ? 0.28f : 1.0f;
            const auto box = juce::Rectangle<float> (p.x - r, p.y - r, r * 2.0f, r * 2.0f);

            g.setColour (th.display);
            g.fillRoundedRectangle (box, corner);
            if (lit)
            {
                g.setColour (col.withAlpha (0.35f));
                g.fillRoundedRectangle (box.expanded (4.0f), corner + 4.0f);
            }
            g.setColour ((target ? col : (patched ? col : col.withAlpha (0.45f))).withMultipliedAlpha (fade));
            g.drawRoundedRectangle (box, corner, target ? 1.6f : 1.1f);
            if (patched)
                g.fillRoundedRectangle (box.reduced (r * 0.55f), square ? 0.8f : r);
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
    cables->onChanged = [this]
    {
        ctx.engine.rebuildTrack (ctx.selectedTrack);
        ctx.touched();
        cables->repaint();
        for (auto* card : cards) card->repaint();       // a utility shows what it has settled on
    };
    cables->onHint = [this] (const juce::String& text) { hintText = text; repaint (0, 0, getWidth(), 36); };

    addButton.getProperties().set ("pal", (int) Pal::Green);
    addButton.onClick = [this]
    {
        struct Entry { ModuleType type; const char* name; };
        static const Entry devices[] = {
            { ModuleType::Sampler, "Sampler" }, { ModuleType::Synth, "Synth" }, { ModuleType::Drums, "Drum Rack" },
            { ModuleType::Filter, "Filter" }, { ModuleType::Delay, "Delay" }, { ModuleType::Reverb, "Reverb" },
            { ModuleType::EQ, "EQ" }, { ModuleType::Compressor, "Compressor" }, { ModuleType::Drive, "Drive" }, { ModuleType::LFO, "LFO" } };
        static const Entry utilities[] = {
            { ModuleType::Attenuator, "Attenuator  (turn a signal down)" }, { ModuleType::Mixer, "Mixer  (four inputs summed)" },
            { ModuleType::Inverter, "Inverter  (flip a signal over)" }, { ModuleType::Crossfade, "Crossfade  (blend A into B)" },
            { ModuleType::Slew, "Slew  (smooth a signal)" } };
        juce::PopupMenu menu;
        menu.addSectionHeader ("Devices: added and auto-wired");
        for (auto& e : devices)
            menu.addItem (1 + (int) e.type, e.name);
        menu.addSectionHeader ("Utilities: patch by hand, for sound or modulation");
        for (auto& e : utilities)
            menu.addItem (1 + (int) e.type, e.name);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addButton),
                            [this] (int r) { if (r > 0) addModule ((ModuleType) (r - 1)); });
    };
    tidyButton.onClick = [this] { ctx.selected().patch.tidy(); rebuild(); };
    addButton.setTooltip ("Add a device (it wires itself into the chain) or a utility for the lower row");
    fullButton.setTooltip ("Fill the window with the patch. Double-clicking empty space does the same; Esc closes it");
    fullButton.onClick = [this] { setFullScreen (! fullScreen); };

    addAndMakeVisible (addButton);
    addAndMakeVisible (tidyButton);
    addAndMakeVisible (fullButton);
    holder.addAndMakeVisible (canvas);
    viewport.setViewedComponent (&holder, false);
    viewport.setScrollBarsShown (true, true);
    viewport.setScrollBarThickness (6);
    addAndMakeVisible (viewport);
    canvas.addAndMakeVisible (*cables);
    canvas.onDoubleClick = [this] { setFullScreen (! fullScreen); };

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
    // The mixer asked for a device: bring it into view and light it up
    if (ctx.showPending && ctx.shownSlot == ctx.selectedTrack)
    {
        ctx.showPending = false;
        if (auto* card = cardFor (ctx.shownModule))
        {
            viewport.setViewPosition (juce::jlimit (0, juce::jmax (0, holder.getWidth() - viewport.getWidth()),
                                                    juce::roundToInt ((float) card->getBounds().getCentreX() * zoom) - viewport.getWidth() / 2), 0);
            card->flash = 1.6f;
        }
    }
    else if (auto* card = ctx.shownSlot == ctx.selectedTrack ? cardFor (ctx.shownModule) : nullptr)
        card->flash = 0.45f;      // stays gently marked while it is the shown device
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
    // Patch edits replace the running graph, so windows into the old one close
    for (int i = pluginWindows.size(); --i >= 0;)
        if (pluginWindows[i]->slot == ctx.selectedTrack || ctx.engine.uiGraph (pluginWindows[i]->slot) == nullptr)
            pluginWindows.remove (i);

    // Numerals follow left-to-right order along the device row
    std::vector<const Module*> byPosition;
    for (auto& m : patch.modules)
        if (! moduleInfo (m.type).utility)
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
        card->setTopLeftPosition (juce::roundToInt (m.position.x), juce::roundToInt (Patch::rowY (m.type) + rowOffset));
        // A clicked card comes forward among the cards, but the cables and jacks stay on top of it
        card->onRaise = [this, card] { card->toFront (false); cables->toFront (false); };
        card->onOpenPlugin = [this, id = m.id] { openPluginEditor (ctx.selectedTrack, id); };
        card->onOpenExpanded = [this, id = m.id] { if (onOpenExpanded) onOpenExpanded (ctx.selectedTrack, id); };
        card->onMoved = [this] { updateCanvasSize(); cables->repaint(); ctx.touched(); };
        card->onRemove = [this, id = m.id]
        {
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<DevicePanel> (this), id]
            {
                if (safe != nullptr)
                {
                    safe->ctx.selected().patch.remove (id);
                    safe->ctx.engine.rebuildTrack (safe->ctx.selectedTrack);
                    safe->ctx.touched();
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

// After adding an instrument: if the track has audio clips, they keep sounding beside it
void DevicePanel::keepTrackSound()
{
    for (auto& c : ctx.selected().clips)
        if (c.kind == ClipKind::Audio)
        {
            ctx.selected().patch.patchTrackSound();
            return;
        }
}

void DevicePanel::addModule (ModuleType type)
{
    auto& patch = ctx.selected().patch;
    const int id = patch.add (type);
    keepTrackSound();
    ctx.engine.rebuildTrack (ctx.selectedTrack);
    ctx.touched();
    rebuild();
    if (auto* card = cardFor (id))
        viewport.setViewPosition (juce::jmax (0, juce::roundToInt ((float) (card->getRight() + 120) * zoom) - viewport.getWidth()), 0);
}

void DevicePanel::addPlugin (const juce::PluginDescription& desc)
{
    auto& patch = ctx.selected().patch;
    const int id = patch.add (ModuleType::Plugin, desc.isInstrument);
    if (auto* m = patch.find (id))
    {
        m->pluginId = desc.createIdentifierString();
        m->pluginName = desc.name;
    }
    keepTrackSound();
    ctx.engine.rebuildTrack (ctx.selectedTrack);
    ctx.touched();
    rebuild();
    if (auto* card = cardFor (id))
        viewport.setViewPosition (juce::jmax (0, juce::roundToInt ((float) (card->getRight() + 120) * zoom) - viewport.getWidth()), 0);
    // Rebuilt graphs are picked up by the audio thread at the next block; open the editor once it is live
    juce::Timer::callAfterDelay (120, [safe = juce::Component::SafePointer<DevicePanel> (this), slot = ctx.selectedTrack, id]
    {
        if (safe != nullptr) safe->openPluginEditor (slot, id);
    });
}

void DevicePanel::openPluginEditor (int slot, int moduleId)
{
    for (auto* w : pluginWindows)
        if (w->slot == slot && w->moduleId == moduleId) { w->toFront (true); return; }

    auto graph = ctx.engine.uiGraph (slot);
    if (graph == nullptr) return;
    auto* node = graph->find (moduleId);
    if (node == nullptr || node->hostedPlugin() == nullptr) return;
    auto* m = ctx.project.slot (slot).patch.find (moduleId);
    const juce::String title = (m != nullptr && m->pluginName.isNotEmpty() ? m->pluginName : juce::String ("Plugin"))
                             + "  \u2014  " + ctx.project.slot (slot).name;
    auto* w = pluginWindows.add (new PluginWindow (juce::String::fromUTF8 (title.toRawUTF8()), graph, *node->hostedPlugin(), slot, moduleId));
    w->onClose = [this, w]
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<DevicePanel> (this), w]
        {
            if (safe != nullptr) safe->pluginWindows.removeObject (w);
        });
    };
}

void DevicePanel::closePluginWindows()
{
    pluginWindows.clear();
}

void DevicePanel::loadSample (const juce::String& nameOrPath)
{
    auto& patch = ctx.selected().patch;
    Module* sampler = nullptr;
    for (auto& m : patch.modules)
        if (m.type == ModuleType::Sampler) { sampler = &m; break; }
    if (sampler == nullptr)
    {
        const int id = patch.add (ModuleType::Sampler);
        keepTrackSound();
        sampler = patch.find (id);
    }
    sampler->sampleName = nameOrPath;
    ctx.engine.rebuildTrack (ctx.selectedTrack);
    ctx.touched();
    rebuild();
}

void DevicePanel::updateCanvasSize()
{
    int right = 0;
    for (auto* card : cards)
        right = juce::jmax (right, card->getRight() + 24);

    const float viewW = (float) viewport.getWidth(), viewH = (float) viewport.getHeight();
    const float rows = Patch::rowsBottom + 22.0f;            // both rows, with room for cables to hang

    // Full screen draws the patch as large as fits: both rows down the window, and the whole chain across
    // it when it can, between actual size and two and a half times.
    zoom = 1.0f;
    if (fullScreen && viewW > 0.0f && viewH > 0.0f)
        zoom = juce::jlimit (1.0f, 2.5f, juce::jmin (viewH / rows, viewW / (float) juce::jmax (1, right)));

    const bool scrolls = (float) right * zoom > viewW;
    // Never shorter than the two rows: a short panel scrolls down to the utilities instead
    const float canvasH = juce::jmax (rows, (viewH - (scrolls ? 6.0f : 0.0f)) / zoom);
    const float offset = std::floor (juce::jmax (0.0f, (canvasH - rows) * 0.5f));
    if (! juce::approximatelyEqual (offset, rowOffset))
    {
        rowOffset = offset;
        for (auto* card : cards)
            if (auto* m = ctx.selected().patch.find (card->getModuleId()))
                card->setTopLeftPosition (card->getX(), juce::roundToInt (Patch::rowY (m->type) + rowOffset));
    }

    canvas.setTransform (juce::AffineTransform::scale (zoom));
    canvas.setBounds (0, 0, juce::jmax (right, juce::roundToInt (std::ceil (viewW / zoom))), juce::roundToInt (canvasH));
    holder.setSize (juce::roundToInt ((float) canvas.getWidth() * zoom), juce::roundToInt (canvasH * zoom));
    cables->rowOffset = rowOffset;
    cables->setBounds (canvas.getLocalBounds());
}

void DevicePanel::resized()
{
    auto r = getLocalBounds().reduced (12, 0);
    auto head = r.removeFromTop (36).reduced (0, 6);
    fullButton.setBounds (head.removeFromRight (fullScreen ? 150 : 100));
    head.removeFromRight (8);
    tidyButton.setBounds (head.removeFromRight (70));
    head.removeFromRight (8);
    addButton.setBounds (head.removeFromRight (130));
    viewport.setBounds (r.withTrimmedBottom (8));
    updateCanvasSize();
}

void DevicePanel::setFullScreen (bool full)
{
    if (fullScreen == full)
        return;
    fullScreen = full;
    fullButton.setButtonText (full ? "Exit full screen" : "Full screen");
    fullButton.setToggleState (full, juce::dontSendNotification);
    viewport.setViewPosition (0, 0);
    if (onFullScreenChanged)
        onFullScreenChanged();      // the window gives the panel its new bounds
    resized();
    repaint();
}

void DevicePanel::mouseDoubleClick (const juce::MouseEvent&)
{
    setFullScreen (! fullScreen);   // the header and the edges count as empty space too
}

void DevicePanel::tick()
{
    const float m = ctx.selected().meter.load();
    for (auto* card : cards)
    {
        card->tick();
        if (card->flash > 0.46f)
        {
            card->flash = juce::jmax (0.45f, card->flash - 0.04f);
            card->repaint();
        }
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
    // Hint line: what the jack under the pointer carries, or why a cable won't go there
    const bool telling = hintText.isNotEmpty();
    g.setColour (telling ? th.text.withAlpha (0.85f) : th.muted.withAlpha (0.7f));
    g.setFont (th.body (11.0f));
    const juce::String help = fullScreen ? "Drag between jacks to patch.  Double-click a cable to remove it.  Esc or double-click empty space to leave full screen."
                                         : "Drag between jacks to patch.  Double-click a cable to remove it.  Double-click empty space for full screen.";
    g.drawFittedText (telling ? hintText : help,
                      head.withTrimmedLeft (260.0f).withTrimmedRight (fullScreen ? 392.0f : 342.0f).toNearestInt(), juce::Justification::centredRight, 1, 0.8f);
}
