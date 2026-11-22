#include "DeviceWallModel.h"

namespace Wall
{
namespace
{
    constexpr juce::uint32 BL = 0xff38a8ff, PK = 0xffff3fa4, YL = 0xffffd23a, CY = 0xff3ff0ff,
                           VI = 0xffb06bff, OR = 0xffff8a3a, GR = 0xff4ade80, RD = 0xffff5a5a;

    const DeviceInfo devices[numDevices] = {
        { "synth",     "Synth",      Kind::Instrument, "Analog-modelled voice",      false, ModuleType::Synth,      SeqType::Clock,     { VI, PK, CY } },
        { "sampler",   "Sampler",    Kind::Instrument, "Chop, slice and play",       false, ModuleType::Sampler,    SeqType::Clock,     { PK, YL, BL } },
        { "drums",     "Drum Rack",  Kind::Instrument, "16 pads, step grid",         false, ModuleType::Drums,      SeqType::Clock,     { OR, YL, PK } },
        { "filter",    "Filter",     Kind::Effect,     "Ladder low / high pass",     false, ModuleType::Filter,     SeqType::Clock,     { CY, BL, PK } },
        { "delay",     "Delay",      Kind::Effect,     "Tempo-synced echoes",        false, ModuleType::Delay,      SeqType::Clock,     { BL, CY, YL } },
        { "reverb",    "Reverb",     Kind::Effect,     "Rooms to halls",             false, ModuleType::Reverb,     SeqType::Clock,     { CY, VI, PK } },
        { "eq",        "EQ",         Kind::Effect,     "Four-band parametric",       false, ModuleType::EQ,         SeqType::Clock,     { YL, OR, CY } },
        { "comp",      "Compressor", Kind::Effect,     "Glue and punch",             false, ModuleType::Compressor, SeqType::Clock,     { GR, YL, PK } },
        { "drive",     "Drive",      Kind::Effect,     "Tube, tape, fuzz, fold",     false, ModuleType::Drive,      SeqType::Clock,     { OR, PK, YL } },
        { "lfo",       "LFO",        Kind::Modulator,  "Modulate any knob",          false, ModuleType::LFO,        SeqType::Clock,     { VI, CY, PK } },
        { "clock",     "Clock",      Kind::RackModule, "The rack's pulse and swing", true,  ModuleType::Filter,     SeqType::Clock,     { BL, CY, PK } },
        { "steps",     "Steps",      Kind::RackModule, "16 steps, five directions",  true,  ModuleType::Filter,     SeqType::Steps,     { PK, YL, CY } },
        { "euclid",    "Euclid",     Kind::RackModule, "Hits spread round the circle", true, ModuleType::Filter,    SeqType::Euclid,    { CY, PK, YL } },
        { "turing",    "Turing",     Kind::RackModule, "Shift-register melodies",    true,  ModuleType::Filter,     SeqType::Turing,    { GR, CY, VI } },
        { "burst",     "Burst",      Kind::RackModule, "Ratchets from one trigger",  true,  ModuleType::Filter,     SeqType::Burst,     { YL, OR, PK } },
        { "chance",    "Chance",     Kind::RackModule, "Probability gate and drift", true,  ModuleType::Filter,     SeqType::Chance,    { PK, VI, YL } },
        { "quantise",  "Quantise",   Kind::RackModule, "Onto any scale",             true,  ModuleType::Filter,     SeqType::Quantise,  { BL, YL, PK } },
        { "mutate",    "Mutate",     Kind::RackModule, "Slow pitch wander",          true,  ModuleType::Filter,     SeqType::Mutate,    { GR, PK, YL } },
        { "circadian", "Circadian",  Kind::RackModule, "Day-and-night modulation",   true,  ModuleType::Filter,     SeqType::Circadian, { OR, YL, VI } },
        { "tide",      "Tide",       Kind::RackModule, "Synced LFO, a 16th to 8 bars", true, ModuleType::Filter,    SeqType::Tide,      { BL, CY, YL } },
        { "out",       "Out",        Kind::RackModule, "The rack's notes to the track", true, ModuleType::Filter,   SeqType::Out,       { CY, GR, PK } },
        { "gas",       "Gas Tube",   Kind::Instrument, "Neon tubes that strike and buzz", false, ModuleType::GasTube, SeqType::Clock,    { OR, PK, YL } },
        { "crate",     "Crate Digger", Kind::Instrument, "Digs slices out of a record", false, ModuleType::CrateDigger, SeqType::Clock, { PK, YL, BL } },
        { "strings",   "String Wall", Kind::Instrument, "Pluck, bow or feed back",   false, ModuleType::StringWall, SeqType::Clock,     { CY, VI, YL } },
        { "ghost",     "Ghost Tag",  Kind::Effect,     "Echoes that fade and drift", false, ModuleType::GhostTag,   SeqType::Clock,     { VI, CY, PK } },
        { "crowd",     "Crowd",      Kind::Effect,     "One sound sung by a crowd",  false, ModuleType::Crowd,      SeqType::Clock,     { YL, PK, CY } },
        { "xfmr",      "Transformer", Kind::Effect,    "Circuit bending, broken parts", false, ModuleType::Transformer, SeqType::Clock,  { GR, OR, CY } },
    };

    int indexOfId (const juce::String& id)
    {
        for (int i = 0; i < numDevices; ++i)
            if (id == devices[i].id)
                return i;
        return -1;
    }
}

const DeviceInfo& device (int index) { return devices[juce::jlimit (0, numDevices - 1, index)]; }

const char* kindName (Kind k)
{
    switch (k)
    {
        case Kind::Instrument: return "Instrument";
        case Kind::Effect:     return "Effect";
        case Kind::Modulator:  return "Modulator";
        case Kind::RackModule: return "Rack module";
    }
    return "";
}

const char* styleName (int style)
{
    static const char* names[] = { "Tubes", "Marquee", "Block" };
    return names[juce::jlimit (0, (int) numStyles - 1, style)];
}

std::array<juce::uint32, 3> rainbowColours (int index, int style)
{
    static const juce::uint32 hot[5][3]  = { { OR, YL, PK }, { YL, PK, VI }, { PK, OR, YL }, { RD, YL, OR }, { VI, PK, YL } };
    static const juce::uint32 wall[4][3] = { { BL, PK, YL }, { PK, BL, YL }, { CY, PK, YL }, { YL, BL, PK } };
    const juce::uint32* c = style == marquee ? hot[index % 5] : style == block ? wall[index % 4] : device (index).rainbow;
    return { c[0], c[1], c[2] };
}

// Skyline packing: each piece drops into the lowest gap that fits, slightly overlapped, big and small mixed,
// so the wall reads as one piece of work rather than a sorted list.
std::array<Piece, numDevices> defaultLayout()
{
    static const char* order[numDevices] = { "synth", "clock", "filter", "steps", "sampler", "euclid", "delay", "turing", "drums", "reverb", "burst",
                                             "chance", "eq", "quantise", "comp", "mutate", "drive", "lfo", "circadian", "tide", "out",
                                             "gas", "ghost", "crate", "crowd", "strings", "xfmr" };
    static const float tilt[8] = { -3.0f, 2.0f, -1.5f, 3.0f, -2.5f, 1.0f, 2.5f, -1.0f };

    std::array<Piece, numDevices> out;
    const int columns = (int) (designWidth / 10.0f);
    std::vector<float> sky ((size_t) columns, 0.0f);

    for (int n = 0; n < numDevices; ++n)
    {
        const int i = indexOfId (order[n]);
        const auto kind = devices[i].kind;
        const float w = kind == Kind::Instrument ? 430.0f : kind == Kind::RackModule ? 215.0f : 290.0f;
        const float footW = w * 0.9f, footH = w * aspect * 0.9f;
        const int span = (int) std::ceil (footW / 10.0f);

        int bestColumn = 0;
        float bestY = 1.0e9f;
        for (int c = 0; c + span <= columns; ++c)
        {
            float y = 0.0f;
            for (int j = c; j < c + span; ++j)
                y = juce::jmax (y, sky[(size_t) j]);
            if (y < bestY - 1.0f) { bestY = y; bestColumn = c; }
        }
        for (int j = bestColumn; j < bestColumn + span; ++j)
            sky[(size_t) j] = bestY + footH;

        auto& p = out[(size_t) i];
        p.w = w;
        p.x = std::round (juce::jlimit (0.0f, designWidth - w, (float) bestColumn * 10.0f - (w - footW) * 0.5f));
        p.y = std::round (bestY);
        p.rotation = tilt[n % 8];
        p.style = tubes;
        p.title = {};
    }
    return out;
}

void Model::resetLayout() { pieces = defaultLayout(); }

juce::String Model::titleOf (int index) const
{
    const auto& t = pieces[(size_t) index].title;
    return t.isNotEmpty() ? t : juce::String (device (index).name);
}

float Model::contentHeight() const
{
    float h = 0.0f;
    for (int i = 0; i < numDevices; ++i)
        h = juce::jmax (h, pieces[(size_t) i].y + height (i));
    return h + 30.0f;
}

bool Model::matches (int index, const juce::String& query) const
{
    if (query.isEmpty())
        return true;
    const auto& d = device (index);
    return (titleOf (index) + " " + d.name + " " + kindName (d.kind)).containsIgnoreCase (query);
}

int Model::firstMatch (const juce::String& query) const
{
    for (int i = 0; i < numDevices; ++i)
        if (matches (i, query))
            return i;
    return -1;
}

int Model::neighbour (int from, int dx, int dy, const juce::String& query) const
{
    auto centre = [this] (int i) { const auto& p = pieces[(size_t) i]; return juce::Point<float> (p.x + p.w * 0.5f, p.y + p.w * aspect * 0.5f); };
    const auto here = centre (from);
    int best = from;
    float bestScore = 1.0e9f;

    for (int i = 0; i < numDevices; ++i)
    {
        if (i == from || ! matches (i, query))
            continue;
        const auto d = centre (i) - here;
        const float along = d.x * (float) dx + d.y * (float) dy;
        const float across = std::abs (d.x * (float) dy) + std::abs (d.y * (float) dx);
        if (along <= 10.0f)
            continue;
        const float score = along + across * 2.2f;
        if (score < bestScore) { bestScore = score; best = i; }
    }
    return best;
}

int Model::commonStyle() const
{
    for (const auto& p : pieces)
        if (p.style != pieces[0].style)
            return -1;
    return pieces[0].style;
}

juce::String Model::toJson() const
{
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty ("version", 1);
    root->setProperty ("bright", alwaysBright);
    root->setProperty ("glow", glow);
    root->setProperty ("zoom", zoom);
    root->setProperty ("fit", fitToScreen);

    juce::DynamicObject::Ptr all = new juce::DynamicObject();
    for (int i = 0; i < numDevices; ++i)
    {
        const auto& p = pieces[(size_t) i];
        juce::DynamicObject::Ptr o = new juce::DynamicObject();
        o->setProperty ("x", p.x);
        o->setProperty ("y", p.y);
        o->setProperty ("w", p.w);
        o->setProperty ("r", p.rotation);
        o->setProperty ("style", p.style);
        if (p.title.isNotEmpty())
            o->setProperty ("title", p.title);
        all->setProperty (device (i).id, o.get());
    }
    root->setProperty ("pieces", all.get());
    return juce::JSON::toString (juce::var (root.get()), true);
}

bool Model::fromJson (const juce::String& text)
{
    const auto root = juce::JSON::parse (text);
    if (! root.isObject())
        return false;

    alwaysBright = (bool) root.getProperty ("bright", false);
    glow = juce::jlimit (0.0f, 1.5f, (float) root.getProperty ("glow", 1.0));
    zoom = juce::jlimit (0.3f, 2.0f, (float) root.getProperty ("zoom", 1.0));
    fitToScreen = (bool) root.getProperty ("fit", false);

    // Anything missing or broken keeps its default place, so a new device still shows up on an old wall
    const auto fresh = defaultLayout();
    const auto all = root["pieces"];
    for (int i = 0; i < numDevices; ++i)
    {
        auto p = fresh[(size_t) i];
        if (const auto o = all[device (i).id]; o.isObject())
        {
            p.w = juce::jlimit (minPieceWidth, designWidth, (float) o.getProperty ("w", p.w));
            p.x = juce::jlimit (-p.w * 0.3f, designWidth - p.w * 0.7f, (float) o.getProperty ("x", p.x));
            p.y = juce::jlimit (0.0f, 20000.0f, (float) o.getProperty ("y", p.y));
            p.rotation = juce::jlimit (-45.0f, 45.0f, (float) o.getProperty ("r", p.rotation));
            p.style = juce::jlimit (0, (int) numStyles - 1, (int) o.getProperty ("style", 0));
            p.title = o.getProperty ("title", juce::String()).toString().substring (0, 18);
        }
        pieces[(size_t) i] = p;
    }
    return true;
}
}
