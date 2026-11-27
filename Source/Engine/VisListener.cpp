#include "VisListener.h"

void VisListener::prepare (double sampleRate)
{
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    hop = juce::jlimit (64, size, juce::roundToInt (sr / 187.5));      // about 5 ms
    for (int i = 0; i < size; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (size - 1));
    numBins = juce::jmin (bins, (int) std::ceil (15000.0 / (sr / size)));
    for (int i = 0; i < spectrumBars; ++i)
    {
        const double f0 = spectrumLow * std::pow (spectrumHigh / spectrumLow, (double) i / spectrumBars);
        const double f1 = spectrumLow * std::pow (spectrumHigh / spectrumLow, (double) (i + 1) / spectrumBars);
        barFrom[(size_t) i] = juce::jlimit (1, numBins - 1, (int) std::floor (f0 / (sr / size)));
        barTo[(size_t) i] = juce::jlimit (barFrom[(size_t) i], numBins - 1, (int) std::ceil (f1 / (sr / size)));
    }
    ring.fill (0.0f);
    writePos = sinceHop = specCount = 0;
    for (auto& d : det) d = {};
    for (auto& p : pending) p.used = false;
}

void VisListener::push (const float* left, const float* right, int numSamples)
{
    if (! enabled.load (std::memory_order_relaxed)) return;
    for (int i = 0; i < numSamples; ++i)
    {
        ring[(size_t) writePos] = right != nullptr ? 0.5f * (left[i] + right[i]) : left[i];
        writePos = (writePos + 1) & (size - 1);
        ++samples;
        if (++sinceHop >= hop) { sinceHop = 0; analyse(); }
    }
    clockAtPush.store ((double) samples / sr);
    wallAtPush.store (juce::Time::getMillisecondCounterHiRes() * 0.001);
}

void VisListener::setBand (int band, float lowHz, float highHz)
{
    if (band < 0 || band > 2) return;
    bandLow[(size_t) band].store (juce::jlimit (20.0f, 20000.0f, lowHz));
    bandHigh[(size_t) band].store (juce::jlimit (20.0f, 20000.0f, juce::jmax (lowHz, highHz)));
}

double VisListener::audioNow() const
{
    const double ahead = juce::Time::getMillisecondCounterHiRes() * 0.001 - wallAtPush.load();
    return clockAtPush.load() + juce::jlimit (0.0, 0.1, ahead);
}

void VisListener::pushHit (const Hit& h)
{
    const auto scope = hitFifo.write (1);
    if (scope.blockSize1 > 0) hits[(size_t) scope.startIndex1] = h;
}

int VisListener::readHits (Hit* dest, int max)
{
    const auto scope = hitFifo.read (juce::jmin (max, hitFifo.getNumReady()));
    int n = 0;
    for (int i = 0; i < scope.blockSize1; ++i) dest[n++] = hits[(size_t) (scope.startIndex1 + i)];
    for (int i = 0; i < scope.blockSize2; ++i) dest[n++] = hits[(size_t) (scope.startIndex2 + i)];
    return n;
}

int VisListener::readBands (Bands* dest, int max)
{
    const auto scope = bandFifo.read (juce::jmin (max, bandFifo.getNumReady()));
    int n = 0;
    for (int i = 0; i < scope.blockSize1; ++i) dest[n++] = bands[(size_t) (scope.startIndex1 + i)];
    for (int i = 0; i < scope.blockSize2; ++i) dest[n++] = bands[(size_t) (scope.startIndex2 + i)];
    return n;
}

// One look at the last 21 ms of sound. A hit is a sudden rise in one part of the spectrum
// (the rise is measured on a compressed scale, so quiet and loud songs behave alike).
void VisListener::analyse()
{
    for (int i = 0; i < size; ++i)
        work[(size_t) i] = ring[(size_t) ((writePos + i) & (size - 1))] * window[(size_t) i];
    std::fill (work.begin() + size, work.end(), 0.0f);
    fft.performFrequencyOnlyForwardTransform (work.data(), true);

    const double hz = sr / size, now = (double) samples / sr, dt = (double) hop / sr;
    auto& cur = logSpec[(size_t) (specCount % history)];
    for (int i = 0; i < numBins; ++i)
    {
        const float m = work[(size_t) i] * (0.84f / (float) size);
        lin[(size_t) i] = m;
        cur[(size_t) i] = std::log1p (m * 400.0f);
        level[(size_t) i] = juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (m, -100.0f) + 100.0f) / 70.0f);
    }
    const bool haveRef = specCount >= history - 1;
    const auto& ref = logSpec[(size_t) ((specCount + 1) % history)];       // three looks ago, about 16 ms
    ++specCount;

    auto average = [&] (double a, double b)
    {
        const int i0 = juce::jmax (1, (int) std::floor (a / hz)), i1 = juce::jmin (numBins - 1, (int) std::ceil (b / hz));
        float s = 0.0f;
        for (int i = i0; i <= i1; ++i) s += level[(size_t) i];
        return i1 >= i0 ? s / (float) (i1 - i0 + 1) : 0.0f;
    };
    {
        const auto scope = bandFifo.write (1);
        if (scope.blockSize1 > 0)
            bands[(size_t) scope.startIndex1] = { now, average (bandLow[0].load(), bandHigh[0].load()),
                                                       average (bandLow[1].load(), bandHigh[1].load()),
                                                       average (bandLow[2].load(), bandHigh[2].load()) };
    }
    for (int i = 0; i < spectrumBars; ++i)
    {
        float top = 0.0f;
        for (int b = barFrom[(size_t) i]; b <= barTo[(size_t) i]; ++b) top = juce::jmax (top, level[(size_t) b]);
        spectrum[(size_t) i].store (top, std::memory_order_relaxed);
    }

    static constexpr double range[4][2] = { { 35, 130 }, { 1500, 6000 }, { 7000, 14000 }, { 180, 1400 } };
    float flux[4], energy[4], threshold[4];
    bool ok[4];
    for (int k = 0; k < 4; ++k)
    {
        const int i0 = juce::jmax (1, juce::roundToInt (range[k][0] / hz)), i1 = juce::jmax (i0, juce::jmin (numBins - 1, juce::roundToInt (range[k][1] / hz)));
        float f = 0.0f, e = 0.0f;
        for (int i = i0; i <= i1; ++i)
        {
            if (haveRef) f += juce::jmax (0.0f, cur[(size_t) i] - ref[(size_t) i]);
            e += lin[(size_t) i];
        }
        flux[k] = f / (float) (i1 - i0 + 1);
        energy[k] = e / (float) (i1 - i0 + 1);
    }

    // A found hit waits a moment so its full loudness is known, then goes out
    for (auto& p : pending)
    {
        if (! p.used) continue;
        p.e = juce::jmax (p.e, energy[p.kind]);
        p.ek = juce::jmax (p.ek, energy[kick]);
        p.eh = juce::jmax (p.eh, energy[hat]);
        p.es = juce::jmax (p.es, energy[snare]);
        if (now - p.t0 < 0.016) continue;
        p.used = false;
        if (p.kind == kick && p.e < p.es * 2.0f) continue;                // as loud up high as down low: a burst of noise, not a kick
        if (p.kind == snare)
        {
            if (p.e < p.ek * 0.008f) continue;                           // only the click of a kick
            if (p.e < p.eh)                                              // brighter than a snare: a hat
            {
                p.kind = hat; p.e = p.eh;
                if (now - det[hat].last < 0.09) continue;
                det[hat].last = now;
            }
        }
        auto& d = det[(size_t) p.kind];
        d.peakEnergy = juce::jmax (d.peakEnergy, p.e);
        pushHit ({ p.stamp, p.kind, juce::jmin (1.0f, p.e / d.peakEnergy) });
    }

    for (int k = 0; k < 4; ++k)
    {
        auto& d = det[(size_t) k];
        threshold[k] = juce::jmax (d.mean * 2.5f + 0.004f, d.peak * 0.25f);
        ok[k] = haveRef && flux[k] > threshold[k] && now - d.last > 0.09;
        d.mean += (flux[k] - d.mean) * (float) (1.0 - std::exp (-dt / 1.2));
        d.peak = juce::jmax (flux[k], d.peak * (float) std::exp (-dt / 8.0));
        d.peakEnergy *= (float) std::exp (-dt / 12.0);
    }
    // A kick's click is not a snare; a snare's fizz is not a hat
    if (ok[snare] && (ok[kick] || now - det[kick].last < 0.04) && flux[snare] < threshold[snare] * 2.0f) ok[snare] = false;
    if (ok[hat] && (ok[snare] || now - det[snare].last < 0.06 || flux[snare] > threshold[snare] * 0.7f)) ok[hat] = false;
    if (ok[snare]) for (auto& p : pending) if (p.used && p.kind == hat) p.used = false;
    if (ok[tone] && (ok[kick] || ok[snare] || now - det[kick].last < 0.05 || now - det[snare].last < 0.05)) ok[tone] = false;

    for (int k = 0; k < 4; ++k)
        if (ok[k])
        {
            det[(size_t) k].last = now;
            for (auto& p : pending)
                if (! p.used)
                {
                    // The rise shows a few milliseconds after the sound really starts
                    p = { k, now, now - (k == kick ? 0.008 : 0.005), energy[k], energy[kick], energy[hat], energy[snare], true };
                    break;
                }
        }
}
