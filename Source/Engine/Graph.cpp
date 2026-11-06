#include "Graph.h"
#include "Samples.h"

using namespace analog;

namespace
{
    float expMap (float v, float lo, float hi) { return lo * std::pow (hi / lo, juce::jlimit (0.0f, 1.0f, v)); }
    float midiHz (float note) { return 440.0f * std::pow (2.0f, (note - 69.0f) / 12.0f); }
    float dbToGain (float db) { return std::pow (10.0f, db / 20.0f); }
}

Node::Node (const Module& m, double sampleRate) : id (m.id), type (m.type), info (moduleInfo (m.type)), sr (sampleRate)
{
    for (size_t i = 0; i < maxKnobs; ++i)
        knob[i] = i < m.knobValues.size() ? m.knobValues[i] : (i < info.knobs.size() ? info.knobs[i].second : 0.0f);
}

//==============================================================================
namespace
{
    struct PassNode : public Node
    {
        using Node::Node;
        void process (float*, float*, int, double) override {}
    };

    //--------------------------------------------------------------------------
    // Virtual analog polysynth: two drifting band-limited oscillators plus a sub, through a
    // transistor ladder with envelope sweep, an analog ADSR on the amp, and drive on the way out.
    struct SynthNode : public Node
    {
        enum { Wave, Sub, Detune, Cutoff, Res, EnvAmt, Attack, Decay, Sustain, Release, Drive, Gain };

        struct Voice
        {
            Oscillator a, b, sub;
            Envelope amp, filt;
            Ladder ladder;
            int pitch = -1;
            float vel = 0.0f, drift = 0.0f;
            double gate = -1.0, age = 0.0;
            bool active = false;
        };
        std::array<Voice, 8> voices;
        juce::Random rng;

        SynthNode (const Module& m, double sampleRate) : Node (m, sampleRate)
        {
            for (auto& v : voices)
            {
                v.a.prepare (sr); v.b.prepare (sr); v.sub.prepare (sr);
                v.amp.prepare (sr); v.filt.prepare (sr); v.ladder.prepare (sr);
            }
        }

        bool isInstrument() const override { return true; }

        void noteOn (int pitch, float velocity, double gateSamples) override
        {
            Voice* target = nullptr;
            for (auto& v : voices) if (! v.active) { target = &v; break; }
            if (target == nullptr)
            {
                target = &voices[0];
                for (auto& v : voices) if (v.age > target->age) target = &v;
            }
            auto& v = *target;
            v.active = true;
            v.pitch = pitch;
            v.vel = velocity;
            v.gate = gateSamples;
            v.age = 0.0;
            v.drift = (rng.nextFloat() - 0.5f) * 0.004f;       // each note lands a hair off, like real oscillators
            v.a.reset (rng.nextFloat()); v.b.reset (rng.nextFloat()); v.sub.reset();
            v.ladder.reset();
            v.amp.noteOn();
            v.filt.noteOn();
        }

        void noteOff (int pitch) override
        {
            for (auto& v : voices)
                if (v.active && v.pitch == pitch && v.gate < 0.0)
                    v.gate = 0.0;
        }

        void allNotesOff() override { for (auto& v : voices) { v.amp.noteOff(); v.filt.noteOff(); v.gate = 0.0; } }

        void process (float* l, float* r, int n, double) override
        {
            const float shape = k (Wave) * 3.0f;
            const float subAmt = k (Sub), det = k (Detune) * 0.03f;
            const float cutoff = expMap (k (Cutoff), 40.0f, 16000.0f), res = k (Res), envAmt = k (EnvAmt);
            const float drive = k (Drive), gain = k (Gain) * 0.5f;
            const float a = expMap (k (Attack), 0.001f, 4.0f), d = expMap (k (Decay), 0.01f, 6.0f), s = k (Sustain), rel = expMap (k (Release), 0.01f, 8.0f);
            int playing = 0;

            for (auto& v : voices)
            {
                if (! v.active) continue;
                ++playing;
                v.amp.set (a, d, s, rel);
                v.filt.set (a * 0.7f, d, s * 0.5f, rel);
                const float hz = midiHz ((float) v.pitch) * (1.0f + v.drift);
                v.a.setFrequency (hz * (1.0f + det));
                v.b.setFrequency (hz * (1.0f - det));
                v.sub.setFrequency (hz * 0.5f);

                for (int i = 0; i < n; ++i)
                {
                    if (v.gate >= 0.0 && v.gate-- <= 0.0) { v.amp.noteOff(); v.filt.noteOff(); v.gate = -2.0; }
                    const float env = v.amp.process();
                    const float fenv = v.filt.process();
                    if (! v.amp.isActive()) { v.active = false; break; }

                    float x = (v.a.process (shape) + v.b.process (shape)) * 0.5f + v.sub.process (1.0f) * subAmt * 0.6f;
                    const float fc = cutoff * std::pow (2.0f, fenv * envAmt * 5.0f + (v.vel - 0.5f) * 1.5f);
                    v.ladder.set (fc, res, drive * 0.5f);
                    x = v.ladder.process (x);
                    x = fastTanh (x * (1.0f + drive * 3.0f)) / (1.0f + drive * 0.8f);
                    x *= env * (0.4f + 0.6f * v.vel) * gain;
                    l[i] += x;
                    r[i] += x;
                    v.age += 1.0;
                }
            }
            display = (float) playing;
        }
    };

    //--------------------------------------------------------------------------
    // Sample player: start / length window, pitch around the root note, analog attack / release
    struct SamplerNode : public Node
    {
        enum { Start, Length, Pitch, Attack, Release, Gain };
        std::shared_ptr<const AudioData> sample;

        struct Voice { double pos = 0.0, step = 1.0, gate = -1.0, age = 0.0; Envelope env; float vel = 0.0f; int pitch = -1; bool active = false; };
        std::array<Voice, 8> voices;

        SamplerNode (const Module& m, double sampleRate, const Graph::SampleResolver& resolve) : Node (m, sampleRate)
        {
            sample = resolve (m.sampleName);
            for (auto& v : voices) v.env.prepare (sr);
        }

        bool isInstrument() const override { return true; }

        void noteOn (int pitch, float velocity, double gateSamples) override
        {
            if (sample == nullptr) return;
            Voice* target = nullptr;
            for (auto& v : voices) if (! v.active) { target = &v; break; }
            if (target == nullptr) { target = &voices[0]; for (auto& v : voices) if (v.age > target->age) target = &v; }
            auto& v = *target;
            v.active = true;
            v.pitch = pitch;
            v.vel = velocity;
            v.gate = gateSamples;
            v.age = 0.0;
            v.pos = k (Start) * sample->buffer.getNumSamples();
            v.env.noteOn();
        }

        void noteOff (int pitch) override
        {
            for (auto& v : voices) if (v.active && v.pitch == pitch && v.gate < 0.0) v.gate = 0.0;
        }
        void allNotesOff() override { for (auto& v : voices) { v.env.noteOff(); v.gate = 0.0; } }

        void process (float* l, float* r, int n, double) override
        {
            if (sample == nullptr) return;
            const int len = sample->buffer.getNumSamples();
            const double end = juce::jmin ((double) len - 1.0, (double) (k (Start) + juce::jmax (0.01f, k (Length))) * len);
            const float semis = (k (Pitch) - 0.5f) * 48.0f;
            const float a = expMap (k (Attack), 0.001f, 2.0f), rel = expMap (k (Release), 0.005f, 4.0f), gain = k (Gain);
            const float* sl = sample->buffer.getReadPointer (0);
            const float* srr = sample->buffer.getReadPointer (1);
            const double rate = sample->sampleRate / sr;
            int playing = 0;

            for (auto& v : voices)
            {
                if (! v.active) continue;
                ++playing;
                v.env.set (a, 0.01f, 1.0f, rel);
                v.step = rate * std::pow (2.0, (semis + (float) (v.pitch - 60)) / 12.0);
                for (int i = 0; i < n; ++i)
                {
                    if (v.gate >= 0.0 && v.gate-- <= 0.0) { v.env.noteOff(); v.gate = -2.0; }
                    const float env = v.env.process();
                    if (! v.env.isActive() || v.pos >= end) { v.active = false; break; }
                    const int idx = (int) v.pos;
                    const float frac = (float) (v.pos - idx);
                    const float g = env * v.vel * gain;
                    l[i] += (sl[idx] + frac * (sl[idx + 1] - sl[idx])) * g;
                    r[i] += (srr[idx] + frac * (srr[idx + 1] - srr[idx])) * g;
                    v.pos += v.step;
                    v.age += 1.0;
                }
            }
            display = (float) playing;
        }
    };

    //--------------------------------------------------------------------------
    // Sixteen pads on MIDI 36..51, one sample each, with global tune / decay / snap
    struct DrumsNode : public Node
    {
        enum { Tune, Decay, Snap, Gain };
        std::array<std::shared_ptr<const AudioData>, 16> pads;
        struct Voice { double pos = 0.0; float vel = 0.0f; bool active = false; float t = 0.0f; };
        std::array<Voice, 16> voices;

        DrumsNode (const Module& m, double sampleRate, const Graph::SampleResolver& resolve) : Node (m, sampleRate)
        {
            for (size_t i = 0; i < 16; ++i)
                pads[i] = resolve (m.padNames[i].isNotEmpty() ? m.padNames[i] : Samples::defaultPads()[i]);
        }

        bool isInstrument() const override { return true; }

        void noteOn (int pitch, float velocity, double) override
        {
            const int pad = pitch - 36;
            if (pad < 0 || pad >= 16 || pads[(size_t) pad] == nullptr) return;
            auto& v = voices[(size_t) pad];
            v.active = true;
            v.pos = 0.0;
            v.t = 0.0f;
            v.vel = velocity;
            // Closed hat chokes the open hat
            if (pad == 6) voices[10].active = false;
            display = 1.0f;
        }

        void process (float* l, float* r, int n, double) override
        {
            const double step = std::pow (2.0, (k (Tune) - 0.5f) * 24.0 / 12.0);
            const float decay = k (Decay), snap = k (Snap), gain = k (Gain);
            const float decayRate = decay >= 0.99f ? 0.0f : (1.0f - decay) * 30.0f;
            for (size_t p = 0; p < 16; ++p)
            {
                auto& v = voices[p];
                if (! v.active) continue;
                const auto& s = *pads[p];
                const int len = s.buffer.getNumSamples() - 1;
                const float* sl = s.buffer.getReadPointer (0);
                const float* srr = s.buffer.getReadPointer (1);
                const double rate = step * s.sampleRate / sr;
                for (int i = 0; i < n; ++i)
                {
                    const int idx = (int) v.pos;
                    if (idx >= len) { v.active = false; break; }
                    const float frac = (float) (v.pos - idx);
                    const float env = std::exp (-v.t * decayRate);
                    const float transient = 1.0f + snap * 2.0f * std::exp (-v.t * 120.0f);
                    const float g = v.vel * gain * env * transient;
                    l[i] += (sl[idx] + frac * (sl[idx + 1] - sl[idx])) * g;
                    r[i] += (srr[idx] + frac * (srr[idx + 1] - srr[idx])) * g;
                    v.pos += rate;
                    v.t += 1.0f / (float) sr;
                }
            }
        }
    };

    //--------------------------------------------------------------------------
    struct FilterNode : public Node
    {
        enum { Cutoff, Res, Drive, Mode };
        Ladder left, right;
        FilterNode (const Module& m, double sampleRate) : Node (m, sampleRate) { left.prepare (sr); right.prepare (sr); }
        void process (float* l, float* r, int n, double) override
        {
            const float fc = expMap (k (Cutoff), 20.0f, 18000.0f), res = k (Res), drive = k (Drive), mode = k (Mode);
            left.set (fc, res, drive);
            right.set (fc, res, drive);
            for (int i = 0; i < n; ++i)
            {
                l[i] = left.process (l[i], mode);
                r[i] = right.process (r[i], mode);
            }
        }
    };

    //--------------------------------------------------------------------------
    // Analog saturation: tube, tape, fuzz or fold, with a tone control and a dry / wet mix
    struct DriveNode : public Node
    {
        enum { Drive, Tone, Mix, Type };
        OnePole toneL, toneR;
        DCBlock dcL, dcR;
        DriveNode (const Module& m, double sampleRate) : Node (m, sampleRate) { toneL.prepare (sr); toneR.prepare (sr); }

        static float shape (float x, int curve)
        {
            switch (curve)
            {
                case 0: return sat::tube (x);
                case 1: return sat::tape (x);
                case 2: return sat::fuzz (x);
                default: return sat::fold (x);
            }
        }

        void process (float* l, float* r, int n, double) override
        {
            const float gain = 1.0f + k (Drive) * 24.0f, mix = k (Mix);
            const int curve = juce::jlimit (0, 3, (int) (k (Type) * 3.999f));
            const float comp = 1.0f / std::sqrt (gain);   // keep loudness roughly steady as drive rises
            toneL.setLowpass (expMap (k (Tone), 600.0f, 18000.0f));
            toneR.setLowpass (toneL.a > 0 ? expMap (k (Tone), 600.0f, 18000.0f) : 1000.0f);
            for (int i = 0; i < n; ++i)
            {
                const float wl = dcL.process (toneL.lowpass (shape (l[i] * gain, curve))) * comp * 1.6f;
                const float wr = dcR.process (toneR.lowpass (shape (r[i] * gain, curve))) * comp * 1.6f;
                l[i] += (wl - l[i]) * mix;
                r[i] += (wr - r[i]) * mix;
            }
        }
    };

    //--------------------------------------------------------------------------
    // Tempo-synced stereo delay with a tape-style tone roll-off in the feedback path
    struct DelayNode : public Node
    {
        enum { Time, Feedback, Tone, Mix };
        std::vector<float> lineL, lineR;
        int write = 0;
        OnePole toneL, toneR;
        float smoothedDelay = 0.0f;

        DelayNode (const Module& m, double sampleRate) : Node (m, sampleRate)
        {
            lineL.assign ((size_t) (sr * 4.0), 0.0f);
            lineR.assign ((size_t) (sr * 4.0), 0.0f);
            toneL.prepare (sr); toneR.prepare (sr);
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
            static const float divisions[] = { 0.25f, 1.0f / 3.0f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f };
            const float beats = divisions[juce::jlimit (0, 6, (int) (k (Time) * 6.999f))];
            const float target = juce::jmin ((float) lineL.size() - 2.0f, beats * 60.0f / (float) bpm * (float) sr);
            const float fb = k (Feedback) * 0.95f, mix = k (Mix);
            toneL.setLowpass (expMap (k (Tone), 800.0f, 16000.0f));
            toneR.a = toneL.a;
            if (smoothedDelay <= 0.0f) smoothedDelay = target;

            for (int i = 0; i < n; ++i)
            {
                smoothedDelay += (target - smoothedDelay) * 0.0005f;     // glides like a tape head moving
                const float dl = read (lineL, smoothedDelay), dr = read (lineR, smoothedDelay);
                lineL[(size_t) write] = fastTanh (l[i] + toneL.lowpass (dr) * fb);   // ping-pong cross feedback
                lineR[(size_t) write] = fastTanh (r[i] + toneR.lowpass (dl) * fb);
                write = (write + 1) % (int) lineL.size();
                l[i] += dl * mix;
                r[i] += dr * mix;
            }
        }
    };

    //--------------------------------------------------------------------------
    struct ReverbNode : public Node
    {
        enum { Size, Damp, Width, Mix };
        juce::Reverb reverb;
        ReverbNode (const Module& m, double sampleRate) : Node (m, sampleRate) { reverb.setSampleRate (sr); }
        void process (float* l, float* r, int n, double) override
        {
            juce::Reverb::Parameters p;
            p.roomSize = k (Size); p.damping = k (Damp); p.width = k (Width);
            p.wetLevel = k (Mix); p.dryLevel = 1.0f - k (Mix) * 0.5f;
            reverb.setParameters (p);
            reverb.processStereo (l, r, n);
        }
    };

    //--------------------------------------------------------------------------
    struct EQNode : public Node
    {
        enum { Low, Mid, High, Output };
        Biquad lowL, lowR, midL, midR, highL, highR;
        float lastLow = 99, lastMid = 99, lastHigh = 99;
        using Node::Node;
        float lo (int i) const override { return i < 3 ? -1.0f : 0.0f; }

        void process (float* l, float* r, int n, double) override
        {
            const float lowDb = k (Low) * 12.0f, midDb = k (Mid) * 12.0f, highDb = k (High) * 12.0f;
            if (! juce::exactlyEqual (lowDb, lastLow))  { lowL.setShelf (sr, 120.0f, lowDb, false);  lowR = lowL;  lastLow = lowDb; }
            if (! juce::exactlyEqual (midDb, lastMid))  { midL.setPeak (sr, 1000.0f, midDb, 0.9f);   midR = midL;  lastMid = midDb; }
            if (! juce::exactlyEqual (highDb, lastHigh)){ highL.setShelf (sr, 6000.0f, highDb, true); highR = highL; lastHigh = highDb; }
            const float out = k (Output) * 1.4f;
            for (int i = 0; i < n; ++i)
            {
                l[i] = highL.process (midL.process (lowL.process (l[i]))) * out;
                r[i] = highR.process (midR.process (lowR.process (r[i]))) * out;
            }
        }
    };

    //--------------------------------------------------------------------------
    // Feed-forward compressor with a soft knee and program-dependent feel; display = gain reduction in dB
    struct CompressorNode : public Node
    {
        enum { Thresh, Ratio, Attack, Release, Makeup };
        float envelope = 0.0f;
        using Node::Node;

        void process (float* l, float* r, int n, double) override
        {
            const float threshDb = -40.0f + k (Thresh) * 40.0f, ratio = 1.0f + k (Ratio) * 19.0f;
            const float att = std::exp (-1.0f / (expMap (k (Attack), 0.0001f, 0.1f) * (float) sr));
            const float rel = std::exp (-1.0f / (expMap (k (Release), 0.01f, 1.0f) * (float) sr));
            const float makeup = dbToGain (k (Makeup) * 24.0f);
            const float knee = 6.0f;
            float maxReduction = 0.0f;

            for (int i = 0; i < n; ++i)
            {
                const float in = juce::jmax (std::abs (l[i]), std::abs (r[i]));
                envelope = in > envelope ? att * envelope + (1.0f - att) * in : rel * envelope + (1.0f - rel) * in;
                const float levelDb = 20.0f * std::log10 (juce::jmax (1.0e-6f, envelope));
                const float over = levelDb - threshDb;
                float reductionDb = 0.0f;
                if (over > knee * 0.5f)           reductionDb = over - over / ratio;
                else if (over > -knee * 0.5f)     { const float x = over + knee * 0.5f; reductionDb = (1.0f - 1.0f / ratio) * x * x / (2.0f * knee); }
                const float g = dbToGain (-reductionDb) * makeup;
                l[i] *= g;
                r[i] *= g;
                maxReduction = juce::jmax (maxReduction, reductionDb);
            }
            display = maxReduction;
        }
    };

    //--------------------------------------------------------------------------
    // Low-frequency oscillator feeding modulation cables; display = current output (-1..1)
    struct LFONode : public Node
    {
        enum { Rate, Depth, Shape };
        double phase = 0.0;
        float value = 0.0f, held = 0.0f;
        juce::Random rng;
        using Node::Node;

        void process (float*, float*, int n, double) override
        {
            const float hz = expMap (k (Rate), 0.05f, 20.0f);
            const int shape = juce::jlimit (0, 4, (int) (k (Shape) * 4.999f));
            const double last = phase;
            phase = std::fmod (phase + hz * n / sr, 1.0);
            const float t = (float) phase;
            float raw = 0.0f;
            switch (shape)
            {
                case 0: raw = std::sin (twoPi * t); break;                       // sine
                case 1: raw = 1.0f - 4.0f * std::abs (t - 0.5f); break;          // triangle
                case 2: raw = 1.0f - 2.0f * t; break;                            // saw down
                case 3: raw = t < 0.5f ? 1.0f : -1.0f; break;                    // square
                default:                                                          // sample and hold
                    if (phase < last) held = rng.nextFloat() * 2.0f - 1.0f;
                    raw = held;
                    break;
            }
            value = raw * k (Depth) * 0.5f;
            display = raw;
        }
        float modValue() const override { return value; }
    };
}

std::unique_ptr<Node> createNode (const Module& m, double sr, const Graph::SampleResolver& resolve)
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
        case ModuleType::EQ:         return std::make_unique<EQNode> (m, sr);
        case ModuleType::Compressor: return std::make_unique<CompressorNode> (m, sr);
        case ModuleType::LFO:        return std::make_unique<LFONode> (m, sr);
        case ModuleType::Input:
        case ModuleType::Output:     break;
    }
    return std::make_unique<PassNode> (m, sr);
}

//==============================================================================
Graph::Graph (const Patch& patch, double sampleRate, const SampleResolver& resolve)
{
    // Build nodes, then order them so every node runs after the ones feeding it (Kahn's algorithm)
    std::vector<std::unique_ptr<Node>> made;
    for (auto& m : patch.modules)
        made.push_back (createNode (m, sampleRate, resolve));

    auto nodeFor = [&made] (int id) -> Node*
    {
        for (auto& n : made) if (n->id == id) return n.get();
        return nullptr;
    };

    std::vector<int> ids;
    for (auto& n : made) ids.push_back (n->id);

    std::vector<int> indegree (made.size(), 0);
    for (auto& c : patch.cables)
        for (size_t i = 0; i < made.size(); ++i)
            if (ids[i] == c.toModule && nodeFor (c.fromModule) != nullptr)
                ++indegree[i];

    std::vector<bool> placed (made.size(), false);
    const size_t total = made.size();
    while (nodes.size() < total)
    {
        bool progress = false;
        for (size_t i = 0; i < total; ++i)
        {
            if (placed[i] || indegree[i] > 0) continue;
            placed[i] = true;
            progress = true;
            for (auto& c : patch.cables)
                if (c.fromModule == ids[i])
                    for (size_t j = 0; j < total; ++j)
                        if (ids[j] == c.toModule) --indegree[j];
            nodes.push_back (std::move (made[i]));
        }
        if (! progress)
        {
            // A feedback loop: place the rest in patch order and break the cycle
            for (size_t i = 0; i < total; ++i)
                if (! placed[i]) { placed[i] = true; nodes.push_back (std::move (made[i])); }
        }
    }

    for (auto& n : nodes)
    {
        if (n->type == ModuleType::Input) input = n.get();
        if (n->type == ModuleType::Output) output = n.get();
        if (n->isInstrument()) instrument = true;
    }
    for (auto& c : patch.cables)
    {
        auto* from = find (c.fromModule);
        auto* to = find (c.toModule);
        if (from != nullptr && to != nullptr)
            links.push_back ({ from, c.fromPort, to, c.toPort });
    }
}

Node* Graph::find (int moduleId)
{
    for (auto& n : nodes) if (n->id == moduleId) return n.get();
    return nullptr;
}

void Graph::noteOn (int pitch, float velocity, double gate) { for (auto& n : nodes) n->noteOn (pitch, velocity, gate); }
void Graph::noteOff (int pitch)                             { for (auto& n : nodes) n->noteOff (pitch); }
void Graph::allNotesOff()                                   { for (auto& n : nodes) n->allNotesOff(); }

void Graph::process (const float* inL, const float* inR, float* outL, float* outR, int n, double bpm)
{
    int done = 0;
    while (done < n)
    {
        const int len = juce::jmin (subBlock, n - done);
        processSubBlock (inL + done, inR + done, outL + done, outR + done, len, bpm);
        done += len;
    }
}

void Graph::processSubBlock (const float* inL, const float* inR, float* outL, float* outR, int n, double bpm)
{
    const float ext = externalMod.load();
    for (auto& node : nodes)
    {
        // Gather audio from cables into this node's input, and modulation into its knobs
        node->mod.fill (0.0f);
        if (ext > 0.0001f && node->isInstrument())
            node->mod[(size_t) (node->type == ModuleType::Synth ? 3 : 0)] += ext * 0.6f;
        if (node.get() == input)
        {
            std::copy (inL, inL + n, node->bufL.begin());
            std::copy (inR, inR + n, node->bufR.begin());
        }
        else
        {
            node->bufL.fill (0.0f);
            node->bufR.fill (0.0f);
        }
        for (auto& link : links)
        {
            if (link.to != node.get()) continue;
            if (link.toPort == Port::audioIn)
            {
                for (int i = 0; i < n; ++i)
                {
                    node->bufL[(size_t) i] += link.from->bufL[(size_t) i];
                    node->bufR[(size_t) i] += link.from->bufR[(size_t) i];
                }
            }
            else if (Port::isModIn (link.toPort))
            {
                const int kn = link.toPort - Port::modInBase;
                if (kn < maxKnobs)
                    node->mod[(size_t) kn] += link.from->modValue();
            }
        }
        node->process (node->bufL.data(), node->bufR.data(), n, bpm);
    }

    if (output != nullptr)
    {
        std::copy (output->bufL.begin(), output->bufL.begin() + n, outL);
        std::copy (output->bufR.begin(), output->bufR.begin() + n, outR);
    }
    else
    {
        std::fill (outL, outL + n, 0.0f);
        std::fill (outR, outR + n, 0.0f);
    }
}
