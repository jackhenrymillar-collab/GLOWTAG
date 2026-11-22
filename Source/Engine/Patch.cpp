#include "Patch.h"

const ModuleInfo& moduleInfo (ModuleType t)
{
    // Pal indices: Rose 0, Green 1, Sky 2, Gold 3, Violet 4, Blue 5, Amber 6, Ice 7
    static const ModuleInfo input      { "Track In",   1, false, true,  false, {}, 74.0f };
    static const ModuleInfo output     { "Out",        1, true,  false, false, {}, 74.0f };
    static const ModuleInfo sampler    { "Sampler",    0, true,  true,  false, { { "Start", 0.0f }, { "Length", 1.0f }, { "Pitch", 0.5f }, { "Attack", 0.0f }, { "Release", 0.3f }, { "Gain", 0.8f } }, 300.0f };
    static const ModuleInfo synth      { "Synth",      4, true,  true,  false, { { "Wave", 0.0f }, { "Sub", 0.3f }, { "Detune", 0.15f }, { "Cutoff", 0.6f }, { "Res", 0.25f }, { "Env", 0.4f },
                                                                                 { "Attack", 0.02f }, { "Decay", 0.3f }, { "Sustain", 0.6f }, { "Release", 0.3f }, { "Drive", 0.2f }, { "Gain", 0.7f } }, 330.0f };
    static const ModuleInfo drums      { "Drum Rack",  6, true,  true,  false, { { "Tune", 0.5f }, { "Decay", 0.7f }, { "Snap", 0.3f }, { "Gain", 0.8f } }, 300.0f };
    static const ModuleInfo filter     { "Filter",     2, true,  true,  false, { { "Cutoff", 0.65f }, { "Res", 0.3f }, { "Drive", 0.2f }, { "Mode", 0.0f } }, 200.0f };
    static const ModuleInfo drive      { "Drive",      6, true,  true,  false, { { "Drive", 0.4f }, { "Tone", 0.6f }, { "Mix", 0.8f }, { "Type", 0.0f } }, 200.0f };
    static const ModuleInfo delay      { "Delay",      5, true,  true,  false, { { "Time", 0.5f }, { "Feedback", 0.4f }, { "Tone", 0.5f }, { "Mix", 0.25f } }, 200.0f };
    static const ModuleInfo reverb     { "Reverb",     7, true,  true,  false, { { "Size", 0.6f }, { "Damp", 0.4f }, { "Width", 0.8f }, { "Mix", 0.3f } }, 200.0f };
    static const ModuleInfo eq         { "EQ",         3, true,  true,  false, { { "Low", 0.0f }, { "Mid", 0.0f }, { "High", 0.0f }, { "Output", 0.7f } }, 200.0f };
    static const ModuleInfo compressor { "Compressor", 1, true,  true,  false, { { "Thresh", 0.5f }, { "Ratio", 0.4f }, { "Attack", 0.2f }, { "Release", 0.4f }, { "Makeup", 0.3f } }, 240.0f };
    static const ModuleInfo lfo        { "LFO",        4, false, false, true,  { { "Rate", 0.4f }, { "Depth", 0.6f }, { "Shape", 0.0f } }, 170.0f };
    static const ModuleInfo plugin     { "Plugin",     5, true,  true,  false, { { "Mix", 1.0f }, { "Gain", 0.7f } }, 240.0f };

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
    }
    return filter;
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

bool Patch::connect (int fromModule, int fromPort, int toModule, int toPort)
{
    if (fromModule == toModule || find (fromModule) == nullptr || find (toModule) == nullptr)
        return false;
    if (Port::isInput (fromPort) || ! Port::isInput (toPort) || Port::isMod (fromPort) != Port::isMod (toPort))
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

juce::Point<float> Patch::nextFreePosition() const
{
    // Just right of the right-most module that isn't the output
    float x = 12.0f;
    for (auto& m : modules)
        if (m.id != outputId)
            x = juce::jmax (x, m.position.x + moduleInfo (m.type).width + gap);
    return { x, top };
}

int Patch::add (ModuleType type)
{
    return addAt (type, nextFreePosition());
}

int Patch::addAt (ModuleType type, juce::Point<float> position)
{
    Module m;
    m.id = nextId++;
    m.type = type;
    m.position = position;
    for (auto& k : moduleInfo (type).knobs)
        m.knobValues.push_back (k.second);
    modules.push_back (m);

    // Keep the output at the end of the row
    if (auto* out = find (outputId))
        out->position.x = juce::jmax (out->position.x, m.position.x + moduleInfo (type).width + gap);

    const auto& info = moduleInfo (type);
    if (info.hasAudioIn && info.hasAudioOut)
    {
        // Insert just before the output
        const int prev = lastInChain();
        const int toOut = cableInto (outputId, Port::audioIn);
        if (toOut >= 0)
            cables.erase (cables.begin() + toOut);
        if (prev >= 0)
            connect (prev, Port::audioOut, m.id, Port::audioIn);
        connect (m.id, Port::audioOut, outputId, Port::audioIn);
    }
    else if (info.hasModOut)
    {
        // Modulate the first free knob of the last module in the chain
        const int target = lastInChain();
        if (auto* t = find (target))
        {
            const int numKnobs = (int) moduleInfo (t->type).knobs.size();
            for (int k = 0; k < numKnobs; ++k)
                if (cableInto (target, Port::modIn (k)) < 0)
                {
                    connect (m.id, Port::modOut, target, Port::modIn (k));
                    break;
                }
        }
    }
    return m.id;
}

void Patch::remove (int id)
{
    if (id == inputId || id == outputId)
        return;

    // Heal the chain: whatever fed this module now feeds whatever it fed
    const int in = cableInto (id, Port::audioIn);
    const int source = in >= 0 ? cables[(size_t) in].fromModule : -1;
    std::vector<std::pair<int, int>> destinations;
    for (auto& c : cables)
        if (c.fromModule == id && c.fromPort == Port::audioOut)
            destinations.push_back ({ c.toModule, c.toPort });

    cables.erase (std::remove_if (cables.begin(), cables.end(),
                                  [id] (const Cable& c) { return c.fromModule == id || c.toModule == id; }),
                  cables.end());
    modules.erase (std::remove_if (modules.begin(), modules.end(), [id] (const Module& m) { return m.id == id; }),
                   modules.end());

    if (source >= 0)
        for (auto& d : destinations)
            connect (source, Port::audioOut, d.first, d.second);
}

void Patch::tidy()
{
    // Follow the audio chain from the input, then place modulators after it
    std::vector<int> order;
    int current = inputId;
    while (current >= 0 && order.size() < modules.size())
    {
        order.push_back (current);
        int next = -1;
        for (auto& c : cables)
            if (c.fromModule == current && c.fromPort == Port::audioOut)
            {
                next = c.toModule;
                break;
            }
        if (next < 0 || std::find (order.begin(), order.end(), next) != order.end())
            break;
        current = next;
    }

    // Modulators go just before the module they modulate; anything unwired goes at the end
    std::vector<int> placed;
    for (int id : order)
    {
        if (id != outputId)
            for (auto& c : cables)
                if (c.toModule == id && c.isMod() && std::find (placed.begin(), placed.end(), c.fromModule) == placed.end()
                    && std::find (order.begin(), order.end(), c.fromModule) == order.end())
                    placed.push_back (c.fromModule);
        if (id != outputId)
            placed.push_back (id);
    }
    for (auto& m : modules)
        if (m.id != outputId && std::find (placed.begin(), placed.end(), m.id) == placed.end())
            placed.push_back (m.id);
    placed.push_back (outputId);

    float x = 12.0f;
    for (int id : placed)
        if (auto* m = find (id))
        {
            m->position = { x, top };
            x += moduleInfo (m->type).width + gap;
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
        *this = Patch();
}
