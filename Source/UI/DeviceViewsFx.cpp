#include "DeviceViewsCommon.h"
#include "../Engine/Samples.h"
#include <juce_dsp/juce_dsp.h>

// The seven effects (the Drum Rack's wall is in DeviceViewsDrums.cpp)
namespace DeviceViews
{
namespace
{
    const auto pctF = [] (float v) { return F::pct (v); };
    const auto dbF  = [] (float v) { return F::db (v); };
    float clampf (float v, float lo = 0.0f, float hi = 1.0f) { return juce::jlimit (lo, hi, v); }
    std::optional<P> scratchPoint (Group* g, const char* key)
    {
        auto& s = g->scratch[key];
        if (s.size() < 2) return std::nullopt;
        return P (s[0], s[1]);
    }

    // The spectrum of a node's latest output, levels 0..1 the way a browser analyser shows them
    struct Spectrum
    {
        juce::dsp::FFT fft { 11 };
        std::array<float, 4096> buf {};
        std::array<float, 1024> mag {};
        double sr = 44100.0;
        void update (const Node* n, double sampleRate)
        {
            sr = sampleRate > 0 ? sampleRate : 44100.0;
            if (n == nullptr) { mag.fill (0.0f); return; }
            n->scope.read (buf.data(), 2048);
            for (int i = 0; i < 2048; ++i) buf[(size_t) i] *= 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / 2047.0f);
            std::fill (buf.begin() + 2048, buf.end(), 0.0f);
            fft.performFrequencyOnlyForwardTransform (buf.data());
            for (size_t i = 0; i < mag.size(); ++i)
            {
                const float db = 20.0f * std::log10 (juce::jmax (1e-9f, buf[i] / 1024.0f));
                const float v = clampf ((db + 100.0f) / 70.0f);
                mag[i] = juce::jmax (v, mag[i] * 0.8f);       // the analyser's smoothing
            }
        }
        float at (float hz) const { return mag[(size_t) juce::jlimit (0, 1023, juce::roundToInt (hz / (float) (sr / 2.0) * 1024.0f))]; }
    };

    // A log-frequency plot with its grid
    struct Plot
    {
        float x0, y0, w, h, lo, hi;
        float X (float f) const { return x0 + std::log (juce::jlimit (20.0f, 20000.0f, f) / 20.0f) / std::log (1000.0f) * w; }
        float Y (float db) const { return y0 + h * (1.0f - (juce::jlimit (lo, hi, db) - lo) / (hi - lo)); }
        Line curve (const std::function<float (float)>& fn) const
        {
            Line l;
            for (int i = 0; i <= 140; ++i) { const float f = 20.0f * std::pow (1000.0f, (float) i / 140.0f); l.push_back ({ X (f), Y (fn (f)) }); }
            return l;
        }
    };
    Plot fplot (juce::Graphics& g, float x0, float y0, float w, float h, float lo, float hi)
    {
        Plot p { x0, y0, w, h, lo, hi };
        juce::Path grid;
        static const std::pair<const char*, float> marks[] = { { "50", 50 }, { "100", 100 }, { "200", 200 }, { "500", 500 }, { "1k", 1000 }, { "2k", 2000 }, { "5k", 5000 }, { "10k", 10000 } };
        for (auto& [s, f] : marks) { grid.startNewSubPath (p.X (f), y0 + 8); grid.lineTo (p.X (f), y0 + h); }
        grid.startNewSubPath (x0, p.Y (0)); grid.lineTo (x0 + w, p.Y (0));
        dashes (g, grid);
        for (auto& [s, f] : marks) label (g, s, p.X (f) + 4, y0 + h - 3);
        return p;
    }
    void specDots (juce::Graphics& g, const Spectrum& s, const Plot& P, float y0, float h, Colour col)
    {
        const int N = 46, R = 13;
        const float dy = h * 0.76f / (float) R;
        juce::Path off, on, lit;
        for (int i = 0; i < N; ++i)
        {
            const float f = 25.0f * std::pow (720.0f, (float) i / (float) (N - 1)), x = P.X (f);
            const int n = juce::roundToInt (s.at (f) * (float) R);
            for (int k = 0; k < R; ++k)
            {
                const float y = y0 + h - 24 - (float) k * dy;
                (k < n ? on : off).addEllipse (x - 2.8f, y - 2.8f, 5.6f, 5.6f);
                if (k < n) lit.addEllipse (x - 4.5f, y - 4.5f, 9, 9);
            }
        }
        g.setColour (juce::Colours::white.withAlpha (0.055f)); g.fillPath (off);
        glow().add (lit, rgba (col, 0.5f), 0.0f);
        g.setColour (mix (col, C::white, 0.35f).withAlpha (0.8f)); g.fillPath (on);
    }

    // Draw with everything at `alpha`, the way the walls fade a whole tube
    template <typename Fn> void faded (juce::Graphics& g, float alpha, Fn&& fn)
    {
        g.beginTransparencyLayer (clampf (alpha));
        const float was = glow().alpha;
        glow().alpha = was * clampf (alpha);
        fn();
        glow().alpha = was;
        g.endTransparencyLayer();
    }

    Line arcPoints (int n, std::function<P (float u)> f) { Line l; for (int k = 0; k <= n; ++k) l.push_back (f ((float) k / (float) n)); return l; }
}

//==============================================================================
// Filter

void filterWall (Builder& b)
{
    using namespace Param::Filter;
    auto* dev = &b.d;
    auto& wall = b.wall;
    b.out.pal[0] = C::CY; b.out.pal[1] = C::BL; b.out.pal[2] = C::PK;
    b.out.parts = { C::CY, C::CY, C::BL, C::PK };
    enum { A = 1, B, Cc };
    b.title ("Filter", 330, [] { return String ("ladder low pass to high pass"); });

    auto spectrum = std::make_shared<Spectrum>();
    auto follow = std::make_shared<float> (0.0f);
    auto lfoHz = [dev] { return expMap (dev->get (Rate), 0.05f, 20); };
    // Where the cutoff is right now: the wobble and the follower move it
    auto live = [dev, follow, lfoHz] (double t)
    {
        const float base = expMap (dev->get (Cutoff), 20, 18000);
        if (auto* n = dev->live()) if (const float m = n->meter[0].load(); m > 0.0f) return m;
        return juce::jlimit (25.0f, 19500.0f, base * std::pow (2.0f, dev->get (Wobble) * std::sin (juce::MathConstants<float>::twoPi * (float) t * lfoHz()) * 2 + dev->get (Follow) * *follow * 5));
    };
    auto morph = [dev] (float f, float fc)
    {
        const float x = f / fc, res = dev->get (Res), m = dev->get (Mode);
        if (dev->choice (Slope) == 1)
        {
            const float Q = 0.6f + res * 11, den = juce::jmax (0.02f, std::hypot (1 - x * x, x / Q));
            return (1 - m) / den + m * x * x / den;
        }
        const float k = res * 4, r = 1 - x * x, i = 2 * x, dr = r * r - i * i + k, di = 2 * r * i, dd = juce::jmax (4e-4f, dr * dr + di * di);
        const float gn = std::pow (1 + k, 0.75f), hr = gn * dr / dd, hi = -gn * di / dd;
        return std::hypot (hr + m * (1 - 2 * hr), hi - m * 2 * hi);
    };
    auto magDb = [dev, morph] (float fc)
    {
        return [dev, morph, fc] (float f)
        {
            const float v = dev->get (Vowel);
            const float vw = 1 + v * 2.4f * (std::exp (-std::pow (std::log (f / (fc * 0.45f)) / 0.18f, 2.0f)) + std::exp (-std::pow (std::log (f / (fc * 0.16f)) / 0.2f, 2.0f)));
            return 20 * std::log10 (juce::jmax (1e-4f, morph (f, fc) * vw));
        };
    };

    // The response over the spectrum
    {
        auto& g = wall.add (A, 110, 196, -0.5f, 690, 250, 760);
        auto* gp = &g;
        const float WW = 1040, H = 450;
        auto& h = g.handle ([gp] { return scratchPoint (gp, "h"); });
        h.ci = 1; h.r = 8;
        h.start = [dev] { return std::vector<float> { dev->get (Cutoff), dev->get (Res) }; };
        h.drag = [dev, WW] (float dx, float dy, const std::vector<float>& s) { dev->set (Cutoff, s[0] + dx / WW * 1.015f); dev->set (Res, s[1] - dy / 260); };
        h.tip = [dev] { return "cutoff " + F::hz (expMap (dev->get (Cutoff), 20, 18000)).toLowerCase() + "   res " + F::pct (dev->get (Res)); };
        g.draw = [=] (juce::Graphics& gr, double t, float, const Colour* c)
        {
            const auto P_ = fplot (gr, 0, 20, WW, H, -48, 24);
            const auto* node = dev->live();
            *follow += (clampf ((node != nullptr ? node->inputLevel.load() : 0.0f) * 3) - *follow) * 0.2f;
            spectrum->update (node, dev->ctx.engine.getSampleRate());
            specDots (gr, *spectrum, P_, 20, H, c[2]);
            const float base = expMap (dev->get (Cutoff), 20, 18000), lv = live (t);
            const bool moving = std::abs (std::log (lv / base)) > 0.02f;
            if (moving) { TubeOptions o; o.off = true; tube (gr, P_.curve (magDb (base)), c[0], o); }
            TubeOptions o; o.base = 20 + H; o.clips = 190; o.w = 1.15f;
            tube (gr, P_.curve (magDb (lv)), c[0], o);
            gp->scratch["h"] = { P_.X (base), P_.Y (magDb (base) (base)) };
            if (moving) bulb (gr, P_.X (lv), P_.Y (magDb (lv) (lv)), C::white, 1, 3.6f);
        };
    }

    // Drive, the morph from low pass to high pass, mix and vowel
    {
        auto& g = wall.add (B, 120, 722, 0.8f, 330, 60, 400);
        b.fader (g, Drive, "Drive", pctF, 0, 30, 240, 30, P (120, 16));
        b.fader (g, Mode, "low pass  to  high pass", [] (float v) { return v < 0.03f ? String ("low pass") : v > 0.97f ? String ("high pass") : String (juce::roundToInt (v * 100)) + "% to high pass"; }, 300, 30, 640, 30);
        b.fader (g, Mix, "Mix", pctF, 0, 112, 240, 112, P (120, 124), true);
        b.fader (g, Vowel, "Vowel", pctF, 300, 112, 640, 112, P (470, 100), true);
        g.draw = [dev] (juce::Graphics& gr, double, float, const Colour* c)
        {
            const float v = dev->get (Vowel), o = 2 + v * 20;
            Line up, lo;
            for (int i = 0; i <= 12; ++i)
            {
                const float s = std::sin (juce::MathConstants<float>::pi * (float) i / 12);
                up.push_back ({ 690.0f + (float) i * 7, 104 - s * o });
                lo.push_back ({ 690.0f + (float) i * 7, 104 + s * o * 1.25f });
            }
            TubeOptions to; if (v > 0.02f) { to.w = 0.8f; to.style = 0; } else to.off = true;
            tube (gr, Lines { up, lo }, c[2], to);
            if (v > 0.02f) text (gr, String::charToString ((juce::juce_wchar) "aeiou"[juce::jmin (4, (int) std::floor (v * 5))]), 732, 74 - o * 0.5f, 17, c[2], 0.7f, Align::centre);
        };
    }

    // The ladder itself: one rung per pole, a resonance loop feeding back
    {
        auto& g = wall.add (A, 1236, 214, 1.2f, 130, 220, 300);
        b.choice (g, Slope, { "24 db", "12 db" }, 64, 440, true).ci = 1;
        auto& a = g.dragArea ({ 40, 40, 180, 340 }, Cursor::PointingHandCursor);
        a.tip = [dev] { return String ("ladder   ") + (dev->choice (Slope) == 1 ? "12" : "24") + " db per octave"; };
        a.down = [dev] (float, float) -> std::optional<Gesture> { dev->setRaw (Slope, dev->choice (Slope) == 1 ? 0.0f : 1.0f); return Gesture {}; };
        g.draw = [dev, follow] (juce::Graphics& gr, double t, float, const Colour* c)
        {
            const int n = dev->choice (Slope) == 1 ? 2 : 4;
            const float lvl = 0.45f + 0.55f * *follow, lp = 1 - dev->get (Mode), cut = dev->get (Cutoff);
            const float open = lp * cut + (1 - lp) * (1 - cut), res = dev->get (Res), drive = dev->get (Drive);
            TubeOptions rails; rails.w = 1.1f; rails.clips = 150;
            tube (gr, Lines { { { 60, 30 }, { 60, 390 } }, { { 200, 30 }, { 200, 390 } } }, c[0], rails);
            const float RY[] = { 90, 170, 250, 330 };
            for (int i = 0; i < 4; ++i)
            {
                const float keep = clampf (std::pow (0.3f + 0.7f * open, (float) i + 1) * 1.5f);
                if (i >= n) { TubeOptions o; o.off = true; tube (gr, Line { { 60, RY[i] }, { 200, RY[i] } }, c[1], o); }
                else faded (gr, 0.3f + 0.7f * keep * lvl, [&] { TubeOptions o; o.w = 0.8f + keep * 0.5f; tube (gr, Line { { 60, RY[i] }, { 200, RY[i] } }, c[1], o); });
                label (gr, String ((i + 1) * 6) + " db", 50, RY[i] + 4, Align::right);
            }
            faded (gr, 0.3f + 0.7f * drive, [&] { TubeOptions o; o.w = 0.7f + drive * 0.6f; o.style = 0; tube (gr, Line { { 130, -36 }, { 118, -14 }, { 140, -10 }, { 124, 16 } }, c[2], o); });
            label (gr, "in", 150, -18); label (gr, "out", 118, 412);
            const auto loop = arcPoints (24, [res] (float u) { const float an = -juce::MathConstants<float>::halfPi + u * juce::MathConstants<float>::pi; return P (200 + std::cos (an) * (44 + res * 34), 210 + std::sin (an) * 180); });
            if (res > 0.03f)
            {
                faded (gr, 0.3f + 0.7f * res, [&] { TubeOptions o; o.w = 0.6f + res * 0.7f; o.style = 0; tube (gr, loop, c[2], o); });
                const auto q = loop[(size_t) (24 - (int) std::floor (std::fmod ((float) t * 1.6f, 1.0f) * 24))];
                bulb (gr, q.x, q.y, C::white, res, 3);
            }
            else { TubeOptions o; o.off = true; tube (gr, loop, c[2], o); }
            text (gr, "resonance", 254 + res * 34, 214, 15, c[2], 0.5f);
            const float u = std::fmod ((float) t * 0.7f, 1.0f);
            bulb (gr, 60, 30 + u * 360, C::white, lvl, 3.2f);
            bulb (gr, 200, 30 + std::fmod (u + 0.12f, 1.0f) * 360, C::white, lvl, 3.2f);
        };
    }

    // The wobble: drag its wave for rate and depth
    {
        auto& g = wall.add (Cc, 900, 712, -1, 300, 70, 380);
        auto& a = g.dragArea ({ 0, 0, 300, 80 }, Cursor::UpDownLeftRightResizeCursor);
        a.start = [dev] { return std::vector<float> { dev->get (Rate), dev->get (Wobble) }; };
        a.drag = [dev] (float dx, float dy, const std::vector<float>& s) { dev->set (Rate, s[0] + dx / 300); dev->set (Wobble, s[1] - dy / 120); };
        a.tip = [dev]
        {
            String rate = dev->on (WobbleSync) ? String (Param::wobbleBeats[juce::jlimit (0, 6, (int) (dev->get (Rate) * 6.999f))] / 4.0f, 2) + " bars"
                                               : String (expMap (dev->get (Rate), 0.05f, 20), 2) + " hz";
            return "wobble " + F::pct (dev->get (Wobble)) + " at " + rate;
        };
        b.fader (g, Follow, "Follow", pctF, 370, 40, 610, 40, P (490, 28), true);
        b.toggle (g, WobbleSync, "wobble in time", 370, 112, true);
        g.draw = [dev, lfoHz] (juce::Graphics& gr, double t, float, const Colour* c)
        {
            const float hz = lfoHz(), span = juce::jlimit (1.2f, 6.0f, 3 / hz), depth = dev->get (Wobble), a = 4 + depth * 30;
            Line pts;
            for (int i = 0; i <= 100; ++i) pts.push_back ({ (float) i * 3, 40 - std::sin (juce::MathConstants<float>::twoPi * ((float) t + ((float) i * 3 - 240) / 300 * span) * hz) * a });
            TubeOptions o; if (depth > 0.02f) o.w = 0.9f; else o.off = true;
            tube (gr, pts, c[0], o);
            bulb (gr, 240, 40 - std::sin (juce::MathConstants<float>::twoPi * (float) t * hz) * a, C::white, 1, 3.2f);
            const float wd = text (gr, "wobble", 0, 108, 17, c[0], 0.55f);
            ringMark (gr, wd + 7, 103, c[0]);
        };
    }
}

//==============================================================================
// Drive

void driveWall (Builder& b)
{
    using namespace Param::Drive;
    auto* dev = &b.d;
    auto& wall = b.wall;
    b.out.pal[0] = C::OR; b.out.pal[1] = C::PK; b.out.pal[2] = C::YL;
    b.out.parts = { C::OR, C::OR, C::PK, C::YL };
    enum { A = 1, B, Cc };
    b.title ("Drive", 320, [] { return String ("tube, tape, fuzz or fold"); });

    auto type = [dev] { return juce::jlimit (0, 3, (int) (dev->get (Type) * 3.999f)); };
    // The curve scaled so its loudest point is 1, for the pictures
    auto norm = [dev, type]
    {
        const int ty = type();
        const float drive = dev->get (Amount), bias = dev->get (Bias);
        float m = 0.01f;
        for (int i = 0; i <= 40; ++i) m = juce::jmax (m, std::abs (Param::driveCurve (ty, drive, bias, (float) i / 20 - 1)));
        return [ty, drive, bias, m] (float x) { return Param::driveCurve (ty, drive, bias, x) / m; };
    };
    auto driveTip = [dev] { return tipFor (*dev, Amount, "Drive", pctF); };

    // The valve: its filament glows with the drive
    {
        auto& g = wall.add (A, 120, 214, -2, 150, 240, 340);
        auto& a = g.dragArea ({ 0, 0, 300, 470 }, Cursor::UpDownResizeCursor);
        a.start = [dev] { return std::vector<float> { dev->get (Amount) }; };
        a.drag = [dev] (float, float dy, const std::vector<float>& s) { dev->set (Amount, s[0] - dy / 300); };
        a.tip = driveTip;
        g.draw = [dev] (juce::Graphics& gr, double, float, const Colour* c)
        {
            const auto* node = dev->live();
            const float drive = dev->get (Amount), lvl = node != nullptr ? clampf (node->inputLevel.load() * 2.5f) : 0.0f;
            const float heat = clampf (0.25f + drive * 0.75f + lvl * 0.5f);
            {
                const auto col = rgba (c[2], 0.4f * (0.25f + drive * 0.75f) * look().glow);
                juce::ColourGradient grad (col, 150, 245, rgba (c[2], 0), 320, 245, true);
                gr.setGradientFill (grad);
                gr.fillEllipse (-20, 75, 340, 340);
            }
            Line outline { { 45, 400 }, { 45, 150 } };
            for (int i = 0; i < 29; ++i) { const float an = juce::MathConstants<float>::pi + (float) (i + 1) / 30 * juce::MathConstants<float>::pi; outline.push_back ({ 150 + std::cos (an) * 105, 150 + std::sin (an) * 120 }); }
            outline.push_back ({ 255, 150 }); outline.push_back ({ 255, 400 });
            TubeOptions o; o.clips = 210; tube (gr, outline, c[0], o);
            TubeOptions cl; cl.closed = true; cl.w = 0.9f; tube (gr, roundRect (30, 400, 240, 34, 8), c[0], cl);
            TubeOptions pins; pins.w = 0.8f; pins.style = 0;
            tube (gr, Lines { { { 60, 436 }, { 60, 464 } }, { { 120, 436 }, { 120, 464 } }, { { 180, 436 }, { 180, 464 } }, { { 240, 436 }, { 240, 464 } } }, c[0], pins);
            tube (gr, roundRect (95, 150, 110, 190, 8), c[1], cl);
            Line fil;
            for (int i = 0; i < 9; ++i) fil.push_back ({ 112 + (float) i * 9.5f, i % 2 ? 172.0f : 318.0f });
            faded (gr, 0.35f + 0.65f * heat, [&] { TubeOptions fo; fo.w = 0.7f + heat * 0.8f; fo.style = 0; tube (gr, fil, c[2], fo); });
            TubeOptions top; top.w = 0.7f; top.style = 0;
            tube (gr, Lines { { { 130, 100 }, { 170, 100 } }, { { 150, 62 }, { 150, 84 } } }, c[1], top);
            text (gr, F::pct (drive), 150, 384, 24, c[2], 0.95f, Align::centre);
        };
    }

    // The transfer curve, with a lamp riding it
    {
        auto& g = wall.add (B, 520, 214, 0.8f, 210, 210, 330);
        auto& ty = g.words ({ { 0, "Tube" }, { 1, "Tape" }, { 2, "Fuzz" }, { 3, "Fold" } }, 40, 474);
        ty.size = 26; ty.gap = 26;
        ty.getChoice = type;
        ty.setChoice = [dev] (int v) { dev->set (Type, (float) v / 3.0f); };
        auto& a = g.dragArea ({ 0, 0, 420, 420 }, Cursor::UpDownResizeCursor);
        a.start = [dev] { return std::vector<float> { dev->get (Amount) }; };
        a.drag = [dev] (float, float dy, const std::vector<float>& s) { dev->set (Amount, s[0] - dy / 300); };
        a.tip = [dev, type] { static const char* n[] = { "tube", "tape", "fuzz", "fold" }; return String (n[type()]) + "   drive " + F::pct (dev->get (Amount)); };
        g.draw = [dev, norm] (juce::Graphics& gr, double, float, const Colour* c)
        {
            const auto fn = norm();
            juce::Path grid;
            grid.startNewSubPath (0, 420); grid.lineTo (420, 0); grid.startNewSubPath (210, 0); grid.lineTo (210, 420); grid.startNewSubPath (0, 210); grid.lineTo (420, 210);
            dashes (gr, grid);
            Line pts;
            for (int i = 0; i <= 120; ++i) pts.push_back ({ (float) i * 3.5f, 210 - fn ((float) i / 60 - 1) * 185 });
            TubeOptions o; o.clips = 170; o.w = 1.15f; tube (gr, pts, c[0], o);
            if (const auto* node = dev->live())
            {
                float s[8]; node->scope.read (s, 8);
                const float x = clampf (s[7] * 2.5f, -1, 1);
                bulb (gr, (x + 1) * 210, 210 - fn (x) * 185, C::white, 1, 4.5f);
            }
            label (gr, "in", 398, 228); label (gr, "out", 218, 16);
        };
    }

    // A sine in, what comes out, and its harmonics as bulb columns
    {
        auto& g = wall.add (Cc, 1030, 204, -0.8f, 260, 220, 370);
        g.draw = [norm] (juce::Graphics& gr, double t, float, const Colour* c)
        {
            const auto fn = norm();
            const int N = 128;
            std::array<float, 128> ys;
            for (int i = 0; i < N; ++i) ys[(size_t) i] = fn (std::sin (juce::MathConstants<float>::twoPi * (float) i / N) * 0.85f);
            Line in, out;
            for (int i = 0; i <= 130; ++i)
            {
                const float s = std::sin (juce::MathConstants<float>::twoPi * ((float) i / 65 + (float) t * 0.4f));
                in.push_back ({ (float) i * 4, 96 - s * 62 });
                out.push_back ({ (float) i * 4, 96 - fn (s * 0.85f) * 74 });
            }
            TubeOptions off; off.off = true; tube (gr, in, c[1], off);
            TubeOptions o; o.clips = 170; o.base = 96; tube (gr, out, c[0], o);
            std::array<float, 10> H;
            for (int h = 0; h < 10; ++h)
            {
                float a = 0, bb = 0;
                for (int i = 0; i < N; ++i) { a += ys[(size_t) i] * std::cos (juce::MathConstants<float>::twoPi * (float) (h + 1) * (float) i / N); bb += ys[(size_t) i] * std::sin (juce::MathConstants<float>::twoPi * (float) (h + 1) * (float) i / N); }
                H[(size_t) h] = std::hypot (a, bb);
            }
            const float hm = H[0] > 0 ? H[0] : 1.0f;
            for (int h = 0; h < 10; ++h)
            {
                const int n = juce::roundToInt (clampf (1 + std::log10 (juce::jmax (1e-4f, H[(size_t) h] / hm)) / 2.2f) * 8);
                for (int k = 0; k < 8; ++k) bulb (gr, 26 + (float) h * 52, 424 - (float) k * 24, c[h ? (h % 2 ? 2 : 1) : 0], k < n ? 1.0f : 0.0f, 6.5f);
                label (gr, String (h + 1), 26 + (float) h * 52, 452, Align::centre);
            }
            text (gr, "harmonics", 0, 226, 18, c[1], 0.55f);
        };
    }

    // Tone, mix, bias, output, sag, oversampling
    {
        auto& g = wall.add (B, 440, 764, 0.5f, 460, 40, 500);
        b.fader (g, Tone, "Tone", [] (float v) { return F::hz (expMap (v, 600, 18000)); }, 0, 30, 200, 30, P (100, 18));
        b.fader (g, Mix, "Mix", pctF, 240, 30, 440, 30, P (340, 42));
        b.fader (g, Bias, "Bias", [] (float v) { return F::signedVal ((float) juce::roundToInt ((v - 0.5f) * 200), "%", 0); }, 480, 30, 680, 30, P (580, 18), true, true);
        b.fader (g, Output, "Output", dbF, 720, 30, 920, 30, P (820, 42), true);
        b.fader (g, Sag, "Sag", pctF, 0, 104, 200, 104, {}, true);
        auto& os = b.choice (g, Oversample, { "1x", "2x", "4x" }, 250, 110, true);
        os.pre = "oversample";
    }
}

//==============================================================================
// Delay

void delayWall (Builder& b)
{
    using namespace Param::Delay;
    auto* dev = &b.d;
    auto& wall = b.wall;
    auto* w = &wall;
    b.out.pal[0] = C::BL; b.out.pal[1] = C::CY; b.out.pal[2] = C::YL;
    b.out.parts = { C::BL, C::BL, C::CY, C::YL };
    enum { A = 1, B, Cc };
    b.title ("Delay", 320, [] { return String ("tempo echoes on tape"); });

    static const char* divs[] = { "1/16", "1/8t", "1/8", "1/8.", "1/4", "1/4.", "1/2" };
    auto timeWord = [dev]
    {
        if (dev->on (Sync)) return String (divs[Param::divisionIndex (dev->get (Time))]);
        return F::time (expMap (dev->get (Time), 0.01f, 2));
    };
    auto timeTip = [dev, timeWord] { return "time   " + timeWord(); };
    auto fbTip = [dev] { return tipFor (*dev, Feedback, "Feedback", pctF); };
    auto bpm = [dev] { return (float) dev->ctx.project.bpm.load(); };
    // The repeat time in beats
    auto beats = [dev, bpm]
    {
        if (dev->on (Sync)) return Param::divisionBeats[Param::divisionIndex (dev->get (Time))];
        return expMap (dev->get (Time), 0.01f, 2) * bpm() / 60.0f;
    };

    // The tape deck: reels, the record and play heads, and the feedback loop
    {
        auto& g = wall.add (Cc, 92, 222, -1.2f, 220, 200, 330);
        auto HX = [dev] { return 140 + dev->get (Time) * 200; };
        auto hist = std::make_shared<std::array<float, 110>>();
        hist->fill (0.0f);
        auto angle = std::make_shared<float> (0.0f);
        auto& h = g.handle ([HX] { return std::optional<P> (P (HX(), 330)); });
        h.ci = 1; h.r = 7;
        h.start = [dev] { return std::vector<float> { dev->get (Time) }; };
        h.drag = [dev] (float dx, float, const std::vector<float>& s) { dev->set (Time, s[0] + dx / 200); };
        h.tip = timeTip;
        auto& a = g.dragArea ({ 60, 364, 330, 80 }, Cursor::UpDownResizeCursor);
        a.start = [dev] { return std::vector<float> { dev->get (Feedback) }; };
        a.drag = [dev] (float, float dy, const std::vector<float>& s) { dev->set (Feedback, s[0] + dy / 120); };
        a.tip = fbTip;
        g.draw = [=] (juce::Graphics& gr, double t, float dt, const Colour* c)
        {
            const auto* node = dev->live();
            const float hx = HX(), fb = dev->on (Freeze) ? 1.0f : dev->get (Feedback), flutter = dev->get (Flutter);
            std::rotate (hist->rbegin(), hist->rbegin() + 1, hist->rend());
            (*hist)[0] = node != nullptr ? clampf (node->inputLevel.load() * 3) : 0.0f;
            *angle += dt * (1 + (1 - dev->get (Time)) * 2.6f);
            Line tape { { 25, 110 }, { 28, 270 }, { 40, 308 }, { 72, 330 } };
            for (int i = 0; i < 30; ++i) tape.push_back ({ 82 + (float) i * 10, 330 + std::sin ((float) i * 0.9f + (float) t * 9) * flutter * 7 });
            for (auto p : { P (378, 330), P (400, 308), P (412, 270), P (415, 110) }) tape.push_back (p);
            TubeOptions off; off.off = true;
            tube (gr, Lines { circle (105, 110, 58, 48), circle (335, 110, 44, 48) }, c[0], off);
            TubeOptions to; to.w = 0.6f; to.style = 0; tube (gr, tape, c[1], to);
            TubeOptions ro; ro.w = 1.05f; ro.clips = 120;
            tube (gr, Lines { circle (105, 110, 80, 48), circle (335, 110, 80, 48) }, c[0], ro);
            for (float x : { 105.0f, 335.0f })
            {
                Lines spokes;
                for (int k = 0; k < 3; ++k) { const float an = *angle + (float) k * 2.094f; spokes.push_back ({ { x + std::cos (an) * 13, 110 + std::sin (an) * 13 }, { x + std::cos (an) * 34, 110 + std::sin (an) * 34 } }); }
                TubeOptions so; so.w = 0.7f; so.style = 0; tube (gr, spokes, c[0], so);
                bulb (gr, x, 110, c[0], 0.8f, 5);
            }
            for (int j = 2; j < 100; j += 3)
            {
                const float x = 140 + (float) j * 2.4f;
                if (x < 372 && (*hist)[(size_t) j] > 0.12f) bulb (gr, x, 330, C::white, (*hist)[(size_t) j] * (x < hx ? 1.0f : fb), 2.6f);
            }
            TubeOptions ho; ho.closed = true; ho.w = 0.7f; ho.style = 0;
            tube (gr, roundRect (128, 338, 24, 24, 5), c[2], ho);
            ho.w = 0.8f; tube (gr, roundRect (hx - 12, 338, 24, 24, 5), c[1], ho);
            label (gr, "rec", 140, 314, Align::centre); label (gr, "play", hx, 314, Align::centre);
            const auto loop = arcPoints (20, [hx, fb] (float u) { return P (hx + (140 - hx) * u, 364 + std::sin (juce::MathConstants<float>::pi * u) * (22 + fb * 46)); });
            if (fb > 0.03f)
            {
                faded (gr, 0.3f + 0.7f * fb, [&] { TubeOptions o; o.w = 0.6f + fb * 0.6f; o.style = 0; tube (gr, loop, c[2], o); });
                const auto q = loop[(size_t) juce::jlimit (0, 20, (int) std::floor (std::fmod ((float) t * 1.4f, 1.0f) * 20))];
                bulb (gr, q.x, q.y, C::white, fb, 3);
            }
            else tube (gr, loop, c[2], off);
            text (gr, "feedback", (hx + 140) / 2, 400 + fb * 46, 15, c[2], 0.5f, Align::centre);
            text (gr, timeWord(), 220, 222, 30, c[1], 0.95f, Align::centre);
        };
    }

    // The echoes as bouncing arcs, left and right when they ping-pong
    {
        auto& g = wall.add (A, 640, 446, 0, 450, 0, 520);
        const float PX = 220;
        struct Geo { float beats, sp, fb; int n; };
        auto geo = [dev, beats, PX]
        {
            const float bt = beats(), sp = juce::jmax (20.0f, bt * PX);
            return Geo { bt, sp, dev->on (Freeze) ? 1.0f : dev->get (Feedback) * 0.95f, juce::jmin (24, (int) std::floor (900 / sp)) };
        };
        auto& dv = g.words ({}, 0, -236);
        for (int i = 0; i < 7; ++i) dv.opts.push_back ({ i, divs[i] });
        dv.size = 21; dv.gap = 20;
        dv.visible = [dev] { return dev->on (Sync); };
        dv.getChoice = [dev] { return Param::divisionIndex (dev->get (Time)); };
        dv.setChoice = [dev, w] (int i) { dev->set (Time, ((float) i + 0.5f) / 7); w->say ("time   " + String (divs[juce::jlimit (0, 6, i)])); };
        auto& ht = g.handle ([geo] { return std::optional<P> (P (geo().sp, 0)); });
        ht.ci = 1; ht.r = 7;
        ht.start = [dev] { return std::vector<float> { dev->get (Time) }; };
        ht.drag = [dev] (float dx, float, const std::vector<float>& s) { dev->set (Time, s[0] + dx / 700); };
        ht.tip = timeTip;
        auto& hf = g.handle ([dev, geo] { const auto G = geo(); return std::optional<P> (P (G.sp * 1.5f, (dev->on (PingPong) ? 1.0f : -1.0f) * 200 * G.fb)); });
        hf.ci = 2; hf.r = 6;
        hf.start = [dev] { return std::vector<float> { dev->get (Feedback) }; };
        hf.drag = [dev] (float, float dy, const std::vector<float>& s) { dev->set (Feedback, s[0] + (dev->on (PingPong) ? dy : -dy) / 210); };
        hf.tip = fbTip;
        g.draw = [=] (juce::Graphics& gr, double t, float, const Colour* c)
        {
            const auto G = geo();
            const bool pp = dev->on (PingPong);
            const float tone = dev->get (Tone), mixv = dev->get (Mix);
            juce::Path axis; axis.startNewSubPath (-20, 0); axis.lineTo (900, 0); dashes (gr, axis);
            auto arc = [&] (int i, float h) { const float dir = pp && i % 2 ? 1.0f : -1.0f; return arcPoints (24, [&] (float u) { return P (((float) i + u) * G.sp, dir * h * 4 * u * (1 - u)); }); };
            for (int i = 0; i < G.n; ++i)
            {
                const float a = std::pow (G.fb, (float) i), h = 200 * a, dull = std::pow (0.35f + 0.65f * tone, (float) i);
                const auto col = c[i % 2 && pp ? 1 : 0];
                if (a < 0.05f) { TubeOptions o; o.off = true; o.w = 0.6f; tube (gr, arc (i, 6), col, o); continue; }
                faded (gr, 0.25f + 0.75f * dull, [&] { TubeOptions o; o.w = 0.55f + 0.6f * a; o.clips = i ? 0.0f : 150.0f; tube (gr, arc (i, h), col, o); });
                bulb (gr, ((float) i + 1) * G.sp, 0, col, clampf (a * (0.4f + mixv * 1.2f)), 3 + 5 * a);
            }
            bulb (gr, 0, 0, C::white, 1, 7);
            text (gr, "dry", -14, 34, 17, c[0], 0.5f);
            const float bt = (float) t * bpm() / 60.0f / juce::jmax (0.05f, G.beats);
            const int m = juce::jmax (1, juce::jmin (G.n, 8)), i = (int) std::floor (bt) % m;
            const float u = bt - std::floor (bt), a = std::pow (G.fb, (float) i);
            if (a > 0.04f) bulb (gr, ((float) i + u) * G.sp, (pp && i % 2 ? 1.0f : -1.0f) * 200 * a * 4 * u * (1 - u), C::white, 1, 4.5f);
            if (pp) { text (gr, "left", 850, -150, 18, c[0], 0.45f); text (gr, "right", 850, 170, 18, c[1], 0.45f); }
        };
    }

    {
        auto& g = wall.add (B, 110, 746, 0.6f, 640, 50, 680);
        b.fader (g, Tone, "Tone", [] (float v) { return F::hz (expMap (v, 800, 16000)); }, 0, 30, 220, 30, P (110, 18));
        b.fader (g, Mix, "Mix", pctF, 270, 30, 490, 30, P (380, 42));
        b.fader (g, Flutter, "Flutter", pctF, 540, 30, 760, 30, P (650, 18), true);
        b.fader (g, Duck, "Duck", pctF, 810, 30, 1030, 30, P (920, 42), true);
        b.fader (g, Spread, "Spread", pctF, 1080, 30, 1300, 30, {}, true);
        b.toggle (g, PingPong, "ping pong", 0, 106, true);
        b.toggle (g, Freeze, "freeze", 170, 106, true).ci = 1;
        b.toggle (g, Sync, "tempo sync", 310, 106, true).ci = 2;
    }
}

//==============================================================================
// Reverb

void reverbWall (Builder& b)
{
    using namespace Param::Reverb;
    auto* dev = &b.d;
    auto& wall = b.wall;
    auto* w = &wall;
    b.out.pal[0] = C::CY; b.out.pal[1] = C::VI; b.out.pal[2] = C::PK;
    b.out.parts = { C::CY, C::CY, C::VI, C::PK };
    enum { A = 1, B, Cc };
    b.title ("Reverb", 390, [] { return String ("rooms to halls"); });

    // The room as nested arches: size, width and pre-delay are lamps on them
    {
        auto& g = wall.add (A, 800, 700, 0, 0, -200, 760);
        struct Geo { int n; float r0, gap, wx; float rx (int i) const { return (r0 + (float) i * gap) * wx; } float ry (int i) const { return (r0 + (float) i * gap) * 0.92f; } };
        auto geo = [dev] { const float s = dev->get (Size); return Geo { 3 + juce::roundToInt (s * 6), 60 + dev->get (PreDelay) * 120, 34 + s * 28, 0.6f + dev->get (Width) * 0.6f }; };
        auto hist = std::make_shared<std::array<float, 90>>();
        hist->fill (0.0f);
        auto& hs = g.handle ([geo] { const auto G = geo(); return std::optional<P> (P (0, -G.ry (G.n - 1))); });
        hs.ci = 1; hs.r = 7;
        hs.start = [dev] { return std::vector<float> { dev->get (Size) }; };
        hs.drag = [dev] (float, float dy, const std::vector<float>& s) { dev->set (Size, s[0] - dy / 330); };
        hs.tip = [dev] { return tipFor (*dev, Size, "Size", pctF); };
        auto& hw = g.handle ([geo] { const auto G = geo(); return std::optional<P> (P (G.rx (G.n - 1), 0)); });
        hw.ci = 2; hw.r = 6;
        hw.start = [dev] { return std::vector<float> { dev->get (Width) }; };
        hw.drag = [dev] (float dx, float, const std::vector<float>& s) { dev->set (Width, s[0] + dx / 260); };
        hw.tip = [dev] { return tipFor (*dev, Width, "Width", pctF); };
        auto& hp = g.handle ([geo] { return std::optional<P> (P (0, -geo().ry (0))); });
        hp.ci = 0; hp.r = 5;
        hp.start = [dev] { return std::vector<float> { dev->get (PreDelay) }; };
        hp.drag = [dev] (float, float dy, const std::vector<float>& s) { dev->set (PreDelay, s[0] - dy / 120); };
        hp.tip = [dev] { return tipFor (*dev, PreDelay, "Pre-delay", [] (float v) { return String (juce::roundToInt (v * 200)) + " ms"; }); };
        g.draw = [=] (juce::Graphics& gr, double, float, const Colour* c)
        {
            const auto G = geo();
            const auto* node = dev->live();
            std::rotate (hist->rbegin(), hist->rbegin() + 1, hist->rend());
            (*hist)[0] = node != nullptr ? clampf (node->inputLevel.load() * 2.5f) : 0.0f;
            juce::Path axis; axis.startNewSubPath (-720, 0); axis.lineTo (720, 0); dashes (gr, axis);
            const float damp = dev->get (Damp), mixv = dev->get (Mix), size = dev->get (Size);
            for (int i = G.n - 1; i >= 0; --i)
            {
                const float br = std::pow (1 - damp * 0.42f, (float) i) * (0.35f + mixv * 0.9f);
                const float pulse = (*hist)[(size_t) juce::jmin (89, juce::roundToInt ((float) i * (3 + size * 6)))] * std::pow (0.88f, (float) i);
                const float lv = clampf (br + pulse * 0.8f);
                const auto col = c[i % 2 ? 1 : 0];
                const auto a = arcPoints (40, [&] (float u) { const float th = juce::MathConstants<float>::pi + u * juce::MathConstants<float>::pi; return P (std::cos (th) * G.rx (i), std::sin (th) * G.ry (i)); });
                if (lv < 0.12f) { TubeOptions o; o.off = true; o.w = 0.8f; tube (gr, a, col, o); continue; }
                faded (gr, lv + 0.15f, [&]
                {
                    TubeOptions o; o.w = 0.6f + lv * 0.6f;
                    if (i > G.n - 3 && i > 1) { o.dashOn = 7; o.dashOff = 7; }
                    o.clips = i > G.n - 3 ? 0.0f : 220.0f;
                    tube (gr, a, col, o);
                });
            }
            bulb (gr, 0, -12, C::white, clampf (0.5f + (*hist)[0]), 6);
            text (gr, "source", 0, 24, 16, c[0], 0.45f, Align::centre);
            const float wd = text (gr, "pre-delay", 16, -G.ry (0) - 8, 14, c[0], 0.45f);
            ringMark (gr, 22 + wd, -G.ry (0) - 12, c[0]);
        };
    }

    {
        auto& g = wall.add (B, 70, 776, 0.6f, 250, 40, 330);
        b.fader (g, Damp, "Damp", pctF, 0, 30, 220, 30, P (110, 18));
        b.fader (g, Mix, "Mix", pctF, 270, 30, 490, 30, P (380, 42));
        auto& sp = b.choice (g, Space, { "Room", "Hall", "Plate", "Cave" }, 0, 104, true);
        sp.setChoice = [dev, w] (int v)
        {
            static const float sizes[] = { 0.3f, 0.6f, 0.5f, 0.92f }, damps[] = { 0.5f, 0.4f, 0.15f, 0.6f };
            static const char* names[] = { "room", "hall", "plate", "cave" };
            v = juce::jlimit (0, 3, v);
            dev->setRaw (Space, (float) v); dev->set (Size, sizes[v]); dev->set (Damp, damps[v]);
            w->say (names[v]);
        };
    }
    {
        auto& g = wall.add (Cc, 1010, 776, -0.6f, 270, 40, 330);
        b.fader (g, LowCut, "Low cut", [] (float v) { return v < 0.02f ? String ("off") : F::hz (expMap (v, 20, 800)); }, 0, 30, 160, 30, P (80, 18), true);
        b.fader (g, Shimmer, "Shimmer", pctF, 200, 30, 360, 30, P (280, 42), true);
        b.fader (g, Drift, "Drift", pctF, 400, 30, 540, 30, {}, true);
        b.toggle (g, Freeze, "freeze", 0, 104, true).ci = 1;
        b.toggle (g, Gated, "gated", 120, 104, true).ci = 2;
    }
}

//==============================================================================
// EQ

void eqWall (Builder& b)
{
    using namespace Param::EQ;
    auto* dev = &b.d;
    auto& wall = b.wall;
    b.out.pal[0] = C::YL; b.out.pal[1] = C::OR; b.out.pal[2] = C::CY;
    b.out.parts = { C::YL, C::YL, C::OR };
    enum { A = 1, B };
    b.title ("EQ", 230, [] { return String ("four bands you can move"); });
    static const char* names[] = { "low shelf", "low mid", "mid", "high shelf" };
    static const int CI[] = { 0, 1, 2, 1 };

    // The response in dB, the same sum the engine's four bands make
    auto eqDb = [dev] (float f)
    {
        float db = 0.0f;
        const float tilt = dev->get (Tilt);
        for (int i = 0; i < 4; ++i)
        {
            const float g = dev->get (gainParam[i]) * 12.0f + tilt * 6.0f * tiltWeight[i];
            const float x = f / freqOf (dev->get (Freq0 + i)), q = juce::jlimit (0.3f, 6.0f, dev->get (Q0 + i));
            if (i == 0) db += g / (1 + std::pow (x, 2.2f));
            else if (i == 3) db += g / (1 + std::pow (1 / x, 2.2f));
            else db += g * std::exp (-std::pow (std::log (x), 2.0f) / (2 * std::pow (0.62f / q, 2.0f)));
        }
        return db;
    };
    const auto eqF = [] (float v) { return F::signedVal (v * 12.0f, " dB"); };

    {
        auto& g = wall.add (A, 110, 200, -0.4f, 690, 240, 760);
        auto* gp = &g;
        const float WW = 1380, H = 460;
        auto spectrum = std::make_shared<Spectrum>();
        for (int i = 0; i < 4; ++i)
        {
            auto& h = g.handle ([gp, i] { auto& s = gp->scratch["l" + String (i)]; if (s.size() < 2) return std::optional<P>(); return std::optional<P> (P (s[0], s[1])); });
            h.ci = CI[i]; h.r = 8;
            h.start = [dev, i] { return std::vector<float> { freqOf (dev->get (Freq0 + i)), dev->get (gainParam[i]) }; };
            h.drag = [dev, i, WW, H] (float dx, float dy, const std::vector<float>& s)
            {
                dev->setRaw (Freq0 + i, valueOf (juce::jlimit (30.0f, 16000.0f, s[0] * std::pow (1000.0f, dx / WW))));
                dev->setRaw (gainParam[i], juce::jlimit (-1.0f, 1.0f, s[1] - dy / (H / 36 * 12)));
            };
            h.wheel = [dev, i] (float delta) { dev->setRaw (Q0 + i, juce::jlimit (0.3f, 6.0f, dev->get (Q0 + i) * (delta > 0 ? 1.1f : 0.9f))); };
            h.doubleClick = [dev, i] (float, float) { dev->setRaw (gainParam[i], 0.0f); };
            h.tip = [dev, i] { return String (names[i]) + "   " + F::hz (freqOf (dev->get (Freq0 + i))).toLowerCase() + "   " + F::signedVal (dev->get (gainParam[i]) * 12.0f, " db"); };
        }
        g.draw = [=] (juce::Graphics& gr, double, float, const Colour* c)
        {
            const auto P_ = fplot (gr, 0, 20, WW, H, -18, 18);
            const float base = 20 + H;
            spectrum->update (dev->live(), dev->ctx.engine.getSampleRate());
            specDots (gr, *spectrum, P_, 20, H, c[2]);
            TubeOptions rail; rail.w = 0.7f; rail.clips = 230;
            tube (gr, Line { { 0, 0 }, { WW, 0 } }, c[1], rail);
            for (int i = 0; i < 4; ++i)
            {
                const auto col = c[CI[i]];
                const float fr = freqOf (dev->get (Freq0 + i)), x = P_.X (fr), y = P_.Y (eqDb (fr));
                gp->scratch["l" + String (i)] = { x, y };
                const float amt = 0.35f + 0.65f * juce::jmin (1.0f, std::abs (dev->get (gainParam[i])) * 1.25f);
                const float hw = juce::jlimit (80.0f, 330.0f, 70 + 110 / juce::jlimit (0.3f, 6.0f, dev->get (Q0 + i)));
                juce::Path beam;
                beam.startNewSubPath (x - 12, y); beam.lineTo (x + 12, y); beam.lineTo (x + hw, base); beam.lineTo (x - hw, base); beam.closeSubPath();
                gr.setGradientFill (juce::ColourGradient (rgba (col, 0.3f * amt * look().glow), 0, y, rgba (col, 0), 0, base, false));
                gr.fillPath (beam);
                TubeOptions cord; cord.w = 0.4f; cord.style = 0;
                if (y > 26) tube (gr, Line { { x, 0 }, { x, y - 22 } }, col, cord);
                TubeOptions shade; shade.w = 0.75f; shade.style = 0;
                tube (gr, Line { { x - 19, y - 5 }, { x - 8, y - 22 }, { x + 8, y - 22 }, { x + 19, y - 5 } }, col, shade);
                const float wd = text (gr, names[i], x + 30, y - 12, 16, col, 0.6f);
                if (i == 1) ringMark (gr, x + 38 + wd, y - 17, col);
            }
            TubeOptions o; o.clips = 190; o.w = 1.15f; o.base = P_.Y (0);
            tube (gr, P_.curve (eqDb), c[0], o);
        };
    }
    {
        auto& g = wall.add (B, 120, 746, 0.6f, 600, 40, 640);
        b.fader (g, LowCut, "Low cut", [] (float v) { return v < 0.02f ? String ("off") : F::hz (expMap (v, 20, 800)); }, 0, 30, 240, 30, P (120, 18), true);
        b.fader (g, HighCut, "High cut", [] (float v) { return v > 0.98f ? String ("off") : F::hz (expMap (v, 1000, 20000)); }, 300, 30, 540, 30, P (420, 42), true);
        auto& tilt = g.fader (600, 30, 840, 30, P (720, 18));
        tilt.text = "tilt"; tilt.isNew = true; tilt.bipolar = true; tilt.def = 0.5f;
        tilt.get = [dev] { return dev->get (Tilt) * 0.5f + 0.5f; };
        tilt.set = [dev] (float v) { dev->setRaw (Tilt, juce::jlimit (-1.0f, 1.0f, v * 2 - 1)); };
        tilt.tip = [dev] { return "tilt   " + F::signedVal (dev->get (Tilt) * 6.0f, " db"); };
        b.fader (g, Output, "Output", dbF, 900, 30, 1140, 30, P (1020, 42));
        b.toggle (g, Analog, "analog warmth", 0, 106, true).ci = 1;
        g.draw = [] (juce::Graphics& gr, double, float, const Colour* c)
        {
            text (gr, "drag a lamp to move its band, scroll on it to narrow or widen its light", 260, 106, 16, c[2], 0.4f);
        };
    }
    juce::ignoreUnused (eqF);
}

//==============================================================================
// Compressor

void compWall (Builder& b)
{
    using namespace Param::Comp;
    auto* dev = &b.d;
    auto& wall = b.wall;
    auto* w = &wall;
    b.out.pal[0] = C::GR; b.out.pal[1] = C::YL; b.out.pal[2] = C::PK;
    b.out.parts = { C::GR, C::GR, C::YL, C::PK };
    enum { A = 1, B, Cc };
    b.title ("Compressor", 600, [] { return String ("glue and punch"); });
    const auto thrF = [] (float v) { return String (-40 + v * 40, 1) + " dB"; };
    const auto ratioF = [] (float v) { return String (1 + v * 19, 1) + " : 1"; };

    // The transfer curve: threshold and ratio are lamps on it
    {
        auto& g = wall.add (A, 100, 230, -0.8f, 220, 220, 340);
        const float Z = 440;
        auto X = [Z] (float db) { return (db + 60) / 60 * Z; };
        auto Y = [Z] (float db) { return Z - (juce::jlimit (-60.0f, 6.0f, db) + 60) / 60 * Z; };
        auto out = [dev] (float x)
        {
            const float th = -40 + dev->get (Thresh) * 40, ratio = 1 + dev->get (Ratio) * 19;
            return x - Param::compReduction (x, th, ratio, dev->get (Knee) * 24) + dev->get (Makeup) * 24;
        };
        auto& ht = g.handle ([=] { const float th = -40 + dev->get (Thresh) * 40; return std::optional<P> (P (X (th), Y (out (th)))); });
        ht.ci = 1; ht.r = 7;
        ht.start = [dev] { return std::vector<float> { dev->get (Thresh) }; };
        ht.drag = [dev, Z] (float dx, float, const std::vector<float>& s) { dev->set (Thresh, s[0] + dx / Z * 1.5f); };
        ht.tip = [dev, thrF] { return tipFor (*dev, Thresh, "Threshold", thrF); };
        auto& hr = g.handle ([=] { return std::optional<P> (P (X (0), Y (out (0)))); });
        hr.ci = 2; hr.r = 6;
        hr.start = [dev] { return std::vector<float> { dev->get (Ratio) }; };
        hr.drag = [dev] (float, float dy, const std::vector<float>& s) { dev->set (Ratio, s[0] + dy / 200); };
        hr.tip = [dev, ratioF] { return tipFor (*dev, Ratio, "Ratio", ratioF); };
        g.draw = [=] (juce::Graphics& gr, double, float, const Colour* c)
        {
            juce::Path grid;
            grid.startNewSubPath (0, Z); grid.lineTo (Z, 0);
            for (float v : { -48.0f, -36.0f, -24.0f, -12.0f }) { grid.startNewSubPath (X (v), 0); grid.lineTo (X (v), Z); grid.startNewSubPath (0, Y (v)); grid.lineTo (Z, Y (v)); }
            dashes (gr, grid);
            Line pts;
            for (int i = 0; i <= 60; ++i) pts.push_back ({ X ((float) (-60 + i)), Y (out ((float) (-60 + i))) });
            TubeOptions o; o.clips = 170; o.w = 1.15f; tube (gr, pts, c[0], o);
            const auto* node = dev->live();
            const float lv = node != nullptr ? 20 * std::log10 (juce::jmax (1e-3f, node->inputLevel.load())) : -60.0f;
            if (lv > -58) bulb (gr, X (lv), Y (out (lv)), C::white, 1, 4.5f);
            label (gr, "in", Z - 16, Z + 16); label (gr, "out", -28, 12);
            const float th = -40 + dev->get (Thresh) * 40;
            text (gr, "threshold", X (th) - 12, Y (out (th)) - 16, 15, c[1], 0.5f, Align::right);
            text (gr, "ratio", X (0) + 14, Y (out (0)) + 5, 15, c[2], 0.5f);
        };
    }

    // The squeeze meter
    {
        auto& g = wall.add (B, 860, 590, 0, 0, -150, 380);
        auto needle = std::make_shared<float> (0.0f);
        g.draw = [dev, needle] (juce::Graphics& gr, double, float, const Colour* c)
        {
            const float R = 270, A0 = -150, A1 = -30;
            auto ang = [] (float a) { return juce::degreesToRadians (a); };
            const auto* node = dev->live();
            const float red = node != nullptr ? std::abs (node->display.load()) : 0.0f;
            *needle += (clampf (red / 20) - *needle) * 0.3f;
            TubeOptions o; o.clips = 210;
            tube (gr, arcPoints (40, [&] (float u) { const float a = ang (A0 + (A1 - A0) * u); return P (std::cos (a) * R, std::sin (a) * R); }), c[0], o);
            Lines lo, hi;
            for (int i = 0; i <= 10; ++i)
            {
                const float a = ang (A0 + (A1 - A0) * (float) i / 10), r2 = i % 5 ? R - 24 : R - 40;
                (i < 7 ? lo : hi).push_back ({ { std::cos (a) * (R - 9), std::sin (a) * (R - 9) }, { std::cos (a) * r2, std::sin (a) * r2 } });
            }
            TubeOptions t; t.w = 0.7f; t.style = 0;
            tube (gr, lo, c[1], t); tube (gr, hi, c[2], t);
            for (int i = 0; i < 3; ++i) { const float a = ang (A0 + (A1 - A0) * (float) i / 2); label (gr, String (i * 10), std::cos (a) * (R - 58), std::sin (a) * (R - 58) + 4, Align::centre, Colour (0xffb9b8b2)); }
            const float na = ang (A0 + (A1 - A0) * *needle);
            TubeOptions n; n.w = 1.1f; n.style = 0;
            tube (gr, Line { { 0, 0 }, { std::cos (na) * (R - 50), std::sin (na) * (R - 50) } }, c[2], n);
            bulb (gr, 0, 0, c[1], 1, 8);
            text (gr, "squeeze", 0, 52, 20, c[0], 0.6f, Align::centre);
            text (gr, String (*needle * 20, 1) + " db", 0, 86, 24, c[2], 0.95f, Align::centre);
        };
    }

    // Attack and release as a clamp and let-go, then the rest
    {
        auto& g = wall.add (Cc, 1190, 240, 0.8f, 190, 240, 330);
        const float H = 190;
        auto ax = [dev] { return 20 + std::pow (dev->get (Attack), 0.8f) * 110; };
        auto rx = [dev, ax] { return ax() + 70 + std::pow (dev->get (Release), 0.8f) * 150; };
        auto& ha = g.handle ([ax, H] { return std::optional<P> (P (ax(), H - 30)); });
        ha.ci = 1;
        ha.start = [dev] { return std::vector<float> { dev->get (Attack) }; };
        ha.drag = [dev] (float dx, float, const std::vector<float>& s) { dev->set (Attack, s[0] + dx / 130); };
        ha.tip = [dev] { return tipFor (*dev, Attack, "Attack", [] (float v) { return F::time (expMap (v, 0.0001f, 0.1f)); }); };
        auto& hr = g.handle ([rx] { return std::optional<P> (P (rx(), 30)); });
        hr.ci = 2;
        hr.start = [dev] { return std::vector<float> { dev->get (Release) }; };
        hr.drag = [dev] (float dx, float, const std::vector<float>& s) { dev->set (Release, s[0] + dx / 170); };
        hr.tip = [dev] { return tipFor (*dev, Release, "Release", [] (float v) { return F::time (expMap (v, 0.01f, 1)); }); };
        b.fader (g, Makeup, "Makeup", [] (float v) { return F::signedVal (v * 24, " dB"); }, 0, 280, 170, 280, P (85, 268));
        b.fader (g, Knee, "Knee", [] (float v) { return String (v * 24, 1) + " dB"; }, 210, 280, 380, 280, P (295, 292), true);
        b.fader (g, Mix, "Mix", pctF, 0, 360, 170, 360, P (85, 372), true);
        b.fader (g, SideCut, "Sidechain cut", [] (float v) { return v < 0.02f ? String ("off") : F::hz (expMap (v, 20, 800)); }, 210, 360, 380, 360, P (295, 348), true);
        b.toggle (g, AutoMakeup, "auto makeup", 0, 430, true).ci = 1;
        auto& ch = b.choice (g, Character, { "Clean", "Glue", "Crush" }, 0, 480, true);
        ch.pre = "character"; ch.ci = 2;
        // Listens to: itself, or another track's sound (click to go through the tracks)
        auto& ls = g.words ({ { 1, "" } }, 0, 530);
        ls.pre = "listens to"; ls.isNew = true; ls.toggle = true;
        ls.dyn = [dev]
        {
            const int v = dev->choice (ListensTo) - 1;
            if (v < 0 || v >= (int) dev->ctx.project.tracks.size()) return String ("self");
            return dev->ctx.project.tracks[(size_t) v]->name;
        };
        ls.getChoice = [] { return 1; };
        ls.setChoice = [dev, w] (int)
        {
            const int n = (int) dev->ctx.project.tracks.size();
            int v = dev->choice (ListensTo);
            do { v = (v + 1) % (n + 1); } while (v > 0 && v - 1 == dev->track);
            dev->setRaw (ListensTo, (float) v);
            w->say (v == 0 ? String ("listens to itself") : "listens to " + dev->ctx.project.tracks[(size_t) (v - 1)]->name.toLowerCase());
        };
        g.draw = [ax, rx, H] (juce::Graphics& gr, double, float, const Colour* c)
        {
            const float a = ax(), r = rx();
            Line pts { { 0, 30 } };
            for (int i = 0; i <= 20; ++i) pts.push_back ({ 20 + (a - 20) * (float) i / 20, 30 + (H - 60) * (1 - std::exp (-(float) i / 20 * 3.2f)) / 0.96f });
            pts.push_back ({ a + 70, H - 30 });
            for (int i = 1; i <= 20; ++i) pts.push_back ({ a + 70 + (r - a - 70) * (float) i / 20, 30 + (H - 60) * std::exp (-(float) i / 20 * 3.2f) });
            pts.push_back ({ 390, 30 });
            TubeOptions o; o.clips = 150; tube (gr, pts, c[0], o);
            text (gr, "clamp", a, H + 8, 16, c[1], 0.5f, Align::centre);
            text (gr, "let go", r, H + 8, 16, c[2], 0.5f, Align::centre);
        };
    }
}

//==============================================================================
// LFO

void lfoWall (Builder& b)
{
    using namespace Param::LFO;
    auto* dev = &b.d;
    auto& wall = b.wall;
    b.out.pal[0] = C::VI; b.out.pal[1] = C::CY; b.out.pal[2] = C::PK;
    b.out.parts = { C::VI, C::VI, C::CY, C::PK };
    enum { A = 1, B, Cc };
    b.title ("LFO", 240, [] { return String ("slow motion for any control"); });

    auto shape = [dev] { return juce::jlimit (0, 4, (int) (dev->get (Shape) * 4.999f)); };
    auto hz = [dev] { return expMap (dev->get (Rate), 0.05f, 20); };
    auto value = [dev, shape, hz] (double t)
    {
        const double ph = t * hz() + dev->get (Phase);
        return Param::lfoValue (shape(), ph, dev->get (Smooth), dev->get (Chaos), dev->choice (Polarity) == 1) * dev->get (Depth);
    };

    {
        auto& g = wall.add (A, 100, 256, -0.6f, 520, 170, 640);
        const float WW = 1040, mid = 170, amp = 150, hxp = 780;
        auto& sh = g.words ({ { 0, "Sine" }, { 1, "Tri" }, { 2, "Saw" }, { 3, "Square" }, { 4, "S&H" } }, 300, -24);
        sh.size = 26; sh.gap = 24;
        sh.getChoice = shape;
        sh.setChoice = [dev] (int v) { dev->set (Shape, (float) v / 4.0f); };
        auto& a = g.dragArea ({ 0, 0, WW, 340 }, Cursor::UpDownLeftRightResizeCursor);
        a.start = [dev] { return std::vector<float> { dev->get (Rate), dev->get (Depth) }; };
        a.drag = [dev] (float dx, float dy, const std::vector<float>& s) { dev->set (Rate, s[0] + dx / 500); dev->set (Depth, s[1] - dy / 300); };
        a.tip = [dev, hz] { return "rate " + String (hz(), 2) + " hz   depth " + F::pct (dev->get (Depth)); };
        g.draw = [=] (juce::Graphics& gr, double t, float, const Colour* c)
        {
            const float span = juce::jlimit (1.0f, 8.0f, 3.2f / hz());
            auto y = [&] (float x) { return mid - value (t + (x - hxp) / WW * span) * amp; };
            juce::Path axis; axis.startNewSubPath (0, mid); axis.lineTo (WW, mid); dashes (gr, axis);
            Line pts;
            for (int i = 0; i <= 260; ++i) pts.push_back ({ (float) i * 4, y ((float) i * 4) });
            TubeOptions o; o.clips = 210; o.w = 1.2f; o.base = mid; tube (gr, pts, c[0], o);
            const float fade = dev->get (Fade);
            if (fade > 0.02f) { TubeOptions f; f.w = 0.7f; f.style = 0; tube (gr, Line { { 0, mid + amp + 26 }, { juce::jmin (WW, fade * 4 / span * WW), mid + amp - 4 } }, c[2], f); }
            bulb (gr, hxp, y (hxp), C::white, 1, 5.5f);
        };
    }

    // The phase wheel and the output meter
    {
        auto& g = wall.add (B, 1340, 426, 0, 0, 0, 320);
        const float R = 160;
        auto& h = g.handle ([dev, R] { const float a = dev->get (Phase) * juce::MathConstants<float>::twoPi - juce::MathConstants<float>::halfPi; return std::optional<P> (P (std::cos (a) * (R + 30), std::sin (a) * (R + 30))); });
        h.ci = 1; h.r = 6;
        h.tip = [dev] { return "phase   " + String (juce::roundToInt (dev->get (Phase) * 360)) + " deg"; };
        h.down = [dev] (float, float) -> std::optional<Gesture>
        {
            Gesture gs;
            gs.move = [dev] (float x, float y) { dev->set (Phase, std::fmod ((std::atan2 (y, x) + juce::MathConstants<float>::halfPi) / juce::MathConstants<float>::twoPi + 1.0f, 1.0f)); };
            return gs;
        };
        g.draw = [=] (juce::Graphics& gr, double t, float, const Colour* c)
        {
            const float v = value (t), a = (float) (t * hz() + dev->get (Phase)) * juce::MathConstants<float>::twoPi - juce::MathConstants<float>::halfPi;
            const bool uni = dev->choice (Polarity) == 1;
            const float x = 232;
            TubeOptions ring; ring.dashOn = 4; ring.dashOff = 8; ring.w = 0.9f;
            tube (gr, circle (0, 0, R, 48), c[0], ring);
            bulb (gr, std::cos (a) * R, std::sin (a) * R, c[2], 1, 7);
            TubeOptions s; s.w = 0.8f; s.style = 0;
            tube (gr, arcPoints (40, [&] (float u) { return P (-110 + u * 220, -Param::lfoShape (shape(), u) * 60); }), c[1], s);
            bulb (gr, 0, 0, c[2], clampf (std::abs (v) + 0.15f), 5);
            TubeOptions off; off.off = true; tube (gr, Line { { x, 150 }, { x, -150 } }, c[2], off);
            const float y0 = uni ? 150.0f : 0.0f, y1 = uni ? 150 - v * 300 : -v * 150;
            if (std::abs (y1 - y0) > 2) { TubeOptions o; o.style = 0; tube (gr, Line { { x, y0 }, { x, y1 } }, c[2], o); }
            bulb (gr, x, y1, c[2], 1, 5);
            text (gr, "out", x, 180, 16, c[2], 0.5f, Align::centre);
            const float wd = text (gr, "start", -20, -R - 46, 15, c[1], 0.5f);
            ringMark (gr, wd - 12, -R - 51, c[1]);
        };
    }

    // Phase, smoothing, fade in, chaos, sync, polarity, restart, and where it goes
    {
        auto& g = wall.add (Cc, 110, 690, 0.5f, 520, 80, 580);
        b.fader (g, Phase, "Phase", [] (float v) { return String (juce::roundToInt (v * 360)) + " deg"; }, 0, 30, 220, 30, P (110, 18), true);
        b.fader (g, Smooth, "Smooth", pctF, 270, 30, 490, 30, P (380, 42), true);
        b.fader (g, Fade, "Fade in", [] (float v) { return v < 0.02f ? String ("off") : String (v * 4, 1) + " s"; }, 540, 30, 760, 30, P (650, 18), true);
        b.fader (g, Chaos, "Chaos", pctF, 810, 30, 1030, 30, P (920, 42), true);
        b.choice (g, Sync, { "Free", "1/4", "1/8", "1/16", "1 bar" }, 0, 106, true).ci = 1;
        b.choice (g, Polarity, { "Bipolar", "Unipolar" }, 420, 106, true).ci = 2;
        b.toggle (g, Retrig, "restart on each note", 720, 106, true);
        g.draw = [dev] (juce::Graphics& gr, double, float, const Colour* c)
        {
            float x = text (gr, "feeds", 0, 172, 18, c[0], 0.55f) + 18;
            auto& slot = dev->ctx.project.slot (dev->track);
            int i = 0;
            for (auto& cb : slot.patch.cables)
            {
                if (cb.fromModule != dev->moduleId || ! Port::isModIn (cb.toPort)) continue;
                if (auto* m = slot.patch.find (cb.toModule))
                {
                    const auto& info = moduleInfo (m->type);
                    const String what = (info.paramName (cb.toPort - Port::modInBase) + " on " + info.title).toLowerCase();
                    x += text (gr, what, x, 172, 18, c[i++ % 2 ? 2 : 1], 0.9f) + 26;
                }
            }
            if (i == 0) text (gr, "nothing yet   patch its jack into a knob", x, 172, 18, c[1], 0.5f);
        };
    }
}
}
