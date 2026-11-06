#include "Patch.h"

const ModuleInfo& moduleInfo (ModuleType t)
{
    // Pal indices: Rose 0, Green 1, Sky 2, Gold 3, Violet 4, Blue 5, Amber 6, Ice 7
    static const ModuleInfo input      { "Track In",   1, false, true,  false, {}, 74.0f };
    static const ModuleInfo output     { "Out",        1, true,  false, false, {}, 74.0f };
    static const ModuleInfo sampler    { "Sampler",    0, true,  true,  false, { { "Start", 0.1f }, { "Length", 0.7f }, { "Pitch", 0.5f }, { "Attack", 0.05f }, { "Release", 0.4f }, { "Gain", 0.8f } }, 230.0f };
    static const ModuleInfo synth      { "Synth",      4, true,  true,  false, { { "Wave", 0.3f }, { "Detune", 0.2f }, { "Cutoff", 0.6f }, { "Attack", 0.1f }, { "Release", 0.5f }, { "Gain", 0.8f } }, 230.0f };
    static const ModuleInfo drums      { "Drum Rack",  6, true,  true,  false, { { "Tune", 0.5f }, { "Decay", 0.5f }, { "Swing", 0.1f }, { "Gain", 0.8f } }, 200.0f };
    static const ModuleInfo filter     { "Filter",     2, true,  true,  false, { { "Cutoff", 0.65f }, { "Res", 0.3f }, { "Drive", 0.2f } }, 170.0f };
    static const ModuleInfo delay      { "Delay",      5, true,  true,  false, { { "Time", 0.5f }, { "Feedback", 0.4f }, { "Mix", 0.25f } }, 170.0f };
    static const ModuleInfo reverb     { "Reverb",     7, true,  true,  false, { { "Size", 0.6f }, { "Damp", 0.4f }, { "Mix", 0.3f } }, 170.0f };
    static const ModuleInfo eq         { "EQ",         3, true,  true,  false, { { "Low", 0.1f }, { "Mid", -0.2f }, { "High", 0.3f }, { "Output", 0.7f } }, 200.0f };
    static const ModuleInfo compressor { "Compressor", 1, true,  true,  false, { { "Thresh", 0.5f }, { "Ratio", 0.4f }, { "Attack", 0.2f }, { "Makeup", 0.5f } }, 200.0f };
    static const ModuleInfo lfo        { "LFO",        4, false, false, true,  { { "Rate", 0.4f }, { "Depth", 0.6f }, { "Shape", 0.2f } }, 170.0f };

    switch (t)
    {
        case ModuleType::Input:      return input;
        case ModuleType::Output:     return output;
        case ModuleType::Sampler:    return sampler;
        case ModuleType::Synth:      return synth;
        case ModuleType::Drums:      return drums;
        case ModuleType::Filter:     return filter;
        case ModuleType::Delay:      return delay;
        case ModuleType::Reverb:     return reverb;
        case ModuleType::EQ:         return eq;
        case ModuleType::Compressor: return compressor;
        case ModuleType::LFO:        return lfo;
    }
    return filter;
}

Patch::Patch()
{
    modules.push_back ({ inputId, ModuleType::Input, { 12.0f, top }, {} });
    modules.push_back ({ outputId, ModuleType::Output, { 12.0f + 74.0f + gap, top }, {} });
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
    Module m;
    m.id = nextId++;
    m.type = type;
    m.position = nextFreePosition();
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
