#include "AudioEngine.h"

void AudioEngine::prepareToPlay (int, double sampleRate)
{
    sr = sampleRate > 0 ? sampleRate : 44100.0;
}

void AudioEngine::play()
{
    samplesUntilNextStep = 0.0;
    samplesPlayed = 0;
    stepPosition = -1;
    playing = true;
}

void AudioEngine::stop()
{
    playing = false;
    stepPosition = -1;
    for (auto& v : voices)
        v.active = false;
    for (auto& t : project.tracks)
        t->meter = 0.0f;
    pulse = 0.0f;
}

double AudioEngine::getSecondsPlayed() const
{
    return playing.load() ? (double) samplesPlayed / sr : 0.0;
}

void AudioEngine::startVoice (Sound s, int track, float freq)
{
    for (auto& v : voices)
    {
        if (! v.active)
        {
            v = Voice {};
            v.active = true;
            v.sound = s;
            v.track = track;
            v.freq = freq;
            return;
        }
    }
}

void AudioEngine::triggerStep (int step)
{
    static constexpr float chopNotes[] = { 523.25f, 622.25f, 783.99f, 932.33f };

    for (size_t i = 0; i < project.tracks.size(); ++i)
    {
        auto& t = *project.tracks[i];
        if (! t.stepOn (step) || ! project.isAudible ((int) i))
            continue;

        switch (t.sound)
        {
            case Sound::Chop: startVoice (Sound::Chop, (int) i, chopNotes[step % 4]); break;
            case Sound::Bass: startVoice (Sound::Bass, (int) i, step < 8 ? 55.0f : 49.0f); break;
            case Sound::Keys:
                for (float f : { 220.0f, 261.63f, 329.63f })
                    startVoice (Sound::Keys, (int) i, f);
                break;
            case Sound::Silent: t.meter = 1.0f; break;   // visual-only track for now
            case Sound::Kick:
            case Sound::Snare:
            case Sound::Hat:
            case Sound::Clap:   startVoice (t.sound, (int) i, 0.0f); break;
        }

        if (t.sound == Sound::Kick)
            pulse = 1.0f;
    }
}

float AudioEngine::renderVoice (Voice& v)
{
    const double dt = 1.0 / sr;
    const float t = (float) v.t;
    float out = 0.0f;
    auto noise = [this] { return random.nextFloat() * 2.0f - 1.0f; };

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
            out = (n - v.prevNoise) * 0.12f * std::exp (-t * 70.0f);
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
        case Sound::Chop:
        {
            v.phase += v.freq * dt;
            const float tri = 4.0f * std::abs ((float) (v.phase - std::floor (v.phase)) - 0.5f) - 1.0f;
            out = tri * 0.22f * std::exp (-t * 9.0f);
            break;
        }
        case Sound::Bass:
        {
            v.phase += v.freq * dt;
            const float saw = 2.0f * (float) (v.phase - std::floor (v.phase)) - 1.0f;
            v.lp += 0.06f * (saw - v.lp);
            out = v.lp * 0.5f * std::exp (-t * 3.5f);
            break;
        }
        case Sound::Keys:
        {
            v.phase += v.freq * dt;
            const float tri = 4.0f * std::abs ((float) (v.phase - std::floor (v.phase)) - 0.5f) - 1.0f;
            out = tri * 0.07f * std::exp (-t * 3.0f);
            break;
        }
        case Sound::Silent: break;
    }

    v.t += dt;
    if (v.t > 1.2)
        v.active = false;
    return out;
}

void AudioEngine::getNextAudioBlock (const juce::AudioSourceChannelInfo& info)
{
    info.clearActiveBufferRegion();
    if (! playing.load())
        return;

    auto* left  = info.buffer->getWritePointer (0, info.startSample);
    auto* right = info.buffer->getNumChannels() > 1 ? info.buffer->getWritePointer (1, info.startSample) : nullptr;

    const double samplesPerStep = sr * 60.0 / project.bpm.load() / 4.0;
    std::array<float, 32> peaks {};

    for (int s = 0; s < info.numSamples; ++s)
    {
        if (samplesUntilNextStep <= 0.0)
        {
            const auto pos = stepPosition.load() + 1;
            stepPosition = pos;
            triggerStep ((int) (pos % Project::stepsPerBar));
            samplesUntilNextStep += samplesPerStep;
        }
        samplesUntilNextStep -= 1.0;
        ++samplesPlayed;

        float l = 0.0f, r = 0.0f;
        for (auto& v : voices)
        {
            if (! v.active)
                continue;
            auto& t = *project.tracks[(size_t) v.track];
            const float x = renderVoice (v) * t.volume.load();
            const float p = t.pan.load();
            l += x * std::sqrt (0.5f * (1.0f - p));
            r += x * std::sqrt (0.5f * (1.0f + p));
            if (v.track < (int) peaks.size())
                peaks[(size_t) v.track] = std::max (peaks[(size_t) v.track], std::abs (x) * 2.0f);
        }

        left[s] = std::tanh (l);
        if (right != nullptr)
            right[s] = std::tanh (r);
    }

    // Meters fall back smoothly between hits
    for (size_t i = 0; i < project.tracks.size() && i < peaks.size(); ++i)
    {
        auto& m = project.tracks[i]->meter;
        m = std::max (std::min (1.0f, peaks[i]), m.load() * 0.82f);
    }
    pulse = pulse.load() * 0.86f;
}
