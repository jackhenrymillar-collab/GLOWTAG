#include "DevicePanel.h"
#include "../Engine/Samples.h"
#include "../Engine/DeviceParams.h"

namespace
{
    constexpr float jackY = 60.0f;
    constexpr float notesJackDrop = 26.0f;      // a second jack on the same edge sits this far below the first
    const juce::Colour greyWord (0xff6d7077);
}

//==============================================================================
ModuleCard::ModuleCard (AppContext& c, Patch& p, Track& t, int trackIdx, int id, int n)
    : ctx (c), patch (p), track (t), trackIndex (trackIdx), moduleId (id), numeral (n)
{
    const auto type = module().type;
    const float w = DeviceCards::width (type), h = Patch::heightOf (type);
    card = DeviceCards::make (ctx, trackIndex, moduleId, w, h);
    card->dev.patchChanged = [this] { if (onPatchChanged) onPatchChanged(); };
    card->dev.loadAudio = [this] { chooseSample(); };
    card->chooseSample = [this] { chooseSample(); };
    card->openPlugin = [this] { if (onOpenPlugin) onOpenPlugin(); };
    setRepaintsOnMouseActivity (false);
    setSize ((int) w, (int) h);
}

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
            const float spacing = info.sigIns > 2 ? 17.0f : 26.0f;
            return { 0.0f, h * 0.5f + ((float) (port - Port::sigInBase) - (float) (info.sigIns - 1) * 0.5f) * spacing };
        }
    }
    if (port == Port::audioIn)  return { 0.0f, jackY };
    if (port == Port::notesIn)  return { 0.0f, info.hasAudioIn ? jackY + notesJackDrop : jackY };
    if (port == Port::audioOut) return { w, jackY };
    if (port == Port::notesOut) return { w, info.hasAudioOut ? jackY + notesJackDrop : jackY };
    if (port == Port::modOut)   return { w, jackY };

    // Modulation inputs sit at the left end of each knob's fader
    const int k = port - Port::modInBase;
    if (auto it = card->modJack.find (k); it != card->modJack.end())
        return it->second;
    return { 10.0f, h - 10.0f };
}

void ModuleCard::paint (juce::Graphics& g)
{
    using namespace Neon;
    const auto& info = moduleInfo (module().type);
    const auto* c = card->built.colours (0);
    const float w = (float) getWidth(), h = (float) getHeight();
    const double t = now();
    const float dt = lastPaint > 0.0 ? (float) juce::jlimit (0.0, 0.1, t - lastPaint) : 0.0f;
    lastPaint = t;
    const bool utility = isUtility(), endpoint = isEndpoint();

    paintLit (g, getWidth(), getHeight(), glowLayer, 1.0f, [&] (juce::Graphics& gg, bool real)
    {
        // The card: a faint pane in an unlit tube frame. Lit while the mixer points here or a file hovers.
        if (real)
        {
            gg.setColour (juce::Colours::white.withAlpha (0.022f));
            gg.fillRoundedRectangle (1.0f, 1.0f, w - 2.0f, h - 2.0f, 12.0f);
        }
        const float lit = juce::jmax (dropHover ? 1.0f : 0.0f, juce::jmin (1.0f, flash));
        TubeOptions frame;
        frame.w = 0.6f;
        frame.style = 0;
        frame.off = lit < 0.05f;
        tube (gg, roundRect (1.5f, 1.5f, w - 3.0f, h - 3.0f, utility ? 9.0f : 12.0f),
              frame.off ? c[0] : rgba (c[0], 0.35f + 0.65f * lit), frame);

        if (endpoint)
        {
            // Track in / out: a lit word and a column of bulbs following the track's level
            const bool in = module().type == ModuleType::Input;
            label (gg, "track", w * 0.5f, 26.0f, Align::centre, greyWord, 12.0f);
            text (gg, in ? "in" : "out", w * 0.5f, 48.0f, 20.0f, c[1], 1.0f, Align::centre);
            if (in)
            {
                label (gg, "sound", w - 10.0f, jackY + 4.0f, Align::right, greyWord, 10.0f);
                label (gg, "notes", w - 10.0f, jackY + notesJackDrop + 4.0f, Align::right, greyWord, 10.0f);
            }
            const float top = in ? 110.0f : 84.0f, bottom = h - 18.0f;
            const int bulbs = juce::jmax (4, (int) ((bottom - top) / 11.0f));
            const float level = track.meter.load();
            for (int i = 0; i < bulbs; ++i)
            {
                const float u = (float) (i + 1) / (float) bulbs;
                const auto col = u > 0.9f ? juce::Colour (0xffff5a5a) : (u > 0.72f ? juce::Colour (0xffffd166) : c[1]);
                bulb (gg, w * 0.5f, bottom - (float) i * 11.0f, col, level >= u - 0.5f / (float) bulbs ? 1.0f : 0.12f, 3.2f);
            }
            return;
        }

        const bool telling = t - card->built.wall->readoutTime < 2.2 && card->built.wall->readout.isNotEmpty();
        if (utility)
        {
            // A small lit name, what it is working on, and a lamp that follows the signal going through
            text (gg, info.title.toLowerCase(), 14.0f, 28.0f, 18.0f, c[1], 1.0f);
            const auto kind = patch.kindOf (moduleId, Port::sigOut);
            label (gg, telling ? card->built.wall->readout : (kind == Signal::Audio ? "sound" : (kind == Signal::Mod ? "mod" : "free")),
                   w - 14.0f, 27.0f, Align::right, greyWord, 11.0f);
            bulb (gg, w - 14.0f, 44.0f, c[0], 0.2f + 0.8f * juce::jlimit (0.0f, 1.0f, shownDisplay * 1.6f), 3.0f);
            if (module().type == ModuleType::Inverter)
            {
                // A wave in bare glass and the same wave turned over, lit
                Line up, down;
                for (int i = 0; i <= 24; ++i)
                {
                    const float u = (float) i / 24.0f, s = std::sin (u * juce::MathConstants<float>::twoPi) * 13.0f;
                    up.push_back ({ w * 0.5f - 24.0f + u * 48.0f, 68.0f - s });
                    down.push_back ({ w * 0.5f - 24.0f + u * 48.0f, 68.0f + s });
                }
                TubeOptions bare;
                bare.w = 0.55f; bare.style = 0; bare.off = true;
                tube (gg, up, c[0], bare);
                TubeOptions litTube;
                litTube.w = 0.65f; litTube.style = 0;
                tube (gg, down, c[1], litTube);
            }
        }
        else
        {
            DeviceCards::art (gg, module().type, 16.0f, 14.0f, c, t);
            const auto name = module().type == ModuleType::Plugin && module().pluginName.isNotEmpty() ? module().pluginName : info.title;
            const float nameSize = juce::jmin (25.0f, 25.0f * (w - 150.0f) / juce::jmax (1.0f, textWidth (name.toLowerCase(), 25.0f)));
            text (gg, name.toLowerCase(), 90.0f, 38.0f, nameSize, c[1], 1.0f);
            label (gg, telling ? card->built.wall->readout : (dropHover ? juce::String ("drop to load") : DeviceCards::line (module().type)),
                   91.0f, 57.0f, Align::left, telling ? c[2] : greyWord, 12.0f);
            label (gg, romanNumeral (numeral).toLowerCase(), w - 16.0f, 30.0f, Align::right, rgba (c[0], 0.8f), 13.0f);
        }

        card->built.wall->paint (gg, t, real ? dt : 0.0f);
    });
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

// Sampler and Crate Digger: a built-in sample. Drum Rack: a sample for the selected pad.
void ModuleCard::chooseSample()
{
    const bool drums = module().type == ModuleType::Drums;
    const int pad = drums ? juce::jlimit (0, 15, card->dev.choice (Param::Drums::Selected)) : 0;
    showSampleMenu ([safe = juce::Component::SafePointer<ModuleCard> (this), drums, pad] (const juce::String& name)
    {
        if (safe == nullptr) return;
        if (auto* m = safe->patch.find (safe->moduleId))
        {
            if (drums) m->padNames[(size_t) pad] = name;
            else       m->sampleName = name;
        }
        if (safe->onPatchChanged) safe->onPatchChanged();
    });
}

void ModuleCard::tick()
{
    // Keep moving things moving: about 20 frames a second while something on the card animates
    // or a readout is showing, and once more when the readout goes
    const double t = Neon::now();
    float d = shownDisplay;
    if (auto* node = ctx.engine.liveNode (trackIndex, moduleId))
        d = node->display.load();
    const bool changed = std::abs (d - shownDisplay) > 0.01f;
    shownDisplay = d;
    const auto& wall = *card->built.wall;
    const bool telling = t - wall.readoutTime < 2.3;
    lastReadout = wall.readoutTime;
    const bool animate = (card->animated && ctx.engine.isPlaying()) || telling;
    // Track in / out only show the level: repainted when their lit bulbs change
    bool levelMoved = false;
    if (isEndpoint())
    {
        const float level = track.meter.load();
        levelMoved = std::abs (level - shownLevel) > 0.01f;
        if (levelMoved) shownLevel = level;
    }
    if (animate || changed || levelMoved)
        repaint();
    else if (! telling && lastReadout > 0.0 && t - lastReadout < 2.6 && t - lastPaint > 0.2)
        repaint();
}

void ModuleCard::mouseDown (const juce::MouseEvent& e)
{
    onControl = moving = false;
    if (! e.mods.isPopupMenu() && card->built.wall->mouseDown (e.position))
    {
        onControl = true;
        if (onRaise) onRaise();
        repaint();
        return;
    }

    if (e.mods.isPopupMenu() && ! isEndpoint())
    {
        const auto type = module().type;
        juce::PopupMenu menu;
        if (type == ModuleType::Plugin)
            menu.addItem (2, "Open " + module().pluginName);
        if (type == ModuleType::Sampler || type == ModuleType::CrateDigger)
            menu.addItem (3, "Choose a sample");
        if (type == ModuleType::Drums)
            menu.addItem (3, "Choose a sample for pad " + juce::String (card->dev.choice (Param::Drums::Selected) + 1));
        if (DeviceViews::hasView (type))
            menu.addItem (4, "Open the expanded view");
        menu.addSeparator();
        menu.addItem (1, "Remove " + (type == ModuleType::Plugin ? module().pluginName : moduleInfo (type).title));
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                            [safe = juce::Component::SafePointer<ModuleCard> (this)] (int result)
                            {
                                if (safe == nullptr) return;
                                if (result == 1 && safe->onRemove)            safe->onRemove();
                                else if (result == 2 && safe->onOpenPlugin)   safe->onOpenPlugin();
                                else if (result == 3)                         safe->chooseSample();
                                else if (result == 4 && safe->onOpenExpanded) safe->onOpenExpanded();
                            });
        return;
    }
    moving = ! e.mods.isPopupMenu();
    dragStart = module().position;
    if (onRaise) onRaise();
}

void ModuleCard::mouseDrag (const juce::MouseEvent& e)
{
    if (onControl)
    {
        card->built.wall->mouseDrag (e.position, e.mods.isShiftDown());
        repaint();
        return;
    }
    if (! moving)
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

void ModuleCard::mouseUp (const juce::MouseEvent&)
{
    if (onControl)
        card->built.wall->mouseUp();
    onControl = moving = false;
    repaint();
}

void ModuleCard::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (juce::MouseCursor (card->built.wall->mouseMove (e.position)));
    repaint();
}

void ModuleCard::mouseExit (const juce::MouseEvent&)
{
    card->built.wall->mouseExit();
    repaint();
}

void ModuleCard::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) return;
    // A fader resets; empty space opens the expanded view (a plugin opens its own window)
    if (card->built.wall->doubleClick (e.position)) { repaint(); return; }
    if (module().type == ModuleType::Plugin && onOpenPlugin)
        onOpenPlugin();
    else if (! isEndpoint() && ! isUtility() && onOpenExpanded)
        onOpenExpanded();
}

void ModuleCard::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (card->built.wall->wheel (e.position, wheel.deltaY)) { repaint(); return; }
    juce::Component::mouseWheelMove (e, wheel);      // the panel scrolls
}

bool ModuleCard::isInterestedInFileDrag (const juce::StringArray& files)
{
    if (module().type != ModuleType::Sampler && module().type != ModuleType::Drums && module().type != ModuleType::CrateDigger) return false;
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg")) return true;
    return false;
}

void ModuleCard::filesDropped (const juce::StringArray& files, int, int)
{
    dropHover = false;
    if (auto* m = patch.find (moduleId))
    {
        if (m->type == ModuleType::Sampler || m->type == ModuleType::CrateDigger)
            m->sampleName = files[0];
        else if (m->type == ModuleType::Drums)
            m->padNames[(size_t) juce::jlimit (0, 15, card->dev.choice (Param::Drums::Selected))] = files[0];
        else
            return;
        if (onPatchChanged) onPatchChanged();
    }
    repaint();
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

bool CableLayer::skipsCard (float fromX, float toX) const
{
    if (patch != nullptr)
        for (auto& m : patch->modules)
            if (! moduleInfo (m.type).utility)
                if (auto* card = cardFor (m.id))
                    if ((float) card->getX() > fromX + 4.0f && (float) card->getRight() < toX - 4.0f)
                        return true;
    return false;
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
    // A sound or note cable that passes other cards on the device row runs under them, through the
    // gap between the rows, so it never crosses a card's faders
    if (! lowA && ! intoKnob && b.x - a.x > 90.0f && skipsCard (a.x, b.x))
    {
        const float lane = std::fmod (a.x * 0.29f + b.x * 0.13f, 8.0f) - 4.0f;
        const float gapY = rowOffset + Patch::top + Patch::cardHeight + 11.0f + lane;
        juce::Path line;
        line.startNewSubPath (a);
        line.lineTo (a.x + 14.0f, a.y);
        line.lineTo (a.x + 14.0f, gapY);
        line.lineTo (b.x - 14.0f, gapY);
        line.lineTo (b.x - 14.0f, b.y);
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

void CableLayer::tick (bool flowing)
{
    if (patch == nullptr || (! flowing && ! wasFlowing))
        return;
    wasFlowing = flowing;
    // Each cable's own box: the cards between them stay as they are
    for (const auto& c : patch->cables)
        repaint (cablePath (c).getBounds().getSmallestIntegerContainer().expanded (8));
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
    // It sits under every card and cable, so it is kept as a picture rather than drawn dot by dot
    auto& th = Theme::get();
    const float ds = juce::jlimit (1.0f, 4.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const int w = juce::jmax (1, juce::roundToInt ((float) getWidth() * ds)), h = juce::jmax (1, juce::roundToInt ((float) getHeight() * ds));
    const auto key = th.panel.getARGB() ^ (th.line.getARGB() * 31u);
    if (dots.getWidth() != w || dots.getHeight() != h || dotsColour != key)
    {
        dotsColour = key;
        dots = juce::Image (juce::Image::RGB, w, h, false);
        juce::Graphics dg (dots);
        dg.addTransform (juce::AffineTransform::scale ((float) w / (float) juce::jmax (1, getWidth()), (float) h / (float) juce::jmax (1, getHeight())));
        dg.fillAll (th.panel);
        dg.setColour (th.line.withAlpha (0.55f));
        for (int y = 10; y < getHeight(); y += 18)
            for (int x = 10; x < getWidth(); x += 18)
                dg.fillEllipse ((float) x - 0.9f, (float) y - 0.9f, 1.8f, 1.8f);
    }
    g.drawImageTransformed (dots, juce::AffineTransform::scale ((float) getWidth() / (float) w, (float) getHeight() / (float) h));
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
        static const Entry workshop[] = {
            { ModuleType::GasTube, "Gas Tube  (neon tubes that strike and buzz)" }, { ModuleType::CrateDigger, "Crate Digger  (slices dug out of a record)" },
            { ModuleType::StringWall, "String Wall  (pluck, bow or feed back)" }, { ModuleType::GhostTag, "Ghost Tag  (echoes that fade and drift)" },
            { ModuleType::Crowd, "Crowd  (one sound sung by a crowd)" }, { ModuleType::Transformer, "Transformer  (circuit bending)" } };
        static const Entry utilities[] = {
            { ModuleType::Attenuator, "Attenuator  (turn a signal down)" }, { ModuleType::Mixer, "Mixer  (four inputs summed)" },
            { ModuleType::Inverter, "Inverter  (flip a signal over)" }, { ModuleType::Crossfade, "Crossfade  (blend A into B)" },
            { ModuleType::Slew, "Slew  (smooth a signal)" } };
        juce::PopupMenu menu;
        menu.addSectionHeader ("Devices: added and auto-wired");
        for (auto& e : devices)
            menu.addItem (1 + (int) e.type, e.name);
        menu.addSectionHeader ("Workshop: stranger instruments and effects");
        for (auto& e : workshop)
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

    // Cards grew taller and wider: keep each row's cards from lying on top of one another
    for (const bool utilityRow : { false, true })
    {
        std::vector<Module*> row;
        for (auto& m : patch.modules)
            if (moduleInfo (m.type).utility == utilityRow)
                row.push_back (&m);
        std::sort (row.begin(), row.end(), [] (auto* a, auto* b) { return a->position.x < b->position.x; });
        float minX = 0.0f;
        for (auto* m : row)
        {
            m->position.x = juce::jmax (m->position.x, minX);
            minX = m->position.x + moduleInfo (m->type).width + (utilityRow ? Patch::utilityGap : Patch::gap) * 0.5f;
        }
    }

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
    addButton.setBounds (head.removeFromRight (80));
    viewport.setBounds (r.withTrimmedBottom (8));
    updateCanvasSize();
}

void DevicePanel::setFullScreen (bool full)
{
    if (fullScreen == full)
        return;
    fullScreen = full;
    fullButton.setButtonText (full ? "exit full screen" : "full screen");
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
            card->flash = juce::jmax (0.45f, card->flash - 0.04f * Neon::frameTime() * 30.0f);
            card->repaint();
        }
    }
    cables->tick (ctx.engine.isPlaying() && m > 0.02f);
}

void DevicePanel::paint (juce::Graphics& g)
{
    auto& th = Theme::get();
    g.fillAll (th.panel);
    g.setColour (th.line2);
    g.drawHorizontalLine (0, 0.0f, (float) getWidth());

    auto head = getLocalBounds().reduced (12, 0).removeFromTop (36).toFloat();
    auto& t = ctx.selected();
    const auto col = th.isRainbow() ? th.pal (t.colourIndex) : th.themeColour();
    Neon::paintLit (g, getWidth(), 36, headGlow, 0.7f, [&] (juce::Graphics& gg, bool)
    {
        Neon::label (gg, "devices", head.getX(), head.getCentreY() + 4.0f, Neon::Align::left, juce::Colour (0xff6d7077), 13.0f);
        Neon::text (gg, t.name.toLowerCase(), head.getX() + 72.0f, head.getCentreY() + 7.0f, 20.0f, col, 0.9f);
    });
    // Hint line: what the jack under the pointer carries, or why a cable won't go there
    const bool telling = hintText.isNotEmpty();
    g.setColour (telling ? th.text.withAlpha (0.85f) : th.muted.withAlpha (0.7f));
    g.setFont (th.body (11.0f));
    const juce::String help = fullScreen ? "Drag between jacks to patch.  Double-click a cable to remove it.  Esc or double-click empty space to leave full screen."
                                         : "Drag between jacks to patch.  Drag faders, click words.  Double-click empty space for full screen.";
    g.drawFittedText (telling ? hintText : help,
                      head.withTrimmedLeft (260.0f).withTrimmedRight (fullScreen ? 392.0f : 342.0f).toNearestInt(), juce::Justification::centredRight, 1, 0.8f);
}
