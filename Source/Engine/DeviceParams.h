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
    namespace Synth
    {
        enum
        {
            Wave, Sub, Detune, Cutoff, Res, Env, Attack, Decay, Sustain, Release, Drive, Gain,
            OctA, WidthA, WaveB, SemiB, Noise, Drift, Key, Vel, FAttack, FDecay, FSustain, FRelease,
            L1Rate, L2Rate, Glide, Unison, Spread, VelAmp,
            FMode, Link, Sync, L1Shape, L2Shape, VMode,
            MxSrc0, MxDst0, MxAmt0, MxSrc1, MxDst1, MxAmt1, MxSrc2, MxDst2, MxAmt2, MxSrc3, MxDst3, MxAmt3,
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
            Marker0, Count = Marker0 + 15
        };
        enum PlayMode { Classic, OneShot, Slice, Loop };
        enum SliceBy { Transient, GridSlices, Manual };
        constexpr int maxMarkers = 15;
        inline int gridCount (int choice)  { return choice <= 0 ? 4 : (choice == 1 ? 8 : 16); }
        inline int voiceCount (int choice) { return choice <= 0 ? 1 : (choice == 1 ? 4 : (choice == 2 ? 8 : 16)); }
        inline int rootNote (float v)      { return 36 + juce::roundToInt (juce::jlimit (0.0f, 1.0f, v) * 48.0f); }
    }

    namespace Drums
    {
        enum { Tune, Decay, Snap, Gain, Swing, Dirt, Selected, Level0, Pitch0 = Level0 + 16, Pan0 = Pitch0 + 16,
               Choke0 = Pan0 + 16, Steps0 = Choke0 + 16, Count = Steps0 + 16 };
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
