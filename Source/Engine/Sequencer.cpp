#include "Sequencer.h"
#include <cmath>
#include <map>

namespace
{
    constexpr float stepBeatsBase = 0.25;   // ×1 on the clock = sixteenth notes

    using K = SeqKnobInfo;
    using P = SeqPortInfo;

    const juce::StringArray divisions   { "/4", "/2", "x1", "x2", "x4" };
    const juce::StringArray octaves     { "-2", "-1", "0", "+1", "+2" };
    const juce::StringArray directions  { "Fwd", "Rev", "Pend", "Brown", "Rnd" };
    const juce::StringArray onOff       { "Off", "On" };
    const juce::StringArray turingLens  { "2", "3", "4", "5", "6", "8", "12", "16" };
    const juce::StringArray burstRates  { "/2", "/3", "/4", "/6", "/8" };
    const juce::StringArray rootNames   { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const juce::StringArray quantModes  { "Nearest", "Up", "Down" };
    const juce::StringArray ranges      { "1 oct", "2 oct", "3 oct", "4 oct" };
    const juce::StringArray periods     { "1 bar", "2 bars", "4 bars", "8 bars", "16 bars", "32 bars", "64 bars" };
    const juce::StringArray shapes      { "Sine", "Tri", "Ramp", "Sun" };
    const juce::StringArray follows     { "Song", "Clock", "Daylight" };
    const juce::StringArray tideRates   { "1/16", "1/8", "1/4", "1/2", "1 bar", "2 bars", "4 bars", "8 bars" };
    const juce::StringArray tideShapes  { "Sine", "Tri", "Ramp", "Square", "S&H" };
    const juce::StringArray voices      { "Mono", "Poly 4" };

    juce::StringArray scaleNames()
    {
        juce::StringArray s;
        for (auto& sc : SeqRack::scales())
            s.add (sc.name);
        return s;
    }

    const SeqInfo& infoFor (SeqType t)
    {
        // Pal: Rose 0, Green 1, Sky 2, Gold 3, Violet 4, Blue 5, Amber 6, Ice 7
        static const SeqInfo infos[numSeqTypes] = {
            { "Clock", 1, 150.0f,
              { K { "Div", 0.5f, divisions }, K { "Swing", 0.0f, {} }, K { "Run", 0.0f, { "Song", "Free" } } },
              { P { "Reset", false, true }, P { "Clk", false, true }, P { "Bar", false, true } },
              "Sixteenths from the song, or free-running" },
            { "Steps", 2, 620.0f,
              { K { "Gate len", 0.5f, {} }, K { "Slide", 0.0f, {} }, K { "Octave", 0.5f, octaves },
                K { "Dir", 0.0f, directions }, K { "Length", 1.0f, {} } },
              { P { "Clk", true, true }, P { "Rst", true, true }, P { "Dir cv", true, false }, P { "Prob cv", true, false }, P { "Len cv", true, false },
                P { "Pitch", false, false }, P { "Gate", false, true }, P { "Vel", false, false }, P { "Slide", false, false } },
              "16 steps of pitch and gate, five directions" },
            { "Euclid", 3, 190.0f,
              { K { "Hits", 5.0f / 16.0f, {} }, K { "Steps", 1.0f, {} }, K { "Rotate", 0.0f, {} }, K { "Gate", 0.5f, {} } },
              { P { "Clk", true, true }, P { "Rst", true, true }, P { "Hits cv", true, false },
                P { "Gate", false, true }, P { "Acc", false, true } },
              "Hits spread evenly round the circle" },
            { "Turing", 4, 190.0f,
              { K { "Prob", 0.3f, {} }, K { "Length", 5.0f / 7.0f, turingLens }, K { "Range", 0.5f, {} }, K { "Gate", 0.5f, {} } },
              { P { "Clk", true, true }, P { "Rst", true, true }, P { "Prob cv", true, false },
                P { "Cv", false, false }, P { "Gate", false, true }, P { "Pulse", false, true } },
              "Shift-register melodies that evolve or lock" },
            { "Burst", 6, 170.0f,
              { K { "Count", 2.0f / 7.0f, {} }, K { "Rate", 0.5f, burstRates }, K { "Decay", 0.5f, {} }, K { "Gate", 0.4f, {} } },
              { P { "Trig", true, true }, P { "Gate", false, true }, P { "Vel", false, false } },
              "Ratchets: a burst of gates from one trigger" },
            { "Chance", 0, 190.0f,
              { K { "Prob", 0.75f, {} }, K { "Drift", 0.0f, {} }, K { "Memory", 0.0f, {} }, K { "Lock", 0.0f, onOff } },
              { P { "Trig", true, true }, P { "Prob cv", true, false }, P { "Cv", true, false },
                P { "Gate", false, true }, P { "Out", false, false } },
              "Let gates through by probability; drift the pitch" },
            { "Quantise", 7, 236.0f,
              { K { "Root", 2.0f / 11.0f, rootNames }, K { "Scale", 2.0f / 11.0f, scaleNames() }, K { "Range", 1.0f / 3.0f, ranges }, K { "Mode", 0.0f, quantModes } },
              { P { "In", true, false }, P { "Trig", true, true }, P { "Out", false, false }, P { "Chg", false, true } },
              "Pull any pitch onto a scale" },
            { "Mutate", 4, 170.0f,
              { K { "Amount", 0.3f, {} }, K { "Rate", 0.5f, {} }, K { "Smooth", 0.0f, {} } },
              { P { "Cv", true, false }, P { "Trig", true, true }, P { "Out", false, false } },
              "Slowly wander away from the pitch coming in" },
            { "Circadian", 6, 310.0f,
              { K { "Period", 4.0f / 6.0f, periods }, K { "Phase", 0.0f, {} }, K { "Depth", 1.0f, {} }, K { "Shape", 0.0f, shapes },
                K { "Follow", 0.0f, follows }, K { "Inv", 0.0f, onOff } },
              { P { "Sync", true, true }, P { "Cv", false, false }, P { "Dawn", false, true }, P { "Noon", false, true }, P { "Dusk", false, true }, P { "Inv", false, false } },
              "A slow day-and-night cycle: song bars, the wall clock or real daylight" },
            { "Tide", 2, 190.0f,
              { K { "Rate", 4.0f / 7.0f, tideRates }, K { "Shape", 0.0f, tideShapes }, K { "Depth", 1.0f, {} }, K { "Offset", 0.5f, {} } },
              { P { "Sync", true, true }, P { "Rate cv", true, false }, P { "Cv", false, false }, P { "Gate", false, true }, P { "Inv", false, false } },
              "Tempo-synced LFO, from a sixteenth to eight bars" },
            { "Out", 1, 170.0f,
              { K { "Voices", 0.0f, voices }, K { "Octave", 0.5f, octaves }, K { "Mod amt", 1.0f, {} } },
              { P { "Pitch", true, false }, P { "Gate", true, true }, P { "Vel", true, false }, P { "Mod", true, false } },
              "Plays the track's instrument" },
        };
        return infos[(int) t];
    }

    double stepBeatsFor (int divChoice)
    {
        static const double mult[] = { 4.0, 2.0, 1.0, 0.5, 0.25 };
        return stepBeatsBase * mult[juce::jlimit (0, 4, divChoice)];
    }

    float shapeValue (int shape, double phase)   // unipolar 0..1, phase 0 = midnight
    {
        const double p = phase - std::floor (phase);
        switch (shape)
        {
            case 0:  return (float) (0.5 - 0.5 * std::cos (p * juce::MathConstants<double>::twoPi));
            case 1:  return (float) (1.0 - std::abs (p * 2.0 - 1.0));
            case 2:  return (float) p;
            default: return (float) juce::jmax (0.0, -std::cos (p * juce::MathConstants<double>::twoPi));
        }
    }
}

//==============================================================================
int SeqInfo::numInputs() const
{
    int n = 0;
    for (auto& p : ports)
        if (p.input) ++n;
    return n;
}

int SeqInfo::portIndex (const juce::String& name) const
{
    for (size_t i = 0; i < ports.size(); ++i)
        if (ports[i].name == name)
            return (int) i;
    return -1;
}

const SeqInfo& seqInfo (SeqType t) { return infoFor (t); }

const std::vector<SeqRack::Scale>& SeqRack::scales()
{
    static const std::vector<Scale> s = {
        { "Major",      { 1,0,1,0,1,1,0,1,0,1,0,1 } },
        { "Minor",      { 1,0,1,1,0,1,0,1,1,0,1,0 } },
        { "Dorian",     { 1,0,1,1,0,1,0,1,0,1,1,0 } },
        { "Phrygian",   { 1,1,0,1,0,1,0,1,1,0,1,0 } },
        { "Lydian",     { 1,0,1,0,1,0,1,1,0,1,0,1 } },
        { "Mixolydian", { 1,0,1,0,1,1,0,1,0,1,1,0 } },
        { "Harm minor", { 1,0,1,1,0,1,0,1,1,0,0,1 } },
        { "Pent major", { 1,0,1,0,1,0,0,1,0,1,0,0 } },
        { "Pent minor", { 1,0,0,1,0,1,0,1,0,0,1,0 } },
        { "Blues",      { 1,0,0,1,0,1,1,1,0,0,1,0 } },
        { "Whole tone", { 1,0,1,0,1,0,1,0,1,0,1,0 } },
        { "Chromatic",  { 1,1,1,1,1,1,1,1,1,1,1,1 } },
    };
    return s;
}

const char* SeqRack::noteName (int semitone)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return names[((semitone % 12) + 12) % 12];
}

//==============================================================================
namespace
{
    SeqModule makeModule (int id, SeqType type)
    {
        SeqModule m;
        m.id = id;
        m.type = type;
        for (auto& kn : seqInfo (type).knobs)
            m.knobs.push_back (kn.defaultValue);
        m.stepPitch.fill (0);
        m.stepOn.fill (false);
        return m;
    }
}

SeqRack::SeqRack()
{
    modules.push_back (makeModule (clockId, SeqType::Clock));
    auto steps = makeModule (nextId++, SeqType::Steps);
    // A starting pattern: a minor-ish riff with rests, so a new rack plays something at once
    const int pitches[seqSteps] = { 0, 0, 7, 0, 3, 0, 7, 10, 0, 0, 7, 0, 3, 5, 7, 12 };
    const bool gates[seqSteps]  = { 1, 0, 1, 1, 1, 0, 1, 0, 1, 0, 1, 1, 1, 0, 1, 1 };
    for (int i = 0; i < seqSteps; ++i) { steps.stepPitch[(size_t) i] = pitches[i]; steps.stepOn[(size_t) i] = gates[i]; }
    modules.push_back (steps);
    modules.push_back (makeModule (nextId++, SeqType::Quantise));
    modules.push_back (makeModule (outId, SeqType::Out));

    const int stepsId = modules[1].id, quantId = modules[2].id;
    const auto& ci = seqInfo (SeqType::Clock); const auto& si = seqInfo (SeqType::Steps);
    const auto& qi = seqInfo (SeqType::Quantise); const auto& oi = seqInfo (SeqType::Out);
    connect (clockId, ci.portIndex ("Clk"), stepsId, si.portIndex ("Clk"));
    connect (clockId, ci.portIndex ("Reset"), stepsId, si.portIndex ("Rst"));
    connect (stepsId, si.portIndex ("Pitch"), quantId, qi.portIndex ("In"));
    connect (stepsId, si.portIndex ("Gate"), quantId, qi.portIndex ("Trig"));
    connect (quantId, qi.portIndex ("Out"), outId, oi.portIndex ("Pitch"));
    connect (stepsId, si.portIndex ("Gate"), outId, oi.portIndex ("Gate"));
    connect (stepsId, si.portIndex ("Vel"), outId, oi.portIndex ("Vel"));
}

SeqModule* SeqRack::find (int id)
{
    for (auto& m : modules)
        if (m.id == id) return &m;
    return nullptr;
}

const SeqModule* SeqRack::find (int id) const
{
    for (auto& m : modules)
        if (m.id == id) return &m;
    return nullptr;
}

int SeqRack::indexOf (int id) const
{
    for (size_t i = 0; i < modules.size(); ++i)
        if (modules[i].id == id) return (int) i;
    return -1;
}

int SeqRack::cableInto (int module, int port) const
{
    for (size_t i = 0; i < cables.size(); ++i)
        if (cables[i].toModule == module && cables[i].toPort == port)
            return (int) i;
    return -1;
}

std::vector<int> SeqRack::cablesFrom (int module, int port) const
{
    std::vector<int> r;
    for (size_t i = 0; i < cables.size(); ++i)
        if (cables[i].fromModule == module && cables[i].fromPort == port)
            r.push_back ((int) i);
    return r;
}

bool SeqRack::connect (int fromModule, int fromPort, int toModule, int toPort)
{
    auto* a = find (fromModule);
    auto* b = find (toModule);
    if (a == nullptr || b == nullptr || fromModule == toModule || fromPort < 0 || toPort < 0)
        return false;
    const auto& ai = seqInfo (a->type);
    const auto& bi = seqInfo (b->type);
    if (fromPort >= (int) ai.ports.size() || toPort >= (int) bi.ports.size() || ai.ports[(size_t) fromPort].input || ! bi.ports[(size_t) toPort].input)
        return false;
    const int existing = cableInto (toModule, toPort);
    if (existing >= 0)
        cables.erase (cables.begin() + existing);
    cables.push_back ({ fromModule, fromPort, toModule, toPort });
    return true;
}

void SeqRack::disconnect (size_t cableIndex)
{
    if (cableIndex < cables.size())
        cables.erase (cables.begin() + (long) cableIndex);
}

void SeqRack::move (int id, int newIndex)
{
    const int from = indexOf (id);
    if (from < 0 || id == clockId || id == outId)
        return;
    auto m = modules[(size_t) from];
    modules.erase (modules.begin() + from);
    newIndex = juce::jlimit (1, (int) modules.size() - 1, newIndex);
    modules.insert (modules.begin() + newIndex, m);
}

void SeqRack::insertInline (int id, int inPort, int outPort, int targetModule, int targetPort)
{
    const int feed = cableInto (targetModule, targetPort);
    if (feed >= 0)
    {
        const auto c = cables[(size_t) feed];
        connect (c.fromModule, c.fromPort, id, inPort);
    }
    connect (id, outPort, targetModule, targetPort);
}

int SeqRack::add (SeqType type)
{
    if (type == SeqType::Clock) return clockId;
    if (type == SeqType::Out) return outId;

    auto m = makeModule (nextId++, type);
    const int id = m.id;
    if (type == SeqType::Steps)
    {
        const int pitches[seqSteps] = { 0, 3, 7, 10, 12, 10, 7, 3, 0, 5, 7, 12, 15, 12, 7, 5 };
        for (int i = 0; i < seqSteps; ++i) { m.stepPitch[(size_t) i] = pitches[i]; m.stepOn[(size_t) i] = i % 2 == 0; }
    }
    // Keep the rail in signal order: clock, pattern makers, gate processors, pitch processors, modulators, out
    auto stage = [] (SeqType t)
    {
        switch (t)
        {
            case SeqType::Clock: return 0;
            case SeqType::Steps: case SeqType::Euclid: case SeqType::Turing: case SeqType::Burst: return 1;
            case SeqType::Chance: return 2;
            case SeqType::Quantise: case SeqType::Mutate: return 3;
            case SeqType::Circadian: case SeqType::Tide: return 4;
            case SeqType::Out: return 5;
        }
        return 4;
    };
    size_t at = modules.size();
    for (size_t i = 1; i < modules.size(); ++i)
        if (stage (modules[i].type) > stage (type)) { at = i; break; }
    modules.insert (modules.begin() + (long) at, m);

    const auto& info = seqInfo (type);
    const auto& ci = seqInfo (SeqType::Clock);
    const auto& oi = seqInfo (SeqType::Out);
    auto port = [&] (const char* name) { return info.portIndex (name); };

    // Something that plays notes: find the pitch path's first stop (a quantiser if there is one)
    int pitchTarget = outId, pitchPort = oi.portIndex ("Pitch");
    for (auto& other : modules)
        if (other.type == SeqType::Quantise && other.id != id)
        {
            pitchTarget = other.id;
            pitchPort = seqInfo (SeqType::Quantise).portIndex ("In");
            break;
        }

    switch (type)
    {
        case SeqType::Steps:
        case SeqType::Euclid:
        case SeqType::Turing:
            connect (clockId, ci.portIndex ("Clk"), id, port ("Clk"));
            connect (clockId, ci.portIndex ("Reset"), id, port ("Rst"));
            if (type == SeqType::Steps)
            {
                if (cableInto (outId, oi.portIndex ("Gate")) < 0) connect (id, port ("Gate"), outId, oi.portIndex ("Gate"));
                if (cableInto (pitchTarget, pitchPort) < 0)        connect (id, port ("Pitch"), pitchTarget, pitchPort);
            }
            else
            {
                connect (id, port ("Gate"), outId, oi.portIndex ("Gate"));
                if (type == SeqType::Turing)
                    connect (id, port ("Cv"), pitchTarget, pitchPort);
            }
            break;

        case SeqType::Chance:
        case SeqType::Burst:
            insertInline (id, port ("Trig"), port ("Gate"), outId, oi.portIndex ("Gate"));
            break;

        case SeqType::Quantise:
            insertInline (id, port ("In"), port ("Out"), outId, oi.portIndex ("Pitch"));
            if (const int g = cableInto (outId, oi.portIndex ("Gate")); g >= 0)
                connect (cables[(size_t) g].fromModule, cables[(size_t) g].fromPort, id, port ("Trig"));
            break;

        case SeqType::Mutate:
            insertInline (id, port ("Cv"), port ("Out"), pitchTarget, pitchPort);
            if (const int g = cableInto (outId, oi.portIndex ("Gate")); g >= 0)
                connect (cables[(size_t) g].fromModule, cables[(size_t) g].fromPort, id, port ("Trig"));
            break;

        case SeqType::Circadian:
        case SeqType::Tide:
        {
            // First free cv input, most musical first
            const std::pair<SeqType, const char*> targets[] = {
                { SeqType::Chance, "Prob cv" }, { SeqType::Euclid, "Hits cv" }, { SeqType::Steps, "Prob cv" },
                { SeqType::Turing, "Prob cv" }, { SeqType::Steps, "Len cv" }, { SeqType::Tide, "Rate cv" } };
            bool done = false;
            for (auto& t : targets)
            {
                for (auto& other : modules)
                {
                    if (other.type != t.first || other.id == id) continue;
                    const int p = seqInfo (t.first).portIndex (t.second);
                    if (cableInto (other.id, p) < 0) { connect (id, port ("Cv"), other.id, p); done = true; break; }
                }
                if (done) break;
            }
            if (! done)
                connect (id, port ("Cv"), outId, oi.portIndex ("Mod"));
            break;
        }

        case SeqType::Clock: case SeqType::Out:
            break;
    }
    return id;
}

void SeqRack::remove (int id)
{
    if (id == clockId || id == outId)
        return;
    auto* m = find (id);
    if (m == nullptr)
        return;
    const auto& info = seqInfo (m->type);

    // Heal: whatever fed the first input carries on to wherever the first output went
    const int firstIn = 0, firstOut = info.firstOutput();
    const int feed = cableInto (id, firstIn);
    if (feed >= 0 && firstOut < (int) info.ports.size())
    {
        const auto src = cables[(size_t) feed];
        for (int ci : cablesFrom (id, firstOut))
        {
            const auto dst = cables[(size_t) ci];
            cables.push_back ({ src.fromModule, src.fromPort, dst.toModule, dst.toPort });
        }
    }
    cables.erase (std::remove_if (cables.begin(), cables.end(), [id] (const SeqCable& c) { return c.fromModule == id || c.toModule == id; }),
                  cables.end());
    modules.erase (modules.begin() + indexOf (id));
}

//==============================================================================
SeqNode::SeqNode (const SeqModule& m) : id (m.id), type (m.type), info (seqInfo (m.type))
{
    for (size_t i = 0; i < knob.size(); ++i)
        knob[i] = i < m.knobs.size() ? m.knobs[i] : 0.0f;
    for (size_t i = 0; i < seqSteps; ++i) { stepPitch[i] = m.stepPitch[i]; stepOn[i] = m.stepOn[i]; }
    for (auto& d : display) d = 0.0f;
    in.assign (info.ports.size(), 0.0f);
    plugged.assign (info.ports.size(), false);
    out.assign (info.ports.size(), 0.0f);
    wasHigh.assign (info.ports.size(), false);
}

bool SeqNode::rose (int input)
{
    const bool high = in[(size_t) input] > 0.5f;
    const bool r = high && ! wasHigh[(size_t) input];
    wasHigh[(size_t) input] = high;
    return r;
}

namespace
{
    // Holds a gate high for a number of ticks
    struct GateTimer
    {
        int left = 0;
        void fire (int ticks) { left = juce::jmax (1, ticks); }
        float advance() { if (left > 0) { --left; return 1.0f; } return 0.0f; }
    };

    // Measures the ticks between incoming clocks so gates can be a fraction of a step
    struct Interval
    {
        int ticks = 0, measured = 48;
        void tick() { ++ticks; }
        void clocked() { if (ticks > 0) measured = ticks; ticks = 0; }
        int fraction (float f) const { return juce::jmax (1, (int) std::lround ((float) measured * juce::jlimit (0.02f, 1.0f, f))); }
    };

    juce::Random& rng() { static juce::Random r; return r; }

    //==========================================================================
    struct ClockNode : SeqNode
    {
        using SeqNode::SeqNode;
        int lastStep = -1, lastBar = -1;
        double freeBeat = 0.0, lastBeat = -1.0;
        bool running = false;

        void process (const SeqTick& t) override
        {
            const int reset = 0, clk = 1, bar = 2;
            out[clk] = out[bar] = out[reset] = 0.0f;
            const bool free = choice (2) == 1;
            double beat;
            bool jumped = t.reset;
            if (t.beat >= 0.0)
            {
                beat = t.beat;
                if (! running) jumped = true;
                running = true;
            }
            else if (free)
            {
                if (running && lastBeat >= 0.0) freeBeat = lastBeat;   // carry on from where the song stopped
                if (running) jumped = true;
                running = false;
                freeBeat += t.beatsPerTick;
                beat = freeBeat;
            }
            else
            {
                running = false;
                lastBeat = -1.0;
                display[0] = 0.0f;
                return;
            }

            const double stepBeats = stepBeatsFor (choice (0));
            const float swing = k (1);
            if (jumped || beat < lastBeat)
            {
                lastStep = (int) std::floor (beat / stepBeats) - 1;
                lastBar = (int) std::floor (beat / 4.0) - 1;
                out[reset] = 1.0f;
            }
            // A step starts at its grid line; odd steps are pushed late by the swing
            const int next = lastStep + 1;
            const double start = next * stepBeats + ((next & 1) ? swing * 0.5 * stepBeats : 0.0);
            if (beat >= start)
            {
                out[clk] = 1.0f;
                lastStep = next;
            }
            const int b = (int) std::floor (beat / 4.0);
            if (b != lastBar) { out[bar] = 1.0f; lastBar = b; }
            lastBeat = beat;
            display[0] = (float) (beat - std::floor (beat));
            if (out[clk] > 0.5f) display[1] = 1.0f;
        }
    };

    //==========================================================================
    struct StepsNode : SeqNode
    {
        using SeqNode::SeqNode;
        int pos = -1, pendDir = 1;
        GateTimer gate;
        Interval interval;
        float pitchCv = 0.0f, targetPitch = 0.0f, glide = 0.0f;
        bool started = false;

        void reset() override { pos = -1; started = false; pendDir = 1; }

        void process (const SeqTick&) override
        {
            enum { clk, rst, dirCv, probCv, lenCv, oPitch, oGate, oVel, oSlide };
            interval.tick();
            if (rose (rst)) reset();
            if (rose (clk))
            {
                interval.clocked();
                const int length = juce::jlimit (1, seqSteps, SeqKnob::count (juce::jlimit (0.0f, 1.0f, k (4) + (plugged[lenCv] ? in[lenCv] - 0.5f : 0.0f)), 1, seqSteps));
                int dir = choice (3) + (plugged[dirCv] ? (int) std::lround (in[dirCv] * 4.0f) : 0);
                dir = ((dir % 5) + 5) % 5;
                if (! started) { pos = dir == 1 ? length - 1 : 0; started = true; }
                else switch (dir)
                {
                    case 0: pos = (pos + 1) % length; break;
                    case 1: pos = (pos - 1 + length) % length; break;
                    case 2:
                        if (length == 1) { pos = 0; break; }
                        if (pos + pendDir < 0 || pos + pendDir >= length) pendDir = -pendDir;
                        pos += pendDir;
                        break;
                    case 3: pos = (pos + (rng().nextBool() ? 1 : length - 1)) % length; break;
                    default: pos = rng().nextInt (length); break;
                }
                pos = juce::jlimit (0, length - 1, pos);

                const float prob = plugged[probCv] ? juce::jlimit (0.0f, 1.0f, in[probCv]) : 1.0f;
                if (stepOn[(size_t) pos].load() && rng().nextFloat() < prob)
                {
                    const float slide = k (1);
                    const bool legato = slide > 0.01f && gate.left > 0;
                    gate.fire (interval.fraction (legato ? 1.0f : k (0)));
                    targetPitch = (float) stepPitch[(size_t) pos].load() + 12.0f * (float) (choice (2) - 2);
                    glide = legato ? 1.0f / (float) juce::jmax (1, (int) (slide * (float) interval.measured)) : 1.0f;
                    out[oVel] = pos == 0 ? 1.0f : 0.8f;
                }
                display[1] = 1.0f;
            }
            pitchCv += (targetPitch - pitchCv) * juce::jmin (1.0f, glide);
            out[oPitch] = pitchCv;
            out[oGate] = gate.advance();
            out[oSlide] = k (1);
            display[0] = (float) pos;
            display[2] = out[oGate];
        }
    };

    //==========================================================================
    struct EuclidNode : SeqNode
    {
        using SeqNode::SeqNode;
        int pos = -1;
        GateTimer gate, acc;
        Interval interval;

        void reset() override { pos = -1; }

        static bool hit (int i, int hits, int steps, int rotate)
        {
            if (hits <= 0 || steps <= 0) return false;
            const int s = ((i + rotate) % steps + steps) % steps;
            return (s * hits) % steps < hits;
        }

        void process (const SeqTick&) override
        {
            enum { clk, rst, hitsCv, oGate, oAcc };
            interval.tick();
            if (rose (rst)) reset();
            const int steps = SeqKnob::count (k (1), 1, seqSteps);
            const int hits = juce::jlimit (0, steps, SeqKnob::count (juce::jlimit (0.0f, 1.0f, k (0) + (plugged[hitsCv] ? in[hitsCv] - 0.5f : 0.0f)), 0, seqSteps));
            const int rotate = SeqKnob::count (k (2), 0, seqSteps - 1);
            if (rose (clk))
            {
                interval.clocked();
                pos = (pos + 1) % steps;
                if (hit (pos, hits, steps, rotate))
                {
                    gate.fire (interval.fraction (k (3)));
                    if (pos == 0) acc.fire (interval.fraction (k (3)));
                }
            }
            out[oGate] = gate.advance();
            out[oAcc] = acc.advance();
            display[0] = (float) pos;
            display[1] = (float) hits;
            display[2] = (float) steps;
            display[3] = (float) rotate;
        }
    };

    //==========================================================================
    struct TuringNode : SeqNode
    {
        using SeqNode::SeqNode;
        uint16_t reg = 0b1011001000110100;
        GateTimer gate, pulse;
        Interval interval;

        void process (const SeqTick&) override
        {
            enum { clk, rst, probCv, oCv, oGate, oPulse };
            interval.tick();
            static const int lens[] = { 2, 3, 4, 5, 6, 8, 12, 16 };
            const int len = lens[choice (1)];
            if (rose (rst)) reg = 0b1011001000110100;
            if (rose (clk))
            {
                interval.clocked();
                const float prob = juce::jlimit (0.0f, 1.0f, k (0) + (plugged[probCv] ? in[probCv] - 0.5f : 0.0f));
                bool bit = (reg >> (len - 1)) & 1;
                if (rng().nextFloat() < prob) bit = ! bit;
                reg = (uint16_t) ((reg << 1) | (bit ? 1 : 0));
                if (reg & 1) gate.fire (interval.fraction (k (3)));
                if ((reg & 2) && ! (reg & 1)) pulse.fire (interval.fraction (k (3)));
            }
            const float range = k (2) * 24.0f;
            out[oCv] = (float) (reg & 0xff) / 255.0f * range;
            out[oGate] = gate.advance();
            out[oPulse] = pulse.advance();
            display[0] = (float) (reg & 0xff);
            display[1] = out[oCv];
        }
    };

    //==========================================================================
    struct BurstNode : SeqNode
    {
        using SeqNode::SeqNode;
        Interval interval;
        GateTimer gate;
        int remaining = 0, spacing = 1, countdown = 0, total = 1;

        void process (const SeqTick&) override
        {
            enum { trig, oGate, oVel };
            interval.tick();
            static const int rates[] = { 2, 3, 4, 6, 8 };
            if (rose (trig))
            {
                interval.clocked();
                total = SeqKnob::count (k (0), 1, 8);
                remaining = total;
                spacing = juce::jmax (1, interval.measured / rates[choice (1)]);
                countdown = 0;
            }
            if (remaining > 0 && countdown-- <= 0)
            {
                const int index = total - remaining;
                gate.fire (juce::jmax (1, (int) ((float) spacing * juce::jlimit (0.1f, 0.9f, k (3)))));
                out[oVel] = juce::jlimit (0.1f, 1.0f, 1.0f - k (2) * (float) index / (float) juce::jmax (1, total - 1));
                countdown = spacing - 1;
                --remaining;
                display[1] = 1.0f;
            }
            out[oGate] = gate.advance();
            display[0] = (float) remaining;
        }
    };

    //==========================================================================
    struct ChanceNode : SeqNode
    {
        using SeqNode::SeqNode;
        bool pass = false;
        std::array<bool, seqSteps> locked {};
        std::array<float, seqSteps> lockedCv {};
        int slot = 0;
        float lastOut = 0.0f;

        void process (const SeqTick&) override
        {
            enum { trig, probCv, cv, oGate, oOut };
            const bool lock = choice (3) == 1;
            if (rose (trig))
            {
                slot = (slot + 1) % seqSteps;
                if (lock)
                {
                    pass = locked[(size_t) slot];
                    lastOut = lockedCv[(size_t) slot];
                }
                else
                {
                    const float p = juce::jlimit (0.0f, 1.0f, k (0) + (plugged[probCv] ? in[probCv] - 0.5f : 0.0f));
                    pass = rng().nextFloat() < p;
                    if (! (rng().nextFloat() < k (2)))   // Memory: chance of keeping the previous value
                        lastOut = in[cv] + k (1) * (rng().nextFloat() * 2.0f - 1.0f) * 12.0f;
                    locked[(size_t) slot] = pass;
                    lockedCv[(size_t) slot] = lastOut;
                }
                display[1] = 1.0f;
            }
            out[oGate] = pass ? in[trig] : 0.0f;
            out[oOut] = lastOut;
            display[0] = pass ? 1.0f : 0.0f;
        }
    };

    //==========================================================================
    struct QuantiseNode : SeqNode
    {
        using SeqNode::SeqNode;
        float lastOut = 0.0f;
        int lastNote = -1000;
        GateTimer chg;

        float quantise (float semis) const
        {
            const int root = choice (0);
            const auto& scale = SeqRack::scales()[(size_t) choice (1)].notes;
            const int octaveSpan = choice (2) + 1;
            const int mode = choice (3);
            const float limit = 12.0f * (float) octaveSpan;
            semis = juce::jlimit (-limit, limit, semis);
            const int base = (int) std::round (semis);
            auto allowed = [&] (int n) { return scale[(size_t) (((n - root) % 12 + 12) % 12)]; };
            if (mode == 1) { for (int n = (int) std::ceil (semis - 1e-4f); ; ++n) if (allowed (n)) return (float) n; }
            if (mode == 2) { for (int n = (int) std::floor (semis + 1e-4f); ; --n) if (allowed (n)) return (float) n; }
            for (int d = 0; d < 12; ++d)
            {
                if (allowed (base + d)) return (float) (base + d);
                if (allowed (base - d)) return (float) (base - d);
            }
            return (float) base;
        }

        void process (const SeqTick&) override
        {
            enum { inCv, trig, oOut, oChg };
            const bool gated = plugged[trig];
            const bool sample = ! gated || rose (trig);
            if (sample)
            {
                const float q = quantise (in[inCv]);
                const int note = (int) std::lround (q);
                if (note != lastNote) { chg.fire (2); lastNote = note; }
                lastOut = q;
            }
            out[oOut] = lastOut;
            out[oChg] = chg.advance();
            display[0] = (float) (((lastNote % 12) + 12) % 12);
            display[1] = (float) lastNote;
        }
    };

    //==========================================================================
    struct MutateNode : SeqNode
    {
        using SeqNode::SeqNode;
        float offset = 0.0f, smoothed = 0.0f;

        void process (const SeqTick&) override
        {
            enum { cv, trig, oOut };
            if (rose (trig) && rng().nextFloat() < k (1))
                offset = juce::jlimit (-12.0f, 12.0f, offset + k (0) * (rng().nextFloat() * 2.0f - 1.0f) * 7.0f);
            const float smooth = k (2);
            smoothed += (offset - smoothed) * (smooth < 0.01f ? 1.0f : 1.0f / (1.0f + smooth * 200.0f));
            out[oOut] = in[cv] + smoothed;
            display[0] = smoothed;
        }
    };

    //==========================================================================
    struct CircadianNode : SeqNode
    {
        using SeqNode::SeqNode;
        double syncOffset = 0.0;

        void process (const SeqTick& t) override
        {
            enum { sync, oCv, oDawn, oNoon, oDusk, oInv };
            static const double barPeriods[] = { 1, 2, 4, 8, 16, 32, 64 };
            static const double secondPeriods[] = { 60, 300, 900, 3600, 14400, 43200, 86400 };
            const int follow = choice (4);
            const int period = choice (0);
            double phase;
            if (follow == 0)
            {
                const double beat = t.beat >= 0.0 ? t.beat : 0.0;
                phase = beat / (4.0 * barPeriods[period]);
            }
            else if (follow == 1)
                phase = t.seconds / secondPeriods[period];
            else
                phase = t.seconds / 86400.0;    // midnight = 0, noon = 0.5
            if (rose (sync)) syncOffset = -phase;
            phase += syncOffset + k (1);
            phase -= std::floor (phase);

            float v = shapeValue (choice (3), phase) * k (2);
            if (choice (5) == 1) v = k (2) - v;
            out[oCv] = v;
            out[oInv] = k (2) - v;
            out[oDawn] = phase >= 0.20 && phase < 0.30 ? 1.0f : 0.0f;
            out[oNoon] = phase >= 0.45 && phase < 0.55 ? 1.0f : 0.0f;
            out[oDusk] = phase >= 0.70 && phase < 0.80 ? 1.0f : 0.0f;
            display[0] = (float) phase;
            display[1] = v;
        }
    };

    //==========================================================================
    struct TideNode : SeqNode
    {
        using SeqNode::SeqNode;
        double phase = 0.0;
        float held = 0.5f;
        int lastCycle = -1;

        void process (const SeqTick& t) override
        {
            enum { sync, rateCv, oCv, oGate, oInv };
            static const double beats[] = { 0.25, 0.5, 1, 2, 4, 8, 16, 32 };
            int rate = choice (0) + (plugged[rateCv] ? (int) std::lround ((in[rateCv] - 0.5f) * 6.0f) : 0);
            rate = juce::jlimit (0, 7, rate);
            if (rose (sync)) phase = 0.0;
            if (t.beat >= 0.0) phase = t.beat / beats[rate];
            else phase += t.beatsPerTick / beats[rate];
            const double p = phase - std::floor (phase);
            const int cycle = (int) std::floor (phase);
            if (cycle != lastCycle) { held = rng().nextFloat(); lastCycle = cycle; }

            float s;
            switch (choice (1))
            {
                case 0:  s = (float) (0.5 - 0.5 * std::cos (p * juce::MathConstants<double>::twoPi)); break;
                case 1:  s = (float) (1.0 - std::abs (p * 2.0 - 1.0)); break;
                case 2:  s = (float) p; break;
                case 3:  s = p < 0.5 ? 1.0f : 0.0f; break;
                default: s = held; break;
            }
            const float v = juce::jlimit (0.0f, 1.0f, k (3) + (s - 0.5f) * k (2));
            out[oCv] = v;
            out[oInv] = 1.0f - v;
            out[oGate] = s > 0.5f ? 1.0f : 0.0f;
            display[0] = (float) p;
            display[1] = v;
        }
    };

    struct OutNode : SeqNode
    {
        using SeqNode::SeqNode;
        void process (const SeqTick&) override
        {
            for (size_t i = 0; i < in.size(); ++i) out[i] = in[i];
            display[0] = in[1];     // gate
            display[1] = in[0];     // pitch cv
            display[2] = in[3];     // mod
        }
    };
}

std::unique_ptr<SeqNode> createSeqNode (const SeqModule& m)
{
    switch (m.type)
    {
        case SeqType::Clock:     return std::make_unique<ClockNode> (m);
        case SeqType::Steps:     return std::make_unique<StepsNode> (m);
        case SeqType::Euclid:    return std::make_unique<EuclidNode> (m);
        case SeqType::Turing:    return std::make_unique<TuringNode> (m);
        case SeqType::Burst:     return std::make_unique<BurstNode> (m);
        case SeqType::Chance:    return std::make_unique<ChanceNode> (m);
        case SeqType::Quantise:  return std::make_unique<QuantiseNode> (m);
        case SeqType::Mutate:    return std::make_unique<MutateNode> (m);
        case SeqType::Circadian: return std::make_unique<CircadianNode> (m);
        case SeqType::Tide:      return std::make_unique<TideNode> (m);
        case SeqType::Out:       return std::make_unique<OutNode> (m);
    }
    return std::make_unique<OutNode> (m);
}

//==============================================================================
SeqRuntime::SeqRuntime (const SeqRack& rack)
{
    std::map<int, SeqNode*> byId;
    std::vector<std::unique_ptr<SeqNode>> made;
    for (auto& m : rack.modules)
    {
        made.push_back (createSeqNode (m));
        byId[m.id] = made.back().get();
    }
    for (auto& c : rack.cables)
    {
        auto a = byId.find (c.fromModule), b = byId.find (c.toModule);
        if (a == byId.end() || b == byId.end()) continue;
        if (c.fromPort < 0 || c.fromPort >= (int) a->second->out.size() || c.toPort < 0 || c.toPort >= (int) b->second->in.size()) continue;
        links.push_back ({ a->second, c.fromPort, b->second, c.toPort });
        b->second->plugged[(size_t) c.toPort] = true;
    }

    // Topological order so a module sees this tick's values from everything upstream;
    // anything in a feedback loop keeps rack order and sees last tick's values
    std::map<SeqNode*, int> indegree;
    for (auto& n : made) indegree[n.get()] = 0;
    for (auto& l : links) ++indegree[l.to];
    std::vector<SeqNode*> order, ready;
    for (auto& n : made) if (indegree[n.get()] == 0) ready.push_back (n.get());
    while (! ready.empty())
    {
        auto* n = ready.front();
        ready.erase (ready.begin());
        order.push_back (n);
        for (auto& l : links)
            if (l.from == n && --indegree[l.to] == 0)
                ready.push_back (l.to);
    }
    for (auto& n : made)
        if (std::find (order.begin(), order.end(), n.get()) == order.end())
            order.push_back (n.get());
    for (auto* n : order)
        for (auto& u : made)
            if (u.get() == n) { nodes.push_back (std::move (u)); break; }
    for (auto& n : nodes)
        if (n->type == SeqType::Out) outNode = n.get();
}

SeqNode* SeqRuntime::find (int moduleId)
{
    for (auto& n : nodes)
        if (n->id == moduleId) return n.get();
    return nullptr;
}

void SeqRuntime::allNotesOff (const NoteSink& sink)
{
    for (auto& h : held)
        if (h >= 0) { sink (h, 0.0f, false); h = -1; }
    gateHigh = false;
    for (auto& n : nodes) n->reset();
}

void SeqRuntime::process (const SeqTick& t, const NoteSink& sink)
{
    if (t.reset)
        for (auto& n : nodes) n->reset();

    for (auto& n : nodes)
    {
        std::fill (n->in.begin(), n->in.end(), 0.0f);
        for (auto& l : links)
            if (l.to == n.get())
                n->in[(size_t) l.toPort] = l.from->out[(size_t) l.fromPort];
        n->process (t);
    }
    if (outNode == nullptr)
        return;

    enum { pitch, gate, vel, mod };
    const auto& o = *outNode;
    const bool poly = SeqKnob::choice (o.knob[0].load(), 2) == 1;
    const int octave = SeqKnob::choice (o.knob[1].load(), 5) - 2;
    modOut = juce::jlimit (0.0f, 1.0f, o.in[mod] * o.knob[2].load());

    const bool high = o.in[gate] > 0.5f;
    const int note = juce::jlimit (0, 127, 60 + (int) std::lround (o.in[pitch]) + 12 * octave);
    const float velocity = o.plugged[vel] ? juce::jlimit (0.05f, 1.0f, o.in[vel]) : 0.8f;

    auto noteOn = [&]
    {
        if (! poly)
            for (auto& h : held) if (h >= 0) { sink (h, 0.0f, false); h = -1; }
        auto& slot = held[(size_t) (poly ? nextVoice : 0)];
        if (slot >= 0) sink (slot, 0.0f, false);
        slot = note;
        sink (note, velocity, true);
        nextVoice = (nextVoice + 1) % (int) held.size();
        lastPitch = note;
    };

    if (high && ! gateHigh)
        noteOn();
    else if (high && note != lastPitch)
        noteOn();                       // a pitch change under a held gate: legato / slide
    else if (! high && gateHigh)
    {
        if (poly)
        {
            auto& last = held[(size_t) ((nextVoice + (int) held.size() - 1) % (int) held.size())];
            if (last >= 0) { sink (last, 0.0f, false); last = -1; }
        }
        else
            for (auto& h : held) if (h >= 0) { sink (h, 0.0f, false); h = -1; }
    }
    gateHigh = high;
}
