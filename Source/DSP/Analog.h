#pragma once

#include <juce_core/juce_core.h>
#include <cmath>

// Virtual analog building blocks. Everything here models circuit behaviour rather than
// idealised maths: band-limited oscillators with drift, an RC-curve envelope, a four-pole
// transistor ladder filter with saturating stages, and soft clippers for drive.
namespace analog
{
    constexpr float pi = 3.14159265358979f;
    constexpr float twoPi = 2.0f * pi;

    inline float fastTanh (float x)
    {
        // Pade approximation of tanh, accurate to ~1e-4 in the audio range, and cheap
        x = juce::jlimit (-4.9f, 4.9f, x);
        const float x2 = x * x;
        return x * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2))) / (135135.0f + x2 * (62370.0f + x2 * (3150.0f + 28.0f * x2)));
    }

    //==========================================================================
    // Band-limited oscillator (PolyBLEP) with a little analog drift and a sub octave.
    struct Oscillator
    {
        enum Wave { Saw, Square, Triangle, Sine };

        void prepare (double sampleRate) { sr = (float) sampleRate; }
        void reset (float startPhase = 0.0f) { phase = startPhase; triState = 0.0f; }

        void setFrequency (float hz) { inc = juce::jlimit (0.0f, 0.45f, hz / sr); }

        // Continuous wave morph: 0 saw, 1 square, 2 triangle, 3 sine
        float process (float shape, float pulseWidth = 0.5f)
        {
            const float t = phase;
            phase += inc;
            if (phase >= 1.0f) phase -= 1.0f;

            auto blep = [this] (float x)
            {
                if (x < inc)       { x /= inc; return x + x - x * x - 1.0f; }
                if (x > 1.0f - inc) { x = (x - 1.0f) / inc; return x * x + x + x + 1.0f; }
                return 0.0f;
            };

            const float saw = 2.0f * t - 1.0f - blep (t);
            float sq = (t < pulseWidth ? 1.0f : -1.0f) + blep (t) - blep (std::fmod (t + 1.0f - pulseWidth, 1.0f));
            // Triangle: leaky integral of the square
            triState = 0.999f * triState + inc * 4.0f * sq;
            const float tri = triState * 0.98f;
            const float sine = std::sin (twoPi * t);

            shape = juce::jlimit (0.0f, 3.0f, shape);
            if (shape < 1.0f) return saw + (sq - saw) * shape;
            if (shape < 2.0f) return sq + (tri - sq) * (shape - 1.0f);
            return tri + (sine - tri) * (shape - 2.0f);
        }

        float sr = 44100.0f, phase = 0.0f, inc = 0.0f, triState = 0.0f;
    };

    //==========================================================================
    // Analog-style ADSR: the capacitor charges toward an overshoot target (so the attack is
    // convex, like a real RC curve) and discharges exponentially on decay and release.
    struct Envelope
    {
        void prepare (double sampleRate) { sr = (float) sampleRate; }

        void set (float attackSec, float decaySec, float sustain, float releaseSec)
        {
            attackCoef  = coefFor (attackSec);
            decayCoef   = coefFor (decaySec);
            releaseCoef = coefFor (releaseSec);
            sus = juce::jlimit (0.0f, 1.0f, sustain);
        }

        void noteOn()  { stage = Attack; }
        void noteOff() { if (stage != Idle) stage = Release; }
        bool isActive() const { return stage != Idle; }

        float process()
        {
            switch (stage)
            {
                case Attack:
                    value += (1.25f - value) * attackCoef;   // aims past 1 so it arrives with a curve
                    if (value >= 1.0f) { value = 1.0f; stage = Decay; }
                    break;
                case Decay:
                    value += (sus - value) * decayCoef;
                    if (value <= sus + 0.0005f) stage = Sustain;
                    break;
                case Sustain:
                    value = sus;
                    break;
                case Release:
                    value += (-0.02f - value) * releaseCoef;  // aims a little below zero so it actually gets there
                    if (value <= 0.0f) { value = 0.0f; stage = Idle; }
                    break;
                case Idle:
                    value = 0.0f;
                    break;
            }
            return value;
        }

    private:
        enum Stage { Idle, Attack, Decay, Sustain, Release };
        float coefFor (float seconds) const { return 1.0f - std::exp (-1.0f / (juce::jmax (0.0005f, seconds) * sr * 0.25f)); }

        Stage stage = Idle;
        float value = 0.0f, sus = 0.7f, sr = 44100.0f;
        float attackCoef = 0.01f, decayCoef = 0.001f, releaseCoef = 0.001f;
    };

    //==========================================================================
    // Four-pole transistor ladder (Huovilainen-style), tanh saturation in every stage,
    // resonance that self-oscillates near the top, and a drive control into the first stage.
    struct Ladder
    {
        void prepare (double sampleRate) { sr = (float) sampleRate; reset(); }
        void reset() { for (auto& s : stage) s = 0.0f; for (auto& d : delay) d = 0.0f; }

        void set (float cutoffHz, float resonance, float driveAmount)
        {
            const float fc = juce::jlimit (20.0f, sr * 0.45f, cutoffHz) / sr;
            // Frequency warping so the cutoff lands where it should
            g = 0.9892f * fc * twoPi * 0.5f;
            g = juce::jmin (1.0f, g * (1.0f - 0.3f * g));
            res = juce::jlimit (0.0f, 1.1f, resonance) * 4.0f;
            drive = 1.0f + driveAmount * 4.0f;
            comp = 1.0f + juce::jmin (1.0f, resonance) * 0.5f;    // make up for resonance thinning the bass
        }

        // mode: 0 low pass 24 dB, 1 high pass (input minus low pass)
        float process (float in, float mode = 0.0f)
        {
            const float x = fastTanh (in * drive);
            // Two passes per sample for stability at high resonance (light oversampling)
            float out = 0.0f;
            for (int pass = 0; pass < 2; ++pass)
            {
                float u = x - res * delay[3];
                u = fastTanh (u);
                stage[0] += g * (u - stage[0]);           delay[0] = stage[0];
                stage[1] += g * (fastTanh (stage[0]) - stage[1]); delay[1] = stage[1];
                stage[2] += g * (fastTanh (stage[1]) - stage[2]); delay[2] = stage[2];
                stage[3] += g * (fastTanh (stage[2]) - stage[3]); delay[3] = stage[3];
                out = stage[3];
            }
            const float lp = out * comp;
            const float hp = x - lp;
            return lp + (hp - lp) * mode;
        }

    private:
        float sr = 44100.0f, g = 0.1f, res = 0.0f, drive = 1.0f, comp = 1.0f;
        float stage[4] {}, delay[4] {};
    };

    //==========================================================================
    // One-pole filters for tone controls and smoothing
    struct OnePole
    {
        void prepare (double sampleRate) { sr = (float) sampleRate; }
        void setLowpass (float hz)  { a = 1.0f - std::exp (-twoPi * juce::jlimit (5.0f, sr * 0.45f, hz) / sr); }
        float lowpass (float x)     { z += a * (x - z); return z; }
        float highpass (float x)    { return x - lowpass (x); }
        float sr = 44100.0f, a = 0.1f, z = 0.0f;
    };

    // Biquad for the EQ shelves and peak
    struct Biquad
    {
        void setShelf (double sampleRate, float hz, float gainDb, bool high)
        {
            const float A = std::pow (10.0f, gainDb / 40.0f);
            const float w0 = twoPi * juce::jlimit (20.0f, (float) sampleRate * 0.45f, hz) / (float) sampleRate;
            const float cw = std::cos (w0), sw = std::sin (w0);
            const float alpha = sw / 2.0f * std::sqrt ((A + 1.0f / A) * (1.0f / 0.9f - 1.0f) + 2.0f);
            const float sqA2a = 2.0f * std::sqrt (A) * alpha;
            float b0, b1, b2, a0, a1, a2;
            if (! high)
            {
                b0 =      A * ((A + 1) - (A - 1) * cw + sqA2a);  b1 =  2 * A * ((A - 1) - (A + 1) * cw);  b2 = A * ((A + 1) - (A - 1) * cw - sqA2a);
                a0 =           (A + 1) + (A - 1) * cw + sqA2a;   a1 = -2 *     ((A - 1) + (A + 1) * cw);  a2 =     (A + 1) + (A - 1) * cw - sqA2a;
            }
            else
            {
                b0 =      A * ((A + 1) + (A - 1) * cw + sqA2a);  b1 = -2 * A * ((A - 1) + (A + 1) * cw);  b2 = A * ((A + 1) + (A - 1) * cw - sqA2a);
                a0 =           (A + 1) - (A - 1) * cw + sqA2a;   a1 =  2 *     ((A - 1) - (A + 1) * cw);  a2 =     (A + 1) - (A - 1) * cw - sqA2a;
            }
            c0 = b0 / a0; c1 = b1 / a0; c2 = b2 / a0; d1 = a1 / a0; d2 = a2 / a0;
        }

        void setPeak (double sampleRate, float hz, float gainDb, float q)
        {
            const float A = std::pow (10.0f, gainDb / 40.0f);
            const float w0 = twoPi * juce::jlimit (20.0f, (float) sampleRate * 0.45f, hz) / (float) sampleRate;
            const float alpha = std::sin (w0) / (2.0f * q);
            const float cw = std::cos (w0);
            const float a0 = 1 + alpha / A;
            c0 = (1 + alpha * A) / a0; c1 = -2 * cw / a0; c2 = (1 - alpha * A) / a0;
            d1 = -2 * cw / a0; d2 = (1 - alpha / A) / a0;
        }

        float process (float x)
        {
            const float y = c0 * x + z1;
            z1 = c1 * x - d1 * y + z2;
            z2 = c2 * x - d2 * y;
            return y;
        }

        float c0 = 1, c1 = 0, c2 = 0, d1 = 0, d2 = 0, z1 = 0, z2 = 0;
    };

    //==========================================================================
    // Saturation curves for the Drive module and the mixer stages
    namespace sat
    {
        inline float tube (float x)   { return fastTanh (x) + 0.08f * x * x * (x > 0 ? 1.0f : -0.6f) / (1.0f + x * x); } // slightly asymmetric
        inline float tape (float x)   { return x / std::sqrt (1.0f + x * x); }
        inline float fuzz (float x)   { const float y = fastTanh (x * 3.0f); return y - 0.3f * y * y * y; }
        inline float fold (float x)   { x = std::fmod (x + 1.0f, 4.0f); if (x < 0) x += 4.0f; return x < 2.0f ? x - 1.0f : 3.0f - x; }
    }

    // DC blocker after asymmetric stages
    struct DCBlock
    {
        float process (float x) { const float y = x - xm1 + 0.995f * ym1; xm1 = x; ym1 = y; return y; }
        float xm1 = 0.0f, ym1 = 0.0f;
    };
}
