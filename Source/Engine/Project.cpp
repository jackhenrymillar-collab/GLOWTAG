#include "Project.h"
#include <juce_audio_formats/juce_audio_formats.h>

std::shared_ptr<const AudioData> AudioData::load (const juce::File& file)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return nullptr;

    // Keep it sensible: up to ten minutes, stereo
    const auto numSamples = (int) juce::jmin<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 600.0));
    auto data = std::make_shared<AudioData>();
    data->buffer.setSize (2, numSamples);
    reader->read (&data->buffer, 0, numSamples, 0, true, true);
    if (reader->numChannels == 1)
        data->buffer.copyFrom (1, 0, data->buffer, 0, 0, numSamples);
    data->sampleRate = reader->sampleRate;
    data->fileName = file.getFileNameWithoutExtension();

    const int numPeaks = 1024;
    data->peaks.resize (numPeaks);
    for (int i = 0; i < numPeaks; ++i)
    {
        const int a = (int) ((juce::int64) numSamples * i / numPeaks);
        const int b = juce::jmax (a + 1, (int) ((juce::int64) numSamples * (i + 1) / numPeaks));
        data->peaks[(size_t) i] = juce::jmin (1.0f, data->buffer.getMagnitude (a, juce::jmin (b, numSamples) - a));
    }
    return data;
}

int Track::clipAt (double beat) const
{
    for (size_t i = 0; i < clips.size(); ++i)
        if (beat >= clips[i].start && beat < clips[i].end())
            return (int) i;
    return -1;
}

namespace
{
    // Turn a 16-step pattern into one bar of notes. `pitches` cycles per hit; chords add every pitch.
    std::vector<Note> notesFromPattern (const char* pattern, std::vector<int> pitches, double length, bool chord, float velocity = 0.8f)
    {
        std::vector<Note> notes;
        int hit = 0;
        for (int i = 0; i < 16 && pattern[i] != 0; ++i)
        {
            if (pattern[i] != 'x')
                continue;
            const float vel = i % 4 == 0 ? velocity : velocity * 0.8f;
            if (chord)
                for (int p : pitches)
                    notes.push_back ({ i * 0.25, length, p, vel });
            else
                notes.push_back ({ i * 0.25, length, pitches[(size_t) (hit % (int) pitches.size())], vel });
            ++hit;
        }
        return notes;
    }
}

namespace
{
    // Starting patches. Each module wires itself in as it is added; knobs are then set to taste.
    enum class Preset { Drums, Chop, LeadVox, Bass, Keys, Strings, Pad, Automation };

    void setKnobs (Patch& p, int id, std::initializer_list<float> values)
    {
        if (auto* m = p.find (id))
        {
            size_t i = 0;
            for (float v : values)
                if (i < m->knobValues.size())
                    m->knobValues[i++] = v;
        }
    }

    void buildPatch (Patch& p, Preset preset)
    {
        switch (preset)
        {
            case Preset::Drums:
            {
                p.add (ModuleType::Drums);
                const int comp = p.add (ModuleType::Compressor);
                setKnobs (p, comp, { 0.55f, 0.35f, 0.25f, 0.3f, 0.15f });
                p.add (ModuleType::EQ);
                break;
            }
            case Preset::Chop:
            case Preset::LeadVox:
            {
                const int s = p.add (ModuleType::Sampler);
                p.find (s)->sampleName = preset == Preset::Chop ? "Vox Ahh" : "Vox Ooh";
                setKnobs (p, s, { 0.0f, 0.6f, 0.5f, 0.02f, 0.35f, 0.8f });
                const int f = p.add (ModuleType::Filter);
                setKnobs (p, f, { 0.75f, 0.25f, 0.1f, 0.0f });
                const int l = p.add (ModuleType::LFO);
                setKnobs (p, l, { 0.35f, 0.25f, 0.0f });
                const int d = p.add (ModuleType::Delay);
                setKnobs (p, d, { 0.5f, 0.35f, 0.5f, preset == Preset::Chop ? 0.2f : 0.3f });
                p.add (ModuleType::EQ);
                break;
            }
            case Preset::Bass:
            {
                const int s = p.add (ModuleType::Synth);
                //                 Wave  Sub   Det   Cut   Res   Env   A      D     S     R     Drv   Gain
                setKnobs (p, s, { 0.0f, 0.7f, 0.05f, 0.28f, 0.2f, 0.55f, 0.0f, 0.35f, 0.5f, 0.15f, 0.35f, 0.8f });
                const int d = p.add (ModuleType::Drive);
                setKnobs (p, d, { 0.25f, 0.4f, 0.5f, 0.3f });
                p.add (ModuleType::EQ);
                break;
            }
            case Preset::Keys:
            {
                const int s = p.add (ModuleType::Synth);
                setKnobs (p, s, { 0.55f, 0.0f, 0.2f, 0.5f, 0.3f, 0.6f, 0.0f, 0.45f, 0.25f, 0.4f, 0.1f, 0.55f });
                const int d = p.add (ModuleType::Delay);
                setKnobs (p, d, { 0.55f, 0.4f, 0.4f, 0.25f });
                const int r = p.add (ModuleType::Reverb);
                setKnobs (p, r, { 0.5f, 0.5f, 0.8f, 0.25f });
                break;
            }
            case Preset::Strings:
            case Preset::Pad:
            {
                const int s = p.add (ModuleType::Synth);
                setKnobs (p, s, { 0.0f, 0.2f, 0.4f, 0.42f, 0.15f, 0.3f, preset == Preset::Pad ? 0.6f : 0.45f, 0.5f, 0.85f, 0.6f, 0.1f, 0.4f });
                const int f = p.add (ModuleType::Filter);
                setKnobs (p, f, { 0.6f, 0.2f, 0.0f, 0.0f });
                const int l = p.add (ModuleType::LFO);
                setKnobs (p, l, { 0.2f, 0.3f, 0.0f });
                const int r = p.add (ModuleType::Reverb);
                setKnobs (p, r, { 0.8f, 0.4f, 1.0f, 0.4f });
                break;
            }
            case Preset::Automation:
            {
                p.add (ModuleType::Filter);
                p.add (ModuleType::LFO);
                break;
            }
        }
        p.tidy();
    }
}

Project::Project()
{
    makeDemo();
}

void Project::clear()
{
    tracks.clear();
    buses.clear();
    for (auto& r : returns) r.reset();
    master.reset();
    file = juce::File();
    dirty = false;
}

void Project::makeBuses()
{
    const char* names[numReturns] = { "Return A", "Return B" };
    const int colours[numReturns] = { 7, 5 };
    for (int i = 0; i < numReturns; ++i)
    {
        auto t = std::make_unique<Track>();
        t->name = names[i];
        t->kind = TrackKind::Return;
        t->colourIndex = colours[i];
        t->volume = 0.8f;
        const int fx = t->patch.add (i == 0 ? ModuleType::Reverb : ModuleType::Delay);
        setKnobs (t->patch, fx, i == 0 ? std::initializer_list<float> { 0.8f, 0.4f, 1.0f, 1.0f }
                                       : std::initializer_list<float> { 0.5f, 0.45f, 0.5f, 1.0f });
        t->patch.tidy();
        returns[(size_t) i] = std::move (t);
    }
    master = std::make_unique<Track>();
    master->name = "Master";
    master->kind = TrackKind::Master;
    master->colourIndex = 1;
    master->volume = 0.8f;
    const int comp = master->patch.add (ModuleType::Compressor);
    setKnobs (master->patch, comp, { 0.7f, 0.25f, 0.4f, 0.4f, 0.1f });
    master->patch.tidy();
}

Track& Project::slot (int index)
{
    if (index < (int) tracks.size()) return *tracks[(size_t) juce::jmax (0, index)];
    if (index < returnSlot (0)) return *buses[(size_t) (index - (int) tracks.size())];
    if (index < masterSlot()) return *returns[(size_t) (index - returnSlot (0))];
    return *master;
}

int Project::slotOf (const Track* t) const
{
    for (int i = 0; i < numSlots(); ++i)
        if (&slot (i) == t)
            return i;
    return -1;
}

int Project::outputSlot (int slotIndex) const
{
    if (slotIndex < 0 || slotIndex >= masterSlot())
        return -1;
    const int out = slot (slotIndex).output.load();
    return out >= 0 && out < (int) buses.size() && busSlot (out) != slotIndex ? busSlot (out) : masterSlot();
}

bool Project::canRoute (int fromSlot, int toSlot) const
{
    if (fromSlot < 0 || fromSlot >= masterSlot() || toSlot < 0 || toSlot >= numSlots() || fromSlot == toSlot)
        return false;
    if (! slot (toSlot).takesInput())
        return false;
    // Follow the destination downstream: it must reach the master without coming back to us
    int at = toSlot;
    for (int guard = 0; guard <= (int) buses.size() + 1 && at != masterSlot(); ++guard)
    {
        if (at == fromSlot)
            return false;
        at = outputSlot (at);
    }
    return at == masterSlot();
}

bool Project::setOutput (int fromSlot, int toSlot)
{
    if (! canRoute (fromSlot, toSlot))
        return false;
    slot (fromSlot).output = toSlot == masterSlot() ? -1 : toSlot - busSlot (0);
    dirty = true;
    return true;
}

int Project::addBus (const juce::String& name)
{
    if ((int) buses.size() >= maxBuses)
        return -1;
    auto t = std::make_unique<Track>();
    t->kind = TrackKind::Bus;
    t->name = name.isNotEmpty() ? name : "Bus " + juce::String ((int) buses.size() + 1);
    t->colourIndex = (int) buses.size() % 2 == 0 ? 3 : 4;
    t->volume = 1.0f;       // a new bus passes its input through unchanged
    buses.push_back (std::move (t));
    dirty = true;
    return (int) buses.size() - 1;
}

void Project::removeBus (int busIndex)
{
    if (busIndex < 0 || busIndex >= (int) buses.size())
        return;
    const int onward = buses[(size_t) busIndex]->output.load();   // where the removed bus was going
    const int last = masterSlot();
    for (int i = 0; i < last; ++i)
    {
        auto& out = slot (i).output;
        int o = out.load();
        if (o == busIndex) o = onward == busIndex ? -1 : onward;
        if (o > busIndex) --o;
        out = o;
    }
    buses.erase (buses.begin() + busIndex);
    for (int i = 0; i < masterSlot(); ++i)        // never leave a channel pointing at itself
        if (slot (i).kind == TrackKind::Bus && slot (i).output.load() == i - busSlot (0))
            slot (i).output = -1;
    dirty = true;
}

const Track& Project::slot (int index) const
{
    return const_cast<Project*> (this)->slot (index);
}

void Project::makeEmpty()
{
    clear();
    struct Def { const char* name; TrackKind kind; int colour; Preset preset; };
    const Def defs[] = { { "Drums", TrackKind::Midi, 6, Preset::Drums }, { "Bass", TrackKind::Midi, 1, Preset::Bass },
                         { "Keys", TrackKind::Midi, 5, Preset::Keys }, { "Pad", TrackKind::Midi, 4, Preset::Pad } };
    for (auto& d : defs)
    {
        auto t = std::make_unique<Track>();
        t->name = d.name;
        t->kind = d.kind;
        t->colourIndex = d.colour;
        buildPatch (t->patch, d.preset);
        tracks.push_back (std::move (t));
    }
    makeBuses();
}

void Project::makeDemo()
{
    clear();
    struct ClipDef { int startBar, lengthBars; const char* name; };
    struct Def
    {
        const char* name; TrackKind kind; int colour; Preset preset;
        const char* pattern; std::vector<int> pitches; double noteLength; bool chord; float volume;
        std::vector<ClipDef> clips;
    };

    // Colour indices follow Pal: Rose, Green, Sky, Gold, Violet, Blue, Amber, Ice.
    // Drum pitches are Drum Rack pads: 36 kick, 38 snare, 39 clap, 42 closed hat, 44 shaker, 45/47/50 congas and toms.
    const std::vector<Def> defs = {
        { "Kick",     TrackKind::Audio,      0, Preset::Drums,      "x...x...x...x..x", { 36 }, 0.25, false, 0.9f,  { { 0, 4, "Four floor" }, { 4, 4, "Four floor" }, { 8, 4, "Half" }, { 12, 4, "Four floor" } } },
        { "Snare",    TrackKind::Audio,      1, Preset::Drums,      "....x.......x...", { 38 }, 0.25, false, 0.75f, { { 0, 8, "Crack" }, { 8, 4, "Roll" }, { 12, 4, "Crack" } } },
        { "Hats",     TrackKind::Audio,      2, Preset::Drums,      "x.x.x.xxx.x.x.xx", { 42, 42, 42, 46 }, 0.125, false, 0.55f, { { 2, 6, "Shuffle" }, { 8, 8, "Open" } } },
        { "Clap",     TrackKind::Audio,      3, Preset::Drums,      "....x..x....x..x", { 39 }, 0.25, false, 0.6f,  { { 4, 4, "Stack" }, { 12, 4, "Stack" } } },
        { "Vox Chop", TrackKind::Sampler,    4, Preset::Chop,       "x..x..x...x..x..", { 60, 63, 67, 70 }, 0.5, false, 0.7f, { { 0, 4, "Hook A" }, { 4, 4, "Hook B" }, { 10, 6, "Hook A" } } },
        { "Sub Bass", TrackKind::Midi,       6, Preset::Bass,       "x.....x.x.....x.", { 33, 33, 31, 31 }, 0.75, false, 0.85f, { { 0, 8, "Root" }, { 8, 8, "Walk" } } },
        { "Keys",     TrackKind::Midi,       5, Preset::Keys,       "..x.......x.....", { 57, 60, 64 }, 1.5, true, 0.6f,  { { 0, 16, "Chords" } } },
        { "Riser FX", TrackKind::Automation, 7, Preset::Automation, "................", {}, 0.25, false, 0.8f,  { { 6, 2, "Riser" }, { 14, 2, "Riser" } } },
        { "Perc",     TrackKind::Audio,      6, Preset::Drums,      "..x...x...x.x...", { 45, 50, 45, 47 }, 0.25, false, 0.5f, { { 4, 4, "Congas" }, { 10, 6, "Congas" } } },
        { "Shaker",   TrackKind::Audio,      2, Preset::Drums,      "x.x.x.x.x.x.x.x.", { 44 }, 0.125, false, 0.4f, { { 0, 16, "Shake" } } },
        { "Lead Vox", TrackKind::Sampler,    0, Preset::LeadVox,    "..........x.....", { 67, 65 }, 1.0, false, 0.5f, { { 8, 4, "Verse" }, { 12, 4, "Hook" } } },
        { "Strings",  TrackKind::Midi,       4, Preset::Strings,    "x.......x.......", { 57, 64 }, 2.0, true, 0.4f,  { { 4, 8, "Swell" } } },
        { "Pad",      TrackKind::Midi,       5, Preset::Pad,        "x...............", { 48, 55, 60 }, 4.0, true, 0.35f, { { 0, 8, "Air" }, { 8, 8, "Air" } } },
        { "Sweep FX", TrackKind::Automation, 7, Preset::Automation, "................", {}, 0.25, false, 0.8f,  { { 3, 1, "Sweep" }, { 7, 1, "Sweep" }, { 15, 1, "Sweep" } } },
    };

    for (auto& d : defs)
    {
        auto t = std::make_unique<Track>();
        t->name = d.name;
        t->kind = d.kind;
        t->colourIndex = d.colour;
        t->volume = d.volume;

        const auto notes = d.pitches.empty() ? std::vector<Note> {} : notesFromPattern (d.pattern, d.pitches, d.noteLength, d.chord);
        for (auto& c : d.clips)
        {
            Clip clip;
            clip.start = c.startBar * beatsPerBar;
            clip.length = c.lengthBars * beatsPerBar;
            clip.loopLength = juce::jmin (beatsPerBar, clip.length);
            clip.name = c.name;
            clip.notes = notes;
            t->clips.push_back (clip);
        }

        buildPatch (t->patch, d.preset);
        tracks.push_back (std::move (t));
    }
    // A touch of the returns on the melodic tracks
    for (auto& t : tracks)
        if (t->kind == TrackKind::Midi || t->kind == TrackKind::Sampler)
        {
            t->send[0] = t->kind == TrackKind::Sampler ? 0.3f : 0.2f;
            t->send[1] = t->kind == TrackKind::Sampler ? 0.2f : 0.0f;
        }
    makeBuses();

    // The drums share a bus so they can be mixed as one; everything else goes straight to the master
    const int drums = addBus ("Drums");
    buses[(size_t) drums]->colourIndex = 6;
    for (auto& t : tracks)
        if (t->kind == TrackKind::Audio)
            t->output = drums;
    dirty = false;
}

bool Project::isAudible (int slotIndex) const
{
    auto& t = slot (slotIndex);
    if (t.kind == TrackKind::Master)
        return true;
    if (t.kind == TrackKind::Return || t.kind == TrackKind::Bus)
        return ! t.muted.load();

    bool anySolo = false;
    for (auto& other : tracks)
        anySolo = anySolo || other->soloed.load();
    return ! t.muted.load() && (! anySolo || t.soloed.load());
}

//==============================================================================
juce::var Track::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("name", name);
    o->setProperty ("kind", (int) kind);
    o->setProperty ("colour", colourIndex);
    o->setProperty ("rolling", rolling.load());
    o->setProperty ("muted", muted.load());
    o->setProperty ("soloed", soloed.load());
    o->setProperty ("volume", volume.load());
    o->setProperty ("pan", pan.load());
    o->setProperty ("sendA", send[0].load());
    o->setProperty ("sendB", send[1].load());
    o->setProperty ("out", output.load());
    o->setProperty ("trimDb", trimDb.load());
    o->setProperty ("phase", phase.load());
    o->setProperty ("mono", mono.load());
    o->setProperty ("width", width.load());
    o->setProperty ("analogue", analogue.load());
    juce::Array<juce::var> cs;
    for (auto& c : clips)
    {
        auto* co = new juce::DynamicObject();
        co->setProperty ("start", c.start);
        co->setProperty ("length", c.length);
        co->setProperty ("loop", c.loopLength);
        co->setProperty ("name", c.name);
        co->setProperty ("kind", (int) c.kind);
        if (c.audioPath.isNotEmpty()) co->setProperty ("audio", c.audioPath);
        juce::Array<juce::var> ns;
        for (auto& n : c.notes)
            ns.add (juce::Array<juce::var> { n.start, n.length, n.pitch, n.velocity });
        co->setProperty ("notes", ns);
        cs.add (juce::var (co));
    }
    o->setProperty ("clips", cs);
    o->setProperty ("patch", patch.toVar());
    o->setProperty ("seq", seq.toVar());
    return juce::var (o);
}

void Track::fromVar (const juce::var& v)
{
    name = v["name"].toString();
    kind = (TrackKind) juce::jlimit (0, 6, (int) v["kind"]);
    colourIndex = (int) v["colour"];
    rolling = (bool) v["rolling"];
    muted = (bool) v["muted"];
    soloed = (bool) v["soloed"];
    volume = (float) v.getProperty ("volume", 0.8f);
    pan = (float) v.getProperty ("pan", 0.0f);
    send[0] = (float) v.getProperty ("sendA", 0.0f);
    send[1] = (float) v.getProperty ("sendB", 0.0f);
    output = (int) v.getProperty ("out", -1);
    trimDb = juce::jlimit (-12.0f, 12.0f, (float) v.getProperty ("trimDb", 0.0f));
    phase = (bool) v.getProperty ("phase", false);
    mono = (bool) v.getProperty ("mono", false);
    width = juce::jlimit (0.0f, 2.0f, (float) v.getProperty ("width", 1.0f));
    analogue = (bool) v.getProperty ("analogue", false);
    clips.clear();
    if (auto* cs = v["clips"].getArray())
        for (auto& cv : *cs)
        {
            Clip c;
            c.start = (double) cv["start"];
            c.length = (double) cv["length"];
            c.loopLength = (double) cv.getProperty ("loop", c.length);
            c.name = cv["name"].toString();
            c.kind = (ClipKind) juce::jlimit (0, 1, (int) cv["kind"]);
            c.audioPath = cv["audio"].toString();
            if (c.kind == ClipKind::Audio && c.audioPath.isNotEmpty())
                c.audio = AudioData::load (juce::File (c.audioPath));
            if (auto* ns = cv["notes"].getArray())
                for (auto& nv : *ns)
                    if (auto* a = nv.getArray(); a != nullptr && a->size() == 4)
                        c.notes.push_back ({ (double) (*a)[0], (double) (*a)[1], (int) (*a)[2], (float) (*a)[3] });
            clips.push_back (c);
        }
    if (v.hasProperty ("patch")) patch.fromVar (v["patch"]);
    if (v.hasProperty ("seq")) seq.fromVar (v["seq"]);
}

juce::var Project::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("app", "GLOWTAG");
    o->setProperty ("version", 1);
    o->setProperty ("bpm", bpm.load());
    juce::Array<juce::var> ts, rs, bs;
    for (auto& t : tracks) ts.add (t->toVar());
    for (auto& b : buses) bs.add (b->toVar());
    for (auto& r : returns) rs.add (r->toVar());
    o->setProperty ("tracks", ts);
    o->setProperty ("buses", bs);
    o->setProperty ("returns", rs);
    o->setProperty ("master", master->toVar());
    return juce::var (o);
}

bool Project::fromVar (const juce::var& v)
{
    if (v["app"].toString() != "GLOWTAG" || v["tracks"].getArray() == nullptr)
        return false;
    std::vector<std::unique_ptr<Track>> newTracks;
    for (auto& tv : *v["tracks"].getArray())
    {
        auto t = std::make_unique<Track>();
        t->fromVar (tv);
        newTracks.push_back (std::move (t));
    }
    if (newTracks.empty())
        return false;

    const juce::SpinLock::ScopedLockType sl (lock);
    tracks = std::move (newTracks);
    buses.clear();
    if (auto* bs = v["buses"].getArray())
        for (int i = 0; i < bs->size() && i < maxBuses; ++i)
        {
            auto b = std::make_unique<Track>();
            b->fromVar ((*bs)[i]);
            b->kind = TrackKind::Bus;
            buses.push_back (std::move (b));
        }
    makeBuses();
    if (auto* rs = v["returns"].getArray())
        for (int i = 0; i < rs->size() && i < numReturns; ++i)
            returns[(size_t) i]->fromVar ((*rs)[i]);
    if (v.hasProperty ("master"))
        master->fromVar (v["master"]);
    bpm = juce::jlimit (20.0, 300.0, (double) v.getProperty ("bpm", 124.0));

    // Routing from the file: anything out of range, or part of a loop, goes to the master instead
    for (int i = 0; i < masterSlot(); ++i)
    {
        auto& out = slot (i).output;
        if (out.load() < -1 || out.load() >= (int) buses.size())
            out = -1;
    }
    for (int b = 0; b < (int) buses.size(); ++b)
    {
        int at = busSlot (b), steps = 0;
        while (at != masterSlot() && steps <= (int) buses.size()) { at = outputSlot (at); ++steps; }
        if (at != masterSlot())
            buses[(size_t) b]->output = -1;
    }
    dirty = false;
    return true;
}

bool Project::saveTo (const juce::File& f)
{
    const auto json = juce::JSON::toString (toVar(), false);
    f.getParentDirectory().createDirectory();
    if (! f.replaceWithText (json))
        return false;
    file = f;
    dirty = false;
    return true;
}

bool Project::loadFrom (const juce::File& f)
{
    const auto v = juce::JSON::parse (f);
    if (! fromVar (v))
        return false;
    file = f;
    return true;
}
