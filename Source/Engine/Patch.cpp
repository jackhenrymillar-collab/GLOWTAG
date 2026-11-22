#include "Patch.h"
#include <functional>

const ModuleInfo& moduleInfo (ModuleType t)
{
    // Pal indices: Rose 0, Green 1, Sky 2, Gold 3, Violet 4, Blue 5, Amber 6, Ice 7
    // Fields: title, colour, audio in, audio out, mod out, knobs, width, notes in, notes out, signal ins, utility
    static const ModuleInfo input      { "Track In",   1, false, true,  false, {}, 74.0f, false, true };
    static const ModuleInfo output     { "Out",        1, true,  false, false, {}, 74.0f };
    static const ModuleInfo sampler    { "Sampler",    0, false, true,  false, { { "Start", 0.0f }, { "Length", 1.0f }, { "Pitch", 0.5f }, { "Attack", 0.0f }, { "Release", 0.3f }, { "Gain", 0.8f } }, 300.0f, true };
    static const ModuleInfo synth      { "Synth",      4, false, true,  false, { { "Wave", 0.0f }, { "Sub", 0.3f }, { "Detune", 0.15f }, { "Cutoff", 0.6f }, { "Res", 0.25f }, { "Env", 0.4f },
                                                                                 { "Attack", 0.02f }, { "Decay", 0.3f }, { "Sustain", 0.6f }, { "Release", 0.3f }, { "Drive", 0.2f }, { "Gain", 0.7f } }, 330.0f, true };
    static const ModuleInfo drums      { "Drum Rack",  6, false, true,  false, { { "Tune", 0.5f }, { "Decay", 0.7f }, { "Snap", 0.3f }, { "Gain", 0.8f } }, 300.0f, true };
    static const ModuleInfo filter     { "Filter",     2, true,  true,  false, { { "Cutoff", 0.65f }, { "Res", 0.3f }, { "Drive", 0.2f }, { "Mode", 0.0f } }, 200.0f };
    static const ModuleInfo drive      { "Drive",      6, true,  true,  false, { { "Drive", 0.4f }, { "Tone", 0.6f }, { "Mix", 0.8f }, { "Type", 0.0f } }, 200.0f };
    static const ModuleInfo delay      { "Delay",      5, true,  true,  false, { { "Time", 0.5f }, { "Feedback", 0.4f }, { "Tone", 0.5f }, { "Mix", 0.25f } }, 200.0f };
    static const ModuleInfo reverb     { "Reverb",     7, true,  true,  false, { { "Size", 0.6f }, { "Damp", 0.4f }, { "Width", 0.8f }, { "Mix", 0.3f } }, 200.0f };
    static const ModuleInfo eq         { "EQ",         3, true,  true,  false, { { "Low", 0.0f }, { "Mid", 0.0f }, { "High", 0.0f }, { "Output", 0.7f } }, 200.0f };
    static const ModuleInfo compressor { "Compressor", 1, true,  true,  false, { { "Thresh", 0.5f }, { "Ratio", 0.4f }, { "Attack", 0.2f }, { "Release", 0.4f }, { "Makeup", 0.3f } }, 240.0f };
    static const ModuleInfo lfo        { "LFO",        4, false, false, true,  { { "Rate", 0.4f }, { "Depth", 0.6f }, { "Shape", 0.0f } }, 170.0f };
    // A plugin may be an instrument or an effect, so it has both a notes and an audio input
    static const ModuleInfo plugin     { "Plugin",     5, true,  true,  false, { { "Mix", 1.0f }, { "Gain", 0.7f } }, 240.0f, true };

    // Utilities: small signal tools for the second row. They work on sound or on modulation.
    static const ModuleInfo attenuator { "Atten",      3, false, false, false, { { "Level", 1.0f } }, 92.0f, false, false, 1, true };
    static const ModuleInfo mixer      { "Mixer",      2, false, false, false, { { "1", 1.0f }, { "2", 1.0f }, { "3", 1.0f }, { "4", 1.0f } }, 204.0f, false, false, 4, true };
    static const ModuleInfo inverter   { "Invert",     0, false, false, false, {}, 84.0f, false, false, 1, true };
    static const ModuleInfo crossfade  { "X-Fade",     4, false, false, false, { { "Mix", 0.5f } }, 100.0f, false, false, 2, true };
    static const ModuleInfo slew       { "Slew",       7, false, false, false, { { "Time", 0.3f } }, 92.0f, false, false, 1, true };

    switch (t)
    {
        case ModuleType::Input:      return input;
        case ModuleType::Output:     return output;
        case ModuleType::Sampler:    return sampler;
        case ModuleType::Synth:      return synth;
        case ModuleType::Drums:      return drums;
        case ModuleType::Filter:     return filter;
        case ModuleType::Drive:      return drive;
        case ModuleType::Delay:      return delay;
        case ModuleType::Reverb:     return reverb;
        case ModuleType::EQ:         return eq;
        case ModuleType::Compressor: return compressor;
        case ModuleType::LFO:        return lfo;
        case ModuleType::Plugin:     return plugin;
        case ModuleType::Attenuator: return attenuator;
        case ModuleType::Mixer:      return mixer;
        case ModuleType::Inverter:   return inverter;
        case ModuleType::Crossfade:  return crossfade;
        case ModuleType::Slew:       return slew;
    }
    return filter;
}

std::vector<int> ModuleInfo::ports() const
{
    std::vector<int> j;
    if (hasAudioIn)  j.push_back (Port::audioIn);
    if (hasNotesIn)  j.push_back (Port::notesIn);
    for (int i = 0; i < sigIns; ++i) j.push_back (Port::sigIn (i));
    if (hasAudioOut) j.push_back (Port::audioOut);
    if (hasNotesOut) j.push_back (Port::notesOut);
    if (hasModOut)   j.push_back (Port::modOut);
    if (utility)     j.push_back (Port::sigOut);
    for (int k = 0; k < (int) knobs.size(); ++k)
        j.push_back (Port::modIn (k));
    return j;
}

bool ModuleInfo::hasPort (int port) const
{
    if (port == Port::audioIn)  return hasAudioIn;
    if (port == Port::audioOut) return hasAudioOut;
    if (port == Port::modOut)   return hasModOut;
    if (port == Port::notesIn)  return hasNotesIn;
    if (port == Port::notesOut) return hasNotesOut;
    if (port == Port::sigOut)   return utility;
    if (Port::isSigIn (port))   return port - Port::sigInBase < sigIns;
    if (Port::isModIn (port))   return port - Port::modInBase < (int) knobs.size();
    return false;
}

Patch::Patch()
{
    Module in;
    in.id = inputId;
    in.type = ModuleType::Input;
    in.position = { 12.0f, top };
    Module out;
    out.id = outputId;
    out.type = ModuleType::Output;
    out.position = { 12.0f + 74.0f + gap, top };
    modules.push_back (in);
    modules.push_back (out);
    connect (inputId, Port::audioOut, outputId, Port::audioIn);
}

Module* Patch::find (int id)
{
    for (auto& m : modules)
        if (m.id == id)
            return &m;
    return nullptr;
}

const Module* Patch::find (int id) const
{
    return const_cast<Patch*> (this)->find (id);
}

int Patch::cableInto (int module, int port) const
{
    for (size_t i = 0; i < cables.size(); ++i)
        if (cables[i].toModule == module && cables[i].toPort == port)
            return (int) i;
    return -1;
}

int Patch::lastInChain() const
{
    const int i = cableInto (outputId, Port::audioIn);
    return i >= 0 ? cables[(size_t) i].fromModule : -1;
}

// A utility works on whatever it is patched to. Follow its signal cables, through other utilities,
// to the first jack that is plainly sound or modulation.
Signal Patch::settledKind (int utilityModule) const
{
    std::vector<int> seen { utilityModule };
    for (size_t i = 0; i < seen.size(); ++i)
    {
        const int id = seen[i];
        for (auto& c : cables)
        {
            int otherModule, otherPort;
            if (c.fromModule == id && c.fromPort == Port::sigOut)     { otherModule = c.toModule;   otherPort = c.toPort; }
            else if (c.toModule == id && Port::isSigIn (c.toPort))    { otherModule = c.fromModule; otherPort = c.fromPort; }
            else continue;

            const auto fixed = Port::kind (otherPort);
            if (fixed != Signal::Any)
                return fixed;
            if (std::find (seen.begin(), seen.end(), otherModule) == seen.end())
                seen.push_back (otherModule);
        }
    }
    return Signal::Any;
}

Signal Patch::kindOf (int module, int port) const
{
    const auto fixed = Port::kind (port);
    return fixed != Signal::Any ? fixed : settledKind (module);
}

bool Patch::canConnect (int fromModule, int fromPort, int toModule, int toPort) const
{
    const auto* from = find (fromModule);
    const auto* to = find (toModule);
    if (from == nullptr || to == nullptr || fromModule == toModule)
        return false;
    if (Port::isInput (fromPort) || ! Port::isInput (toPort))
        return false;
    if (! moduleInfo (from->type).hasPort (fromPort) || ! moduleInfo (to->type).hasPort (toPort))
        return false;

    const auto a = kindOf (fromModule, fromPort), b = kindOf (toModule, toPort);
    if (a == b)
        return true;
    // An unsettled utility takes sound or modulation, never notes
    return (a == Signal::Any && b != Signal::Notes) || (b == Signal::Any && a != Signal::Notes);
}

bool Patch::connect (int fromModule, int fromPort, int toModule, int toPort)
{
    if (! canConnect (fromModule, fromPort, toModule, toPort))
        return false;

    const int existing = cableInto (toModule, toPort);
    if (existing >= 0)
        cables.erase (cables.begin() + existing);

    cables.push_back ({ fromModule, fromPort, toModule, toPort });
    return true;
}

void Patch::disconnect (size_t cableIndex)
{
    if (cableIndex < cables.size())
        cables.erase (cables.begin() + (long) cableIndex);
}

juce::Point<float> Patch::nextFreePosition (bool utilityRow) const
{
    // Just right of the right-most module on that row (the output stays at the end of its row)
    float x = 12.0f;
    for (auto& m : modules)
        if (m.id != outputId && moduleInfo (m.type).utility == utilityRow)
            x = juce::jmax (x, m.position.x + moduleInfo (m.type).width + (utilityRow ? utilityGap : gap));
    return { x, utilityRow ? utilityTop : top };
}

int Patch::add (ModuleType type, bool asInstrument)
{
    return addAt (type, nextFreePosition (moduleInfo (type).utility), asInstrument);
}

int Patch::addAt (ModuleType type, juce::Point<float> position, bool asInstrument)
{
    const auto& info = moduleInfo (type);
    Module m;
    m.id = nextId++;
    m.type = type;
    m.position = { position.x, rowY (type) };
    for (auto& k : info.knobs)
        m.knobValues.push_back (k.second);
    modules.push_back (m);
    const int id = m.id;

    if (info.utility)
        return id;                  // utilities are patched by hand

    // Keep the output at the end of the row
    if (auto* out = find (outputId))
        out->position.x = juce::jmax (out->position.x, m.position.x + info.width + gap);

    if (info.isInstrument() || (type == ModuleType::Plugin && asInstrument))
    {
        wireInstrument (id);
    }
    else if (info.isEffect())
    {
        // Insert just before the output
        const int prev = lastInChain();
        const int toOut = cableInto (outputId, Port::audioIn);
        const int prevPort = toOut >= 0 ? cables[(size_t) toOut].fromPort : Port::audioOut;
        if (toOut >= 0)
            cables.erase (cables.begin() + toOut);
        if (prev >= 0)
            connect (prev, prevPort, id, Port::audioIn);
        connect (id, Port::audioOut, outputId, Port::audioIn);
    }
    else if (info.hasModOut)
    {
        // Modulate the first free knob of the last device in the chain (looking past any utilities)
        int target = lastInChain();
        for (int guard = 0; guard < 16 && find (target) != nullptr && moduleInfo (find (target)->type).utility; ++guard)
        {
            const int in = cableInto (target, Port::sigIn (0));
            target = in >= 0 ? cables[(size_t) in].fromModule : -1;
        }
        if (auto* t = find (target))
        {
            const int numKnobs = (int) moduleInfo (t->type).knobs.size();
            for (int k = 0; k < numKnobs; ++k)
                if (cableInto (target, Port::modIn (k)) < 0)
                {
                    connect (id, Port::modOut, target, Port::modIn (k));
                    break;
                }
        }
    }
    return id;
}

// An instrument plays the track's notes and starts the audio chain
void Patch::wireInstrument (int id)
{
    connect (inputId, Port::notesOut, id, Port::notesIn);
    joinChain (id, Port::audioOut, true);
}

// The track's own sound (its audio clips, or what is sent to a bus) is only heard if the track
// input's sound jack goes somewhere. Patch it into the start of the chain if it is loose.
bool Patch::patchTrackSound()
{
    for (auto& c : cables)
        if (c.fromModule == inputId && c.fromPort == Port::audioOut)
            return false;
    joinChain (inputId, Port::audioOut, false);
    return true;
}

// Feed a source into the start of the audio chain. If something already starts it, the two are mixed
// through a Mixer on the utility row, except that a new instrument may take the place of the track's
// own sound (`replaceTrackSound`), which on a note track is silence anyway.
void Patch::joinChain (int fromModule, int fromPort, bool replaceTrackSound)
{
    // Walk back from the output to where the chain begins
    int toModule = outputId, toPort = Port::audioIn;
    for (int guard = 0; guard < 64; ++guard)
    {
        const int c = cableInto (toModule, toPort);
        if (c < 0)
        {
            connect (fromModule, fromPort, toModule, toPort);         // nothing feeds it yet
            return;
        }
        const auto cable = cables[(size_t) c];
        if (cable.fromModule == fromModule)
            return;                                                    // already there
        if (cable.fromModule == inputId && replaceTrackSound)
        {
            cables.erase (cables.begin() + c);
            connect (fromModule, fromPort, toModule, toPort);
            return;
        }
        const auto* source = find (cable.fromModule);
        if (source == nullptr)
            return;
        const auto& si = moduleInfo (source->type);
        if (source->type == ModuleType::Mixer)
        {
            // Layered sources share a Mixer: take its next free input
            for (int i = 0; i < si.sigIns; ++i)
                if (cableInto (source->id, Port::sigIn (i)) < 0)
                {
                    connect (fromModule, fromPort, source->id, Port::sigIn (i));
                    return;
                }
        }
        const bool pluginInstrument = source->type == ModuleType::Plugin && cableInto (source->id, Port::audioIn) < 0
                                      && cableInto (source->id, Port::notesIn) >= 0;
        if (si.utility)                          { toModule = source->id; toPort = Port::sigIn (0); continue; }
        if (si.hasAudioIn && ! pluginInstrument) { toModule = source->id; toPort = Port::audioIn;   continue; }

        // A source already starts the chain: mix the two, under whichever of them is a device
        const auto* anchor = find (fromModule == inputId ? cable.fromModule : fromModule);
        const float x = anchor != nullptr ? anchor->position.x : 12.0f;
        const int mix = addAt (ModuleType::Mixer, { x, utilityTop });
        cables.erase (cables.begin() + cableInto (toModule, toPort));
        connect (cable.fromModule, cable.fromPort, mix, Port::sigIn (0));
        connect (fromModule, fromPort, mix, Port::sigIn (1));
        connect (mix, Port::sigOut, toModule, toPort);
        return;
    }
}

void Patch::remove (int id)
{
    if (id == inputId || id == outputId)
        return;

    // Heal the chain: whatever fed this module's main input now feeds whatever it fed
    const auto* m = find (id);
    if (m == nullptr)
        return;
    const auto& info = moduleInfo (m->type);
    int inPort = -1, outPort = -1;
    if (info.isEffect())    { inPort = Port::audioIn; outPort = Port::audioOut; }
    else if (info.utility)  { outPort = Port::sigOut; for (int i = info.sigIns; --i >= 0;) if (cableInto (id, Port::sigIn (i)) >= 0) inPort = Port::sigIn (i); }

    const int in = inPort >= 0 ? cableInto (id, inPort) : -1;
    const auto source = in >= 0 ? cables[(size_t) in] : Cable { -1, 0, 0, 0 };
    std::vector<std::pair<int, int>> destinations;
    for (auto& c : cables)
        if (c.fromModule == id && c.fromPort == outPort)
            destinations.push_back ({ c.toModule, c.toPort });

    cables.erase (std::remove_if (cables.begin(), cables.end(),
                                  [id] (const Cable& c) { return c.fromModule == id || c.toModule == id; }),
                  cables.end());
    modules.erase (std::remove_if (modules.begin(), modules.end(), [id] (const Module& mod) { return mod.id == id; }),
                   modules.end());

    if (source.fromModule >= 0)
        for (auto& d : destinations)
            connect (source.fromModule, source.fromPort, d.first, d.second);
}

void Patch::tidy()
{
    // Devices in signal order: everything that feeds the output, sources first. Utilities are looked
    // through here and laid out on their own row afterwards.
    std::vector<int> order, visiting;
    std::function<void (int)> visit = [&] (int id)
    {
        if (std::find (order.begin(), order.end(), id) != order.end() || std::find (visiting.begin(), visiting.end(), id) != visiting.end())
            return;
        visiting.push_back (id);
        for (auto& c : cables)
            if (c.toModule == id && kindOf (c) != Signal::Mod && c.fromModule != inputId)
                visit (c.fromModule);
        order.push_back (id);
    };
    visit (outputId);

    auto isUtility = [this] (int id) { auto* m = find (id); return m != nullptr && moduleInfo (m->type).utility; };
    auto has = [] (const std::vector<int>& v, int id) { return std::find (v.begin(), v.end(), id) != v.end(); };

    // Modulators go just before the device they modulate; anything unwired goes at the end
    std::vector<int> placed { inputId };
    for (int id : order)
    {
        if (id == outputId || isUtility (id))
            continue;
        for (auto& c : cables)
            if (c.toModule == id && kindOf (c) == Signal::Mod && ! isUtility (c.fromModule)
                && ! has (placed, c.fromModule) && ! has (order, c.fromModule))
                placed.push_back (c.fromModule);
        if (! has (placed, id))
            placed.push_back (id);
    }
    for (auto& m : modules)
        if (m.id != outputId && ! isUtility (m.id) && ! has (placed, m.id))
            placed.push_back (m.id);
    placed.push_back (outputId);

    float x = 12.0f;
    for (int id : placed)
        if (auto* m = find (id))
        {
            m->position = { x, top };
            x += moduleInfo (m->type).width + gap;
        }

    // Utilities: each under the device it takes from (or, failing that, feeds), left to right
    auto anchorOf = [&] (int id)
    {
        for (int pass = 0; pass < 2; ++pass)
            for (auto& c : cables)
            {
                const int other = pass == 0 ? (c.toModule == id && Port::isSigIn (c.toPort) ? c.fromModule : -1)
                                            : (c.fromModule == id && c.fromPort == Port::sigOut ? c.toModule : -1);
                if (other >= 0 && ! isUtility (other))
                    if (auto* o = find (other))
                        return o->position.x + (pass == 0 ? 0.0f : -40.0f);
            }
        return find (id)->position.x;
    };
    std::vector<std::pair<float, int>> utilities;
    for (auto& m : modules)
        if (isUtility (m.id))
            utilities.push_back ({ anchorOf (m.id), m.id });
    std::stable_sort (utilities.begin(), utilities.end(), [] (auto& a, auto& b) { return a.first < b.first; });
    float cursor = 12.0f;
    for (auto& u : utilities)
        if (auto* m = find (u.second))
        {
            m->position = { juce::jmax (cursor, u.first), utilityTop };
            cursor = m->position.x + moduleInfo (m->type).width + utilityGap;
        }
}

// Patches saved before note cables: instruments took their notes without a cable and passed the sound
// before them straight through. Give each its notes cable, and keep any layering with a Mixer.
void Patch::upgrade()
{
    std::vector<int> ids;
    for (auto& m : modules)
        ids.push_back (m.id);

    for (int id : ids)
    {
        const auto type = find (id)->type;
        const auto& info = moduleInfo (type);
        if (! info.hasNotesIn)
            continue;
        if (info.isInstrument())
        {
            // The old audio input: gone now. If something other than the track input fed it, the two
            // were heard together, so mix them where this instrument's sound used to go.
            for (size_t i = 0; i < cables.size(); ++i)
            {
                if (cables[i].toModule != id || cables[i].toPort != Port::audioIn)
                    continue;
                const auto before = cables[i];
                cables.erase (cables.begin() + (long) i);
                if (before.fromModule != inputId && find (before.fromModule) != nullptr)
                {
                    const float x = find (id)->position.x;
                    const int mix = addAt (ModuleType::Mixer, { x, utilityTop });
                    for (auto& c : cables)
                        if (c.fromModule == id && c.fromPort == Port::audioOut)
                        {
                            c.fromModule = mix;
                            c.fromPort = Port::sigOut;
                        }
                    cables.push_back ({ before.fromModule, before.fromPort, mix, Port::sigIn (0) });
                    cables.push_back ({ id, Port::audioOut, mix, Port::sigIn (1) });
                }
                break;
            }
        }
        if (cableInto (id, Port::notesIn) < 0)
            cables.push_back ({ inputId, Port::notesOut, id, Port::notesIn });
    }
}

//==============================================================================
juce::var Patch::toVar() const
{
    auto* o = new juce::DynamicObject();
    juce::Array<juce::var> ms, cs;
    for (auto& m : modules)
    {
        auto* mo = new juce::DynamicObject();
        mo->setProperty ("id", m.id);
        mo->setProperty ("type", (int) m.type);
        mo->setProperty ("x", m.position.x);
        mo->setProperty ("y", m.position.y);
        juce::Array<juce::var> knobs;
        for (float k : m.knobValues) knobs.add (k);
        mo->setProperty ("knobs", knobs);
        if (m.sampleName.isNotEmpty()) mo->setProperty ("sample", m.sampleName);
        juce::Array<juce::var> pads;
        bool anyPad = false;
        for (auto& pn : m.padNames) { pads.add (pn); anyPad = anyPad || pn.isNotEmpty(); }
        if (anyPad) mo->setProperty ("pads", pads);
        if (m.pluginId.isNotEmpty()) mo->setProperty ("plugin", m.pluginId);
        if (m.pluginName.isNotEmpty()) mo->setProperty ("pluginName", m.pluginName);
        if (m.pluginState.getSize() > 0) mo->setProperty ("pluginState", m.pluginState.toBase64Encoding());
        ms.add (juce::var (mo));
    }
    for (auto& c : cables)
    {
        juce::Array<juce::var> a { c.fromModule, c.fromPort, c.toModule, c.toPort };
        cs.add (a);
    }
    o->setProperty ("modules", ms);
    o->setProperty ("cables", cs);
    o->setProperty ("nextId", nextId);
    o->setProperty ("v", 2);                // 2: note cables and the utility row
    return juce::var (o);
}

void Patch::fromVar (const juce::var& v)
{
    modules.clear();
    cables.clear();
    if (auto* ms = v["modules"].getArray())
        for (auto& mv : *ms)
        {
            Module m;
            m.id = (int) mv["id"];
            m.type = (ModuleType) juce::jlimit (0, numModuleTypes - 1, (int) mv["type"]);
            m.position = { (float) mv["x"], (float) mv["y"] };
            const auto& info = moduleInfo (m.type);
            for (auto& k : info.knobs) m.knobValues.push_back (k.second);
            if (auto* ks = mv["knobs"].getArray())
                for (int i = 0; i < ks->size() && i < (int) m.knobValues.size(); ++i)
                    m.knobValues[(size_t) i] = (float) (*ks)[i];
            m.sampleName = mv["sample"].toString();
            if (auto* ps = mv["pads"].getArray())
                for (int i = 0; i < ps->size() && i < 16; ++i)
                    m.padNames[(size_t) i] = (*ps)[i].toString();
            m.pluginId = mv["plugin"].toString();
            m.pluginName = mv["pluginName"].toString();
            if (mv.hasProperty ("pluginState")) m.pluginState.fromBase64Encoding (mv["pluginState"].toString());
            modules.push_back (m);
        }
    if (auto* cs = v["cables"].getArray())
        for (auto& cv : *cs)
            if (auto* a = cv.getArray(); a != nullptr && a->size() == 4)
                cables.push_back ({ (int) (*a)[0], (int) (*a)[1], (int) (*a)[2], (int) (*a)[3] });
    nextId = juce::jmax ((int) v.getProperty ("nextId", 3), 3);
    for (auto& m : modules) nextId = juce::jmax (nextId, m.id + 1);
    if (find (inputId) == nullptr || find (outputId) == nullptr)
    {
        *this = Patch();
        return;
    }
    for (auto& m : modules)
        m.position.y = rowY (m.type);       // cards sit on their row
    upgraded = (int) v.getProperty ("v", 1) < 2;
    if (upgraded)
        upgrade();
    // Anything a newer or damaged file wired that the rules don't allow is dropped
    for (size_t i = cables.size(); i-- > 0;)
    {
        const auto c = cables[i];
        cables.erase (cables.begin() + (long) i);
        const bool ok = canConnect (c.fromModule, c.fromPort, c.toModule, c.toPort);
        cables.insert (cables.begin() + (long) i, c);
        if (! ok)
            cables.erase (cables.begin() + (long) i);
    }
}
