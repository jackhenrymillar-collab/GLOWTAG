// Offline checks for the engine and patch model: run with ./GlowtagTests
#include <juce_audio_basics/juce_audio_basics.h>
#include "Engine/Project.h"
#include "Engine/AudioEngine.h"
#include "Engine/Patch.h"
#include "Engine/Graph.h"
#include "Engine/Samples.h"
#include "Engine/Sequencer.h"
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

    std::cout << (failures == 0 ? "\nAll passed\n" : "\nSome checks failed\n");
    return failures == 0 ? 0 : 1;
}
