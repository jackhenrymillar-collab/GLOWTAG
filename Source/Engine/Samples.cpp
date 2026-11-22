#include "Samples.h"
#include "../DSP/Analog.h"

namespace
{
    constexpr double sr = 48000.0;
    std::shared_ptr<AudioData> finish (std::shared_ptr<AudioData>);

    std::shared_ptr<AudioData> make (const juce::String& name, double seconds, std::function<float (float t, juce::Random&)> gen)
    {
        auto d = std::make_shared<AudioData>();
        d->sampleRate = sr;
        d->fileName = name;
        const int n = (int) (seconds * sr);
        d->buffer.setSize (2, n);
        juce::Random rng (name.hashCode());
        float peak = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float v = gen ((float) i / (float) sr, rng);
            d->buffer.setSample (0, i, v);
            peak = juce::jmax (peak, std::abs (v));
        }
        if (peak > 0.0f)
            d->buffer.applyGain (0, 0, n, 0.9f / peak);
        // Short fade out so nothing clicks
        d->buffer.applyGainRamp (0, juce::jmax (0, n - 64), juce::jmin (64, n), 1.0f, 0.0f);
        d->buffer.copyFrom (1, 0, d->buffer, 0, 0, n);
        return finish (d);
    }

    std::shared_ptr<AudioData> finish (std::shared_ptr<AudioData> d)
    {
        const int n = d->buffer.getNumSamples();
        const int numPeaks = 256;
        d->peaks.resize (numPeaks);
        for (int p = 0; p < numPeaks; ++p)
        {
            const int a = (int) ((juce::int64) n * p / numPeaks), b = juce::jmax (a + 1, (int) ((juce::int64) n * (p + 1) / numPeaks));
            d->peaks[(size_t) p] = d->buffer.getMagnitude (a, juce::jmin (b, n) - a);
        }
        d->findOnsets();
        return d;
    }

    float noise (juce::Random& r) { return r.nextFloat() * 2.0f - 1.0f; }
    float env (float t, float rate) { return std::exp (-t * rate); }

    // A vowel: a buzzy source through three formant peaks
    std::function<float (float, juce::Random&)> vowel (float f0, float f1, float f2, float f3, float decay)
    {
        struct State { analog::Biquad b1, b2, b3; analog::Oscillator osc; bool init = false; };
        auto s = std::make_shared<State>();
        return [=] (float t, juce::Random&) mutable
        {
            if (! s->init)
            {
                s->init = true;
                s->osc.prepare (sr);
                s->b1.setPeak (sr, f1, 18.0f, 6.0f);
                s->b2.setPeak (sr, f2, 14.0f, 8.0f);
                s->b3.setPeak (sr, f3, 8.0f, 8.0f);
            }
            s->osc.setFrequency (f0 * (1.0f + 0.006f * std::sin (t * 5.5f)));   // a little vibrato
            const float src = s->osc.process (0.0f);
            const float v = (s->b1.process (src) + s->b2.process (src) + s->b3.process (src)) * 0.05f;
            const float a = juce::jmin (1.0f, t * 30.0f);
            return v * a * env (t, decay);
        };
    }


    // "Found Record": two bars of a soul loop at 90 bpm (electric piano chords, bass, vibes, a break),
    // pressed a little hot. The Crate Digger digs through it until you load a record of your own.
    std::shared_ptr<AudioData> foundRecord()
    {
        const double bt = 60.0 / 90.0;
        const int N = (int) std::round (8.0 * bt * sr);
        auto d = std::make_shared<AudioData>();
        d->sampleRate = sr;
        d->fileName = "Found Record";
        d->buffer.setSize (2, N);
        d->buffer.clear();
        auto* L = d->buffer.getWritePointer (0);
        auto* R = d->buffer.getWritePointer (1);
        juce::Random rng (3);
        auto rnd = [&rng] { return rng.nextFloat() * 2.0f - 1.0f; };
        auto mtof = [] (double n) { return 440.0 * std::pow (2.0, (n - 69.0) / 12.0); };
        auto put = [&] (double t0, double len, const std::function<std::pair<float, float> (double, int)>& fn)
        {
            const int s0 = (int) std::round (t0 * sr), n = (int) std::round (len * sr);
            for (int i = 0; i < n && s0 + i < N; ++i)
            {
                const auto v = fn (i / sr, i);
                L[s0 + i] += v.first;
                R[s0 + i] += v.second;
            }
        };
        auto ep = [&] (int note, double t0, double len, float pan)
        {
            const double w = analog::twoPi * mtof (note);
            put (t0, len + 0.35, [=] (double x, int)
            {
                const double env = juce::jmin (1.0, x / 0.004) * std::exp (-x / 1.1) * (x > len ? std::exp (-(x - len) / 0.07) : 1.0);
                const double tr = 1.0 + 0.2 * std::sin (analog::twoPi * 5.2 * x);
                const double v = (std::sin (w * x) + 0.42 * std::sin (2 * w * x) + 0.12 * std::sin (3 * w * x) + 0.22 * std::sin (7.1 * w * x) * std::exp (-x / 0.04)) * env * 0.07;
                return std::pair<float, float> ((float) (v * (1 - pan) * tr), (float) (v * (1 + pan) * (2 - tr)));
            });
        };
        auto bass = [&] (int note, double t0, double len)
        {
            const double w = analog::twoPi * mtof (note);
            put (t0, len + 0.1, [=] (double x, int)
            {
                const double env = juce::jmin (1.0, x / 0.006) * std::exp (-x / 0.5) * (x > len ? std::exp (-(x - len) / 0.03) : 1.0);
                const float v = (float) ((std::sin (w * x) + 0.3 * std::sin (2 * w * x)) * env * 0.3);
                return std::pair<float, float> (v, v);
            });
        };
        auto vib = [&] (int note, double t0)
        {
            const double w = analog::twoPi * mtof (note);
            put (t0, 1.0, [=] (double x, int)
            {
                const double v = (std::sin (w * x) + 0.3 * std::sin (4 * w * x) * std::exp (-x / 0.08)) * std::exp (-x / 0.45) * 0.08 * (1 + 0.3 * std::sin (analog::twoPi * 6 * x));
                return std::pair<float, float> ((float) (v * 0.7), (float) (v * 1.2));
            });
        };
        auto kick = [&] (double t)
        {
            double ph = 0.0;
            put (t, 0.45, [&] (double x, int i)
            {
                ph += analog::twoPi * (48 + 90 * std::exp (-x / 0.03)) / sr;
                const float v = (float) ((std::sin (ph) * std::exp (-x / 0.15) + (i < 90 ? rnd() * 0.3 : 0.0)) * 0.75);
                return std::pair<float, float> (v, v);
            });
        };
        auto snare = [&] (double t, float g)
        {
            float pr = 0.0f;
            put (t, 0.35, [&] (double x, int)
            {
                const float n = rnd(), hp = n - pr;
                pr = n;
                const float v = (float) ((std::sin (analog::twoPi * 190 * x) * std::exp (-x / 0.045) * 0.35 + (n * 0.4 + hp * 0.3) * std::exp (-x / 0.1) * 0.5) * g);
                return std::pair<float, float> (v, v * 0.95f);
            });
        };
        auto hat = [&] (double t, float g)
        {
            float pr = 0.0f;
            put (t, 0.12, [&] (double x, int)
            {
                const float n = rnd(), hp = n - pr;
                pr = n;
                const float v = (float) (hp * std::exp (-x / 0.025) * 0.16 * g);
                return std::pair<float, float> (v * 0.85f, v * 1.1f);
            });
        };
        const int chords[4][5] = { { 41, 56, 60, 63, 67 }, { 37, 56, 60, 61, 65 }, { 46, 53, 56, 60, 61 }, { 36, 55, 58, 60, 65 } };
        for (int k = 0; k < 4; ++k)
        {
            const double t0 = k * 2 * bt;
            for (int j = 0; j < 4; ++j)
            {
                ep (chords[k][j + 1], t0 + j * 0.006, 1.35 * bt, j % 2 ? 0.35f : -0.35f);
                ep (chords[k][j + 1], t0 + 1.5 * bt, 0.4 * bt, j % 2 ? -0.25f : 0.25f);
            }
            bass (chords[k][0], t0, 0.9 * bt);
            bass (chords[k][0], t0 + 1.5 * bt, 0.35 * bt);
            bass (chords[k][0] + 12, t0 + 1.75 * bt, 0.2 * bt);
        }
        const int vibes[5] = { 72, 70, 67, 65, 63 };
        const double vibeAt[5] = { 0, 0.5, 1, 2, 2.5 };
        for (int i = 0; i < 5; ++i) vib (vibes[i], (4.5 + vibeAt[i]) * bt);
        for (int bar = 0; bar < 2; ++bar)
        {
            const double o = bar * 4 * bt;
            for (double b : { 0.0, 1.5, 2.75 }) kick (o + b * bt);
            for (double b : { 1.0, 3.0 }) snare (o + b * bt, 1.0f);
            snare (o + 3.75 * bt, 0.3f);
            for (int h = 0; h < 8; ++h) hat (o + h * 0.5 * bt + (h % 2 ? 0.08 * bt : 0.0), h % 2 ? 0.7f : 1.0f);
        }
        const float peak = juce::jmax (d->buffer.getMagnitude (0, 0, N), d->buffer.getMagnitude (1, 0, N));
        const float k = 1.15f / juce::jmax (1e-6f, peak);
        for (int i = 0; i < N; ++i) { L[i] = std::tanh (L[i] * k); R[i] = std::tanh (R[i] * k); }
        return finish (d);
    }

    std::vector<Samples::Entry> build()
    {
        std::vector<Samples::Entry> v;
        auto add = [&v] (const char* name, const char* cat, double secs, std::function<float (float, juce::Random&)> gen)
        {
            v.push_back ({ name, cat, make (name, secs, gen) });
        };

        add ("Kick 808", "Drums", 0.7, [p = 0.0] (float t, juce::Random&) mutable
        {
            const double f = 44.0 + 110.0 * std::exp (-t * 30.0);
            p += analog::twoPi * f / sr;
            return analog::fastTanh ((float) std::sin (p) * 1.6f) * env (t, 5.5f) + 0.3f * (float) std::sin (p * 1.0) * env (t, 60.0f);
        });
        add ("Kick Tight", "Drums", 0.35, [p = 0.0] (float t, juce::Random&) mutable
        {
            const double f = 55.0 + 160.0 * std::exp (-t * 45.0);
            p += analog::twoPi * f / sr;
            return (float) std::sin (p) * env (t, 14.0f);
        });
        add ("Snare Crack", "Drums", 0.4, [p = 0.0, lp = 0.0f] (float t, juce::Random& r) mutable
        {
            p += analog::twoPi * 190.0 / sr;
            const float n = noise (r);
            lp += 0.35f * (n - lp);
            return (n - lp) * 0.7f * env (t, 16.0f) + (float) std::sin (p) * 0.5f * env (t, 32.0f) + lp * 0.3f * env (t, 10.0f);
        });
        add ("Rim", "Drums", 0.15, [p = 0.0] (float t, juce::Random& r) mutable
        {
            p += analog::twoPi * 820.0 / sr;
            return ((float) std::sin (p) * 0.6f + noise (r) * 0.4f) * env (t, 60.0f);
        });
        add ("Clap Stack", "Drums", 0.5, [lp = 0.0f] (float t, juce::Random& r) mutable
        {
            const float bursts = t < 0.032f ? (std::fmod (t, 0.011f) < 0.005f ? 1.0f : 0.25f) : 1.0f;
            lp += 0.22f * (noise (r) - lp);
            return (noise (r) - lp) * bursts * env (t, 18.0f);
        });
        add ("Hat Closed", "Drums", 0.15, [hp = 0.0f] (float t, juce::Random& r) mutable
        {
            const float n = noise (r);
            const float out = n - hp;
            hp = n;
            return out * env (t, 55.0f);
        });
        add ("Hat Open", "Drums", 0.6, [hp = 0.0f] (float t, juce::Random& r) mutable
        {
            const float n = noise (r);
            const float out = n - hp;
            hp = n;
            return out * env (t, 7.0f);
        });
        add ("Shaker", "Drums", 0.2, [lp = 0.0f, lp2 = 0.0f] (float t, juce::Random& r) mutable
        {
            lp += 0.5f * (noise (r) - lp);
            lp2 += 0.5f * (lp - lp2);
            return (lp - lp2) * 3.0f * juce::jmin (1.0f, t * 200.0f) * env (t, 28.0f);
        });
        auto tom = [] (float hz, float decay) { return [p = 0.0, hz, decay] (float t, juce::Random& r) mutable
        {
            p += analog::twoPi * hz * (1.0 + 0.5 * std::exp (-t * 30.0)) / sr;
            return (float) std::sin (p) * env (t, decay) + noise (r) * 0.15f * env (t, 50.0f);
        }; };
        add ("Tom Low", "Drums", 0.6, tom (90.0f, 7.0f));
        add ("Tom High", "Drums", 0.45, tom (160.0f, 10.0f));
        add ("Conga Low", "Drums", 0.4, tom (190.0f, 14.0f));
        add ("Conga High", "Drums", 0.3, tom (260.0f, 18.0f));
        add ("Cowbell", "Drums", 0.35, [p1 = 0.0, p2 = 0.0] (float t, juce::Random&) mutable
        {
            p1 += analog::twoPi * 560.0 / sr;
            p2 += analog::twoPi * 845.0 / sr;
            const float sq = ((std::fmod (p1, analog::twoPi) < analog::pi) ? 1.0f : -1.0f) + ((std::fmod (p2, analog::twoPi) < analog::pi) ? 1.0f : -1.0f);
            return sq * 0.4f * env (t, 12.0f);
        });
        add ("Crash", "Drums", 1.6, [hp = 0.0f, lp = 0.0f] (float t, juce::Random& r) mutable
        {
            const float n = noise (r);
            lp += 0.6f * (n - lp);
            const float out = lp - hp;
            hp = lp;
            return out * 2.0f * env (t, 2.2f);
        });
        add ("Ride", "Drums", 1.0, [p = 0.0, hp = 0.0f] (float t, juce::Random& r) mutable
        {
            p += analog::twoPi * 3200.0 / sr;
            const float n = noise (r);
            const float out = n - hp;
            hp = n;
            return (out * 0.5f + (float) std::sin (p) * 0.3f) * env (t, 4.0f);
        });
        add ("Sub Drop", "FX", 1.2, [p = 0.0] (float t, juce::Random&) mutable
        {
            const double f = 30.0 + 90.0 * std::exp (-t * 2.5);
            p += analog::twoPi * f / sr;
            return (float) std::sin (p) * juce::jmin (1.0f, t * 40.0f) * env (t, 2.0f);
        });

        add ("Vox Ahh", "Vocals", 1.4, vowel (220.0f, 800.0f, 1150.0f, 2900.0f, 1.2f));
        add ("Vox Ooh", "Vocals", 1.4, vowel (196.0f, 350.0f, 650.0f, 2600.0f, 1.0f));
        add ("Vox Eee", "Vocals", 1.0, vowel (261.6f, 300.0f, 2300.0f, 3000.0f, 1.6f));
        add ("Choir Pad", "Texture", 2.5, [a = vowel (130.8f, 450.0f, 1000.0f, 2500.0f, 0.4f), b = vowel (196.0f, 450.0f, 1000.0f, 2500.0f, 0.4f)] (float t, juce::Random& r) mutable
        {
            return (a (t, r) + b (t, r)) * 0.5f;
        });
        add ("Vinyl Crackle", "Texture", 2.0, [lp = 0.0f] (float t, juce::Random& r) mutable
        {
            lp += 0.02f * (noise (r) - lp);
            const float pop = r.nextFloat() < 0.0015f ? noise (r) * 2.0f : 0.0f;
            return lp * 1.5f + pop * env (std::fmod (t, 0.01f), 300.0f);
        });
        v.push_back ({ "Found Record", "Loops", foundRecord() });
        return v;
    }
}

namespace Samples
{
    const std::vector<Entry>& bank()
    {
        static const std::vector<Entry> b = build();
        return b;
    }

    std::shared_ptr<const AudioData> find (const juce::String& name)
    {
        for (auto& e : bank())
            if (e.name == name)
                return e.data;
        return nullptr;
    }

    const std::array<juce::String, 16>& defaultPads()
    {
        static const std::array<juce::String, 16> pads = {
            "Kick 808", "Rim", "Snare Crack", "Clap Stack", "Kick Tight", "Tom Low", "Hat Closed", "Tom High",
            "Shaker", "Conga Low", "Hat Open", "Conga High", "Cowbell", "Crash", "Sub Drop", "Ride" };
        return pads;
    }
}
