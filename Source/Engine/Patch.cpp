#include "Patch.h"
#include "DeviceParams.h"
#include <functional>

namespace
{
    ModuleInfo withExtras (ModuleInfo info, std::vector<ModuleInfo::Extra> extras)
    {
        info.extras = std::move (extras);
        return info;
    }

    // The expanded views' controls. Each: name, value on a new module, value for an older project.
    std::vector<ModuleInfo::Extra> synthExtras()
    {
        std::vector<ModuleInfo::Extra> e {
            { "Octave A", 0.5f, 0.5f }, { "Width A", 0.5f, 0.5f }, { "Wave B", 0.0f, 0.0f }, { "Semi B", 0.5f, 0.5f },
            { "Noise", 0.0f, 0.0f }, { "Drift", 0.3f, 0.3f }, { "Key", 0.3f, 0.0f }, { "Vel", 0.5f, 0.5f },
            { "Filter Attack", 0.02f, 0.02f }, { "Filter Decay", 0.3f, 0.3f }, { "Filter Sustain", 0.3f, 0.3f }, { "Filter Release", 0.3f, 0.3f },
            { "LFO 1 Rate", 0.5f, 0.5f }, { "LFO 2 Rate", 0.22f, 0.22f }, { "Glide", 0.0f, 0.0f }, { "Unison", 0.0f, 0.0f },
            { "Spread", 0.3f, 0.3f }, { "Vel Amp", 0.6f, 0.6f },
            { "Filter Mode", 0.0f, 0.0f }, { "Filter Follows Amp", 1.0f, 1.0f }, { "Hard Sync", 0.0f, 0.0f },
            { "LFO 1 Shape", 0.0f, 0.0f }, { "LFO 2 Shape", 1.0f, 1.0f }, { "Voicing", 0.0f, 0.0f } };
        // Modulation matrix: four rows of source, destination, amount (-1..1). New synths start with the
        // rows set up but at zero, so nothing moves until you turn one up.
        const float rows[4][2] = { { 0, 0 }, { 1, 2 }, { 2, 0 }, { 0, 1 } };
        for (int i = 0; i < 4; ++i)
        {
            const juce::String n ("Mod " + juce::String (i + 1));
            e.push_back ({ n + " Source", rows[i][0], rows[i][0] });
            e.push_back ({ n + " Destination", rows[i][1], rows[i][1] });
            e.push_back ({ n + " Amount", 0.0f, 0.0f });
        }
        e.push_back ({ "Osc Blend", 0.5f, 0.5f });
        return e;
    }

    std::vector<ModuleInfo::Extra> samplerExtras()
    {
        std::vector<ModuleInfo::Extra> e {
            { "Sensitivity", 0.55f, 0.55f }, { "Crossfade", 0.1f, 0.1f }, { "Fine", 0.5f, 0.5f }, { "Root", 0.5f, 0.5f },
            { "Hold", 0.0f, 0.0f }, { "Decay", 0.5f, 0.5f }, { "Sustain", 1.0f, 1.0f },
            { "Filter Cutoff", 1.0f, 1.0f }, { "Filter Res", 0.15f, 0.0f }, { "Drive", 0.0f, 0.0f }, { "Tape", 0.0f, 0.0f },
            { "Crush", 0.0f, 0.0f }, { "Pan", 0.5f, 0.5f }, { "Vel Gain", 0.7f, 1.0f },
            { "Play Mode", 0.0f, 0.0f }, { "Slice By", 0.0f, 0.0f }, { "Grid", 1.0f, 1.0f }, { "Reverse", 0.0f, 0.0f },
            { "Filter Mode", 0.0f, 0.0f }, { "Voices", 2.0f, 2.0f }, { "Loop Start", 0.25f, 0.25f }, { "Loop End", 0.5f, 0.5f },
            { "Warp", 0.0f, 0.0f } };
        for (int i = 0; i < Param::Sampler::maxMarkers; ++i)
            e.push_back ({ "Marker " + juce::String (i + 1), -1.0f, -1.0f });
        e.push_back ({ "Scratch", 0.0f, 0.0f });
        e.push_back ({ "Scratch Position", 0.0f, 0.0f });
        return e;
    }

    std::vector<ModuleInfo::Extra> drumExtras()
    {
        std::vector<ModuleInfo::Extra> e { { "Swing", 0.0f, 0.0f }, { "Dirt", 0.0f, 0.0f }, { "Selected Pad", 0.0f, 0.0f } };
        // Pads spread a little across the stereo field on a new rack; older racks stay in the middle
        const float pans[16] = { 0.5f, 0.4f, 0.5f, 0.6f, 0.5f, 0.3f, 0.62f, 0.7f, 0.25f, 0.35f, 0.62f, 0.7f, 0.45f, 0.3f, 0.5f, 0.72f };
        for (int i = 0; i < 16; ++i) e.push_back ({ "Pad " + juce::String (i + 1) + " Level", 0.8f, 0.8f });
        for (int i = 0; i < 16; ++i) e.push_back ({ "Pad " + juce::String (i + 1) + " Pitch", 0.5f, 0.5f });
        for (int i = 0; i < 16; ++i) e.push_back ({ "Pad " + juce::String (i + 1) + " Pan", pans[i], 0.5f });
        // The closed hat cuts the open hat off, as it always has
        for (int i = 0; i < 16; ++i) e.push_back ({ "Pad " + juce::String (i + 1) + " Choke", (i == 6 || i == 10) ? 1.0f : 0.0f, (i == 6 || i == 10) ? 1.0f : 0.0f });
        for (int i = 0; i < 16; ++i) e.push_back ({ "Pad " + juce::String (i + 1) + " Steps", 0.0f, 0.0f });
        // Kit velocity, then each pad's own shaping. Every default leaves an older rack sounding as it did:
        // no attack or hold, a decay that never ends (the kit's decay still shapes it), the filter open.
        e.push_back ({ "Velocity", 1.0f, 1.0f });
        struct PerPad { const char* name; float value; };
        const PerPad perPad[] = { { "Attack", 0.0f }, { "Hold", 0.0f }, { "Decay", 1.0f }, { "Start", 0.0f }, { "Reverse", 0.0f },
                                  { "Cutoff", 1.0f }, { "Res", 0.0f }, { "Filter Mode", 0.0f }, { "Drive", 0.0f }, { "Snap", 0.0f },
                                  { "Out", 0.0f }, { "Mute", 0.0f }, { "Solo", 0.0f }, { "Voice", 0.0f },
                                  { "Voice A", 0.5f }, { "Voice B", 0.5f }, { "Voice C", 0.5f } };
        for (auto& pp : perPad)
            for (int i = 0; i < 16; ++i)
                e.push_back ({ "Pad " + juce::String (i + 1) + " " + pp.name, pp.value, pp.value });
        jassert ((int) e.size() + 4 == Param::Drums::Count);
        return e;
    }

    std::vector<ModuleInfo::Extra> drumSeqExtras()
    {
        using namespace Param::DrumSeq;
        std::vector<ModuleInfo::Extra> e { { "Pattern", 0.0f, 0.0f }, { "Step Size", 1.0f, 1.0f }, { "Play", 1.0f, 1.0f }, { "Selected Row", 0.0f, 0.0f } };
        for (int r = 0; r < rows; ++r) e.push_back ({ "Row " + juce::String (r + 1) + " Length", 16.0f, 16.0f });
        const char* names = "ABCD";
        for (int p = 0; p < patterns; ++p)
            for (int r = 0; r < rows; ++r)
                for (int i = 0; i < steps; ++i)
                    e.push_back ({ juce::String::charToString ((juce::juce_wchar) names[p]) + " " + juce::String (r + 1) + "." + juce::String (i + 1), 0.0f, 0.0f });
        jassert ((int) e.size() + 2 == Count);
        return e;
    }

    ModuleInfo withAuxOut (ModuleInfo info) { info.hasAuxOut = true; return info; }

    std::vector<ModuleInfo::Extra> eqExtras()
    {
        using namespace Param::EQ;
        return { { "Low Mid", 0.0f, 0.0f },
                 { "Low Freq", valueOf (120.0f), valueOf (120.0f) }, { "Low Mid Freq", valueOf (400.0f), valueOf (400.0f) },
                 { "Mid Freq", valueOf (1000.0f), valueOf (1000.0f) }, { "High Freq", valueOf (6000.0f), valueOf (6000.0f) },
                 { "Low Width", 0.9f, 0.9f }, { "Low Mid Width", 0.9f, 0.9f }, { "Mid Width", 0.9f, 0.9f }, { "High Width", 0.9f, 0.9f },
                 { "Low Cut", 0.0f, 0.0f }, { "High Cut", 1.0f, 1.0f }, { "Tilt", 0.0f, 0.0f }, { "Analog Warmth", 0.0f, 0.0f } };
    }
    std::vector<ModuleInfo::Extra> crateExtras()
    {
        using namespace Param::Crate;
        std::vector<ModuleInfo::Extra> e { { "Spread", 0.45f, 0.45f }, { "Depth", 1.0f, 1.0f }, { "Era", 1.0f, 1.0f }, { "Selected Slice", 0.0f, 0.0f } };
        // Before the first dig the slices sit evenly round the record, at their own pitch, forwards
        for (int i = 0; i < slices; ++i) e.push_back ({ "Slice " + juce::String (i + 1) + " Position", (float) i / (float) slices, (float) i / (float) slices });
        for (int i = 0; i < slices; ++i) e.push_back ({ "Slice " + juce::String (i + 1) + " Pitch", 0.5f, 0.5f });
        for (int i = 0; i < slices; ++i) e.push_back ({ "Slice " + juce::String (i + 1) + " Reverse", 0.0f, 0.0f });
        for (int i = 0; i < slices; ++i) e.push_back ({ "Slice " + juce::String (i + 1) + " Lock", 0.0f, 0.0f });
        return e;
    }

    std::vector<ModuleInfo::Extra> crowdExtras()
    {
        using namespace Param::Crowd;
        std::vector<ModuleInfo::Extra> e { { "Tuning", 0.35f, 0.35f }, { "Sway", 0.35f, 0.35f }, { "Depth", 0.35f, 0.35f }, { "Preset", 1.0f, 1.0f } };
        // Where everyone stands to begin with: a fixed scatter, the first person front and centre
        float home[people][3];
        long long seed = 5;
        auto rnd = [&seed] { seed = (seed * 16807) % 2147483647; return (float) ((double) seed / 2147483647.0); };
        for (int i = 0; i < people; ++i)
        {
            home[i][0] = i ? rnd() * 2.0f - 1.0f : 0.0f;
            home[i][1] = i ? rnd() : 0.2f;
            home[i][2] = rnd();
        }
        for (int i = 0; i < people; ++i) e.push_back ({ "Person " + juce::String (i + 1) + " Across", home[i][0] * 0.5f + 0.5f, home[i][0] * 0.5f + 0.5f });
        for (int i = 0; i < people; ++i) e.push_back ({ "Person " + juce::String (i + 1) + " Back", home[i][1], home[i][1] });
        for (int i = 0; i < people; ++i) e.push_back ({ "Person " + juce::String (i + 1) + " Quirk", home[i][2], home[i][2] });
        return e;
    }

}

float ModuleInfo::defaultValue (int param, bool legacy) const
{
    if (param < 0) return 0.0f;
    if (param < (int) knobs.size()) return knobs[(size_t) param].second;
    const int e = param - (int) knobs.size();
    return e < (int) extras.size() ? (legacy ? extras[(size_t) e].legacy : extras[(size_t) e].fresh) : 0.0f;
}

juce::String ModuleInfo::paramName (int param) const
{
    if (param >= 0 && param < (int) knobs.size()) return knobs[(size_t) param].first;
    const int e = param - (int) knobs.size();
    return e >= 0 && e < (int) extras.size() ? extras[(size_t) e].name : juce::String();
}

JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wmissing-field-initializers")   // the module table leaves `extras` to withExtras()
const ModuleInfo& moduleInfo (ModuleType t)
{
    // Pal indices: Rose 0, Green 1, Sky 2, Gold 3, Violet 4, Blue 5, Amber 6, Ice 7
    // Fields: title, colour, audio in, audio out, mod out, knobs, width, notes in, notes out, signal ins, utility
    static const ModuleInfo input      { "Track In",   1, false, true,  false, {}, 74.0f, false, true };
    static const ModuleInfo output     { "Out",        1, true,  false, false, {}, 74.0f };
    static const ModuleInfo sampler = withExtras ({ "Sampler",    0, false, true,  false, { { "Start", 0.0f }, { "Length", 1.0f }, { "Pitch", 0.5f }, { "Attack", 0.0f }, { "Release", 0.3f }, { "Gain", 0.8f } }, 440.0f, true },
                                                  samplerExtras());
    static const ModuleInfo synth = withExtras ({ "Synth",      4, false, true,  false, { { "Wave", 0.0f }, { "Sub", 0.3f }, { "Detune", 0.15f }, { "Cutoff", 0.6f }, { "Res", 0.25f }, { "Env", 0.4f },
                                                                                          { "Attack", 0.02f }, { "Decay", 0.3f }, { "Sustain", 0.6f }, { "Release", 0.3f }, { "Drive", 0.2f }, { "Gain", 0.7f } }, 480.0f, true },
                                                synthExtras());
    static const ModuleInfo drums = withAuxOut (withExtras ({ "Drum Rack",  6, false, true,  false, { { "Tune", 0.5f }, { "Decay", 0.7f }, { "Snap", 0.3f }, { "Gain", 0.8f } }, 640.0f, true },
                                                            drumExtras()));
    // Sends notes, makes no sound: its notes in passes the track's notes on to the rack with its own steps
    static const ModuleInfo drumSeq = withExtras ({ "Drum Rack Sequencer", 6, false, false, false, { { "Swing", 0.0f }, { "Humanise", 0.0f } }, 380.0f, true, true },
                                                  drumSeqExtras());
    static const ModuleInfo filter = withExtras ({ "Filter",     2, true,  true,  false, { { "Cutoff", 0.65f }, { "Res", 0.3f }, { "Drive", 0.2f }, { "Mode", 0.0f } }, 320.0f },
                                                 { { "Mix", 1.0f, 1.0f }, { "Follow", 0.0f, 0.0f }, { "Wobble", 0.0f, 0.0f }, { "Wobble Rate", 0.3f, 0.3f },
                                                   { "Vowel", 0.0f, 0.0f }, { "Slope", 0.0f, 0.0f }, { "Wobble In Time", 0.0f, 0.0f } });
    static const ModuleInfo drive = withExtras ({ "Drive",      6, true,  true,  false, { { "Drive", 0.4f }, { "Tone", 0.6f }, { "Mix", 0.8f }, { "Type", 0.0f } }, 300.0f },
                                                { { "Bias", 0.5f, 0.5f }, { "Output", 0.7f, 0.7f }, { "Sag", 0.0f, 0.0f }, { "Oversample", 1.0f, 0.0f } });
    static const ModuleInfo delay = withExtras ({ "Delay",      5, true,  true,  false, { { "Time", 0.5f }, { "Feedback", 0.4f }, { "Tone", 0.5f }, { "Mix", 0.25f } }, 330.0f },
                                                { { "Flutter", 0.15f, 0.0f }, { "Duck", 0.0f, 0.0f }, { "Spread", 0.5f, 1.0f }, { "Ping Pong", 1.0f, 1.0f },
                                                  { "Freeze", 0.0f, 0.0f }, { "Tempo Sync", 1.0f, 1.0f } });
    static const ModuleInfo reverb = withExtras ({ "Reverb",     7, true,  true,  false, { { "Size", 0.6f }, { "Damp", 0.4f }, { "Width", 0.8f }, { "Mix", 0.3f } }, 310.0f },
                                                 { { "Pre-delay", 0.1f, 0.0f }, { "Low Cut", 0.0f, 0.0f }, { "Shimmer", 0.0f, 0.0f }, { "Drift", 0.2f, 0.0f },
                                                   { "Space", 1.0f, 1.0f }, { "Freeze", 0.0f, 0.0f }, { "Gated", 0.0f, 0.0f } });
    static const ModuleInfo eq = withExtras ({ "EQ",         3, true,  true,  false, { { "Low", 0.0f }, { "Mid", 0.0f }, { "High", 0.0f }, { "Output", 0.7f } }, 350.0f },
                                             eqExtras());
    static const ModuleInfo compressor = withExtras ({ "Compressor", 1, true,  true,  false, { { "Thresh", 0.5f }, { "Ratio", 0.4f }, { "Attack", 0.2f }, { "Release", 0.4f }, { "Makeup", 0.3f } }, 350.0f },
                                                     { { "Knee", 0.25f, 0.25f }, { "Mix", 1.0f, 1.0f }, { "Sidechain Cut", 0.0f, 0.0f }, { "Auto Makeup", 0.0f, 0.0f },
                                                       { "Character", 0.0f, 0.0f }, { "Listens To", 0.0f, 0.0f } });
    static const ModuleInfo lfo = withExtras ({ "LFO",        4, false, false, true,  { { "Rate", 0.4f }, { "Depth", 0.6f }, { "Shape", 0.0f } }, 310.0f },
                                              { { "Phase", 0.0f, 0.0f }, { "Smooth", 0.0f, 0.0f }, { "Fade In", 0.0f, 0.0f }, { "Chaos", 0.0f, 0.0f },
                                                { "Sync", 0.0f, 0.0f }, { "Polarity", 0.0f, 0.0f }, { "Restart On Note", 0.0f, 0.0f } });
    // The workshop devices
    static const ModuleInfo gasTube = withExtras ({ "Gas Tube",   6, false, true,  false, { { "Voltage", 0.3f }, { "Strike", 0.12f }, { "Body", 0.45f }, { "Glow", 0.7f }, { "Fade", 0.38f }, { "Gain", 0.7f } }, 400.0f, true },
                                                  { { "Pressure", 0.55f, 0.55f }, { "Hum", 0.22f, 0.22f }, { "Crackle", 0.18f, 0.18f }, { "Length", 0.5f, 0.5f },
                                                    { "Warm-up", 0.3f, 0.3f }, { "Gas", 0.0f, 0.0f }, { "Mains", 0.0f, 0.0f } });
    static const ModuleInfo crate = withExtras ({ "Crate Digger", 0, false, true, false, { { "Wear", 0.35f }, { "Dust", 0.3f }, { "Wow", 0.18f }, { "Pitch", 0.5f }, { "Chop", 0.5f }, { "Gain", 0.75f } }, 400.0f, true },
                                                crateExtras());
    static const ModuleInfo strings = withExtras ({ "String Wall", 7, false, true,  false, { { "Tone", 0.55f }, { "Sustain", 0.6f }, { "Pick", 0.3f }, { "Body", 0.5f }, { "Sympathy", 0.35f }, { "Bar", 0.42f }, { "Gain", 0.75f } }, 430.0f, true },
                                                  { { "Bow Pressure", 0.5f, 0.5f }, { "Slide Bar", 0.0f, 0.0f }, { "Pedal A", 0.0f, 0.0f }, { "Pedal B", 0.0f, 0.0f },
                                                    { "Mode", 0.0f, 0.0f }, { "Material", 1.0f, 1.0f }, { "Tuning", 0.0f, 0.0f } });
    static const ModuleInfo ghost = withExtras ({ "Ghost Tag",  4, true,  true,  false, { { "Time", 0.55f }, { "Fade", 0.6f }, { "Wash", 0.45f }, { "Mix", 0.45f } }, 320.0f },
                                                { { "Drift", 0.3f, 0.3f }, { "Blur", 0.25f, 0.25f }, { "Hold", 0.0f, 0.0f }, { "Hold As", 0.0f, 0.0f },
                                                  { "Hold From", 2.0f, 2.0f }, { "Hold To", 4.0f, 4.0f } });
    static const ModuleInfo crowd = withExtras ({ "Crowd",      3, true,  true,  false, { { "Size", 0.6f }, { "Spread", 0.55f }, { "Timing", 0.35f }, { "Mix", 0.6f } }, 320.0f },
                                                crowdExtras());
    static const ModuleInfo transformer = withExtras ({ "Transformer", 1, true, true, false, { { "Battery", 0.75f }, { "Cap", 0.5f }, { "Pot", 0.45f }, { "Diode", 0.5f }, { "Sag", 0.55f },
                                                                                              { "Clock", 0.4f }, { "Joint", 0.4f }, { "Hum", 0.35f }, { "Mix", 1.0f }, { "Output", 0.7f } }, 430.0f },
                                                      { { "Dying Cap On", 1.0f, 1.0f }, { "Scratchy Pot On", 1.0f, 1.0f }, { "Leaky Diode On", 0.0f, 0.0f },
                                                        { "Sagging Supply On", 1.0f, 1.0f }, { "Bent Clock On", 0.0f, 0.0f }, { "Cold Joint On", 0.0f, 0.0f },
                                                        { "Hot Hum On", 0.0f, 0.0f } });

    // A plugin may be an instrument or an effect, so it has both a notes and an audio input
    static const ModuleInfo plugin     { "Plugin",     5, true,  true,  false, { { "Mix", 1.0f }, { "Gain", 0.7f } }, 260.0f, true };

    // Utilities: small signal tools for the second row. They work on sound or on modulation.
    static const ModuleInfo attenuator { "Atten",      3, false, false, false, { { "Level", 1.0f } }, 120.0f, false, false, 1, true };
    static const ModuleInfo mixer      { "Mixer",      2, false, false, false, { { "1", 1.0f }, { "2", 1.0f }, { "3", 1.0f }, { "4", 1.0f } }, 290.0f, false, false, 4, true };
    static const ModuleInfo inverter   { "Invert",     0, false, false, false, {}, 110.0f, false, false, 1, true };
    static const ModuleInfo crossfade  { "X-Fade",     4, false, false, false, { { "Mix", 0.5f } }, 130.0f, false, false, 2, true };
    static const ModuleInfo slew       { "Slew",       7, false, false, false, { { "Time", 0.3f } }, 120.0f, false, false, 1, true };

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
        case ModuleType::GasTube:    return gasTube;
        case ModuleType::CrateDigger: return crate;
        case ModuleType::StringWall: return strings;
        case ModuleType::GhostTag:   return ghost;
        case ModuleType::Crowd:      return crowd;
        case ModuleType::Transformer: return transformer;
        case ModuleType::DrumSeq:    return drumSeq;
    }
    return filter;
}
JUCE_END_IGNORE_WARNINGS_GCC_LIKE

std::vector<int> ModuleInfo::ports() const
{
    std::vector<int> j;
    if (hasAudioIn)  j.push_back (Port::audioIn);
    if (hasNotesIn)  j.push_back (Port::notesIn);
    for (int i = 0; i < sigIns; ++i) j.push_back (Port::sigIn (i));
    if (hasAudioOut) j.push_back (Port::audioOut);
    if (hasAuxOut)   j.push_back (Port::auxOut);
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
    if (port == Port::auxOut)   return hasAuxOut;
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
    for (int i = 0; i < info.numParams(); ++i)
        m.knobValues.push_back (info.defaultValue (i));
    if (type == ModuleType::CrateDigger)
        m.sampleName = "Found Record";          // the built-in soul loop, until you load your own
    modules.push_back (m);
    const int id = m.id;

    if (info.utility)
        return id;                  // utilities are patched by hand

    if (type == ModuleType::DrumSeq)
    {
        // In front of the first Drum Rack that has no sequencer yet (or loose, to patch by hand)
        for (auto& other : modules)
            if (other.type == ModuleType::Drums)
            {
                const int into = cableInto (other.id, Port::notesIn);
                const auto* src = into >= 0 ? find (cables[(size_t) into].fromModule) : nullptr;
                if (src == nullptr || src->type != ModuleType::DrumSeq)
                {
                    placeBefore (id, other.id);
                    if (src != nullptr) connect (src->id, cables[(size_t) into].fromPort, id, Port::notesIn);
                    else connect (inputId, Port::notesOut, id, Port::notesIn);
                    connect (id, Port::notesOut, other.id, Port::notesIn);
                    return id;
                }
            }
        connect (inputId, Port::notesOut, id, Port::notesIn);
        if (auto* out = find (outputId))
            out->position.x = juce::jmax (out->position.x, find (id)->position.x + info.width + gap);
        return id;
    }

    // Keep the output at the end of the row
    if (auto* out = find (outputId))
        out->position.x = juce::jmax (out->position.x, m.position.x + info.width + gap);

    if (info.isInstrument() || (type == ModuleType::Plugin && asInstrument))
    {
        wireInstrument (id);
        // A Drum Rack added after its sequencer: take the notes of a sequencer that plays nothing yet
        if (type == ModuleType::Drums)
            for (auto& other : modules)
                if (other.type == ModuleType::DrumSeq
                    && std::none_of (cables.begin(), cables.end(), [&] (const Cable& c) { return c.fromModule == other.id && c.fromPort == Port::notesOut; }))
                {
                    connect (other.id, Port::notesOut, id, Port::notesIn);
                    break;
                }
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

// Slide a new card in just left of another: everything from there on moves right to make room
void Patch::placeBefore (int id, int before)
{
    auto* m = find (id);
    const auto* b = find (before);
    if (m == nullptr || b == nullptr)
        return;
    const float x = b->position.x, room = moduleInfo (m->type).width + gap;
    for (auto& o : modules)
        if (o.id != id && ! moduleInfo (o.type).utility && o.position.x >= x - 1.0f)
            o.position.x += room;
    m->position.x = x;
}

int Patch::addSequencerFor (int drumRack)
{
    const int into = cableInto (drumRack, Port::notesIn);
    if (into >= 0)
        if (const auto* src = find (cables[(size_t) into].fromModule); src != nullptr && src->type == ModuleType::DrumSeq)
            return src->id;
    const auto& info = moduleInfo (ModuleType::DrumSeq);
    Module m;
    m.id = nextId++;
    m.type = ModuleType::DrumSeq;
    m.position = { 12.0f, top };
    for (int i = 0; i < info.numParams(); ++i)
        m.knobValues.push_back (info.defaultValue (i));
    modules.push_back (m);
    placeBefore (m.id, drumRack);
    if (into >= 0)
    {
        const auto was = cables[(size_t) into];
        connect (was.fromModule, was.fromPort, m.id, Port::notesIn);
    }
    else
        connect (inputId, Port::notesOut, m.id, Port::notesIn);
    connect (m.id, Port::notesOut, drumRack, Port::notesIn);
    return m.id;
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
    else if (info.hasNotesIn && info.hasNotesOut) { inPort = Port::notesIn; outPort = Port::notesOut; }   // notes pass on
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

// A rack's own step rows (one bit per sixteenth, the downbeats a little louder, the off-beats swung)
// become pattern A of a sequencer in front of it, so the project plays just as it did.
void Patch::moveDrumSteps (const std::vector<int>& oldRacks)
{
    using namespace Param;
    for (int rack : oldRacks)
    {
        auto* m = find (rack);
        if (m == nullptr)
            continue;
        const float swing = m->knobValues[(size_t) Drums::Swing];
        std::array<int, 16> masks {};
        for (int p = 0; p < 16; ++p)
        {
            masks[(size_t) p] = juce::roundToInt (m->knobValues[(size_t) (Drums::Steps0 + p)]);
            m->knobValues[(size_t) (Drums::Steps0 + p)] = 0.0f;
        }
        m->knobValues[(size_t) Drums::Swing] = 0.0f;
        const int seq = addSequencerFor (rack);
        auto* s = find (seq);
        s->knobValues[(size_t) DrumSeq::Swing] = swing;
        for (int p = 0; p < 16; ++p)
            for (int i = 0; i < 16; ++i)
                if ((masks[(size_t) p] >> i) & 1)
                {
                    DrumSeq::Step st;
                    st.on = true;
                    st.velocity = i % 4 == 0 ? 121 : 95;        // 0.95 and 0.75, as the rack played them
                    s->knobValues[(size_t) DrumSeq::stepParam (0, p, i)] = DrumSeq::encode (st);
                }
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
        if (m.kitName.isNotEmpty()) mo->setProperty ("kit", m.kitName);
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
    std::vector<int> oldRacks;
    if (auto* ms = v["modules"].getArray())
        for (auto& mv : *ms)
        {
            Module m;
            m.id = (int) mv["id"];
            m.type = (ModuleType) juce::jlimit (0, numModuleTypes - 1, (int) mv["type"]);
            m.position = { (float) mv["x"], (float) mv["y"] };
            const auto& info = moduleInfo (m.type);
            // Controls the file doesn't have yet take the value that keeps it sounding as it did
            for (int i = 0; i < info.numParams(); ++i) m.knobValues.push_back (info.defaultValue (i, true));
            int saved = 0;
            if (auto* ks = mv["knobs"].getArray())
                for (int i = 0; i < ks->size() && i < (int) m.knobValues.size(); ++i, ++saved)
                    m.knobValues[(size_t) i] = (float) (*ks)[i];
            // A rack saved before the sequencer was its own device, with steps in it
            if (m.type == ModuleType::Drums && saved <= Param::Drums::Steps0 + 16)
                for (int p = 0; p < 16; ++p)
                    if (juce::roundToInt (m.knobValues[(size_t) (Param::Drums::Steps0 + p)]) != 0) { oldRacks.push_back (m.id); break; }
            // ...and with pitch over an octave each way, where it now goes two
            if (m.type == ModuleType::Drums && saved <= Param::Drums::Steps0 + 16)
                for (int p = 0; p < 16; ++p)
                {
                    auto& pv = m.knobValues[(size_t) (Param::Drums::Pitch0 + p)];
                    pv = 0.5f + (pv - 0.5f) * 0.5f;
                }
            // Before the expanded view the synth's two oscillators shared one wave
            if (m.type == ModuleType::Synth && saved <= Param::Synth::WaveB)
                m.knobValues[(size_t) Param::Synth::WaveB] = m.knobValues[(size_t) Param::Synth::Wave];
            m.sampleName = mv["sample"].toString();
            if (auto* ps = mv["pads"].getArray())
                for (int i = 0; i < ps->size() && i < 16; ++i)
                    m.padNames[(size_t) i] = (*ps)[i].toString();
            m.kitName = mv["kit"].toString();
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
    moveDrumSteps (oldRacks);
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
