#include "AudioEngine.h"
#include "Samples.h"

AudioEngine::AudioEngine (Project& p) : project (p)
{
    midiInput.reset (44100.0);
    makeSlots();
}

void AudioEngine::makeSlots()
{
    slots.clear();
    for (int i = 0; i < project.numSlots(); ++i)
    {
        slots.push_back (std::make_unique<TrackSlot>());
        slots.back()->input.setSize (2, blockSize);
        slots.back()->output.setSize (2, blockSize);
        slots.back()->last.setSize (2, blockSize);
        for (auto& c : slots.back()->console) c.prepare (sr);
    }
    sideBuffers.clear();
    for (auto& s : slots) sideBuffers.push_back (&s->last);
    busOrder.reserve (Project::maxBuses);
    busDepth.reserve (Project::maxBuses);
}

void AudioEngine::projectChanged()
{
    replaceProject ([] { return true; });
}

bool AudioEngine::replaceProject (const std::function<bool()>& load)
{
    return swapProject (load, true);
}

bool AudioEngine::restructure (const std::function<bool()>& edit)
{
    return swapProject (edit, false);
}

bool AudioEngine::swapProject (const std::function<bool()>& load, bool resetTransport)
{
    const juce::SpinLock::ScopedLockType sl (engineLock);
    if (resetTransport)
    {
        playing = false;
        position = 0.0;
        playStartPosition = 0.0;
        requestedPosition = -1.0;
    }
    const bool ok = load();
    makeSlots();
    rebuildAll();
    // Nothing is playing while we hold the lock, so the new graphs can go straight in
    for (auto& s : slots)
    {
        s->current = s->pending;
        s->seq = s->seqPending;
        s->pending.reset();
        s->seqPending.reset();
    }
    return ok;
}

void AudioEngine::preview (std::shared_ptr<const AudioData> data)
{
    std::shared_ptr<const AudioData> old;
    {
        const juce::SpinLock::ScopedLockType sl (previewLock);
        old = std::move (previewRetired);        // freed here, not on the audio thread
        previewPending = std::move (data);
        previewChanged = true;
    }
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
    auto graph = std::make_shared<Graph> (project.slot (track).patch, sr,
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
    {
        rebuildTrack ((int) i);
        rebuildSequencer ((int) i);
    }
}

void AudioEngine::rebuildSequencer (int track)
{
    if (track < 0 || track >= (int) slots.size())
        return;
    auto& slot = *slots[(size_t) track];
    auto rt = std::make_shared<SeqRuntime> (project.slot (track).seq);
    std::shared_ptr<SeqRuntime> old;
    {
        const juce::SpinLock::ScopedLockType sl (slot.lock);
        old = std::move (slot.seqRetired);
        slot.seqPending = rt;
        slot.seqUi = rt;
    }
    old.reset();
}

void AudioEngine::setSeqKnob (int track, int moduleId, int knob, float value)
{
    if (auto* n = liveSeqNode (track, moduleId))
        if (knob >= 0 && knob < (int) n->knob.size())
            n->knob[(size_t) knob] = value;
}

void AudioEngine::setSeqStep (int track, int moduleId, int step, int pitch, bool on)
{
    if (auto* n = liveSeqNode (track, moduleId))
        if (step >= 0 && step < seqSteps)
        {
            n->stepPitch[(size_t) step] = pitch;
            n->stepOn[(size_t) step] = on;
        }
}

SeqNode* AudioEngine::liveSeqNode (int track, int moduleId)
{
    if (track < 0 || track >= (int) slots.size())
        return nullptr;
    if (auto rt = slots[(size_t) track]->seqUi)
        return rt->find (moduleId);
    return nullptr;
}

void AudioEngine::runSequencers (int numSamples, double beatsPerSample, bool isPlaying, bool jumped)
{
    // Wall clock for the Circadian module: seconds since local midnight
    const auto now = juce::Time::getCurrentTime();
    const double seconds = now.getHours() * 3600.0 + now.getMinutes() * 60.0 + now.getSeconds() + now.getMilliseconds() / 1000.0;

    for (size_t ti = 0; ti < project.tracks.size() && ti < slots.size(); ++ti)
    {
        auto& slot = *slots[ti];
        auto* rt = slot.seq.get();
        if (rt == nullptr)
            continue;
        const bool active = project.tracks[ti]->rolling.load();
        auto sink = [&] (int offset)
        {
            return [&, offset] (int pitch, float velocity, bool on)
            {
                events.push_back ({ offset, (int) ti, pitch, velocity, on ? -1.0 : -3.0, false, false });
            };
        };
        if (! active)
        {
            if (slot.seqWasActive) rt->allNotesOff (sink (0));
            slot.seqWasActive = false;
            continue;
        }
        if ((! isPlaying && wasPlaying) || jumped)
            rt->allNotesOff (sink (0));
        slot.seqWasActive = true;

        size_t seg = 0;
        for (int s = 0; s < numSamples; s += subBlock)
        {
            while (seg + 1 < segments.size() && segments[seg + 1].startSample <= s) ++seg;
            SeqTick tick;
            tick.beat = isPlaying ? segments[seg].startBeat + (s - segments[seg].startSample) * beatsPerSample : -1.0;
            tick.beatsPerTick = juce::jmin (subBlock, numSamples - s) * beatsPerSample;
            tick.seconds = seconds + s / sr;
            tick.reset = (jumped && s == 0) || (seg > 0 && segments[seg].startSample > s - subBlock);
            rt->process (tick, sink (s));
        }
        if (auto* g = slot.current.get())
            g->externalMod = rt->modOut.load();
    }
}

void AudioEngine::setKnob (int track, int moduleId, int knob, float value)
{
    if (track < 0 || track >= (int) slots.size() || knob < 0 || knob >= maxParams)
        return;
    if (auto g = slots[(size_t) track]->ui)
        if (auto* node = g->find (moduleId))
            node->knob[(size_t) knob] = value;
}

std::shared_ptr<Graph> AudioEngine::uiGraph (int track)
{
    return track >= 0 && track < (int) slots.size() ? slots[(size_t) track]->ui : nullptr;
}

void AudioEngine::capturePluginStates()
{
    for (size_t i = 0; i < slots.size() && (int) i < project.numSlots(); ++i)
        if (auto g = slots[i]->ui)
            g->capturePluginState (project.slot ((int) i).patch);
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
    visListener.prepare (sr);
    events.reserve (1024);
    liveMidi.ensureSize (2048);
    for (auto& s : slots)
    {
        s->input.setSize (2, blockSize);
        s->output.setSize (2, blockSize);
        s->last.setSize (2, blockSize);
        s->last.clear();
        for (auto& c : s->console) c.prepare (sr);
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
    for (int i = 0; i < project.numSlots(); ++i)
    {
        project.slot (i).meter = 0.0f;
        project.slot (i).heat = 0.0f;
    }
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

float AudioEngine::runChannel (TrackSlot& slot, Track& track, int numSamples)
{
    float* l = slot.output.getWritePointer (0);
    float* r = slot.output.getWritePointer (1);
    const float trim = juce::Decibels::decibelsToGain (track.trimDb.load()) * (track.phase.load() ? -1.0f : 1.0f);
    const float width = track.mono.load() ? 0.0f : track.width.load();
    const bool analogue = track.analogue.load();
    if (analogue && ! slot.consoleWasOn)
        for (auto& c : slot.console) c.reset();
    slot.consoleWasOn = analogue;

    float peak = 0.0f, drive = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        float a = l[i] * trim, b = r[i] * trim;
        if (width != 1.0f)
        {
            const float mid = 0.5f * (a + b), side = 0.5f * (a - b) * width;
            a = mid + side;
            b = mid - side;
        }
        if (analogue)
        {
            drive = juce::jmax (drive, std::abs (a), std::abs (b));
            a = slot.console[0].process (a);
            b = slot.console[1].process (b);
        }
        l[i] = a;
        r[i] = b;
        peak = juce::jmax (peak, std::abs (a), std::abs (b));
    }
    // Heat: nothing below about -7 dB into the stage, fully hot from +3 dB
    const float heat = analogue ? juce::jlimit (0.0f, 1.0f, (drive - 0.45f) / 0.95f) : 0.0f;
    track.heat = juce::jmax (heat, track.heat.load() * 0.9f);
    return peak;
}

void AudioEngine::getNextAudioBlock (const juce::AudioSourceChannelInfo& info)
{
    info.clearActiveBufferRegion();
    const juce::SpinLock::ScopedTryLockType engineScope (engineLock);
    if (! engineScope.isLocked())
        return;             // the project is being replaced; a block of silence
    const int numSamples = info.numSamples;
    if (numSamples > blockSize)
    {
        // The device handed us more than it promised; grow on the message thread next time
        blockSize = numSamples;
        for (auto& s : slots) { s->input.setSize (2, blockSize, false, false, true); s->output.setSize (2, blockSize, false, false, true); s->last.setSize (2, blockSize, false, false, true); }
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
        if (sl.isLocked() && s->seqPending != nullptr)
        {
            s->seqRetired = std::move (s->seq);
            s->seq = std::move (s->seqPending);
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

    segments.clear();
    segments.push_back ({ 0, pos });
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
                    const bool rolling = track.rolling.load();
                    for (auto& clip : track.clips)
                    {
                        if (rolling && clip.kind == ClipKind::Midi)
                            continue;
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
                if (wrap && done < numSamples)
                    segments.push_back ({ done, pos });
            }
        }
    }

    // Rolling sequencers add their notes at sub-block offsets
    runSequencers (numSamples, beatsPerSample, isPlaying, req >= 0.0);
    wasPlaying = isPlaying;

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
    std::vector<float>& peaks = slotPeaks;
    peaks.assign (slots.size(), 0.0f);

    const int numTracks = (int) project.tracks.size();
    auto& masterSlot = *slots[(size_t) project.masterSlot()];
    float* mixL = masterSlot.input.getWritePointer (0);
    float* mixR = masterSlot.input.getWritePointer (1);

    auto runGraph = [&] (size_t ti, Graph& graph, Track& track)
    {
        auto& slot = *slots[ti];
        const float* inL = slot.input.getReadPointer (0);
        const float* inR = slot.input.getReadPointer (1);
        float* outL = slot.output.getWritePointer (0);
        float* outR = slot.output.getWritePointer (1);

        // The song position at a sample of this block, for step rows, synced LFOs and the like
        graph.host.sideBuffers = sideBuffers.data();
        graph.host.numSide = (int) sideBuffers.size();
        auto locate = [&] (int s)
        {
            size_t seg = 0;
            while (seg + 1 < segments.size() && segments[seg + 1].startSample <= s) ++seg;
            graph.setTransport (segments[seg].startBeat + (s - segments[seg].startSample) * beatsPerSample,
                                beatsPerSample, isPlaying, s);
        };

        // Run the graph between this track's events so notes land on their exact sample
        int cursor = 0;
        for (auto& e : events)
        {
            if (e.click || e.track != (int) ti)
                continue;
            const int at = juce::jlimit (0, numSamples, e.offset);
            if (at > cursor)
            {
                locate (cursor);
                graph.process (inL + cursor, inR + cursor, outL + cursor, outR + cursor, at - cursor, bpm);
                cursor = at;
            }
            if (e.lengthBeats <= -2.5)
                graph.noteOff (e.pitch);
            else
            {
                graph.noteOn (e.pitch, e.velocity, e.lengthBeats < 0.0 ? -1.0 : e.lengthBeats * 60.0 / bpm * sr);
                if (e.pitch <= 38 && track.kind == TrackKind::Audio)
                    pulse = juce::jmax (pulse.load(), e.velocity);
            }
        }
        if (cursor < numSamples)
        {
            locate (cursor);
            graph.process (inL + cursor, inR + cursor, outL + cursor, outR + cursor, numSamples - cursor, bpm);
        }
    };

    // Where each channel goes. Buses are ordered so that one feeding another runs first; a bus caught
    // in a loop (which the project never allows) falls back to the master.
    const int numBuses = (int) project.buses.size();
    const int masterIndex = project.masterSlot();
    busDepth.assign ((size_t) numBuses, 0);
    busOrder.clear();
    for (int b = 0; b < numBuses; ++b)
    {
        int depth = 1, at = project.buses[(size_t) b]->output.load();
        while (at >= 0 && at < numBuses && at != b && depth <= numBuses) { ++depth; at = project.buses[(size_t) at]->output.load(); }
        busDepth[(size_t) b] = depth > numBuses || at == b ? 0 : depth;     // 0: looped
        busOrder.push_back (b);
    }
    std::sort (busOrder.begin(), busOrder.end(), [this] (int x, int y) { return busDepth[(size_t) x] > busDepth[(size_t) y]; });

    auto destination = [&] (int si, Track& track) -> TrackSlot&
    {
        const int out = track.output.load();
        const bool looped = track.kind == TrackKind::Bus && busDepth[(size_t) (si - numTracks)] == 0;
        if (out >= 0 && out < numBuses && ! looped && project.busSlot (out) != si)
            return *slots[(size_t) project.busSlot (out)];
        return masterSlot;
    };

    // One channel: its patch, the console stage, then fader and pan into wherever it is patched
    auto runSlot = [&] (int si, Track& track, bool withSends)
    {
        auto& slot = *slots[(size_t) si];
        auto* graph = slot.current.get();
        if (graph == nullptr)
            return;
        runGraph ((size_t) si, *graph, track);
        if (! project.isAudible (si))
            return;

        const float channelPeak = runChannel (slot, track, numSamples);
        const float vol = track.volume.load(), pan = track.pan.load();
        const float gl = vol * std::sqrt (0.5f * (1.0f - pan)), gr = vol * std::sqrt (0.5f * (1.0f + pan));
        auto& dest = destination (si, track);
        dest.input.addFrom (0, 0, slot.output, 0, 0, numSamples, gl);
        dest.input.addFrom (1, 0, slot.output, 1, 0, numSamples, gr);
        peaks[(size_t) si] = channelPeak * vol;
        if (withSends)
            for (int r = 0; r < Project::numReturns; ++r)
            {
                const float amount = track.send[(size_t) r].load();
                if (amount <= 0.001f) continue;
                auto& ret = *slots[(size_t) project.returnSlot (r)];
                ret.input.addFrom (0, 0, slot.output, 0, 0, numSamples, gl * amount);
                ret.input.addFrom (1, 0, slot.output, 1, 0, numSamples, gr * amount);
            }
    };

    // Tracks (with their post-fader sends), then the returns, then the buses from the deepest up
    for (int ti = 0; ti < numTracks; ++ti)
        runSlot (ti, *project.tracks[(size_t) ti], true);
    for (int r = 0; r < Project::numReturns; ++r)
        runSlot (project.returnSlot (r), *project.returns[(size_t) r], false);
    for (int b : busOrder)
        runSlot (project.busSlot (b), *project.buses[(size_t) b], false);

    // Master chain and console stage, then out
    {
        auto& track = *project.master;
        const float vol = track.volume.load();
        if (auto* graph = masterSlot.current.get())
        {
            runGraph ((size_t) masterIndex, *graph, track);
            const float channelPeak = runChannel (masterSlot, track, numSamples);
            const float* outL = masterSlot.output.getReadPointer (0);
            const float* outR = masterSlot.output.getReadPointer (1);
            for (int i = 0; i < numSamples; ++i)
            {
                left[i] = outL[i] * vol;
                if (right != nullptr) right[i] = outR[i] * vol;
            }
            peaks[(size_t) masterIndex] = channelPeak * vol;
        }
        else
        {
            for (int i = 0; i < numSamples; ++i)
            {
                left[i] = mixL[i] * vol;
                if (right != nullptr) right[i] = mixR[i] * vol;
            }
        }
    }

    // Metronome clicks and a gentle master limiter
    for (auto& e : events)
        if (e.click)
            for (auto& c : clicks)
                if (! c.active) { c = { true, 0.0, 0.0f, e.accent }; break; }

    // The browser's preview, mixed in before the limiter
    {
        const juce::SpinLock::ScopedTryLockType sl (previewLock);
        if (sl.isLocked() && previewChanged)
        {
            previewRetired = std::move (previewPlaying);
            previewPlaying = std::move (previewPending);
            previewChanged = false;
            previewPos = 0.0;
        }
    }
    if (const auto* pv = previewPlaying.get(); pv != nullptr && pv->buffer.getNumSamples() > 1)
    {
        const int len = pv->buffer.getNumSamples() - 1;
        const float* pl = pv->buffer.getReadPointer (0);
        const float* pr = pv->buffer.getReadPointer (juce::jmin (1, pv->buffer.getNumChannels() - 1));
        const double rate = pv->sampleRate / sr;
        for (int i = 0; i < numSamples && (int) previewPos < len; ++i)
        {
            const int idx = (int) previewPos;
            const float frac = (float) (previewPos - idx);
            left[i] += (pl[idx] + frac * (pl[idx + 1] - pl[idx])) * 0.7f;
            if (right != nullptr) right[i] += (pr[idx] + frac * (pr[idx + 1] - pr[idx])) * 0.7f;
            previewPos += rate;
        }
    }

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

    // Keep each channel's sound for compressors listening to it in the next block
    for (auto& s : slots)
        for (int ch = 0; ch < 2; ++ch)
            s->last.copyFrom (ch, 0, s->output, ch, 0, juce::jmin (numSamples, s->last.getNumSamples()));

    for (size_t i = 0; i < slots.size() && i < peaks.size(); ++i)
    {
        auto& m = project.slot ((int) i).meter;
        m = std::max (std::min (1.5f, peaks[i]), m.load() * 0.82f);    // above 1.0 is over: the strip shows red
    }
    if (visListener.enabled.load (std::memory_order_relaxed))
    {
        const int feed = visFeed.load();
        if (feed >= 0 && feed < (int) slots.size() && feed != masterIndex)
            visListener.push (slots[(size_t) feed]->output.getReadPointer (0), slots[(size_t) feed]->output.getReadPointer (1), numSamples);
        else
            visListener.push (left, right, numSamples);
    }
    pulse = pulse.load() * 0.86f;
}
