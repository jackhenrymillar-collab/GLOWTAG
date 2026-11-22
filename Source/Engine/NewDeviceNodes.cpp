#include "Graph.h"
#include "DeviceParams.h"
#include "Samples.h"

// The workshop devices: Gas Tube, Crate Digger and String Wall (instruments), Ghost Tag, Crowd and
// Transformer (effects). Like the other built-in devices they read card knobs through k() and the
// expanded view's controls through raw() / choice() / on().
//
// The methods, briefly:
//  - Gas Tube: band-limited (mipmapped) wavetables built from each gas's line spectrum, with the
//    discharge's instability as low-passed noise on pitch and level, Poisson crackle, a striking
//    flash, mains hum as amplitude modulation, and a short comb for the glass body.
//  - Crate Digger: 4-point Hermite resampling of the record, wow at the record's own 33 1/3 rpm plus
//    flutter as a modulated fractional delay, a Butterworth band per pressing era, groove saturation,
//    and dust as Poisson clicks while the record turns.
//  - String Wall: digital waveguides. Plucks are a loop with a fractional (Hermite) delay, a loss
//    filter and allpass dispersion for stiff strings; the bow is the McIntyre-Schumacher-Woodhouse
//    friction model in Smith's two-delay-line form; strings are coupled through a shared bridge, so
//    the others ring in sympathy at the partials they share.
//  - Ghost Tag: a feedback delay whose loop loses a little more each pass (saturation, a darkening
//    low pass, Schroeder allpass diffusion), with drift on the read head and a hold that loops a
//    captured stretch of the echoes, forwards or backwards.
//  - Crowd: twelve voices on modulated fractional delays (sine plus smoothed random), each placed with
//    interaural time and level differences, head shadow on the far ear, and air absorption with distance.
//  - Transformer: a chain of failing parts, with antiderivative anti-aliasing on the diode's clipping.

using namespace analog;
using Param::expMap;

namespace
{
    float midiHz (float note) { return 440.0f * std::pow (2.0f, (note - 69.0f) / 12.0f); }

    struct Rng
    {
        juce::uint32 state = 22222;
        float uni()    { state = state * 1664525u + 1013904223u; return (float) (state >> 8) * (1.0f / 16777216.0f); }   // 0..1
        float bi()     { return uni() * 2.0f - 1.0f; }
    };

    // 4-point Hermite interpolation between y1 and y2
    inline float hermite (float y0, float y1, float y2, float y3, float t)
    {
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    }

    // A power-of-two delay line read with Hermite interpolation
    struct DelayLine
    {
        std::vector<float> b;
        int mask = 0, w = 0;
        void prepare (int minSize)
        {
            int size = 16;
            while (size < minSize + 4) size <<= 1;
            b.assign ((size_t) size, 0.0f);
            mask = size - 1;
            w = 0;
        }
        void clear() { std::fill (b.begin(), b.end(), 0.0f); }
        void push (float x) { b[(size_t) w] = x; w = (w + 1) & mask; }
        // `delay` samples back from the newest sample (at least 1)
        float read (float delay) const
        {
            delay = juce::jlimit (1.0f, (float) (mask - 3), delay);
            const float pos = (float) w - 1.0f - delay;
            const float fl = std::floor (pos);
            const int i = (int) fl;
            const float t = pos - fl;
            return hermite (b[(size_t) ((i - 1) & mask)], b[(size_t) (i & mask)], b[(size_t) ((i + 1) & mask)], b[(size_t) ((i + 2) & mask)], t);
        }
        float tap (int delay) const { return b[(size_t) ((w - 1 - delay) & mask)]; }
    };

    // First-order antiderivative anti-aliasing for tanh: the average of tanh over the step, so a hard
    // drive does not fold its harmonics back down into the audio band
    struct TanhAA
    {
        float x1 = 0.0f;
        static float logCosh (float x) { const float a = std::abs (x); return a + std::log1p (std::exp (-2.0f * a)) - 0.693147f; }
        float process (float x)
        {
            const float d = x - x1;
            const float y = std::abs (d) > 1e-4f ? (logCosh (x) - logCosh (x1)) / d : std::tanh (0.5f * (x + x1));
            x1 = x;
            return y;
        }
    };

    // The leaky diode's curve, f(x) = tanh (g x) / (g n) above zero and b x below, with the same
    // antiderivative anti-aliasing (F is continuous at zero, so the two sides join without a click)
    struct DiodeAA
    {
        float x1 = 0.0f;
        static float F (float x, float g, float n, float b) { return x > 0.0f ? TanhAA::logCosh (g * x) / (g * n) : 0.5f * b * x * x; }
        static float f (float x, float g, float n, float b) { return x > 0.0f ? std::tanh (g * x) / n : b * x; }
        float process (float x, float g, float n, float b)
        {
            const float d = x - x1;
            const float y = std::abs (d) > 1e-4f ? (F (x, g, n, b) - F (x1, g, n, b)) / d : f (0.5f * (x + x1), g, n, b);
            x1 = x;
            return y;
        }
    };

    struct OnePoleLP
    {
        float a = 1.0f, z = 0.0f;
        void set (double sr, float hz) { a = 1.0f - std::exp (-twoPi * juce::jlimit (5.0f, (float) sr * 0.45f, hz) / (float) sr); }
        float process (float x) { z += a * (x - z); return z; }
    };

    float coefFor (double sr, float seconds) { return 1.0f - std::exp (-1.0f / (juce::jmax (1e-4f, seconds) * (float) sr)); }

    //==========================================================================
    // Gas Tube: six glass tubes, one per voice. They strike, buzz and glow.
    struct GasTubeNode : public Node
    {
        static constexpr int tableSize = 2048, levels = 7;          // harmonics per level: 64, 32 ... 1
        std::vector<float> tables;                                    // [gas][level][tableSize + 1]

        struct Voice
        {
            Envelope env;
            SVF lp, breath;
            DelayLine comb;
            double ph1 = 0.0, ph2 = 0.0;
            float jit = 0.0f, warmPitch = 1.0f, warmCut = 1.0f;
            float crack = 0.0f, crackHp = 0.0f, crackPrev = 0.0f;
            float flash = 0.0f, vel = 0.0f, level = 0.0f;
            int pitch = -1;
            double age = 0.0, released = -1.0, gate = -1.0;
            bool active = false;
            Rng rng;
        };
        std::array<Voice, Param::GasTube::tubes> voices;
        int nextTube = 0;
        double humPhase = 0.0, buzzPhase = 0.0;
        OnePoleLP buzzLp;
        TanhAA clipL, clipR;

        GasTubeNode (const Module& m, double sampleRate) : Node (m, sampleRate)
        {
            buildTables();
            int seed = 11;
            for (auto& v : voices)
            {
                v.env.prepare (sr); v.lp.prepare (sr); v.breath.prepare (sr);
                v.comb.prepare ((int) (sr * 0.008));
                v.rng.state = (juce::uint32) (seed++ * 104729);
            }
            buzzLp.set (sr, 700.0f);
        }

        bool isInstrument() const override { return true; }

        void buildTables()
        {
            using namespace Param::GasTube;
            tables.assign ((size_t) (numGases * levels * (tableSize + 1)), 0.0f);
            for (int gas = 0; gas < numGases; ++gas)
            {
                float norm = 1.0f;
                for (int lv = 0; lv < levels; ++lv)
                {
                    float* t = &tables[(size_t) ((gas * levels + lv) * (tableSize + 1))];
                    const int maxH = 64 >> lv;
                    for (int n = 1; n <= maxH; ++n)
                    {
                        const float a = harmonic (gas, n);
                        if (std::abs (a) < 1e-9f) continue;
                        for (int i = 0; i < tableSize; ++i)
                            t[i] += a * std::sin (twoPi * (float) n * (float) i / (float) tableSize);
                    }
                    if (lv == 0)
                    {
                        float pk = 0.0f;
                        for (int i = 0; i < tableSize; ++i) pk = juce::jmax (pk, std::abs (t[i]));
                        norm = pk > 0.0f ? 1.0f / pk : 1.0f;
                    }
                    for (int i = 0; i < tableSize; ++i) t[i] *= norm;
                    t[tableSize] = t[0];
                }
            }
        }

        const float* tableFor (int gas, float hz) const
        {
            // The fullest table whose top harmonic stays under the Nyquist limit
            int lv = 0;
            while (lv < levels - 1 && (float) (64 >> lv) * hz > (float) sr * 0.45f) ++lv;
            return &tables[(size_t) ((juce::jlimit (0, Param::GasTube::numGases - 1, gas) * levels + lv) * (tableSize + 1))];
        }

        static float lookup (const float* t, double phase)
        {
            const float p = (float) (phase * tableSize);
            const int i = (int) p;
            const float f = p - (float) i;
            return t[i] + f * (t[i + 1] - t[i]);
        }

        void noteOn (int pitch, float velocity, double gateSamples) override
        {
            using namespace Param::GasTube;
            // The next free tube; if all six are lit the oldest goes out
            Voice* target = nullptr;
            for (int k = 0; k < tubes && target == nullptr; ++k)
                if (! voices[(size_t) ((nextTube + k) % tubes)].active) { target = &voices[(size_t) ((nextTube + k) % tubes)]; nextTube = (nextTube + k + 1) % tubes; }
            if (target == nullptr)
            {
                target = &voices[0];
                for (auto& v : voices) if (v.age > target->age) target = &v;
            }
            auto& v = *target;
            const float warm = raw (WarmUp);
            v.active = true;
            v.pitch = pitch;
            v.vel = velocity;
            v.gate = gateSamples;
            v.age = 0.0;
            v.released = -1.0;
            v.ph1 = v.rng.uni();
            v.ph2 = v.rng.uni();
            v.warmPitch = std::pow (2.0f, -warm * 0.45f);   // cold gas: the tube comes up flat and dull
            v.warmCut = 1.0f - warm * 0.75f;
            v.flash = 1.0f;
            v.lp.reset();
            v.comb.clear();
            v.env.noteOn();
        }

        void noteOff (int pitch) override
        {
            for (auto& v : voices)
                if (v.active && v.pitch == pitch && v.gate < 0.0 && v.released < 0.0)
                {
                    v.env.noteOff();
                    v.released = v.age;
                }
        }

        void allNotesOff() override { for (auto& v : voices) if (v.active) { v.env.noteOff(); if (v.released < 0.0) v.released = v.age; v.gate = -1.0; } }

        void process (float* l, float* r, int n, double) override
        {
            using namespace Param::GasTube;
            const float volt = k (Voltage), gain = k (Gain);
            const float a = expMap (k (Strike), 0.001f, 2.0f), d = expMap (k (Body), 0.02f, 6.0f), s = k (Glow), rel = expMap (k (Fade), 0.01f, 6.0f);
            const float press = raw (Pressure), hum = raw (Hum), crackle = raw (Crackle), len = raw (Length), warm = raw (WarmUp);
            const int gas = juce::jlimit (0, numGases - 1, choice (Gas));
            const float mains = choice (Mains) == 1 ? 60.0f : 50.0f;
            const float breathAmt = breath (gas) * (1.0f + volt * 2.0f);

            // The discharge's wander: noise low-passed at 5..43 Hz, scaled to keep its spread steady
            const float jitHz = 5.0f + volt * 38.0f;
            const float jitA = coefFor (sr, 1.0f / (twoPi * jitHz));
            const float jitNorm = 1.0f / (0.577f * std::sqrt (2.0f * jitHz / (float) sr));
            const float jitCents = volt * volt * 25.0f, jitFlick = volt * volt * 0.5f;
            const float detune2 = std::pow (2.0f, (3.0f + volt * 9.0f) / 1200.0f);
            const float warmA = coefFor (sr, 0.015f + warm * 0.3f), warmCutA = coefFor (sr, 0.03f + warm * 0.5f);
            const float crackGain = crackle * (0.25f + volt * 0.9f) * 0.7f;
            const float crackProb = crackle > 0.001f ? 0.0009f * 44100.0f / (float) sr * (0.4f + volt) : 0.0f;
            const float combDelay = (0.0005f + len * 0.0055f) * (float) sr;

            // Mains: the tube brightens and dims at twice the mains, and the transformer buzzes
            float humBuf[subBlock], buzzBuf[subBlock];
            for (int i = 0; i < n; ++i)
            {
                humPhase += 2.0 * mains / sr; humPhase -= std::floor (humPhase);
                buzzPhase += mains / sr; buzzPhase -= std::floor (buzzPhase);
                humBuf[i] = (1.0f - hum * 0.5f) + hum * 0.5f * std::sin (twoPi * (float) humPhase);
                buzzBuf[i] = buzzLp.process ((float) buzzPhase * 2.0f - 1.0f) * hum * 0.035f;
            }

            int playing = 0;
            for (size_t vi = 0; vi < voices.size(); ++vi)
            {
                auto& v = voices[vi];
                auto& view = views[vi];
                if (! v.active) { view.pitch = -1; view.level = 0.0f; continue; }
                ++playing;
                v.env.set (a, d, s, rel);
                const float hz = midiHz ((float) v.pitch);
                const float* tab = tableFor (gas, hz * 1.01f);
                const float base = expMap (press, 60.0f, 16000.0f) * std::pow (2.0f, (v.vel - 0.5f) * 1.2f);
                v.breath.set (juce::jmin (8000.0f, hz * 3.0f), 1.2f);
                const float velGain = 0.55f + 0.45f * v.vel;
                const float th = (((float) vi / 5.0f - 0.5f) * 0.5f + 1.0f) * juce::MathConstants<float>::pi * 0.25f;   // tubes spread a little, left to right
                const float pl = std::cos (th) * 1.41f, pr = std::sin (th) * 1.41f;
                float lvl = 0.0f;

                for (int i = 0; i < n; ++i)
                {
                    if (v.gate >= 0.0 && v.gate-- <= 0.0) { v.env.noteOff(); if (v.released < 0.0) v.released = v.age; v.gate = -2.0; }
                    const float env = v.env.process();
                    if (! v.env.isActive()) { v.active = false; break; }

                    v.jit += (v.rng.bi() - v.jit) * jitA;
                    const float j = v.jit * jitNorm;
                    v.warmPitch += (1.0f - v.warmPitch) * warmA;
                    v.warmCut += (1.0f - v.warmCut) * warmCutA;
                    const float f = hz * v.warmPitch * std::pow (2.0f, j * jitCents / 1200.0f);
                    v.ph1 += f / sr;             v.ph1 -= std::floor (v.ph1);
                    v.ph2 += f * detune2 / sr;   v.ph2 -= std::floor (v.ph2);
                    float x = (lookup (tab, v.ph1) + lookup (tab, v.ph2)) * 0.2f;

                    const float nz = v.rng.bi();
                    if (breathAmt > 0.0f) { v.breath.process (nz); x += v.breath.band * breathAmt; }

                    // Striking: the gas ionises with a short bright flash
                    if (v.flash > 0.001f) { x += nz * v.flash * 0.12f * (0.3f + volt); v.flash *= 0.995f; }

                    if (i == 0) v.lp.set (juce::jmin (base * v.warmCut, (float) sr * 0.45f), 0.9f);
                    v.lp.process (x);
                    const float lpOut = v.lp.low;
                    const float back = v.comb.read (combDelay);
                    v.comb.push (lpOut + back * 0.42f);
                    float y = (lpOut + back * 0.45f) * humBuf[i];

                    // Crackle: little discharges, a few dozen a second, high-passed
                    if (crackProb > 0.0f && v.rng.uni() < crackProb) v.crack = (v.rng.uni() * 0.9f + 0.1f) * (v.rng.uni() < 0.5f ? -1.0f : 1.0f);
                    const float c = v.crack;
                    v.crack *= 0.867f;                                  // e^(-1/7) per sample
                    v.crackHp = 0.78f * (v.crackHp + c - v.crackPrev);
                    v.crackPrev = c;
                    y += v.crackHp * crackGain + buzzBuf[i];

                    const float flick = juce::jmax (0.0f, 1.0f + j * jitFlick);
                    const float out = y * flick * env * velGain * 0.9f;
                    l[i] += out * pl;
                    r[i] += out * pr;
                    lvl = juce::jmax (lvl, env * flick);
                    v.age += 1.0;
                }
                v.level = lvl;
                view.pitch = v.active ? v.pitch : -1;
                view.velocity = v.vel;
                view.level = lvl;
                view.age = (float) (v.age / sr);
                view.released = v.released < 0.0 ? -1.0f : (float) (v.released / sr);
            }
            const float g = gain * 1.1f;
            for (int i = 0; i < n; ++i)
            {
                l[i] = clipL.process (l[i] * 1.2f) * g;
                r[i] = clipR.process (r[i] * 1.2f) * g;
            }
            display = (float) playing;
            meter[0] = volt;
        }
    };

    //==========================================================================
    // Crate Digger: sixteen slices dug out of a record, each with its own pitch and direction,
    // played through a worn turntable.
    struct CrateNode : public Node
    {
        std::shared_ptr<const AudioData> sample;
        struct Voice
        {
            double pos = 0.0, rate = 1.0;          // in file frames, per output sample
            double left = 0.0, total = 0.0;        // output samples to go, and the whole run
            float vel = 0.0f;
            int slice = -1, pitch = -1, dir = 1;
            double age = 0.0;
            bool active = false;
        };
        std::array<Voice, 8> voices;
        DelayLine wowL, wowR;
        Biquad hpL, hpR, lpL, lpR;
        float lastLow = -1.0f, lastHigh = -1.0f;
        double rev = 0.0, flut = 0.0;
        float wander = 0.0f, spin = 0.0f;
        float dust = 0.0f, dustHp = 0.0f, dustPrev = 0.0f, hiss = 0.0f, thump = 0.0f;
        float dustPan = 0.5f;
        Rng rng;
        OnePoleLP thumpLp;

        CrateNode (const Module& m, double sampleRate, const Graph::SampleResolver& resolve) : Node (m, sampleRate)
        {
            sample = resolve (m.sampleName.isNotEmpty() ? m.sampleName : juce::String ("Found Record"));
            wowL.prepare ((int) (sr * 0.05));
            wowR.prepare ((int) (sr * 0.05));
            thumpLp.set (sr, 90.0f);
            rng.state = 777;
        }

        bool isInstrument() const override { return true; }

        void noteOn (int pitch, float velocity, double) override
        {
            using namespace Param::Crate;
            if (sample == nullptr || sample->buffer.getNumSamples() < 64) return;
            const int slice = ((pitch - firstNote) % slices + slices) % slices;
            const double len = sample->buffer.getNumSamples();
            const double ratio = sample->sampleRate / sr;
            const double bpm = host != nullptr && host->bpm > 1.0 ? host->bpm : 120.0;
            const double span = juce::jmin (len * 0.98, Param::Crate::chopBeats[chopIndex (k (Chop))] * 60.0 / bpm * sample->sampleRate);
            const int semis = shiftOf (raw (Shift0 + slice)) + juce::roundToInt ((k (Pitch) - 0.5f) * 24.0f);
            const double rate = std::pow (2.0, semis / 12.0);

            Voice* target = nullptr;
            for (auto& v : voices) if (! v.active) { target = &v; break; }
            if (target == nullptr) { target = &voices[0]; for (auto& v : voices) if (v.age > target->age) target = &v; }
            auto& v = *target;
            v.active = true;
            v.slice = slice;
            v.pitch = pitch;
            v.vel = velocity;
            v.dir = on (Rev0 + slice) ? -1 : 1;
            v.rate = rate * ratio;
            const double start = juce::jlimit (0.0f, 0.999f, raw (Pos0 + slice)) * len;
            v.pos = v.dir > 0 ? start : start + span;
            v.total = v.left = span / v.rate;
            v.age = 0.0;
        }

        float frame (int ch, double pos) const
        {
            const int len = sample->buffer.getNumSamples();
            const auto* d = sample->buffer.getReadPointer (juce::jmin (ch, sample->buffer.getNumChannels() - 1));
            const double fl = std::floor (pos);
            const int i = (int) fl;
            const float t = (float) (pos - fl);
            auto at = [d, len] (int j) { j %= len; if (j < 0) j += len; return d[j]; };   // the record goes round
            return hermite (at (i - 1), at (i), at (i + 1), at (i + 2), t);
        }

        void process (float* l, float* r, int n, double) override
        {
            using namespace Param::Crate;
            const float wear = k (Wear), dustAmt = k (Dust), wow = k (Wow), gain = k (Gain);
            const int era = juce::jlimit (0, 3, choice (Era));
            const float low = eraLow[era], high = juce::jmin (eraHigh[era], expMap (1.0f - wear * 0.85f, 1200.0f, 18000.0f));
            if (std::abs (low - lastLow) > 0.5f || std::abs (high - lastHigh) > 1.0f)
            {
                hpL.setPass (sr, low, 0.707f, true);  hpR = hpL;
                lpL.setPass (sr, high, 0.707f, false); lpR = lpL;
                lastLow = low; lastHigh = high;
            }
            const float attack = 0.004f * (float) sr, fade = 0.02f * (float) sr;
            const float drive = 1.0f + wear * 1.2f, driveNorm = 1.0f / std::tanh (drive);
            const float width = 1.0f - wear * 0.4f;

            // Wow is the record's own turn (33 1/3 rpm, slightly off centre), flutter the motor, plus a
            // slow random wander. Depths in seconds of delay for a pitch swing in cents.
            const float wowHz = 0.5556f, flutHz = 6.3f;
            const float wowDepth = (wow * 26.0f / 1731.0f) / (twoPi * wowHz), flutDepth = (wow * 7.0f / 1731.0f) / (twoPi * flutHz);
            const float baseDelay = 0.012f;
            const float dustProb = dustAmt * dustAmt * 0.0016f * 44100.0f / (float) sr;

            int playing = 0;
            for (size_t vi = 0; vi < voices.size(); ++vi)
            {
                auto& v = voices[vi];
                auto& view = views[vi];
                if (! v.active || sample == nullptr) { view.pitch = -1; view.slice = -1; continue; }
                ++playing;
                const float amp = 0.9f * (0.5f + 0.5f * v.vel);
                for (int i = 0; i < n; ++i)
                {
                    if (v.left <= 0.0) { v.active = false; break; }
                    const double done = v.total - v.left;
                    const float env = juce::jmin (1.0f, (float) done / attack, (float) v.left / fade) * amp;
                    l[i] += frame (0, v.pos) * env;
                    r[i] += frame (1, v.pos) * env;
                    v.pos += v.rate * v.dir;
                    v.left -= 1.0;
                    v.age += 1.0;
                }
                const double len = sample->buffer.getNumSamples();
                double p = std::fmod (v.pos, len); if (p < 0) p += len;
                view.pitch = v.active ? v.pitch : -1;
                view.slice = v.active ? v.slice : -1;
                view.pos = (float) (p / len);
                view.velocity = v.vel;
                view.age = (float) (v.age / sr);
                view.level = v.active ? (float) (v.left / juce::jmax (1.0, v.total)) : 0.0f;
            }

            // The turntable spins up while anything plays and coasts down after
            spin += ((playing > 0 ? 1.0f : 0.0f) - spin) * (playing > 0 ? 0.2f : 0.02f);

            for (int i = 0; i < n; ++i)
            {
                rev += wowHz / sr; rev -= std::floor (rev);
                flut += flutHz / sr; flut -= std::floor (flut);
                wander += (rng.bi() - wander) * 0.0004f;
                const float wobble = baseDelay + wowDepth * std::sin (twoPi * (float) rev) + flutDepth * std::sin (twoPi * (float) flut)
                                + wowDepth * 0.4f * wander * 8.0f;
                wowL.push (l[i]); wowR.push (r[i]);
                const float ds = juce::jmax (0.001f, wobble) * (float) sr;
                float xl = wowL.read (ds), xr = wowR.read (ds * 1.0002f);

                xl = lpL.process (hpL.process (xl));
                xr = lpR.process (hpR.process (xr));
                xl = std::tanh (xl * drive) * driveNorm;
                xr = std::tanh (xr * drive) * driveNorm;
                const float mid = (xl + xr) * 0.5f, side = (xl - xr) * 0.5f * width;
                xl = mid + side; xr = mid - side;

                // Dust: clicks and a soft hiss, only while the needle is in the groove
                if (spin > 0.02f)
                {
                    if (rng.uni() < dustProb * spin)
                    {
                        dust = (rng.uni() * 0.9f + 0.1f) * (rng.uni() < 0.5f ? -1.0f : 1.0f);
                        dustPan = rng.uni();
                        if (rng.uni() < 0.06f) thump = dust * 2.0f;
                    }
                    const float c = dust;
                    dust *= 0.867f;
                    dustHp = 0.88f * (dustHp + c - dustPrev);
                    dustPrev = c;
                    hiss += (rng.bi() - hiss) * 0.5f;
                    const float th = thumpLp.process (thump);
                    thump *= 0.995f;
                    const float dz = (dustHp + hiss * 0.012f + th * 0.6f) * dustAmt * 0.4f * spin;
                    xl += dz * (1.2f - dustPan * 0.4f);
                    xr += dz * (0.8f + dustPan * 0.4f);
                }
                l[i] = xl * gain * 1.2f;
                r[i] = xr * gain * 1.2f;
            }
            display = (float) playing;
            meter[0] = spin;
        }
    };

    //==========================================================================
    // String Wall: eight strings across the brick. Pluck, bow or let them feed back; a slide bar and
    // two pedals bend them; the bridge lets each string ring in sympathy with the others.
    struct StringsNode : public Node
    {
        struct Str
        {
            DelayLine loop, neck, bridge;
            float lpPrev = 0.0f, ap1x = 0.0f, ap1y = 0.0f, ap2x = 0.0f, ap2y = 0.0f;
            float bowFilt = 0.0f, bowEnv = 0.0f;
            std::vector<float> excite;
            int exciteAt = 0, exciteLen = 0;
            float note = 60.0f, semis = 0.0f;      // the note it plays, and the glide of bar and pedals on top
            float vel = 0.0f, energy = 0.0f, dc = 0.0f;
            int mode = 0;
            double age = 0.0, released = -1.0, gate = -1.0;
            bool active = false, held = false;
            float out = 0.0f;
        };
        std::array<Str, Param::Strings::count> strs;
        float pedA = 0.0f, pedB = 0.0f;
        Biquad bodyL1, bodyL2, bodyR1, bodyR2;
        TanhAA outClipL, outClipR;    // the pickup's preamp: soft, alias-free headroom above 1
        float lastBody = -1.0f;
        Rng rng;

        StringsNode (const Module& m, double sampleRate) : Node (m, sampleRate)
        {
            const int maxLen = (int) (sr / 25.0) + 8;           // down to 25 Hz
            for (auto& s : strs)
            {
                s.loop.prepare (maxLen);
                s.neck.prepare (maxLen);
                s.bridge.prepare (maxLen);
                s.excite.assign ((size_t) maxLen, 0.0f);
            }
            rng.state = 4242;
        }

        bool isInstrument() const override { return true; }

        int tuning() const { return juce::jlimit (0, Param::Strings::numTunings - 1, choice (Param::Strings::Tuning)); }

        void noteOn (int pitch, float velocity, double gateSamples) override
        {
            using namespace Param::Strings;
            const int si = stringFor (tuning(), pitch);
            auto& s = strs[(size_t) si];
            const int mode = juce::jlimit (0, 2, choice (Mode));
            const int mat = juce::jlimit (0, 3, choice (Material));
            const float br = juce::jlimit (0.0f, 1.0f, k (Tone) * matTone[mat]);
            s.active = true;
            s.held = true;
            s.mode = mode;
            s.note = (float) pitch;
            s.vel = velocity;
            s.gate = gateSamples;
            s.age = 0.0;
            s.released = -1.0;
            s.bowEnv = 0.0f;
            s.dc = 0.0f;
            if (mode == Bow)
            {
                s.neck.clear(); s.bridge.clear();
                s.exciteLen = 0;
                return;
            }
            // The pick: a burst one period long, low-passed by the tone and combed by where it is picked
            const float hz = midiHz (s.note + s.semis);
            const int D = juce::jlimit (4, (int) s.excite.size() - 1, (int) std::round (sr / hz));
            const float bk = 0.2f + 0.8f * br;
            float lp = 0.0f, peak = 0.0f;
            for (int i = 0; i < D; ++i) { lp += (rng.bi() - lp) * bk; s.excite[(size_t) i] = lp; }
            const int pk = juce::jmax (1, (int) std::round ((float) D * (0.06f + k (Pick) * 0.42f)));
            float tmp[2048];
            const int Dc = juce::jmin (D, 2048);
            for (int i = 0; i < Dc; ++i) tmp[i] = s.excite[(size_t) i];
            for (int i = 0; i < Dc; ++i) { s.excite[(size_t) i] = tmp[i] - tmp[(i - pk + Dc) % Dc] * 0.9f; peak = juce::jmax (peak, std::abs (s.excite[(size_t) i])); }
            const float amp = (0.3f + 0.6f * velocity) * 0.55f / juce::jmax (1e-4f, peak);
            for (int i = 0; i < Dc; ++i) s.excite[(size_t) i] *= amp;
            s.exciteLen = Dc;
            s.exciteAt = 0;
        }

        void noteOff (int pitch) override
        {
            for (auto& s : strs)
                if (s.active && s.held && juce::roundToInt (s.note) == pitch && s.gate < 0.0)
                {
                    s.held = false;
                    s.released = s.age;
                }
        }

        void allNotesOff() override { for (auto& s : strs) if (s.held) { s.held = false; s.released = s.age; s.gate = -1.0; } }

        static float bowTable (float x, float slope)
        {
            const float v = std::pow (std::abs (x * slope) + 0.75f, -4.0f);
            return juce::jmin (1.0f, v);
        }

        void process (float* l, float* r, int n, double) override
        {
            using namespace Param::Strings;
            const int mat = juce::jlimit (0, 3, choice (Material));
            const float br = juce::jlimit (0.0f, 1.0f, k (Tone) * matTone[mat]);
            const float sus = k (Sustain), sym = k (Sympathy), gain = k (Gain);
            const float bowP = raw (BowPressure);
            const bool barOn = on (BarOn);
            const float bar = barOn ? k (Bar) * 12.0f : 0.0f;
            const float stiff = matStiffness[mat];
            const float sLoss = 0.5f + 0.48f * br;                     // the loss filter: brighter is closer to 1
            const float pedalA_ = raw (PedalA) > 0.5f ? 1.0f : 0.0f, pedalB_ = raw (PedalB) > 0.5f ? 1.0f : 0.0f;
            const float pedCoef = coefFor (sr, 0.11f) * (float) n;
            pedA += (pedalA_ - pedA) * juce::jmin (1.0f, pedCoef);
            pedB += (pedalB_ - pedB) * juce::jmin (1.0f, pedCoef);
            const float glideA = juce::jmin (1.0f, coefFor (sr, 0.03f) * (float) n);
            const float couple = sym * 0.0022f;

            const float bodyHz = expMap (1.0f - k (Body), 95.0f, 420.0f);
            if (std::abs (bodyHz - lastBody) > 0.5f)
            {
                bodyL1.setPeak (sr, bodyHz, 7.0f, 1.4f);         bodyR1 = bodyL1;
                bodyL2.setPeak (sr, bodyHz * 2.7f, 4.0f, 2.0f);  bodyR2 = bodyL2;
                lastBody = bodyHz;
            }

            bool any = false;
            for (auto& s : strs) any = any || s.active;
            if (! any)
            {
                for (auto& v : views) v.pitch = -1;
                display = 0.0f;
                return;
            }

            // Per string, for this sub-block: delay length, loss and pan
            struct Setup { float L, g, ap, pan, gl, gr, beta; };
            std::array<Setup, count> set {};
            for (int si = 0; si < count; ++si)
            {
                auto& s = strs[(size_t) si];
                const float target = bar + pedalSemis (si, pedA, pedB);
                s.semis += (target - s.semis) * glideA;
                const float hz = midiHz (s.note + s.semis);
                const float L = (float) sr / hz;
                if (! s.active && s.energy < 1e-6f) s.note = (float) tunings[tuning()][si];   // at rest: back to its open note
                float T60 = s.mode == Pluck ? 0.3f + sus * 6.0f * matSustain[mat] : 2.0f + sus * 8.0f;
                if (! s.held && s.mode == Pluck) T60 = juce::jmin (T60, 0.25f);          // let go: damped
                if (! s.held && s.mode == Feedback) T60 = juce::jmin (T60, 0.5f);
                if (! s.active) T60 = 2.0f + sus * 8.0f;                              // a quiet string, free to ring in sympathy
                const float g = std::pow (0.001f, 1.0f / (T60 * hz)) * (1.0f - couple * 3.0f);
                // Each stage of the allpass adds (1 - a) / (1 + a) samples of delay at low frequencies
                const float a = -stiff;
                const float apDelay = stiff > 0.0f ? 2.0f * (1.0f - a) / (1.0f + a) : 0.0f;
                const float Ld = juce::jmax (2.0f, L - (1.0f - sLoss) - apDelay);
                const float pan = ((float) si / 7.0f - 0.5f) * 0.7f;
                const float th = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
                set[(size_t) si] = { Ld, g, a, pan, std::cos (th), std::sin (th), 0.027f + k (Pick) * 0.4f };
            }

            int sounding = 0;
            for (int i = 0; i < n; ++i)
            {
                float bridgeSum = 0.0f;
                for (auto& s : strs) bridgeSum += s.out;
                float sumL = 0.0f, sumR = 0.0f;

                for (int si = 0; si < count; ++si)
                {
                    auto& s = strs[(size_t) si];
                    const auto& st = set[(size_t) si];
                    const float sympIn = couple * (bridgeSum - s.out);
                    if (! s.active && std::abs (sympIn) < 1e-6f && s.energy < 1e-6f) { s.out = 0.0f; continue; }
                    if (s.gate >= 0.0 && s.gate-- <= 0.0) { s.held = false; if (s.released < 0.0) s.released = s.age; s.gate = -2.0; }

                    float y;
                    if (s.mode == Bow && s.active)
                    {
                        // Smith's bowed string: neck and bridge delay lines, with the bow's friction between them
                        const float attack = 0.08f + (1.0f - bowP) * 0.25f;
                        s.bowEnv += ((s.held ? 1.0f : 0.0f) - s.bowEnv) * coefFor (sr, s.held ? attack : 0.35f);
                        const float bowVel = (0.03f + 0.2f * s.vel) * s.bowEnv;
                        const float base = juce::jmax (4.0f, st.L + (1.0f - sLoss) - 4.0f);
                        const float neckOut = s.neck.read (base * (1.0f - st.beta));
                        const float bridgeOut = s.bridge.read (base * st.beta);
                        s.bowFilt = s.bowFilt + (0.25f + 0.6f * br) * (bridgeOut - s.bowFilt);
                        const float bridgeRefl = -s.bowFilt * 0.95f;
                        const float nutRefl = -neckOut;
                        const float stringVel = bridgeRefl + nutRefl;
                        const float dv = bowVel - stringVel;
                        const float newVel = dv * bowTable (dv, 5.0f - 4.0f * bowP);
                        s.neck.push (bridgeRefl + newVel + sympIn);
                        s.bridge.push (nutRefl + newVel);
                        y = bridgeOut * 2.2f;
                        if (! s.held && s.bowEnv < 0.0005f && std::abs (bridgeOut) < 1e-4f) { s.active = false; s.released = -1.0; }
                    }
                    else
                    {
                        // Plucked (or feeding back): one loop, its loss filter and dispersion
                        const float x = s.loop.read (st.L);
                        float v = sLoss * x + (1.0f - sLoss) * s.lpPrev;
                        s.lpPrev = x;
                        if (stiff > 0.0f)
                        {
                            const float y1 = st.ap * v + s.ap1x - st.ap * s.ap1y; s.ap1x = v; s.ap1y = y1;
                            const float y2 = st.ap * y1 + s.ap2x - st.ap * s.ap2y; s.ap2x = y1; s.ap2y = y2;
                            v = y2;
                        }
                        float nv = st.g * v + sympIn;
                        if (s.exciteAt < s.exciteLen) nv += s.excite[(size_t) s.exciteAt++];
                        float out = x;
                        if (s.mode == Feedback && s.active)
                        {
                            // The amp drives the string back: past a moment it sustains, then blooms to a harmonic
                            const float t = (float) (s.age / sr);
                            const float bloom = s.held ? juce::jlimit (0.0f, 1.0f, (t - 0.6f) / 2.4f) * 0.7f : 0.0f;
                            if (s.held) nv = 0.6f * std::tanh ((nv * (1.0f + 0.06f * juce::jlimit (0.0f, 1.0f, t * 2.0f)) + rng.bi() * 0.0004f) / 0.6f);
                            s.dc += (std::abs (x) - s.dc) * 0.001f;
                            out = x + bloom * (std::abs (x) - s.dc) * 1.8f;
                        }
                        s.loop.push (nv);
                        y = out;
                        // Done once it has died away (a pluck can be left held: it still dies away)
                        if (s.active && (! s.held || s.mode == Pluck) && s.energy < (s.held ? 1e-7f : 1e-5f) && s.age > sr * 0.05)
                        {
                            s.active = false; s.held = false; s.released = -1.0;
                        }
                    }
                    s.out = y;
                    s.energy += (y * y - s.energy) * 0.001f;
                    sumL += y * st.gl;
                    sumR += y * st.gr;
                    s.age += 1.0;
                }
                l[i] += 1.5f * outClipL.process ((bodyL2.process (bodyL1.process (sumL))) * gain * 0.6f);
                r[i] += 1.5f * outClipR.process ((bodyR2.process (bodyR1.process (sumR))) * gain * 0.6f);
            }

            for (int si = 0; si < count; ++si)
            {
                auto& s = strs[(size_t) si];
                auto& view = views[(size_t) si];
                const float level = juce::jlimit (0.0f, 1.0f, std::sqrt (s.energy) * 4.0f);
                if (s.active) ++sounding;
                view.pitch = (s.active || level > 0.02f) ? juce::roundToInt (s.note + s.semis) : -1;
                view.level = level;
                view.velocity = s.vel;
                view.age = (float) (s.age / sr);
                view.released = s.released < 0.0 ? -1.0f : (float) (s.released / sr);
                view.filter = s.semis;
            }
            display = (float) sounding;
            meter[0] = pedA;
            meter[1] = pedB;
        }
    };

    //==========================================================================
    // Ghost Tag: echoes that come back more washed out, blurred and drifting each time
    struct GhostNode : public Node
    {
        DelayLine lineL, lineR;
        std::vector<float> histL, histR, heldL, heldR;     // the echoes' last eight seconds, and what is held
        int histW = 0, heldLen = 0;
        double heldPos = 0.0;
        bool holding = false;
        int heldFrom = -1, heldTo = -1, heldHow = -1;
        float heldGain = 1.0f, holdFade = 0.0f;
        Biquad washL, washR;
        float lastWash = -1.0f;
        float hpL = 0.0f, hpR = 0.0f, hpXL = 0.0f, hpXR = 0.0f;
        struct Diffuser { DelayLine d; int len = 1; float g = 0.62f; float process (float x) { const float v = d.tap (len - 1); const float w = x + g * v; d.push (w); return v - g * w; } };
        std::array<Diffuser, 4> difL, difR;
        float smoothed = 0.0f;
        double lfo1 = 0.0, lfo2 = 0.0;
        float wander = 0.0f;
        Rng rng;
        TanhAA satL, satR;

        GhostNode (const Module& m, double sampleRate) : Node (m, sampleRate)
        {
            lineL.prepare ((int) (sr * 4.2));
            lineR.prepare ((int) (sr * 4.2));
            histL.assign ((size_t) (sr * 8.0), 0.0f);
            histR.assign ((size_t) (sr * 8.0), 0.0f);
            heldL.assign ((size_t) (sr * 8.0), 0.0f);
            heldR.assign ((size_t) (sr * 8.0), 0.0f);
            const float ms[4] = { 4.3f, 7.1f, 11.7f, 17.3f };
            for (int i = 0; i < 4; ++i)
            {
                difL[(size_t) i].d.prepare ((int) (sr * 0.03));
                difR[(size_t) i].d.prepare ((int) (sr * 0.03));
                difL[(size_t) i].len = juce::jmax (2, (int) (ms[i] * 0.001f * (float) sr));
                difR[(size_t) i].len = juce::jmax (2, (int) (ms[i] * 1.13f * 0.001f * (float) sr));
            }
            rng.state = 99;
        }

        void capture (float T)
        {
            using namespace Param::Ghost;
            const int from = juce::jlimit (1, echoes, choice (HoldFrom)), to = juce::jlimit (from, echoes, choice (HoldTo));
            const int size = (int) histL.size();
            heldLen = juce::jlimit (64, size - 1, (int) ((float) (to - from + 1) * T * (float) sr));
            for (int i = 0; i < heldLen; ++i)
            {
                const int j = ((histW - heldLen + i) % size + size) % size;
                heldL[(size_t) i] = histL[(size_t) j];
                heldR[(size_t) i] = histR[(size_t) j];
            }
            const bool reverse = choice (HoldHow) == Reverse;
            if (reverse) { std::reverse (heldL.begin(), heldL.begin() + heldLen); std::reverse (heldR.begin(), heldR.begin() + heldLen); }
            const int xf = juce::jmin (400, heldLen / 8);
            for (int i = 0; i < xf; ++i)
            {
                const float u = (float) i / (float) xf;
                heldL[(size_t) i] *= u; heldR[(size_t) i] *= u;
                heldL[(size_t) (heldLen - 1 - i)] *= u; heldR[(size_t) (heldLen - 1 - i)] *= u;
            }
            heldPos = 0.0;
            heldGain = juce::jmin (1.4f, std::pow (k (Fade), (float) juce::jmax (0, from - 1)) * 1.3f);
            heldFrom = from; heldTo = to; heldHow = choice (HoldHow);
        }

        void process (float* l, float* r, int n, double bpm) override
        {
            using namespace Param::Ghost;
            const float T = Param::divisionBeats[Param::divisionIndex (k (Time))] * 60.0f / (float) juce::jmax (20.0, bpm);
            const float target = juce::jmin ((float) lineL.mask - 8.0f, T * (float) sr);
            if (smoothed <= 0.0f) smoothed = target;
            const float fade = k (Fade), wash = k (Wash), mix = k (Mix), drift = raw (Drift), blur = raw (Blur);
            const bool hold = on (Hold);

            if (hold && (! holding || heldFrom != choice (HoldFrom) || heldTo != choice (HoldTo) || heldHow != choice (HoldHow)))
                capture (T);
            holding = hold;

            const float washHz = expMap (1.0f - wash, 600.0f, 16000.0f);
            if (std::abs (washHz - lastWash) > 1.0f) { washL.setPass (sr, washHz, 0.6f, false); washR = washL; lastWash = washHz; }
            const float fb = hold ? 0.0f : fade * 0.93f;
            const float driftDepth = drift * 0.0035f * juce::jmin (1.0f, T * 2.0f) * (float) sr;
            const float hpCoef = std::exp (-twoPi * 70.0f / (float) sr);
            const int histSize = (int) histL.size();

            for (int i = 0; i < n; ++i)
            {
                smoothed += (target - smoothed) * 0.0006f;
                lfo1 += 0.23 / sr; lfo2 += 0.37 / sr;
                wander += (rng.bi() - wander) * 0.0002f;
                const float dmod = driftDepth * (0.5f * std::sin (twoPi * (float) (lfo1 - std::floor (lfo1))) + 0.5f * std::sin (twoPi * (float) (lfo2 - std::floor (lfo2))) + wander * 3.0f);
                const float dL = juce::jmax (2.0f, smoothed + dmod), dR = juce::jmax (2.0f, smoothed - dmod * 0.7f);
                const float echoL = lineL.read (dL), echoR = lineR.read (dR);

                // What goes round again: darker, thinner at the bottom, and smeared by the blur
                float wl = washL.process (echoL), wr = washR.process (echoR);
                const float hl = hpCoef * (hpL + wl - hpXL); hpXL = wl; hpL = hl;
                const float hr = hpCoef * (hpR + wr - hpXR); hpXR = wr; hpR = hr;
                float bl = hl, brr = hr;
                if (blur > 0.01f)
                {
                    float dl = hl, dr = hr;
                    for (int s = 0; s < 4; ++s) { dl = difL[(size_t) s].process (dl); dr = difR[(size_t) s].process (dr); }
                    bl = hl * (1.0f - blur) + dl * blur;
                    brr = hr * (1.0f - blur) + dr * blur;
                }
                const float inL = hold ? 0.0f : l[i], inR = hold ? 0.0f : r[i];
                lineL.push (satL.process (inL + bl * fb));
                lineR.push (satR.process (inR + brr * fb));

                histL[(size_t) histW] = echoL;
                histR[(size_t) histW] = echoR;
                histW = (histW + 1) % histSize;

                float outL = echoL * mix * 1.4f, outR = echoR * mix * 1.4f;
                holdFade += ((hold ? 1.0f : 0.0f) - holdFade) * 0.002f;
                if (holdFade > 0.001f && heldLen > 0)
                {
                    const int p = (int) heldPos;
                    outL += heldL[(size_t) p] * heldGain * holdFade * mix * 1.4f;
                    outR += heldR[(size_t) p] * heldGain * holdFade * mix * 1.4f;
                    heldPos += 1.0;
                    if (heldPos >= heldLen) heldPos = 0.0;
                }
                l[i] += outL;
                r[i] += outR;
            }
            meter[0] = hold ? 1.0f : 0.0f;
            meter[1] = heldLen > 0 ? (float) (heldPos / heldLen) : 0.0f;
            meter[2] = T;
        }
    };

    //==========================================================================
    // Crowd: one sound sung by up to twelve people, each with their own timing, tuning and place
    struct CrowdNode : public Node
    {
        DelayLine line;
        struct Person
        {
            double phase = 0.0;
            float wander = 0.0f, wanderV = 0.0f, gain = 0.0f;
            float delayS = 0.0f;
            OnePoleLP airL, airR, shadow;
            float level = 0.0f;
        };
        std::array<Person, Param::Crowd::people> folk;
        Rng rng;

        CrowdNode (const Module& m, double sampleRate) : Node (m, sampleRate)
        {
            line.prepare ((int) (sr * 0.3));
            rng.state = 31337;
            for (int i = 0; i < Param::Crowd::people; ++i) folk[(size_t) i].phase = (double) i * 0.37;
        }

        void process (float* l, float* r, int n, double) override
        {
            using namespace Param::Crowd;
            const int count = headcount (k (Size));
            const float spread = k (Spread), timing = k (Timing), mix = k (Mix);
            const float tune = raw (Tuning), depth = raw (Depth);
            const float swayHz = expMap (raw (Sway), 0.1f, 4.0f);

            struct Setup { float del, depthS, rate, gl, gr, itdL, itdR, gainT; };
            std::array<Setup, people> set {};
            for (int i = 0; i < people; ++i)
            {
                auto& p = folk[(size_t) i];
                const float quirk = raw (Quirk0 + i);
                const auto pl = place (raw (HomeX0 + i), raw (HomeY0 + i), spread, depth);
                const float del = lateness (timing, quirk, pl.sy);
                const float rate = swayHz * (0.6f + quirk * 0.8f);
                const float cents = tune * 30.0f * (0.5f + quirk * 0.5f);
                const float depthS = juce::jmin ((cents / 1731.0f) / (twoPi * rate), del * 0.9f);
                // Placement: level difference (equal power) and the far ear hearing it a little later and duller
                const float th = (pl.sx * 0.9f + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
                const float itd = std::abs (pl.sx) * 0.00063f;
                p.airL.set (sr, expMap (1.0f - pl.sy * 0.9f, 1200.0f, 16000.0f));
                p.airR.a = p.airL.a;
                p.shadow.set (sr, 18000.0f - std::abs (pl.sx) * 14000.0f);
                const float g = i < count ? (1.0f - pl.sy * 0.6f) / std::sqrt ((float) count) * 1.2f : 0.0f;
                set[(size_t) i] = { del, depthS, rate, std::cos (th), std::sin (th), pl.sx > 0.0f ? itd : 0.0f, pl.sx < 0.0f ? itd : 0.0f, g };
            }
            const float gainA = coefFor (sr, 0.05f);

            for (int i = 0; i < n; ++i)
            {
                const float x = (l[i] + r[i]) * 0.5f;
                line.push (x);
                float wl = 0.0f, wr = 0.0f;
                for (int pi = 0; pi < people; ++pi)
                {
                    auto& p = folk[(size_t) pi];
                    const auto& st = set[(size_t) pi];
                    p.gain += (st.gainT - p.gain) * gainA;
                    if (p.gain < 1e-4f && st.gainT <= 0.0f) { p.level = 0.0f; continue; }
                    p.phase += st.rate / sr;
                    if (p.phase >= 1.0) p.phase -= 1.0;
                    // Organic movement: a sine plus a slow random wander, as real singers drift
                    p.wanderV += (rng.bi() - p.wanderV) * (st.rate * 2.0f / (float) sr);
                    p.wander += (p.wanderV * 6.0f - p.wander) * (st.rate * 4.0f / (float) sr);
                    p.delayS += (st.del - p.delayS) * 0.0005f;
                    const float d = p.delayS + st.depthS * (0.75f * std::sin (twoPi * (float) p.phase) + 0.25f * juce::jlimit (-1.0f, 1.0f, p.wander));
                    const float base = juce::jmax (0.0005f, d) * (float) sr;
                    float outL = p.airL.process (line.read (base + st.itdL * (float) sr));
                    float outR = p.airR.process (line.read (base + st.itdR * (float) sr));
                    if (st.itdL > 0.0f) outL = p.shadow.process (outL);          // the far ear is in the head's shadow
                    else if (st.itdR > 0.0f) outR = p.shadow.process (outR);
                    wl += outL * st.gl * p.gain;
                    wr += outR * st.gr * p.gain;
                    p.level = juce::jmax (std::abs (outL), p.level * 0.9995f);
                }
                l[i] = l[i] * (1.0f - mix * 0.7f) + wl * mix * 1.3f * 1.41f;
                r[i] = r[i] * (1.0f - mix * 0.7f) + wr * mix * 1.3f * 1.41f;
            }
            for (int pi = 0; pi < people; ++pi)
            {
                views[(size_t) pi].level = juce::jlimit (0.0f, 1.0f, folk[(size_t) pi].level * 2.0f);
                views[(size_t) pi].pitch = pi < count ? 0 : -1;
            }
            display = (float) count;
        }
    };

    //==========================================================================
    // Transformer: the sound passes through a board of failing parts
    struct TransformerNode : public Node
    {
        std::vector<float> ringL, ringR;
        int wp = 0;
        float env = 0.0f, drop = 1.0f, potG = 1.0f;
        int dropT = 0, potT = 0, potK = 0;
        struct Stutter { int len = 0, start = 0, k = 0, reps = 0; bool on = false; } st;
        float hold[2] {}, hpS[2] {}, hpX[2] {}, dcs[2] {};
        float hc = 0.0f;
        double ph = 0.0;
        Rng rng;
        DiodeAA diodeAA[2];
        TanhAA sagAA[2], humAA[2], outAA[2];
        std::array<float, Param::Transformer::parts> activity {};

        TransformerNode (const Module& m, double sampleRate) : Node (m, sampleRate)
        {
            ringL.assign ((size_t) (sr * 0.5), 0.0f);
            ringR.assign ((size_t) (sr * 0.5), 0.0f);
            rng.state = 1234;
        }

        float amount (int part) const
        {
            using namespace Param::Transformer;
            return on (On0 + part) ? k (Cap + part) : 0.0f;
        }

        void process (float* l, float* r, int n, double) override
        {
            using namespace Param::Transformer;
            const float starve = 1.0f - k (Battery), mix = k (Mix), outGain = k (Output) * 1.4f;
            const float aCap = amount (DyingCap), aPot = amount (ScratchyPot), aDi = amount (LeakyDiode), aSag = amount (SaggingSupply);
            const float aClk = amount (BentClock), aJ = amount (ColdJoint), aHum = amount (HotHum);
            const float fs = (float) sr;
            const float hpC = std::exp (-twoPi * (20.0f + aCap * aCap * 1100.0f) / fs);
            std::array<float, parts> mx {};
            const int RL = (int) ringL.size();

            // A cold joint loses contact and the last moment repeats
            if (aJ > 0.0f && ! st.on && rng.uni() < aJ * aJ * 0.08f * (float) n / 1024.0f)
            {
                st.len = (int) (fs * (0.03f + rng.uni() * 0.09f));
                st.start = ((wp - st.len) % RL + RL) % RL;
                st.k = 0;
                st.reps = 2 + (int) (rng.uni() * 4.0f * aJ + 1.0f);
                st.on = true;
            }

            for (int i = 0; i < n; ++i)
            {
                ph += 1.0 / sr;
                if (ph > 1000.0) ph -= 1000.0;
                // Dying cap: now and then the signal nearly drops out
                if (aCap > 0.0f)
                {
                    if (dropT <= 0 && rng.uni() < aCap * aCap * 5.0f / fs) dropT = (int) (fs * (0.006f + rng.uni() * 0.04f * aCap));
                    if (dropT > 0) { --dropT; drop += (0.03f - drop) * 0.02f; mx[DyingCap] = 1.0f; }
                    else drop += (1.0f - drop) * 0.01f;
                }
                else drop = 1.0f;
                // Scratchy pot: the wiper skips and the level jumps about
                if (aPot > 0.0f)
                {
                    if (potT <= 0 && rng.uni() < aPot * 3.0f / fs) potT = (int) (fs * (0.03f + rng.uni() * 0.12f));
                    if (potT > 0) { --potT; if (++potK >= 96) { potK = 0; potG = rng.uni() * rng.uni(); } mx[ScratchyPot] = 1.0f; }
                    else potG += (1.0f - potG) * 0.02f;
                }
                else potG = 1.0f;
                // Bent clock: the sample clock runs slow and wobbles
                const float hN = 1.0f + aClk * aClk * 34.0f * (1.0f + 0.35f * std::sin (twoPi * 0.7f * (float) ph));
                hc += 1.0f;
                const bool take = hc >= hN;
                if (take) hc -= hN;

                float* io[2] = { l, r };
                for (int c = 0; c < 2; ++c)
                {
                    float x = io[c][i];
                    auto& ring = c == 0 ? ringL : ringR;
                    const float dry = x;
                    ring[(size_t) wp] = x;
                    if (st.on) { x = ring[(size_t) ((st.start + st.k % juce::jmax (1, st.len)) % RL)]; mx[ColdJoint] = 1.0f; }
                    if (aCap > 0.0f)
                    {
                        const float y = hpC * (hpS[c] + x - hpX[c]);
                        hpX[c] = x; hpS[c] = y;
                        x = y * drop;
                    }
                    x *= potG;
                    if (potT > 0 && rng.uni() < 0.002f) x += (rng.uni() - 0.5f) * 0.6f * aPot;
                    if (aDi > 0.0f)
                    {
                        // A leaky diode: rectifies a little and clips one side only
                        dcs[c] += (std::abs (x) - dcs[c]) * 0.0015f;
                        x = x + aDi * 1.4f * (std::abs (x) - dcs[c]);
                        x = diodeAA[c].process (x, 1.0f + aDi * 3.0f, 1.0f + aDi * 0.6f, 1.0f - aDi * 0.4f);
                        mx[LeakyDiode] = juce::jmax (mx[LeakyDiode], juce::jmin (1.0f, std::abs (x) * 2.0f));
                    }
                    if (c == 0) { const float a = std::abs (x); env += (a - env) * (a > env ? 0.01f : 0.00012f); }
                    // Sagging supply: loud passages pull the rails down and the headroom goes
                    const float dr = 1.0f + aSag * 3.0f + starve * 5.0f, gs = 1.0f / (1.0f + env * aSag * 7.0f * (1.0f + starve * 2.0f));
                    x = sagAA[c].process (x * dr * gs) / juce::jmax (0.6f, std::sqrt (dr) * 0.8f);
                    if (aSag > 0.0f) mx[SaggingSupply] = juce::jmax (mx[SaggingSupply], juce::jmin (1.0f, (1.0f - gs) * 2.5f));
                    // A flat battery gates the quiet bits
                    if (starve > 0.05f && env < starve * 0.05f && rng.uni() < 0.5f) x = 0.0f;
                    if (aHum > 0.0f)
                    {
                        const float t = (float) ph;
                        x += aHum * 0.05f * (std::sin (twoPi * 50.0f * t) + 0.5f * std::sin (twoPi * 100.0f * t) + 0.3f * std::sin (twoPi * 150.0f * t));
                        x = humAA[c].process (x * (1.0f + aHum * 2.0f)) / (1.0f + aHum * 0.6f);
                        mx[HotHum] = aHum;
                    }
                    if (aClk > 0.0f)
                    {
                        if (take) hold[c] = x;
                        x = hold[c];
                        const float q = std::pow (2.0f, 12.0f - aClk * 8.0f);
                        x = std::round (x * q) / q;
                        mx[BentClock] = aClk;
                    }
                    const float wet = outAA[c].process (x * 1.05f) * (1.0f - starve * 0.35f);
                    io[c][i] = (wet * mix + dry * (1.0f - mix)) * outGain;
                }
                if (st.on) { ++st.k; if (st.k >= st.len * st.reps) st.on = false; }
                wp = (wp + 1) % RL;
            }
            for (int p = 0; p < parts; ++p)
            {
                activity[(size_t) p] = juce::jmax (mx[(size_t) p], activity[(size_t) p] * 0.85f);
                views[(size_t) p].level = activity[(size_t) p];       // the board's lamps flicker with each part's trouble
            }
            meter[0] = env;
        }
    };
}

std::unique_ptr<Node> createWorkshopNode (const Module& m, double sr, const Graph::SampleResolver& resolve)
{
    switch (m.type)
    {
        case ModuleType::GasTube:     return std::make_unique<GasTubeNode> (m, sr);
        case ModuleType::CrateDigger: return std::make_unique<CrateNode> (m, sr, resolve);
        case ModuleType::StringWall:  return std::make_unique<StringsNode> (m, sr);
        case ModuleType::GhostTag:    return std::make_unique<GhostNode> (m, sr);
        case ModuleType::Crowd:       return std::make_unique<CrowdNode> (m, sr);
        case ModuleType::Transformer: return std::make_unique<TransformerNode> (m, sr);
        case ModuleType::Input: case ModuleType::Output: case ModuleType::Sampler: case ModuleType::Synth: case ModuleType::Drums:
        case ModuleType::Filter: case ModuleType::Drive: case ModuleType::Delay: case ModuleType::Reverb: case ModuleType::EQ:
        case ModuleType::Compressor: case ModuleType::LFO: case ModuleType::Plugin: case ModuleType::Attenuator: case ModuleType::Mixer:
        case ModuleType::Inverter: case ModuleType::Crossfade: case ModuleType::Slew:
            break;
    }
    return nullptr;
}
