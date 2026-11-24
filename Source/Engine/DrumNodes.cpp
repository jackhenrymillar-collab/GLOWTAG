#include "Graph.h"
#include "DeviceParams.h"
#include "Samples.h"

// The Drum Rack and the Drum Rack Sequencer.
//
// The rack: sixteen pads on MIDI 36..51. Each pad plays a sample or one of six analogue voices, through
// its own envelope (attack, hold, decay), start point and direction, filter, drive and snap, then its
// level, pitch and pan, choke group, mute and solo. A pad goes to the kit's output, where the kit's
// dirt colours it, or to the rack's own output jack when that is patched.
//
// The sequencer: four patterns of sixteen rows, one row per pad, each row as long as you like, every
// step with its own velocity, chance and ratchet. It plays with the transport and sends its notes
// straight into whatever its notes output is patched to.

using namespace analog;

namespace
{
    float semis (float s) { return std::pow (2.0f, s / 12.0f); }

    struct Noise
    {
        juce::uint32 state = 22222;
        float next() { state = state * 1664525u + 1013904223u; return (float) (state >> 8) * (2.0f / 16777216.0f) - 1.0f; }
    };

    void balance (float pan, float& gl, float& gr)
    {
        gl = juce::jmin (1.0f, 2.0f * (1.0f - pan));
        gr = juce::jmin (1.0f, 2.0f * pan);
    }

    // The same steady random number for the same step, row and use, so a step's chance and its humanised
    // timing come out the same however the blocks fall
    float hash01 (long long step, int row, int salt)
    {
        auto z = (juce::uint64) step * 0x9E3779B97F4A7C15ull + (juce::uint64) (row * 131 + salt) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        z ^= z >> 31;
        return (float) (z >> 40) / (float) (1ull << 24);
    }

    inline float sq (float phase) { return phase < 0.5f ? 1.0f : -1.0f; }
    inline void advance (float& phase, float inc) { phase += inc; phase -= std::floor (phase); }

    //==========================================================================
    struct DrumsNode : public Node
    {
        std::array<std::shared_ptr<const AudioData>, 16> pads;

        struct Voice
        {
            bool active = false, fading = false;
            double pos = 0.0;
            float vel = 0.0f, t = 0.0f, fade = 1.0f, level = 0.0f, tail = 1.0f;
            int model = 0;
            float a = 0.5f, b = 0.5f, c = 0.5f;           // the analogue voice's controls, as it was struck
            float p1 = 0.0f, p2 = 0.0f;
            std::array<float, 6> ph {};
            Noise noise;
            SVF tone;                                     // colours an analogue voice's noise or metal
            SVF filterL, filterR;                         // the pad's own filter
        };
        std::array<Voice, 16> voices;
        int holdCount = 0;
        float heldL = 0.0f, heldR = 0.0f;

        DrumsNode (const Module& m, double sampleRate, const Graph::SampleResolver& resolve) : Node (m, sampleRate)
        {
            for (size_t i = 0; i < 16; ++i)
            {
                pads[i] = resolve (m.padNames[i].isNotEmpty() ? m.padNames[i] : Samples::defaultPads()[i]);
                voices[i].noise.state = 1234u + (juce::uint32) i * 7919u;
                voices[i].tone.prepare (sr);
                voices[i].filterL.prepare (sr);
                voices[i].filterR.prepare (sr);
            }
            meter[0] = -1.0f;
        }

        bool isInstrument() const override { return true; }

        bool anySolo() const
        {
            for (int p = 0; p < 16; ++p) if (on (Param::Drums::Solo0 + p)) return true;
            return false;
        }
        bool audible (int pad, bool solo) const
        {
            using namespace Param::Drums;
            return ! on (Mute0 + pad) && (! solo || on (Solo0 + pad));
        }

        void trigger (int pad, float velocity)
        {
            using namespace Param::Drums;
            if (pad < 0 || pad >= 16) return;
            const int model = juce::jlimit (0, (int) numVoices - 1, choice (Voice0 + pad));
            if (model == Sample && pads[(size_t) pad] == nullptr) return;
            if (! audible (pad, anySolo())) return;
            auto& v = voices[(size_t) pad];
            v.active = true;
            v.fading = false;
            v.fade = 1.0f;
            v.t = 0.0f;
            v.vel = velocity;
            v.model = model;
            v.a = raw (VoiceA0 + pad); v.b = raw (VoiceB0 + pad); v.c = raw (VoiceC0 + pad);
            v.p1 = v.p2 = 0.0f;
            v.ph.fill (0.0f);
            v.tone.reset(); v.filterL.reset(); v.filterR.reset();
            if (model == Sample)
            {
                const double len = (double) juce::jmax (1, pads[(size_t) pad]->buffer.getNumSamples() - 1);
                const double start = juce::jlimit (0.0f, 0.95f, raw (Start0 + pad)) * len;
                v.pos = on (Reverse0 + pad) ? len - 1.0 - start : start;
                v.tail = 1.0e9f;
            }
            else
            {
                // How long the voice rings by itself, and the filter that colours it
                const float a = v.a, b = v.b, c = v.c;
                switch (model)
                {
                    case Kick:  v.tail = 3.5f; break;
                    case Snare: v.tail = 7.0f * juce::jmax (0.05f + c * 0.15f, 0.16f); v.tone.set (1800.0f + b * 2000.0f, 0.8f); break;
                    case Hat:   v.tail = 7.0f * (0.025f + (1.0f - c) * 0.5f); v.tone.set (7000.0f, 0.9f); break;
                    case Clap:  v.tail = 3.0f * (0.006f + a * 0.016f) + 7.0f * (0.05f + c * 0.35f); v.tone.set (800.0f + b * 1800.0f, 2.0f); break;
                    case Tom:   v.tail = 2.5f; v.tone.set (3000.0f, 0.7f); break;
                    case Perc:  v.tail = 7.0f * (0.03f + c * 0.4f); v.tone.set (1.5f * (380.0f + a * 700.0f), 3.0f); break;
                    default:    v.tail = 1.0f; break;
                }
            }
            // A pad in a choke group cuts off the others in it (the closed hat stops the open hat)
            if (const int group = choice (Choke0 + pad); group > 0)
                for (int o = 0; o < 16; ++o)
                    if (o != pad && voices[(size_t) o].active && choice (Choke0 + o) == group)
                        voices[(size_t) o].fading = true;
            display = 1.0f;
        }

        void noteOn (int pitch, float velocity, double) override { trigger (pitch - 36, velocity); }

        // One sample of an analogue voice, before the pad's envelope. `pf` is the pad's pitch as a ratio.
        float analogue (Voice& v, float pf, float dt)
        {
            using namespace Param::Drums;
            const float t = v.t, a = v.a, b = v.b, c = v.c;
            switch (v.model)
            {
                case Kick:
                {
                    // A sine that drops from a high thump to its body, with a click of noise on the front
                    const float f = (38.0f + a * 60.0f) * pf * (1.0f + b * 7.0f * std::exp (-t / 0.03f));
                    advance (v.p1, f * dt);
                    const float body = std::sin (twoPi * v.p1) * std::exp (-t / 0.5f);
                    const float click = c * v.noise.next() * std::exp (-t / 0.0025f) * 0.8f;
                    return (body + click) * 0.95f;
                }
                case Snare:
                {
                    // Two drum-head tones under a bright high-passed rattle
                    const float f = (140.0f + a * 160.0f) * pf;
                    advance (v.p1, f * dt);
                    advance (v.p2, f * 1.48f * dt);
                    const float head = (std::sin (twoPi * v.p1) + 0.4f * std::sin (twoPi * v.p2)) * std::exp (-t / (0.05f + c * 0.15f)) * 0.55f;
                    v.tone.process (v.noise.next());
                    const float rattle = v.tone.high * std::exp (-t / 0.16f) * (0.45f + 0.55f * b);
                    return head + rattle;
                }
                case Hat:
                {
                    // Six square waves at odd ratios: the metal of a cymbal, with noise mixed in
                    static const float ratios[6] = { 205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f };
                    const float base = (1.0f + a * 1.2f) * 1.8f * pf;
                    float metal = 0.0f;
                    for (size_t i = 0; i < 6; ++i) { advance (v.ph[i], ratios[i] * base * dt); metal += sq (v.ph[i]); }
                    const float x = metal * (1.0f / 6.0f) * (1.0f - b * 0.7f) + v.noise.next() * b * 0.7f;
                    v.tone.process (x);
                    return v.tone.high * std::exp (-t / (0.025f + (1.0f - c) * 0.5f)) * 1.4f;
                }
                case Clap:
                {
                    // Three quick bursts of band-passed noise, then the room's tail
                    const float sp = 0.006f + a * 0.016f;
                    float env = 0.0f;
                    for (int k = 0; k < 3; ++k)
                        if (t >= (float) k * sp) env += std::exp (-(t - (float) k * sp) / 0.004f);
                    if (t >= 3.0f * sp) env += 0.8f * std::exp (-(t - 3.0f * sp) / (0.05f + c * 0.35f));
                    v.tone.process (v.noise.next());
                    return v.tone.band * env * 1.6f;
                }
                case Tom:
                {
                    const float f = (70.0f + a * 220.0f) * pf * (1.0f + b * 1.5f * std::exp (-t / 0.05f));
                    advance (v.p1, f * dt);
                    v.tone.process (v.noise.next());
                    return std::sin (twoPi * v.p1) * std::exp (-t / 0.35f) * 0.9f + c * 0.35f * v.tone.low * std::exp (-t / 0.03f);
                }
                case Perc:
                {
                    // Two squares a strange interval apart, rung through a band pass: cowbells, blocks, bells
                    const float f1 = (380.0f + a * 700.0f) * pf, f2 = f1 * (1.15f + b * 0.8f);
                    advance (v.p1, f1 * dt);
                    advance (v.p2, f2 * dt);
                    v.tone.process ((sq (v.p1) + sq (v.p2)) * 0.5f);
                    return v.tone.band * std::exp (-t / (0.03f + c * 0.4f)) * 1.5f;
                }
                default: return 0.0f;
            }
        }

        void process (float* l, float* r, int n, double) override
        {
            using namespace Param::Drums;
            const float tune = (k (Tune) - 0.5f) * 24.0f;
            const float decay = k (Decay), kitSnap = k (Snap), gain = k (Gain), sens = raw (VelSens);
            const float decayRate = decay >= 0.99f ? 0.0f : (1.0f - decay) * 30.0f;
            const float chokeFall = std::exp (-1.0f / (0.008f * (float) sr));
            const float dt = 1.0f / (float) sr;
            const bool solo = anySolo();
            if (auxPatched)
            {
                std::fill (auxL.begin(), auxL.begin() + n, 0.0f);
                std::fill (auxR.begin(), auxR.begin() + n, 0.0f);
            }
            for (int p = 0; p < 16; ++p)
            {
                auto& v = voices[(size_t) p];
                auto& view = views[(size_t) p];
                if (! v.active) { view.pitch = -1; view.level = 0.0f; continue; }
                if (! audible (p, solo)) v.fading = true;

                const float pitch = tune + (raw (Pitch0 + p) - 0.5f) * 48.0f;
                const float level = raw (Level0 + p) / 0.8f;
                float gl, gr;
                balance (raw (Pan0 + p), gl, gr);
                const float atk = raw (Attack0 + p), hold = raw (Hold0 + p), dec = raw (PDecay0 + p);
                const float snap = juce::jmin (1.5f, kitSnap + raw (PSnap0 + p));
                const float velGain = 1.0f - sens + sens * v.vel;
                const float drive = raw (Drive0 + p);
                const float push = 1.0f + drive * 7.0f, pushNorm = (1.0f - 0.35f * drive) / fastTanh (push);
                const int mode = juce::jlimit (0, 2, choice (FMode0 + p));
                const float cut = raw (Cutoff0 + p), res = raw (Res0 + p);
                const bool filtered = ! (mode == LP && cut >= 0.995f);
                if (filtered)
                {
                    const float q = 0.6f + res * 7.0f;
                    v.filterL.set (cutoffHz (cut), q);
                    v.filterR.set (cutoffHz (cut), q);
                }
                const bool own = choice (Out0 + p) == 1 && auxPatched;
                float* outL = own ? auxL.data() : l;
                float* outR = own ? auxR.data() : r;
                const float g0 = gain * level * velGain;

                const bool sample = v.model == Sample;
                const AudioData* s = sample ? pads[(size_t) p].get() : nullptr;
                if (sample && s == nullptr) { v.active = false; continue; }
                const int len = sample ? s->buffer.getNumSamples() - 1 : 0;
                const float* sl = sample ? s->buffer.getReadPointer (0) : nullptr;
                const float* srr = sample ? s->buffer.getReadPointer (juce::jmin (1, s->buffer.getNumChannels() - 1)) : nullptr;
                const bool reverse = sample && on (Reverse0 + p);
                const double rate = sample ? semis (pitch) * s->sampleRate / sr * (reverse ? -1.0 : 1.0) : 0.0;
                const float pf = semis (pitch);

                for (int i = 0; i < n; ++i)
                {
                    float xl, xr;
                    if (sample)
                    {
                        const int idx = (int) v.pos;
                        if (idx >= len || v.pos < 0.0) { v.active = false; break; }
                        const float frac = (float) (v.pos - idx);
                        xl = sl[idx] + frac * (sl[idx + 1] - sl[idx]);
                        xr = srr[idx] + frac * (srr[idx + 1] - srr[idx]);
                        v.pos += rate;
                    }
                    else
                    {
                        if (v.t > v.tail) { v.active = false; break; }
                        xl = xr = analogue (v, pf, dt);
                    }
                    if (v.fading && (v.fade *= chokeFall) < 0.0005f) { v.active = false; break; }

                    const float env = padEnvelope (v.t, atk, hold, dec) * (decayRate > 0.0f ? std::exp (-v.t * decayRate) : 1.0f);
                    if (env < 0.0001f && v.t > attackSec (atk) + holdSec (hold)) { v.active = false; break; }
                    const float transient = 1.0f + snap * 2.0f * std::exp (-v.t * 120.0f);
                    const float shape = env * transient;
                    xl *= shape; xr *= shape;
                    if (drive > 0.01f) { xl = fastTanh (xl * push) * pushNorm; xr = fastTanh (xr * push) * pushNorm; }
                    if (filtered)
                    {
                        v.filterL.process (xl);
                        v.filterR.process (xr);
                        xl = mode == LP ? v.filterL.low : (mode == BP ? v.filterL.band * 1.6f : v.filterL.high);
                        xr = mode == LP ? v.filterR.low : (mode == BP ? v.filterR.band * 1.6f : v.filterR.high);
                    }
                    const float g = g0 * v.fade;
                    v.level = env * v.fade;
                    outL[i] += xl * g * gl;
                    outR[i] += xr * g * gr;
                    v.t += dt;
                }
                view.pitch = v.active ? 36 + p : -1;
                view.level = v.active ? v.level : 0.0f;
                view.velocity = v.vel;
                view.age = v.t;
                view.pos = sample && len > 0 ? (float) (v.pos / (double) len) : 0.0f;
            }

            // Dirt, on the kit's own output: a coarser sample clock, fewer bits and a push into saturation
            const float dirt = raw (Dirt);
            if (dirt > 0.01f)
            {
                const int holdN = 1 + (int) (dirt * 7.0f);
                const float levels = std::pow (2.0f, 15.0f - dirt * 10.0f), pushD = 1.0f + dirt * 3.0f, norm = 1.0f / fastTanh (pushD);
                for (int i = 0; i < n; ++i)
                {
                    if (holdCount++ % holdN == 0) { heldL = l[i]; heldR = r[i]; }
                    l[i] = fastTanh (std::round (heldL * levels) / levels * pushD) * norm;
                    r[i] = fastTanh (std::round (heldR * levels) / levels * pushD) * norm;
                }
            }
        }
    };

    //==========================================================================
    struct DrumSeqNode : public Node
    {
        std::vector<Node*> targets;

        DrumSeqNode (const Module& m, double sampleRate) : Node (m, sampleRate)
        {
            meter[0] = -1.0f;
            targets.reserve (4);
        }

        void addNoteTarget (Node* n) override
        {
            if (std::find (targets.begin(), targets.end(), n) == targets.end())
                targets.push_back (n);
        }

        void fire (int row, float velocity, double beat)
        {
            for (auto* t : targets)
                t->noteOn (36 + row, velocity, 0.0);
            views[(size_t) row].velocity = velocity;
            views[(size_t) row].released = (float) beat;     // when it last played, for the lamps
        }

        void process (float*, float*, int n, double) override
        {
            using namespace Param::DrumSeq;
            if (host == nullptr || ! host->playing || host->beatsPerSample <= 0.0 || ! on (Play))
            {
                meter[0] = -1.0f;
                for (auto& v : views) v.slice = -1;
                return;
            }
            const double b0 = host->beat, b1 = b0 + n * host->beatsPerSample;
            const double stepLen = stepBeats (choice (Rate));
            const double swing = k (Swing) * 0.5 * stepLen;
            const float human = k (Humanise);
            const int pattern = juce::jlimit (0, patterns - 1, choice (Pattern));

            // Steps that could land in this block: the one before (late by swing or humanise) up to now
            const long long first = (long long) std::floor (b0 / stepLen) - 1, last = (long long) std::floor (b1 / stepLen);
            for (long long s = juce::jmax (0LL, first); s <= last; ++s)
            {
                const double base = (double) s * stepLen + ((s & 1) ? swing : 0.0);
                for (int row = 0; row < rows; ++row)
                {
                    const int len = juce::jlimit (1, steps, juce::roundToInt (raw (Length0 + row)));
                    const auto st = decode (raw (stepParam (pattern, row, (int) (s % len))));
                    if (! st.on) continue;
                    if (st.chance < 100 && hash01 (s, row, 1) * 100.0f >= (float) st.chance) continue;
                    for (int h = 0; h < st.ratchet; ++h)
                    {
                        const double at = base + (double) h * stepLen / st.ratchet
                                          + (double) (human * (hash01 (s, row, 2 + h) - 0.5f)) * stepLen * 0.25;
                        if (at < b0 || at >= b1) continue;
                        float vel = (float) st.velocity / 127.0f;
                        if (human > 0.0f) vel *= 1.0f - human * 0.25f * hash01 (s, row, 9 + h);
                        if (h > 0) vel *= 0.85f;                 // a ratchet's later hits a little softer
                        fire (row, juce::jlimit (0.02f, 1.0f, vel), at);
                    }
                }
            }
            const long long now = (long long) std::floor (b0 / stepLen);
            meter[0] = (float) (now % 16);
            meter[1] = (float) pattern;
            for (int row = 0; row < rows; ++row)
                views[(size_t) row].slice = (int) (now % juce::jlimit (1, steps, juce::roundToInt (raw (Length0 + row))));
        }
    };
}

std::unique_ptr<Node> createDrumNode (const Module& m, double sr, const Graph::SampleResolver& resolve)
{
    if (m.type == ModuleType::Drums)   return std::make_unique<DrumsNode> (m, sr, resolve);
    if (m.type == ModuleType::DrumSeq) return std::make_unique<DrumSeqNode> (m, sr);
    return nullptr;
}
