// Offline checks for the engine and patch model: run with ./GlowtagTests
#include <juce_audio_basics/juce_audio_basics.h>
#include "Engine/Project.h"
#include "Engine/AudioEngine.h"
#include "Engine/Patch.h"
#include "Engine/Graph.h"
#include "Engine/Samples.h"
#include "Engine/Sequencer.h"
#include "Engine/DeviceParams.h"
#include "UI/DeviceWallModel.h"
#include <iostream>

namespace
{
    int failures = 0;
    void check (bool ok, const char* what)
    {
        std::cout << (ok ? "  pass  " : "  FAIL  ") << what << "\n";
        if (! ok) ++failures;
    }

    constexpr double sr = 48000.0;
    constexpr int block = 512;

    float render (AudioEngine& e, double beats, double bpm)
    {
        juce::AudioBuffer<float> buf (2, block);
        const int blocks = (int) std::ceil (beats * 60.0 / bpm * sr / block);
        float peak = 0.0f;
        for (int i = 0; i < blocks; ++i)
        {
            juce::AudioSourceChannelInfo info (&buf, 0, block);
            e.getNextAudioBlock (info);
            peak = juce::jmax (peak, buf.getMagnitude (0, block));
        }
        return peak;
    }
}

int main()
{
    std::cout << "Transport and clips\n";
    {
        Project p;
        AudioEngine e (p);
        e.prepareToPlay (block, sr);
        e.looping = false;
        check (render (e, 1.0, p.bpm) < 1e-6f, "silent while stopped");
        e.play();
        const float peak = render (e, 8.0, p.bpm);
        check (peak > 0.05f, "arrangement makes sound when playing");
        check (std::abs (e.getPosition() - 8.0) < 0.1, "position advances by the beats played");
        e.stop();
        check (std::abs (e.getPosition() - 0.0) < 1e-9, "stop returns to where play started");
        e.setPosition (12.0);
        e.play();
        render (e, 1.0, p.bpm);
        e.stop();
        check (std::abs (e.getPosition() - 12.0) < 1e-9, "play from a set position, stop returns there");
        e.stop();
        check (e.getPosition() == 0.0, "second stop goes to the start");
    }

    std::cout << "Loop region\n";
    {
        Project p;
        AudioEngine e (p);
        e.prepareToPlay (block, sr);
        e.looping = true;
        e.loopStart = 4.0;
        e.loopEnd = 8.0;
        e.setPosition (4.0);
        e.play();
        render (e, 10.0, p.bpm);
        const double pos = e.getPosition();
        check (pos >= 4.0 && pos < 8.0, "playhead stays inside the loop");
        check (std::abs (pos - 6.0) < 0.1, "loop wraps to the right place");
    }

    std::cout << "Mute and an empty project\n";
    {
        Project p;
        for (auto& t : p.tracks) t->muted = true;
        AudioEngine e (p);
        e.prepareToPlay (block, sr);
        e.play();
        check (render (e, 4.0, p.bpm) < 1e-6f, "all tracks muted is silent");
    }

    std::cout << "Audio clips\n";
    {
        Project p;
        for (auto& t : p.tracks) t->clips.clear();
        auto data = std::make_shared<AudioData>();
        data->sampleRate = sr;
        data->buffer.setSize (2, (int) sr);
        for (int i = 0; i < (int) sr; ++i)
            data->buffer.setSample (0, i, 0.5f * std::sin (i * 0.05f)), data->buffer.setSample (1, i, 0.5f * std::sin (i * 0.05f));
        Clip c;
        c.kind = ClipKind::Audio;
        c.audio = data;
        c.start = 0.0;
        c.length = 2.0;
        p.tracks[0]->clips.push_back (c);
        // The demo track plays a drum rack, so its own sound is not patched in until audio arrives
        // (the arranger does this when a file is dropped on the lane)
        check (p.tracks[0]->patch.patchTrackSound(), "an audio clip on an instrument track patches the track's sound in");
        check (! p.tracks[0]->patch.patchTrackSound(), "and only once");
        AudioEngine e (p);
        e.prepareToPlay (block, sr);
        e.play();
        check (render (e, 1.0, p.bpm) > 0.1f, "audio clip plays");
        e.setPosition (3.0);
        check (render (e, 0.5, p.bpm) < 1e-4f, "silence after the audio clip ends");   // below -80 dB: the effect chain may still be ringing down
    }

    std::cout << "Live MIDI and recording\n";
    {
        Project p;
        for (auto& t : p.tracks) t->clips.clear();
        AudioEngine e (p);
        e.prepareToPlay (block, sr);
        e.liveTrack = 6; // Keys (synth)
        e.recordArmed = true;
        e.play();
        render (e, 0.5, p.bpm);
        const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
        e.midiInput.addMessageToQueue (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100).withTimeStamp (now));
        const float peak = render (e, 0.5, p.bpm);
        e.midiInput.addMessageToQueue (juce::MidiMessage::noteOff (1, 60).withTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001));
        render (e, 0.5, p.bpm);
        check (peak > 0.01f, "live note sounds on the selected track");
        AudioEngine::RecordedEvent ev[8];
        const int n = e.readRecorded (ev, 8);
        check (n == 2 && ev[0].noteOn && ! ev[1].noteOn && ev[0].pitch == 60, "note on and off are recorded");
        check (n == 2 && ev[1].beat > ev[0].beat, "recorded note has a length");
    }

    std::cout << "Patch auto-wiring\n";
    {
        Patch p;
        const int f = p.add (ModuleType::Filter);
        const int d = p.add (ModuleType::Delay);
        check (p.cableInto (Patch::outputId, Port::audioIn) >= 0 && p.cables[(size_t) p.cableInto (Patch::outputId, Port::audioIn)].fromModule == d,
               "new effect goes just before the output");
        const int l = p.add (ModuleType::LFO);
        check (p.cableInto (d, Port::modIn (0)) >= 0 && p.cables[(size_t) p.cableInto (d, Port::modIn (0))].fromModule == l,
               "LFO patches into the last module's first knob");
        p.remove (d);
        const int into = p.cableInto (Patch::outputId, Port::audioIn);
        check (into >= 0 && p.cables[(size_t) into].fromModule == f, "removing a module joins its neighbours");
        check (p.connect (Patch::inputId, Port::audioOut, Patch::outputId, Port::audioIn), "manual re-wire works");
        check (! p.connect (l, Port::modOut, Patch::outputId, Port::audioIn), "modulation can't go into an audio input");
    }

    std::cout << "Graph: virtual analog synth and effects\n";
    {
        auto resolve = [] (const juce::String& n) { return Samples::find (n); };
        auto renderGraph = [] (Graph& g, int blocks, double bpm = 120.0)
        {
            std::vector<float> zl (block, 0.0f), zr (block, 0.0f), ol (block), orr (block);
            float peak = 0.0f;
            for (int b = 0; b < blocks; ++b)
            {
                g.process (zl.data(), zr.data(), ol.data(), orr.data(), block, bpm);
                for (float v : ol) peak = juce::jmax (peak, std::abs (v));
            }
            return peak;
        };

        Patch synthPatch;
        const int synthId = synthPatch.add (ModuleType::Synth);
        Graph synth (synthPatch, sr, resolve);
        check (synth.hasInstrument(), "synth patch has an instrument");
        check (renderGraph (synth, 4) < 1e-6f, "synth is silent with no notes");
        synth.noteOn (48, 0.9f, -1.0);
        const float held = renderGraph (synth, 20);
        check (held > 0.05f && held <= 1.0f, "held synth note sounds and stays in range");
        synth.noteOff (48);
        renderGraph (synth, 200);
        check (renderGraph (synth, 4) < 1e-4f, "synth note releases to silence");

        // Low cutoff should be quieter than open cutoff
        synth.find (synthId)->knob[3] = 0.0f;
        synth.noteOn (48, 0.9f, -1.0);
        const float dark = renderGraph (synth, 20);
        synth.allNotesOff(); renderGraph (synth, 200);
        synth.find (synthId)->knob[3] = 1.0f;
        synth.noteOn (48, 0.9f, -1.0);
        const float bright = renderGraph (synth, 20);
        check (dark < bright, "ladder filter: closed cutoff is quieter than open");

        Patch drumPatch;
        drumPatch.add (ModuleType::Drums);
        Graph drums (drumPatch, sr, resolve);
        drums.noteOn (36, 1.0f, 0.0);
        check (renderGraph (drums, 4) > 0.1f, "drum rack kick plays a built-in sample");

        Patch samplerPatch;
        const int sid = samplerPatch.add (ModuleType::Sampler);
        samplerPatch.find (sid)->sampleName = "Vox Ahh";
        Graph sampler (samplerPatch, sr, resolve);
        sampler.noteOn (60, 1.0f, -1.0);
        check (renderGraph (sampler, 10) > 0.05f, "sampler plays the vowel sample");

        // Effects chain keeps the signal bounded, LFO modulates
        Patch fx;
        fx.add (ModuleType::Drums);
        fx.add (ModuleType::Drive);
        fx.add (ModuleType::Delay);
        fx.add (ModuleType::Reverb);
        fx.add (ModuleType::EQ);
        fx.add (ModuleType::Compressor);
        const int lfoId = fx.add (ModuleType::LFO);
        Graph chain (fx, sr, resolve);
        chain.noteOn (38, 1.0f, 0.0);
        const float p = renderGraph (chain, 30);
        check (p > 0.01f && p < 4.0f, "full effect chain passes a bounded signal");
        check (std::abs (chain.find (lfoId)->display.load()) > 0.0f, "LFO is running");
        check (Samples::bank().size() >= 16, "sample bank has at least sixteen sounds");
    }


    std::cout << "Signal kinds: sound, notes and modulation\n";
    {
        auto resolve = [] (const juce::String& n) { return Samples::find (n); };
        // Feed a steady tone into the track input and return the peak of what comes out
        auto renderTone = [] (Graph& g, int blocks, float level = 0.5f)
        {
            std::vector<float> il (block), ir (block), ol (block), orr (block);
            float peak = 0.0f;
            int t = 0;
            for (int b = 0; b < blocks; ++b)
            {
                for (int i = 0; i < block; ++i, ++t)
                    il[(size_t) i] = ir[(size_t) i] = level * std::sin ((float) t * 0.05f);
                g.process (il.data(), ir.data(), ol.data(), orr.data(), block, 120.0);
                if (b > 0)
                    for (float v : ol) peak = juce::jmax (peak, std::abs (v));
            }
            return peak;
        };
        auto cableFrom = [] (const Patch& p, int module, int port, int toModule, int toPort)
        {
            for (auto& c : p.cables)
                if (c.fromModule == module && c.fromPort == port && c.toModule == toModule && c.toPort == toPort)
                    return true;
            return false;
        };

        Patch p;
        const int synth = p.add (ModuleType::Synth);
        check (cableFrom (p, Patch::inputId, Port::notesOut, synth, Port::notesIn), "a new instrument takes its notes from the track input");
        check (cableFrom (p, synth, Port::audioOut, Patch::outputId, Port::audioIn), "and starts the audio chain");
        check (! moduleInfo (ModuleType::Synth).hasPort (Port::audioIn), "an instrument has no audio input");
        const int filter = p.add (ModuleType::Filter);
        const int lfo = p.add (ModuleType::LFO);

        check (! p.canConnect (Patch::inputId, Port::audioOut, synth, Port::notesIn), "sound can't go into an instrument's notes input");
        check (! p.canConnect (lfo, Port::modOut, synth, Port::notesIn), "nor can modulation");
        check (! p.canConnect (filter, Port::audioOut, synth, Port::audioIn), "an instrument takes no sound at all");
        check (! p.canConnect (Patch::inputId, Port::notesOut, filter, Port::audioIn), "notes can't go into an effect");
        check (! p.canConnect (Patch::inputId, Port::notesOut, Patch::outputId, Port::audioIn), "or into the output");
        check (! p.canConnect (Patch::inputId, Port::notesOut, filter, Port::modIn (0)), "or into a knob");
        check (! p.canConnect (synth, Port::audioOut, filter, Port::modIn (0)), "sound can't go into a knob");
        check (! p.canConnect (lfo, Port::modOut, filter, Port::audioIn), "modulation can't go into a sound input");
        check (! p.canConnect (filter, Port::audioIn, synth, Port::audioOut), "a cable runs from an output to an input");
        check (! p.canConnect (synth, Port::modOut, filter, Port::modIn (0)), "a jack the module doesn't have can't be used");
        check (p.canConnect (lfo, Port::modOut, synth, Port::modIn (3)) && p.canConnect (synth, Port::audioOut, filter, Port::audioIn),
               "matching jacks connect");
        const auto before = p.cables.size();
        check (! p.connect (Patch::inputId, Port::notesOut, filter, Port::audioIn) && p.cables.size() == before, "a refused cable changes nothing");

        // Notes only reach an instrument that is patched to the notes output
        {
            Graph g (p, sr, resolve);
            g.noteOn (48, 0.9f, -1.0);
            check (renderTone (g, 12, 0.0f) > 0.02f, "a patched instrument plays its notes");
        }
        {
            Patch loose = p;
            loose.disconnect ((size_t) loose.cableInto (synth, Port::notesIn));
            Graph g (loose, sr, resolve);
            check (! g.hasInstrument(), "with the notes cable pulled the track has no instrument");
            g.noteOn (48, 0.9f, -1.0);
            check (renderTone (g, 12, 0.0f) < 1e-6f, "and the instrument stays silent");
        }

        // A second instrument is layered with the first through a Mixer
        Patch layered;
        const int s1 = layered.add (ModuleType::Synth);
        const int s2 = layered.add (ModuleType::Sampler);
        int mixer = -1;
        for (auto& m : layered.modules) if (m.type == ModuleType::Mixer) mixer = m.id;
        check (mixer >= 0 && cableFrom (layered, s1, Port::audioOut, mixer, Port::sigIn (0)) && cableFrom (layered, s2, Port::audioOut, mixer, Port::sigIn (1))
               && cableFrom (layered, mixer, Port::sigOut, Patch::outputId, Port::audioIn), "two instruments are mixed on the utility row");
        check (layered.find (mixer)->position.y == Patch::utilityTop && layered.kindOf (mixer, Port::sigOut) == Signal::Audio,
               "the mixer sits on the utility row and is working on sound");
        const int s3 = layered.add (ModuleType::Drums);
        check (cableFrom (layered, s3, Port::audioOut, mixer, Port::sigIn (2)), "a third instrument takes the mixer's next input");
        layered.remove (mixer);
        check (cableFrom (layered, s1, Port::audioOut, Patch::outputId, Port::audioIn), "removing a utility joins its first input to what it fed");

        // The track's own sound beside an instrument
        Patch both;
        const int drum = both.add (ModuleType::Drums);
        check (both.patchTrackSound(), "the track's own sound can be patched in beside an instrument");
        {
            Graph g (both, sr, resolve);
            check (renderTone (g, 6) > 0.3f, "and is then heard");
            juce::ignoreUnused (drum);
        }
    }

    std::cout << "Utilities: attenuator, mixer, inverter, crossfade, slew\n";
    {
        auto resolve = [] (const juce::String& n) { return Samples::find (n); };
        auto renderTone = [] (Graph& g, int blocks)
        {
            std::vector<float> il (block), ir (block), ol (block), orr (block);
            float peak = 0.0f;
            int t = 0;
            for (int b = 0; b < blocks; ++b)
            {
                for (int i = 0; i < block; ++i, ++t)
                    il[(size_t) i] = ir[(size_t) i] = 0.5f * std::sin ((float) t * 0.05f);
                g.process (il.data(), ir.data(), ol.data(), orr.data(), block, 120.0);
                if (b > 0)
                    for (float v : ol) peak = juce::jmax (peak, std::abs (v));
            }
            return peak;
        };
        const int in = Patch::inputId, out = Patch::outputId;

        Patch p;
        const int att = p.add (ModuleType::Attenuator);
        check (p.find (att)->position.y == Patch::utilityTop && p.cables.size() == 1, "a utility lands on the lower row, unpatched");
        check (p.kindOf (att, Port::sigIn (0)) == Signal::Any, "a loose utility takes sound or modulation");
        check (! p.canConnect (in, Port::notesOut, att, Port::sigIn (0)), "but never notes");
        check (p.connect (in, Port::audioOut, att, Port::sigIn (0)) && p.connect (att, Port::sigOut, out, Port::audioIn), "it patches into the sound");
        check (p.kindOf (att, Port::sigOut) == Signal::Audio, "and is then working on sound");
        const int lfo = p.add (ModuleType::LFO);
        check (! p.canConnect (att, Port::sigOut, lfo, Port::modIn (0)), "so its output can't go into a knob");
        {
            Graph g (p, sr, resolve);
            const float full = renderTone (g, 6);
            g.find (att)->knob[0] = 0.5f;
            const float half = renderTone (g, 6);
            check (full > 0.45f && full < 0.55f && half > 0.2f && half < 0.3f, "attenuator at half level halves the sound");
        }

        // Sound plus its inverse cancels in the mixer
        Patch cancel;
        const int inv = cancel.add (ModuleType::Inverter);
        const int mix = cancel.add (ModuleType::Mixer);
        cancel.connect (in, Port::audioOut, inv, Port::sigIn (0));
        cancel.connect (in, Port::audioOut, mix, Port::sigIn (0));
        cancel.connect (inv, Port::sigOut, mix, Port::sigIn (1));
        cancel.connect (mix, Port::sigOut, out, Port::audioIn);
        {
            Graph g (cancel, sr, resolve);
            check (renderTone (g, 6) < 1e-5f, "mixer: a sound and its inverse cancel");
            g.find (mix)->knob[1] = 0.0f;
            check (renderTone (g, 6) > 0.45f, "mixer: with the inverse turned down the sound is back");
        }

        // Crossfade between the sound and silence
        Patch fade;
        const int xf = fade.add (ModuleType::Crossfade);
        fade.connect (in, Port::audioOut, xf, Port::sigIn (0));
        fade.connect (xf, Port::sigOut, out, Port::audioIn);
        {
            Graph g (fade, sr, resolve);
            g.find (xf)->knob[0] = 0.0f;
            const float a = renderTone (g, 6);
            g.find (xf)->knob[0] = 1.0f;
            const float b = renderTone (g, 6);
            check (a > 0.45f && b < 1e-4f, "crossfade moves from input A to input B");
        }

        // Modulation through utilities: an LFO, inverted, into a knob
        Patch mod;
        const int filter = mod.add (ModuleType::Filter);
        const int l = mod.add (ModuleType::LFO);
        const int flip = mod.add (ModuleType::Inverter);
        const int lag = mod.add (ModuleType::Slew);
        mod.disconnect ((size_t) mod.cableInto (filter, Port::modIn (0)));
        check (mod.connect (l, Port::modOut, flip, Port::sigIn (0)) && mod.kindOf (flip, Port::sigOut) == Signal::Mod, "a utility fed by an LFO works on modulation");
        check (! mod.canConnect (flip, Port::sigOut, out, Port::audioIn), "so it can't go to a sound input");
        check (mod.connect (flip, Port::sigOut, lag, Port::sigIn (0)) && mod.kindOf (lag, Port::sigOut) == Signal::Mod, "a utility after it follows suit");
        check (! mod.canConnect (in, Port::audioOut, lag, Port::sigIn (0)), "sound can't join a utility that is working on modulation");
        check (mod.connect (flip, Port::sigOut, filter, Port::modIn (0)), "and reaches a knob");
        {
            Graph g (mod, sr, resolve);
            renderTone (g, 8);
            const float direct = g.find (l)->modValue(), flipped = g.find (flip)->modValue();
            check (std::abs (direct) > 0.001f && std::abs (direct + flipped) < 1e-6f, "inverter turns the LFO over");
            check (std::abs (g.find (lag)->modValue()) < std::abs (flipped) + 1e-6f, "slew trails behind its input");
        }

        // Saving keeps utilities and their cables
        Patch loaded;
        loaded.fromVar (juce::JSON::parse (juce::JSON::toString (cancel.toVar())));
        check (loaded.modules.size() == cancel.modules.size() && loaded.cables.size() == cancel.cables.size() && ! loaded.upgraded,
               "utilities survive saving and loading");
    }

    std::cout << "Older patches\n";
    {
        auto resolve = [] (const juce::String& n) { return Samples::find (n); };
        // Before note cables an instrument sat in the audio chain: Track In -> Synth -> Filter -> Out
        auto old = [] (const juce::String& cables, const juce::String& modules)
        {
            return juce::JSON::parse ("{ \"modules\": [ {\"id\":1,\"type\":0,\"x\":12,\"y\":14}, {\"id\":2,\"type\":1,\"x\":900,\"y\":14}, " + modules
                                      + " ], \"cables\": [ " + cables + " ], \"nextId\": 9 }");
        };
        Patch p;
        p.fromVar (old ("[1,1,3,0], [3,1,4,0], [4,1,2,0]", "{\"id\":3,\"type\":3,\"x\":120,\"y\":50}, {\"id\":4,\"type\":5,\"x\":480,\"y\":14}"));
        check (p.upgraded && p.cableInto (3, Port::notesIn) >= 0 && p.cableInto (3, Port::audioIn) < 0, "an old instrument gets a notes cable in place of its audio input");
        check (p.find (3)->position.y == Patch::top, "cards are put back on their row");
        check (p.cableInto (4, Port::audioIn) >= 0 && p.cables[(size_t) p.cableInto (4, Port::audioIn)].fromModule == 3, "and still feeds its effects");
        {
            Graph g (p, sr, resolve);
            g.noteOn (48, 0.9f, -1.0);
            std::vector<float> z (block, 0.0f), ol (block), orr (block);
            float peak = 0.0f;
            for (int b = 0; b < 12; ++b)
            {
                g.process (z.data(), z.data(), ol.data(), orr.data(), block, 120.0);
                for (float v : ol) peak = juce::jmax (peak, std::abs (v));
            }
            check (peak > 0.02f, "and still plays");
        }

        // Two instruments in a row were heard together: Track In -> Synth -> Sampler -> Out
        Patch two;
        two.fromVar (old ("[1,1,3,0], [3,1,5,0], [5,1,2,0]", "{\"id\":3,\"type\":3,\"x\":120,\"y\":14}, {\"id\":5,\"type\":2,\"x\":480,\"y\":14}"));
        int mixer = -1;
        for (auto& m : two.modules) if (m.type == ModuleType::Mixer) mixer = m.id;
        check (mixer >= 0 && two.cableInto (mixer, Port::sigIn (0)) >= 0 && two.cableInto (mixer, Port::sigIn (1)) >= 0
               && two.cableInto (Patch::outputId, Port::audioIn) >= 0 && two.cables[(size_t) two.cableInto (Patch::outputId, Port::audioIn)].fromModule == mixer,
               "old layered instruments are mixed");
        check (two.cableInto (3, Port::notesIn) >= 0 && two.cableInto (5, Port::notesIn) >= 0, "and both take notes");

        // A file with a cable the rules don't allow: it is dropped
        Patch bad;
        auto v = p.toVar();
        v["cables"].getArray()->add (juce::Array<juce::var> { 1, 4, 4, 0 });       // notes into the filter
        const auto good = p.cables.size();
        bad.fromVar (v);
        check (bad.cables.size() == good, "a cable that breaks the rules is dropped on loading");
    }

    std::cout << "Rolling sequencer rack\n";
    {
        SeqRack rack;
        check (rack.modules.size() == 4 && rack.find (SeqRack::clockId) && rack.find (SeqRack::outId), "new rack has clock, steps, quantise and out");
        const auto& oi = seqInfo (SeqType::Out);
        check (rack.cableInto (SeqRack::outId, oi.portIndex ("Gate")) >= 0 && rack.cableInto (SeqRack::outId, oi.portIndex ("Pitch")) >= 0,
               "out has pitch and gate patched by default");

        const int euclid = rack.add (SeqType::Euclid);
        const int gateFeed = rack.cableInto (SeqRack::outId, oi.portIndex ("Gate"));
        check (gateFeed >= 0 && rack.cables[(size_t) gateFeed].fromModule == euclid, "euclid takes over the gate when added");
        check (rack.cableInto (euclid, seqInfo (SeqType::Euclid).portIndex ("Clk")) >= 0, "euclid is clocked automatically");

        const int chance = rack.add (SeqType::Chance);
        const auto& chi = seqInfo (SeqType::Chance);
        check (rack.cables[(size_t) rack.cableInto (SeqRack::outId, oi.portIndex ("Gate"))].fromModule == chance
               && rack.cables[(size_t) rack.cableInto (chance, chi.portIndex ("Trig"))].fromModule == euclid,
               "chance slips in between euclid and out");

        rack.remove (chance);
        check (rack.cables[(size_t) rack.cableInto (SeqRack::outId, oi.portIndex ("Gate"))].fromModule == euclid, "removing chance heals the gate path");

        const int tide = rack.add (SeqType::Tide);
        check (rack.cableInto (euclid, seqInfo (SeqType::Euclid).portIndex ("Hits cv")) >= 0 || rack.cableInto (SeqRack::outId, oi.portIndex ("Mod")) >= 0,
               "a modulator lands on a free cv input");
        juce::ignoreUnused (tide);
        check (rack.indexOf (SeqRack::outId) == (int) rack.modules.size() - 1 && rack.indexOf (SeqRack::clockId) == 0, "clock stays first and out stays last");
    }

    std::cout << "Rolling sequencer engine\n";
    {
        // Count the clocks and notes over one bar at 120 bpm
        SeqRack rack;
        for (auto& m : rack.modules)
            if (m.type == SeqType::Steps) m.stepOn.fill (true);
        SeqRuntime rt (rack);
        int notesOn = 0, notesOff = 0;
        auto sink = [&] (int, float, bool on) { if (on) ++notesOn; else ++notesOff; };
        const double beatsPerTick = 32.0 * 120.0 / 60.0 / sr;
        const int ticks = (int) std::ceil (4.0 / beatsPerTick);
        for (int i = 0; i < ticks; ++i)
            rt.process ({ i * beatsPerTick, beatsPerTick, 43200.0, i == 0 }, sink);
        check (notesOn == 16, "sixteen steps play in one bar of sixteenths");
        check (notesOff >= 15, "gates close again");

        // Quantise pulls pitches onto D dorian
        SeqRack q;
        SeqRuntime qrt (q);
        SeqNode* quant = nullptr;
        for (auto& m : q.modules) if (m.type == SeqType::Quantise) quant = qrt.find (m.id);
        check (quant != nullptr, "quantiser found in runtime");
        const auto& qi = seqInfo (SeqType::Quantise);
        quant->in[(size_t) qi.portIndex ("In")] = 1.0f;     // C# is not in D dorian
        quant->plugged[(size_t) qi.portIndex ("Trig")] = false;
        quant->process ({ -1.0, beatsPerTick, 0.0, false });
        const float out = quant->out[(size_t) qi.portIndex ("Out")];
        check (std::abs (out - 0.0f) < 0.01f || std::abs (out - 2.0f) < 0.01f, "quantiser moves C# to a scale note");

        // Euclid 5/16 = x...x..x..x..x..
        SeqRack er;
        const int eid = er.add (SeqType::Euclid);
        SeqRuntime ert (er);
        auto* eu = ert.find (eid);
        const auto& ei = seqInfo (SeqType::Euclid);
        juce::String pattern;
        for (int step = 0; step < 16; ++step)
        {
            eu->in[(size_t) ei.portIndex ("Clk")] = 1.0f;
            eu->process ({ -1.0, beatsPerTick, 0.0, false });
            pattern << (eu->out[(size_t) ei.portIndex ("Gate")] > 0.5f ? "x" : ".");
            eu->in[(size_t) ei.portIndex ("Clk")] = 0.0f;
            for (int t = 0; t < 40; ++t) eu->process ({ -1.0, beatsPerTick, 0.0, false });
        }
        check (pattern == "x...x..x..x..x..", ("euclid 5/16 pattern is " + pattern).toRawUTF8());

        // Circadian daylight: noon is the peak, midnight the trough
        SeqRack cr;
        const int cid = cr.add (SeqType::Circadian);
        SeqRuntime crt (cr);
        auto* circ = crt.find (cid);
        const auto& ci = seqInfo (SeqType::Circadian);
        circ->knob[4] = 1.0f;   // Follow: Daylight
        circ->knob[3] = 1.0f;   // Shape: Sun
        circ->process ({ -1.0, beatsPerTick, 12.0 * 3600.0, false });
        const float noon = circ->out[(size_t) ci.portIndex ("Cv")], noonGate = circ->out[(size_t) ci.portIndex ("Noon")];
        circ->process ({ -1.0, beatsPerTick, 0.0, false });
        const float midnight = circ->out[(size_t) ci.portIndex ("Cv")];
        check (noon > 0.99f && midnight < 0.01f && noonGate > 0.5f, "circadian follows the time of day");

        // Turing with probability zero loops the same melody
        SeqRack tr;
        const int tid = tr.add (SeqType::Turing);
        SeqRuntime trt (tr);
        auto* tur = trt.find (tid);
        tur->knob[0] = 0.0f;
        tur->knob[1] = 1.0f;    // 16 steps
        const auto& ti = seqInfo (SeqType::Turing);
        std::vector<float> first, second;
        for (int step = 0; step < 32; ++step)
        {
            tur->in[(size_t) ti.portIndex ("Clk")] = 1.0f;
            tur->process ({ -1.0, beatsPerTick, 0.0, false });
            (step < 16 ? first : second).push_back (tur->out[(size_t) ti.portIndex ("Cv")]);
            tur->in[(size_t) ti.portIndex ("Clk")] = 0.0f;
            tur->process ({ -1.0, beatsPerTick, 0.0, false });
        }
        check (first == second, "locked turing machine repeats its loop");

        // Through the engine: a rolling synth track makes sound while a track in clip mode uses its clips
        Project p;
        AudioEngine e (p);
        e.prepareToPlay (block, sr);
        e.looping = false;
        int synthTrack = -1;
        for (size_t i = 0; i < p.tracks.size(); ++i)
            for (auto& m : p.tracks[i]->patch.modules)
                if (m.type == ModuleType::Synth && synthTrack < 0) synthTrack = (int) i;
        check (synthTrack >= 0, "project has a synth track");
        for (size_t i = 0; i < p.tracks.size(); ++i)
            if ((int) i != synthTrack) p.tracks[i]->muted = true;
        p.tracks[(size_t) synthTrack]->clips.clear();
        for (auto& send : p.tracks[(size_t) synthTrack]->send) send = 0.0f;   // keep the reverb return out of the measurement
        e.rebuildAll();
        e.play();
        check (render (e, 4.0, p.bpm) < 1e-6f, "synth track with no clips is silent in clip mode");
        p.tracks[(size_t) synthTrack]->rolling = true;
        check (render (e, 4.0, p.bpm) > 0.02f, "switching the track to SEQ makes the rack play the synth");
        p.tracks[(size_t) synthTrack]->rolling = false;
        render (e, 0.5, p.bpm);
        check (render (e, 2.0, p.bpm) < 0.02f, "switching back to clips stops the rack");
        check (e.liveSeqNode (synthTrack, SeqRack::clockId) != nullptr, "live rack is reachable for the UI");
    }


    std::cout << "Saving and loading\n";
    {
        Project p;
        p.bpm = 99.0;
        p.tracks[0]->send[0] = 0.4f;
        p.tracks[0]->volume = 0.5f;
        p.tracks[0]->rolling = true;
        const int euclidId = p.tracks[0]->seq.add (SeqType::Euclid);
        auto* euclid = p.tracks[0]->seq.find (euclidId);
        check (euclid != nullptr, "added module is found by its id");
        if (euclid != nullptr) euclid->knobs[0] = 0.75f;
        const int numModules = (int) p.tracks[0]->patch.modules.size();
        const int numCables = (int) p.tracks[0]->patch.cables.size();
        const int numClips = (int) p.tracks[1]->clips.size();
        const juce::File f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("glowtag-test.glowtag");
        check (p.saveTo (f), "project saves to disk");
        check (! p.dirty && p.file == f, "saving clears the dirty flag and remembers the file");

        Project q;
        q.makeEmpty();
        check (q.tracks.size() != p.tracks.size() || q.bpm.load() != 99.0, "empty project differs from the saved one");
        check (q.loadFrom (f), "project loads from disk");
        check (q.tracks.size() == p.tracks.size(), "loaded project has the same number of tracks");
        check (std::abs (q.bpm.load() - 99.0) < 1e-9, "tempo round-trips");
        check (q.tracks[0]->name == p.tracks[0]->name && q.tracks[0]->kind == p.tracks[0]->kind, "track name and kind round-trip");
        check (std::abs (q.tracks[0]->send[0].load() - 0.4f) < 1e-6f && std::abs (q.tracks[0]->volume.load() - 0.5f) < 1e-6f, "mixer settings round-trip");
        check (q.tracks[0]->rolling.load(), "SEQ switch round-trips");
        check ((int) q.tracks[0]->patch.modules.size() == numModules && (int) q.tracks[0]->patch.cables.size() == numCables, "device patch round-trips");
        auto* e2 = q.tracks[0]->seq.find (euclidId);
        check (e2 != nullptr && e2->type == SeqType::Euclid && std::abs (e2->knobs[0] - 0.75f) < 1e-6f, "sequencer rack round-trips");
        check (q.tracks[0]->seq.cables.size() == p.tracks[0]->seq.cables.size(), "rack cables round-trip");
        check ((int) q.tracks[1]->clips.size() == numClips, "clips round-trip");
        if (numClips > 0)
            check (q.tracks[1]->clips[0].notes.size() == p.tracks[1]->clips[0].notes.size(), "notes round-trip");
        check (q.returns[0] != nullptr && q.master != nullptr && q.master->kind == TrackKind::Master, "returns and master round-trip");
        check (q.returns[0]->patch.modules.size() == p.returns[0]->patch.modules.size(), "return bus patch round-trips");
        check (! q.fromVar (juce::var()), "garbage is rejected");
        f.deleteFile();
    }

    std::cout << "Mixer routing\n";
    {
        Project p;
        AudioEngine e (p);
        e.prepareToPlay (block, sr);
        e.looping = false;
        check (p.numSlots() == (int) p.tracks.size() + (int) p.buses.size() + 3, "slots cover tracks, buses, two returns and the master");
        check (&p.slot (p.masterSlot()) == p.master.get() && &p.slot (p.returnSlot (1)) == p.returns[1].get(), "slot lookup reaches the buses");
        check (p.isAudible (p.masterSlot()), "master is always audible");
        e.rebuildAll();
        e.play();
        const float full = render (e, 4.0, p.bpm);
        check (full > 0.05f, "demo plays through the master");
        check (p.master->meter.load() > 0.0f, "master meter moves");
        e.stop();
        p.master->volume = 0.0f;
        e.play();
        check (render (e, 2.0, p.bpm) < 1e-6f, "master fader at zero silences everything");
        e.stop();
        p.master->volume = 0.8f;

        // Only the return bus is heard when every track is muted but one track is feeding Return A
        for (auto& t : p.tracks) { t->muted = true; for (auto& s : t->send) s = 0.0f; }
        int src = -1;
        for (size_t i = 0; i < p.tracks.size(); ++i)
            if (! p.tracks[i]->clips.empty() && p.tracks[i]->clips[0].kind == ClipKind::Midi && src < 0) src = (int) i;
        check (src >= 0, "project has a MIDI track to send from");
        p.tracks[(size_t) src]->muted = false;
        p.tracks[(size_t) src]->volume = 0.0f;       // nothing direct to the master
        p.tracks[(size_t) src]->send[0] = 1.0f;
        p.returns[0]->muted = true;
        p.returns[1]->muted = true;   // Return B still carries a delay tail from the renders above
        e.play();
        check (render (e, 4.0, p.bpm) < 1e-6f, "sends are post-fader: fader down means no send");
        p.tracks[(size_t) src]->volume = 0.8f;
        render (e, 2.0, p.bpm);
        const float direct = render (e, 2.0, p.bpm);
        p.returns[0]->muted = false;
        render (e, 2.0, p.bpm);
        const float withReturn = render (e, 2.0, p.bpm);
        check (withReturn > direct * 1.05f, "unmuting Return A adds the reverb to the mix");
        e.stop();
        check (p.master->meter.load() == 0.0f && p.returns[0]->meter.load() == 0.0f, "stop clears the bus meters");
    }

    std::cout << "Patch routing and buses\n";
    {
        Project p;
        AudioEngine e (p);
        e.prepareToPlay (block, sr);
        e.looping = false;
        check (! p.buses.empty() && p.buses[0]->kind == TrackKind::Bus, "demo has a drum bus");
        const int drums = p.busSlot (0);
        check (p.outputSlot (0) == drums && p.outputSlot (drums) == p.masterSlot(), "kick feeds the drum bus, the bus feeds the master");
        check (p.outputSlot (p.masterSlot()) == -1, "the master feeds nothing");

        // Only the kick plays, through the bus: the bus mute and fader are in its path
        for (size_t i = 1; i < p.tracks.size(); ++i) p.tracks[i]->muted = true;
        for (auto& s : p.tracks[0]->send) s = 0.0f;
        e.rebuildAll();
        e.play();
        const float viaBus = render (e, 4.0, p.bpm);
        check (viaBus > 0.02f, "a track patched into a bus is heard");
        check (p.buses[0]->meter.load() > 0.0f, "bus meter moves");
        p.buses[0]->muted = true;
        check (render (e, 2.0, p.bpm) < 1e-6f, "muting the bus silences what feeds it");
        p.buses[0]->muted = false;
        check (p.setOutput (0, p.masterSlot()), "re-patching a track to the master is allowed");
        p.buses[0]->muted = true;
        render (e, 1.0, p.bpm);
        check (render (e, 3.0, p.bpm) > 0.02f, "patched straight to the master, the track no longer goes through the bus");
        p.buses[0]->muted = false;
        e.stop();

        // A second bus: chains are fine, loops are refused
        check (e.restructure ([&] { return p.addBus ("Glue") == 1; }), "a bus can be added while the engine runs");
        const int glue = p.busSlot (1);
        check (p.numSlots() == (int) p.tracks.size() + 2 + 3 && &p.slot (p.masterSlot()) == p.master.get(), "slots grow with the new bus");
        check (p.setOutput (0, drums) && p.setOutput (drums, glue), "a bus can feed another bus");
        check (! p.canRoute (glue, drums) && ! p.canRoute (glue, glue), "a loop of buses is refused");
        check (! p.canRoute (0, 1) && ! p.canRoute (0, p.returnSlot (0)), "only buses and the master take inputs");
        check (p.canRoute (p.returnSlot (0), glue), "a return can be patched into a bus");
        e.play();
        check (render (e, 4.0, p.bpm) > 0.02f, "track through two buses in a chain is heard");
        p.buses[1]->volume = 0.0f;
        check (render (e, 2.0, p.bpm) < 1e-6f, "the last bus in the chain controls the level");
        p.buses[1]->volume = 1.0f;
        e.stop();

        // Removing the middle bus sends its feeders on to where it was going
        e.restructure ([&] { p.removeBus (0); return true; });
        check (p.buses.size() == 1 && p.buses[0]->name == "Glue", "bus removed");
        check (p.outputSlot (0) == p.busSlot (0) && p.outputSlot (p.busSlot (0)) == p.masterSlot(), "feeders of a removed bus follow its output");
        e.play();
        check (render (e, 4.0, p.bpm) > 0.02f, "still heard after the bus was removed");
        e.stop();
    }

    std::cout << "Console channel\n";
    {
        Project p;
        AudioEngine e (p);
        e.prepareToPlay (block, sr);
        e.looping = false;
        for (size_t i = 1; i < p.tracks.size(); ++i) p.tracks[i]->muted = true;
        for (auto& s : p.tracks[0]->send) s = 0.0f;
        p.master->patch = Patch();          // no master compressor in the measurement
        e.rebuildAll();
        auto measure = [&]
        {
            e.setPosition (0.0);
            e.play();
            render (e, 1.0, p.bpm);
            const float peak = render (e, 4.0, p.bpm);
            e.stop(); e.stop();
            render (e, 4.0, p.bpm);         // let tails ring out
            return peak;
        };
        const float flat = measure();
        p.tracks[0]->trimDb = 6.0f;
        const float trimmed = measure();
        check (trimmed > flat * 1.6f && trimmed < flat * 2.2f, "+6 dB of trim roughly doubles the level");
        p.tracks[0]->trimDb = -12.0f;
        check (measure() < flat * 0.3f, "-12 dB of trim cuts the level to a quarter");
        p.tracks[0]->trimDb = 0.0f;

        // Analogue: close to transparent when quiet, squashes when driven, and reports its heat
        p.tracks[0]->volume = 0.1f;
        p.tracks[0]->analogue = true;
        p.tracks[0]->trimDb = -12.0f;
        const float quietCut = measure();
        p.tracks[0]->analogue = false;
        const float quietCutDry = measure();
        check (std::abs (quietCut - quietCutDry) < quietCutDry * 0.25f, "Analogue leaves a quiet signal about the same level");
        p.tracks[0]->trimDb = 12.0f;
        const float hotDry = measure();
        p.tracks[0]->analogue = true;
        e.setPosition (0.0);
        e.play();
        const float hotWet = render (e, 4.0, p.bpm);
        const float heat = p.tracks[0]->heat.load();
        e.stop(); e.stop();
        check (hotWet < hotDry * 0.8f, "Analogue squashes a signal driven hard into it");
        check (heat > 0.1f, "Analogue reports heat when driven");
        check (p.tracks[0]->heat.load() == 0.0f, "stop clears the heat");
        p.tracks[0]->analogue = false;
        p.tracks[0]->trimDb = 0.0f;
        p.tracks[0]->volume = 0.9f;

        // Phase flip of the only track playing changes nothing you can hear in level
        p.tracks[0]->phase = true;
        check (std::abs (measure() - flat) < flat * 0.05f, "phase flip keeps the level");
        p.tracks[0]->phase = false;

        // Save and load keep the console settings and the patching
        p.tracks[0]->trimDb = 4.5f;
        p.tracks[0]->phase = true;
        p.tracks[0]->mono = true;
        p.tracks[0]->width = 1.5f;
        p.tracks[0]->analogue = true;
        p.addBus ("Extra");
        p.tracks[1]->output = 1;
        p.buses[0]->output = 1;
        p.buses[1]->analogue = true;
        const juce::File f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("glowtag-console-test.glowtag");
        check (p.saveTo (f), "project with buses saves");
        Project q;
        q.makeEmpty();
        check (q.loadFrom (f), "project with buses loads");
        check (q.buses.size() == 2 && q.buses[1]->name == "Extra" && q.buses[1]->kind == TrackKind::Bus, "buses round-trip");
        check (q.tracks[1]->output.load() == 1 && q.buses[0]->output.load() == 1 && q.tracks[0]->output.load() == 0, "patching round-trips");
        check (std::abs (q.tracks[0]->trimDb.load() - 4.5f) < 1e-6f && q.tracks[0]->phase.load() && q.tracks[0]->mono.load()
                   && std::abs (q.tracks[0]->width.load() - 1.5f) < 1e-6f && q.tracks[0]->analogue.load() && q.buses[1]->analogue.load(),
               "trim, phase, mono, width and Analogue round-trip");

        // A file with a loop of buses, or a bus that does not exist, loads with those sent to the master
        auto v = p.toVar();
        (*v["buses"].getArray())[1].getDynamicObject()->setProperty ("out", 0);
        (*v["tracks"].getArray())[2].getDynamicObject()->setProperty ("out", 9);
        Project r;
        check (r.fromVar (v), "file with bad patching still loads");
        check (r.tracks[2]->output.load() == -1, "a missing bus becomes the master");
        check (r.outputSlot (r.busSlot (0)) == r.masterSlot() || r.outputSlot (r.busSlot (1)) == r.masterSlot(), "a loop of buses is broken on load");
        f.deleteFile();
    }

    std::cout << "Device wall\n";
    {
        Wall::Model m;
        bool inside = true, sized = true;
        for (int i = 0; i < Wall::numDevices; ++i)
        {
            const auto& p = m.pieces[(size_t) i];
            inside = inside && p.x >= 0.0f && p.x + p.w <= Wall::designWidth && p.y >= 0.0f;
            sized = sized && p.w >= Wall::minPieceWidth;
        }
        check (inside && sized, "every piece starts on the sheet at a usable size");
        check (m.contentHeight() > 300.0f && m.contentHeight() < 1400.0f, "the default wall is about one screen tall");
        check (m.pieces[0].w > m.pieces[10].w, "instruments start larger than rack modules");

        // Device table matches what the app can load
        bool table = true;
        for (int i = 0; i < Wall::numDevices; ++i)
            table = table && juce::String (Wall::device (i).id).isNotEmpty() && (Wall::device (i).kind == Wall::Kind::RackModule) == Wall::device (i).sequencer;
        check (table, "rack modules are the sequencer devices");
        check (Wall::device (0).module == ModuleType::Synth && Wall::device (12).seq == SeqType::Euclid, "devices load the right module");

        // Finding and arrow keys
        check (m.matches (2, "drum") && ! m.matches (2, "synth") && m.firstMatch ("rev") == 5, "typing finds devices by name");
        check (m.firstMatch ("zzz") == -1, "nothing matches nonsense");
        m.pieces[5].title = "Cathedral";
        check (m.matches (5, "cath") && m.matches (5, "reverb") && m.titleOf (5) == "Cathedral", "a retitled piece is found by its title and its real name");
        const int right = m.neighbour (0, 1, 0, {});
        check (right != 0 && m.pieces[(size_t) right].x > m.pieces[0].x, "right arrow moves to a piece on the right");
        check (m.neighbour (right, -1, 0, {}) != right, "and left comes back");
        int leftmost = 0;
        for (int i = 1; i < Wall::numDevices; ++i)
            if (m.pieces[(size_t) i].x + m.pieces[(size_t) i].w * 0.5f < m.pieces[(size_t) leftmost].x + m.pieces[(size_t) leftmost].w * 0.5f)
                leftmost = i;
        check (m.neighbour (leftmost, -1, 0, {}) == leftmost, "no move when nothing lies that way");
        check (m.neighbour (0, 1, 0, "reverb") == 5, "arrows only land on pieces that match the search");

        // Styles
        check (m.commonStyle() == Wall::tubes, "all pieces start in style I");
        m.pieces[3].style = Wall::block;
        check (m.commonStyle() == -1, "mixed styles are reported as mixed");
        const auto a = Wall::rainbowColours (0, Wall::tubes), b = Wall::rainbowColours (0, Wall::marquee), c = Wall::rainbowColours (0, Wall::block);
        check (a != b && b != c && a != c, "each style has its own Rainbow colours");

        // Saving
        m.pieces[7].x = 321.0f; m.pieces[7].y = 654.0f; m.pieces[7].w = 512.0f;
        m.alwaysBright = true; m.glow = 1.25f; m.zoom = 0.6f; m.fitToScreen = true;
        Wall::Model n;
        check (n.fromJson (m.toJson()), "the wall saves and loads");
        check (n.pieces[7].x == 321.0f && n.pieces[7].y == 654.0f && n.pieces[7].w == 512.0f && n.pieces[3].style == Wall::block
                   && n.titleOf (5) == "Cathedral" && n.pieces[0].title.isEmpty(), "positions, sizes, styles and titles round-trip");
        check (n.alwaysBright && std::abs (n.glow - 1.25f) < 1e-5f && std::abs (n.zoom - 0.6f) < 1e-5f && n.fitToScreen, "Always bright, glow and zoom round-trip");

        Wall::Model bad;
        check (! bad.fromJson ("not json") && bad.pieces[0].w == Wall::Model().pieces[0].w, "a broken file leaves the default wall");
        check (bad.fromJson (R"({"glow":9,"zoom":0.01,"pieces":{"synth":{"w":5,"x":-9999,"style":7,"title":"A title that is far too long to fit"}}})"),
               "a file with wild values loads");
        check (bad.glow <= 1.5f && bad.zoom >= 0.3f && bad.pieces[0].w >= Wall::minPieceWidth && bad.pieces[0].style < Wall::numStyles
                   && bad.pieces[0].title.length() <= 18 && bad.pieces[1].w == Wall::Model().pieces[1].w, "wild values are brought into range and missing pieces keep their place");

        n.resetLayout();
        check (n.pieces[7].x == Wall::Model().pieces[7].x && n.titleOf (5) == "Reverb" && n.alwaysBright, "reset restores the layout but keeps the view settings");
    }

    std::cout << "Devices: the expanded view's controls\n";
    {
        auto resolve = [] (const juce::String& n) { return Samples::find (n); };
        auto run = [] (Graph& g, int blocks, double bpm = 120.0, float* rmsOut = nullptr)
        {
            std::vector<float> zl (block, 0.0f), zr (block, 0.0f), ol (block), orr (block);
            float peak = 0.0f; double sum = 0.0;
            for (int b = 0; b < blocks; ++b)
            {
                g.process (zl.data(), zr.data(), ol.data(), orr.data(), block, bpm);
                for (int i = 0; i < block; ++i) { peak = juce::jmax (peak, std::abs (ol[(size_t) i])); sum += ol[(size_t) i] * ol[(size_t) i]; }
            }
            if (rmsOut != nullptr) *rmsOut = (float) std::sqrt (sum / (blocks * block));
            return peak;
        };
        // Same as run, but with a steady tone going in
        auto runTone = [] (Graph& g, int blocks, float amp = 0.5f)
        {
            std::vector<float> il (block), ir (block), ol (block), orr (block);
            float peak = 0.0f; static double ph = 0.0;
            for (int b = 0; b < blocks; ++b)
            {
                for (int i = 0; i < block; ++i) { il[(size_t) i] = ir[(size_t) i] = amp * (float) std::sin (ph); ph += 0.06; }
                g.process (il.data(), ir.data(), ol.data(), orr.data(), block, 120.0);
                for (float v : ol) peak = juce::jmax (peak, std::abs (v));
            }
            return peak;
        };

        // Every module carries its card knobs plus its extra controls
        for (auto t : { ModuleType::Synth, ModuleType::Sampler, ModuleType::Drums, ModuleType::Filter, ModuleType::Drive,
                        ModuleType::Delay, ModuleType::Reverb, ModuleType::EQ, ModuleType::Compressor, ModuleType::LFO })
        {
            Patch q; const int id = q.add (t);
            if ((int) q.find (id)->knobValues.size() != moduleInfo (t).numParams() || moduleInfo (t).numParams() > maxParams)
                check (false, ("a new " + moduleInfo (t).title + " has all its controls").toRawUTF8());
        }
        check (moduleInfo (ModuleType::Synth).numParams() == Param::Synth::Count && moduleInfo (ModuleType::Sampler).numParams() == Param::Sampler::Count
                   && moduleInfo (ModuleType::Drums).numParams() == Param::Drums::Count && moduleInfo (ModuleType::Reverb).numParams() == Param::Reverb::Count
                   && moduleInfo (ModuleType::EQ).numParams() == Param::EQ::Count && moduleInfo (ModuleType::LFO).numParams() == Param::LFO::Count,
               "the control lists match the names both sides use");

        // An older file has only the card knobs: the rest take values that keep its sound
        {
            Patch q;
            const int syn = q.add (ModuleType::Synth), del = q.add (ModuleType::Delay);
            q.find (syn)->knobValues[Param::Synth::Wave] = 0.7f;
            auto v = q.toVar();
            for (auto& mv : *v["modules"].getArray())
            {
                const auto t = (ModuleType) (int) mv["type"];
                auto* ks = mv["knobs"].getArray();
                ks->resize (moduleInfo (t).knobs.size());
            }
            Patch r; r.fromVar (v);
            check (std::abs (r.find (del)->knobValues[Param::Delay::Flutter]) < 1e-6f && r.find (del)->knobValues.size() == (size_t) Param::Delay::Count,
                   "an older delay loads with its new controls, flutter off");
            check (std::abs (r.find (syn)->knobValues[Param::Synth::WaveB] - 0.7f) < 1e-6f, "an older synth's second oscillator copies its first");
            check (std::abs (r.find (syn)->knobValues[Param::Synth::MxAmt0]) < 1e-6f, "and its mod matrix is empty");
        }

        // Synth: unison is wider and louder than one oscillator; mono plays one voice
        {
            Patch q; const int syn = q.add (ModuleType::Synth);
            Graph g (q, sr, resolve);
            g.noteOn (48, 0.9f, -1.0);
            float one = 0.0f; run (g, 30, 120.0, &one);
            check (one > 0.01f, "the synth plays");
            g.allNotesOff(); run (g, 300);
            g.find (syn)->knob[Param::Synth::Unison] = 1.0f;
            g.find (syn)->knob[Param::Synth::Spread] = 0.6f;
            g.noteOn (48, 0.9f, -1.0);
            const float wide = run (g, 30);
            check (wide > 0.01f && wide < 1.5f, "five unison voices stay in range");
            g.allNotesOff(); run (g, 300);
            g.find (syn)->knob[Param::Synth::VMode] = (float) Param::Synth::Mono;
            g.noteOn (48, 0.9f, -1.0); g.noteOn (55, 0.9f, -1.0); run (g, 4);
            check (g.find (syn)->display.load() <= 1.01f, "mono plays one voice at a time");
            g.find (syn)->knob[Param::Synth::VMode] = (float) Param::Synth::Poly;
            g.allNotesOff(); run (g, 300);
            g.noteOn (48, 0.9f, -1.0); g.noteOn (55, 0.9f, -1.0); run (g, 4);
            check (g.find (syn)->display.load() >= 1.99f, "poly plays both");
            int lit = 0; for (auto& vw : g.find (syn)->views) if (vw.pitch.load() >= 0) ++lit;
            check (lit == 2, "each voice shows what it is playing");
        }

        // Sampler: grid slices map to keys from C2; reverse and one-shot still sound
        {
            Patch q; const int sid = q.add (ModuleType::Sampler);
            q.find (sid)->sampleName = "Vox Ahh";
            q.find (sid)->knobValues[Param::Sampler::Mode] = (float) Param::Sampler::Slice;
            q.find (sid)->knobValues[Param::Sampler::Slicer] = (float) Param::Sampler::GridSlices;
            q.find (sid)->knobValues[Param::Sampler::Grid] = 1.0f;     // 8
            Graph g (q, sr, resolve);
            g.noteOn (36 + 3, 1.0f, -1.0);
            check (run (g, 10) > 0.02f, "a slice plays from its key");
            check (g.find (sid)->views[0].slice.load() == 3 || g.find (sid)->display.load() > 0.0f, "and shows which slice");
            Param::SliceSettings st; st.slicer = Param::Sampler::GridSlices; st.grid = 8;
            check (Param::slicesFrom (0.0f, 1.0f, Param::sliceMarkers (st, {})).size() == 8, "a grid of eight makes eight slices");
            st.slicer = Param::Sampler::Transient; st.sens = 1.0f;
            check (Param::sliceMarkers (st, { { 0.2f, 0.9f }, { 0.5f, 0.05f }, { 0.999f, 1.0f } }).size() == 2, "transients inside the sample become markers");
            st.sens = 0.0f;
            check (Param::sliceMarkers (st, { { 0.2f, 0.9f }, { 0.5f, 0.05f } }).size() == 1, "low sensitivity keeps only the strong hits");

            Patch q2; const int s2 = q2.add (ModuleType::Sampler);
            q2.find (s2)->sampleName = "Vox Ahh";
            q2.find (s2)->knobValues[Param::Sampler::Reverse] = 1.0f;
            q2.find (s2)->knobValues[Param::Sampler::Mode] = (float) Param::Sampler::OneShot;
            Graph g2 (q2, sr, resolve);
            g2.noteOn (60, 1.0f, 0.0);
            check (run (g2, 20) > 0.02f, "a reversed one-shot plays through");

            auto hit = std::make_shared<AudioData>();
            hit->buffer.setSize (1, 48000); hit->buffer.clear(); hit->sampleRate = 48000.0;
            for (int at : { 6000, 30000 }) for (int i = 0; i < 2000; ++i) hit->buffer.setSample (0, at + i, std::exp (-i / 300.0f) * ((i & 8) ? 0.8f : -0.8f));
            hit->findOnsets();
            check (hit->onsets.size() == 2 && std::abs (hit->onsets[0].first - 0.125f) < 0.02f && std::abs (hit->onsets[1].first - 0.625f) < 0.02f,
                   "two hits in a file are found where they are");
        }

        // Drum Rack: the step row plays with the transport; choke groups cut each other off
        {
            Patch q; const int dr = q.add (ModuleType::Drums);
            q.find (dr)->knobValues[Param::Drums::Steps0] = (float) (1 | (1 << 4));      // kick on steps 1 and 5
            Graph g (q, sr, resolve);
            check (run (g, 20) < 1e-6f, "steps stay quiet while stopped");
            g.setTransport (0.0, 120.0 / 60.0 / sr, true, 0);
            check (run (g, 4) > 0.1f, "the first step plays when the song runs");
            check (g.find (dr)->meter[0].load() >= 0.0f, "and the row shows where it is");
            Patch c; const int dc = c.add (ModuleType::Drums);
            Graph h (c, sr, resolve);
            const int open = 6, closed = 10;
            check (h.find (dc)->choice (Param::Drums::Choke0 + open) > 0 && h.find (dc)->choice (Param::Drums::Choke0 + open) == h.find (dc)->choice (Param::Drums::Choke0 + closed),
                   "the hats start in one choke group");
            h.noteOn (36 + open, 1.0f, 0.0); run (h, 2);
            h.noteOn (36 + closed, 1.0f, 0.0); run (h, 8);
            check (h.find (dc)->views[(size_t) open].pitch.load() < 0, "the closed hat cuts off the open one");
        }

        // Delay freeze holds what is in the line; the compressor listens to another track
        {
            Patch q; const int d = q.add (ModuleType::Delay);
            q.connect (Patch::inputId, Port::audioOut, d, Port::audioIn);
            Graph g (q, sr, resolve);
            g.find (d)->knob[Param::Delay::Mix] = 1.0f;
            runTone (g, 40);
            g.find (d)->knob[Param::Delay::Freeze] = 1.0f;
            const float held = run (g, 200);
            check (held > 0.05f, "a frozen delay keeps repeating with nothing going in");

            Patch c; const int cp = c.add (ModuleType::Compressor);
            c.connect (Patch::inputId, Port::audioOut, cp, Port::audioIn);
            Graph h (c, sr, resolve);
            h.find (cp)->knob[Param::Comp::Thresh] = 0.0f;
            h.find (cp)->knob[Param::Comp::Ratio] = 1.0f;
            const float free = runTone (h, 20, 0.05f);
            juce::AudioBuffer<float> loud (2, block);
            for (int i = 0; i < block; ++i) { loud.setSample (0, i, 0.9f); loud.setSample (1, i, 0.9f); }
            const juce::AudioBuffer<float>* sides[] = { &loud };
            h.host.sideBuffers = sides; h.host.numSide = 1;
            h.find (cp)->knob[Param::Comp::ListensTo] = 1.0f;
            runTone (h, 10, 0.05f);
            const float ducked = runTone (h, 20, 0.05f);
            check (ducked < free * 0.7f, "a compressor listening to a loud track turns this one down");
            h.host.sideBuffers = nullptr; h.host.numSide = 0;
        }

        // LFO: synced to a quarter note, it lands on the beat; unipolar stays above zero
        {
            Patch q; const int l = q.add (ModuleType::LFO);
            Graph g (q, sr, resolve);
            g.find (l)->knob[Param::LFO::Sync] = 1.0f;        // 1/4
            g.find (l)->knob[Param::LFO::Shape] = 0.0f;
            g.find (l)->knob[Param::LFO::Polarity] = 1.0f;
            float lowest = 1.0f;
            g.setTransport (0.0, 120.0 / 60.0 / sr, true, 0);
            std::vector<float> z (subBlock, 0.0f), ol (subBlock), orr (subBlock);
            for (int b = 0; b < 400; ++b) { g.process (z.data(), z.data(), ol.data(), orr.data(), subBlock, 120.0); lowest = juce::jmin (lowest, g.find (l)->display.load()); }
            check (lowest >= -1e-4f, "a unipolar LFO stays above zero");
            check (std::abs (Param::lfoValue (1, 0.25, 0.0f, 0.0f, false) - 0.0f) < 1e-5f && std::abs (Param::lfoValue (1, 0.5, 0.0f, 0.0f, false) - 1.0f) < 1e-5f,
                   "the triangle passes zero and peaks where the picture shows it");
        }

        // EQ: the low-mid band boosts its range; Filter vowel and Reverb shimmer stay bounded
        {
            Patch q; const int e = q.add (ModuleType::EQ);
            q.connect (Patch::inputId, Port::audioOut, e, Port::audioIn);
            Graph g (q, sr, resolve);
            const float flat = runTone (g, 20);
            g.find (e)->knob[Param::EQ::Freq0 + 1] = Param::EQ::valueOf (460.0f);
            g.find (e)->knob[Param::EQ::LowMid] = 1.0f;
            runTone (g, 10);
            const float boosted = runTone (g, 20);
            check (boosted > flat * 1.5f, "the low mid band boosts a tone in its range");
            check (std::abs (Param::EQ::freqOf (Param::EQ::valueOf (1234.0f)) - 1234.0f) < 0.5f, "EQ frequencies map both ways");

            Patch f; const int fi = f.add (ModuleType::Filter), rv = f.add (ModuleType::Reverb);
            Graph h (f, sr, resolve);
            h.find (fi)->knob[Param::Filter::Vowel] = 0.7f;
            h.find (fi)->knob[Param::Filter::Wobble] = 0.8f;
            h.find (rv)->knob[Param::Reverb::Shimmer] = 1.0f;
            h.find (rv)->knob[Param::Reverb::Space] = (float) Param::Reverb::Cave;
            const float p = runTone (h, 200);
            check (p > 0.01f && p < 4.0f, "vowel, wobble and shimmer keep the sound bounded");
            check (std::abs (Param::compReduction (-10.0f, -20.0f, 4.0f, 0.0f) - 7.5f) < 1e-3f, "the compressor curve takes 4:1 above the threshold");
        }
    }

    std::cout << "Workshop devices: Gas Tube, Crate Digger, String Wall, Ghost Tag, Crowd, Transformer\n";
    {
        auto resolve = [] (const juce::String& n) { return Samples::find (n); };
        auto render = [] (Graph& g, int blocks, float level = 0.0f, bool skipFirst = false)
        {
            std::vector<float> il (block), ir (block), ol (block), orr (block);
            float peak = 0.0f;
            int t = 0;
            bool finite = true;
            for (int b = 0; b < blocks; ++b)
            {
                for (int i = 0; i < block; ++i, ++t)
                    il[(size_t) i] = ir[(size_t) i] = level * std::sin ((float) t * 0.05f);
                g.process (il.data(), ir.data(), ol.data(), orr.data(), block, 90.0);
                if (skipFirst && b == 0) continue;
                for (int i = 0; i < block; ++i)
                {
                    if (! std::isfinite (ol[(size_t) i]) || ! std::isfinite (orr[(size_t) i])) finite = false;
                    peak = juce::jmax (peak, std::abs (ol[(size_t) i]), std::abs (orr[(size_t) i]));
                }
            }
            return finite ? peak : 100.0f;
        };

        check (Samples::find ("Found Record") != nullptr, "the Found Record loop is in the bank");

        for (auto type : { ModuleType::GasTube, ModuleType::CrateDigger, ModuleType::StringWall })
        {
            const auto name = moduleInfo (type).title;
            Patch p;
            p.add (type);
            Graph g (p, sr, resolve);
            check (g.hasInstrument(), (name + " is an instrument").toRawUTF8());
            check (render (g, 4) < 1e-4f, (name + " is quiet with no notes").toRawUTF8());
            g.noteOn (48, 0.9f, -1.0);
            g.noteOn (55, 0.8f, -1.0);
            const float held = render (g, 40);
            check (held > 0.01f && held <= 2.0f, (name + " sounds and stays in range").toRawUTF8());
            g.allNotesOff();
            render (g, 1500);
            const float after = render (g, 8);
            check (after < (type == ModuleType::CrateDigger ? 0.05f : 0.02f), (name + " fades after the notes end").toRawUTF8());
        }

        // String Wall: each mode and material stays bounded, and the bow sustains
        {
            Patch p; const int s = p.add (ModuleType::StringWall);
            Graph g (p, sr, resolve);
            bool bounded = true;
            for (int mode = 0; mode < 3; ++mode)
                for (int mat = 0; mat < 3; ++mat)
                {
                    g.find (s)->knob[Param::Strings::Mode] = (float) mode;
                    g.find (s)->knob[Param::Strings::Material] = (float) mat;
                    g.noteOn (48, 1.0f, -1.0); g.noteOn (52, 1.0f, -1.0); g.noteOn (55, 1.0f, -1.0);
                    const float pk = render (g, 60);
                    if (! (pk > 0.001f && pk <= 2.0f)) { bounded = false; std::cout << "    strings mode " << mode << " mat " << mat << " peak " << pk << "\n"; }
                    g.allNotesOff(); render (g, 200);
                }
            check (bounded, "String Wall: pluck, bow and feedback stay bounded on every material");
        }

        // Gas Tube: every gas sounds
        {
            Patch p; const int s = p.add (ModuleType::GasTube);
            Graph g (p, sr, resolve);
            bool all = true;
            for (int gas = 0; gas < 4; ++gas)
            {
                g.find (s)->knob[Param::GasTube::Gas] = (float) gas;
                g.noteOn (60, 1.0f, -1.0);
                const float pk = render (g, 30);
                if (! (pk > 0.01f && pk <= 2.0f)) all = false;
                g.allNotesOff(); render (g, 400);
            }
            check (all, "Gas Tube: every gas glows within range");
        }

        for (auto type : { ModuleType::GhostTag, ModuleType::Crowd, ModuleType::Transformer })
        {
            const auto name = moduleInfo (type).title;
            Patch p; const int e = p.add (type);
            p.connect (Patch::inputId, Port::audioOut, e, Port::audioIn);
            Graph g (p, sr, resolve);
            const float pk = render (g, 200, 0.5f, true);
            check (pk > 0.01f && pk < 4.0f, (name + " passes a bounded signal").toRawUTF8());
            g.find (e)->knob[0] = 1.0f; g.find (e)->knob[1] = 1.0f; g.find (e)->knob[2] = 1.0f;
            const float hard = render (g, 200, 0.9f, true);
            check (hard > 0.001f && hard < 4.0f, (name + " stays bounded with its first knobs up").toRawUTF8());
        }

        // Ghost Tag hold freezes the past and keeps sounding without input
        {
            Patch p; const int e = p.add (ModuleType::GhostTag);
            p.connect (Patch::inputId, Port::audioOut, e, Port::audioIn);
            Graph g (p, sr, resolve);
            g.find (e)->knob[Param::Ghost::Mix] = 1.0f;
            render (g, 400, 0.5f);
            g.find (e)->knob[Param::Ghost::Hold] = 1.0f;
            render (g, 100, 0.5f);
            const float held = render (g, 100, 0.0f);
            check (held > 0.005f && held < 4.0f, "Ghost Tag: a held stretch keeps playing after the input stops");
        }
    }

    std::cout << (failures == 0 ? "\nAll passed\n" : "\nSome checks failed\n");
    return failures == 0 ? 0 : 1;
}
