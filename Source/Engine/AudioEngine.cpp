#include "AudioEngine.h"

namespace
{
    float midiToHz (int note) { return 440.0f * std::pow (2.0f, (float) (note - 69) / 12.0f); }

    bool isDrum (Sound s) { return s == Sound::Kick || s == Sound::Snare || s == Sound::Hat || s == Sound::Clap || s == Sound::Perc; }

    // Attack and release times (seconds) for the melodic voices
    float attackTime (Sound s)  { return s == Sound::Pad ? 0.25f : (s == Sound::Keys ? 0.006f : 0.003f); }
    float releaseTime (Sound s) { return s == Sound::Pad ? 0.6f : (s == Sound::Keys ? 0.25f : 0.08f); }
}

void AudioEngine::prepareToPlay (int samplesPerBlockExpected, double sampleRate)
{
    sr = sampleRate > 0 ? sampleRate : 44100.0;
    midiInput.reset (sr);
    events.reserve (512);
    liveMidi.ensureSize (2048);
    juce::ignoreUnused (samplesPerBlockExpected);
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

void AudioEngine::startVoice (const Event& e)
{
    Sound sound = Sound::Keys;
    if (! e.click)
    {
        sound = project.tracks[(size_t) e.track]->sound;
        if (sound == Sound::Silent)
            return;
    }

    // Steal the oldest voice if every voice is busy
    Voice* target = nullptr;
    double oldest = -1.0;
    for (auto& v : voices)
    {
        if (! v.active) { target = &v; break; }
        if (v.t > oldest) { oldest = v.t; target = &v; }
    }

    auto& v = *target;
    v = Voice {};
    v.active = true;
    v.click = e.click;
    v.accent = e.accent;
    v.sound = sound;
    v.track = e.track;
    v.pitch = e.pitch;
    v.velocity = e.velocity;
    v.freq = midiToHz (e.pitch);
    v.gateSamples = e.lengthBeats < 0.0 ? -1.0 : e.lengthBeats * 60.0 / project.bpm.load() * sr;

    if (sound == Sound::Kick && ! e.click)
        pulse = juce::jmax (pulse.load(), e.velocity);
}

void AudioEngine::releaseLiveNote (int track, int pitch)
{
    for (auto& v : voices)
        if (v.active && v.track == track && v.pitch == pitch && v.gateSamples < 0.0)
            v.gateSamples = 0.0;
}

float AudioEngine::renderVoice (Voice& v)
{
    const double dt = 1.0 / sr;
    const float t = (float) v.t;
    float out = 0.0f;
    auto noise = [this] { return random.nextFloat() * 2.0f - 1.0f; };

    if (v.click)
    {
        v.phase += (v.accent ? 1760.0 : 1320.0) * dt;
        out = (float) std::sin (v.phase * juce::MathConstants<double>::twoPi) * 0.25f * std::exp (-t * 60.0f);
        v.t += dt;
        if (v.t > 0.1) v.active = false;
        return out;
    }

    // Gate envelope for melodic voices
    if (! isDrum (v.sound))
    {
        if (v.gateSamples > 0.0)
            v.gateSamples -= 1.0;
        else if (v.gateSamples > -0.5)
            v.releasing = true;

        if (v.releasing)
            v.env *= (float) std::exp (-dt / releaseTime (v.sound) * 4.6);
        else
            v.env = juce::jmin (1.0f, v.env + (float) (dt / attackTime (v.sound)));

        if (v.releasing && v.env < 0.0005f)
            v.active = false;
    }

    switch (v.sound)
    {
        case Sound::Kick:
        {
            const double f = 42.0 + 118.0 * std::exp (-t * 35.0);
            v.phase += juce::MathConstants<double>::twoPi * f * dt;
            out = (float) std::sin (v.phase) * std::exp (-t * 8.0f);
            break;
        }
        case Sound::Snare:
        {
            v.phase += juce::MathConstants<double>::twoPi * 185.0 * dt;
            const float n = noise();
            const float hp = n - v.prevNoise;
            v.prevNoise = n;
            out = hp * 0.35f * std::exp (-t * 18.0f) + (float) std::sin (v.phase) * 0.3f * std::exp (-t * 30.0f);
            break;
        }
        case Sound::Hat:
        {
            const float n = noise();
            out = (n - v.prevNoise) * 0.12f * std::exp (-t * (v.pitch >= 44 ? 45.0f : 70.0f));
            v.prevNoise = n;
            break;
        }
        case Sound::Clap:
        {
            const float bursts = t < 0.03f ? (std::fmod (t, 0.01f) < 0.004f ? 1.0f : 0.3f) : 1.0f;
            v.lp += 0.25f * (noise() - v.lp);
            out = (noise() - v.lp) * 0.4f * bursts * std::exp (-t * 22.0f);
            break;
        }
        case Sound::Perc:
        {
            const double f = v.freq * (1.0 + 0.6 * std::exp (-t * 40.0));
            v.phase += juce::MathConstants<double>::twoPi * f * dt;
            out = (float) std::sin (v.phase) * 0.5f * std::exp (-t * 16.0f);
            break;
        }
        case Sound::Chop:
        {
            v.phase += v.freq * dt;
            const float tri = 4.0f * std::abs ((float) (v.phase - std::floor (v.phase)) - 0.5f) - 1.0f;
            out = tri * 0.3f * v.env * (0.55f + 0.45f * std::exp (-t * 6.0f));
            break;
        }
        case Sound::Bass:
        {
            v.phase += v.freq * dt;
            const float saw = 2.0f * (float) (v.phase - std::floor (v.phase)) - 1.0f;
            v.lp += 0.06f * (saw - v.lp);
            out = v.lp * 0.6f * v.env;
            break;
        }
        case Sound::Keys:
        {
            v.phase += v.freq * dt;
            const float tri = 4.0f * std::abs ((float) (v.phase - std::floor (v.phase)) - 0.5f) - 1.0f;
            out = tri * 0.12f * v.env * (0.4f + 0.6f * std::exp (-t * 2.5f));
            break;
        }
        case Sound::Pad:
        {
            v.phase  += v.freq * 1.003 * dt;
            v.phase2 += v.freq * 0.997 * dt;
            const float saw = (float) (v.phase - std::floor (v.phase)) + (float) (v.phase2 - std::floor (v.phase2)) - 1.0f;
            v.lp += 0.03f * (saw - v.lp);
            out = v.lp * 0.18f * v.env;
            break;
        }
        case Sound::Silent: break;
    }

    v.t += dt;
    if (isDrum (v.sound) && v.t > 1.2)
        v.active = false;
    if (v.t > 12.0)
        v.active = false;
    return out * v.velocity;
}

void AudioEngine::mixAudioClips (juce::AudioBuffer<float>& out, int startSample, int numSamples, double startBeat,
                                 double beatsPerSample, std::array<float, 32>& peaks)
{
    const double secondsPerBeat = 60.0 / project.bpm.load();
    const double endBeat = startBeat + numSamples * beatsPerSample;

    for (size_t ti = 0; ti < project.tracks.size(); ++ti)
    {
        auto& track = *project.tracks[ti];
        if (! project.isAudible ((int) ti))
            continue;

        const float vol = track.volume.load(), pan = track.pan.load();
        const float gl = vol * std::sqrt (0.5f * (1.0f - pan)), gr = vol * std::sqrt (0.5f * (1.0f + pan));

        for (auto& clip : track.clips)
        {
            if (clip.kind != ClipKind::Audio || clip.audio == nullptr || clip.end() <= startBeat || clip.start >= endBeat)
                continue;

            const auto& data = *clip.audio;
            const int len = data.buffer.getNumSamples();
            const float* srcL = data.buffer.getReadPointer (0);
            const float* srcR = data.buffer.getReadPointer (1);
            float peak = 0.0f;

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
                const float l = srcL[idx] + frac * (srcL[idx + 1] - srcL[idx]);
                const float r = srcR[idx] + frac * (srcR[idx + 1] - srcR[idx]);
                out.addSample (0, startSample + i, l * gl);
                if (out.getNumChannels() > 1)
                    out.addSample (1, startSample + i, r * gr);
                peak = juce::jmax (peak, std::abs (l) * vol);
            }
            if (ti < peaks.size())
                peaks[ti] = juce::jmax (peaks[ti], peak);
        }
    }
}

void AudioEngine::getNextAudioBlock (const juce::AudioSourceChannelInfo& info)
{
    info.clearActiveBufferRegion();
    const int numSamples = info.numSamples;
    events.clear();
    std::array<float, 32> peaks {};

    // A new position from the UI (click on the ruler, stop) takes effect at the block start
    const double req = requestedPosition.exchange (-1.0);
    double pos = req >= 0.0 ? req : position.load();
    if (req >= 0.0)
        for (auto& v : voices)
            if (! v.click && ! isDrum (v.sound))
                v.releasing = true;

    const double beatsPerSample = project.bpm.load() / 60.0 / sr;
    const bool isPlaying = playing.load();

    // Live MIDI: play the selected track, and record it while armed
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
                    if (! project.isAudible ((int) ti) || track.sound == Sound::Silent)
                        continue;
                    for (auto& clip : track.clips)
                    {
                        if (clip.kind != ClipKind::Midi || clip.end() <= pos || clip.start >= segEnd || clip.loopLength <= 0.0)
                            continue;
                        // Which loop repetitions of the clip overlap this segment
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
                }

                if (metronome.load())
                    for (double b = std::ceil (pos - 1e-9); b < segEnd; b += 1.0)
                        events.push_back ({ done + (int) ((b - pos) / beatsPerSample), 0, 0, 1.0f, 0.05, true,
                                            std::fmod (b, beatsPerBar) < 0.001 });

                mixAudioClips (*info.buffer, info.startSample + done, segSamples, pos, beatsPerSample, peaks);

                done += segSamples;
                pos = wrap ? ls : segEnd;
            }
        }
    }

    // Live notes go in at their own offsets
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
            if (isPlaying && recordArmed.load())
                pushRecorded ({ blockStartBeat + meta.samplePosition * beatsPerSample, msg.getNoteNumber(), 0.0f, false });
        }
    }

    std::sort (events.begin(), events.end(), [] (const Event& a, const Event& b) { return a.offset < b.offset; });

    auto* left  = info.buffer->getWritePointer (0, info.startSample);
    auto* right = info.buffer->getNumChannels() > 1 ? info.buffer->getWritePointer (1, info.startSample) : nullptr;
    size_t nextEvent = 0;
    auto nextOff = liveMidi.cbegin();

    for (int s = 0; s < numSamples; ++s)
    {
        while (nextEvent < events.size() && events[nextEvent].offset <= s)
            startVoice (events[nextEvent++]);

        // Release live notes at their note-off time
        while (nextOff != liveMidi.cend() && (*nextOff).samplePosition <= s)
        {
            const auto msg = (*nextOff).getMessage();
            if (msg.isNoteOff())
                releaseLiveNote (live, msg.getNoteNumber());
            ++nextOff;
        }

        float l = 0.0f, r = 0.0f;
        for (auto& v : voices)
        {
            if (! v.active)
                continue;
            if (v.click)
            {
                const float x = renderVoice (v);
                l += x;
                r += x;
                continue;
            }
            auto& t = *project.tracks[(size_t) v.track];
            const float x = renderVoice (v) * t.volume.load();
            const float p = t.pan.load();
            l += x * std::sqrt (0.5f * (1.0f - p));
            r += x * std::sqrt (0.5f * (1.0f + p));
            if (v.track < (int) peaks.size())
                peaks[(size_t) v.track] = std::max (peaks[(size_t) v.track], std::abs (x) * 2.0f);
        }

        left[s] = std::tanh (left[s] + l);
        if (right != nullptr)
            right[s] = std::tanh (right[s] + r);
    }

    if (isPlaying)
        position = pos;

    // Meters fall back smoothly between hits
    for (size_t i = 0; i < project.tracks.size() && i < peaks.size(); ++i)
    {
        auto& m = project.tracks[i]->meter;
        m = std::max (std::min (1.0f, peaks[i]), m.load() * 0.82f);
    }
    pulse = pulse.load() * 0.86f;
}
