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
}

bool Project::isAudible (int trackIndex) const
{
    bool anySolo = false;
    for (auto& t : tracks)
        anySolo = anySolo || t->soloed.load();

    auto& t = *tracks[(size_t) trackIndex];
    return ! t.muted.load() && (! anySolo || t.soloed.load());
}
