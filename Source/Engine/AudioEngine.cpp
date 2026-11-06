#include "AudioEngine.h"
#include "Samples.h"

AudioEngine::AudioEngine (Project& p) : project (p)
{
    midiInput.reset (44100.0);
    for (size_t i = 0; i < project.tracks.size(); ++i)
        slots.push_back (std::make_unique<TrackSlot>());
}

std::shared_ptr<const AudioData> AudioEngine::resolveSample (const juce::String& nameOrPath)
{
    if (nameOrPath.isEmpty())
        return nullptr;
    if (auto builtIn = Samples::find (nameOrPath))
        return builtIn;
    auto it = fileCache.find (nameOrPath);
    if (it != fileCache.end())
        return it->second;
    auto loaded = AudioData::load (juce::File (nameOrPath));
    fileCache[nameOrPath] = loaded;
    return loaded;
}

void AudioEngine::rebuildTrack (int track)
{
    if (track < 0 || track >= (int) slots.size())
        return;
    auto& slot = *slots[(size_t) track];
    auto graph = std::make_shared<Graph> (project.tracks[(size_t) track]->patch, sr,
                                          [this] (const juce::String& n) { return resolveSample (n); });
    std::shared_ptr<Graph> old;
    {
        const juce::SpinLock::ScopedLockType sl (slot.lock);
        old = std::move (slot.retired);
        slot.pending = graph;
        slot.ui = graph;
    }
    old.reset();   // freed here, on the message thread
}

void AudioEngine::rebuildAll()
{
    for (size_t i = 0; i < slots.size(); ++i)
        rebuildTrack ((int) i);
}

void AudioEngine::setKnob (int track, int moduleId, int knob, float value)
{
    if (track < 0 || track >= (int) slots.size() || knob < 0 || knob >= maxKnobs)
        return;
    if (auto g = slots[(size_t) track]->ui)
        if (auto* node = g->find (moduleId))
            node->knob[(size_t) knob] = value;
}

Node* AudioEngine::liveNode (int track, int moduleId)
{
    if (track < 0 || track >= (int) slots.size())
        return nullptr;
    if (auto g = slots[(size_t) track]->ui)
        return g->find (moduleId);
    return nullptr;
}

void AudioEngine::prepareToPlay (int samplesPerBlockExpected, double sampleRate)
{
    sr = sampleRate > 0 ? sampleRate : 44100.0;
    blockSize = juce::jmax (16, samplesPerBlockExpected);
    midiInput.reset (sr);
    events.reserve (1024);
    liveMidi.ensureSize (2048);
    for (auto& s : slots)
    {
        s->input.setSize (2, blockSize);
        s->output.setSize (2, blockSize);
    }
    rebuildAll();
}

void AudioEngine::play()
{
    playStartPosition = position.load();
    playing = true;
}

void AudioEngine::stop()
{
    if (! playing.load())
    {
        setPosition (0.0);
        return;
    }
    playing = false;
    setPosition (playStartPosition);
    for (auto& t : project.tracks)
        t->meter = 0.0f;
    pulse = 0.0f;
}

void AudioEngine::setPosition (double beats)
{
    beats = juce::jlimit (0.0, Project::lengthBars * beatsPerBar, beats);
    requestedPosition = beats;
    position = beats;
    if (! playing.load())
        playStartPosition = beats;
}

void AudioEngine::pushRecorded (const RecordedEvent& e)
{
    const auto scope = recordFifo.write (1);
    if (scope.blockSize1 > 0) recordBuffer[(size_t) scope.startIndex1] = e;
    else if (scope.blockSize2 > 0) recordBuffer[(size_t) scope.startIndex2] = e;
}

int AudioEngine::readRecorded (RecordedEvent* dest, int max)
{
    const auto scope = recordFifo.read (juce::jmin (max, recordFifo.getNumReady()));
    int n = 0;
    for (int i = 0; i < scope.blockSize1; ++i) dest[n++] = recordBuffer[(size_t) (scope.startIndex1 + i)];
    for (int i = 0; i < scope.blockSize2; ++i) dest[n++] = recordBuffer[(size_t) (scope.startIndex2 + i)];
    return n;
}

void AudioEngine::renderAudioClips (TrackSlot& slot, Track& track, int offset, int numSamples, double startBeat, double beatsPerSample)
{
    const double secondsPerBeat = 60.0 / project.bpm.load();
    const double endBeat = startBeat + numSamples * beatsPerSample;
    auto* l = slot.input.getWritePointer (0) + offset;
    auto* r = slot.input.getWritePointer (1) + offset;

    for (auto& clip : track.clips)
    {
        if (clip.kind != ClipKind::Audio || clip.audio == nullptr || clip.end() <= startBeat || clip.start >= endBeat)
            continue;
        const auto& data = *clip.audio;
        const int len = data.buffer.getNumSamples();
        const float* srcL = data.buffer.getReadPointer (0);
        const float* srcR = data.buffer.getReadPointer (1);
        for (int i = 0; i < numSamples; ++i)
        {
            const double beat = startBeat + i * beatsPerSample;
            if (beat < clip.start || beat >= clip.end())
                continue;
            const double srcPos = (beat - clip.start) * secondsPerBeat * data.sampleRate;
            const int idx = (int) srcPos;
            if (idx + 1 >= len)
                continue;
            const float frac = (float) (srcPos - idx);
            l[i] += srcL[idx] + frac * (srcL[idx + 1] - srcL[idx]);
            r[i] += srcR[idx] + frac * (srcR[idx + 1] - srcR[idx]);
        }
    }
}

void AudioEngine::getNextAudioBlock (const juce::AudioSourceChannelInfo& info)
{
    info.clearActiveBufferRegion();
    const int numSamples = info.numSamples;
    if (numSamples > blockSize)
    {
        // The device handed us more than it promised; grow on the message thread next time
        blockSize = numSamples;
        for (auto& s : slots) { s->input.setSize (2, blockSize, false, false, true); s->output.setSize (2, blockSize, false, false, true); }
    }
    events.clear();

    // Pick up rebuilt patches
    for (auto& s : slots)
    {
        const juce::SpinLock::ScopedTryLockType sl (s->lock);
        if (sl.isLocked() && s->pending != nullptr)
        {
            s->retired = std::move (s->current);
            s->current = std::move (s->pending);
        }
        s->input.clear();
        s->output.clear();
    }

    // A new position from the UI (click on the ruler, stop) takes effect at the block start
    const double req = requestedPosition.exchange (-1.0);
    double pos = req >= 0.0 ? req : position.load();
    if (req >= 0.0)
        for (auto& s : slots)
            if (s->current != nullptr)
                s->current->allNotesOff();

    const double bpm = project.bpm.load();
    const double beatsPerSample = bpm / 60.0 / sr;
    const bool isPlaying = playing.load();

    liveMidi.clear();
    midiInput.removeNextBlockOfMessages (liveMidi, numSamples);
    const int live = juce::jlimit (0, (int) project.tracks.size() - 1, liveTrack.load());

    {
        const juce::SpinLock::ScopedLockType sl (project.lock);

        if (isPlaying)
        {
            // Split the block where the loop wraps, and gather every note that starts inside it
            const bool loop = looping.load();
            const double ls = loopStart.load(), le = loopEnd.load();
            int done = 0;
            while (done < numSamples)
            {
                int segSamples = numSamples - done;
                double segEnd = pos + segSamples * beatsPerSample;
                bool wrap = false;
                if (loop && le > ls && pos < le && segEnd >= le)
                {
                    segSamples = juce::jlimit (1, segSamples, (int) std::ceil ((le - pos) / beatsPerSample));
                    segEnd = le;
                    wrap = true;
                }

                for (size_t ti = 0; ti < project.tracks.size(); ++ti)
                {
                    auto& track = *project.tracks[ti];
                    for (auto& clip : track.clips)
                    {
                        if (clip.kind != ClipKind::Midi || clip.end() <= pos || clip.start >= segEnd || clip.loopLength <= 0.0)
                            continue;
                        const double from = juce::jmax (pos, clip.start), to = juce::jmin (segEnd, clip.end());
                        const int firstRep = (int) std::floor ((from - clip.start) / clip.loopLength);
                        const int lastRep  = (int) std::floor ((to - clip.start) / clip.loopLength);
                        for (int rep = firstRep; rep <= lastRep; ++rep)
                            for (auto& n : clip.notes)
                            {
                                if (n.start >= clip.loopLength)
                                    continue;
                                const double beat = clip.start + rep * clip.loopLength + n.start;
                                if (beat >= from && beat < to)
                                    events.push_back ({ done + (int) ((beat - pos) / beatsPerSample), (int) ti, n.pitch, n.velocity,
                                                        juce::jmin (n.length, clip.end() - beat), false, false });
                            }
                    }
                    renderAudioClips (*slots[ti], track, done, segSamples, pos, beatsPerSample);
                }

                if (metronome.load())
                    for (double b = std::ceil (pos - 1e-9); b < segEnd; b += 1.0)
                        events.push_back ({ done + (int) ((b - pos) / beatsPerSample), 0, 0, 1.0f, 0.05, true,
                                            std::fmod (b, beatsPerBar) < 0.001 });

                done += segSamples;
                pos = wrap ? ls : segEnd;
            }
        }
    }

    // Live notes at their own offsets; note-offs are applied in the per-track loop below
    const double blockStartBeat = req >= 0.0 ? req : position.load();
    for (const auto meta : liveMidi)
    {
        const auto msg = meta.getMessage();
        if (msg.isNoteOn())
        {
            events.push_back ({ meta.samplePosition, live, msg.getNoteNumber(), msg.getFloatVelocity(), -1.0, false, false });
            if (isPlaying && recordArmed.load())
                pushRecorded ({ blockStartBeat + meta.samplePosition * beatsPerSample, msg.getNoteNumber(), msg.getFloatVelocity(), true });
        }
        else if (msg.isNoteOff())
        {
            events.push_back ({ meta.samplePosition, live, msg.getNoteNumber(), 0.0f, -3.0, false, false });   // -3 = note off
            if (isPlaying && recordArmed.load())
                pushRecorded ({ blockStartBeat + meta.samplePosition * beatsPerSample, msg.getNoteNumber(), 0.0f, false });
        }
    }

    std::sort (events.begin(), events.end(), [] (const Event& a, const Event& b) { return a.offset < b.offset; });

    auto* left  = info.buffer->getWritePointer (0, info.startSample);
    auto* right = info.buffer->getNumChannels() > 1 ? info.buffer->getWritePointer (1, info.startSample) : nullptr;
    std::array<float, 32> peaks {};

    // Each track: run its graph between events so notes land on their exact sample
    for (size_t ti = 0; ti < slots.size(); ++ti)
    {
        auto& slot = *slots[ti];
        auto& track = *project.tracks[ti];
        auto* graph = slot.current.get();
        if (graph == nullptr)
            continue;

        const float* inL = slot.input.getReadPointer (0);
        const float* inR = slot.input.getReadPointer (1);
        float* outL = slot.output.getWritePointer (0);
        float* outR = slot.output.getWritePointer (1);

        int cursor = 0;
        for (auto& e : events)
        {
            if (e.click || e.track != (int) ti)
                continue;
            const int at = juce::jlimit (0, numSamples, e.offset);
            if (at > cursor)
            {
                graph->process (inL + cursor, inR + cursor, outL + cursor, outR + cursor, at - cursor, bpm);
                cursor = at;
            }
            if (e.lengthBeats <= -2.5)
                graph->noteOff (e.pitch);
            else
            {
                graph->noteOn (e.pitch, e.velocity, e.lengthBeats < 0.0 ? -1.0 : e.lengthBeats * 60.0 / bpm * sr);
                if (e.pitch <= 38 && track.kind == TrackKind::Audio)
                    pulse = juce::jmax (pulse.load(), e.velocity);
            }
        }
        if (cursor < numSamples)
            graph->process (inL + cursor, inR + cursor, outL + cursor, outR + cursor, numSamples - cursor, bpm);

        if (! project.isAudible ((int) ti))
            continue;

        const float vol = track.volume.load(), pan = track.pan.load();
        const float gl = vol * std::sqrt (0.5f * (1.0f - pan)), gr = vol * std::sqrt (0.5f * (1.0f + pan));
        float peak = 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            left[i] += outL[i] * gl;
            if (right != nullptr) right[i] += outR[i] * gr;
            peak = juce::jmax (peak, std::abs (outL[i]) * vol);
        }
        if (ti < peaks.size())
            peaks[ti] = peak;
    }

    // Metronome clicks and a gentle master limiter
    for (auto& e : events)
        if (e.click)
            for (auto& c : clicks)
                if (! c.active) { c = { true, 0.0, 0.0f, e.accent }; break; }

    for (int i = 0; i < numSamples; ++i)
    {
        float click = 0.0f;
        for (auto& c : clicks)
        {
            if (! c.active) continue;
            c.phase += (c.accent ? 1760.0 : 1320.0) / sr;
            click += (float) std::sin (c.phase * juce::MathConstants<double>::twoPi) * 0.25f * std::exp (-c.t * 60.0f);
            c.t += 1.0f / (float) sr;
            if (c.t > 0.1f) c.active = false;
        }
        left[i] = std::tanh (left[i] + click);
        if (right != nullptr) right[i] = std::tanh (right[i] + click);
    }

    if (isPlaying)
        position = pos;

    for (size_t i = 0; i < project.tracks.size() && i < peaks.size(); ++i)
    {
        auto& m = project.tracks[i]->meter;
        m = std::max (std::min (1.0f, peaks[i]), m.load() * 0.82f);
    }
    pulse = pulse.load() * 0.86f;
}
