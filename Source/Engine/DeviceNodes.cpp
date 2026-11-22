#include "Graph.h"
#include "DeviceParams.h"
#include "Samples.h"
#include <juce_dsp/juce_dsp.h>

// The built-in instruments and effects. Each reads its card knobs through k() (which adds modulation
// from cables) and the expanded view's controls through raw() / choice() / on().

using namespace analog;
using Param::expMap;

namespace
{
    float midiHz (float note) { return 440.0f * std::pow (2.0f, (note - 69.0f) / 12.0f); }
    float dbToGain (float db) { return std::pow (10.0f, db / 20.0f); }
    float semis (float s)     { return std::pow (2.0f, s / 12.0f); }

    // A cheap white noise source, one per voice or node
    struct Noise
    {
        juce::uint32 state = 22222;
        float next() { state = state * 1664525u + 1013904223u; return (float) (state >> 8) * (2.0f / 16777216.0f) - 1.0f; }
    };

    // Balance: centre leaves both sides alone, a side turns the other one down
    void balance (float pan, float& gl, float& gr)
    {
        gl = juce::jmin (1.0f, 2.0f * (1.0f - pan));
        gr = juce::jmin (1.0f, 2.0f * pan);
    }

    // A pitch shifter made of two crossfading taps on a delay line (the shimmer's octave up)
    struct Shifter
    {
        std::vector<float> line;
        int write = 0, window = 2048;
        float phase = 0.0f;
        void prepare (double sr) { window = juce::jmax (256, (int) (sr * 0.045)); line.assign ((size_t) window * 2 + 8, 0.0f); }
        float process (float x, float ratio)
        {
            const int size = (int) line.size();
            line[(size_t) write] = x;
            phase += (ratio - 1.0f) / (float) window;
            phase -= std::floor (phase);
            float out = 0.0f;
            for (int tap = 0; tap < 2; ++tap)
            {
                const float p = std::fmod (phase + tap * 0.5f, 1.0f);
                const float d = (float) window * (1.0f - p) + 2.0f;
                float pos = (float) write - d;
                while (pos < 0.0f) pos += (float) size;
                const int i0 = (int) pos, i1 = (i0 + 1) % size;
                const float f = pos - (float) i0;
                const float w = std::sin (pi * p);
                out += (line[(size_t) i0] + f * (line[(size_t) i1] - line[(size_t) i0])) * w * w;
            }
            write = (write + 1) % size;
            return out;
        }
    };

    //==========================================================================
    // Virtual analog polysynth. Two oscillators (each up to five unison copies) and a sub, noise,
    // drift, hard sync; a ladder or a state variable filter swept by its own envelope or the amp's;
    // two LFOs and a four-row modulation matrix; poly, mono or legato with glide.
    struct SynthNode : public Node
    {
        static constexpr int maxUni = 5;
        struct Voice
        {
            std::array<Oscillator, maxUni> a, b;
            Oscillator sub;
            Envelope amp, filt;
            Ladder ladder;
            SVF svf;
            Noise noise;
            int pitch = -1;
            float vel = 0.0f, drift = 0.0f, wander = 0.0f, wanderRate = 0.3f;
            float note = 60.0f, target = 60.0f;          // glide moves `note` towards `target`
            double gate = -1.0, age = 0.0, released = -1.0;
            bool active = false;
        };
        std::array<Voice, 8> voices;
        juce::Random rng;
        double lfoPhase[2] {};
        float randomHeld = 0.0f;
        double randomClock = 0.0;
        float lastNote = -1.0f;
        std::vector<int> held;                           // mono / legato note stack

        SynthNode (const Module& m, double sampleRate) : Node (m, sampleRate)
        {
            int seed = 1;
            for (auto& v : voices)
            {
                for (auto& o : v.a) o.prepare (sr);
                for (auto& o : v.b) o.prepare (sr);
                v.sub.prepare (sr); v.amp.prepare (sr); v.filt.prepare (sr); v.ladder.prepare (sr); v.svf.prepare (sr);
                v.noise.state = (juce::uint32) (seed++ * 7919);
            }
            held.reserve (32);
        }

        bool isInstrument() const override { return true; }

        void start (Voice& v, int pitch, float velocity, double gateSamples, bool retrigger)
        {
            using namespace Param::Synth;
            const bool wasActive = v.active;
            v.active = true;
            v.pitch = pitch;
            v.vel = velocity;
            v.gate = gateSamples;
            v.target = (float) pitch;
            const float glide = raw (Glide);
            v.note = (glide > 0.01f && lastNote >= 0.0f) ? (wasActive && ! retrigger ? v.note : lastNote) : (float) pitch;
            lastNote = (float) pitch;
            if (! retrigger)
                return;
            v.age = 0.0;
            v.released = -1.0;
            const float drift = raw (Drift);
            v.drift = (rng.nextFloat() - 0.5f) * drift * 0.0133f;     // each note lands a hair off, like real oscillators
            v.wanderRate = 0.15f + rng.nextFloat() * 0.4f;
            for (auto& o : v.a) o.reset (rng.nextFloat());
            for (auto& o : v.b) o.reset (rng.nextFloat());
            v.sub.reset();
            v.ladder.reset();
            v.svf.reset();
            v.amp.noteOn();
            v.filt.noteOn();
        }

        void release (Voice& v)
        {
            v.amp.noteOff();
            v.filt.noteOff();
            if (v.released < 0.0) v.released = v.age;
        }

        void noteOn (int pitch, float velocity, double gateSamples) override
        {
            const int voicing = choice (Param::Synth::VMode);
            if (voicing == Param::Synth::Poly)
            {
                Voice* target = nullptr;
                for (auto& v : voices) if (! v.active) { target = &v; break; }
                if (target == nullptr)
                {
                    target = &voices[0];
                    for (auto& v : voices) if (v.age > target->age) target = &v;
                }
                start (*target, pitch, velocity, gateSamples, true);
                return;
            }
            // One voice: the newest note wins; legato keeps the envelopes going while a note is held
            for (size_t i = 1; i < voices.size(); ++i) if (voices[i].active) release (voices[i]);
            held.erase (std::remove (held.begin(), held.end(), pitch), held.end());
            if (held.size() < 32) held.push_back (pitch);
            auto& v = voices[0];
            const bool legato = voicing == Param::Synth::Legato && v.active && v.released < 0.0;
            start (v, pitch, velocity, gateSamples, ! legato);
        }

        void noteOff (int pitch) override
        {
            if (choice (Param::Synth::VMode) == Param::Synth::Poly)
            {
                for (auto& v : voices)
                    if (v.active && v.pitch == pitch && v.gate < 0.0)
                        release (v);
                return;
            }
            held.erase (std::remove (held.begin(), held.end(), pitch), held.end());
            auto& v = voices[0];
            if (! v.active || v.pitch != pitch) return;
            if (! held.empty())
            {
                // Back to the note still held, without starting the envelopes again
                v.pitch = held.back();
                v.target = (float) held.back();
                if (raw (Param::Synth::Glide) <= 0.01f) v.note = v.target;
            }
            else if (v.gate < 0.0)
                release (v);
        }

        void allNotesOff() override { held.clear(); for (auto& v : voices) { release (v); v.gate = 0.0; } }

        float source (int src, const Voice& v, float l1, float l2) const
        {
            switch (src)
            {
                case Param::Synth::Lfo1:     return l1;
                case Param::Synth::Lfo2:     return l2;
                case Param::Synth::Velocity: return (v.vel - 0.5f) * 2.0f;
                default:                     return randomHeld;
            }
        }

        void process (float* l, float* r, int n, double) override
        {
            using namespace Param::Synth;
            const float shapeA = k (Wave) * 3.0f, shapeB = juce::jlimit (0.0f, 1.0f, raw (WaveB)) * 3.0f;
            const float subAmt = k (Sub), det = k (Detune) * 0.03f;
            const float cutoff = expMap (k (Cutoff), 40.0f, 16000.0f), res = k (Res), envAmt = k (Env);
            const float drive = k (Drive), gain = k (Gain) * 0.5f;
            const float a = expMap (k (Attack), 0.001f, 4.0f), d = expMap (k (Decay), 0.01f, 6.0f), s = k (Sustain), rel = expMap (k (Release), 0.01f, 8.0f);
            const bool link = on (Link);
            const float fa = link ? a * 0.7f : expMap (raw (FAttack), 0.001f, 4.0f), fd = link ? d : expMap (raw (FDecay), 0.01f, 6.0f);
            const float fs = link ? s * 0.5f : juce::jlimit (0.0f, 1.0f, raw (FSustain)), fr = link ? rel : expMap (raw (FRelease), 0.01f, 8.0f);
            const float octA = std::pow (2.0f, (float) juce::roundToInt (raw (OctA) * 4.0f - 2.0f));
            const float semiB = semis ((float) juce::roundToInt (raw (SemiB) * 24.0f - 12.0f));
            const float noiseAmt = raw (Param::Synth::Noise) * 0.5f, driftAmt = raw (Drift);
            const float keyAmt = raw (Key), velCut = raw (Vel) * 3.0f, velAmp = juce::jlimit (0.0f, 1.0f, raw (VelAmp));
            const int uni = 1 + juce::jlimit (0, 4, juce::roundToInt (raw (Unison) * 4.0f));
            const float spread = raw (Spread) * 50.0f, uniGain = 1.0f / std::sqrt ((float) uni);
            const bool sync = on (Sync);
            const int fmode = juce::jlimit (0, 3, choice (FMode));
            const float glide = raw (Glide) * 0.8f;
            const float glideCoef = glide > 0.01f ? 1.0f - std::exp (-(float) n / (glide / 3.0f * (float) sr)) : 1.0f;

            // Control-rate modulation sources
            for (int i = 0; i < 2; ++i)
                lfoPhase[i] = std::fmod (lfoPhase[i] + expMap (raw (i == 0 ? L1Rate : L2Rate), 0.05f, 20.0f) * n / sr, 1.0);
            const float l1 = Param::lfoShape (choice (L1Shape), lfoPhase[0]), l2 = Param::lfoShape (choice (L2Shape), lfoPhase[1]);
            const double rc = randomClock;
            randomClock += 3.0 * n / sr;
            if ((juce::int64) randomClock != (juce::int64) rc) randomHeld = rng.nextFloat() * 2.0f - 1.0f;

            int playing = 0;
            for (size_t vi = 0; vi < voices.size(); ++vi)
            {
                auto& v = voices[vi];
                auto& view = views[vi];
                if (! v.active) { view.pitch = -1; continue; }
                ++playing;

                float toCut = 0.0f, toPitch = 0.0f, toWidth = 0.0f, toRes = 0.0f;
                for (int row = 0; row < matrixRows; ++row)
                {
                    const float amt = juce::jlimit (-1.0f, 1.0f, raw (MxAmt0 + row * 3));
                    if (std::abs (amt) < 0.001f) continue;
                    const float val = amt * source (choice (MxSrc0 + row * 3), v, l1, l2);
                    switch (choice (MxDst0 + row * 3))
                    {
                        case ToCutoff:    toCut += val; break;
                        case ToPitch:     toPitch += val; break;
                        case ToWidthA:    toWidth += val; break;
                        default:          toRes += val; break;
                    }
                }

                v.amp.set (a, d, s, rel);
                v.filt.set (fa, fd, fs, fr);
                v.note += (v.target - v.note) * glideCoef;
                v.wander += v.wanderRate * (float) n / (float) sr;
                const float hz = midiHz (v.note + toPitch * 1.2f) * (1.0f + v.drift + std::sin (twoPi * v.wander) * driftAmt * 0.002f);
                for (int u = 0; u < uni; ++u)
                {
                    const float off = uni > 1 ? ((float) u / (float) (uni - 1) - 0.5f) * spread : 0.0f;
                    v.a[(size_t) u].setFrequency (hz * octA * (1.0f + det) * semis (off / 100.0f));
                    v.b[(size_t) u].setFrequency (hz * semiB * (1.0f - det) * semis (-off / 100.0f));
                }
                v.sub.setFrequency (hz * 0.5f);
                const float pw = juce::jlimit (0.04f, 0.96f, 0.05f + raw (WidthA) * 0.9f + toWidth * 0.35f);
                const float keyOct = std::log2 (hz / 261.6f) * keyAmt;
                const float resNow = juce::jlimit (0.0f, 1.0f, res + toRes * 0.4f);
                const float velGain = (1.0f - velAmp) + velAmp * v.vel;

                for (int i = 0; i < n; ++i)
                {
                    if (v.gate >= 0.0 && v.gate-- <= 0.0) { release (v); v.gate = -2.0; }
                    const float env = v.amp.process();
                    const float fenv = v.filt.process();
                    if (! v.amp.isActive()) { v.active = false; break; }

                    float x = 0.0f;
                    for (int u = 0; u < uni; ++u)
                    {
                        x += v.a[(size_t) u].process (shapeA, pw);
                        if (sync && v.a[(size_t) u].wrapped) v.b[(size_t) u].phase = v.a[(size_t) u].phase;
                        x += v.b[(size_t) u].process (shapeB);
                    }
                    x *= 0.5f * uniGain;
                    x += v.sub.process (1.0f) * subAmt * 0.6f;
                    if (noiseAmt > 0.0001f) x += v.noise.next() * noiseAmt;

                    const float fc = cutoff * std::pow (2.0f, fenv * envAmt * 5.0f + (v.vel - 0.5f) * velCut + keyOct + toCut * 2.0f);
                    if (fmode == LP24)
                    {
                        v.ladder.set (fc, resNow, drive * 0.5f);
                        x = v.ladder.process (x);
                    }
                    else
                    {
                        v.svf.set (fc, 0.6f + resNow * 11.0f);
                        v.svf.process (x * (1.0f + drive * 0.5f));
                        x = fmode == LP12 ? v.svf.low : (fmode == BP ? v.svf.band : v.svf.high);
                    }
                    x = fastTanh (x * (1.0f + drive * 3.0f)) / (1.0f + drive * 0.8f);
                    x *= env * velGain * gain;
                    l[i] += x;
                    r[i] += x;
                    v.age += 1.0;
                }

                view.pitch = v.active ? v.pitch : -1;
                view.velocity = v.vel;
                view.level = v.amp.level();
                view.filter = v.filt.level();
                view.age = (float) (v.age / sr);
                view.released = v.released < 0.0 ? -1.0f : (float) (v.released / sr);
            }
            display = (float) playing;
            meter[0] = l1;
            meter[1] = l2;
        }
    };

    //--------------------------------------------------------------------------
    // Sample player: classic, one-shot, slices or a loop; forwards or reversed; repitched or stretched
    // (pitch moves, length stays); an envelope with hold; a filter, drive, tape and crush after the voices.
    struct SamplerNode : public Node
    {
        std::shared_ptr<const AudioData> sample;

        struct Voice
        {
            double pos = 0.0, base = 0.0, step = 1.0, pitchRatio = 1.0;
            double a = 0.0, b = 0.0, la = 0.0, lb = 0.0;     // region and loop, in frames
            double grain[2] {};
            float grainPhase[2] { 0.0f, 0.5f };
            double gate = -1.0, age = 0.0, released = -1.0;
            Envelope env;
            float vel = 0.0f;
            int pitch = -1, slice = -1, dir = 1;
            bool active = false, oneShot = false, loop = false, stretch = false;
        };
        std::array<Voice, 16> voices;
        SVF fl, fr;
        DCBlock dcl, dcr;

        SamplerNode (const Module& m, double sampleRate, const Graph::SampleResolver& resolve) : Node (m, sampleRate)
        {
            sample = resolve (m.sampleName);
            for (auto& v : voices) v.env.prepare (sr);
            fl.prepare (sr); fr.prepare (sr);
        }

        bool isInstrument() const override { return true; }

        std::vector<std::pair<float, float>> currentSlices() const
        {
            using namespace Param::Sampler;
            Param::SliceSettings st;
            st.start = k (Start);
            st.end = juce::jmin (1.0f, st.start + juce::jmax (0.01f, k (Length)));
            st.sens = raw (Sens);
            st.slicer = choice (Slicer);
            st.grid = gridCount (choice (Grid));
            for (int i = 0; i < maxMarkers; ++i) if (raw (Marker0 + i) >= 0.0f) st.manual.push_back (raw (Marker0 + i));
            return Param::slicesFrom (st.start, st.end, Param::sliceMarkers (st, sample->onsets));
        }

        void noteOn (int pitch, float velocity, double gateSamples) override
        {
            using namespace Param::Sampler;
            if (sample == nullptr) return;
            const int mode = choice (Mode);
            const auto slices = currentSlices();
            std::pair<float, float> region { slices.front().first, slices.back().second };
            int slice = -1;
            float noteOffset = (float) (pitch - rootNote (raw (Root)));
            if (mode == Slice)
            {
                slice = pitch - 36;
                if (slice < 0 || slice >= (int) slices.size()) return;
                region = slices[(size_t) slice];
                noteOffset = 0.0f;
            }

            // Voice count: the oldest gives way
            const int maxVoices = voiceCount (choice (Voices));
            int playing = 0;
            Voice* oldest = nullptr;
            for (int i = 0; i < maxVoices; ++i)
                if (voices[(size_t) i].active)
                {
                    ++playing;
                    if (oldest == nullptr || voices[(size_t) i].age > oldest->age) oldest = &voices[(size_t) i];
                }
            Voice* target = nullptr;
            if (playing >= maxVoices) target = oldest;
            else for (int i = 0; i < maxVoices; ++i) if (! voices[(size_t) i].active) { target = &voices[(size_t) i]; break; }
            if (target == nullptr) return;

            const double len = sample->buffer.getNumSamples();
            auto& v = *target;
            v.active = true;
            v.pitch = pitch;
            v.slice = slice;
            v.vel = velocity;
            v.gate = gateSamples;
            v.age = 0.0;
            v.released = -1.0;
            v.oneShot = mode == OneShot;
            v.loop = mode == Loop;
            v.stretch = choice (Warp) == 1;
            v.dir = on (Reverse) ? -1 : 1;
            v.a = region.first * len;
            v.b = juce::jmax (v.a + 2.0, (double) region.second * len - 2.0);
            v.la = v.a + (v.b - v.a) * juce::jlimit (0.0f, 1.0f, raw (LoopA));
            v.lb = juce::jmax (v.la + 64.0, v.a + (v.b - v.a) * juce::jlimit (0.0f, 1.0f, raw (LoopB)));
            v.pitchRatio = std::pow (2.0, (double) ((k (Pitch) - 0.5f) * 48.0f + (raw (Fine) - 0.5f) + noteOffset) / 12.0);
            v.pos = v.base = v.dir > 0 ? v.a : v.b;
            v.grain[0] = v.grain[1] = v.pos;
            v.grainPhase[0] = 0.0f; v.grainPhase[1] = 0.5f;
            v.env.noteOn();
        }

        void noteOff (int pitch) override
        {
            for (auto& v : voices)
                if (v.active && v.pitch == pitch && v.gate < 0.0 && ! v.oneShot) { v.env.noteOff(); if (v.released < 0.0) v.released = v.age; }
        }
        void allNotesOff() override { for (auto& v : voices) { v.env.noteOff(); v.gate = 0.0; } }

        float read (const float* data, double pos, int len) const
        {
            const int idx = juce::jlimit (0, len - 2, (int) pos);
            const float frac = (float) juce::jlimit (0.0, 1.0, pos - idx);
            return data[idx] + frac * (data[idx + 1] - data[idx]);
        }

        void process (float* l, float* r, int n, double) override
        {
            using namespace Param::Sampler;
            if (sample == nullptr) return;
            const int len = sample->buffer.getNumSamples();
            const float* sl = sample->buffer.getReadPointer (0);
            const float* srr = sample->buffer.getReadPointer (juce::jmin (1, sample->buffer.getNumChannels() - 1));
            const double rate = sample->sampleRate / sr;
            const float a = expMap (k (Attack), 0.001f, 2.0f), rel = expMap (k (Release), 0.005f, 4.0f), gain = k (Gain);
            const float hold = raw (Hold) * 0.5f, dec = expMap (raw (Decay), 0.01f, 6.0f), sus = juce::jlimit (0.0f, 1.0f, raw (Sustain));
            const float velg = juce::jlimit (0.0f, 1.0f, raw (VelGain));
            const double xfade = raw (XFade) * 0.12 * sample->sampleRate;
            const double grainLen = 0.08 * sample->sampleRate;
            int playing = 0;

            for (size_t vi = 0; vi < voices.size(); ++vi)
            {
                auto& v = voices[vi];
                auto& view = views[vi];
                if (! v.active) { view.pitch = -1; continue; }
                ++playing;
                v.env.set (a, dec, sus, rel);
                v.env.setHold (hold);
                const double speed = rate * (v.stretch ? 1.0 : v.pitchRatio);
                const float vg = (1.0f - velg) + velg * v.vel;
                for (int i = 0; i < n; ++i)
                {
                    if (v.gate >= 0.0 && v.gate-- <= 0.0)
                    {
                        if (! v.oneShot) { v.env.noteOff(); if (v.released < 0.0) v.released = v.age; }
                        v.gate = -2.0;
                    }
                    const float env = v.env.process();
                    if (! v.env.isActive()) { v.active = false; break; }

                    // Where the voice is in the file, and whether it has run off the end
                    double& p = v.stretch ? v.base : v.pos;
                    if (v.loop)
                    {
                        const double loopLen = v.lb - v.la;
                        if (v.dir > 0 && p >= v.lb) p -= loopLen;
                        if (v.dir < 0 && p <= v.la) p += loopLen;
                    }
                    else if ((v.dir > 0 && p >= v.b) || (v.dir < 0 && p <= v.a)) { v.active = false; break; }

                    float outL, outR;
                    if (v.stretch)
                    {
                        // Two grains read at the new pitch while the voice moves through the file at its own speed
                        outL = outR = 0.0f;
                        for (int gi = 0; gi < 2; ++gi)
                        {
                            const double at = v.grain[gi] + v.dir * (double) v.grainPhase[gi] * grainLen * v.pitchRatio;
                            const float w = std::sin (pi * v.grainPhase[gi]);
                            outL += read (sl, at, len) * w * w;
                            outR += read (srr, at, len) * w * w;
                            v.grainPhase[gi] += (float) (rate / grainLen);
                            if (v.grainPhase[gi] >= 1.0f) { v.grainPhase[gi] -= 1.0f; v.grain[gi] = v.base; }
                        }
                    }
                    else
                    {
                        outL = read (sl, v.pos, len);
                        outR = read (srr, v.pos, len);
                        if (v.loop && xfade > 1.0)
                        {
                            // Crossfade into the loop start as the loop end comes up
                            const double toEnd = v.dir > 0 ? v.lb - v.pos : v.pos - v.la;
                            if (toEnd < xfade)
                            {
                                const float t = (float) (1.0 - toEnd / xfade);
                                const double other = v.pos - v.dir * (v.lb - v.la);
                                outL += (read (sl, other, len) - outL) * t;
                                outR += (read (srr, other, len) - outR) * t;
                            }
                        }
                    }
                    p += v.dir * speed;
                    const float g = env * vg * gain;
                    l[i] += outL * g;
                    r[i] += outR * g;
                    v.age += 1.0;
                }
                view.pitch = v.active ? v.pitch : -1;
                view.velocity = v.vel;
                view.level = v.env.level();
                view.pos = (float) ((v.stretch ? v.base : v.pos) / juce::jmax (1, len));
                view.slice = v.slice;
                view.age = (float) (v.age / sr);
                view.released = v.released < 0.0 ? -1.0f : (float) (v.released / sr);
            }
            display = (float) playing;

            // Colour after the voices: filter, drive, tape, crush, pan
            const float cut = raw (Cutoff), res = raw (Res), drive = raw (Drive), tape = raw (Tape), crush = raw (Crush);
            const int fmode = choice (FMode);
            const bool filterOn = ! (fmode == 0 && cut >= 0.999f);
            if (filterOn)
            {
                const float fc = expMap (cut, 40.0f, 16000.0f), q = 0.6f + res * 12.0f;
                fl.set (fc, q); fr.set (fc, q);
            }
            const float dk = 1.0f + drive * 6.0f, dnorm = 1.0f / fastTanh (dk);
            const float levels = crush >= 0.02f ? std::pow (2.0f, 15.0f - (float) juce::roundToInt (crush * 12.0f)) : 0.0f;
            float gl, gr;
            balance (raw (Pan), gl, gr);
            for (int i = 0; i < n; ++i)
            {
                float x[2] = { l[i], r[i] };
                for (int c = 0; c < 2; ++c)
                {
                    float y = x[c];
                    if (filterOn)
                    {
                        auto& f = c == 0 ? fl : fr;
                        f.process (y);
                        y = fmode == 0 ? f.low : (fmode == 1 ? f.band : f.high);
                    }
                    if (drive > 0.005f) y = fastTanh (y * dk) * dnorm;
                    if (tape > 0.005f)
                    {
                        const float t = juce::jlimit (-1.5f, 1.5f, y);
                        y = y * (1.0f - tape * 0.25f) + tape * 0.25f * (t - 0.21f * t * t * (t > 0 ? 1.0f : -1.0f) + 0.08f * (1.0f - t * t));
                        y = (c == 0 ? dcl : dcr).process (y);
                    }
                    if (levels > 0.0f) y = std::round (y * levels) / levels;
                    x[c] = y;
                }
                l[i] = x[0] * gl;
                r[i] = x[1] * gr;
            }
        }
    };

    //--------------------------------------------------------------------------
    // Sixteen pads on MIDI 36..51, one sample each: global tune, decay, snap; per pad level, pitch,
    // pan and choke group; a sixteen-step row per pad that plays with the transport; swing; dirt.
    struct DrumsNode : public Node
    {
        std::array<std::shared_ptr<const AudioData>, 16> pads;
        struct Voice { double pos = 0.0; float vel = 0.0f, t = 0.0f, fade = 1.0f, level = 0.0f; bool active = false, fading = false; };
        std::array<Voice, 16> voices;
        long long lastStep = -1000000;
        double lastBeat = -1.0;
        int holdCount = 0;
        float heldL = 0.0f, heldR = 0.0f;

        DrumsNode (const Module& m, double sampleRate, const Graph::SampleResolver& resolve) : Node (m, sampleRate)
        {
            for (size_t i = 0; i < 16; ++i)
                pads[i] = resolve (m.padNames[i].isNotEmpty() ? m.padNames[i] : Samples::defaultPads()[i]);
            meter[0] = -1.0f;
        }

        bool isInstrument() const override { return true; }

        void trigger (int pad, float velocity)
        {
            using namespace Param::Drums;
            if (pad < 0 || pad >= 16 || pads[(size_t) pad] == nullptr) return;
            auto& v = voices[(size_t) pad];
            v.active = true;
            v.fading = false;
            v.fade = 1.0f;
            v.pos = 0.0;
            v.t = 0.0f;
            v.vel = velocity;
            // A pad in a choke group cuts off the others in it (the closed hat stops the open hat)
            if (const int group = choice (Choke0 + pad); group > 0)
                for (int o = 0; o < 16; ++o)
                    if (o != pad && voices[(size_t) o].active && choice (Choke0 + o) == group)
                        voices[(size_t) o].fading = true;
            display = 1.0f;
        }

        void noteOn (int pitch, float velocity, double) override { trigger (pitch - 36, velocity); }

        // The step row: sixteenths through the bar, the off-beats pushed late by the swing
        void runSteps (int n)
        {
            using namespace Param::Drums;
            if (host == nullptr || ! host->playing || host->beatsPerSample <= 0.0)
            {
                meter[0] = -1.0f;
                lastBeat = -1.0;
                return;
            }
            const double stepLen = 0.25, b0 = host->beat, b1 = b0 + n * host->beatsPerSample;
            if (b0 < lastBeat - 1e-6 || lastBeat < 0.0)
                lastStep = (long long) std::floor (b0 / stepLen) - 2;      // started, or the loop jumped back
            lastBeat = b0;
            const double swing = raw (Swing) * 0.5 * stepLen;
            for (long long s = (long long) std::floor (b0 / stepLen) - 1; s <= (long long) std::floor (b1 / stepLen); ++s)
            {
                const double at = (double) s * stepLen + ((s & 1) ? swing : 0.0);
                if (s <= lastStep || at < b0 - 1e-9 || at >= b1) continue;
                lastStep = s;
                const int idx = (int) (((s % 16) + 16) % 16);
                for (int p = 0; p < 16; ++p)
                    if ((juce::roundToInt (raw (Steps0 + p)) >> idx) & 1)
                        trigger (p, idx % 4 == 0 ? 0.95f : 0.75f);
            }
            meter[0] = (float) (((long long) std::floor (b0 / stepLen) % 16 + 16) % 16);
        }

        void process (float* l, float* r, int n, double) override
        {
            using namespace Param::Drums;
            runSteps (n);
            const float tune = (k (Tune) - 0.5f) * 24.0f;
            const float decay = k (Decay), snap = k (Snap), gain = k (Gain);
            const float decayRate = decay >= 0.99f ? 0.0f : (1.0f - decay) * 30.0f;
            const float chokeFall = std::exp (-1.0f / (0.008f * (float) sr));
            for (size_t p = 0; p < 16; ++p)
            {
                auto& v = voices[p];
                auto& view = views[p];
                if (! v.active) { view.pitch = -1; view.level = 0.0f; continue; }
                const auto& s = *pads[p];
                const int len = s.buffer.getNumSamples() - 1;
                const float* sl = s.buffer.getReadPointer (0);
                const float* srr = s.buffer.getReadPointer (juce::jmin (1, s.buffer.getNumChannels() - 1));
                const double rate = semis (tune + (raw (Pitch0 + (int) p) - 0.5f) * 24.0f) * s.sampleRate / sr;
                const float level = raw (Level0 + (int) p) / 0.8f;
                float gl, gr;
                balance (raw (Pan0 + (int) p), gl, gr);
                for (int i = 0; i < n; ++i)
                {
                    const int idx = (int) v.pos;
                    if (idx >= len) { v.active = false; break; }
                    if (v.fading && (v.fade *= chokeFall) < 0.0005f) { v.active = false; break; }
                    const float frac = (float) (v.pos - idx);
                    const float env = std::exp (-v.t * decayRate);
                    const float transient = 1.0f + snap * 2.0f * std::exp (-v.t * 120.0f);
                    const float g = v.vel * gain * env * transient * level * v.fade;
                    v.level = env * v.fade;
                    l[i] += (sl[idx] + frac * (sl[idx + 1] - sl[idx])) * g * gl;
                    r[i] += (srr[idx] + frac * (srr[idx + 1] - srr[idx])) * g * gr;
                    v.pos += rate;
                    v.t += 1.0f / (float) sr;
                }
                view.pitch = v.active ? 36 + (int) p : -1;
                view.level = v.active ? v.level : 0.0f;
                view.velocity = v.vel;
            }

            // Dirt: a coarser sample clock, fewer bits and a push into saturation
            const float dirt = raw (Dirt);
            if (dirt > 0.01f)
            {
                const int hold = 1 + (int) (dirt * 7.0f);
                const float levels = std::pow (2.0f, 15.0f - dirt * 10.0f), push = 1.0f + dirt * 3.0f, norm = 1.0f / fastTanh (push);
                for (int i = 0; i < n; ++i)
                {
                    if (holdCount++ % hold == 0) { heldL = l[i]; heldR = r[i]; }
                    l[i] = fastTanh (std::round (heldL * levels) / levels * push) * norm;
                    r[i] = fastTanh (std::round (heldR * levels) / levels * push) * norm;
                }
            }
        }
    };

    //--------------------------------------------------------------------------
    // Ladder filter morphing low pass to high pass, 24 or 12 dB, with drive, a vowel colour, a wobble
    // LFO (free or in time), an envelope follower that opens it with the music, and a dry / wet mix.
    struct FilterNode : public Node
    {
        Ladder left, right;
        SVF svfL, svfR;
        Biquad f1L, f1R, f2L, f2R;
        Follower follow;
        double phase = 0.0;
        float lastF1 = -1.0f, lastVowel = -1.0f;

        FilterNode (const Module& m, double sampleRate) : Node (m, sampleRate)
        {
            left.prepare (sr); right.prepare (sr); svfL.prepare (sr); svfR.prepare (sr);
            follow.prepare (sr, 0.005f, 0.12f);
        }

        void process (float* l, float* r, int n, double bpm) override
        {
            using namespace Param::Filter;
            for (int i = 0; i < n; ++i) follow.process (juce::jmax (std::abs (l[i]), std::abs (r[i])));
            const float env = juce::jlimit (0.0f, 1.0f, follow.value * 2.0f);

            if (on (WobbleSync))
            {
                const float beats = Param::wobbleBeats[juce::jlimit (0, 6, (int) (raw (Rate) * 6.999f))];
                if (host != nullptr && host->playing) phase = host->beat / beats;
                else phase += n / sr * (bpm / 60.0) / beats;
            }
            else
                phase += expMap (raw (Rate), 0.05f, 20.0f) * n / sr;
            phase -= std::floor (phase);

            const float oct = raw (Wobble) * 2.0f * std::sin (twoPi * (float) phase) + raw (Follow) * env * 5.0f;
            const float fc = juce::jlimit (20.0f, 19500.0f, expMap (k (Cutoff), 20.0f, 18000.0f) * std::pow (2.0f, oct));
            const float res = k (Res), drive = k (Drive), mode = k (Mode), mix = juce::jlimit (0.0f, 1.0f, raw (Mix));
            meter[0] = fc;
            meter[1] = env;
            const bool twelve = choice (Slope) == 1;
            if (twelve)
            {
                svfL.set (fc, 0.6f + res * 11.0f);
                svfR.set (fc, 0.6f + res * 11.0f);
            }
            else
            {
                left.set (fc, res, drive);
                right.set (fc, res, drive);
            }
            const float vowel = raw (Vowel);
            if (vowel > 0.01f && (std::abs (fc - lastF1) > 1.0f || ! juce::exactlyEqual (vowel, lastVowel)))
            {
                f1L.setPeak (sr, juce::jmax (80.0f, fc * 0.45f), vowel * 12.0f, 4.0f); f1R = f1L;
                f2L.setPeak (sr, juce::jmax (60.0f, fc * 0.16f), vowel * 12.0f, 4.0f); f2R = f2L;
                lastF1 = fc; lastVowel = vowel;
            }
            const float pre = 1.0f + drive * 4.0f;
            for (int i = 0; i < n; ++i)
            {
                float wl, wr;
                if (twelve)
                {
                    svfL.process (fastTanh (l[i] * pre));
                    svfR.process (fastTanh (r[i] * pre));
                    wl = svfL.low + (svfL.high - svfL.low) * mode;
                    wr = svfR.low + (svfR.high - svfR.low) * mode;
                }
                else
                {
                    wl = left.process (l[i], mode);
                    wr = right.process (r[i], mode);
                }
                if (vowel > 0.01f) { wl = f2L.process (f1L.process (wl)); wr = f2R.process (f1R.process (wr)); }
                l[i] += (wl - l[i]) * mix;
                r[i] += (wr - r[i]) * mix;
            }
        }
    };

    //--------------------------------------------------------------------------
    // Analog saturation: tube, tape, fuzz or fold, with bias (even harmonics), sag (the supply giving
    // way when hit hard), oversampling, a tone control, output level and a dry / wet mix.
    struct DriveNode : public Node
    {
        OnePole toneL, toneR;
        DCBlock dcL, dcR;
        Follower sag;
        std::unique_ptr<juce::dsp::Oversampling<float>> os[2];
        juce::AudioBuffer<float> work { 2, subBlock };
        std::array<float, subBlock> dryL {}, dryR {};

        DriveNode (const Module& m, double sampleRate) : Node (m, sampleRate)
        {
            toneL.prepare (sr); toneR.prepare (sr);
            sag.prepare (sr, 0.002f, 0.15f);
            for (int f = 0; f < 2; ++f)
            {
                os[f] = std::make_unique<juce::dsp::Oversampling<float>> (2, (size_t) (f + 1), juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
                os[f]->initProcessing ((size_t) subBlock);
            }
        }

        void process (float* l, float* r, int n, double) override
        {
            using namespace Param::Drive;
            const float drive = k (Amount), mix = k (Mix), bias = raw (Bias), out = raw (Output) / 0.7f, sagAmt = raw (Sag);
            const int curve = juce::jlimit (0, 3, (int) (k (Type) * 3.999f));
            toneL.setLowpass (expMap (k (Tone), 600.0f, 18000.0f));
            toneR.a = toneL.a;
            std::copy (l, l + n, dryL.begin());
            std::copy (r, r + n, dryR.begin());
            const float give = 1.0f / (1.0f + sagAmt * sag.value * 3.0f);
            auto shape = [&] (float x) { return Param::driveCurve (curve, drive, bias, x * give); };

            const int factor = juce::jlimit (0, 2, choice (Oversample));
            if (factor == 0)
            {
                for (int i = 0; i < n; ++i) { l[i] = shape (l[i]); r[i] = shape (r[i]); }
            }
            else
            {
                work.copyFrom (0, 0, l, n);
                work.copyFrom (1, 0, r, n);
                juce::dsp::AudioBlock<float> block (work.getArrayOfWritePointers(), 2, (size_t) n);
                auto& o = *os[factor - 1];
                auto up = o.processSamplesUp (block);
                for (size_t c = 0; c < up.getNumChannels(); ++c)
                {
                    auto* d = up.getChannelPointer (c);
                    for (size_t i = 0; i < up.getNumSamples(); ++i) d[i] = shape (d[i]);
                }
                o.processSamplesDown (block);
                std::copy (work.getReadPointer (0), work.getReadPointer (0) + n, l);
                std::copy (work.getReadPointer (1), work.getReadPointer (1) + n, r);
            }
            for (int i = 0; i < n; ++i)
            {
                const float wl = dcL.process (toneL.lowpass (l[i])) * out;
                const float wr = dcR.process (toneR.lowpass (r[i])) * out;
                sag.process (juce::jmax (std::abs (wl), std::abs (wr)));
                l[i] = dryL[(size_t) i] + (wl - dryL[(size_t) i]) * mix;
                r[i] = dryR[(size_t) i] + (wr - dryR[(size_t) i]) * mix;
            }
            meter[0] = sag.value;
        }
    };

    //--------------------------------------------------------------------------
    // Tape delay: tempo divisions or free time, ping-pong with a spread, tone in the feedback, flutter
    // on the heads, ducking under the dry signal, and freeze (the loop holds and nothing new goes in).
    struct DelayNode : public Node
    {
        std::vector<float> lineL, lineR;
        int write = 0;
        OnePole toneL, toneR;
        float smoothedDelay = 0.0f;
        double wow = 0.0, flut = 0.0;
        Follower duck;

        DelayNode (const Module& m, double sampleRate) : Node (m, sampleRate)
        {
            lineL.assign ((size_t) (sr * 4.0), 0.0f);
            lineR.assign ((size_t) (sr * 4.0), 0.0f);
            toneL.prepare (sr); toneR.prepare (sr);
            duck.prepare (sr, 0.005f, 0.25f);
        }

        float read (const std::vector<float>& b, float delaySamples) const
        {
            float pos = (float) write - delaySamples;
            while (pos < 0.0f) pos += (float) b.size();
            const int i0 = (int) pos % (int) b.size(), i1 = (i0 + 1) % (int) b.size();
            const float frac = pos - std::floor (pos);
            return b[(size_t) i0] + frac * (b[(size_t) i1] - b[(size_t) i0]);
        }

        void process (float* l, float* r, int n, double bpm) override
        {
            using namespace Param::Delay;
            const float maxDelay = (float) lineL.size() - 4.0f;
            const float seconds = on (Sync) ? Param::divisionBeats[Param::divisionIndex (k (Time))] * 60.0f / (float) bpm
                                            : expMap (k (Time), 0.01f, 2.0f);
            const float target = juce::jmin (maxDelay / 1.06f, seconds * (float) sr);
            const bool freeze = on (Freeze), pp = on (PingPong);
            const float fb = freeze ? 1.0f : k (Feedback) * 0.95f, mix = k (Mix);
            const float cross = pp ? juce::jlimit (0.0f, 1.0f, raw (Spread)) : 0.0f;
            const float rightStretch = pp ? 1.0f : 1.0f + raw (Spread) * 0.05f;
            const float flutter = raw (Flutter), duckAmt = raw (Duck);
            toneL.setLowpass (expMap (k (Tone), 800.0f, 16000.0f));
            toneR.a = toneL.a;
            if (smoothedDelay <= 0.0f) smoothedDelay = target;

            for (int i = 0; i < n; ++i)
            {
                smoothedDelay += (target - smoothedDelay) * 0.0005f;     // glides like a tape head moving
                wow += 0.7 / sr; flut += 5.3 / sr;
                const float wobble = 1.0f + flutter * (0.006f * std::sin (twoPi * (float) (flut - std::floor (flut))) + 0.004f * std::sin (twoPi * (float) (wow - std::floor (wow))));
                const float dL = juce::jmin (maxDelay, smoothedDelay * wobble), dR = juce::jmin (maxDelay, smoothedDelay * wobble * rightStretch);
                const float dl = read (lineL, dL), dr = read (lineR, dR);
                const float inL = freeze ? 0.0f : l[i], inR = freeze ? 0.0f : r[i];
                const float backL = (1.0f - cross) * dl + cross * dr, backR = (1.0f - cross) * dr + cross * dl;
                if (freeze)
                {
                    lineL[(size_t) write] = backL;
                    lineR[(size_t) write] = backR;
                }
                else
                {
                    lineL[(size_t) write] = fastTanh (inL + toneL.lowpass (backL) * fb);
                    lineR[(size_t) write] = fastTanh (inR + toneR.lowpass (backR) * fb);
                }
                write = (write + 1) % (int) lineL.size();
                const float env = duck.process (juce::jmax (std::abs (l[i]), std::abs (r[i])));
                const float wet = mix / (1.0f + duckAmt * env * 10.0f);
                l[i] += dl * wet;
                r[i] += dr * wet;
            }
            meter[0] = duck.value;
        }
    };

    //--------------------------------------------------------------------------
    // Reverb with pre-delay, a low cut into the tail, shimmer (an octave up fed back in), drift (the
    // tail slowly wavers), freeze, and a gate that shuts the tail when the playing stops.
    struct ReverbNode : public Node
    {
        juce::Reverb reverb;
        std::vector<float> preL, preR;
        int preWrite = 0;
        OnePole hp1L, hp1R, hp2L, hp2R;
        Shifter shiftL, shiftR;
        float shimL = 0.0f, shimR = 0.0f;
        std::vector<float> chorusL, chorusR;
        int chorusWrite = 0;
        double driftPhase = 0.0;
        Follower gateFollow;
        float gateGain = 1.0f, quietFor = 0.0f;
        std::array<float, subBlock> wetL {}, wetR {};

        ReverbNode (const Module& m, double sampleRate) : Node (m, sampleRate)
        {
            reverb.setSampleRate (sr);
            preL.assign ((size_t) (sr * 0.25) + 4, 0.0f);
            preR.assign (preL.size(), 0.0f);
            for (auto* f : { &hp1L, &hp1R, &hp2L, &hp2R }) f->prepare (sr);
            shiftL.prepare (sr); shiftR.prepare (sr);
            chorusL.assign ((size_t) (sr * 0.03) + 4, 0.0f);
            chorusR.assign (chorusL.size(), 0.0f);
            gateFollow.prepare (sr, 0.001f, 0.05f);
        }

        void process (float* l, float* r, int n, double) override
        {
            using namespace Param::Reverb;
            const float mix = k (Mix);
            juce::Reverb::Parameters p;
            p.roomSize = k (Size); p.damping = k (Damp); p.width = k (Width);
            p.wetLevel = 1.0f; p.dryLevel = 0.0f;
            p.freezeMode = on (Freeze) ? 1.0f : 0.0f;
            reverb.setParameters (p);

            const int preSamples = juce::jlimit (0, (int) preL.size() - 2, (int) (raw (PreDelay) * 0.2f * (float) sr));
            const float lowCut = raw (LowCut), shimmer = raw (Shimmer), drift = raw (Drift);
            if (lowCut >= 0.02f)
            {
                const float hz = expMap (lowCut, 20.0f, 800.0f);
                for (auto* f : { &hp1L, &hp1R, &hp2L, &hp2R }) f->setLowpass (hz);
            }
            const int preSize = (int) preL.size();
            for (int i = 0; i < n; ++i)
            {
                preL[(size_t) preWrite] = l[i];
                preR[(size_t) preWrite] = r[i];
                const int rd = (preWrite - preSamples + preSize) % preSize;
                preWrite = (preWrite + 1) % preSize;
                float xl = preL[(size_t) rd], xr = preR[(size_t) rd];
                if (lowCut >= 0.02f) { xl = hp2L.highpass (hp1L.highpass (xl)); xr = hp2R.highpass (hp1R.highpass (xr)); }
                wetL[(size_t) i] = xl + shimL * shimmer * 0.6f;
                wetR[(size_t) i] = xr + shimR * shimmer * 0.6f;
            }
            reverb.processStereo (wetL.data(), wetR.data(), n);

            const bool gated = on (Gated);
            const float holdTime = 0.05f + k (Size) * 0.3f;
            const int csize = (int) chorusL.size();
            for (int i = 0; i < n; ++i)
            {
                float wl = wetL[(size_t) i], wr = wetR[(size_t) i];
                if (shimmer > 0.01f)
                {
                    shimL = shiftL.process (wl * 0.33f, 2.0f);
                    shimR = shiftR.process (wr * 0.33f, 2.0f);
                    wl += shimL * shimmer * 0.5f;
                    wr += shimR * shimmer * 0.5f;
                }
                if (drift > 0.01f)
                {
                    chorusL[(size_t) chorusWrite] = wl;
                    chorusR[(size_t) chorusWrite] = wr;
                    driftPhase += 0.31 / sr;
                    const float base = 0.008f * (float) sr;
                    auto tap = [&] (const std::vector<float>& b, float d)
                    {
                        float pos = (float) chorusWrite - d;
                        while (pos < 0.0f) pos += (float) csize;
                        const int i0 = (int) pos % csize, i1 = (i0 + 1) % csize;
                        const float f = pos - std::floor (pos);
                        return b[(size_t) i0] + f * (b[(size_t) i1] - b[(size_t) i0]);
                    };
                    const float ph = twoPi * (float) (driftPhase - std::floor (driftPhase));
                    wl = tap (chorusL, base * (1.0f + 0.5f * drift * std::sin (ph)));
                    wr = tap (chorusR, base * (1.0f + 0.5f * drift * std::sin (ph + 1.9f)));
                    chorusWrite = (chorusWrite + 1) % csize;
                }
                if (gated)
                {
                    const float env = gateFollow.process (juce::jmax (std::abs (l[i]), std::abs (r[i])));
                    quietFor = env > 0.02f ? 0.0f : quietFor + 1.0f / (float) sr;
                    const float want = quietFor > holdTime ? 0.0f : 1.0f;
                    gateGain += (want - gateGain) * (want > gateGain ? 0.01f : 0.0015f);
                    wl *= gateGain; wr *= gateGain;
                }
                // Same balance as before these controls existed: dry lifted by the reverb's own dry scale
                l[i] = l[i] * 2.0f * (1.0f - mix * 0.5f) + wl * mix;
                r[i] = r[i] * 2.0f * (1.0f - mix * 0.5f) + wr * mix;
            }
            meter[0] = gateFollow.value;
        }
    };

    //--------------------------------------------------------------------------
    // Four-band EQ (low shelf, low mid, mid, high shelf) with movable frequencies and widths, low and
    // high cuts, a tilt across the bands and an analogue warmth stage.
    struct EQNode : public Node
    {
        std::array<Biquad, 4> bandL, bandR;
        Biquad lc1L, lc1R, lc2L, lc2R, hc1L, hc1R, hc2L, hc2R;
        std::array<float, 4 * 3> last {};
        float lastLow = -1.0f, lastHigh = -1.0f;
        std::array<Console, 2> console;
        using Node::Node;
        float lo (int i) const override
        {
            using namespace Param::EQ;
            return (i == Low || i == Mid || i == High) ? -1.0f : 0.0f;
        }

        void process (float* l, float* r, int n, double) override
        {
            using namespace Param::EQ;
            const float tilt = juce::jlimit (-1.0f, 1.0f, raw (Tilt));
            for (int b = 0; b < 4; ++b)
            {
                const int gp = gainParam[b];
                const float g = (gp == LowMid ? juce::jlimit (-1.0f, 1.0f, raw (gp)) : k (gp)) * 12.0f + tilt * 6.0f * tiltWeight[b];
                const float f = freqOf (raw (Freq0 + b)), q = juce::jlimit (0.3f, 6.0f, raw (Q0 + b));
                if (! juce::exactlyEqual (g, last[(size_t) b * 3]) || ! juce::exactlyEqual (f, last[(size_t) b * 3 + 1]) || ! juce::exactlyEqual (q, last[(size_t) b * 3 + 2]))
                {
                    if (b == 0)      bandL[0].setShelf (sr, f, g, false, juce::jmin (1.0f, q));
                    else if (b == 3) bandL[3].setShelf (sr, f, g, true, juce::jmin (1.0f, q));
                    else             bandL[(size_t) b].setPeak (sr, f, g, q);
                    // Keep each side's filter memory, take the new coefficients
                    auto& R = bandR[(size_t) b];
                    const float z1 = R.z1, z2 = R.z2;
                    R = bandL[(size_t) b];
                    R.z1 = z1; R.z2 = z2;
                    last[(size_t) b * 3] = g; last[(size_t) b * 3 + 1] = f; last[(size_t) b * 3 + 2] = q;
                }
            }
            const float lowCut = raw (LowCut), highCut = raw (HighCut);
            const bool lcOn = lowCut >= 0.02f, hcOn = highCut <= 0.98f;
            auto setPair = [this] (Biquad& a1, Biquad& a2, Biquad& b1, Biquad& b2, float hz, bool high)
            {
                for (auto* bq : { &a1, &a2, &b1, &b2 }) { const float z1 = bq->z1, z2 = bq->z2; bq->setPass (sr, hz, 0.707f, high); bq->z1 = z1; bq->z2 = z2; }
            };
            if (lcOn && ! juce::exactlyEqual (lowCut, lastLow))   { setPair (lc1L, lc2L, lc1R, lc2R, expMap (lowCut, 20.0f, 800.0f), true);   lastLow = lowCut; }
            if (hcOn && ! juce::exactlyEqual (highCut, lastHigh)) { setPair (hc1L, hc2L, hc1R, hc2R, expMap (highCut, 1000.0f, 20000.0f), false); lastHigh = highCut; }
            const bool warm = on (Analog);
            const float out = k (Output) * 1.4f;
            for (int i = 0; i < n; ++i)
            {
                float xl = l[i], xr = r[i];
                if (lcOn) { xl = lc2L.process (lc1L.process (xl)); xr = lc2R.process (lc1R.process (xr)); }
                for (size_t b = 0; b < 4; ++b) { xl = bandL[b].process (xl); xr = bandR[b].process (xr); }
                if (hcOn) { xl = hc2L.process (hc1L.process (xl)); xr = hc2R.process (hc1R.process (xr)); }
                if (warm) { xl = console[0].process (xl); xr = console[1].process (xr); }
                l[i] = xl * out;
                r[i] = xr * out;
            }
        }

        void prepareConsole() { console[0].prepare (sr); console[1].prepare (sr); }
    };

    //--------------------------------------------------------------------------
    // Compressor with a soft knee you can set, a dry / wet mix, a low cut on what it listens to,
    // auto makeup, three characters, and the choice of listening to another track (sidechain).
    struct CompressorNode : public Node
    {
        float envelope = 0.0f, rms = 0.0f;
        OnePole sc1, sc2;
        using Node::Node;

        void process (float* l, float* r, int n, double) override
        {
            using namespace Param::Comp;
            const int feel = juce::jlimit (0, 2, choice (Character));
            const float threshDb = -40.0f + k (Thresh) * 40.0f;
            const float ratio = juce::jmin (30.0f, (1.0f + k (Ratio) * 19.0f) * (feel == Crush ? 1.5f : 1.0f));
            const float attSec = expMap (k (Attack), 0.0001f, 0.1f) * (feel == Crush ? 0.25f : 1.0f);
            const float relSec = expMap (k (Release), 0.01f, 1.0f) * (feel == Glue ? 1.6f : 1.0f);
            const float att = std::exp (-1.0f / (attSec * (float) sr)), rel = std::exp (-1.0f / (relSec * (float) sr));
            float makeupDb = k (Makeup) * 24.0f;
            if (on (AutoMakeup)) makeupDb += -threshDb * (1.0f - 1.0f / ratio) * 0.5f;
            const float makeup = dbToGain (makeupDb);
            const float knee = raw (Knee) * 24.0f, mix = juce::jlimit (0.0f, 1.0f, raw (Mix));
            const float sideCut = raw (SideCut);
            if (sideCut >= 0.02f) { sc1.prepare (sr); sc2.prepare (sr); sc1.setLowpass (expMap (sideCut, 20.0f, 800.0f)); sc2.a = sc1.a; }
            const float rmsCoef = 1.0f - std::exp (-1.0f / (0.015f * (float) sr));

            // Another track to listen to: its sound from the last block
            const juce::AudioBuffer<float>* side = nullptr;
            const int listens = choice (ListensTo) - 1;
            if (listens >= 0 && host != nullptr && host->sideBuffers != nullptr && listens < host->numSide)
                side = host->sideBuffers[listens];

            float maxReduction = 0.0f, level = -90.0f;
            for (int i = 0; i < n; ++i)
            {
                float key;
                if (side != nullptr && side->getNumSamples() > 0)
                {
                    const int at = juce::jlimit (0, side->getNumSamples() - 1, host->blockPos + i);
                    key = side->getSample (0, at);
                    if (side->getNumChannels() > 1) key = std::abs (key) > std::abs (side->getSample (1, at)) ? key : side->getSample (1, at);
                }
                else
                    key = std::abs (l[i]) > std::abs (r[i]) ? l[i] : r[i];
                if (sideCut >= 0.02f) key = sc2.highpass (sc1.highpass (key));
                float in = std::abs (key);
                if (feel == Glue) { rms += (in * in - rms) * rmsCoef; in = std::sqrt (rms) * 1.41f; }
                envelope = in > envelope ? att * envelope + (1.0f - att) * in : rel * envelope + (1.0f - rel) * in;
                const float levelDb = 20.0f * std::log10 (juce::jmax (1.0e-6f, envelope));
                const float reductionDb = Param::compReduction (levelDb, threshDb, ratio, knee);
                const float g = dbToGain (-reductionDb) * makeup;
                float wl = l[i] * g, wr = r[i] * g;
                if (feel == Crush) { wl = fastTanh (wl * 1.4f) / 1.2f; wr = fastTanh (wr * 1.4f) / 1.2f; }
                l[i] += (wl - l[i]) * mix;
                r[i] += (wr - r[i]) * mix;
                maxReduction = juce::jmax (maxReduction, reductionDb);
                level = juce::jmax (level, levelDb);
            }
            display = maxReduction;
            meter[1] = level;
        }
    };

    //--------------------------------------------------------------------------
    // LFO feeding modulation cables: shape, rate free or in time, a phase offset, smoothing, chaos,
    // a fade in, bipolar or unipolar, and an option to start again on every note.
    struct LFONode : public Node
    {
        double phase = 0.0;
        float value = 0.0f, fadeClock = 0.0f;
        bool wasPlaying = false;
        using Node::Node;

        void anyNoteOn() override
        {
            if (on (Param::LFO::Retrig)) { phase = 0.0; fadeClock = 0.0f; }
        }

        void process (float*, float*, int n, double bpm) override
        {
            using namespace Param::LFO;
            const int sync = juce::jlimit (0, 4, choice (Sync));
            const bool playing = host != nullptr && host->playing;
            if (playing && ! wasPlaying) fadeClock = 0.0f;
            wasPlaying = playing;

            double ph;
            if (sync > 0)
            {
                const double beats = Param::lfoSyncBeats[sync];
                if (playing && ! on (Retrig)) phase = host->beat / beats;
                else phase += n / sr * (bpm / 60.0) / beats;
            }
            else
                phase += expMap (k (Rate), 0.05f, 20.0f) * n / sr;
            phase -= std::floor (phase);
            ph = phase + raw (Phase);

            fadeClock += (float) (n / sr);
            const float fade = raw (Fade);
            const float fadeGain = fade < 0.02f ? 1.0f : juce::jmin (1.0f, fadeClock / (fade * 4.0f));
            const int shape = juce::jlimit (0, 4, (int) (k (Shape) * 4.999f));
            const float v = Param::lfoValue (shape, ph, raw (Smooth), raw (Chaos), choice (Polarity) == 1);
            value = v * k (Depth) * 0.5f * fadeGain;
            display = v;
            meter[1] = (float) (ph - std::floor (ph));
            meter[2] = fadeGain;
        }
        float modValue() const override { return value; }
    };
}

std::unique_ptr<Node> createDeviceNode (const Module& m, double sr, const Graph::SampleResolver& resolve)
{
    switch (m.type)
    {
        case ModuleType::Sampler:    return std::make_unique<SamplerNode> (m, sr, resolve);
        case ModuleType::Synth:      return std::make_unique<SynthNode> (m, sr);
        case ModuleType::Drums:      return std::make_unique<DrumsNode> (m, sr, resolve);
        case ModuleType::Filter:     return std::make_unique<FilterNode> (m, sr);
        case ModuleType::Drive:      return std::make_unique<DriveNode> (m, sr);
        case ModuleType::Delay:      return std::make_unique<DelayNode> (m, sr);
        case ModuleType::Reverb:     return std::make_unique<ReverbNode> (m, sr);
        case ModuleType::EQ:         { auto e = std::make_unique<EQNode> (m, sr); e->prepareConsole(); return e; }
        case ModuleType::Compressor: return std::make_unique<CompressorNode> (m, sr);
        case ModuleType::LFO:        return std::make_unique<LFONode> (m, sr);
        case ModuleType::Input:
        case ModuleType::Output:
        case ModuleType::Plugin:
        case ModuleType::Attenuator:
        case ModuleType::Mixer:
        case ModuleType::Inverter:
        case ModuleType::Crossfade:
        case ModuleType::Slew:       break;
        case ModuleType::GasTube: case ModuleType::CrateDigger: case ModuleType::StringWall:
        case ModuleType::GhostTag: case ModuleType::Crowd: case ModuleType::Transformer:
            return createWorkshopNode (m, sr, resolve);
    }
    return nullptr;
}
