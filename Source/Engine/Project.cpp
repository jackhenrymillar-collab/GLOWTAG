#include "Project.h"

uint16_t Project::patternFromString (const char* s)
{
    uint16_t bits = 0;
    for (int i = 0; i < 16 && s[i] != 0; ++i)
        if (s[i] == 'x')
            bits = (uint16_t) (bits | (1u << i));
    return bits;
}

Project::Project()
{
    struct Def { const char* name; TrackKind kind; int colour; Sound sound; const char* pat; std::vector<Clip> clips; };

    // Colour indices follow Pal: Rose, Green, Sky, Gold, Violet, Blue, Amber, Ice
    const std::vector<Def> defs = {
        { "Kick",     TrackKind::Audio,      0, Sound::Kick,   "x...x...x...x..x", { { 0, 4, "Four floor" }, { 4, 4, "Four floor" }, { 8, 4, "Half" }, { 12, 4, "Four floor" } } },
        { "Snare",    TrackKind::Audio,      1, Sound::Snare,  "....x.......x...", { { 0, 8, "Crack" }, { 8, 4, "Roll" }, { 12, 4, "Crack" } } },
        { "Hats",     TrackKind::Audio,      2, Sound::Hat,    "x.x.x.xxx.x.x.xx", { { 2, 6, "Shuffle" }, { 8, 8, "Open" } } },
        { "Clap",     TrackKind::Audio,      3, Sound::Clap,   "....x..x....x..x", { { 4, 4, "Stack" }, { 12, 4, "Stack" } } },
        { "Vox Chop", TrackKind::Sampler,    4, Sound::Chop,   "x..x..x...x..x..", { { 0, 4, "Hook A" }, { 4, 4, "Hook B" }, { 10, 6, "Hook A" } } },
        { "Sub Bass", TrackKind::Midi,       6, Sound::Bass,   "x.....x.x.....x.", { { 0, 8, "Root" }, { 8, 8, "Walk" } } },
        { "Keys",     TrackKind::Midi,       5, Sound::Keys,   "..x.......x.....", { { 0, 16, "Chords" } } },
        { "Riser FX", TrackKind::Automation, 7, Sound::Silent, "................", { { 6, 2, "Riser" }, { 14, 2, "Riser" } } },
        { "Perc",     TrackKind::Audio,      6, Sound::Silent, "..x...x...x.x...", { { 4, 4, "Congas" }, { 10, 6, "Congas" } } },
        { "Shaker",   TrackKind::Audio,      2, Sound::Silent, "x.x.x.x.x.x.x.x.", { { 0, 16, "Shake" } } },
        { "Lead Vox", TrackKind::Sampler,    0, Sound::Silent, "..........x.....", { { 8, 4, "Verse" }, { 12, 4, "Hook" } } },
        { "Strings",  TrackKind::Midi,       4, Sound::Silent, "x.......x.......", { { 4, 8, "Swell" } } },
        { "Pad",      TrackKind::Midi,       5, Sound::Silent, "x...............", { { 0, 8, "Air" }, { 8, 8, "Air" } } },
        { "Sweep FX", TrackKind::Automation, 7, Sound::Silent, "................", { { 3, 1, "Sweep" }, { 7, 1, "Sweep" }, { 15, 1, "Sweep" } } },
    };

    for (auto& d : defs)
    {
        auto t = std::make_unique<Track>();
        t->name = d.name;
        t->kind = d.kind;
        t->colourIndex = d.colour;
        t->sound = d.sound;
        t->clips = d.clips;
        t->steps = patternFromString (d.pat);
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
