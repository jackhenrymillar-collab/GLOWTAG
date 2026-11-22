#include "Graph.h"
#include "PluginHost.h"
#include "Samples.h"

using namespace analog;

Node::Node (const Module& m, double sampleRate) : id (m.id), type (m.type), info (moduleInfo (m.type)), sr (sampleRate)
{
    for (size_t i = 0; i < (size_t) maxParams; ++i)
        knob[i] = i < m.knobValues.size() ? m.knobValues[i] : ((int) i < info.numParams() ? info.defaultValue ((int) i) : 0.0f);
}

//==============================================================================
namespace
{
    struct PassNode : public Node
    {
        using Node::Node;
        void process (float*, float*, int, double) override {}
    };

    // A hosted AU / VST3 plugin, run in sub-blocks like everything else. Instruments take the
    // patch's notes as MIDI; effects take the audio input. Mix blends dry and wet, Gain trims the output.
    struct PluginNode : public Node
    {
        struct PlayHead : public juce::AudioPlayHead
        {
            std::atomic<double> bpm { 120.0 };
            juce::Optional<PositionInfo> getPosition() const override
            {
                PositionInfo p;
                p.setBpm (bpm.load());
                p.setIsPlaying (true);
                p.setTimeSignature (TimeSignature { 4, 4 });
                return p;
            }
        };

        std::unique_ptr<juce::AudioPluginInstance> plugin;
        PlayHead playHead;
        bool synth = false;
        juce::AudioBuffer<float> buf;
        juce::MidiBuffer midi;
        struct Pending { int pitch; double samplesLeft; };
        std::vector<Pending> pendingOffs;
        std::array<float, subBlock> dryL {}, dryR {};

        PluginNode (const Module& m, double sampleRate) : Node (m, sampleRate)
        {
            juce::String error;
            plugin = PluginHost::get().create (m.pluginId, sr, subBlock, error);
            if (plugin == nullptr)
                return;
            const auto desc = plugin->getPluginDescription();
            synth = plugin->acceptsMidi() && (desc.isInstrument || plugin->getTotalNumInputChannels() == 0);
            plugin->enableAllBuses();
            plugin->setPlayHead (&playHead);
            plugin->setRateAndBufferSizeDetails (sr, subBlock);
            plugin->prepareToPlay (sr, subBlock);
            if (m.pluginState.getSize() > 0)
                plugin->setStateInformation (m.pluginState.getData(), (int) m.pluginState.getSize());
            buf.setSize (juce::jmax (2, plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels()), subBlock);
            pendingOffs.reserve (64);
        }

        ~PluginNode() override
        {
            if (plugin != nullptr)
            {
                plugin->setPlayHead (nullptr);
                plugin->releaseResources();
            }
        }

        bool isInstrument() const override { return synth; }
        juce::AudioProcessor* hostedPlugin() override { return plugin.get(); }

        void captureState (Module& m) override
        {
            if (plugin != nullptr)
            {
                juce::MemoryBlock state;
                plugin->getStateInformation (state);
                if (state.getSize() > 0)
                    m.pluginState = state;
            }
        }

        void noteOn (int pitch, float velocity, double gateSamples) override
        {
            midi.addEvent (juce::MidiMessage::noteOn (1, pitch, velocity), 0);
            if (gateSamples > 0.0)
                pendingOffs.push_back ({ pitch, gateSamples });
        }

        void noteOff (int pitch) override
        {
            midi.addEvent (juce::MidiMessage::noteOff (1, pitch), 0);
            pendingOffs.erase (std::remove_if (pendingOffs.begin(), pendingOffs.end(), [pitch] (const Pending& p) { return p.pitch == pitch; }),
                               pendingOffs.end());
        }

        void allNotesOff() override
        {
            midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
            pendingOffs.clear();
        }

        void process (float* l, float* r, int n, double bpm) override
        {
            if (plugin == nullptr)
                return;    // no plugin: audio passes straight through
            playHead.bpm = bpm;

            // Gated notes end inside this sub-block
            for (auto it = pendingOffs.begin(); it != pendingOffs.end();)
            {
                it->samplesLeft -= n;
                if (it->samplesLeft <= 0.0)
                {
                    midi.addEvent (juce::MidiMessage::noteOff (1, it->pitch), juce::jlimit (0, n - 1, n - 1 + (int) it->samplesLeft));
                    it = pendingOffs.erase (it);
                }
                else
                    ++it;
            }

            std::copy (l, l + n, dryL.begin());
            std::copy (r, r + n, dryR.begin());
            buf.clear();
            if (buf.getNumChannels() > 0) buf.copyFrom (0, 0, l, n);
            if (buf.getNumChannels() > 1) buf.copyFrom (1, 0, r, n);

            juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), buf.getNumChannels(), n);
            if (plugin->isSuspended())
                view.clear();
            else
                plugin->processBlock (view, midi);
            midi.clear();

            const float mix = synth ? 1.0f : k (0);
            const float gain = k (1) / 0.7f;
            const float* wl = buf.getReadPointer (0);
            const float* wr = buf.getNumChannels() > 1 ? buf.getReadPointer (1) : wl;
            float peak = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                l[i] = dryL[(size_t) i] * (1.0f - mix) + wl[i] * mix * gain;
                r[i] = dryR[(size_t) i] * (1.0f - mix) + wr[i] * mix * gain;
                peak = juce::jmax (peak, std::abs (l[i]));
            }
            display = peak;
        }
    };
}

//==============================================================================
// Utilities

void UtilityNode::clearInputs()
{
    for (auto& b : inL) b.fill (0.0f);
    for (auto& b : inR) b.fill (0.0f);
    inMod.fill (0.0f);
}

void UtilityNode::process (float* l, float* r, int n, double)
{
    if (modMode)
    {
        modOut = control (n);
        std::fill (l, l + n, 0.0f);
        std::fill (r, r + n, 0.0f);
        display = std::abs (modOut);
        return;
    }
    audio (l, r, n);
    float peak = 0.0f;
    for (int i = 0; i < n; ++i)
        peak = juce::jmax (peak, std::abs (l[i]));
    display = peak;
}

namespace
{
    // Level: turns a signal down. Patch an LFO into its knob and it is a tremolo, or a VCA.
    struct AttenuatorNode : public UtilityNode
    {
        using UtilityNode::UtilityNode;
        float last = -1.0f;
        void audio (float* l, float* r, int n) override
        {
            const float target = k (0);
            if (last < 0.0f) last = target;
            const float step = (target - last) / (float) n;
            for (int i = 0; i < n; ++i)
            {
                last += step;
                l[i] = inL[0][(size_t) i] * last;
                r[i] = inR[0][(size_t) i] * last;
            }
        }
        float control (int) override { return inMod[0] * k (0); }
    };

    // Four inputs, each with a level, summed
    struct MixerNode : public UtilityNode
    {
        using UtilityNode::UtilityNode;
        std::array<float, Port::maxSigIns> last { -1.0f, -1.0f, -1.0f, -1.0f };
        void audio (float* l, float* r, int n) override
        {
            std::fill (l, l + n, 0.0f);
            std::fill (r, r + n, 0.0f);
            for (size_t in = 0; in < (size_t) Port::maxSigIns; ++in)
            {
                const float target = k ((int) in);
                if (last[in] < 0.0f) last[in] = target;
                const float step = (target - last[in]) / (float) n;
                for (int i = 0; i < n; ++i)
                {
                    last[in] += step;
                    l[i] += inL[in][(size_t) i] * last[in];
                    r[i] += inR[in][(size_t) i] * last[in];
                }
            }
        }
        float control (int) override
        {
            float sum = 0.0f;
            for (size_t in = 0; in < (size_t) Port::maxSigIns; ++in)
                sum += inMod[in] * k ((int) in);
            return sum;
        }
    };

    // Flips the signal: polarity for sound, direction for modulation
    struct InverterNode : public UtilityNode
    {
        using UtilityNode::UtilityNode;
        void audio (float* l, float* r, int n) override
        {
            for (int i = 0; i < n; ++i)
            {
                l[i] = -inL[0][(size_t) i];
                r[i] = -inR[0][(size_t) i];
            }
        }
        float control (int) override { return -inMod[0]; }
    };

    // Blends input A into input B
    struct CrossfadeNode : public UtilityNode
    {
        using UtilityNode::UtilityNode;
        float last = -1.0f;
        void audio (float* l, float* r, int n) override
        {
            const float target = k (0);
            if (last < 0.0f) last = target;
            const float step = (target - last) / (float) n;
            for (int i = 0; i < n; ++i)
            {
                last += step;
                l[i] = inL[0][(size_t) i] * (1.0f - last) + inL[1][(size_t) i] * last;
                r[i] = inR[0][(size_t) i] * (1.0f - last) + inR[1][(size_t) i] * last;
            }
        }
        float control (int) override { const float m = k (0); return inMod[0] * (1.0f - m) + inMod[1] * m; }
    };

    // Lag: the output follows the input slowly. Smooths modulation; on sound it rounds off the top.
    struct SlewNode : public UtilityNode
    {
        using UtilityNode::UtilityNode;
        float yl = 0.0f, yr = 0.0f, ym = 0.0f;
        double seconds() const { return 0.0005 * std::pow (4000.0, (double) k (0)); }      // 0.5 ms .. 2 s
        void audio (float* l, float* r, int n) override
        {
            const float a = (float) (1.0 - std::exp (-1.0 / (seconds() * sr)));
            for (int i = 0; i < n; ++i)
            {
                yl += (inL[0][(size_t) i] - yl) * a;
                yr += (inR[0][(size_t) i] - yr) * a;
                l[i] = yl;
                r[i] = yr;
            }
        }
        float control (int n) override
        {
            ym += (inMod[0] - ym) * (float) (1.0 - std::exp (-(double) n / (seconds() * sr)));
            return ym;
        }
    };
}

std::unique_ptr<Node> createNode (const Module& m, double sr, const Graph::SampleResolver& resolve)
{
    if (auto device = createDeviceNode (m, sr, resolve))
        return device;
    switch (m.type)
    {
        case ModuleType::Plugin:     return std::make_unique<PluginNode> (m, sr);
        case ModuleType::Attenuator: return std::make_unique<AttenuatorNode> (m, sr);
        case ModuleType::Mixer:      return std::make_unique<MixerNode> (m, sr);
        case ModuleType::Inverter:   return std::make_unique<InverterNode> (m, sr);
        case ModuleType::Crossfade:  return std::make_unique<CrossfadeNode> (m, sr);
        case ModuleType::Slew:       return std::make_unique<SlewNode> (m, sr);
        case ModuleType::Sampler: case ModuleType::Synth: case ModuleType::Drums: case ModuleType::Filter:
        case ModuleType::Drive: case ModuleType::Delay: case ModuleType::Reverb: case ModuleType::EQ:
        case ModuleType::Compressor: case ModuleType::LFO:        // made by createDeviceNode
        case ModuleType::GasTube: case ModuleType::CrateDigger: case ModuleType::StringWall:
        case ModuleType::GhostTag: case ModuleType::Crowd: case ModuleType::Transformer:
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
        n->host = &host;
        if (n->type == ModuleType::Input) input = n.get();
        if (n->type == ModuleType::Output) output = n.get();
        if (auto* u = n->asUtility())
            u->modMode = patch.kindOf (u->id, Port::sigOut) == Signal::Mod;
    }
    for (auto& c : patch.cables)
    {
        auto* from = find (c.fromModule);
        auto* to = find (c.toModule);
        if (from == nullptr || to == nullptr)
            continue;
        const auto kind = patch.kindOf (c);
        if (kind == Signal::Notes)
        {
            // Notes all come from the track input, so a notes cable just switches its instrument on
            if (c.toPort == Port::notesIn)
                to->takesNotes = true;
            continue;
        }
        links.push_back ({ from, c.fromPort, to, c.toPort, kind == Signal::Mod });
    }
    for (auto& n : nodes)
        if (n->isInstrument() && n->takesNotes) instrument = true;
}

void Graph::capturePluginState (Patch& patch)
{
    for (auto& n : nodes)
        if (n->hostedPlugin() != nullptr)
            if (auto* m = patch.find (n->id))
                n->captureState (*m);
}

Node* Graph::find (int moduleId)
{
    for (auto& n : nodes) if (n->id == moduleId) return n.get();
    return nullptr;
}

void Graph::noteOn (int pitch, float velocity, double gate)
{
    for (auto& n : nodes)
    {
        if (n->takesNotes) n->noteOn (pitch, velocity, gate);
        n->anyNoteOn();
    }
}
void Graph::noteOff (int pitch)                             { for (auto& n : nodes) if (n->takesNotes) n->noteOff (pitch); }
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
        auto* utility = node->asUtility();
        if (utility != nullptr)
            utility->clearInputs();
        for (auto& link : links)
        {
            if (link.to != node.get()) continue;
            if (Port::isSigIn (link.toPort) && utility != nullptr)
            {
                const auto in = (size_t) juce::jlimit (0, Port::maxSigIns - 1, link.toPort - Port::sigInBase);
                if (link.mod)
                    utility->inMod[in] += link.from->modValue();
                else
                    for (int i = 0; i < n; ++i)
                    {
                        utility->inL[in][(size_t) i] += link.from->bufL[(size_t) i];
                        utility->inR[in][(size_t) i] += link.from->bufR[(size_t) i];
                    }
            }
            else if (link.toPort == Port::audioIn)
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
        float peakIn = 0.0f;
        for (int i = 0; i < n; ++i)
            peakIn = juce::jmax (peakIn, std::abs (node->bufL[(size_t) i]), std::abs (node->bufR[(size_t) i]));
        const float was = node->inputLevel.load (std::memory_order_relaxed);
        node->inputLevel.store (peakIn > was ? peakIn : was * 0.995f, std::memory_order_relaxed);

        node->process (node->bufL.data(), node->bufR.data(), n, bpm);
        node->scope.push (node->bufL.data(), node->bufR.data(), n);
    }
    host.bpm = bpm;
    host.beat += n * host.beatsPerSample;
    host.blockPos += n;

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
