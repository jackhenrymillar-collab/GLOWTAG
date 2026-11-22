#include "DeviceViewsCommon.h"
#include "../Theme.h"
#include <set>

// The workshop devices' walls, after the "GLOWTAG New Devices" designs: Gas Tube, Crate Digger,
// String Wall, Ghost Tag, Crowd and Transformer. Each is one neon installation with its own
// centrepiece; the neon is the control.
namespace DeviceViews
{
namespace
{
    const auto pctF = [] (float v) { return F::pct (v); };
    const auto dbF  = [] (float v) { return F::db (v); };
    float clampf (float v, float lo = 0.0f, float hi = 1.0f) { return juce::jlimit (lo, hi, v); }
    constexpr float pi = juce::MathConstants<float>::pi, twoPi = juce::MathConstants<float>::twoPi;

    const Colour OR (0xffff8a3a), PK (0xffff3fa4), YL (0xffffd23a), BL (0xff38a8ff), CY (0xff3ff0ff), VI (0xffb06bff), GR (0xff4ade80);
    const Colour metalGrey (0xff7a7d86), labelGrey (0xff77797f), bright (0xffe7e5df);

    // Bare metal: electrodes, clamps, the tone arm's pivot
    void metal (juce::Graphics& g, const Lines& lines, float w = 1.2f)
    {
        juce::Path p;
        for (auto& l : lines)
            for (size_t i = 0; i < l.size(); ++i)
                if (i == 0) p.startNewSubPath (l[i]); else p.lineTo (l[i]);
        g.setColour (metalGrey);
        g.strokePath (p, juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // A black cable between parts
    void cable (juce::Graphics& g, const Line& pts)
    {
        juce::Path p;
        for (size_t i = 0; i < pts.size(); ++i)
            if (i == 0) p.startNewSubPath (pts[i]); else p.lineTo (pts[i]);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.strokePath (p, juce::PathStrokeType (3.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (Colour (0xff2b2c31));
        g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    template <typename Fn> void faded (juce::Graphics& g, float alpha, Fn&& fn)
    {
        g.beginTransparencyLayer (clampf (alpha));
        fn();
        g.endTransparencyLayer();
    }

    Line wavy (float x0, float x1, float y, float amp, int cycles, int n)
    {
        Line l;
        for (int k = 0; k <= n; ++k) { const float u = (float) k / (float) n; l.push_back ({ x0 + (x1 - x0) * u, y + std::sin (u * twoPi * (float) cycles) * amp }); }
        return l;
    }
    Line jagged (float x0, float x1, float y, float amp, int n)
    {
        Line l;
        for (int k = 0; k <= n; ++k) l.push_back ({ x0 + (x1 - x0) * (float) k / (float) n, y + (k % 2 ? -amp : amp) * (k == 0 || k == n ? 0.0f : 1.0f) });
        return l;
    }

    // What the input did a moment ago: the walls sample the node's level each frame
    struct LevelHistory
    {
        std::vector<std::pair<double, float>> h;
        void add (double t, float v)
        {
            if (! h.empty() && t <= h.back().first) return;
            h.push_back ({ t, v });
            if (h.size() > 2400) h.erase (h.begin(), h.begin() + 400);
        }
        float at (double t) const
        {
            if (h.empty() || t <= h.front().first) return 0.0f;
            if (t >= h.back().first) return h.back().second;
            auto it = std::lower_bound (h.begin(), h.end(), t, [] (const std::pair<double, float>& a, double v) { return a.first < v; });
            return it == h.begin() ? it->second : (it - 1)->second;
        }
    };

    String sgn (int v, const String& unit) { return (v > 0 ? "+" : "") + String (v) + unit; }

    // A keyboard of neon tubes from `lo` to `hi`
    void keyRow (Builder& b, int mod, float x, float y, int lo, int hi)
    {
        auto* dev = &b.d;
        struct Key { int n; float x, w; bool black; };
        auto keys = std::make_shared<std::vector<Key>>();
        int wi = 0;
        for (int n = lo; n <= hi; ++n)
        {
            const int pc = n % 12;
            if (pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10) keys->push_back ({ n, (float) wi * 84 - 24, 48, true });
            else keys->push_back ({ n, (float) wi++ * 84, 84, false });
        }
        const float width = (float) wi * 84;
        auto& g = b.wall.add (mod, x, y, 0, width * 0.5f, 34, width * 0.45f);
        auto keyAt = [keys] (float lx, float ly) -> int
        {
            if (ly < 40) for (auto& k : *keys) if (k.black && lx >= k.x && lx < k.x + k.w) return k.n;
            for (auto& k : *keys) if (! k.black && lx >= k.x && lx < k.x + k.w) return k.n;
            return -1;
        };
        auto& a = g.dragArea ({ 0, 0, width, 66 }, Cursor::PointingHandCursor);
        a.down = [dev, keyAt] (float lx, float ly) -> std::optional<Gesture>
        {
            auto held = std::make_shared<int> (keyAt (lx, ly));
            if (*held >= 0) dev->note (*held, true);
            Gesture gs;
            gs.move = [dev, keyAt, held] (float px, float py) { const int q = keyAt (px, py); if (q >= 0 && q != *held) { if (*held >= 0) dev->note (*held, false); *held = q; dev->note (q, true); } };
            gs.up = [dev, held] { if (*held >= 0) dev->note (*held, false); };
            return gs;
        };
        g.draw = [dev, keys, width] (juce::Graphics& gr, double, float, const Colour* c)
        {
            std::set<int> down;
            if (auto* n = dev->live()) for (auto& v : n->views) if (v.pitch.load() >= 0 && v.released.load() < 0) down.insert (v.pitch.load());
            for (auto& k : *keys)
                if (down.count (k.n)) { gr.setColour (rgba (c[k.black ? 1 : 0], 0.3f)); gr.fillRect (k.x + 2, 2.0f, k.w - 4, (k.black ? 40.0f : 66.0f) - 4); }
            TubeOptions frame; frame.closed = true; frame.w = 0.9f; frame.clips = 210;
            tube (gr, roundRect (0, 0, width, 66, 7), c[0], frame);
            Lines whites, blacks;
            for (auto& k : *keys)
            {
                if (! k.black && k.x > 0)
                {
                    bool blackBefore = false;
                    for (auto& o : *keys) if (o.black && std::abs (o.x + 24 - k.x) < 1) blackBefore = true;
                    whites.push_back ({ { k.x, blackBefore ? 42.0f : 3.0f }, { k.x, 63.0f } });
                }
                if (k.black) blacks.push_back ({ { k.x + 3, 3 }, { k.x + 3, 40 }, { k.x + k.w - 3, 40 }, { k.x + k.w - 3, 3 } });
            }
            TubeOptions ow; ow.w = 0.55f; ow.style = 0; tube (gr, whites, c[0], ow);
            TubeOptions ob; ob.w = 0.65f; ob.style = 0; tube (gr, blacks, c[1], ob);
            for (auto& k : *keys) if (! k.black && k.n % 12 == 0) label (gr, F::note (k.n).toLowerCase(), k.x + 42, 60, Align::centre);
        };
    }

    void rainbow (Builder& b, Colour a, Colour c2, Colour c3)
    {
        b.out.pal[0] = a; b.out.pal[1] = c2; b.out.pal[2] = c3;
        b.out.parts = { a, a, c2, c3 };
    }
}

//==============================================================================
// Gas Tube

void gasWall (Builder& b)
{
    using namespace Param::GasTube;
    using Param::GasTube::Glow;   // not the Glow drawing helpers
    auto* dev = &b.d;
    auto& wall = b.wall;
    auto* w = &wall;
    auto* out = &b.out;
    static const Colour gasPal[numGases][3] = { { Colour (0xffff5a36), PK, YL }, { VI, BL, CY }, { CY, BL, VI }, { Colour (0xffc9b8ff), CY, PK } };
    static const char* gasSay[numGases] = { "warm red buzz, every harmonic", "hollow violet, odd harmonics", "clean blue glass", "bright white, breathy" };
    auto gasNow = [dev] { return juce::jlimit (0, numGases - 1, dev->choice (Gas)); };
    rainbow (b, gasPal[0][0], gasPal[0][1], gasPal[0][2]);
    auto& title = b.title ("Gas Tube", 470, [] { return String ("a synth made of neon tubes"); });
    // In Rainbow the gas is the colour: keep the palette in step with it
    {
        auto draw = title.draw;
        title.draw = [draw, out, gasNow] (juce::Graphics& gr, double t, float dt, const Colour* c)
        {
            const int gi = gasNow();
            out->pal[0] = gasPal[gi][0]; out->pal[1] = gasPal[gi][1]; out->pal[2] = gasPal[gi][2];
            out->parts = { out->pal[0], out->pal[0], out->pal[1], out->pal[2] };
            draw (gr, t, dt, c);
        };
    }
    enum { A = 1, B, Cc };
    const auto secF = [] (float lo, float hi) { return [lo, hi] (float v) { return F::time (expMap (v, lo, hi)); }; };

    // The tubes: one per voice, each lit while it plays. Click one to strike it.
    {
        auto& g = wall.add (A, 90, 196, -0.5f, 330, 260, 430);
        static const int notes[tubes] = { 48, 52, 55, 59, 62, 67 };
        auto TX = [] (int i) { return 40.0f + (float) i * 112.0f; };
        const float BOT = 470;
        auto top = [dev, BOT] { return BOT - (300 + dev->get (Length) * 170); };
        auto& lh = g.handle ([TX, top] { return std::optional<P> (P (TX (5) + 64, top() - 26)); });
        lh.ci = 2; lh.r = 7;
        lh.start = [dev] { return std::vector<float> { dev->get (Length) }; };
        lh.drag = [dev] (float, float dy, const std::vector<float>& s) { dev->set (Length, s[0] - dy / 170); };
        lh.tip = [dev] { return tipFor (*dev, Length, "Length", [] (float v) { return String (juce::roundToInt (40 + v * 160)) + " cm of glass"; }); };
        for (int i = 0; i < tubes; ++i)
        {
            auto& a = g.dragArea ({ TX (i) - 26, BOT - 470, 52, 470 }, Cursor::PointingHandCursor);
            a.tip = [i] { return "tube " + String (i + 1) + "   " + F::note (notes[i]).toLowerCase(); };
            a.down = [dev, i] (float, float) -> std::optional<Gesture>
            {
                dev->note (notes[i], true, 0.8f);
                Gesture gs;
                gs.up = [dev, i] { dev->note (notes[i], false); };
                return gs;
            };
        }
        auto rng = std::make_shared<juce::Random> (7);
        auto* gp = &g;
        g.draw = [=] (juce::Graphics& gr, double t, float, const Colour* c)
        {
            const auto* node = dev->live();
            const float tp = top(), volt = dev->get (Voltage);
            for (auto& it : gp->items) if (it->kind == Item::area) { it->box.setY (tp); it->box.setHeight (BOT - tp); }
            for (int i = 0; i < tubes; ++i) cable (gr, { { TX (i), tp - 12 }, { TX (i), tp - 26 } });
            TubeOptions rail; rail.w = 0.8f; rail.clips = 150;
            tube (gr, Line { { TX (0) - 30, tp - 26 }, { TX (5) + 64, tp - 26 } }, c[2], rail);
            text (gr, "length", TX (5) + 80, tp - 20, 16, c[2], 0.5f);
            for (int i = 0; i < tubes; ++i)
            {
                const float x = TX (i);
                const VoiceView* v = node != nullptr ? &node->views[(size_t) i] : nullptr;
                const int pitch = v != nullptr ? v->pitch.load() : -1;
                const float lv = pitch >= 0 ? clampf (v->level.load()) : 0.0f;
                const bool strike = pitch >= 0 && v->age.load() < 0.06f;
                TubeOptions glass; glass.closed = true; glass.off = true; glass.w = 0.9f;
                tube (gr, roundRect (x - 22, tp, 44, BOT - tp, 22), c[i % 2], glass);
                metal (gr, { roundRect (x - 11, tp - 12, 22, 14, 3), roundRect (x - 11, BOT - 2, 22, 14, 3) });
                metal (gr, { { { x - 6, tp + 8 }, { x - 2, tp + 16 }, { x + 2, tp + 8 }, { x + 6, tp + 16 } },
                             { { x - 6, BOT - 10 }, { x - 2, BOT - 18 }, { x + 2, BOT - 10 }, { x + 6, BOT - 18 } } }, 0.8f);
                if (lv > 0.02f)
                {
                    const auto col = strike ? C::white : c[i % 2];
                    faded (gr, 0.3f + 0.7f * lv, [&]
                    {
                        TubeOptions o; o.w = 1.2f + lv * 1.5f; o.style = look().style == 2 ? 0 : look().style;
                        tube (gr, Line { { x, tp + 26 }, { x, BOT - 26 } }, col, o);
                        if (volt > 0.35f)
                        {
                            Line zz;
                            for (int k = 0; k < 16; ++k) zz.push_back ({ x + (rng->nextFloat() - 0.5f) * 16 * volt, tp + 26 + (BOT - tp - 52) * (float) k / 15 });
                            TubeOptions zo; zo.w = 0.4f; zo.style = 0; tube (gr, zz, C::white, zo);
                        }
                    });
                    text (gr, F::note (pitch).toLowerCase(), x, BOT + 50, 18, c[i % 2], 0.85f, Align::centre);
                }
                else label (gr, F::note (notes[i]).toLowerCase(), x, BOT + 48, Align::centre);
            }
            juce::ignoreUnused (t);
        };
    }

    // Voltage: a lightning bolt into the transformer. Hum and crackle beside it.
    {
        auto& g = wall.add (B, 790, 196, 0.6f, 170, 250, 320);
        auto& volt = b.fader (g, Voltage, "Voltage", pctF, 0, 0, 0, 0);
        volt.pts = { { 30, 470 }, { 80, 370 }, { 44, 360 }, { 100, 250 }, { 60, 240 }, { 124, 110 } };
        volt.lx = 96; volt.ly = 476;
        auto& hum = b.fader (g, Hum, "Hum", pctF, 180, 400, 340, 400);
        hum.pts = wavy (180, 340, 400, 9, 4, 64);
        hum.lx = 180; hum.ly = 372;
        auto& mains = b.choice (g, Mains, { "50 hz", "60 hz" }, 180, 446);
        mains.size = 16; mains.ci = 1;
        auto& cr = b.fader (g, Crackle, "Crackle", pctF, 180, 520, 340, 520);
        cr.pts = jagged (180, 340, 520, 22, 6);
        cr.lx = 180; cr.ly = 496;
        auto rng = std::make_shared<juce::Random> (13);
        g.draw = [dev, rng] (juce::Graphics& gr, double, float, const Colour* c)
        {
            const float v = dev->get (Voltage), on = 0.3f + 0.7f * v;
            faded (gr, 0.55f + 0.45f * v, [&]
            {
                for (auto [x, k] : { std::pair<float, int> (196, 0), std::pair<float, int> (296, 1) })
                {
                    Lines coils;
                    for (int j = 0; j < 8; ++j)
                    {
                        const float y = 96 + (float) j * 24;
                        Line ring;
                        for (int q = 0; q <= 16; ++q) { const float a = (float) q / 16 * twoPi; ring.push_back ({ x + std::cos (a) * 30, y + std::sin (a) * 6 }); }
                        coils.push_back (ring);
                    }
                    TubeOptions o; o.w = 0.7f; o.style = look().style == 1 ? 1 : 0;
                    tube (gr, coils, c[k], o);
                }
            });
            TubeOptions core; core.off = true; core.w = 0.8f;
            tube (gr, Lines { { { 241, 84 }, { 241, 290 } }, { { 251, 84 }, { 251, 290 } } }, c[2], core);
            bulb (gr, 196, 62, c[0], on, 4.5f);
            bulb (gr, 296, 62, c[1], on, 4.5f);
            cable (gr, { { 196, 62 }, { 160, 70 }, { 124, 110 } });
            if (v > 0.5f && rng->nextFloat() < (v - 0.45f) * 1.4f)
            {
                TubeOptions arc; arc.w = 0.5f; arc.style = 0;
                tube (gr, jagged (200, 292, 62 + (rng->nextFloat() - 0.5f) * 10, 4 + rng->nextFloat() * 7, 9), C::white, arc);
            }
            text (gr, "transformer", 246, 330, 16, c[2], 0.45f, Align::centre);
        };
    }

    // The envelope: strike, glow and fade, with a lamp per tube riding it
    {
        auto& g = wall.add (Cc, 1180, 206, -0.8f, 180, 100, 260);
        auto pic = std::make_shared<EnvPicture>();
        auto env = [dev] { return AnalogEnv (expMap (dev->get (Strike), 0.001f, 2), expMap (dev->get (Body), 0.02f, 6), dev->get (Glow), expMap (dev->get (Fade), 0.01f, 6)); };
        auto& hs = g.handle ([pic] { return pic->h > 0 ? std::optional<P> (P (pic->xs[1], pic->Y (1))) : std::nullopt; });
        hs.ci = 0;
        hs.start = [dev] { return std::vector<float> { dev->get (Strike) }; };
        hs.drag = [dev] (float dx, float, const std::vector<float>& s) { dev->set (Strike, s[0] + dx / 170); };
        hs.tip = [dev, secF] { return tipFor (*dev, Strike, "Strike", secF (0.001f, 2)); };
        auto& hb = g.handle ([pic, dev] { return pic->h > 0 ? std::optional<P> (P (pic->xs[3], pic->Y (dev->get (Glow)))) : std::nullopt; });
        hb.ci = 1;
        hb.start = [dev] { return std::vector<float> { dev->get (Body), dev->get (Glow) }; };
        hb.drag = [dev] (float dx, float dy, const std::vector<float>& s) { dev->set (Body, s[0] + dx / 170); dev->set (Glow, s[1] - dy / 170); };
        hb.tip = [dev, secF] { return tipFor (*dev, Body, "Body", secF (0.02f, 6)) + "   " + tipFor (*dev, Glow, "Glow", pctF); };
        auto& hf = g.handle ([pic] { return pic->h > 0 ? std::optional<P> (P (pic->xs[5], pic->Y (0))) : std::nullopt; });
        hf.ci = 2;
        hf.start = [dev] { return std::vector<float> { dev->get (Fade) }; };
        hf.drag = [dev] (float dx, float, const std::vector<float>& s) { dev->set (Fade, s[0] + dx / 170); };
        hf.tip = [dev, secF] { return tipFor (*dev, Fade, "Fade", secF (0.01f, 6)); };
        g.draw = [dev, pic, env] (juce::Graphics& gr, double, float, const Colour* c)
        {
            const auto e = env();
            *pic = drawEnv (gr, e, 0, 30, 360, 170, c[0]);
            text (gr, "strike", 0, 10, 17, c[0], 0.55f);
            text (gr, "glow", pic->xs[3] + 12, pic->Y (dev->get (Glow)) - 14, 17, c[1], 0.55f);
            text (gr, "fade", pic->xs[5] + 10, 226, 17, c[2], 0.55f);
            if (auto* n = dev->live())
                for (auto& v : n->views)
                    if (v.pitch.load() >= 0)
                    {
                        const auto q = pic->at (e, v.age.load(), v.released.load());
                        bulb (gr, q.x, q.y, C::white, 1, 3.2f);
                    }
        };
    }

    // The gas, its pressure, warm-up and the gain
    {
        auto& g = wall.add (B, 1180, 486, 0.6f, 180, 110, 260);
        b.fader (g, Pressure, "Pressure", [] (float v) { return F::hz (expMap (v, 60, 16000)); }, 0, 30, 160, 30, P (80, 18));
        b.fader (g, WarmUp, "Warm-up", pctF, 200, 30, 360, 30, P (280, 42));
        b.fader (g, Gain, "Gain", dbF, 0, 104, 160, 104, P (80, 116));
        auto& gas = b.choice (g, Gas, { "Neon", "Argon", "Mercury", "Xenon" }, 0, 206);
        gas.size = 24; gas.gap = 14;
        gas.setChoice = [dev, w] (int v) { dev->setRaw (Gas, (float) v); w->say (String (gasSay[juce::jlimit (0, numGases - 1, v)])); };
        g.draw = [gasNow] (juce::Graphics& gr, double, float, const Colour* c)
        {
            text (gr, "gas", 0, 166, 17, c[1], 0.5f);
            text (gr, gasSay[gasNow()], 0, 246, 17, c[2], 0.6f);
        };
    }
    keyRow (b, 0, 170, 836, 48, 72);
}

//==============================================================================
// Crate Digger

void crateWall (Builder& b)
{
    using namespace Param::Crate;
    auto* dev = &b.d;
    auto& wall = b.wall;
    auto* w = &wall;
    rainbow (b, PK, YL, BL);
    b.title ("Crate Digger", 620, [dev]
    {
        auto s = dev->sample();
        if (s == nullptr) return String ("no record loaded");
        return String (s->fileName.isNotEmpty() ? s->fileName : String ("a record")).toLowerCase() + "   " + String (s->lengthSeconds(), 1) + " s";
    });
    enum { A = 1, B, Cc };
    static const char* keys = "awsedftgyhujkolp";
    auto bpm = [dev] { return (float) juce::jmax (20.0, (double) dev->ctx.project.bpm.load()); };
    auto chopSec = [dev, bpm]
    {
        auto s = dev->sample();
        const float len = s != nullptr ? (float) s->lengthSeconds() : 5.0f;
        return juce::jmin (len * 0.98f, chopBeats[chopIndex (dev->get (Chop))] * 60.0f / bpm());
    };
    auto sliceTip = [dev] (int i)
    {
        auto s = dev->sample();
        const float sec = (s != nullptr ? (float) s->lengthSeconds() : 0.0f) * dev->get (Pos0 + i);
        const int sh = shiftOf (dev->get (Shift0 + i));
        return "slice " + String::charToString ((juce::juce_wchar) keys[i]) + "   at " + String (sec, 2) + " s   "
             + (sh != 0 ? sgn (sh, " st") : String ("original pitch")) + (dev->on (Rev0 + i) ? "   reversed" : "") + (dev->on (Lock0 + i) ? "   locked" : "");
    };
    auto play = [dev] (int i) { dev->setRaw (Selected, (float) i); dev->note (firstNote + i, true, 0.85f); dev->note (firstNote + i, false); };

    // Dig: every unlocked slice gets a new place on the record (a hit, if the record has them),
    // a new pitch within the spread, and now and then plays backwards
    auto digT = std::make_shared<double> (-9.0);
    auto dig = [dev, digT] (bool all)
    {
        auto s = dev->sample();
        juce::Random r;
        const float lim = juce::jmax (0.15f, dev->get (Depth)) * 0.98f;
        std::vector<float> pool;
        if (s != nullptr && s->onsets.size() > 8) for (auto& o : s->onsets) if (o.first <= lim) pool.push_back (o.first);
        if (pool.empty()) for (int k = 0; k < 64; ++k) if ((float) k / 64 <= lim) pool.push_back ((float) k / 64);
        if (pool.empty()) pool.push_back (0.0f);
        static const int intervals[] = { 0, 0, 0, 12, -12, 7, -5, 5, -7, 3, -3, 2, -2, 10, -10 };
        std::vector<int> ints;
        const int reach = juce::roundToInt (dev->get (Spread) * 12);
        for (int v : intervals) if (std::abs (v) <= reach) ints.push_back (v);
        int dug = 0;
        for (int i = 0; i < slices; ++i)
        {
            if (! all && dev->on (Lock0 + i)) continue;
            dev->setRaw (Pos0 + i, pool[(size_t) r.nextInt ((int) pool.size())]);
            dev->setRaw (Shift0 + i, shiftValue (ints[(size_t) r.nextInt ((int) ints.size())]));
            dev->setRaw (Rev0 + i, r.nextFloat() < 0.12f ? 1.0f : 0.0f);
            if (all) dev->setRaw (Lock0 + i, 0.0f);
            ++dug;
        }
        *digT = now();
        return dug;
    };

    // Which voice plays now: the newest
    auto latest = [dev]() -> const VoiceView*
    {
        const VoiceView* best = nullptr;
        if (auto* n = dev->live())
            for (auto& v : n->views)
                if (v.slice.load() >= 0 && (best == nullptr || v.age.load() < best->age.load())) best = &v;
        return best;
    };

    // The turntable: the record ring is the waveform; slice lamps sit on it and drag round it
    {
        auto& g = wall.add (A, 70, 190, -1, 300, 300, 380);
        const float CX = 300, CY = 300, R = 268, NA = -pi * 0.28f;
        auto spin = std::make_shared<float> (NA);
        auto hold = std::make_shared<std::optional<float>>();
        for (int i = 0; i < slices; ++i)
        {
            auto& h = g.handle ([dev, spin, i, CX, CY] { const float a = *spin + dev->get (Pos0 + i) * twoPi; return std::optional<P> (P (CX + std::cos (a) * 246, CY + std::sin (a) * 246)); });
            h.ci = 2; h.r = 6;
            h.tip = [sliceTip, i] { return sliceTip (i); };
            h.down = [dev, spin, hold, i, play, sliceTip, w, CX, CY] (float, float) -> std::optional<Gesture>
            {
                const float r0 = *spin;
                *hold = r0;
                play (i);
                w->say (sliceTip (i));
                Gesture gs;
                gs.move = [dev, r0, i, sliceTip, w, CX, CY] (float x, float y)
                {
                    const float a = std::atan2 (y - CY, x - CX);
                    float u = std::fmod ((a - r0) / twoPi, 1.0f);
                    if (u < 0) u += 1.0f;
                    dev->setRaw (Pos0 + i, clampf (u, 0.0f, 0.995f));
                    w->say (sliceTip (i));
                };
                gs.up = [hold] { hold->reset(); };
                return gs;
            };
        }
        auto rng = std::make_shared<juce::Random> (21);
        g.draw = [=] (juce::Graphics& gr, double t, float, const Colour* c)
        {
            auto s = dev->sample();
            const auto* v = latest();
            const auto* node = dev->live();
            const float spinning = node != nullptr ? node->meter[0].load() : 0.0f;
            if (hold->has_value()) *spin = **hold;
            else if (v != nullptr)
            {
                // Turn the record so the playing spot sits under the needle
                const float target = NA - twoPi * v->pos.load();
                float d = std::fmod (target - *spin + pi, twoPi);
                if (d < 0) d += twoPi;
                d -= pi;
                *spin += d * (v->age.load() < 0.08f ? 0.45f : 1.0f);
            }
            TubeOptions rim; rim.clips = 200; rim.w = 1.1f;
            tube (gr, circle (CX, CY, R, 90), c[0], rim);
            TubeOptions off; off.off = true; off.w = 0.7f;
            tube (gr, circle (CX, CY, R - 16, 90), c[0], off);
            if (s != nullptr && ! s->peaks.empty())
            {
                Lines segs;
                const int N = 220;
                for (int k = 0; k < N; ++k)
                {
                    const float u = (float) k / N, a = *spin + u * twoPi;
                    const float pk = s->peaks[(size_t) juce::jlimit (0, (int) s->peaks.size() - 1, (int) (u * (float) s->peaks.size()))];
                    const float r0 = 136, r1 = r0 + 6 + clampf (pk) * 80;
                    segs.push_back ({ { CX + std::cos (a) * r0, CY + std::sin (a) * r0 }, { CX + std::cos (a) * r1, CY + std::sin (a) * r1 } });
                }
                faded (gr, 0.4f + 0.45f * (1 - dev->get (Wear)), [&] { TubeOptions o; o.w = 0.42f; o.style = 0; tube (gr, segs, c[1], o); });
            }
            TubeOptions lab; lab.w = 0.9f;
            tube (gr, circle (CX, CY, 92), c[1], lab);
            TubeOptions sp; sp.w = 0.7f; sp.style = 0;
            tube (gr, Line { { CX + std::cos (*spin) * 36, CY + std::sin (*spin) * 36 }, { CX + std::cos (*spin) * 82, CY + std::sin (*spin) * 82 } }, c[2], sp);
            bulb (gr, CX, CY, C::white, 0.8f, 5);
            const float len = s != nullptr ? juce::jmin (1.0f, chopSec() / (float) juce::jmax (0.01, s->lengthSeconds())) : 0.1f;
            const int sel = dev->choice (Selected);
            for (int i = 0; i < slices; ++i)
            {
                const float a0 = *spin + dev->get (Pos0 + i) * twoPi;
                const bool onNow = v != nullptr && v->slice.load() == i, rev = dev->on (Rev0 + i);
                Line arc;
                for (int k = 0; k <= 12; ++k) { const float a = a0 + (rev ? -1.0f : 1.0f) * len * twoPi * (float) k / 12; arc.push_back ({ CX + std::cos (a) * 246, CY + std::sin (a) * 246 }); }
                faded (gr, onNow ? 1.0f : i == sel ? 0.8f : 0.32f, [&] { TubeOptions o; o.w = onNow ? 1.15f : 0.7f; o.style = 0; tube (gr, arc, c[dev->on (Lock0 + i) ? 2 : 0], o); });
                label (gr, String::charToString ((juce::juce_wchar) keys[i]), CX + std::cos (a0) * 286, CY + std::sin (a0) * 286 + 5, Align::centre, i == sel ? bright : labelGrey);
            }
            if (spinning > 0.05f)
                for (int k = 0; k < juce::roundToInt (dev->get (Dust) * 16); ++k)
                {
                    const float a = rng->nextFloat() * twoPi, r = 100 + rng->nextFloat() * 150;
                    bulb (gr, CX + std::cos (a) * r, CY + std::sin (a) * r, C::white, 0.6f * rng->nextFloat() * spinning, 1.3f);
                }
            // The tone arm, wobbling with the wow
            const P nd (CX + std::cos (NA) * 246, CY + std::sin (NA) * 246), pv (CX + 300, CY - 236);
            const float wob = std::sin ((float) t * 3.4f) * dev->get (Wow) * 5;
            TubeOptions arm; arm.w = 1.0f;
            tube (gr, Line { pv, { pv.x - 8, pv.y + 150 }, { nd.x + 30 + wob, nd.y - 22 }, { nd.x + wob, nd.y } }, c[2], arm);
            TubeOptions piv; piv.w = 0.8f;
            tube (gr, circle (pv.x, pv.y, 20, 30), c[2], piv);
            metal (gr, { roundRect (pv.x - 9, pv.y - 48, 18, 26, 4) });
            bulb (gr, nd.x + wob, nd.y, C::white, v != nullptr ? 1.0f : 0.35f, 4.5f);
            text (gr, "the record", CX - 60, CY + R + 40, 16, c[1], 0.45f);
        };
    }

    // The crate: sixteen sleeves, a lock pin on each, and the dig word
    {
        auto& g = wall.add (B, 760, 430, 0.6f, 370, -40, 470);
        auto SX = [] (int i) { return 26.0f + (float) i * 44.0f; };
        auto top = [dev] (int i) { return -(150.0f + (float) ((i * 37) % 5) * 10.0f + (i == dev->choice (Selected) ? 40.0f : 0.0f)); };
        auto& a = g.dragArea ({ 0, -260, 740, 260 }, Cursor::PointingHandCursor);
        a.tip = [] { return String ("click a sleeve to play its slice, click its pin to lock it"); };
        a.down = [dev, SX, top, play, sliceTip, w] (float lx, float ly) -> std::optional<Gesture>
        {
            for (int i = 0; i < slices; ++i)
                if (std::abs (SX (i) + 18 - lx) < 22)
                {
                    if (std::abs (ly - (top (i) + 13)) < 15)
                    {
                        const bool lock = ! dev->on (Lock0 + i);
                        dev->setRaw (Lock0 + i, lock ? 1.0f : 0.0f);
                        w->say (String (lock ? "locked" : "unlocked") + "   slice " + String::charToString ((juce::juce_wchar) keys[i]));
                    }
                    else { play (i); w->say (sliceTip (i)); }
                    break;
                }
            return std::nullopt;
        };
        auto& digWord = g.toggle ("dig", 600, -268, [] { return true; }, [dig, w] (bool) { const int n = dig (false); w->say ("dug up " + String (n) + " new slices"); });
        digWord.size = 44; digWord.ci = 1;
        digWord.tip = [] { return String ("dig: new slices for every one not locked"); };
        auto sel = [dev] { return juce::jlimit (0, slices - 1, dev->choice (Selected)); };
        auto& pitch = g.fader (0, 236, 220, 236);
        pitch.text = "slice pitch"; pitch.bipolar = true; pitch.def = 0.5f; pitch.ci = 1;
        pitch.get = [dev, sel] { return dev->get (Shift0 + sel()); };
        pitch.set = [dev, sel] (float v) { dev->setRaw (Shift0 + sel(), shiftValue (juce::roundToInt ((clampf (v) - 0.5f) * 24))); };
        pitch.tip = [sliceTip, sel] { return sliceTip (sel()); };
        auto& rev = g.toggle ("reverse", 270, 244, [dev, sel] { return dev->on (Rev0 + sel()); }, [dev, sel, w, sliceTip] (bool v) { dev->setRaw (Rev0 + sel(), v ? 1.0f : 0.0f); w->say (sliceTip (sel())); });
        rev.ci = 1;
        auto& lock = g.toggle ("lock", 400, 244, [dev, sel] { return dev->on (Lock0 + sel()); }, [dev, sel, w, sliceTip] (bool v) { dev->setRaw (Lock0 + sel(), v ? 1.0f : 0.0f); w->say (sliceTip (sel())); });
        lock.ci = 2;
        auto& load = g.toggle ("load a record", 520, 244, [] { return true; }, [dev] (bool) { if (dev->loadAudio) dev->loadAudio(); });
        load.ci = 0; load.size = 17;
        g.draw = [=] (juce::Graphics& gr, double, float, const Colour* c)
        {
            const double tn = now();
            const float jig = clampf (1.0f - (float) (tn - *digT) / 0.5f);
            std::set<int> playing;
            if (auto* n = dev->live()) for (auto& v : n->views) if (v.slice.load() >= 0) playing.insert (v.slice.load());
            const int s = sel();
            for (int i = 0; i < slices; ++i)
            {
                const bool locked = dev->on (Lock0 + i);
                const float x = SX (i) + (! locked && jig > 0 ? std::sin ((float) tn * 46 + (float) i * 2) * 5 * jig : 0.0f), y = top (i);
                const bool onNow = playing.count (i) > 0, me = i == s;
                faded (gr, onNow ? 1.0f : me ? 0.9f : 0.55f, [&]
                {
                    TubeOptions o; o.w = onNow ? 1.1f : 0.8f; o.style = look().style == 1 ? 1 : 0;
                    tube (gr, Line { { x, 8 }, { x, y + 7 }, { x + 7, y }, { x + 29, y }, { x + 36, y + 7 }, { x + 36, 8 } }, onNow ? C::white : c[me ? 1 : 0], o);
                });
                bulb (gr, x + 18, y + 13, c[2], locked ? 1.0f : 0.0f, 4.5f);
                label (gr, String::charToString ((juce::juce_wchar) keys[i]), x + 18, y + 40, Align::centre, me ? bright : Colour (0xff8d8f96));
                const int sh = shiftOf (dev->get (Shift0 + i));
                if (sh != 0 || dev->on (Rev0 + i)) label (gr, (sh != 0 ? sgn (sh, "") : String()) + (dev->on (Rev0 + i) ? " r" : ""), x + 18, y + 60, Align::centre, Colour (0xff6e7078));
            }
            TubeOptions box; box.closed = true; box.clips = 200;
            tube (gr, roundRect (0, 0, 740, 150, 10), c[0], box);
            TubeOptions off; off.off = true; off.w = 0.7f;
            tube (gr, Lines { { { 14, 50 }, { 726, 50 } }, { { 14, 100 }, { 726, 100 } } }, c[0], off);
            TubeOptions grip; grip.closed = true; grip.w = 0.8f;
            tube (gr, roundRect (300, 18, 140, 24, 12), c[1], grip);
            text (gr, "the crate", 20, 132, 16, c[0], 0.45f);
            text (gr, "pin", SX (15) + 52, top (15) + 18, 15, c[2], 0.45f);
            text (gr, "slice " + String::charToString ((juce::juce_wchar) keys[s]), 0, 196, 22, c[1], 0.9f);
        };
    }

    // The turntable's wear and the dig's reach
    {
        auto& g = wall.add (Cc, 760, 752, 0.4f, 400, 60, 440);
        b.fader (g, Wear, "Wear", pctF, 0, 30, 150, 30, P (75, 18));
        b.fader (g, Dust, "Dust", pctF, 190, 30, 340, 30, P (265, 42));
        b.fader (g, Wow, "Wow", pctF, 380, 30, 530, 30, P (455, 18));
        b.fader (g, Spread, "Spread", [] (float v) { return "±" + String (juce::roundToInt (v * 12)) + " st"; }, 570, 30, 720, 30, P (645, 42));
        static const char* chops[] = { "1/16", "1/8", "1/4", "1/2", "1 bar" };
        b.fader (g, Chop, "Chop", [] (float v) { return String (chops[chopIndex (v)]); }, 0, 104, 150, 104).bulbs = 5;
        b.fader (g, Depth, "Depth", pctF, 190, 104, 340, 104, P (265, 92));
        b.fader (g, Pitch, "Pitch", [] (float v) { return sgn (juce::roundToInt ((v - 0.5f) * 24), " st"); }, 380, 104, 530, 104, P (455, 116), false, true);
        b.fader (g, Gain, "Gain", dbF, 570, 104, 720, 104, P (645, 92));
        auto& era = b.choice (g, Era, { "60s", "70s", "80s", "90s" }, 0, 160);
        era.pre = "pressed in the"; era.ci = 1; era.size = 18;
    }
}

//==============================================================================
// String Wall

void stringsWall (Builder& b)
{
    using namespace Param::Strings;
    auto* dev = &b.d;
    auto& wall = b.wall;
    auto* w = &wall;
    rainbow (b, CY, VI, YL);
    b.title ("String Wall", 600, [] { return String ("strings stretched across the brick"); });
    enum { A = 1, B, Cc };
    auto tune = [dev] { return juce::jlimit (0, numTunings - 1, dev->choice (Tuning)); };
    auto openNote = [tune] (int s) { return tunings[tune()][s]; };

    // The strings, nut to bridge, with the slide bar and the pick point
    {
        auto& g = wall.add (A, 110, 226, -0.35f, 590, 160, 700);
        const float LEN = 1180;
        auto SY = [] (int s) { return (float) (7 - s) * 44.0f; };
        auto fx = [LEN] (float k) { return LEN * (1.0f - std::pow (2.0f, -k / 12.0f)); };
        auto bx = [dev, fx] { return dev->on (BarOn) ? fx (dev->get (Bar) * 12) : -70.0f; };
        auto pkx = [dev, LEN] { return LEN - 40 - dev->get (Pick) * 460; };
        auto& bar = g.handle ([bx] { return std::optional<P> (P (bx(), -44)); });
        bar.ci = 2; bar.r = 7;
        bar.tip = [dev] { return dev->on (BarOn) ? "slide bar   " + String (dev->get (Bar) * 12, 1) + " frets up" : String ("slide bar   lifted, drag it onto the strings"); };
        bar.down = [dev, w, LEN] (float, float) -> std::optional<Gesture>
        {
            Gesture gs;
            gs.move = [dev, w, LEN] (float x, float)
            {
                if (x < -20) { if (dev->on (BarOn)) { dev->setRaw (BarOn, 0.0f); w->say ("slide bar lifted"); } return; }
                dev->setRaw (BarOn, 1.0f);
                const float k = 12.0f * std::log2 (LEN / juce::jmax (1.0f, LEN - clampf (x, 0, LEN * 0.75f)));
                dev->set (Bar, k / 12.0f);
                w->say ("slide bar   " + String (dev->get (Bar) * 12, 1) + " frets");
            };
            return gs;
        };
        auto& pick = g.handle ([pkx] { return std::optional<P> (P (pkx(), 340)); });
        pick.ci = 1; pick.r = 5.5f;
        pick.start = [dev] { return std::vector<float> { dev->get (Pick) }; };
        pick.drag = [dev] (float dx, float, const std::vector<float>& s) { dev->set (Pick, s[0] - dx / 460); };
        pick.tip = [dev] { return tipFor (*dev, Pick, "Pick point", pctF); };

        auto stringAt = [SY] (float ly) { for (int s = 0; s < count; ++s) if (std::abs (SY (s) - ly) < 13) return s; return -1; };
        auto& strum = g.dragArea ({ 0, -16, LEN, 340 }, Cursor::CrosshairCursor);
        strum.tip = [dev] { return dev->choice (Mode) == Pluck ? String ("drag across the strings to strum") : String ("hold on a string to keep it sounding"); };
        strum.down = [dev, stringAt, SY, openNote] (float, float ly) -> std::optional<Gesture>
        {
            const bool sustained = dev->choice (Mode) != Pluck;
            auto held = std::make_shared<std::set<int>>();
            auto last = std::make_shared<std::pair<float, double>> (ly, now());
            auto hit = [dev, held, sustained, openNote] (int s, float vel)
            {
                if (sustained && held->count (s)) return;
                dev->note (openNote (s), true, vel);
                if (sustained) held->insert (s);
            };
            if (const int s0 = stringAt (ly); s0 >= 0) hit (s0, 0.8f);
            Gesture gs;
            gs.move = [hit, last, SY] (float, float y)
            {
                const double t2 = now();
                const float speed = std::abs (y - last->first) / (float) juce::jmax (1.0, (t2 - last->second) * 1000.0);
                for (int s = 0; s < count; ++s) { const float yy = SY (s); if ((last->first - yy) * (y - yy) < 0) hit (s, clampf (0.35f + speed * 0.4f, 0.3f, 1.0f)); }
                *last = { y, t2 };
            };
            gs.up = [dev, held, openNote] { for (int s : *held) dev->note (openNote (s), false); };
            return gs;
        };

        g.draw = [=] (juce::Graphics& gr, double t, float, const Colour* c)
        {
            const float B = bx();
            const bool barOn = dev->on (BarOn);
            std::array<float, count> amps {};
            std::array<int, count> notes {};
            const auto* node = dev->live();
            for (int s = 0; s < count; ++s)
            {
                amps[(size_t) s] = node != nullptr ? clampf (node->views[(size_t) s].level.load()) : 0.0f;
                const int p = node != nullptr ? node->views[(size_t) s].pitch.load() : -1;
                notes[(size_t) s] = p >= 0 ? p : openNote (s) + juce::roundToInt ((barOn ? dev->get (Bar) * 12 : 0.0f));
            }
            Lines frets;
            for (int k = 1; k <= 15; ++k) frets.push_back ({ { fx ((float) k), 326 }, { fx ((float) k), 334 } });
            TubeOptions off; off.off = true; off.w = 0.6f;
            tube (gr, frets, c[0], off);
            for (int k : { 3, 5, 7, 9, 15 }) bulb (gr, (fx ((float) k) + fx ((float) k - 1)) / 2, 352, c[0], 0.25f, 2.6f);
            bulb (gr, (fx (12) + fx (11)) / 2 - 6, 352, c[0], 0.25f, 2.6f);
            bulb (gr, (fx (12) + fx (11)) / 2 + 6, 352, c[0], 0.25f, 2.6f);
            TubeOptions posts; posts.w = 1.1f; posts.clips = 120;
            tube (gr, Lines { { { 0, -26 }, { 0, 334 } }, { { LEN, -26 }, { LEN, 334 } } }, c[0], posts);
            for (int s = 0; s < count; ++s)
            {
                const float y = SY (s), x0 = barOn ? B : 0.0f, a = amps[(size_t) s];
                const auto col = c[s % 2 ? 1 : 0];
                const float ped = pedalSemis (s, node != nullptr ? node->meter[0].load() : 0.0f, node != nullptr ? node->meter[1].load() : 0.0f);
                if (barOn) tube (gr, Line { { 0, y }, { B, y } }, col, off);
                Line pts;
                for (int k = 0; k <= 40; ++k)
                {
                    const float u = (float) k / 40;
                    pts.push_back ({ x0 + (LEN - x0) * u, y + a * 15 * std::sin (pi * u) * std::sin ((float) t * (38.0f + (float) s * 9.0f)) });
                }
                faded (gr, 0.5f + 0.5f * clampf (a * 1.5f), [&] { TubeOptions o; o.w = 0.5f + (float) (8 - s) * 0.07f + a * 0.3f; tube (gr, pts, col, o); });
                bulb (gr, 0, y, c[0], 0.55f, 3);
                bulb (gr, LEN, y, ped > 0.05f ? c[2] : c[0], ped > 0.05f ? 1.0f : 0.55f, 3.5f);
                label (gr, F::note (notes[(size_t) s]).toLowerCase(), LEN + 20, y + 5, Align::left, a > 0.05f ? bright : labelGrey);
            }
            faded (gr, barOn ? 1.0f : 0.7f, [&] { TubeOptions o; o.w = 1.7f; o.off = ! barOn; tube (gr, Line { { B, -30 }, { B, 338 } }, c[2], o); });
            text (gr, "slide bar", B + 14, -40, 15, c[2], 0.5f);
            TubeOptions pk; pk.w = 0.4f; pk.dashOn = 3; pk.dashOff = 8; pk.style = 0;
            tube (gr, Line { { pkx(), -14 }, { pkx(), 322 } }, c[1], pk);
            text (gr, "pick", pkx() + 12, 346, 15, c[1], 0.5f);
            text (gr, "nut", -14, -40, 15, c[0], 0.4f);
            text (gr, "bridge", LEN - 60, -40, 15, c[0], 0.4f);
        };
    }

    // The pedals, the bar and the way the strings are played
    {
        auto& g = wall.add (B, 110, 628, 0.5f, 300, 80, 380);
        for (int i = 0; i < 2; ++i)
        {
            const int param = i == 0 ? PedalA : PedalB;
            auto& a = g.dragArea ({ (float) i * 170, 0, 140, 96 }, Cursor::PointingHandCursor);
            a.tip = [i, openNote]
            {
                const auto& ped = i == 0 ? pedalA : pedalB;
                return "pedal " + String (i == 0 ? "a" : "b") + "   raises " + F::note (openNote (ped[0][0])).toLowerCase() + " by " + String (ped[0][1])
                     + " and " + F::note (openNote (ped[1][0])).toLowerCase() + " by " + String (ped[1][1]);
            };
            a.down = [dev, param] (float, float) -> std::optional<Gesture>
            {
                dev->setRaw (param, 1.0f);
                Gesture gs;
                gs.up = [dev, param] { dev->setRaw (param, 0.0f); };
                return gs;
            };
        }
        b.toggle (g, BarOn, "slide bar", 350, 56).ci = 2;
        auto& mode = b.choice (g, Mode, { "Pluck", "Bow", "Feedback" }, 0, 166);
        mode.size = 28; mode.gap = 26;
        g.draw = [dev] (juce::Graphics& gr, double, float, const Colour* c)
        {
            const auto* node = dev->live();
            for (int i = 0; i < 2; ++i)
            {
                const float a = node != nullptr ? clampf (node->meter[(size_t) i].load()) : (dev->on (i == 0 ? PedalA : PedalB) ? 1.0f : 0.0f);
                const float x = (float) i * 170, dn = a * 8;
                TubeOptions o; o.closed = true; o.off = a <= 0.05f; o.w = 0.8f + a * 0.4f;
                tube (gr, roundRect (x, dn, 140, 88, 14), c[i + 1], o);
                bulb (gr, x + 70, 30 + dn, c[i + 1], a, 8);
                text (gr, i == 0 ? "pedal a" : "pedal b", x + 70, 72 + dn, 17, c[i + 1], 0.4f + 0.6f * a, Align::centre);
            }
        };
    }

    // Material and tuning, and a strum of everything
    {
        auto& g = wall.add (A, 640, 640, -0.3f, 300, 50, 360);
        auto& mat = b.choice (g, Material, { "Nylon", "Steel", "Wire", "Gut" }, 0, 24);
        mat.pre = "strings"; mat.ci = 1;
        auto& tn = b.choice (g, Tuning, { "Open C", "Steel E9", "Harp", "Drone D" }, 0, 84);
        tn.pre = "tuning"; tn.ci = 2;
        tn.setChoice = [dev, w] (int v)
        {
            dev->setRaw (Tuning, (float) v);
            static const char* names[] = { "open c", "steel e9", "harp", "drone d" };
            String s (names[juce::jlimit (0, numTunings - 1, v)]);
            for (int k = 0; k < count; ++k) s << (k ? " " : "   ") << F::note (tunings[juce::jlimit (0, numTunings - 1, v)][k]).toLowerCase();
            w->say (s);
        };
        auto* ctx = &dev->ctx;
        auto& all = g.toggle ("strum all", 0, 144, [] { return true; }, [ctx, openNote] (bool)
        {
            // Each string a moment after the last, low to high (the app context outlives the wall)
            for (int s = 0; s < count; ++s)
            {
                const int note = openNote (s);
                juce::Timer::callAfterDelay (s * 28, [ctx, note]
                {
                    ctx->engine.midiInput.addMessageToQueue (juce::MidiMessage::noteOn (1, note, 0.6f).withTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001));
                });
            }
        });
        all.ci = 0;
    }

    // The tone controls
    {
        auto& g = wall.add (Cc, 1336, 206, 0.8f, 130, 210, 240);
        const int params[6] = { Tone, Sustain, Body, Sympathy, BowPressure, Gain };
        const char* names[6] = { "Tone", "Sustain", "Body", "Sympathy", "Bow pressure", "Gain" };
        for (int i = 0; i < 6; ++i)
            b.fader (g, params[i], names[i], params[i] == Gain ? std::function<String (float)> (dbF) : std::function<String (float)> (pctF),
                     0, 40 + (float) i * 72, 220, 40 + (float) i * 72, P (110, 40 + (float) i * 72 + (i % 2 ? 12.0f : -12.0f)));
    }
    keyRow (b, 0, 170, 846, 36, 60);
}

//==============================================================================
// Ghost Tag

void ghostWall (Builder& b)
{
    using namespace Param::Ghost;
    auto* dev = &b.d;
    auto& wall = b.wall;
    auto* w = &wall;
    rainbow (b, VI, CY, PK);
    b.title ("Ghost Tag", 500, [] { return String ("echoes written on the wall that fade"); });
    enum { A = 1, B, Cc };
    static const char* divs[] = { "1/16", "1/8t", "1/8", "1/8.", "1/4", "1/4.", "1/2" };
    auto echoSec = [dev]
    {
        const float bpm = (float) juce::jmax (20.0, (double) dev->ctx.project.bpm.load());
        return Param::divisionBeats[Param::divisionIndex (dev->get (Time))] * 60.0f / bpm;
    };
    auto holdTip = [dev] { return "hold echoes " + String (dev->choice (HoldFrom)) + " to " + String (dev->choice (HoldTo)); };
    auto setHold = [dev, w] (bool v)
    {
        dev->setRaw (Hold, v ? 1.0f : 0.0f);
        w->say (v ? "holding echoes " + String (dev->choice (HoldFrom)) + " to " + String (dev->choice (HoldTo)) + "   " + (dev->choice (HoldHow) == Reverse ? "reverse" : "freeze")
                  : String ("released"));
    };

    // The tag and its ghosts: each echo a smaller, paler copy further along the wall
    {
        auto& g = wall.add (A, 50, 176, 0, 720, 280, 800);
        auto POS = [] (int k) { return P (20 + (float) k * 192, 330 - (float) k * 22); };
        auto SC = [] (int k) { return std::pow (0.92f, (float) k); };
        const float WIN = 1.7f, RY = 520;
        auto hist = std::make_shared<LevelHistory>();
        auto snap = std::make_shared<std::map<int, Line>>();
        auto scribble = [hist, WIN] (double tEnd, float sc, float amp)
        {
            Line l;
            for (int j = 0; j <= 140; ++j)
            {
                const float u = (float) j / 140;
                const double ts = tEnd - WIN * (1 - u);
                const float lv = clampf (hist->at (ts) * 2.4f), ph = (float) (ts * 19.0);
                l.push_back ({ u * 240 * sc + std::sin (ph) * lv * 34 * sc, -lv * 190 * sc * amp + std::cos (ph) * lv * 34 * sc * amp - std::sin (u * pi) * 8 * sc });
            }
            return l;
        };
        auto& ha = g.handle ([dev, POS] { return std::optional<P> (P (POS (dev->choice (HoldFrom)).x + 30, 520)); });
        ha.ci = 2; ha.r = 7; ha.tip = holdTip;
        ha.down = [dev, w, holdTip] (float, float) -> std::optional<Gesture>
        {
            Gesture gs;
            gs.move = [dev, w, holdTip] (float x, float) { dev->setRaw (HoldFrom, (float) juce::jlimit (1, dev->choice (HoldTo), juce::roundToInt ((x - 50) / 192))); w->say (holdTip()); };
            return gs;
        };
        auto& hb = g.handle ([dev, POS, SC] { const int k = dev->choice (HoldTo); return std::optional<P> (P (POS (k).x + 220 * SC (k), 520)); });
        hb.ci = 2; hb.r = 7; hb.tip = holdTip;
        hb.down = [dev, w, holdTip] (float, float) -> std::optional<Gesture>
        {
            Gesture gs;
            gs.move = [dev, w, holdTip] (float x, float) { dev->setRaw (HoldTo, (float) juce::jlimit (dev->choice (HoldFrom), echoes, juce::roundToInt ((x - 220) / 192))); w->say (holdTip()); };
            return gs;
        };
        g.draw = [=] (juce::Graphics& gr, double t, float, const Colour* c)
        {
            if (auto* n = dev->live()) hist->add (t, n->inputLevel.load());
            const float T = echoSec(), fb = dev->get (Fade), wash = dev->get (Wash), drift = dev->get (Drift), blur = dev->get (Blur);
            const bool hold = dev->on (Hold), rev = dev->choice (HoldHow) == Reverse;
            const int from = juce::jlimit (1, echoes, dev->choice (HoldFrom)), to = juce::jlimit (from, echoes, dev->choice (HoldTo));
            if (hold && snap->empty()) for (int k = from; k <= to; ++k) (*snap)[k] = scribble (t - k * T, SC (k), std::pow (fb, (float) k));
            if (! hold) snap->clear();
            TubeOptions rail; rail.w = 0.8f; rail.clips = 200;
            tube (gr, Line { { 0, RY }, { POS (7).x + 150, RY } }, c[2], rail);
            for (int k = 1; k <= echoes; ++k) bulb (gr, POS (k).x + 100 * SC (k), RY, c[2], k >= from && k <= to ? 0.6f : 0.12f, 3);
            const float xa = POS (from).x - 24, xb = POS (to).x + 250 * SC (to), ya = POS (to).y - 190 * SC (to), yb = POS (from).y + 64;
            TubeOptions box; box.closed = true;
            if (hold) box.w = 0.95f; else { box.w = 0.55f; box.dashOn = 6; box.dashOff = 9; box.style = 0; }
            tube (gr, roundRect (xa, ya, xb - xa, yb - ya, 18), c[2], box);
            text (gr, hold ? (rev ? "held   backwards" : "held   frozen") : "hold", xa + 8, ya - 12, 17, c[2], hold ? 0.95f : 0.45f);
            for (int k = echoes; k >= 0; --k)
            {
                const float a = k ? std::pow (fb, (float) k) : 1.0f;
                const bool held = hold && k >= from && k <= to;
                const auto p0 = POS (k);
                const float sc = SC (k);
                const auto colBase = c[k % 2 ? 1 : 0];
                if (k && a < 0.035f && ! held)
                {
                    TubeOptions o; o.off = true; o.w = 0.6f;
                    tube (gr, Line { p0, { p0.x + 220 * sc, p0.y } }, colBase, o);
                    continue;
                }
                const Line pts = held && snap->count (k) ? (*snap)[k] : scribble (t - k * T, sc, k ? a : 1.0f);
                const float dy = std::sin ((float) t * 0.7f + (float) k * 1.3f) * drift * (float) k * 5;
                const float rot = juce::degreesToRadians (-3.0f + (float) k * 1.4f * drift);
                const auto col = mix (colBase, Colour (0xff8c8e96), clampf (wash * (float) k / 5));
                const float al = clampf (held ? 1.0f : 0.22f + 0.78f * a);
                {
                    juce::Graphics::ScopedSaveState ss (gr);
                    auto tr = juce::AffineTransform::rotation (rot).translated (p0.x, p0.y + dy);
                    if (held && rev) tr = juce::AffineTransform::scale (-1.0f, 1.0f).translated (240 * sc, 0).followedBy (tr);
                    gr.addTransform (tr);
                    faded (gr, al, [&]
                    {
                        if (blur > 0.03f && k)
                        {
                            // The blur: a softer copy drifting off the line
                            juce::Graphics::ScopedSaveState s2 (gr);
                            gr.addTransform (juce::AffineTransform::translation (blur * (float) k * 2.6f, blur * (float) k * 1.6f));
                            faded (gr, 0.4f, [&] { TubeOptions o; o.w = 0.8f * sc; o.style = 0; tube (gr, pts, col, o); });
                        }
                        TubeOptions o; o.w = (1.15f - (float) k * 0.06f) * sc; o.clips = k ? 0.0f : 170.0f;
                        tube (gr, pts, col, o);
                    });
                }
                text (gr, k ? String (k) : String ("dry"), p0.x, p0.y + 52, 16, colBase, k ? 0.4f * al + 0.1f : 0.55f);
            }
        };
    }

    // Time and the hold
    {
        auto& g = wall.add (B, 90, 770, 0.4f, 330, 40, 380);
        auto& dv = g.words ({}, 0, 0);
        for (int i = 0; i < 7; ++i) dv.opts.push_back ({ i, divs[i] });
        dv.size = 21; dv.gap = 20;
        dv.getChoice = [dev] { return Param::divisionIndex (dev->get (Time)); };
        dv.setChoice = [dev, w] (int i) { dev->set (Time, ((float) i + 0.5f) / 7); w->say ("time   " + String (divs[juce::jlimit (0, 6, i)])); };
        auto& hold = g.toggle ("hold", 0, 72, [dev] { return dev->on (Hold); }, setHold);
        hold.size = 26; hold.ci = 2;
        auto& how = b.choice (g, HoldHow, { "Freeze", "Reverse" }, 100, 72);
        how.ci = 1;
        g.draw = [] (juce::Graphics& gr, double, float, const Colour* c)
        {
            text (gr, "time", -2, -30, 16, c[0], 0.45f);
            text (gr, "drag the lamps on the rail to choose which echoes to hold", 310, 72, 15, c[2], 0.4f);
        };
    }

    {
        auto& g = wall.add (Cc, 850, 776, -0.4f, 330, 40, 380);
        b.fader (g, Fade, "Fade", pctF, 0, 0, 150, 0, P (75, -12));
        b.fader (g, Wash, "Wash", pctF, 190, 0, 340, 0, P (265, 12));
        b.fader (g, Drift, "Drift", pctF, 380, 0, 530, 0, P (455, -12));
        b.fader (g, Blur, "Blur", pctF, 0, 76, 150, 76, P (75, 88));
        b.fader (g, Mix, "Mix", pctF, 190, 76, 340, 76, P (265, 64));
    }
}

//==============================================================================
// Crowd

void crowdWall (Builder& b)
{
    using namespace Param::Crowd;
    auto* dev = &b.d;
    auto& wall = b.wall;
    auto* w = &wall;
    rainbow (b, YL, PK, CY);
    b.title ("Crowd", 350, [] { return String ("one sound becomes a crowd"); });
    enum { A = 1, B, Cc };
    struct Geo { float sx, sy, x, y, s; };
    auto geo = [dev] (int i)
    {
        const auto pl = place (dev->get (HomeX0 + i), dev->get (HomeY0 + i), dev->get (Spread), dev->get (Depth));
        const float hw = 540 - pl.sy * 190;
        return Geo { pl.sx, pl.sy, pl.sx * hw, -28 - pl.sy * 290, pl.scale };
    };
    auto delayOf = [dev, geo] (int i) { return lateness (dev->get (Timing), dev->get (Quirk0 + i), geo (i).sy); };
    auto headcountNow = [dev] { return headcount (dev->get (Size)); };
    auto personTip = [geo, delayOf] (int i)
    {
        const auto q = geo (i);
        const String side = q.sx < -0.1f ? "left " + String (juce::roundToInt (-q.sx * 100)) : q.sx > 0.1f ? "right " + String (juce::roundToInt (q.sx * 100)) : String ("centre");
        return "person " + String (i + 1) + "   " + side + "   " + String (juce::roundToInt (delayOf (i) * 1000)) + " ms away";
    };

    // The stage: drag a person to move them; the lamp above spreads them out
    {
        auto& g = wall.add (A, 800, 640, 0, 0, -190, 640);
        for (int i = 0; i < people; ++i)
        {
            auto& h = g.handle ([geo, i] { const auto q = geo (i); return std::optional<P> (P (q.x, q.y - 58 * q.s)); });
            h.ci = i % 3; h.r = 5.5f;
            h.visible = [headcountNow, i] { return i < headcountNow(); };
            h.tip = [personTip, i] { return personTip (i); };
            h.down = [dev, w, personTip, i] (float, float) -> std::optional<Gesture>
            {
                Gesture gs;
                gs.move = [dev, w, personTip, i] (float x, float y)
                {
                    const float sy = clampf ((-28 - (y + 50)) / 290), hw = 540 - sy * 190, sx = clampf (x / hw, -1, 1);
                    const float spread = juce::jmax (0.05f, dev->get (Spread));
                    dev->setRaw (HomeX0 + i, clampf (sx / spread, -1, 1) * 0.5f + 0.5f);
                    dev->setRaw (HomeY0 + i, clampf ((sy - dev->get (Depth) * 0.5f) / (spread * 0.5f)));
                    w->say (personTip (i));
                };
                return gs;
            };
        }
        auto& sp = g.handle ([dev] { return std::optional<P> (P (40 + dev->get (Spread) * 560, -200)); });
        sp.ci = 1; sp.r = 7;
        sp.start = [dev] { return std::vector<float> { dev->get (Spread) }; };
        sp.drag = [dev] (float dx, float, const std::vector<float>& s) { dev->set (Spread, s[0] + dx / 560); };
        sp.tip = [dev] { return tipFor (*dev, Spread, "Spread", pctF); };
        g.draw = [=] (juce::Graphics& gr, double t, float, const Colour* c)
        {
            const auto* node = dev->live();
            const int n = headcountNow();
            const float l = node != nullptr ? clampf (node->inputLevel.load() * 4) : 0.0f;
            const float sway = expMap (dev->get (Sway), 0.1f, 4.0f), tune = dev->get (Tuning);
            Line arch { { -640, 24 }, { -640, -330 } };
            for (int k = 0; k <= 24; ++k) { const float a = pi + (float) k / 24 * pi; arch.push_back ({ std::cos (a) * 640, -330 + std::sin (a) * 60 }); }
            arch.push_back ({ 640, -330 }); arch.push_back ({ 640, 24 });
            TubeOptions ao; ao.clips = 220;
            tube (gr, arch, c[1], ao);
            TubeOptions edge; edge.w = 1.1f; edge.clips = 220;
            tube (gr, Line { { -560, 0 }, { 560, 0 } }, c[0], edge);
            TubeOptions off; off.off = true; off.w = 0.7f;
            tube (gr, Lines { { { -560, 0 }, { -370, -320 } }, { { 560, 0 }, { 370, -320 } }, { { -370, -320 }, { 370, -320 } } }, c[0], off);
            for (int k = 0; k < 17; ++k) bulb (gr, -560 + (float) k * 70, 18, c[2], 0.3f + 0.7f * l, 4.2f);
            TubeOptions dash; dash.w = 0.5f; dash.dashOn = 3; dash.dashOff = 8; dash.style = 0;
            tube (gr, Line { { 40, -200 }, { 600, -200 } }, c[1], dash);
            text (gr, "spread", 604, -190, 15, c[1], 0.5f);
            std::vector<int> order;
            for (int i = 0; i < n; ++i) order.push_back (i);
            std::sort (order.begin(), order.end(), [&geo] (int a, int bb) { return geo (bb).sy < geo (a).sy; });
            for (int i : order)
            {
                const auto q = geo (i);
                const float quirk = dev->get (Quirk0 + i);
                const float lv = node != nullptr ? clampf (node->views[(size_t) i].level.load() * 1.6f) : 0.0f;
                const float sw = std::sin ((float) t * twoPi * sway * (0.6f + quirk * 0.8f) + (float) i) * (2 + tune * 10) * q.s;
                const float x = q.x + sw, y = q.y, s = q.s;
                const auto col = c[i % 3];
                Line shoulders;
                for (int k = 0; k <= 16; ++k) { const float a = pi + (float) k / 16 * pi; shoulders.push_back ({ x + std::cos (a) * 33 * s, y + std::sin (a) * 28 * s }); }
                faded (gr, 0.4f + 0.6f * lv, [&] { TubeOptions o; o.w = 0.6f + 0.5f * s; tube (gr, Lines { shoulders, circle (x, y - 58 * s, 15 * s, 24) }, col, o); });
                bulb (gr, x, y - 58 * s, C::white, lv * 0.7f, 2.6f * s);
            }
            TubeOptions mic; mic.w = 0.7f; mic.style = 0;
            tube (gr, Lines { { { -10, 40 }, { -10, 58 }, { 10, 58 }, { 10, 40 } }, { { 0, 58 }, { 0, 76 } } }, c[1], mic);
            mic.closed = true;
            tube (gr, roundRect (-7, 22, 14, 22, 7), c[1], mic);
            text (gr, "you", 0, 102, 17, c[1], 0.5f, Align::centre);
            text (gr, "left", -690, 60, 16, c[0], 0.4f);
            text (gr, "right", 690, 60, 16, c[0], 0.4f, Align::right);
            text (gr, "far", -380, -334, 15, c[0], 0.35f, Align::right);
            text (gr, "near", -580, -10, 15, c[0], 0.35f, Align::right);
        };
    }

    // Presets
    {
        auto& g = wall.add (B, 1040, 186, 0.5f, 200, 30, 260);
        auto& pr = b.choice (g, Preset, { "Unison", "Choir", "Rabble", "Stadium" }, 0, 0);
        pr.size = 22; pr.gap = 18;
        pr.setChoice = [dev, w] (int v)
        {
            v = juce::jlimit (0, 3, v);
            dev->setRaw (Preset, (float) v);
            const int params[6] = { Size, Spread, Timing, Tuning, Sway, Depth };
            for (int k = 0; k < 6; ++k) dev->set (params[k], presets[v][k]);
            static const char* names[] = { "unison", "choir", "rabble", "stadium" };
            w->say (names[v]);
        };
    }

    {
        auto& g = wall.add (Cc, 100, 780, 0.4f, 330, 40, 380);
        auto& size = g.counter (11, 0, 0);
        size.text = "crowd size";
        size.getChoice = [dev] { return juce::roundToInt (dev->get (Size) * 10); };
        size.setChoice = [dev, w] (int i) { dev->set (Size, (float) i / 10); w->say ("crowd size   " + String (headcount (dev->get (Size))) + " people"); };
        size.tip = [dev] { return "crowd size   " + String (headcount (dev->get (Size))) + " people"; };
        b.fader (g, Depth, "Depth", pctF, 0, 76, 180, 76, P (90, 88));
        b.fader (g, Mix, "Mix", pctF, 230, 76, 410, 76, P (320, 64));
    }

    {
        auto& g = wall.add (A, 900, 780, -0.4f, 300, 40, 360);
        b.fader (g, Timing, "Timing", [] (float v) { return "up to " + String (juce::roundToInt (6 + v * 40)) + " ms"; }, 0, 0, 160, 0, P (80, -12));
        b.fader (g, Tuning, "Tuning", [] (float v) { return "±" + String (juce::roundToInt (v * 30)) + " ct"; }, 200, 0, 360, 0, P (280, 12));
        b.fader (g, Sway, "Sway", [] (float v) { return String (expMap (v, 0.1f, 4.0f), 2) + " Hz"; }, 400, 0, 560, 0, P (480, -12));
        g.draw = [] (juce::Graphics& gr, double, float, const Colour* c)
        {
            text (gr, "drag any person to move them, drag the lamp above the crowd to spread it", 0, 76, 15, c[2], 0.4f);
        };
    }
}

//==============================================================================
// Transformer

void xfmrWall (Builder& b)
{
    using namespace Param::Transformer;
    auto* dev = &b.d;
    auto& wall = b.wall;
    auto* w = &wall;
    rainbow (b, GR, OR, CY);
    b.title ("Transformer", 640, [] { return String ("circuit bending with broken parts"); });
    enum { A = 1, B, Cc };
    static const char* names[parts] = { "dying cap", "scratchy pot", "leaky diode", "sagging supply", "bent clock", "cold joint", "hot hum" };
    auto voltsWord = [] (float v) { return String (volts (v), 1) + " v"; };

    // Each part's symbol, drawn in neon
    auto symbol = [] (int k, float x, float y) -> Lines
    {
        switch (k)
        {
            case DyingCap:
            {
                Line plate;
                for (int q = 0; q <= 12; ++q) { const float a = -0.5f + (float) q / 12; plate.push_back ({ x + 10 - std::cos (a) * 4, y + a * 52 }); }
                return { { { x - 36, y }, { x - 6, y } }, { { x - 6, y - 26 }, { x - 6, y + 26 } }, plate, { { x + 6, y }, { x + 36, y } },
                         { { x - 20, y - 28 }, { x - 12, y - 28 } }, { { x - 16, y - 32 }, { x - 16, y - 24 } } };
            }
            case ScratchyPot:
                return { { { x - 36, y }, { x - 24, y }, { x - 18, y - 12 }, { x - 6, y + 12 }, { x + 6, y - 12 }, { x + 18, y + 12 }, { x + 24, y }, { x + 36, y } },
                         { { x - 14, y + 30 }, { x + 14, y - 26 } }, { { x + 3, y - 24 }, { x + 14, y - 26 }, { x + 13, y - 15 } } };
            case LeakyDiode:
                return { { { x - 36, y }, { x - 14, y } }, { { x - 14, y - 18 }, { x - 14, y + 18 }, { x + 14, y }, { x - 14, y - 18 } }, { { x + 14, y - 18 }, { x + 14, y + 18 } },
                         { { x + 14, y }, { x + 36, y } }, { { x + 22, y - 30 }, { x + 18, y - 22 }, { x + 26, y - 20 }, { x + 22, y - 12 } } };
            case SaggingSupply:
            {
                Line coil { { x - 36, y }, { x - 26, y } };
                for (int j = 0; j < 4; ++j)
                    for (int q = 0; q <= 8; ++q) { const float a = pi - (float) q / 8 * pi, cx = x - 19.5f + (float) j * 13; coil.push_back ({ cx + std::cos (a) * 6.5f, y - std::sin (a) * 12 }); }
                coil.push_back ({ x + 26, y }); coil.push_back ({ x + 36, y });
                return { coil };
            }
            case BentClock:
                return { { { x - 36, y }, { x - 14, y } }, { { x - 14, y - 22 }, { x - 14, y + 22 } }, roundRect (x - 7, y - 17, 14, 34, 2), { { x + 14, y - 22 }, { x + 14, y + 22 } }, { { x + 14, y }, { x + 36, y } } };
            case ColdJoint:
                return { { { x - 36, y }, { x - 11, y } }, circle (x, y, 11, 24), { { x - 3, y - 11 }, { x + 2, y - 3 }, { x - 3, y + 3 }, { x + 3, y + 11 } }, { { x + 11, y }, { x + 36, y } } };
            default:
            {
                Line l1, l2;
                for (int q = 0; q <= 12; ++q) { const float u = (float) q / 12; l1.push_back ({ x - 20 + std::abs (std::sin (u * 3 * pi)) * 8, y - 20 + u * 40 }); l2.push_back ({ x + 20 - std::abs (std::sin (u * 3 * pi)) * 8, y - 20 + u * 40 }); }
                return { { { x - 36, y - 20 }, { x - 20, y - 20 } }, l1, { { x - 20, y + 20 }, { x - 36, y + 20 } }, { { x - 3, y - 26 }, { x - 3, y + 26 } },
                         { { x + 3, y - 26 }, { x + 3, y + 26 } }, { { x + 36, y - 20 }, { x + 20, y - 20 } }, l2, { { x + 20, y + 20 }, { x + 36, y + 20 } } };
            }
        }
    };

    // The board
    {
        auto& g = wall.add (A, 80, 206, -0.35f, 560, 270, 640);
        const float BW = 1120, BH = 540, MY = 300;
        auto CX = [] (int i) { return 110.0f + (float) i * 150.0f; };
        for (int i = 0; i < parts; ++i)
        {
            const float x = CX (i), y = i % 2 ? 120.0f : 92.0f, tw = (float) String (names[i]).length() * 8.6f;
            auto flip = [dev, w, i] { const bool v = ! dev->on (On0 + i); dev->setRaw (On0 + i, v ? 1.0f : 0.0f); w->say (String (names[i]) + (v ? "   switched on" : "   switched off")); };
            auto& t = g.toggle (names[i], x - tw / 2, y, [dev, i] { return dev->on (On0 + i); }, [flip] (bool) { flip(); });
            t.ci = i % 3; t.size = 17;
            auto& lamp = g.dragArea ({ x - 18, MY - 136, 36, 44 }, Cursor::PointingHandCursor);
            lamp.tip = [dev, i] { return String (names[i]) + "   " + (dev->on (On0 + i) ? "on" : "off") + "   " + F::pct (dev->get (Cap + i)); };
            lamp.down = [flip] (float, float) -> std::optional<Gesture> { flip(); return std::nullopt; };
            auto& f = b.fader (g, Cap + i, "", pctF, x - 56, MY + 112, x + 56, MY + 112, P (x, MY + 126));
            f.ci = i % 3;
            f.tip = [dev, i] { return String (names[i]) + "   " + F::pct (dev->get (Cap + i)); };
        }
        auto rng = std::make_shared<juce::Random> (5);
        g.draw = [=] (juce::Graphics& gr, double t, float, const Colour* c)
        {
            const auto* node = dev->live();
            const float l = node != nullptr ? clampf (node->inputLevel.load() * 4) : 0.0f, batt = dev->get (Battery);
            TubeOptions frame; frame.closed = true; frame.clips = 220;
            tube (gr, roundRect (0, 0, BW, BH, 26), c[0], frame);
            TubeOptions screw; screw.off = true; screw.w = 0.7f;
            for (auto p : { P (24, 24), P (BW - 24, 24), P (24, BH - 24), P (BW - 24, BH - 24) }) tube (gr, circle (p.x, p.y, 6, 16), c[0], screw);
            faded (gr, 0.3f + 0.7f * batt, [&] { TubeOptions o; o.w = 0.9f; o.clips = 200; tube (gr, Line { { 44, 52 }, { BW - 44, 52 } }, c[1], o); });
            text (gr, "+ " + voltsWord (batt), 50, 40, 16, c[1], 0.5f);
            TubeOptions gnd; gnd.off = true; gnd.w = 0.8f;
            tube (gr, Line { { 44, BH - 52 }, { BW - 44, BH - 52 } }, c[0], gnd);
            text (gr, "ground", 50, BH - 62, 15, c[0], 0.35f);
            Lines trace { { { 24, MY }, { CX (0) - 36, MY } } };
            for (int i = 0; i < parts - 1; ++i) trace.push_back ({ { CX (i) + 36, MY }, { CX (i + 1) - 36, MY } });
            trace.push_back ({ { CX (parts - 1) + 36, MY }, { BW - 24, MY } });
            TubeOptions tr; tr.w = 0.9f;
            tube (gr, trace, c[0], tr);
            TubeOptions jack; jack.w = 0.9f;
            tube (gr, Lines { circle (14, MY, 10, 20), circle (BW - 14, MY, 10, 20) }, c[2], jack);
            text (gr, "in", 6, MY - 22, 15, c[2], 0.5f);
            text (gr, "out", BW - 30, MY - 22, 15, c[2], 0.5f);
            TubeOptions dead; dead.off = true; dead.w = 0.6f;
            tube (gr, Lines { { { CX (1) + 70, 52 }, { CX (1) + 70, 170 }, { CX (2) - 60, 170 } }, { { CX (4) + 70, BH - 52 }, { CX (4) + 70, 420 }, { CX (5) - 40, 420 } },
                              { { CX (3) - 60, BH - 52 }, { CX (3) - 60, 440 } } }, c[1], dead);
            for (auto p : { P (CX (2) - 60, 170), P (CX (5) - 40, 420), P (CX (3) - 60, 440) }) tube (gr, circle (p.x, p.y, 5, 14), c[1], dead);
            for (int i = 0; i < parts; ++i)
            {
                const float x = CX (i);
                const bool on = dev->on (On0 + i);
                const auto col = c[i % 3];
                const float a = node != nullptr ? clampf (node->views[(size_t) i].level.load()) : 0.0f;
                const float fl = 1.0f - a * rng->nextFloat() * 0.7f;
                TubeOptions lead; lead.off = true; lead.w = 0.5f;
                tube (gr, Line { { x, 52 }, { x, MY - 128 } }, col, lead);
                if (on)
                    faded (gr, (0.55f + 0.45f * l) * fl, [&] { TubeOptions o; o.w = 0.95f; o.style = look().style == 1 ? 1 : 0; tube (gr, symbol (i, x, MY), a > 0.5f && rng->nextFloat() < 0.3f ? C::white : col, o); });
                else { TubeOptions o; o.off = true; o.w = 0.95f; tube (gr, symbol (i, x, MY), col, o); }
                bulb (gr, x, MY - 114, col, on ? clampf (0.55f + 0.45f * a * fl) : 0.0f, 8.5f);
                TubeOptions stem; stem.w = 0.45f; stem.style = 0; stem.off = ! on;
                tube (gr, Line { { x, MY - 104 }, { x, MY - 40 } }, col, stem);
            }
            // The signal running along the trace, stuttering where a part is acting up
            if (l > 0.02f)
                for (int k = 0; k < 18; ++k)
                {
                    const float u = std::fmod ((float) t * 0.22f * (0.4f + l) + (float) k / 18, 1.0f), x = 24 + u * (BW - 48);
                    bool inside = false;
                    int near = -1;
                    for (int i = 0; i < parts; ++i) { if (std::abs (x - CX (i)) < 40) inside = true; if (near < 0 && std::abs (x - CX (i)) < 110) near = i; }
                    if (inside) continue;
                    const float jit = near >= 0 && dev->on (On0 + near) && node != nullptr ? clampf (node->views[(size_t) near].level.load()) : 0.0f;
                    bulb (gr, x, MY, C::white, clampf (l * (1 - jit * rng->nextFloat())), 2.6f);
                }
            text (gr, "click a lamp or its name to switch a part on, drag the tube under it to set how broken it is", 0, BH + 44, 15, c[2], 0.4f);
        };
    }

    // The battery: drag it down to starve the circuit
    {
        auto& g = wall.add (B, 1270, 246, 1, 60, 200, 230);
        auto& a = g.dragArea ({ 0, 0, 120, 420 }, Cursor::UpDownResizeCursor);
        a.start = [dev] { return std::vector<float> { dev->get (Battery) }; };
        a.drag = [dev] (float, float dy, const std::vector<float>& s) { dev->set (Battery, s[0] - dy / 300); };
        a.tip = [dev, voltsWord] { return "battery   " + voltsWord (dev->get (Battery)); };
        g.draw = [dev, voltsWord] (juce::Graphics& gr, double, float, const Colour* c)
        {
            const float v = dev->get (Battery);
            TubeOptions box; box.closed = true; box.clips = 200;
            tube (gr, roundRect (0, 30, 120, 380, 16), c[0], box);
            TubeOptions cap; cap.closed = true; cap.w = 0.8f;
            tube (gr, roundRect (36, 6, 48, 24, 6), c[0], cap);
            const int n = (int) std::ceil (v * 6 - 0.01f);
            for (int k = 0; k < 6; ++k)
            {
                const float y = 356 - (float) k * 56;
                TubeOptions cell; cell.closed = true; cell.w = 0.8f; cell.off = k >= n;
                tube (gr, roundRect (18, y, 84, 40, 6), k < 2 && n <= 2 ? c[2] : c[1], cell);
            }
            text (gr, "battery", 60, -14, 17, c[0], 0.55f, Align::centre);
            text (gr, voltsWord (v), 60, 452, 26, c[2], 0.95f, Align::centre);
            text (gr, v > 0.8f ? "fresh" : v > 0.4f ? "tired" : "dying", 60, 484, 16, c[2], 0.5f, Align::centre);
            cable (gr, { { 60, 6 }, { 60, -30 }, { -20, -40 }, { -58, 12 } });
        };
    }

    {
        auto& g = wall.add (Cc, 1250, 800, 0.5f, 100, 40, 200);
        b.fader (g, Mix, "Mix", pctF, 0, 0, 150, 0, P (75, -12));
        b.fader (g, Output, "Output", dbF, 0, 74, 150, 74, P (75, 86));
    }
}
}
