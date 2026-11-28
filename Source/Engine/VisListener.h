#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>

// Listens to the sound the visualiser is fed (the master or one channel) on the audio thread:
// finds kicks, snares, hats and other sharp starts, and measures the low, middle and high levels.
// Everything it finds is stamped with the audio clock, so the picture can react at the moment
// the sound is heard. The drawing side reads the results; nothing here waits or allocates.
class VisListener
{
public:
    enum Kind { kick, snare, hat, tone };
    struct Hit   { double time; int kind; float rel; };          // rel: loudness against recent hits of its kind, 0..1
    struct Bands { double time; float low, mid, high; };         // 0..1 each

    void prepare (double sampleRate);
    void push (const float* left, const float* right, int numSamples);   // audio thread

    // Drawing side
    int readHits (Hit* dest, int max);
    int readBands (Bands* dest, int max);
    double audioNow() const;            // the audio clock right now, in seconds, carried on between blocks

    std::atomic<bool> enabled { false };

    // What the low, middle and high levels listen to, in Hz. Set from the panel at any time.
    void setBand (int band, float lowHz, float highHz);

    // The sound as a row of bars from 30 Hz to 16 kHz, evenly spaced by pitch (0..1 each), for the panel
    static constexpr int spectrumBars = 56;
    static constexpr double spectrumLow = 30.0, spectrumHigh = 16000.0;
    std::array<std::atomic<float>, spectrumBars> spectrum {};

private:
    void analyse();
    static constexpr int order = 10, size = 1 << order;
    double sr = 48000.0;
    int hop = 256;
    juce::dsp::FFT fft { order };
    std::array<float, size> ring {}, window {};
    std::array<float, size * 2> work {};
    int writePos = 0, sinceHop = 0;
    juce::int64 samples = 0;
    std::atomic<double> clockAtPush { 0.0 }, wallAtPush { 0.0 };

    static constexpr int bins = 360, history = 4;
    std::array<std::array<float, bins>, history> logSpec {};
    std::array<float, bins> lin {}, level {};
    int specCount = 0;
    int numBins = bins;

    struct Det { float mean = 0.0f, peak = 0.0f, peakEnergy = 1.0e-9f; double last = -9.0; };
    std::array<Det, 4> det;
    struct Pending { int kind; double t0, stamp; float e, ek, eh, es; bool used; };
    std::array<Pending, 8> pending {};
    std::array<int, spectrumBars> barFrom {}, barTo {};
    std::array<std::atomic<float>, 3> bandLow { 35.0f, 200.0f, 3000.0f }, bandHigh { 140.0f, 2000.0f, 12000.0f };

    juce::AbstractFifo hitFifo { 256 }, bandFifo { 1024 };
    std::array<Hit, 256> hits {};
    std::array<Bands, 1024> bands {};
    void pushHit (const Hit&);
};
