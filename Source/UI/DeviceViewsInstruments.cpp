#include "DeviceViewsCommon.h"
#include <set>

// The Synth and the Sampler walls.
//
// Synth "Street": the signal runs left to right through objects you handle. A lens per oscillator
// (drag its lamp round the ring to morph the wave), octave stairs and a semitone ladder, a blend
// hourglass, a sub moon and a box of static; a unison fan; a window blind over the light the filter
// lets through (its cord sets cutoff and resonance, its spring the envelope); a heater coil for drive;
// the envelopes as a suspension bridge with a light per note; two pendulums for the LFOs and a patch
// bay for the matrix; a glide slide.
//
// Sampler "Projector": the sample is a strip of film. Frames between scissor cuts are the pads; a
// crank sets the speed or scratches the film; the lamp is the level and a burn on the gate drives it;
// the lens focus is the filter and its flare the resonance; an iris shutter opens and closes each
// note; the beam lands on a screen that shows the sound.
namespace DeviceViews
{
namespace
{
    constexpr float twoPi = juce::MathConstants<float>::twoPi;
    constexpr float halfPi = juce::MathConstants<float>::halfPi;

    const char* waveNames[] = { "Saw", "Square", "Tri", "Sine" };
    String waveWord (float v)
    {
        const float s = v * 3.0f;
        const int i = juce::roundToInt (s);
        if (std::abs (s - (float) i) < 0.06f) return waveNames[juce::jlimit (0, 3, i)];
        const int f = juce::jlimit (0, 2, (int) std::floor (s));
        return String (waveNames[f]) + " into " + waveNames[f + 1];
    }

    struct Preset { const char* name; std::vector<std::pair<int, float>> values; };
    const std::vector<Preset>& presets()
    {
        using namespace Param::Synth;
        static const std::vector<Preset> list {
            { "Night Bus Lead", {} },
            { "Glass Pluck",   { { Wave, 0.66f }, { WaveB, 0.33f }, { Attack, 0.0f }, { Decay, 0.3f }, { Sustain, 0.0f }, { Release, 0.32f }, { Cutoff, 0.42f }, { Env, 0.75f }, { Res, 0.4f }, { Sub, 0.1f } } },
            { "Cellar Bass",   { { Wave, 0.33f }, { OctA, 0.25f }, { Sub, 0.8f }, { Cutoff, 0.3f }, { Res, 0.15f }, { Env, 0.5f }, { Decay, 0.24f }, { Sustain, 0.3f }, { Drive, 0.55f }, { Detune, 0.05f } } },
            { "Slow Halo Pad", { { Wave, 0.1f }, { Unison, 0.5f }, { Spread, 0.6f }, { Attack, 0.6f }, { Release, 0.6f }, { Sustain, 0.8f }, { Cutoff, 0.5f }, { Env, 0.3f }, { Detune, 0.3f }, { WidthA, 0.3f } } } };
        return list;
    }

    float lfoHz (const Device& d, int rateParam) { return expMap (d.get (rateParam), 0.05f, 20.0f); }

    // How hard each LFO is patched in, for the pendulum's swing
    float lfoDepth (const Device& d, int lfo)
    {
        using namespace Param::Synth;
        float s = 0.0f;
        for (int row = 0; row < matrixRows; ++row)
            if (d.choice (MxSrc0 + row * 3) == lfo) s += std::abs (d.get (MxAmt0 + row * 3));
        return juce::jlimit (0.0f, 1.0f, s);
    }

    float modSum (const Device& d, int dest, double t)
    {
        using namespace Param::Synth;
        float s = 0.0f;
        for (int row = 0; row < matrixRows; ++row)
        {
            const float a = d.get (MxAmt0 + row * 3);
            if (std::abs (a) < 0.001f || d.choice (MxDst0 + row * 3) != dest) continue;
            const int src = d.choice (MxSrc0 + row * 3);
            if (src == Lfo1) s += a * Param::lfoShape (d.choice (L1Shape), t * lfoHz (d, L1Rate));
            else if (src == Lfo2) s += a * Param::lfoShape (d.choice (L2Shape), t * lfoHz (d, L2Rate));
        }
        return s;
    }

    std::vector<const VoiceView*> liveVoices (const Node* n)
    {
        std::vector<const VoiceView*> v;
        if (n == nullptr) return v;
        for (auto& view : n->views) if (view.pitch.load() >= 0) v.push_back (&view);
        std::sort (v.begin(), v.end(), [] (auto* a, auto* b) { return a->age.load() < b->age.load(); });
        return v;
    }

    // An arc, angles in radians measured the usual way (0 points right, growing clockwise on screen)
    Line arc (float cx, float cy, float r, float a0, float a1, int n = 40)
    {
        Line l;
        for (int i = 0; i <= n; ++i) { const float a = a0 + (a1 - a0) * (float) i / (float) n; l.push_back ({ cx + std::cos (a) * r, cy + std::sin (a) * r }); }
        return l;
    }
    float hash (float s) { const float x = std::sin (s * 12.9898f) * 43758.5453f; return x - std::floor (x); }
    std::optional<P> fromScratch (Group* g, const char* key, int i = 0)
    {
        auto& s = g->scratch[key];
        if ((int) s.size() < i * 2 + 2) return std::nullopt;
        return P (s[(size_t) i * 2], s[(size_t) i * 2 + 1]);
    }

    // A neon keyboard of 25 keys. `base` is the lowest note; `name` labels a key (empty: no label).
    void keyboard (Builder& b, int mod, float x, float y, float w, std::function<int()> base, std::function<String (int)> name)
    {
        auto* dev = &b.d;
        auto& g = b.wall.add (mod, x, y, 0, w / 2, 30, w * 0.4f);
        struct Key { int n; float x, w; bool black; };
        auto keys = std::make_shared<std::vector<Key>>();
        const float ww = w / 15.0f;
        int wi = 0;
        for (int n = 0; n < 25; ++n)
        {
            const int pc = n % 12;
            if (pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10) keys->push_back ({ n, (float) wi * ww - ww * 0.3f, ww * 0.6f, true });
            else keys->push_back ({ n, (float) wi++ * ww, ww, false });
        }
        auto keyAt = [keys] (float lx, float ly) -> int
        {
            if (ly < 34) for (auto& k : *keys) if (k.black && lx >= k.x && lx < k.x + k.w) return k.n;
            for (auto& k : *keys) if (! k.black && lx >= k.x && lx < k.x + k.w) return k.n;
            return -1;
        };
        auto& a = g.dragArea ({ 0, 0, w, 56 }, Cursor::PointingHandCursor);
        a.tip = [] { return String ("keys   or play a to p on your keyboard"); };
        a.down = [dev, keyAt, base] (float lx, float ly) -> std::optional<Gesture>
        {
            const int k = keyAt (lx, ly);
            auto held = std::make_shared<int> (k >= 0 ? base() + k : -1);
            if (*held >= 0) dev->note (*held, true, juce::jlimit (0.3f, 1.0f, 0.45f + ly / 100.0f));
            Gesture gs;
            gs.move = [dev, keyAt, held, base] (float px, float yy)
            {
                const int q = keyAt (px, yy);
                if (q >= 0 && base() + q != *held) { if (*held >= 0) dev->note (*held, false); *held = base() + q; dev->note (*held, true); }
            };
            gs.up = [dev, held] { if (*held >= 0) dev->note (*held, false); };
            return gs;
        };
        g.draw = [dev, keys, base, name, w, ww] (juce::Graphics& gr, double, float, const Colour* c)
        {
            std::set<int> down;
            if (auto* n = dev->live()) for (auto& v : n->views) if (v.pitch.load() >= 0 && v.released.load() < 0) down.insert (v.pitch.load());
            const int b0 = base();
            for (auto& k : *keys)
                if (down.count (b0 + k.n)) { gr.setColour (rgba (c[k.black ? 1 : 0], 0.3f)); gr.fillRect (k.x + 2, 2.0f, k.w - 4, (k.black ? 34.0f : 56.0f) - 4); }
            TubeOptions frame; frame.closed = true; frame.w = 0.85f; frame.clips = 220;
            tube (gr, roundRect (0, 0, w, 56, 7), c[0], frame);
            Lines whites, blacks;
            for (auto& k : *keys)
            {
                if (! k.black && k.x > 0)
                {
                    bool blackBefore = false;
                    for (auto& o : *keys) if (o.black && std::abs (o.x + o.w / 2 - k.x) < 2) blackBefore = true;
                    whites.push_back ({ { k.x, blackBefore ? 36.0f : 3.0f }, { k.x, 53.0f } });
                }
                if (k.black) blacks.push_back ({ { k.x + 2, 3 }, { k.x + 2, 34 }, { k.x + k.w - 2, 34 }, { k.x + k.w - 2, 3 } });
            }
            TubeOptions ow; ow.w = 0.5f; ow.style = 0; tube (gr, whites, c[0], ow);
            TubeOptions ob; ob.w = 0.6f; ob.style = 0; tube (gr, blacks, c[1], ob);
            for (auto& k : *keys)
                if (const auto s = name (b0 + k.n); s.isNotEmpty())
                    label (gr, s, k.x + (k.black ? k.w : ww) / 2, k.black ? 28.0f : 50.0f, Align::centre, Colour (k.black ? 0xff9a9ca3 : 0xffb9b8b2));
        };
    }
}

//==============================================================================
// Synth: Street

void synthWall (Builder& b)
{
    using namespace Param::Synth;
    auto& d = b.d;
    auto* dev = &d;
    auto& wall = b.wall;
    auto* w = &wall;
    b.out.pal[0] = C::VI; b.out.pal[1] = C::PK; b.out.pal[2] = C::CY;
    b.out.parts = { C::VI, C::VI, C::BL, C::PK, C::OR, C::CY, C::YL, C::BL };
    enum { osc = 1, fan, filt, heat, env, mot, voice };

    // Title, with the presets between two arrows
    auto preset = std::make_shared<int> (0);
    auto& tg = b.title ("Synth", 290, [preset] { return String (presets()[(size_t) *preset].name).toLowerCase(); });
    auto setPreset = [dev, w, preset] (int i)
    {
        const int n = (int) presets().size();
        *preset = ((i % n) + n) % n;
        if (auto* m = dev->module())
            for (int k = 0; k < Count; ++k) dev->setRaw (k, moduleInfo (m->type).defaultValue (k));
        for (auto& [k, v] : presets()[(size_t) *preset].values) dev->setRaw (k, v);
        w->say (String (presets()[(size_t) *preset].name).toLowerCase());
    };
    {
        auto line = [preset] { return String (presets()[(size_t) *preset].name).toLowerCase(); };
        tg.draw = [w, line] (juce::Graphics& g, double, float, const Colour* c)
        {
            bigTag (g, "Synth", 0, 96, c[0]);
            text (g, line(), 290 + 135, 36, 24, c[1], 0.9f, Align::centre);
            text (g, "signal street", 6, 166, 18, c[1], 0.7f);
            const double age = now() - w->readoutTime;
            if (age < 3.5) text (g, w->readout, 290, 112, 21, C::white, juce::jlimit (0.0f, 1.0f, 1.2f - (float) age / 3.0f) * 0.65f);
        };
        auto& prev = tg.toggle ("<", 290, 36, [] { return true; }, [setPreset, preset] (bool) { setPreset (*preset - 1); });
        prev.size = 26;
        auto& next = tg.toggle (">", 540, 36, [] { return true; }, [setPreset, preset] (bool) { setPreset (*preset + 1); });
        next.size = 26;
    }
    const auto pctF = [] (float v) { return F::pct (v); };

    // The newest voice's envelopes, for the pictures that move with the sound
    auto newest = [dev]() -> const VoiceView* { auto v = liveVoices (dev->live()); return v.empty() ? nullptr : v[0]; };

    //--------------------------------------------------------------------------
    // Oscillators: two lenses
    {
        auto& g = wall.add (osc, 40, 196, 0, 180, 250, 330);
        const float cx = 130, R = 72;
        struct Lens { int wave, inner; float cy; const char* name; };
        const Lens lenses[] = { { Wave, WidthA, 118, "a" }, { WaveB, Detune, 350, "b" } };
        for (const auto& ln : lenses)
        {
            const int wave = ln.wave, inner = ln.inner;
            const float cy = ln.cy;
            const String nm (ln.name);
            // The lamp on the rim: saw at the top, then square, triangle and sine round to 300 degrees
            auto& h = g.handle ([dev, wave, cx, cy, R]() -> std::optional<P>
            {
                const float a = dev->get (wave) * twoPi * (300.0f / 360.0f);
                return P (cx + std::sin (a) * R, cy - std::cos (a) * R);
            });
            h.r = 7;
            h.tip = [dev, wave, nm] { return "osc " + nm + "   " + waveWord (dev->get (wave)).toLowerCase(); };
            h.down = [dev, w, wave, cx, cy, nm] (float, float) -> std::optional<Gesture>
            {
                Gesture gs;
                gs.move = [dev, w, wave, cx, cy, nm] (float x, float y)
                {
                    float deg = juce::radiansToDegrees (std::atan2 (x - cx, -(y - cy)));
                    if (deg < 0) deg += 360.0f;
                    if (deg > 330.0f) deg = 0.0f;
                    dev->set (wave, juce::jmin (deg, 300.0f) / 300.0f);
                    w->say ("osc " + nm + "   " + waveWord (dev->get (wave)).toLowerCase());
                };
                return gs;
            };
            for (int i = 0; i < 4; ++i)
            {
                const float a = (float) i / 3.0f * twoPi * (300.0f / 360.0f), x = cx + std::sin (a) * (R + 22), y = cy - std::cos (a) * (R + 22) + 5;
                const String word = String (waveNames[i]).toLowerCase();
                const float v = (float) i / 3.0f;
                auto& t = g.toggle (word, i == 0 ? x - 12 : (x > cx ? x + 2 : x - textWidth (word, 14) - 2), i == 2 ? y + 8 : y,
                                    [dev, wave, v] { return std::abs (dev->get (wave) - v) < 0.02f; },
                                    [dev, w, wave, v, word, nm] (bool) { dev->set (wave, v); w->say ("osc " + nm + "   " + word); });
                t.size = 14; t.ci = 2;
            }
            // Inside the lens: A's pulse width, B's detune
            auto& in = g.dragArea ({ cx - R * 0.6f, cy - R * 0.6f, R * 1.2f, R * 1.2f }, Cursor::UpDownResizeCursor);
            in.start = [dev, inner] { return std::vector<float> { dev->get (inner) }; };
            in.drag = [dev, inner] (float, float dy, const std::vector<float>& s) { dev->set (inner, s[0] - dy / 140); };
            in.tip = inner == WidthA ? std::function<String()> ([dev] { return "osc a width   " + String (juce::roundToInt (5 + dev->get (WidthA) * 90)) + "%"; })
                                     : std::function<String()> ([dev] { return "osc b drifts   " + String (1200.0f * std::log2 (1.0f + dev->get (Detune) * 0.03f), 1) + " ct from a"; });
        }
        // Octave stairs for A
        {
            auto& st = g.dragArea ({ 252, 66, 110, 110 }, Cursor::PointingHandCursor);
            st.tip = [dev] { return "osc a   " + F::signedVal ((float) juce::roundToInt (dev->get (OctA) * 4 - 2), " oct", 0); };
            st.down = [dev, w] (float lx, float) -> std::optional<Gesture>
            {
                auto f = [dev, w] (float x, float) { dev->set (OctA, (float) juce::jlimit (0, 4, juce::roundToInt ((x - 262) / 20)) / 4.0f);
                                                     w->say ("osc a   " + F::signedVal ((float) juce::roundToInt (dev->get (OctA) * 4 - 2), " oct", 0)); };
                f (lx, 0);
                Gesture gs; gs.move = f; return gs;
            };
        }
        // A semitone ladder for B
        {
            auto& la = g.dragArea ({ 262, 290, 60, 120 }, Cursor::UpDownResizeCursor);
            la.tip = [dev] { return "osc b   " + F::signedVal ((float) juce::roundToInt (dev->get (SemiB) * 24 - 12), " st", 0); };
            la.down = [dev, w] (float, float ly) -> std::optional<Gesture>
            {
                auto f = [dev, w] (float, float y) { dev->set (SemiB, (float) juce::jlimit (0, 24, juce::roundToInt ((400 - y) / 100 * 24)) / 24.0f);
                                                     w->say ("osc b   " + F::signedVal ((float) juce::roundToInt (dev->get (SemiB) * 24 - 12), " st", 0)); };
                f (0, ly);
                Gesture gs; gs.move = f; return gs;
            };
            b.toggle (g, Sync, "hard sync", 236, 456);
        }
        auto& hg = g.dragArea ({ 372, 150, 56, 160 }, Cursor::UpDownResizeCursor);
        hg.start = [dev] { return std::vector<float> { dev->get (Blend) }; };
        hg.drag = [dev] (float, float dy, const std::vector<float>& s) { dev->set (Blend, s[0] + dy / 140); };
        hg.tip = [dev] { const float m = dev->get (Blend); return "blend   a " + F::pct (juce::jmin (1.0f, 2 * (1 - m))) + "   b " + F::pct (juce::jmin (1.0f, 2 * m)); };
        hg.isNew = true;
        auto& sub = g.dragArea ({ 40, 446, 120, 70 }, Cursor::UpDownResizeCursor);
        sub.start = [dev] { return std::vector<float> { dev->get (Sub) }; };
        sub.drag = [dev] (float, float dy, const std::vector<float>& s) { dev->set (Sub, s[0] - dy / 100); };
        sub.tip = [dev] { return "sub moon   " + F::pct (dev->get (Sub)); };
        auto& nz = g.dragArea ({ 366, 326, 84, 66 }, Cursor::UpDownResizeCursor);
        nz.start = [dev] { return std::vector<float> { dev->get (Param::Synth::Noise) }; };
        nz.drag = [dev] (float, float dy, const std::vector<float>& s) { dev->set (Param::Synth::Noise, s[0] - dy / 100); };
        nz.tip = [dev] { return "static   " + F::pct (dev->get (Param::Synth::Noise)); };

        g.draw = [dev, cx, R, newest] (juce::Graphics& gr, double t, float, const Colour* c)
        {
            const auto* v = newest();
            const float lv = v != nullptr ? v->level.load() : 0.0f;
            const float pwm = juce::jlimit (0.04f, 0.96f, 0.05f + dev->get (WidthA) * 0.9f + modSum (*dev, ToWidthA, t) * 0.35f);
            const float blend = dev->get (Blend), la = juce::jmin (1.0f, 2 * (1 - blend)), lb = juce::jmin (1.0f, 2 * blend);
            auto lens = [&] (float cy, float shape, float pw, float level, const char* name, float phase)
            {
                TubeOptions o; o.closed = true; o.w = 0.9f;
                tube (gr, circle (cx, cy, R, 64), c[0], o);
                TubeOptions inner; inner.closed = true; inner.off = true; inner.w = 0.45f;
                tube (gr, circle (cx, cy, R - 9, 64), c[0], inner);
                for (int i = 0; i < 4; ++i) { const float a = (float) i / 3.0f * twoPi * (300.0f / 360.0f); bulb (gr, cx + std::sin (a) * R, cy - std::cos (a) * R, c[2], 0.25f, 2.2f); }
                Line wl;
                for (int i = 0; i <= 96; ++i) { const float x = (float) i / 96.0f; wl.push_back ({ cx - R * 0.62f + x * R * 1.24f, cy - waveShape (shape, x * 1.5f + phase, pw) * R * 0.34f * (1 + lv * 0.15f) }); }
                TubeOptions wo; wo.w = 0.85f;
                if (level < 0.02f) wo.off = true;
                tube (gr, wl, c[1], wo);
                text (gr, name, cx - R - 8, cy - R + 8, 26, c[0], 0.8f, Align::right);
            };
            lens (118, dev->get (Wave) * 3, pwm, la, "a", 0);
            lens (350, dev->get (WaveB) * 3, 0.5f, lb, "b", (float) t * dev->get (Detune) * 0.6f);

            // Octave stairs (A) and the semitone ladder (B)
            {
                Line st;
                for (int i = 0; i < 5; ++i) { const float x = 262 + (float) i * 20, y = 158 - (float) i * 18; st.push_back ({ x - 10, y }); st.push_back ({ x + 10, y }); }
                TubeOptions so; so.w = 0.6f; so.style = 0; tube (gr, st, c[2], so);
                const int o = juce::roundToInt (dev->get (OctA) * 4);
                for (int i = 0; i < 5; ++i) bulb (gr, 262 + (float) i * 20, 158 - (float) i * 18 - 7, c[1], i == o ? 1.0f : 0.0f, 4.5f);
                text (gr, "octave " + F::signedVal ((float) (o - 2), "", 0), 252, 194, 14, c[2], 0.5f);
                Lines ticks;
                for (int s = 0; s <= 24; ++s) { const float y = 400 - (float) s / 24 * 100, len = s % 12 == 0 ? 12.0f : (s % 12 == 7 ? 8.0f : 4.0f); ticks.push_back ({ { 292 - len, y }, { 292 + len, y } }); }
                TubeOptions lo; lo.w = 0.35f; lo.style = 0; tube (gr, ticks, c[2], lo);
                TubeOptions rail; rail.w = 0.6f; rail.style = 0; tube (gr, Line { { 292, 296 }, { 292, 404 } }, c[2], rail);
                const int semi = juce::roundToInt (dev->get (SemiB) * 24);
                bulb (gr, 292, 400 - (float) semi / 24 * 100, c[1], 1, 5);
                text (gr, "semi " + F::signedVal ((float) (semi - 12), "", 0), 252, 428, 14, c[2], 0.5f);
            }
            // The blend hourglass
            {
                const Line hgl { { 376, 160 }, { 424, 160 }, { 404, 230 }, { 424, 300 }, { 376, 300 }, { 396, 230 } };
                TubeOptions o; o.closed = true; o.w = 0.7f; o.style = look().style == 1 ? 0 : look().style;
                tube (gr, hgl, c[0], o);
                auto sand = [&] (bool top, float amt)
                {
                    Lines ls;
                    const int n = juce::roundToInt (amt * 9);
                    for (int i = 0; i < n; ++i)
                    {
                        const float y = top ? 224 - (float) i * 7 : 296 - (float) i * 7;
                        const float k = top ? (230 - y) / 70 : (y - 230) / 70, hw = 4 + 20 * clamp01 (k);
                        ls.push_back ({ { 400 - hw + 3, y }, { 400 + hw - 3, y } });
                    }
                    if (! ls.empty()) { TubeOptions so; so.w = 0.6f; so.style = 0; tube (gr, ls, c[1], so); }
                };
                sand (true, 1 - dev->get (Blend));
                sand (false, dev->get (Blend));
                text (gr, "blend", 372, 150, 15, c[2], 0.55f);
                text (gr, "a", 432, 186, 15, c[0], 0.6f); text (gr, "b", 432, 290, 15, c[0], 0.6f);
            }
            // The sub moon and the static
            {
                Line mo = arc (100, 470, 30, 0.15f * juce::MathConstants<float>::pi, 0.85f * juce::MathConstants<float>::pi, 20);
                auto back = arc (100, 456, 30, 0.85f * juce::MathConstants<float>::pi, 0.15f * juce::MathConstants<float>::pi, 20);
                mo.insert (mo.end(), back.begin(), back.end());
                const float subLevel = dev->get (Sub);
                TubeOptions o; o.closed = true;
                if (subLevel > 0.02f) o.w = 0.5f + subLevel * 0.6f; else { o.off = true; o.w = 0.6f; }
                tube (gr, mo, c[1], o);
                text (gr, "sub", 84, 528, 15, c[2], 0.55f);
                TubeOptions bo; bo.closed = true; bo.off = true; bo.w = 0.45f;
                tube (gr, roundRect (372, 332, 70, 44, 8), c[2], bo);
                const int fr = (int) std::floor (t * 12.0);
                const float noise = dev->get (Param::Synth::Noise);
                for (int i = 0; i < juce::roundToInt (noise * 26); ++i)
                    bulb (gr, 380 + hash ((float) (fr * 31 + i)) * 54, 340 + hash ((float) (fr * 17 + i * 7)) * 28, c[1], 0.6f + 0.4f * hash ((float) (i + fr)), 1.4f);
                text (gr, "static", 382, 398, 15, c[2], 0.55f);
            }
        };
    }

    //--------------------------------------------------------------------------
    // Unison fan: open it to spread the voices, lamps choose how many
    {
        auto& g = wall.add (fan, 486, 196, 0, 110, 180, 240);
        auto& a = g.dragArea ({ 0, 30, 220, 230 }, Cursor::LeftRightResizeCursor);
        a.start = [dev] { return std::vector<float> { dev->get (Spread) }; };
        a.drag = [dev] (float dx, float, const std::vector<float>& s) { dev->set (Spread, s[0] + dx / 200); };
        a.tip = [dev] { return "fan   " + String (juce::roundToInt (dev->get (Spread) * 50)) + " cents across the voices"; };
        auto& u = g.counter (5, 0, 298);
        u.text = "voices"; u.dx = 22;
        u.getChoice = [dev] { return juce::roundToInt (dev->get (Unison) * 4); };
        u.setChoice = [dev] (int i) { dev->set (Unison, (float) i / 4); };
        u.tip = [dev] { const int n = 1 + juce::roundToInt (dev->get (Unison) * 4); return "unison   " + String (n) + (n > 1 ? " voices" : " voice"); };
        b.fader (g, Drift, "Drift", pctF, 0, 350, 214, 350);
        g.draw = [dev] (juce::Graphics& gr, double t, float, const Colour* c)
        {
            text (gr, "unison fan", 0, 22, 19, c[0], 0.9f);
            const float px = 110, py = 250, L = 205;
            const int n = 1 + juce::roundToInt (dev->get (Unison) * 4);
            const float sp = dev->get (Spread) * 0.95f, drift = dev->get (Drift);
            float lv = 0.0f;
            for (auto* v : liveVoices (dev->live())) lv = juce::jmax (lv, v->level.load());
            Lines blades;
            std::vector<P> tips;
            for (int i = 0; i < n; ++i)
            {
                const float ang = (n > 1 ? (float) i / (float) (n - 1) * 2 - 1 : 0.0f) * sp + std::sin ((float) t * 1.3f + (float) i * 2.1f) * drift * 0.035f;
                blades.push_back ({ { px + std::sin (ang) * 22, py - std::cos (ang) * 22 }, { px + std::sin (ang) * L, py - std::cos (ang) * L } });
                tips.push_back ({ px + std::sin (ang) * L, py - std::cos (ang) * L });
            }
            TubeOptions o; o.w = 0.75f; tube (gr, blades, c[0], o);
            if (n > 1) { TubeOptions dd; dd.dashOn = 3; dd.dashOff = 6; dd.w = 0.55f; dd.style = 0; tube (gr, arc (px, py, L, -halfPi - sp, -halfPi + sp, 30), c[1], dd); }
            for (auto& p : tips) bulb (gr, p.x, p.y, c[1], 0.25f + 0.75f * lv, 4.5f);
            bulb (gr, px, py, c[0], 1, 6);
        };
    }

    //--------------------------------------------------------------------------
    // Filter: a window blind over the light it lets through
    {
        auto& g = wall.add (filt, 740, 190, 0, 200, 190, 300);
        auto* gp = &g;
        const float top = 60, bot = 290, W = 400;
        auto X = [W] (float f) { return std::log (juce::jlimit (30.0f, 18000.0f, f) / 30.0f) / std::log (600.0f) * W; };
        b.choice (g, FMode, { "LP 24", "LP 12", "BP", "HP" }, 0, 32);
        auto& cord = g.handle ([gp] { return fromScratch (gp, "cord"); });
        cord.r = 7;
        cord.start = [dev] { return std::vector<float> { dev->get (Cutoff), dev->get (Res) }; };
        cord.drag = [dev] (float dx, float dy, const std::vector<float>& s) { dev->set (Cutoff, s[0] + dx / 400 * 1.068f); dev->set (Res, s[1] + dy / 170); };
        cord.tip = [dev] { return "blind   " + F::hz (expMap (dev->get (Cutoff), 40, 16000)).toLowerCase() + "   cord " + F::pct (dev->get (Res)) + " resonance"; };
        auto& spring = g.handle ([gp] { return fromScratch (gp, "spring"); });
        spring.r = 5; spring.ci = 1;
        spring.start = [dev] { return std::vector<float> { dev->get (Env) }; };
        spring.drag = [dev] (float dx, float, const std::vector<float>& s) { dev->set (Env, s[0] + dx / 260); };
        spring.tip = [dev] { return "each note pulls the blind open   " + F::pct (dev->get (Env)); };
        b.fader (g, Key, "Follows keys", pctF, 0, 340, 180, 340);
        b.fader (g, Vel, "Velocity opens", pctF, 220, 340, 400, 340);
        g.draw = [dev, gp, X, top, bot, W, newest] (juce::Graphics& gr, double t, float, const Colour* c)
        {
            const float base = expMap (dev->get (Cutoff), 40, 16000), res = dev->get (Res), envAmt = dev->get (Env);
            const int mode = dev->choice (FMode);
            float oct = modSum (*dev, ToCutoff, t) * 2;
            if (const auto* v = newest())
                oct += v->filter.load() * envAmt * 5 + (v->velocity.load() - 0.5f) * 3 * dev->get (Vel)
                       + std::log2 (440.0f * std::pow (2.0f, ((float) v->pitch.load() - 69) / 12.0f) / 261.6f) * dev->get (Key);
            const float live = juce::jlimit (25.0f, 19000.0f, base * std::pow (2.0f, oct));
            const float ex = X (live), bx = X (base);
            // The light let through: the response as bars, brighter while notes play
            const bool sounding = newest() != nullptr;
            static const int pictureMode[] = { 0, 4, 2, 3 };
            for (float x = 4; x < W; x += 6)
            {
                const float f = 30.0f * std::pow (600.0f, x / W), m = filterMagnitude (f, live, res, pictureMode[juce::jlimit (0, 3, mode)]);
                const float hgt = juce::jlimit (0.0f, 1.0f, (20.0f * std::log10 (juce::jmax (1e-5f, m)) + 36.0f) / 48.0f) * (bot - top - 24);
                if (hgt < 1) continue;
                gr.setColour (rgba (c[1], (sounding ? 0.32f : 0.14f) + 0.25f * hgt / (bot - top)));
                gr.drawLine (x, bot - 2, x, bot - 2 - hgt, 3.0f);
            }
            TubeOptions fo; fo.closed = true; fo.off = true; fo.w = 0.7f;
            tube (gr, roundRect (-6, top - 12, W + 12, bot - top + 18, 6), c[0], fo);
            TubeOptions ro; ro.w = 0.8f; ro.clips = 140;
            tube (gr, Line { { -10, top - 12 }, { W + 10, top - 12 } }, c[0], ro);
            auto slats = [&] (float x0, float x1)
            {
                if (x1 - x0 < 2) return;
                gr.setColour (juce::Colour (0x8c0a0a0c));
                gr.fillRect (x0, top - 6, x1 - x0, bot - top + 4);
                Lines ls;
                for (float y = top - 2; y < bot; y += 11) ls.push_back ({ { x0, y }, { x1, y } });
                TubeOptions so; so.w = 0.5f; so.style = 0; tube (gr, ls, c[0], so);
            };
            std::vector<float> edges;
            if (mode == HP) { slats (0, ex); edges.push_back (ex); }
            else if (mode == BP) { const float gap = 30 + (1 - res) * 110; slats (0, juce::jmax (0.0f, ex - gap / 2)); slats (juce::jmin (W, ex + gap / 2), W); edges = { ex - gap / 2, ex + gap / 2 }; }
            else { slats (ex, W); edges.push_back (ex); }
            for (float e : edges) { TubeOptions eo; eo.w = 0.8f + res * 1.1f; tube (gr, Line { { e, top - 8 }, { e, bot } }, c[1], eo); }
            const float hy = top + 40 + res * 170;
            TubeOptions co; co.w = 0.45f; co.style = 0; co.dashOn = 2; co.dashOff = 4;
            tube (gr, Line { { bx, top - 12 }, { bx, hy - 7 } }, c[2], co);
            const float sx = juce::jmin (W, X (base * std::pow (2.0f, envAmt * 5)));
            if (sx - bx > 8)
            {
                Line sp;
                for (int i = 0; i <= 40; ++i) { const float x = bx + (sx - bx) * (float) i / 40; sp.push_back ({ x, top - 4 + std::sin ((float) i / 40 * 31.4f) * 4 }); }
                TubeOptions so; so.w = 0.5f; so.style = 0; tube (gr, sp, c[1], so);
                text (gr, "spring", (bx + sx) / 2 - 18, top - 14, 13, c[1], 0.55f);
            }
            gp->scratch["cord"] = { bx, hy };
            gp->scratch["spring"] = { juce::jmax (bx + 10, sx), top - 4 };
            label (gr, "30 Hz", 0, bot + 22); label (gr, "18 kHz", W, bot + 22, Align::right);
            text (gr, "cord", bx + 12, hy + 5, 14, c[2], 0.5f);
        };
    }

    //--------------------------------------------------------------------------
    // Drive: a heater coil, and the level after it
    {
        auto& g = wall.add (heat, 1178, 196, 0, 70, 130, 170);
        auto& a = g.dragArea ({ 0, 30, 140, 200 }, Cursor::UpDownResizeCursor);
        a.start = [dev] { return std::vector<float> { dev->get (Drive) }; };
        a.drag = [dev] (float, float dy, const std::vector<float>& s) { dev->set (Drive, s[0] - dy / 150); };
        a.tip = [dev] { return "heat   " + F::pct (dev->get (Drive)) + " drive"; };
        b.fader (g, Gain, "Level", [] (float v) { return F::db (v); }, 6, 290, 130, 290);
        b.fader (g, VelAmp, "Vel level", pctF, 6, 340, 130, 340);
        g.draw = [dev] (juce::Graphics& gr, double t, float, const Colour* c)
        {
            text (gr, "heater", 0, 22, 19, c[0], 0.9f);
            const float h = dev->get (Drive);
            const auto col = mix (c[0], Colour (0xffffd9a8), h * 0.75f);
            Lines front, back;
            Line cur; bool curFront = true;
            for (int i = 0; i <= 160; ++i)
            {
                const float s = (float) i / 160, ang = s * 7 * twoPi, x = 66 + std::sin (ang) * 30, y = 56 + s * 160 + std::cos (ang) * 7;
                const bool f = std::cos (ang) > 0;
                if (i > 0 && f != curFront) { (curFront ? front : back).push_back (cur); cur.clear(); }
                curFront = f; cur.push_back ({ x, y });
            }
            (curFront ? front : back).push_back (cur);
            TubeOptions bo; bo.w = 0.55f; bo.off = h < 0.03f; tube (gr, back, col, bo);
            TubeOptions fo; fo.w = 0.65f + h * 0.7f; fo.off = h < 0.03f; tube (gr, front, col, fo);
            if (h > 0.03f)
            {
                Lines ls;
                for (int k = 0; k < 3; ++k)
                {
                    Line p;
                    for (int i = 0; i <= 12; ++i) p.push_back ({ 46 + (float) k * 20 + std::sin ((float) i * 0.9f + (float) t * 5 + (float) k) * 3 * h, 50 - (float) i * 3.2f });
                    ls.push_back (p);
                }
                TubeOptions so; so.w = 0.35f; so.style = 0; tube (gr, ls, col, so);
            }
            TubeOptions base; base.off = true; base.w = 0.6f;
            tube (gr, Lines { { { 66, 226 }, { 66, 250 } }, { { 50, 250 }, { 82, 250 } } }, c[2], base);
        };
    }

    //--------------------------------------------------------------------------
    // Envelopes: the amp as a suspension bridge, the filter's own as a lower deck
    {
        auto& g = wall.add (env, 738, 576, 0, 320, 110, 380);
        auto* gp = &g;
        // Where the bridge's parts sit, from the stored values
        struct Bridge { float x1, x2, x3, x4, top, gnd, ys; };
        auto geom = [dev] (bool filter)
        {
            const bool link = dev->on (Link);
            const int A = filter && ! link ? FAttack : Attack, D = filter && ! link ? FDecay : Decay, S = filter && ! link ? FSustain : Sustain, R = filter && ! link ? FRelease : Release;
            const float top = filter ? 110.0f : 34.0f, gnd = 190.0f;
            const float sus = dev->get (S) * (filter && link ? 0.5f : 1.0f);
            Bridge q;
            q.x1 = 20 + dev->get (A) * 150; q.x2 = q.x1 + 20 + dev->get (D) * 150; q.x3 = q.x2 + 110; q.x4 = q.x3 + 20 + dev->get (R) * 150;
            q.top = top; q.gnd = gnd; q.ys = gnd - (gnd - top) * sus;
            return q;
        };
        const auto sA = [] (float v) { return F::time (expMap (v, 0.001f, 4)); };
        const auto sD = [] (float v) { return F::time (expMap (v, 0.01f, 6)); };
        const auto sR = [] (float v) { return F::time (expMap (v, 0.01f, 8)); };
        auto handles = [&] (bool filter)
        {
            const int A = filter ? FAttack : Attack, D = filter ? FDecay : Decay, S = filter ? FSustain : Sustain, R = filter ? FRelease : Release;
            const String pre = filter ? "filter " : "";
            auto unlink = [dev, w, filter]
            {
                if (! filter || ! dev->on (Link)) return;
                dev->setRaw (Link, 0);
                dev->set (FAttack, inverse (expMap (dev->get (Attack), 0.001f, 4) * 0.7f, 0.001f, 4));
                dev->set (FDecay, dev->get (Decay)); dev->set (FSustain, dev->get (Sustain) * 0.5f); dev->set (FRelease, dev->get (Release));
                w->say ("filter envelope set free");
            };
            auto st = [dev, unlink, A, D, S, R] { unlink(); return std::vector<float> { dev->get (A), dev->get (D), dev->get (S), dev->get (R) }; };
            auto& h1 = g.handle ([geom, filter]() -> std::optional<P> { const auto q = geom (filter); return P (q.x1, q.top); });
            h1.r = filter ? 5.0f : 6.5f; h1.ci = filter ? 1 : 0; h1.start = st;
            h1.drag = [dev, A] (float dx, float, const std::vector<float>& s) { dev->set (A, s[0] + dx / 150); };
            h1.tip = [dev, A, sA, pre] { return pre + "rise   " + sA (dev->get (A)); };
            auto& h2 = g.handle ([geom, filter]() -> std::optional<P> { const auto q = geom (filter); return P (q.x2, q.ys); });
            h2.r = filter ? 5.0f : 6.5f; h2.ci = filter ? 1 : 0; h2.start = st;
            h2.drag = [dev, D, S, filter] (float dx, float dy, const std::vector<float>& s) { dev->set (D, s[1] + dx / 150); dev->set (S, s[2] - dy / (filter ? 80.0f : 156.0f)); };
            h2.tip = [dev, D, S, sD, pre] { return pre + "fall   " + sD (dev->get (D)) + "   deck " + F::pct (dev->get (S)); };
            auto& h3 = g.handle ([geom, filter]() -> std::optional<P> { const auto q = geom (filter); return P (q.x4, q.gnd); });
            h3.r = filter ? 5.0f : 6.5f; h3.ci = filter ? 1 : 0; h3.start = st;
            h3.drag = [dev, R] (float dx, float, const std::vector<float>& s) { dev->set (R, s[3] + dx / 150); };
            h3.tip = [dev, R, sR, pre] { return pre + "fade   " + sR (dev->get (R)); };
            if (filter) { h3.visible = [dev] { return ! dev->on (Link); }; }
        };
        handles (false);
        handles (true);
        b.toggle (g, Link, "filter follows amp", 440, 226);
        juce::ignoreUnused (gp);
        g.draw = [dev, geom] (juce::Graphics& gr, double, float, const Colour* c)
        {
            text (gr, "amp bridge", 0, -6, 19, c[0], 0.9f);
            auto ex = [] (float u) { return (std::exp (-4 * u) - std::exp (-4.0f)) / (1 - std::exp (-4.0f)); };
            auto path = [&] (const Bridge& q)
            {
                Line p { { 0, q.gnd }, { q.x1, q.top } };
                for (int i = 1; i <= 24; ++i) { const float u = (float) i / 24; p.push_back ({ q.x1 + (q.x2 - q.x1) * u, q.ys - (q.ys - q.top) * ex (u) }); }
                p.push_back ({ q.x3, q.ys });
                for (int i = 1; i <= 24; ++i) { const float u = (float) i / 24; p.push_back ({ q.x3 + (q.x4 - q.x3) * u, q.gnd - (q.gnd - q.ys) * ex (u) }); }
                return p;
            };
            const auto amp = geom (false), fil = geom (true);
            const auto ap = path (amp);
            // Hangers from the cable to the road
            Lines hang;
            for (float x = 8; x < amp.x4; x += 16)
            {
                float y = amp.gnd;
                for (size_t i = 1; i < ap.size(); ++i) if (ap[i].x >= x) { const float u = (x - ap[i - 1].x) / juce::jmax (0.01f, ap[i].x - ap[i - 1].x); y = ap[i - 1].y + (ap[i].y - ap[i - 1].y) * u; break; }
                if (amp.gnd - y > 8) hang.push_back ({ { x, y + 5 }, { x, amp.gnd } });
            }
            TubeOptions ho; ho.off = true; ho.w = 0.35f; tube (gr, hang, c[0], ho);
            TubeOptions go; go.off = true; go.w = 0.6f; tube (gr, Line { { -10, amp.gnd }, { 650, amp.gnd } }, c[0], go);
            TubeOptions to; to.w = 0.6f; to.style = 0;
            tube (gr, Lines { { { amp.x1, amp.gnd }, { amp.x1, amp.top - 16 } }, { { amp.x3, amp.gnd }, { amp.x3, amp.ys - 16 } } }, c[2], to);
            TubeOptions fo; fo.w = 0.6f; fo.dashOn = 4; fo.dashOff = 5; fo.style = 0;
            tube (gr, path (fil), c[1], fo);
            TubeOptions mo; mo.w = 0.95f; mo.clips = 120;
            tube (gr, ap, c[0], mo);
            text (gr, "rise", amp.x1 / 2 - 14, (amp.gnd + amp.top) / 2 - 6, 15, c[1], 0.6f);
            text (gr, "fall", (amp.x1 + amp.x2) / 2, amp.top + 18, 15, c[1], 0.6f);
            text (gr, "hold", (amp.x2 + amp.x3) / 2 - 12, amp.ys - 8, 15, c[1], 0.6f);
            text (gr, "fade", (amp.x3 + amp.x4) / 2, (amp.ys + amp.gnd) / 2 - 10, 15, c[1], 0.6f);
            text (gr, "filter", fil.x4 + 10, fil.gnd - 8, 13, c[1], 0.45f);
            // A light per note, driving over the bridge
            const float A = expMap (dev->get (Attack), 0.001f, 4), D = expMap (dev->get (Decay), 0.01f, 6), R = expMap (dev->get (Release), 0.01f, 8);
            int shown = 0;
            for (auto* v : liveVoices (dev->live()))
            {
                if (shown++ >= 6) break;
                const float el = v->age.load(), tr = v->released.load();
                float x;
                if (tr < 0 || el < tr)
                {
                    if (el < A) x = amp.x1 * el / A;
                    else { const float u = juce::jmin (1.0f, (el - A) / D); x = amp.x1 + (amp.x2 - amp.x1) * u; if (u >= 1) x = amp.x2 + juce::jmin (1.0f, (el - A - D) / 1.4f) * (amp.x3 - amp.x2); }
                }
                else x = amp.x3 + (amp.x4 - amp.x3) * juce::jmin (1.0f, (el - tr) / R);
                bulb (gr, x, amp.gnd - (amp.gnd - amp.top) * clamp01 (v->level.load()) - 6, C::white, 1, 4);
                bulb (gr, x, fil.gnd - (fil.gnd - fil.top) * clamp01 (v->filter.load()) - 4, c[1], 0.8f, 2.6f);
            }
        };
    }

    //--------------------------------------------------------------------------
    // Motion: two pendulums (the LFOs) and the patch bay (the matrix)
    {
        auto& g = wall.add (mot, 1336, 186, 0, 130, 180, 240);
        static const std::vector<String> shapes { "Sine", "Tri", "Saw", "Square", "S&H" };
        for (int k = 0; k < 2; ++k)
        {
            const int rate = k == 0 ? L1Rate : L2Rate, shape = k == 0 ? L1Shape : L2Shape;
            auto& a = g.dragArea ({ (float) k * 130, 40, 130, 270 }, Cursor::UpDownResizeCursor);
            a.start = [dev, rate] { return std::vector<float> { dev->get (rate) }; };
            a.drag = [dev, rate] (float, float dy, const std::vector<float>& s) { dev->set (rate, s[0] - dy / 170); };
            a.tip = [dev, rate, k] { return "pendulum " + String (k + 1) + "   " + String (lfoHz (*dev, rate), 2) + " hz   drag down: slower"; };
            auto& wd = b.choice (g, shape, shapes, 0, 352 + (float) k * 28);
            wd.size = 14; wd.gap = 9; wd.ci = k;
        }
        g.draw = [dev] (juce::Graphics& gr, double t, float, const Colour* c)
        {
            text (gr, "pendulums", 0, 22, 19, c[0], 0.9f);
            for (int k = 0; k < 2; ++k)
            {
                const int rate = k == 0 ? L1Rate : L2Rate, shape = dev->choice (k == 0 ? L1Shape : L2Shape);
                const float px = 65 + (float) k * 130, py = 44, L = 60 + (1 - dev->get (rate)) * 200;
                const float amp = (0.12f + lfoDepth (*dev, k == 0 ? Lfo1 : Lfo2)) * 0.5f;
                const float th = amp * Param::lfoShape (shape, t * lfoHz (*dev, rate));
                const float bx = px + std::sin (th) * L, by = py + std::cos (th) * L;
                TubeOptions bar; bar.w = 0.7f; bar.style = 0; tube (gr, Line { { px - 24, py - 6 }, { px + 24, py - 6 } }, c[2], bar);
                TubeOptions sw; sw.dashOn = 2; sw.dashOff = 6; sw.w = 0.5f; sw.style = 0;
                tube (gr, arc (px, py, L, halfPi - amp, halfPi + amp, 20), c[k], sw);
                TubeOptions rod; rod.w = 0.7f; tube (gr, Line { { px, py }, { bx, by } }, c[k], rod);
                TubeOptions bob; bob.closed = true; bob.w = 0.85f; tube (gr, circle (bx, by, 12, 24), c[k], bob);
                bulb (gr, bx, by, c[k], 0.45f + 0.55f * std::abs (th) / juce::jmax (0.01f, amp), 5);
                bulb (gr, px, py, c[2], 1, 3.5f);
                text (gr, String (lfoHz (*dev, rate), 2) + " hz", px, 336, 13, c[k], 0.5f, Align::centre);
            }
        };
    }
    {
        auto& g = wall.add (mot, 348, 690, 0, 170, 90, 240);
        static const std::vector<String> srcs { "Pendulum 1", "Pendulum 2", "Velocity", "Random" }, dsts { "Blind", "Pitch", "Width A", "Resonance" };
        for (int row = 0; row < matrixRows; ++row)
        {
            const float y = 40 + (float) row * 40;
            const int src = MxSrc0 + row * 3, dst = MxDst0 + row * 3, amt = MxAmt0 + row * 3;
            auto& s = g.add (Item::words);
            s.opts = { { 1, "" } }; s.x = 0; s.y = y + 6; s.size = 16; s.gap = 0; s.toggle = true; s.ci = 1;
            s.dyn = [dev, src] { return srcs[(size_t) juce::jlimit (0, 3, dev->choice (src))]; };
            s.getChoice = [] { return 1; };
            s.setChoice = [dev, w, src] (int) { const int v = (dev->choice (src) + 1) % 4; dev->setRaw (src, (float) v); w->say ("from   " + srcs[(size_t) v].toLowerCase()); };
            auto& f = g.fader (110, y, 230, y);
            f.bipolar = true; f.def = 0.5f;
            f.get = [dev, amt] { return dev->get (amt) / 2 + 0.5f; };
            f.set = [dev, amt] (float v) { float a = v * 2 - 1; if (std::abs (a) < 0.07f) a = 0; dev->setRaw (amt, a); };
            f.tip = [dev, src, dst, amt]
            {
                const float a = dev->get (amt);
                return (srcs[(size_t) juce::jlimit (0, 3, dev->choice (src))] + " to " + dsts[(size_t) juce::jlimit (0, 3, dev->choice (dst))] + "   "
                        + (a > 0 ? "+" : "") + String (juce::roundToInt (a * 100)) + "%").toLowerCase();
            };
            auto& t = g.add (Item::words);
            t.opts = { { 1, "" } }; t.x = 246; t.y = y + 6; t.size = 16; t.gap = 0; t.toggle = true; t.ci = 0;
            t.dyn = [dev, dst] { return dsts[(size_t) juce::jlimit (0, 3, dev->choice (dst))]; };
            t.getChoice = [] { return 1; };
            t.setChoice = [dev, w, dst] (int) { const int v = (dev->choice (dst) + 1) % 4; dev->setRaw (dst, (float) v); w->say ("to   " + dsts[(size_t) v].toLowerCase()); };
        }
        g.draw = [] (juce::Graphics& gr, double, float, const Colour* c) { text (gr, "patch bay", 0, 4, 19, c[0], 0.9f); };
    }

    //--------------------------------------------------------------------------
    // Voices: poly, mono or legato, and a slide for the glide
    {
        auto& g = wall.add (voice, 40, 744, 0, 130, 60, 200);
        b.choice (g, VMode, { "Poly", "Mono", "Legato" }, 0, 10).size = 18;
        auto& h = g.handle ([dev]() -> std::optional<P> { return P (40 + dev->get (Glide) * 220, 110); });
        h.r = 6;
        h.start = [dev] { return std::vector<float> { dev->get (Glide) }; };
        h.drag = [dev] (float dx, float, const std::vector<float>& s) { dev->set (Glide, s[0] + dx / 220); };
        h.tip = [dev] { const float v = dev->get (Glide); return "glide slide   " + (v < 0.01f ? String ("off") : String (juce::roundToInt (v * 800)) + " ms"); };
        auto lastPitch = std::make_shared<int> (-1);
        auto slideStart = std::make_shared<double> (-10.0);
        g.draw = [dev, lastPitch, slideStart] (juce::Graphics& gr, double, float, const Colour* c)
        {
            const float ex = 40 + dev->get (Glide) * 220;
            TubeOptions lo; lo.w = 0.5f; lo.style = 0;
            tube (gr, Lines { { { 14, 110 }, { 14, 40 } }, { { 30, 110 }, { 30, 40 } } }, c[2], lo);
            Lines rungs; for (float y = 48; y < 110; y += 12) rungs.push_back ({ { 14, y }, { 30, y } });
            TubeOptions ro; ro.w = 0.4f; ro.style = 0; tube (gr, rungs, c[2], ro);
            TubeOptions so; so.w = 0.85f; tube (gr, Line { { 30, 40 }, { ex, 110 } }, c[0], so);
            text (gr, "glide", ex + 14, 114, 15, c[1], 0.55f);
            auto vs = liveVoices (dev->live());
            const int p = vs.empty() ? -1 : vs[0]->pitch.load();
            if (p >= 0 && p != *lastPitch) { *lastPitch = p; *slideStart = now(); }
            const float gt = dev->get (Glide) * 0.8f;
            if (gt > 0.01f)
            {
                const float u = (float) juce::jlimit (0.0, 1.0, (now() - *slideStart) / gt);
                if (u < 1) bulb (gr, 30 + (ex - 30) * u, 40 + 70 * u - 6, C::white, 1, 4.5f);
            }
        };
    }

    keyboard (b, osc, 740, 846, 820, [] { return 48; }, [] (int n) { return n % 12 == 0 ? F::note (n) : String(); });
    wall.wires = {};
}

//==============================================================================
// Sampler: Projector

void samplerWall (Builder& b)
{
    using namespace Param::Sampler;
    auto& d = b.d;
    auto* dev = &d;
    auto& wall = b.wall;
    auto* w = &wall;
    b.out.pal[0] = C::PK; b.out.pal[1] = C::YL; b.out.pal[2] = C::BL;
    b.out.parts = { C::PK, C::PK, C::YL, C::GR, C::VI, C::CY, C::BL };
    enum { film = 1, crank, lamp, lens, shutter, screen };

    auto sample = std::make_shared<std::shared_ptr<const AudioData>> (d.sample());
    auto seconds = [sample] { return *sample != nullptr ? (float) (*sample)->lengthSeconds() : 1.0f; };
    auto peakAt = [sample] (float n)
    {
        if (*sample == nullptr || (*sample)->peaks.empty()) return 0.0f;
        const auto& pk = (*sample)->peaks;
        return pk[(size_t) juce::jlimit (0, (int) pk.size() - 1, (int) (n * (float) pk.size()))];
    };
    auto sEnd = [dev] { return juce::jmin (1.0f, dev->get (Start) + juce::jmax (0.01f, dev->get (Length))); };
    auto manual = [dev]
    {
        std::vector<float> m;
        for (int i = 0; i < maxMarkers; ++i) if (const float v = dev->get (Marker0 + i); v >= 0.0f) m.push_back (v);
        std::sort (m.begin(), m.end());
        return m;
    };
    auto setManual = [dev] (std::vector<float> m)
    {
        std::sort (m.begin(), m.end());
        for (int i = 0; i < maxMarkers; ++i) dev->setRaw (Marker0 + i, i < (int) m.size() ? m[(size_t) i] : -1.0f);
    };
    auto markers = [dev, sample, sEnd, manual]
    {
        Param::SliceSettings st;
        st.start = dev->get (Start); st.end = sEnd(); st.sens = dev->get (Sens);
        st.slicer = dev->choice (Slicer); st.grid = gridCount (dev->choice (Grid)); st.manual = manual();
        static const std::vector<std::pair<float, float>> none;
        return Param::sliceMarkers (st, *sample != nullptr ? (*sample)->onsets : none);
    };
    auto slices = [dev, sEnd, markers]
    {
        if (dev->choice (Mode) != Slice) return std::vector<std::pair<float, float>> { { dev->get (Start), sEnd() } };
        return Param::slicesFrom (dev->get (Start), sEnd(), markers());
    };
    auto newestLevel = [dev]
    {
        float lv = 0.0f;
        if (auto* n = dev->live()) for (auto& v : n->views) if (v.pitch.load() >= 0) lv = juce::jmax (lv, v.level.load());
        return lv;
    };

    b.title ("Sampler", 400, [sample, seconds]
    {
        if (*sample == nullptr) return String ("no film yet   load some audio");
        return (*sample)->fileName.toLowerCase() + " · " + String (seconds(), 2) + " s";
    });
    {
        auto& tg = *wall.groups.back();
        auto line = tg.draw;
        tg.draw = [line] (juce::Graphics& gr, double t, float dt, const Colour* c) { line (gr, t, dt, c); text (gr, "projector", 6, 166, 18, c[1], 0.7f); };
        tg.toggle ("load audio", 400, 74, [] { return true; }, [dev] (bool) { if (dev->loadAudio) dev->loadAudio(); });
        for (auto& it : tg.items) if (it->opts.size() == 1 && it->opts[0].second == "play") it->x = 530;
    }

    const auto pctF = [] (float v) { return F::pct (v); };
    const float FW = 1480, FH = 150;
    auto hover = std::make_shared<int> (-1);

    //--------------------------------------------------------------------------
    // The film strip
    {
        auto& g = wall.add (film, 60, 212, 0, 740, 75, 800);
        b.choice (g, Mode, { "Classic", "One-shot", "Slice", "Loop" }, 270, -42);
        auto& slicerW = b.choice (g, Slicer, { "Transient", "Grid", "Manual" }, 600, -42);
        slicerW.ci = 1;
        slicerW.visible = [dev] { return dev->choice (Mode) == Slice; };
        slicerW.setChoice = [dev, w, markers, setManual] (int v)
        {
            if (v == Manual && dev->get (Marker0) < 0.0f) setManual (markers());
            dev->setRaw (Slicer, (float) v);
            static const char* names[] = { "transient", "grid", "manual" };
            w->say (String ("cut by ") + names[juce::jlimit (0, 2, v)]);
        };
        auto& gridW = b.choice (g, Grid, { "4", "8", "16" }, 890, -42);
        gridW.ci = 1;
        gridW.visible = [dev] { return dev->choice (Mode) == Slice && dev->choice (Slicer) == GridSlices; };
        b.toggle (g, Reverse, "rewind", 1010, -42).ci = 2;
        b.fader (g, Sens, "Sensitivity", pctF, 1150, -46, 1320, -46).visible = [dev] { return dev->choice (Mode) == Slice && dev->choice (Slicer) == Transient; };
        b.fader (g, XFade, "Splice fade", [] (float v) { return String (juce::roundToInt (v * 120)) + " ms"; }, 1150, -46, 1320, -46).visible = [dev] { return dev->choice (Mode) == Loop; };

        struct HitResult { char kind = 0; int index = -1; };
        auto hit = [dev, sEnd, markers, FW] (float n) -> HitResult
        {
            const float a = dev->get (Start), e = sEnd();
            auto px = [FW] (float x, float y) { return std::abs (x - y) * FW; };
            if (dev->choice (Mode) == Loop)
            {
                const float la = a + (e - a) * dev->get (LoopA), lb = a + (e - a) * dev->get (LoopB);
                if (px (la, n) < 9) return { 'l' };
                if (px (lb, n) < 9) return { 'L' };
            }
            if (dev->choice (Mode) == Slice)
            {
                const auto m = markers();
                for (size_t i = 0; i < m.size(); ++i) if (px (m[i], n) < 9) return { 'm', (int) i };
            }
            if (px (a, n) < 10) return { 'a' };
            if (px (e, n) < 10) return { 'b' };
            return {};
        };
        auto& area = g.dragArea ({ -10, -24, FW + 20, FH + 34 }, Cursor::PointingHandCursor);
        area.hover = [hit, hover, FW] (float lx, float)
        {
            const auto h = hit (clamp01 (lx / FW));
            *hover = h.kind == 'm' ? h.index : -1;
            return h.kind != 0 ? Cursor::LeftRightResizeCursor : Cursor::PointingHandCursor;
        };
        area.tip = [dev, sEnd, seconds, slices]
        {
            String s = String ((sEnd() - dev->get (Start)) * seconds(), 2) + " s of film";
            if (dev->choice (Mode) == Slice) s += "   " + String ((int) slices().size()) + " frames   double-click to cut or join";
            return s;
        };
        area.doubleClick = [dev, w, markers, setManual, FW] (float lx, float)
        {
            if (dev->choice (Mode) != Slice) { dev->setRaw (Mode, (float) Slice); w->say ("cut into frames"); }
            const float n = clamp01 (lx / FW);
            auto m = markers();
            auto it = std::find_if (m.begin(), m.end(), [n, FW] (float x) { return std::abs (x - n) * FW < 12; });
            if (it != m.end()) m.erase (it);
            else if ((int) m.size() < maxMarkers) m.push_back (n);
            setManual (m);
            dev->setRaw (Slicer, (float) Manual);
        };
        area.down = [dev, w, hit, sEnd, slices, markers, manual, setManual, FW] (float lx, float) -> std::optional<Gesture>
        {
            const float n = clamp01 (lx / FW);
            const auto h = hit (n);
            Gesture gs;
            if (h.kind == 0)
            {
                // A frame is a pad
                auto played = std::make_shared<int> (-1);
                if (dev->choice (Mode) == Slice)
                {
                    const auto sl = slices();
                    for (size_t i = 0; i < sl.size(); ++i) if (n >= sl[i].first && n < sl[i].second) { *played = 36 + (int) i; w->say ("frame " + String ((int) i + 1)); }
                }
                else if (n >= dev->get (Start) && n <= sEnd())
                    *played = rootNote (dev->get (Root));
                if (*played >= 0) dev->note (*played, true, 0.9f);
                gs.up = [dev, played] { if (*played >= 0) dev->note (*played, false); };
                return gs;
            }
            auto held = std::make_shared<float> (-1.0f);
            if (h.kind == 'm')
            {
                const auto m = markers();
                if (dev->choice (Slicer) != Manual)
                {
                    setManual (m);
                    dev->setRaw (Slicer, (float) Manual);
                    w->say ("cuts are now yours to move");
                }
                *held = m[(size_t) h.index];
            }
            const char kind = h.kind;
            gs.move = [dev, kind, held, sEnd, manual, setManual, FW] (float x, float)
            {
                const float v = clamp01 (x / FW), a = dev->get (Start), e = sEnd();
                if (kind == 'a') { dev->set (Start, juce::jmin (v, e - 0.02f)); dev->set (Length, e - dev->get (Start)); }
                else if (kind == 'b') dev->set (Length, juce::jmax (0.02f, v - a));
                else if (kind == 'l') dev->set (LoopA, juce::jlimit (0.0f, dev->get (LoopB) - 0.02f, (v - a) / (e - a)));
                else if (kind == 'L') dev->set (LoopB, juce::jlimit (dev->get (LoopA) + 0.02f, 1.0f, (v - a) / (e - a)));
                else
                {
                    auto m = manual();
                    for (auto& mk : m)
                        if (std::abs (mk - *held) < 1e-6f) { mk = *held = juce::jlimit (a + 0.006f, e - 0.006f, v); break; }
                    setManual (m);
                }
            };
            return gs;
        };
        g.draw = [dev, sEnd, slices, markers, peakAt, hover, seconds, FW, FH] (juce::Graphics& gr, double, float, const Colour* c)
        {
            const float top = 26, bot = 124, mid = 75, a = dev->get (Start), e = sEnd();
            const bool rev = dev->on (Reverse);
            const int mode = dev->choice (Mode);
            auto X = [FW] (float n) { return n * FW; };
            const auto sl = slices();
            const auto* node = dev->live();
            std::vector<float> heads;
            if (node != nullptr)
                for (auto& v : node->views)
                    if (v.pitch.load() >= 0)
                    {
                        heads.push_back (X (v.pos.load()));
                        if (mode == Slice)
                            if (const int si = v.slice.load(); si >= 0 && si < (int) sl.size())
                            {
                                gr.setColour (rgba (c[1], 0.04f + 0.12f * v.level.load()));
                                gr.fillRect (X (sl[(size_t) si].first), top, X (sl[(size_t) si].second) - X (sl[(size_t) si].first), bot - top);
                            }
                    }
            if (dev->on (Scrub)) heads.push_back (X (dev->get (ScrubPos)));
            Lines lit, dark;
            for (float x = 2; x < FW; x += 4)
            {
                const float n = x / FW, hh = juce::jmax (1.0f, peakAt (rev ? 1 - n : n) * 44);
                (n >= a && n <= e ? lit : dark).push_back ({ { x, mid - hh }, { x, mid + hh } });
            }
            if (! dark.empty()) { TubeOptions o; o.off = true; o.w = 0.6f; tube (gr, dark, c[0], o); }
            if (! lit.empty()) { TubeOptions o; o.w = 0.6f; o.step = 6; tube (gr, lit, c[0], o); }
            // The film's edges and sprocket holes, lit where a playhead passes
            TubeOptions eo; eo.w = 0.6f; eo.clips = 200;
            tube (gr, Lines { { { -14, 0 }, { FW + 14, 0 } }, { { -14, FH }, { FW + 14, FH } } }, c[2], eo);
            Lines holes;
            for (float x = 6; x < FW; x += 24) { holes.push_back (roundRect (x, 6, 10, 9, 2)); holes.push_back (roundRect (x, FH - 15, 10, 9, 2)); }
            TubeOptions hoo; hoo.off = true; hoo.closed = true; hoo.w = 0.4f; tube (gr, holes, c[2], hoo);
            for (float px : heads)
                for (float x = 6; x < FW; x += 24)
                    if (std::abs (x + 5 - px) < 30) { const float lv = 1 - std::abs (x + 5 - px) / 30; bulb (gr, x + 5, 10.5f, c[1], lv, 3); bulb (gr, x + 5, FH - 10.5f, c[1], lv, 3); }
            if (mode == Slice)
            {
                for (size_t i = 0; i < sl.size(); ++i) text (gr, String ((int) i + 1), X (sl[i].first) + 8, top + 16, 16, c[1], 0.75f);
                const auto m = markers();
                for (size_t i = 0; i < m.size(); ++i)
                {
                    const float x = X (m[i]);
                    TubeOptions o; o.dashOn = 3; o.dashOff = 5; o.w = *hover == (int) i ? 1.15f : 0.75f;
                    tube (gr, Line { { x, top - 8 }, { x, bot + 8 } }, c[1], o);
                    TubeOptions so; so.w = 0.5f; so.style = 0; so.closed = true;
                    tube (gr, Lines { circle (x - 5, -14, 4, 12), circle (x + 5, -14, 4, 12) }, c[1], so);
                    TubeOptions bl; bl.w = 0.5f; bl.style = 0;
                    tube (gr, Lines { { { x - 3, -11 }, { x + 6, 0 } }, { { x + 3, -11 }, { x - 6, 0 } } }, c[1], bl);
                }
            }
            if (mode == Loop)
            {
                const float la = X (a + (e - a) * dev->get (LoopA)), lb = X (a + (e - a) * dev->get (LoopB));
                gr.setColour (rgba (c[2], 0.06f)); gr.fillRect (la, top, lb - la, bot - top);
                TubeOptions o; o.w = 0.9f; o.style = 0;
                tube (gr, Line { { la, top + 18 }, { la, top }, { lb, top }, { lb, top + 18 } }, c[2], o);
                text (gr, "loop", (la + lb) / 2, top + 22, 16, c[2], 0.8f, Align::centre);
            }
            for (auto [n, s] : { std::pair<float, float> { a, 1.0f }, { e, -1.0f } })
            {
                const float x = X (n);
                TubeOptions o; o.w = 1.1f; o.style = 0; tube (gr, Line { { x, -8 }, { x, FH + 8 } }, c[2], o);
                TubeOptions f; f.w = 0.6f; f.style = 0; tube (gr, Line { { x, FH + 8 }, { x + 12 * s, FH + 8 }, { x + 12 * s, FH - 4 } }, c[2], f);
            }
            gr.setColour (juce::Colour (0x99080809));
            if (a > 0) gr.fillRect (0.0f, 2.0f, X (a), FH - 4);
            if (e < 1) gr.fillRect (X (e), 2.0f, FW - X (e), FH - 4);
            for (float px : heads)
            {
                gr.setColour (juce::Colours::white.withAlpha (0.25f)); gr.drawLine (px, top - 4, px, bot + 4, 6.0f);
                gr.setColour (juce::Colours::white.withAlpha (0.9f)); gr.drawLine (px, top - 4, px, bot + 4, 1.5f);
            }
            const float sec = seconds();
            const int nt = sec < 12 ? juce::jmax (2, juce::roundToInt (sec * 2)) : 12;
            for (int i = 1; i < nt; ++i) label (gr, String (sec * (float) i / (float) nt, sec < 12 ? 1 : 0) + "s", (float) i / (float) nt * FW, FH + 20, Align::centre);
        };
    }

    const float PX = 300, PY = 400;    // where the projector stands

    //--------------------------------------------------------------------------
    // The crank: speed and pitch, or a scratch
    {
        auto& g = wall.add (crank, PX, PY, 0, 0, 150, 170);
        auto scratchMode = std::make_shared<bool> (false);
        auto angle = std::make_shared<float> (0.0f);
        const float cx = 0, cy = 150, R = 58;
        auto& a = g.dragArea ({ cx - R - 14, cy - R - 14, 2 * R + 28, 2 * R + 28 }, Cursor::DraggingHandCursor);
        a.tip = [dev, scratchMode]
        {
            if (*scratchMode) return String ("scratch   wind it either way");
            const int st = juce::roundToInt ((dev->get (Pitch) - 0.5f) * 48);
            return "crank   " + F::signedVal ((float) st, " st", 0) + "   " + String (std::pow (2.0f, (float) st / 12.0f), 2) + "x speed";
        };
        a.down = [dev, w, scratchMode, angle, cx, cy, sEnd, seconds] (float lx, float ly) -> std::optional<Gesture>
        {
            auto last = std::make_shared<float> (std::atan2 (lx - cx, -(ly - cy)));
            auto setting = std::make_shared<float> ((dev->get (Pitch) - 0.5f) * 48.0f / 24.0f * 0.75f * juce::MathConstants<float>::pi);
            if (*scratchMode)
            {
                float start = dev->get (Start);
                if (auto* n = dev->live()) for (auto& v : n->views) if (v.pitch.load() >= 0) { start = v.pos.load(); break; }
                dev->setRaw (ScrubPos, start);
                dev->setRaw (Scrub, 1.0f);
            }
            Gesture gs;
            gs.move = [dev, w, scratchMode, angle, last, setting, cx, cy, sEnd, seconds] (float x, float y)
            {
                const float an = std::atan2 (x - cx, -(y - cy));
                float dA = an - *last;
                if (dA > juce::MathConstants<float>::pi) dA -= twoPi;
                if (dA < -juce::MathConstants<float>::pi) dA += twoPi;
                *last = an;
                if (*scratchMode)
                {
                    *angle += dA;
                    const float a0 = dev->get (Start), e0 = sEnd(), span = juce::jmax (0.001f, e0 - a0);
                    float p = dev->get (ScrubPos) + dA / twoPi * 0.55f / juce::jmax (0.05f, seconds());
                    while (p > e0) p -= span;
                    while (p < a0) p += span;
                    dev->setRaw (ScrubPos, p);
                }
                else
                {
                    const float lim = 0.75f * juce::MathConstants<float>::pi;
                    *setting = juce::jlimit (-lim, lim, *setting + dA);
                    const int st = juce::roundToInt (*setting / lim * 24.0f);
                    dev->set (Pitch, 0.5f + (float) st / 48.0f);
                    w->say ("crank   " + F::signedVal ((float) st, " st", 0));
                }
            };
            gs.up = [dev, scratchMode] { if (*scratchMode) dev->setRaw (Scrub, 0.0f); };
            return gs;
        };
        auto& modes = g.words ({ { 0, "speed" }, { 1, "scratch" } }, -56, 262);
        modes.size = 17;
        modes.getChoice = [scratchMode] { return *scratchMode ? 1 : 0; };
        modes.setChoice = [w, scratchMode] (int v) { *scratchMode = v == 1; w->say (v == 1 ? "wind the crank to scratch the film" : "the crank sets the speed and pitch"); };
        b.fader (g, Fine, "Fine", [] (float v) { return F::signedVal ((float) juce::roundToInt ((v - 0.5f) * 100), " ct", 0); }, -70, 322, 60, 322, {}, false, true);
        b.fader (g, Root, "Root key", [] (float v) { return F::note (rootNote (v)); }, -70, 372, 60, 372);
        auto& warp = b.choice (g, Warp, { "Repitch", "Stretch" }, 80, 330);
        warp.size = 16;
        g.draw = [dev, scratchMode, angle, cx, cy, R] (juce::Graphics& gr, double, float, const Colour* c)
        {
            text (gr, "crank", -60, 64, 19, c[0], 0.9f);
            const float lim = 0.75f * juce::MathConstants<float>::pi;
            const int st = juce::roundToInt ((dev->get (Pitch) - 0.5f) * 48);
            if (! *scratchMode)
            {
                Lines ticks;
                for (int s = -24; s <= 24; s += 2)
                {
                    const float an = (float) s / 24 * lim - halfPi, r1 = R + (s % 12 ? 4.0f : 10.0f);
                    ticks.push_back ({ { cx + std::cos (an) * R, cy + std::sin (an) * R }, { cx + std::cos (an) * r1, cy + std::sin (an) * r1 } });
                }
                TubeOptions o; o.w = 0.4f; o.style = 0; tube (gr, ticks, c[2], o);
                label (gr, "-24", cx - R - 12, cy + R + 8, Align::centre); label (gr, "+24", cx + R + 12, cy + R + 8, Align::centre); label (gr, "0", cx, cy - R - 14, Align::centre);
            }
            else { TubeOptions o; o.dashOn = 2; o.dashOff = 6; o.w = 0.5f; o.style = 0; tube (gr, circle (cx, cy, R, 48), c[2], o); }
            const float an = (*scratchMode ? *angle : (float) st / 24 * lim) - halfPi;
            const float hx = cx + std::cos (an) * R * 0.82f, hy = cy + std::sin (an) * R * 0.82f;
            TubeOptions arm; arm.w = 0.9f; tube (gr, Line { { cx, cy }, { hx, hy } }, c[0], arm);
            TubeOptions kn; kn.closed = true; kn.w = 0.7f; tube (gr, circle (hx, hy, 8, 16), c[0], kn);
            bulb (gr, hx, hy, c[1], 1, 4.5f); bulb (gr, cx, cy, c[2], 1, 4);
            text (gr, *scratchMode ? String ("wind it") : F::signedVal ((float) st, " st", 0), cx, cy + R + 40, 16, c[1], 0.7f, Align::centre);
        };
    }

    //--------------------------------------------------------------------------
    // The body: the lamp is the level, a burn on the gate is the drive; tape and crush below
    {
        auto& g = wall.add (lamp, PX, PY, 0, 290, 150, 260);
        auto& la = g.dragArea ({ 150, 90, 110, 120 }, Cursor::UpDownResizeCursor);
        la.start = [dev] { return std::vector<float> { dev->get (Gain) }; };
        la.drag = [dev] (float, float dy, const std::vector<float>& s) { dev->set (Gain, s[0] - dy / 120); };
        la.tip = [dev] { return "lamp   " + F::db (dev->get (Gain)).toLowerCase(); };
        auto& bu = g.dragArea ({ 300, 84, 100, 134 }, Cursor::UpDownResizeCursor);
        bu.start = [dev] { return std::vector<float> { dev->get (Drive) }; };
        bu.drag = [dev] (float, float dy, const std::vector<float>& s) { dev->set (Drive, s[0] - dy / 130); };
        bu.tip = [dev] { return "burn   " + F::pct (dev->get (Drive)) + " drive"; };
        b.fader (g, Tape, "Tape", pctF, 130, 300, 290, 300);
        b.fader (g, Crush, "Crush", [] (float v) { return v < 0.02f ? String ("off") : String (16 - juce::roundToInt (v * 12)) + " bit"; }, 320, 300, 480, 300);
        g.draw = [dev, peakAt, newestLevel] (juce::Graphics& gr, double, float, const Colour* c)
        {
            TubeOptions bo; bo.closed = true; bo.w = 0.85f; bo.clips = 160;
            tube (gr, roundRect (120, 60, 370, 190, 18), c[0], bo);
            TubeOptions legs; legs.off = true; legs.w = 0.7f;
            tube (gr, Lines { { { 140, 250 }, { 130, 272 } }, { { 470, 250 }, { 480, 272 } } }, c[0], legs);
            TubeOptions feed; feed.off = true; feed.w = 0.45f;
            tube (gr, Lines { { { 360, -48 }, { 350, 60 } }, { { 380, -48 }, { 370, 60 } } }, c[2], feed);
            const float gain = dev->get (Gain);
            bulb (gr, 205, 150, c[1], 0.25f + 0.75f * gain, 14 + gain * 6);
            TubeOptions ring; ring.closed = true; ring.w = 0.5f; ring.style = 0; tube (gr, circle (205, 150, 30, 30), c[0], ring);
            text (gr, "lamp", 182, 236, 15, c[2], 0.55f);
            TubeOptions gate; gate.closed = true; gate.w = 0.6f; gate.style = 0; tube (gr, roundRect (310, 92, 80, 120, 6), c[1], gate);
            if (auto* n = dev->live())
                for (auto& v : n->views)
                    if (v.pitch.load() >= 0)
                    {
                        Lines ls;
                        const float p = v.pos.load();
                        for (int i = 0; i < 20; ++i) { const float hh = peakAt (p + ((float) i - 10) / 20 * 0.03f) * 32; ls.push_back ({ { 350 - hh, 100 + (float) i * 5.6f }, { 350 + hh, 100 + (float) i * 5.6f } }); }
                        TubeOptions fo; fo.w = 0.4f; fo.style = 0; tube (gr, ls, c[1], fo);
                        break;
                    }
            const float burn = dev->get (Drive);
            if (burn > 0.02f)
            {
                Line scorch;
                const float r = 6 + burn * 30;
                for (int i = 0; i <= 30; ++i) { const float an = (float) i / 30 * twoPi; scorch.push_back ({ 350 + std::cos (an) * r * (1 + 0.25f * std::sin (an * 5 + 1)), 152 + std::sin (an) * r * (1 + 0.25f * std::cos (an * 4)) }); }
                TubeOptions so; so.closed = true; so.w = 0.6f + burn * 0.6f; so.style = 0;
                tube (gr, scorch, mix (c[1], Colour (0xffffb070), 0.6f), so);
            }
            text (gr, "burn", 334, 236, 15, c[2], 0.55f);
            text (gr, "gate", 330, 84, 13, c[2], 0.4f);
            juce::ignoreUnused (newestLevel);
        };
    }

    //--------------------------------------------------------------------------
    // The lens: focus is the filter, the flare its resonance
    {
        auto& g = wall.add (lens, PX, PY, 0, 560, 150, 180);
        auto& fo = g.dragArea ({ 520, 80, 70, 140 }, Cursor::UpDownResizeCursor);
        fo.start = [dev] { return std::vector<float> { dev->get (Cutoff) }; };
        fo.drag = [dev] (float, float dy, const std::vector<float>& s) { dev->set (Cutoff, s[0] - dy / 160); };
        fo.tip = [dev] { return "focus   " + F::hz (expMap (dev->get (Cutoff), 40, 16000)).toLowerCase() + "   soft focus sounds darker"; };
        auto& fl = g.dragArea ({ 598, 104, 50, 90 }, Cursor::UpDownResizeCursor);
        fl.start = [dev] { return std::vector<float> { dev->get (Res) }; };
        fl.drag = [dev] (float, float dy, const std::vector<float>& s) { dev->set (Res, s[0] - dy / 120); };
        fl.tip = [dev] { return "flare   " + F::pct (dev->get (Res)) + " resonance"; };
        auto& md = b.choice (g, FMode, { "LP", "BP", "HP" }, 500, 52);
        md.size = 15; md.gap = 10;
        g.draw = [dev] (juce::Graphics& gr, double, float, const Colour* c)
        {
            TubeOptions bar; bar.w = 0.8f;
            tube (gr, Line { { 490, 104 }, { 600, 96 }, { 600, 204 }, { 490, 196 } }, c[0], bar);
            const float focus = dev->get (Cutoff), rx = 12, ry = 58;
            Line ring;
            for (int i = 0; i <= 40; ++i) { const float an = (float) i / 40 * twoPi; ring.push_back ({ 550 + std::cos (an) * rx, 150 + std::sin (an) * ry }); }
            TubeOptions ro; ro.closed = true; ro.w = 0.8f; tube (gr, ring, c[1], ro);
            Lines marks;
            for (int i = 0; i < 14; ++i) { const float an = (float) i / 14 * twoPi + focus * 4; if (std::cos (an) > 0) marks.push_back ({ { 550 + std::cos (an) * rx - 2, 150 + std::sin (an) * ry }, { 550 + std::cos (an) * rx + 3, 150 + std::sin (an) * ry } }); }
            TubeOptions mo; mo.w = 0.5f; mo.style = 0; tube (gr, marks, c[1], mo);
            text (gr, "focus", 526, 236, 15, c[2], 0.55f);
            const float res = dev->get (Res), s = 6 + res * 22, sx = 616, sy = 150;
            Lines star;
            for (int i = 0; i < 4; ++i)
            {
                const float an = (float) i / 4 * juce::MathConstants<float>::pi, k = i % 2 ? 0.55f : 1.0f;
                star.push_back ({ { sx - std::cos (an) * s * k, sy - std::sin (an) * s * k }, { sx + std::cos (an) * s * k, sy + std::sin (an) * s * k } });
            }
            TubeOptions so; so.w = 0.45f + res * 0.4f; so.style = 0; tube (gr, star, c[1], so);
            text (gr, "flare", 602, 92, 13, c[2], 0.5f);
        };
    }

    //--------------------------------------------------------------------------
    // The shutter: opens with the attack, closes with the release; how far it stays open is the sustain
    {
        auto& g = wall.add (shutter, PX, PY, 0, 690, 150, 150);
        const float cx = 690, cy = 150, R = 40;
        auto& op = b.fader (g, Attack, "Open", [] (float v) { return F::time (expMap (v, 0.001f, 2)); }, 0, 0, 1, 0);
        op.pts = arc (cx, cy, R + 18, -juce::MathConstants<float>::pi * 0.92f, -juce::MathConstants<float>::pi * 0.08f);
        op.lx = cx - 28; op.ly = cy - R - 34;
        auto& cl = b.fader (g, Release, "Close", [] (float v) { return F::time (expMap (v, 0.005f, 4)); }, 0, 0, 1, 0);
        cl.pts = arc (cx, cy, R + 18, juce::MathConstants<float>::pi * 0.92f, juce::MathConstants<float>::pi * 0.08f);
        cl.lx = cx - 26; cl.ly = cy + R + 46;
        auto& su = g.dragArea ({ cx - R * 0.7f, cy - R * 0.7f, R * 1.4f, R * 1.4f }, Cursor::UpDownResizeCursor);
        su.start = [dev] { return std::vector<float> { dev->get (Sustain) }; };
        su.drag = [dev] (float, float dy, const std::vector<float>& s) { dev->set (Sustain, s[0] - dy / 100); };
        su.tip = [dev] { return "stays open   " + F::pct (dev->get (Sustain)) + " sustain"; };
        b.fader (g, Hold, "Hold", [] (float v) { return String (juce::roundToInt (v * 500)) + " ms"; }, 600, 300, 700, 300);
        b.fader (g, Decay, "Settle", [] (float v) { return F::time (expMap (v, 0.01f, 6)); }, 730, 300, 830, 300);
        g.draw = [dev, cx, cy, R] (juce::Graphics& gr, double, float, const Colour* c)
        {
            float lv = 0.0f;
            if (auto* n = dev->live()) for (auto& v : n->views) if (v.pitch.load() >= 0) lv = juce::jmax (lv, v.level.load());
            const float ir = 4 + clamp01 (lv) * (R - 8);
            Lines blades;
            for (int i = 0; i < 6; ++i)
            {
                const float an = (float) i / 6 * twoPi + lv * 0.6f;
                blades.push_back ({ { cx + std::cos (an) * R, cy + std::sin (an) * R }, { cx + std::cos (an + 1.25f) * ir, cy + std::sin (an + 1.25f) * ir } });
            }
            TubeOptions o; o.closed = true; o.w = 0.8f; tube (gr, circle (cx, cy, R, 40), c[0], o);
            TubeOptions bo; bo.w = 0.5f; bo.style = 0; tube (gr, blades, c[0], bo);
            TubeOptions io; io.closed = true; io.w = 0.6f; io.style = 0; tube (gr, circle (cx, cy, ir, 24), c[1], io);
            TubeOptions so; so.closed = true; so.w = 0.35f; so.style = 0; so.dashOn = 2; so.dashOff = 4;
            tube (gr, circle (cx, cy, 4 + dev->get (Sustain) * (R - 8), 24), c[2], so);
            text (gr, "shutter", cx + R + 26, cy + 5, 15, c[2], 0.5f);
        };
    }

    //--------------------------------------------------------------------------
    // The screen: the beam lands here and shows the sound; pan, velocity and voices below it
    {
        auto& g = wall.add (screen, 1150, 392, 0, 195, 110, 220);
        const float SW = 390, SH = 230;
        b.fader (g, Pan, "Pan", [] (float v) { const int p = juce::roundToInt ((v - 0.5f) * 200); return p == 0 ? String ("centre") : (p < 0 ? "L " : "R ") + String (std::abs (p)); }, 0, 300, 170, 300, {}, false, true).bulbs = 9;
        b.fader (g, VelGain, "Velocity", pctF, 210, 300, 380, 300);
        auto& v = b.choice (g, Voices, { "1", "4", "8", "16" }, 0, 350);
        v.pre = "voices"; v.size = 17;
        g.draw = [dev, SW, SH, newestLevel, PX, PY] (juce::Graphics& gr, double, float, const Colour* c)
        {
            const float lv = newestLevel(), L = dev->get (Gain) * (0.2f + 0.8f * lv);
            const float lx = PX + 690 - 1150, ly = PY + 150 - 392;
            juce::Path beam;
            beam.startNewSubPath (lx, ly - 10); beam.lineTo (0, 0); beam.lineTo (0, SH); beam.lineTo (lx, ly + 10); beam.closeSubPath();
            gr.setGradientFill (juce::ColourGradient (rgba (c[1], 0.16f * L * look().glow), lx, ly, rgba (c[1], 0.04f * L * look().glow), 0, ly, false));
            gr.fillPath (beam);
            TubeOptions fo; fo.closed = true; fo.w = 0.7f; tube (gr, roundRect (0, 0, SW, SH, 6), c[0], fo);
            TubeOptions st; st.off = true; st.w = 0.6f;
            tube (gr, Lines { { { SW / 2, SH }, { SW / 2, SH + 26 } }, { { SW / 2 - 40, SH + 26 }, { SW / 2 + 40, SH + 26 } } }, c[0], st);
            if (auto* n = dev->live())
            {
                int slice = -1;
                for (auto& vv : n->views) if (vv.pitch.load() >= 0) slice = vv.slice.load();
                if (slice >= 0) text (gr, String (slice + 1), SW - 20, 50, 44, c[1], 0.25f, Align::right);
            }
            if (lv > 0.01f || dev->on (Scrub)) scope (gr, dev->live(), c[1], 14, 20, SW - 28, SH - 40);
            else { TubeOptions o; o.off = true; o.w = 0.5f; tube (gr, Line { { 14, SH / 2 }, { SW - 14, SH / 2 } }, c[1], o); }
            text (gr, "screen", SW / 2 + 14, SH + 14, 15, c[2], 0.45f);
        };
    }

    // Keys: frames from C2 when cut into frames, otherwise notes around the root
    keyboard (b, film, 60, 846, 1480,
              [dev] { return dev->choice (Mode) == Slice ? 36 : rootNote (dev->get (Root)) - 12; },
              [dev, slices] (int n)
              {
                  if (dev->choice (Mode) == Slice) { const int i = n - 36; return i >= 0 && i < (int) slices().size() ? String (i + 1) : String(); }
                  return n % 12 == 0 ? F::note (n) : String();
              });
    wall.wires = {};
    juce::ignoreUnused (w);
}
}
