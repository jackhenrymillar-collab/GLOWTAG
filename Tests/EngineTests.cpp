// Offline checks for the engine and patch model: run with ./GlowtagTests
#include <juce_audio_basics/juce_audio_basics.h>
#include "Engine/Project.h"
#include "Engine/AudioEngine.h"
#include "Engine/Patch.h"
#include "Engine/Graph.h"
#include "Engine/Samples.h"
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
        check (render (e, 0.5, p.bpm) < 1e-6f, "silence after the audio clip ends");
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

    std::cout << (failures == 0 ? "\nAll passed\n" : "\nSome checks failed\n");
    return failures == 0 ? 0 : 1;
}
