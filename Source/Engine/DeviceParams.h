#pragma once

#include <juce_core/juce_core.h>
#include <cmath>
#include <algorithm>
#include <vector>
#include "../DSP/Analog.h"

// Where each device keeps its controls. A module's values are its card knobs first (the ones on the
// rack card, each with a modulation jack), then the extra controls that only the expanded view shows.
// Choices (modes, toggles) are stored as whole numbers. The engine and the expanded view both use
// these names, so a control means the same thing on both sides.
namespace Param
{
    inline float expMap (float v, float lo, float hi);

    namespace Synth
    {
        enum
        {
            Wave, Sub, Detune, Cutoff, Res, Env, Attack, Decay, Sustain, Release, Drive, Gain,
            OctA, WidthA, WaveB, SemiB, Noise, Drift, Key, Vel, FAttack, FDecay, FSustain, FRelease,
            L1Rate, L2Rate, Glide, Unison, Spread, VelAmp,
            FMode, Link, Sync, L1Shape, L2Shape, VMode,
            MxSrc0, MxDst0, MxAmt0, MxSrc1, MxDst1, MxAmt1, MxSrc2, MxDst2, MxAmt2, MxSrc3, MxDst3, MxAmt3,
            Blend,          // how much of A and B: 0.5 is both at full
            Count
        };
        enum FilterMode { LP24, LP12, BP, HP };
        enum Voicing { Poly, Mono, Legato };
        enum Source { Lfo1, Lfo2, Velocity, Random };
        enum Dest { ToCutoff, ToPitch, ToWidthA, ToResonance };
        constexpr int matrixRows = 4;
    }

    namespace Sampler
    {
        enum
        {
            Start, Length, Pitch, Attack, Release, Gain,
            Sens, XFade, Fine, Root, Hold, Decay, Sustain, Cutoff, Res, Drive, Tape, Crush, Pan, VelGain,
            Mode, Slicer, Grid, Reverse, FMode, Voices, LoopA, LoopB, Warp,
            Marker0, Scrub = Marker0 + 15, ScrubPos,    // the crank's scratch: on, and where the hand has the film
            Count
        };
        enum PlayMode { Classic, OneShot, Slice, Loop };
        enum SliceBy { Transient, GridSlices, Manual };
        constexpr int maxMarkers = 15;
        inline int gridCount (int choice)  { return choice <= 0 ? 4 : (choice == 1 ? 8 : 16); }
        inline int voiceCount (int choice) { return choice <= 0 ? 1 : (choice == 1 ? 4 : (choice == 2 ? 8 : 16)); }
        inline int rootNote (float v)      { return 36 + juce::roundToInt (juce::jlimit (0.0f, 1.0f, v) * 48.0f); }
    }

    // Kit-wide controls first (tune, decay, snap, gain, dirt, velocity), then sixteen of each per-pad
    // control. Swing and Steps are from before the sequencer had its own device: a project that still
    // has them is moved onto a Drum Rack Sequencer when it loads, and the rack itself ignores them.
    namespace Drums
    {
        enum { Tune, Decay, Snap, Gain, Swing, Dirt, Selected, Level0, Pitch0 = Level0 + 16, Pan0 = Pitch0 + 16,
               Choke0 = Pan0 + 16, Steps0 = Choke0 + 16,
               VelSens = Steps0 + 16,
               Attack0, Hold0 = Attack0 + 16, PDecay0 = Hold0 + 16, Start0 = PDecay0 + 16, Reverse0 = Start0 + 16,
               Cutoff0 = Reverse0 + 16, Res0 = Cutoff0 + 16, FMode0 = Res0 + 16, Drive0 = FMode0 + 16, PSnap0 = Drive0 + 16,
               Out0 = PSnap0 + 16, Mute0 = Out0 + 16, Solo0 = Mute0 + 16, Voice0 = Solo0 + 16,
               VoiceA0 = Voice0 + 16, VoiceB0 = VoiceA0 + 16, VoiceC0 = VoiceB0 + 16, Count = VoiceC0 + 16 };
        enum FilterMode { LP, BP, HP };
        enum Voice { Sample, Kick, Snare, Hat, Clap, Tom, Perc, numVoices };
        inline const char* voiceName (int v)
        {
            static const char* n[] = { "sample", "kick", "snare", "hat", "clap", "tom", "perc" };
            return n[juce::jlimit (0, (int) numVoices - 1, v)];
        }
        // What each analogue voice's three controls do, in the order VoiceA, VoiceB, VoiceC
        inline const char* voiceParam (int v, int which)
        {
            static const char* n[numVoices][3] = { { "", "", "" }, { "tone", "sweep", "click" }, { "tone", "snappy", "body" },
                                                   { "metal", "noise", "closed" }, { "spread", "colour", "tail" },
                                                   { "tone", "sweep", "skin" }, { "tone", "ratio", "ring" } };
            return n[juce::jlimit (0, (int) numVoices - 1, v)][juce::jlimit (0, 2, which)];
        }

        // The pad's own envelope: attack and hold in seconds, the decay's time constant (0: it never fades)
        inline float attackSec (float v) { return v * v * 0.25f; }
        inline float holdSec (float v)   { return v * v * 0.6f; }
        inline float decayTau (float v)  { return v >= 0.99f ? 0.0f : expMap (v, 0.01f, 1.2f); }
        inline float padEnvelope (float t, float atk, float hold, float dec)
        {
            const float a = attackSec (atk), h = holdSec (hold), tau = decayTau (dec);
            if (t < a) return t / a;
            if (t < a + h || tau <= 0.0f) return 1.0f;
            return std::exp (-(t - a - h) / tau);
        }
        inline float cutoffHz (float v) { return expMap (v, 40.0f, 18000.0f); }
    }

    // The Drum Rack Sequencer: a groove (swing, humanise, pattern, step size, play), a length for each
    // row, then four patterns of sixteen rows of sixteen steps. A step is one stored number: 0 is off,
    // otherwise 1 + velocity (0..127) + 128 x how much chance is taken away (0..20, 5% each)
    // + 2688 x (ratchet - 1), the ratchet being 1..4 hits in the step.
    namespace DrumSeq
    {
        constexpr int rows = 16, steps = 16, patterns = 4;
        enum { Swing, Humanise, Pattern, Rate, Play, Selected, Length0, Step0 = Length0 + rows, Count = Step0 + patterns * rows * steps };
        inline int stepParam (int pattern, int row, int step) { return Step0 + (juce::jlimit (0, patterns - 1, pattern) * rows + juce::jlimit (0, rows - 1, row)) * steps + juce::jlimit (0, steps - 1, step); }
        // Step size in beats: 1/8, 1/16, 1/16 triplets, 1/32
        inline double stepBeats (int rate) { static const double b[] = { 0.5, 0.25, 1.0 / 6.0, 0.125 }; return b[juce::jlimit (0, 3, rate)]; }
        inline const char* rateName (int rate) { static const char* n[] = { "1/8", "1/16", "1/16t", "1/32" }; return n[juce::jlimit (0, 3, rate)]; }

        struct Step
        {
            bool on = false;
            int velocity = 100;      // 0..127
            int chance = 100;        // percent, in fives
            int ratchet = 1;         // hits in the step, 1..4
            bool accent() const { return velocity >= 115; }
        };
        inline Step decode (float stored)
        {
            Step s;
            const int v = juce::roundToInt (stored);
            if (v <= 0) return s;
            const int x = v - 1;
            s.on = true;
            s.velocity = x % 128;
            s.chance = 100 - 5 * juce::jlimit (0, 20, (x / 128) % 21);
            s.ratchet = 1 + juce::jlimit (0, 3, x / 2688);
            return s;
        }
        inline float encode (const Step& s)
        {
            if (! s.on) return 0.0f;
            const int miss = juce::jlimit (0, 20, (100 - s.chance + 2) / 5);
            return (float) (1 + juce::jlimit (0, 127, s.velocity) + 128 * miss + 2688 * (juce::jlimit (1, 4, s.ratchet) - 1));
        }
    }

    namespace Filter  { enum { Cutoff, Res, Drive, Mode, Mix, Follow, Wobble, Rate, Vowel, Slope, WobbleSync, Count }; }
    namespace Drive   { enum { Amount, Tone, Mix, Type, Bias, Output, Sag, Oversample, Count }; }
    namespace Delay   { enum { Time, Feedback, Tone, Mix, Flutter, Duck, Spread, PingPong, Freeze, Sync, Count }; }
    namespace Reverb  { enum { Size, Damp, Width, Mix, PreDelay, LowCut, Shimmer, Drift, Space, Freeze, Gated, Count };
                        enum SpaceKind { Room, Hall, Plate, Cave }; }
    namespace EQ      { enum { Low, Mid, High, Output, LowMid, Freq0, Q0 = Freq0 + 4, LowCut = Q0 + 4, HighCut, Tilt, Analog, Count };
                        // Bands in order low shelf, low mid, mid, high shelf. Gains are -1..1 (12 dB each way).
                        constexpr int gainParam[4] = { Low, LowMid, Mid, High };
                        constexpr float tiltWeight[4] = { -1.0f, -0.5f, 0.5f, 1.0f };
                        inline float freqOf (float v) { return 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f, v)); }
                        inline float valueOf (float hz) { return std::log (juce::jlimit (20.0f, 20000.0f, hz) / 20.0f) / std::log (1000.0f); } }
    namespace Comp    { enum { Thresh, Ratio, Attack, Release, Makeup, Knee, Mix, SideCut, AutoMakeup, Character, ListensTo, Count };
                        enum Feel { Clean, Glue, Crush }; }
    namespace LFO     { enum { Rate, Depth, Shape, Phase, Smooth, Fade, Chaos, Sync, Polarity, Retrig, Count }; }

    // The workshop devices: three instruments and three effects that are not the usual kind
    namespace GasTube
    {
        enum { Voltage, Strike, Body, Glow, Fade, Gain, Pressure, Hum, Crackle, Length, WarmUp, Gas, Mains, Count };
        enum Kind { Neon, Argon, Mercury, Xenon };
        constexpr int tubes = 6;                       // one glass tube per voice
        constexpr int numGases = 4;
        // Each gas's line spectrum: the level of harmonic n (1 = the fundamental), and how breathy it is
        inline float harmonic (int gas, int n)
        {
            switch (gas)
            {
                case Argon:   return n % 2 ? 1.0f / (float) n : 0.06f / (float) n;           // hollow: odd harmonics
                case Mercury: return n == 1 ? 1.0f : n == 2 ? 0.38f : n == 3 ? 0.06f : n == 4 ? 0.14f : n == 6 ? 0.04f : 0.0f;
                case Xenon:   return std::pow ((float) n, -0.62f);                           // bright, every harmonic
                default:      return (n == 3 ? 1.4f : 1.0f) / (float) n;                     // neon: warm, a strong third
            }
        }
        inline float breath (int gas) { return gas == Argon ? 0.01f : gas == Mercury ? 0.0f : gas == Xenon ? 0.05f : 0.015f; }
    }

    namespace Crate
    {
        constexpr int slices = 16;
        enum { Wear, Dust, Wow, Pitch, Chop, Gain, Spread, Depth, Era, Selected,
               Pos0, Shift0 = Pos0 + slices, Rev0 = Shift0 + slices, Lock0 = Rev0 + slices, Count = Lock0 + slices };
        enum EraKind { Sixties, Seventies, Eighties, Nineties };
        constexpr int firstNote = 48;                  // C3 plays slice 1, up to D#4 for slice 16
        // How long a slice plays, in beats: a sixteenth up to a bar
        constexpr float chopBeats[5] = { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };
        inline int chopIndex (float v) { return juce::jlimit (0, 4, juce::roundToInt (v * 4.0f)); }
        // A slice's own transposition is stored 0..1 for -12..+12 semitones
        inline int shiftOf (float v)   { return juce::roundToInt ((juce::jlimit (0.0f, 1.0f, v) - 0.5f) * 24.0f); }
        inline float shiftValue (int semis) { return (float) juce::jlimit (-12, 12, semis) / 24.0f + 0.5f; }
        // The pressing's band: highpass and lowpass corners for each era
        constexpr float eraLow[4]  = { 200.0f, 70.0f, 35.0f, 20.0f };
        constexpr float eraHigh[4] = { 4500.0f, 8500.0f, 13000.0f, 17000.0f };
    }

    namespace Strings
    {
        constexpr int count = 8;
        enum { Tone, Sustain, Pick, Body, Sympathy, Bar, Gain, BowPressure, BarOn, PedalA, PedalB, Mode, Material, Tuning, Count };
        enum Play { Pluck, Bow, Feedback };
        enum Mat { Nylon, Steel, Wire, Gut };
        constexpr int numTunings = 4;
        constexpr int tunings[numTunings][count] = {
            { 36, 43, 48, 52, 55, 60, 64, 67 },       // Open C
            { 47, 52, 54, 56, 59, 64, 66, 68 },       // Steel E9
            { 48, 50, 52, 53, 55, 57, 59, 60 },       // Harp
            { 38, 45, 50, 57, 62, 64, 69, 74 } };     // Drone D
        // Material: brightness and sustain multipliers, and stiffness (how sharp the upper partials run)
        constexpr float matTone[4] = { 0.55f, 1.0f, 1.2f, 0.42f };
        constexpr float matSustain[4] = { 0.6f, 1.0f, 1.5f, 0.45f };
        constexpr float matStiffness[4] = { 0.0f, 0.18f, 0.32f, 0.0f };
        // The pedals, as on a pedal steel: string index and semitones raised
        constexpr int pedalA[2][2] = { { 1, 2 }, { 4, 2 } };
        constexpr int pedalB[2][2] = { { 3, 1 }, { 7, 1 } };
        inline float pedalSemis (int string, float a, float b)
        {
            float s = 0.0f;
            for (auto& p : pedalA) if (p[0] == string) s += (float) p[1] * a;
            for (auto& p : pedalB) if (p[0] == string) s += (float) p[1] * b;
            return s;
        }
        // Which string plays a note from the keyboard: the highest one tuned at or below it
        inline int stringFor (int tuning, int note)
        {
            int s = 0;
            for (int k = 0; k < count; ++k) if (tunings[juce::jlimit (0, numTunings - 1, tuning)][k] <= note) s = k;
            return s;
        }
    }

    namespace Ghost
    {
        constexpr int echoes = 7;
        enum { Time, Fade, Wash, Mix, Drift, Blur, Hold, HoldHow, HoldFrom, HoldTo, Count };
        enum How { Freeze, Reverse };
    }

    namespace Crowd
    {
        constexpr int people = 12;
        enum { Size, Spread, Timing, Mix, Tuning, Sway, Depth, Preset, HomeX0, HomeY0 = HomeX0 + people, Quirk0 = HomeY0 + people,
               Count = Quirk0 + people };
        enum Presets { Unison, Choir, Rabble, Stadium };
        inline int headcount (float size) { return 2 + juce::roundToInt (juce::jlimit (0.0f, 1.0f, size) * 10.0f); }
        // Where person i stands: across the stage -1..1 and back from the front 0..1, and how big they look
        struct Place { float sx, sy, scale; };
        inline Place place (float homeX, float homeY, float spread, float depth)
        {
            const float sx = juce::jlimit (-1.0f, 1.0f, (homeX * 2.0f - 1.0f) * spread);
            const float sy = juce::jlimit (0.0f, 1.0f, depth * 0.5f + homeY * spread * 0.5f);
            return { sx, sy, 1.0f - sy * 0.42f };
        }
        // How late a person sings (seconds): their own looseness plus the distance to them
        inline float lateness (float timing, float quirk, float sy) { return 0.006f + timing * 0.04f * quirk + sy * 0.03f; }
        // Preset values for Size, Spread, Timing, Tuning, Sway, Depth
        constexpr float presets[4][6] = { { 0.1f, 0.12f, 0.05f, 0.12f, 0.25f, 0.1f }, { 0.6f, 0.55f, 0.35f, 0.35f, 0.35f, 0.35f },
                                          { 1.0f, 0.95f, 0.85f, 0.7f, 0.6f, 0.5f },   { 1.0f, 1.0f, 0.6f, 0.45f, 0.2f, 0.95f } };
    }

    namespace Transformer
    {
        constexpr int parts = 7;
        enum { Battery, Cap, Pot, Diode, Sag, Clock, Joint, Hum, Mix, Output, On0, Count = On0 + parts };
        enum Part { DyingCap, ScratchyPot, LeakyDiode, SaggingSupply, BentClock, ColdJoint, HotHum };
        inline float volts (float battery) { return 3.0f + juce::jlimit (0.0f, 1.0f, battery) * 6.0f; }   // a 9 V battery going flat
    }

    //==========================================================================
    // Maths both sides share

    inline float expMap (float v, float lo, float hi) { return lo * std::pow (hi / lo, juce::jlimit (0.0f, 1.0f, v)); }
    inline float inverseExp (float x, float lo, float hi) { return juce::jlimit (0.0f, 1.0f, std::log (x / lo) / std::log (hi / lo)); }

    // Tempo divisions: the delay's times, in beats
    constexpr int numDivisions = 7;
    constexpr float divisionBeats[numDivisions] = { 0.25f, 1.0f / 3.0f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f };
    inline int divisionIndex (float v) { return juce::jlimit (0, numDivisions - 1, (int) (v * 6.999f)); }

    // The filter's wobble when it follows the tempo: four bars down to a sixteenth
    constexpr float wobbleBeats[7] = { 16.0f, 8.0f, 4.0f, 2.0f, 1.0f, 0.5f, 0.25f };
    // The LFO's sync choices after Free: 1/4, 1/8, 1/16, one bar
    constexpr float lfoSyncBeats[5] = { 0.0f, 1.0f, 0.5f, 0.25f, 4.0f };

    // LFO shapes: sine, triangle, saw down, square, sample and hold (`step` picks the held value)
    inline float lfoShape (int shape, double phase)
    {
        const double fl = std::floor (phase);
        const float t = (float) (phase - fl);
        switch (shape)
        {
            case 0: return std::sin (analog::twoPi * t);
            case 1: return t < 0.5f ? 4.0f * t - 1.0f : 3.0f - 4.0f * t;
            case 2: return 1.0f - 2.0f * t;
            case 3: return t < 0.5f ? 1.0f : -1.0f;
            default:
            {
                const double x = std::sin (fl * 127.1 + 311.7) * 43758.5453;
                return (float) (x - std::floor (x)) * 2.0f - 1.0f;
            }
        }
    }

    // The LFO module's output for a phase, before depth: smoothing, chaos and polarity applied
    inline float lfoValue (int shape, double phase, float smooth, float chaos, bool unipolar)
    {
        float v;
        if (smooth > 0.02f)
        {
            float s = 0.0f;
            for (int i = 0; i < 7; ++i)
                s += lfoShape (shape, phase - (i / 7.0) * smooth * 0.5);
            v = s / 7.0f;
        }
        else
            v = lfoShape (shape, phase);
        if (chaos > 0.02f)
            v += chaos * 0.6f * (float) std::sin (phase * 7.31 + std::sin (phase * 2.3) * 3.0);
        v = juce::jlimit (-1.0f, 1.0f, v);
        return unipolar ? v * 0.5f + 0.5f : v;
    }

    // The Drive module's curves, with bias: the same function draws the picture and shapes the sound
    inline float driveShape (int type, float x)
    {
        switch (type)
        {
            case 0: return analog::sat::tube (x);
            case 1: return analog::sat::tape (x);
            case 2: return analog::sat::fuzz (x);
            default: return analog::sat::fold (x);
        }
    }
    inline float driveCurve (int type, float drive, float bias, float x)
    {
        const float g = 1.0f + drive * 24.0f, b = (bias - 0.5f) * 0.8f;
        return (driveShape (type, (x + b) * g) - driveShape (type, b * g)) / std::sqrt (g) * 1.6f;
    }

    // The compressor's gain reduction (dB) for a level (dB)
    inline float compReduction (float levelDb, float threshDb, float ratio, float kneeDb)
    {
        const float over = levelDb - threshDb, knee = juce::jmax (0.01f, kneeDb);
        if (over > knee * 0.5f)  return over - over / ratio;
        if (over > -knee * 0.5f) { const float x = over + knee * 0.5f; return (1.0f - 1.0f / ratio) * x * x / (2.0f * knee); }
        return 0.0f;
    }

    // Sampler slices. Markers are positions 0..1 in the file; slices run between them inside start..end.
    struct SliceSettings { float start = 0.0f, end = 1.0f, sens = 0.55f; int slicer = 0, grid = 8; std::vector<float> manual; };
    inline std::vector<float> sliceMarkers (const SliceSettings& s, const std::vector<std::pair<float, float>>& onsets)
    {
        std::vector<float> m;
        const float a = s.start, b = s.end;
        if (s.slicer == Sampler::GridSlices)
            for (int i = 1; i < s.grid; ++i) m.push_back (a + (b - a) * (float) i / (float) s.grid);
        else if (s.slicer == Sampler::Manual)
            m = s.manual;
        else
        {
            const float thr = 0.03f + 0.6f * std::pow (1.0f - s.sens, 1.6f);
            for (auto& o : onsets) if (o.second >= thr) m.push_back (o.first);
        }
        std::vector<float> keep;
        for (float x : m) if (x > a + 0.004f && x < b - 0.004f) keep.push_back (x);
        std::sort (keep.begin(), keep.end());
        if (keep.size() > (size_t) Sampler::maxMarkers) keep.resize ((size_t) Sampler::maxMarkers);
        return keep;
    }
    inline std::vector<std::pair<float, float>> slicesFrom (float start, float end, const std::vector<float>& markers)
    {
        std::vector<std::pair<float, float>> out;
        float prev = start;
        for (float x : markers) { out.push_back ({ prev, x }); prev = x; }
        out.push_back ({ prev, end });
        return out;
    }
}
