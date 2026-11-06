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

Project::Project()
{
    struct ClipDef { int startBar, lengthBars; const char* name; };
    struct Def
    {
        const char* name; TrackKind kind; int colour; Sound sound;
        const char* pattern; std::vector<int> pitches; double noteLength; bool chord; float volume;
        std::vector<ClipDef> clips;
    };

    // Colour indices follow Pal: Rose, Green, Sky, Gold, Violet, Blue, Amber, Ice
    const std::vector<Def> defs = {
        { "Kick",     TrackKind::Audio,      0, Sound::Kick,   "x...x...x...x..x", { 36 }, 0.25, false, 0.85f, { { 0, 4, "Four floor" }, { 4, 4, "Four floor" }, { 8, 4, "Half" }, { 12, 4, "Four floor" } } },
        { "Snare",    TrackKind::Audio,      1, Sound::Snare,  "....x.......x...", { 38 }, 0.25, false, 0.75f, { { 0, 8, "Crack" }, { 8, 4, "Roll" }, { 12, 4, "Crack" } } },
        { "Hats",     TrackKind::Audio,      2, Sound::Hat,    "x.x.x.xxx.x.x.xx", { 42 }, 0.125, false, 0.7f, { { 2, 6, "Shuffle" }, { 8, 8, "Open" } } },
        { "Clap",     TrackKind::Audio,      3, Sound::Clap,   "....x..x....x..x", { 39 }, 0.25, false, 0.6f, { { 4, 4, "Stack" }, { 12, 4, "Stack" } } },
        { "Vox Chop", TrackKind::Sampler,    4, Sound::Chop,   "x..x..x...x..x..", { 72, 75, 79, 82 }, 0.5, false, 0.7f, { { 0, 4, "Hook A" }, { 4, 4, "Hook B" }, { 10, 6, "Hook A" } } },
        { "Sub Bass", TrackKind::Midi,       6, Sound::Bass,   "x.....x.x.....x.", { 33, 33, 31, 31 }, 0.75, false, 0.8f, { { 0, 8, "Root" }, { 8, 8, "Walk" } } },
        { "Keys",     TrackKind::Midi,       5, Sound::Keys,   "..x.......x.....", { 57, 60, 64 }, 1.5, true, 0.7f, { { 0, 16, "Chords" } } },
        { "Riser FX", TrackKind::Automation, 7, Sound::Silent, "................", {}, 0.25, false, 0.0f, { { 6, 2, "Riser" }, { 14, 2, "Riser" } } },
        { "Perc",     TrackKind::Audio,      6, Sound::Perc,   "..x...x...x.x...", { 45, 50, 45, 52 }, 0.25, false, 0.55f, { { 4, 4, "Congas" }, { 10, 6, "Congas" } } },
        { "Shaker",   TrackKind::Audio,      2, Sound::Hat,    "x.x.x.x.x.x.x.x.", { 44 }, 0.125, false, 0.35f, { { 0, 16, "Shake" } } },
        { "Lead Vox", TrackKind::Sampler,    0, Sound::Chop,   "..........x.....", { 79, 77 }, 1.0, false, 0.45f, { { 8, 4, "Verse" }, { 12, 4, "Hook" } } },
        { "Strings",  TrackKind::Midi,       4, Sound::Pad,    "x.......x.......", { 57, 64 }, 2.0, true, 0.4f, { { 4, 8, "Swell" } } },
        { "Pad",      TrackKind::Midi,       5, Sound::Pad,    "x...............", { 48, 55, 60 }, 4.0, true, 0.35f, { { 0, 8, "Air" }, { 8, 8, "Air" } } },
        { "Sweep FX", TrackKind::Automation, 7, Sound::Silent, "................", {}, 0.25, false, 0.0f, { { 3, 1, "Sweep" }, { 7, 1, "Sweep" }, { 15, 1, "Sweep" } } },
    };

    for (auto& d : defs)
    {
        auto t = std::make_unique<Track>();
        t->name = d.name;
        t->kind = d.kind;
        t->colourIndex = d.colour;
        t->sound = d.sound;
        t->volume = d.volume > 0.0f ? d.volume : 0.8f;

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

        // Starting patch for each kind of track; every module wires itself in as it is added
        auto& p = t->patch;
        switch (d.kind)
        {
            case TrackKind::Sampler:
                for (auto m : { ModuleType::Sampler, ModuleType::Filter, ModuleType::LFO, ModuleType::Delay, ModuleType::EQ })
                    p.add (m);
                break;
            case TrackKind::Midi:
                for (auto m : { ModuleType::Synth, ModuleType::Filter, ModuleType::LFO, ModuleType::Reverb })
                    p.add (m);
                break;
            case TrackKind::Audio:
                for (auto m : { ModuleType::Drums, ModuleType::Compressor, ModuleType::EQ })
                    p.add (m);
                break;
            case TrackKind::Automation:
                for (auto m : { ModuleType::Filter, ModuleType::LFO })
                    p.add (m);
                break;
        }
        p.tidy();
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
