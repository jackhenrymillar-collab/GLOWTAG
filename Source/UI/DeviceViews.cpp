#include "DeviceViewsCommon.h"
#include "../Theme.h"
#include <set>

namespace DeviceViews
{
//==============================================================================
// The device

float Device::get (int i) const
{
    if (auto* m = module())
        if (i >= 0 && i < (int) m->knobValues.size())
            return m->knobValues[(size_t) i];
    return 0.0f;
}

void Device::setRaw (int i, float v)
{
    auto* m = module();
    if (m == nullptr || i < 0 || i >= (int) m->knobValues.size()) return;
    if (juce::exactlyEqual (m->knobValues[(size_t) i], v)) return;
    m->knobValues[(size_t) i] = v;
    ctx.engine.setKnob (track, moduleId, i, v);
    ctx.touched();
    ctx.paramTouched (track, { AutoTarget::knob, moduleId, i }, v);     // records its lane, or rests it
}

void Device::note (int pitch, bool down, float velocity) const
{
    const double t = juce::Time::getMillisecondCounterHiRes() * 0.001;
    ctx.engine.midiInput.addMessageToQueue ((down ? juce::MidiMessage::noteOn (1, juce::jlimit (0, 127, pitch), velocity)
                                                  : juce::MidiMessage::noteOff (1, juce::jlimit (0, 127, pitch))).withTimeStamp (t));
}

std::shared_ptr<const AudioData> Device::sample() const
{
    auto* m = module();
    return m != nullptr && m->sampleName.isNotEmpty() ? ctx.engine.resolveSample (m->sampleName) : nullptr;
}

const Colour* Built::colours (int mod)
{
    auto* c = cache[juce::jlimit (0, 15, mod)];
    auto& th = Theme::get();
    if (th.isRainbow())
    {
        const auto first = mod > 0 && mod < (int) parts.size() ? parts[(size_t) mod] : pal[0];
        int i = 0;
        for (int k = 0; k < 3; ++k) if (pal[k] == first) i = k;
        c[0] = first;
        c[1] = pal[(i + 1) % 3] == first ? pal[1] : pal[(i + 1) % 3];
        c[2] = pal[(i + 2) % 3];
    }
    else
    {
        const auto t = th.themeColour();
        c[0] = t; c[1] = mix (t, C::white, 0.45f); c[2] = mix (t, C::white, 0.22f);
    }
    return c;
}

//==============================================================================
// Words

namespace F
{
    String time (float s) { return s < 1.0f ? String (juce::roundToInt (s * 1000.0f)) + " ms" : String (s, s < 10.0f ? 2 : 1) + " s"; }
    String hz (float f)   { return f < 1000.0f ? String (juce::roundToInt (f)) + " Hz" : String (f / 1000.0f, f < 10000.0f ? 2 : 1) + " kHz"; }
    String pct (float v)  { return String (juce::roundToInt (v * 100.0f)) + "%"; }
    String signedVal (float v, const String& unit, int decimals) { return (v > 0.0f ? "+" : "") + (decimals == 0 ? String (juce::roundToInt (v)) : String (v, decimals)) + unit; }
    String db (float v)   { return v < 0.005f ? "-inf" : String (20.0f * std::log10 (v), 1) + " dB"; }
    String note (int n)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return String (names[((n % 12) + 12) % 12]) + String (n / 12 - 1);
    }
}

//==============================================================================
// Pieces bound to controls

String tipFor (const Device& d, int param, const String& label, const std::function<String (float)>& fmt)
{
    return label.toLowerCase() + "   " + fmt (d.get (param)).toLowerCase();
}

String Builder::tip (int param, const String& label, const std::function<String (float)>& fmt) const { return tipFor (d, param, label, fmt); }

Item& Builder::fader (Group& g, int param, const String& label, std::function<String (float)> fmt,
                      float x0, float y0, float x1, float y1, std::optional<P> bend, bool isNew, bool bipolar)
{
    auto& it = g.fader (x0, y0, x1, y1, bend);
    auto* dev = &d;
    it.text = label.toLowerCase();
    it.param = param;
    it.isNew = isNew;
    it.bipolar = bipolar;
    if (auto* m = d.module()) it.def = moduleInfo (m->type).defaultValue (param);
    it.get = [dev, param] { return dev->get (param); };
    it.set = [dev, param] (float v) { dev->set (param, v); };
    it.tip = [dev, param, label, fmt] { return tipFor (*dev, param, label, fmt); };
    return it;
}

Item& Builder::choice (Group& g, int param, std::vector<String> words, float x, float y, bool isNew, std::vector<int> values)
{
    std::vector<std::pair<int, String>> opts;
    for (size_t i = 0; i < words.size(); ++i) opts.push_back ({ i < values.size() ? values[i] : (int) i, words[i] });
    auto& it = g.words (opts, x, y);
    auto* dev = &d;
    it.param = param;
    it.isNew = isNew;
    it.getChoice = [dev, param] { return dev->choice (param); };
    it.setChoice = [dev, param] (int v) { dev->setRaw (param, (float) v); };
    return it;
}

Item& Builder::toggle (Group& g, int param, const String& word, float x, float y, bool isNew)
{
    auto* dev = &d;
    auto* w = &wall;
    auto& it = g.toggle (word, x, y, [dev, param] { return dev->on (param); },
                         [dev, w, param, word] (bool v) { dev->setRaw (param, v ? 1.0f : 0.0f); w->say (word.toLowerCase() + (v ? "   on" : "   off")); });
    it.param = param;
    it.isNew = isNew;
    return it;
}

Group& Builder::title (const String& name, float nx, std::function<String()> line)
{
    auto& g = wall.add (0, 48, 30, 0, 170, 60, 300);
    auto* eng = &d.ctx.engine;
    auto* w = &wall;
    const bool isLfo = name == "LFO";
    if (! isLfo)
    {
        auto& play = g.toggle ("play", nx, 74, [eng] { return eng->isPlaying(); }, [eng] (bool v) { if (v) eng->play(); else eng->stop(); });
        play.tip = [eng] { return String (eng->isPlaying() ? "the song is playing" : "play the song"); };
    }
    g.draw = [name, nx, line, w, isLfo] (juce::Graphics& gr, double, float, const Colour* c)
    {
        bigTag (gr, name, 0, 96, c[0]);
        text (gr, line(), nx, 36, 24, c[1], 0.9f);
        const double age = now() - w->readoutTime;
        if (age < 3.5) text (gr, w->readout, nx, isLfo ? 76.0f : 112.0f, 21, C::white, juce::jlimit (0.0f, 1.0f, 1.2f - (float) age / 3.0f) * 0.65f);
    };
    return g;
}

//==============================================================================
// Pictures

AnalogEnv::AnalogEnv (float a, float d, float s, float r, float h) : H (h), S (s), A (a), D (d), R (r)
{
    tA = a * 0.25f * std::log (5.0f);
    dD = s < 0.999f ? d * 1.25f : 0.0f;
    dR = r * 1.25f;
}

float AnalogEnv::base (float t) const
{
    if (t < tA) return juce::jmin (1.0f, 1.25f * (1.0f - std::exp (-t / (A / 4.0f))));
    if (t < tA + H) return 1.0f;
    return S + (1.0f - S) * std::exp (-(t - tA - H) / (D / 4.0f));
}

float AnalogEnv::level (float t, float tr) const
{
    if (tr < 0.0f || t < tr) return base (t);
    return juce::jmax (0.0f, (base (tr) + 0.02f) * std::exp (-(t - tr) / (R / 4.0f)) - 0.02f);
}

P EnvPicture::at (const AnalogEnv& e, float t, float tr) const
{
    const float pre = e.tA + e.H + e.dD;
    if (tr >= 0.0f && t >= tr)
    {
        const float u = juce::jmin (1.0f, (t - tr) / e.dR);
        return { xs[4] + ws[4] * u, Y (e.level (t, tr)) };
    }
    float x;
    if (t < e.tA) x = xs[0] + ws[0] * t / e.tA;
    else if (t < e.tA + e.H) x = xs[1] + ws[1] * (t - e.tA) / e.H;
    else if (t < pre) x = xs[2] + ws[2] * (t - e.tA - e.H) / e.dD;
    else x = xs[3] + ws[3] * juce::jmin (1.0f, (t - pre) / 1.2f);
    return { x, Y (e.level (t, -1.0f)) };
}

EnvPicture drawEnv (juce::Graphics& g, const AnalogEnv& e, float x0, float y0, float w, float h, Colour col, TubeOptions o, bool clipsSet)
{
    auto f = [] (float dd) { return std::pow (juce::jmin (dd, 8.0f) / 8.0f, 0.4f); };
    EnvPicture p;
    p.y0 = y0; p.h = h;
    const float ws[5] = { 0.03f + 0.25f * f (e.tA), e.H > 0.0f ? 0.1f * f (e.H) + 0.02f : 0.0f, e.dD > 0.0f ? 0.03f + 0.25f * f (e.dD) : 0.0f, 0.16f, 0.03f + 0.28f * f (e.dR) };
    p.xs[0] = x0;
    for (int i = 0; i < 5; ++i) { p.ws[i] = ws[i] * w; p.xs[i + 1] = p.xs[i] + p.ws[i]; }
    Line pts;
    for (int i = 0; i <= 20; ++i) pts.push_back ({ p.xs[0] + p.ws[0] * (float) i / 20.0f, p.Y (e.base (e.tA * (float) i / 20.0f)) });
    if (e.H > 0.0f) pts.push_back ({ p.xs[2], p.Y (1.0f) });
    if (e.dD > 0.0f)
        for (int i = 1; i <= 20; ++i) pts.push_back ({ p.xs[2] + p.ws[2] * (float) i / 20.0f, p.Y (e.base (e.tA + e.H + e.dD * (float) i / 20.0f)) });
    pts.push_back ({ p.xs[4], p.Y (e.S) });
    for (int i = 1; i <= 22; ++i)
    {
        const float t = e.dR * (float) i / 22.0f;
        pts.push_back ({ p.xs[4] + p.ws[4] * (float) i / 22.0f, p.Y (juce::jmax (0.0f, (e.S + 0.02f) * std::exp (-t / (e.dR / 5.0f)) - 0.02f)) });
    }
    if (! o.base) o.base = y0 + h;
    if (! clipsSet) o.clips = 130.0f;
    tube (g, pts, col, o);
    return p;
}

float filterMagnitude (float f, float fc, float res, int mode)
{
    const float x = f / fc, Q = 0.6f + res * 11.0f;
    if (mode == 0)
    {
        const float k = res * 4.0f, r = 1.0f - x * x, i = 2.0f * x, r4 = r * r - i * i, i4 = 2.0f * r * i;
        return std::pow (1.0f + k, 0.75f) / juce::jmax (0.02f, std::hypot (r4 + k, i4));
    }
    const float den = juce::jmax (0.02f, std::hypot (1.0f - x * x, x / Q));
    if (mode == 3) return x * x / den;
    if (mode == 2) return (x / Q) / den * juce::jmin (Q, 2.2f);
    return 1.0f / den;
}

void dashes (juce::Graphics& g, const juce::Path& p)
{
    juce::Path d;
    const float dash[] = { 2.0f, 6.0f };
    juce::PathStrokeType (1.0f).createDashedStroke (d, p, dash, 2);
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.fillPath (d);
}

FilterPicture drawFilter (juce::Graphics& g, float x0, float y0, float w, float h, float fcBase, float fcLive, float res, int mode, const Colour* c)
{
    FilterPicture fp { x0, y0, w, h, 0, 0, y0 + h };
    juce::Path grid;
    for (float f : { 100.0f, 1000.0f, 10000.0f }) { grid.startNewSubPath (fp.X (f), y0 + 10); grid.lineTo (fp.X (f), y0 + h); }
    dashes (g, grid);
    label (g, "100", fp.X (100) + 4, y0 + h - 2); label (g, "1k", fp.X (1000) + 4, y0 + h - 2); label (g, "10k", fp.X (10000) + 4, y0 + h - 2);
    auto curveFor = [&] (float fc)
    {
        Line p;
        for (int i = 0; i <= 90; ++i) { const float f = 20.0f * std::pow (1000.0f, (float) i / 90.0f); p.push_back ({ fp.X (f), fp.Y (filterMagnitude (f, fc, res, mode)) }); }
        return p;
    };
    const bool moving = std::abs (std::log (fcLive / fcBase)) > 0.02f;
    if (moving) { TubeOptions o; o.off = true; tube (g, curveFor (fcBase), c[0], o); }
    TubeOptions o; o.base = y0 + h; o.clips = 170;
    tube (g, curveFor (fcLive), c[0], o);
    if (moving) bulb (g, fp.X (fcLive), fp.Y (filterMagnitude (fcLive, fcLive, res, mode)), c[1], 1, 3.4f);
    fp.bx = fp.X (fcBase);
    fp.by = fp.Y (filterMagnitude (fcBase, fcBase, res, mode));
    return fp;
}

void scope (juce::Graphics& g, const Node* n, Colour col, float x0, float y0, float w, float h)
{
    Line pts;
    if (n != nullptr)
    {
        float d[512];
        n->scope.read (d, 512);
        int z = 0;
        for (int i = 1; i < 200; ++i) if (d[i - 1] < 0.0f && d[i] >= 0.0f) { z = i; break; }
        for (int i = 0; i < 300; i += 2) pts.push_back ({ x0 + (float) i / 300.0f * w, y0 + h / 2 - juce::jlimit (-1.0f, 1.0f, d[z + i] * 2.2f) * h * 0.46f });
    }
    else
        pts = { { x0, y0 + h / 2 }, { x0 + w, y0 + h / 2 } };
    TubeOptions o; o.clips = 150; o.step = 5;
    tube (g, pts, col, o);
}

float waveShape (float shape, float t, float pw)
{
    t -= std::floor (t);
    const float saw = 2.0f * t - 1.0f, sq = t < pw ? 1.0f : -1.0f, tri = t < 0.5f ? 4.0f * t - 1.0f : 3.0f - 4.0f * t;
    const float sine = std::sin (juce::MathConstants<float>::twoPi * t);
    return shape < 1.0f ? saw + (sq - saw) * shape : shape < 2.0f ? sq + (tri - sq) * (shape - 1.0f) : tri + (sine - tri) * (shape - 2.0f);
}

//==============================================================================
// Synth

namespace
{
    const char* waveNames[] = { "Saw", "Square", "Tri", "Sine" };
    String waveWord (float v)
    {
        const float s = v * 3.0f;
        const int i = juce::roundToInt (s);
        if (std::abs (s - (float) i) < 0.06f) return waveNames[juce::jlimit (0, 3, i)];
        const int f = juce::jlimit (0, 2, (int) std::floor (s));
        return String (waveNames[f]) + " > " + waveNames[f + 1];
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

    // The synth's mod matrix, worked out for the picture
    float modSum (const Device& d, int dest, double t, float vel = 0.7f)
    {
        using namespace Param::Synth;
        float s = 0.0f;
        for (int row = 0; row < matrixRows; ++row)
        {
            const float a = d.get (MxAmt0 + row * 3);
            if (std::abs (a) < 0.001f || d.choice (MxDst0 + row * 3) != dest) continue;
            const int src = d.choice (MxSrc0 + row * 3);
            float v;
            if (src == Lfo1) v = Param::lfoShape (d.choice (L1Shape), t * lfoHz (d, L1Rate));
            else if (src == Lfo2) v = Param::lfoShape (d.choice (L2Shape), t * lfoHz (d, L2Rate));
            else if (src == Velocity) v = (vel - 0.5f) * 2.0f;
            else v = std::sin (std::floor ((float) t * 3.0f) * 91.7f) * 0.8f;
            s += a * v;
        }
        return s;
    }

    // Voices the node is playing, newest first
    std::vector<const VoiceView*> liveVoices (const Node* n)
    {
        std::vector<const VoiceView*> v;
        if (n == nullptr) return v;
        for (auto& view : n->views) if (view.pitch.load() >= 0) v.push_back (&view);
        std::sort (v.begin(), v.end(), [] (auto* a, auto* b) { return a->age.load() < b->age.load(); });
        return v;
    }
}

void synthWall (Builder& b)
{
    using namespace Param::Synth;
    auto& d = b.d;
    auto* dev = &d;
    auto& wall = b.wall;
    auto* w = &wall;
    b.out.pal[0] = C::VI; b.out.pal[1] = C::PK; b.out.pal[2] = C::CY;
    b.out.parts = { C::VI, C::VI, C::PK, C::CY, C::YL, C::BL };
    enum { osc = 1, filt, env, mot, voice };

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
        // Redo the title's drawing so the preset name is centred between the arrows
        auto line = [preset] { return String (presets()[(size_t) *preset].name).toLowerCase(); };
        tg.draw = [w, line] (juce::Graphics& g, double, float, const Colour* c)
        {
            bigTag (g, "Synth", 0, 96, c[0]);
            text (g, line(), 290 + 135, 36, 24, c[1], 0.9f, Align::centre);
            const double age = now() - w->readoutTime;
            if (age < 3.5) text (g, w->readout, 290, 112, 21, C::white, juce::jlimit (0.0f, 1.0f, 1.2f - (float) age / 3.0f) * 0.65f);
        };
        auto& prev = tg.toggle ("<", 290, 36, [] { return true; }, [setPreset, preset] (bool) { setPreset (*preset - 1); });
        prev.size = 26;
        auto& next = tg.toggle (">", 540, 36, [] { return true; }, [setPreset, preset] (bool) { setPreset (*preset + 1); });
        next.size = 26;
    }

    const auto pctF = [] (float v) { return F::pct (v); };

    // Oscillators: drag a wave sideways to morph it, up and down for its width (or B's detune)
    {
        auto& g = wall.add (osc, 62, 196, -1.5f, 250, 200, 330);
        auto& a = g.dragArea ({ 0, 36, 500, 108 }, Cursor::UpDownLeftRightResizeCursor);
        a.start = [dev] { return std::vector<float> { dev->get (Wave), dev->get (WidthA) }; };
        a.drag = [dev] (float dx, float dy, const std::vector<float>& s) { dev->set (Wave, s[0] + dx / 380); dev->set (WidthA, s[1] - dy / 160); };
        a.tip = [dev] { return "osc a   " + waveWord (dev->get (Wave)).toLowerCase() + "   width " + String (juce::roundToInt (5 + dev->get (WidthA) * 90)) + "%"; };
        auto& bb = g.dragArea ({ 0, 176, 500, 108 }, Cursor::UpDownLeftRightResizeCursor);
        bb.start = [dev] { return std::vector<float> { dev->get (WaveB), dev->get (Detune) }; };
        bb.drag = [dev] (float dx, float dy, const std::vector<float>& s) { dev->set (WaveB, s[0] + dx / 380); dev->set (Detune, s[1] - dy / 160); };
        bb.tip = [dev] { return "osc b   " + waveWord (dev->get (WaveB)).toLowerCase() + "   " + String (1200.0f * std::log2 (1.0f + dev->get (Detune) * 0.03f), 1) + " ct apart"; };
        auto& sub = g.dragArea ({ 0, 305, 500, 60 }, Cursor::UpDownResizeCursor);
        sub.start = [dev] { return std::vector<float> { dev->get (Sub) }; };
        sub.drag = [dev] (float, float dy, const std::vector<float>& s) { dev->set (Sub, s[0] - dy / 90); };
        sub.tip = [dev] { return "sub   " + F::pct (dev->get (Sub)); };
        const auto octF = [] (float v) { const int o = juce::roundToInt (v * 4 - 2); return F::signedVal ((float) o, " oct", 0); };
        const auto semiF = [] (float v) { const int o = juce::roundToInt (v * 24 - 12); return F::signedVal ((float) o, " st", 0); };
        b.fader (g, OctA, "Octave", octF, 540, 140, 540, 48, {}, true, true);
        b.fader (g, SemiB, "Semi", semiF, 540, 284, 540, 192, {}, true, true);
        b.fader (g, Param::Synth::Noise, "Noise", pctF, 0, 408, 150, 408, P (75, 394), true);
        b.fader (g, Drift, "Drift", pctF, 190, 408, 340, 408, P (265, 422), true);
        b.toggle (g, Sync, "hard sync", 390, 412, true);
        g.draw = [dev] (juce::Graphics& gr, double t, float, const Colour* c)
        {
            const float pwm = juce::jlimit (0.04f, 0.96f, 0.05f + dev->get (WidthA) * 0.9f + modSum (*dev, ToWidthA, t) * 0.35f);
            const float oA = std::pow (2.0f, (float) juce::roundToInt (dev->get (OctA) * 4 - 2));
            const float sB = std::pow (2.0f, (float) juce::roundToInt (dev->get (SemiB) * 24 - 12) / 12.0f);
            const float detune = dev->get (Detune), drift = dev->get (Drift), noise = dev->get (Param::Synth::Noise), sub = dev->get (Sub);
            const float ph = (float) t * (0.12f + detune * 2.2f) + std::sin ((float) t * 0.7f) * drift * 0.15f;
            const bool sync = dev->on (Sync);
            const float wA = dev->get (Wave) * 3, wB = dev->get (WaveB) * 3;
            auto nz = [&] (int i) { return noise * std::sin ((float) i * 12.9898f + std::floor ((float) t * 24) * 78.233f) * 0.45f; };
            auto line = [] (auto fn, float mid, float amp)
            {
                Line l;
                for (int i = 0; i <= 200; ++i) l.push_back ({ (float) i / 200 * 500, mid - juce::jlimit (-1.25f, 1.25f, fn ((float) i / 200 * 2, i)) * amp });
                return l;
            };
            TubeOptions oa; oa.clips = 170; oa.base = 90;
            tube (gr, line ([&] (float x, int i) { return waveShape (wA, x * oA, pwm) + nz (i); }, 90, 44), c[0], oa);
            TubeOptions ob; ob.clips = 170; ob.base = 230;
            tube (gr, line ([&] (float x, int i)
            {
                const float phase = sync ? std::fmod (x * oA, 1.0f) * sB / oA + ph * 0.3f : x * sB + ph;
                return waveShape (wB, phase, 0.5f) + nz (i + 50);
            }, 230, 44), c[1], ob);
            const auto sl = line ([] (float x, int) { return waveShape (1, x * 0.5f + 0.001f, 0.5f); }, 336, 5 + sub * 20);
            TubeOptions os; if (sub > 0.03f) os.w = 0.8f; else os.off = true;
            tube (gr, sl, c[2], os);
            text (gr, "a", -26, 98, 28, c[0]); text (gr, "b", -26, 238, 28, c[1]); text (gr, "sub", 508, 342, 18, c[2], 0.6f);
        };
    }

    // Filter: the response curve, with a lamp for cutoff and resonance and one for the envelope sweep
    {
        auto& g = wall.add (filt, 668, 156, 1, 235, 170, 320);
        b.choice (g, FMode, { "LP 24", "LP 12", "BP", "HP" }, 0, 18, true);
        auto* gp = &g;
        auto& h = g.handle ([gp]() -> std::optional<P> { auto& s = gp->scratch["f"]; if (s.size() < 2) return std::nullopt; return P (s[0], s[1]); });
        h.r = 6.5f; h.ci = 1;
        h.start = [dev] { return std::vector<float> { dev->get (Cutoff), dev->get (Res) }; };
        h.drag = [dev] (float dx, float dy, const std::vector<float>& s) { dev->set (Cutoff, s[0] + dx / 470 * 1.153f); dev->set (Res, s[1] - dy / 170); };
        h.tip = [dev] { return "cutoff " + F::hz (expMap (dev->get (Cutoff), 40, 16000)).toLowerCase() + "   res " + F::pct (dev->get (Res)); };
        auto& e = g.handle ([gp]() -> std::optional<P> { auto& s = gp->scratch["e"]; if (s.size() < 2) return std::nullopt; return P (s[0], s[1]); });
        e.r = 4.5f; e.ci = 2;
        e.start = [dev] { return std::vector<float> { dev->get (Env) }; };
        e.drag = [dev] (float dx, float, const std::vector<float>& s) { dev->set (Env, s[0] + dx / 470 * 2); };
        e.tip = [dev] { return "envelope sweep   " + F::pct (dev->get (Env)); };
        b.fader (g, Drive, "Drive", pctF, 0, 345, 140, 345, P (70, 333));
        b.fader (g, Key, "Key", pctF, 165, 345, 305, 345, {}, true);
        b.fader (g, Vel, "Vel", pctF, 330, 345, 470, 345, P (400, 357), true);
        g.draw = [dev, gp] (juce::Graphics& gr, double t, float, const Colour* c)
        {
            const float base = expMap (dev->get (Cutoff), 40, 16000), envAmt = dev->get (Env);
            const auto voices = liveVoices (dev->live());
            float oct = modSum (*dev, ToCutoff, t, voices.empty() ? 0.7f : voices[0]->velocity.load()) * 2;
            if (! voices.empty())
            {
                const auto* v = voices[0];
                oct += v->filter.load() * envAmt * 5 + (v->velocity.load() - 0.5f) * 3 * dev->get (Vel)
                       + std::log2 (440.0f * std::pow (2.0f, ((float) v->pitch.load() - 69) / 12.0f) / 261.6f) * dev->get (Key);
            }
            const int mode = dev->choice (FMode);
            const auto fp = drawFilter (gr, 0, 40, 470, 250, base, juce::jlimit (25.0f, 19000.0f, base * std::pow (2.0f, oct)),
                                        juce::jlimit (0.0f, 1.0f, dev->get (Res) + modSum (*dev, ToResonance, t) * 0.4f), mode, c);
            gp->scratch["f"] = { fp.bx, fp.by };
            const float yb = 262, xa = fp.bx, xb = fp.X (base * std::pow (2.0f, envAmt * 5));
            gp->scratch["e"] = { xb, yb };
            if (xb - xa > 6) { TubeOptions o; o.w = 0.7f; o.dashOn = 3; o.dashOff = 6; o.style = 0; tube (gr, Line { { xa, yb }, { xb - 7, yb } }, c[2], o); }
            text (gr, "sweep", (xa + xb) / 2, yb - 9, 15, c[2], 0.5f, Align::centre);
        };
    }

    // Envelopes: amp, and the filter's own (which can follow the amp)
    {
        auto& g = wall.add (env, 1172, 170, -1.2f, 200, 170, 290);
        auto* gp = &g;
        auto ampEnv = [dev] { return AnalogEnv (expMap (dev->get (Attack), 0.001f, 4), expMap (dev->get (Decay), 0.01f, 6), dev->get (Sustain), expMap (dev->get (Release), 0.01f, 8)); };
        auto filtEnv = [dev]
        {
            if (dev->on (Link)) return AnalogEnv (expMap (dev->get (Attack), 0.001f, 4) * 0.7f, expMap (dev->get (Decay), 0.01f, 6), dev->get (Sustain) * 0.5f, expMap (dev->get (Release), 0.01f, 8));
            return AnalogEnv (expMap (dev->get (FAttack), 0.001f, 4), expMap (dev->get (FDecay), 0.01f, 6), dev->get (FSustain), expMap (dev->get (FRelease), 0.01f, 8));
        };
        const auto sA = [] (float v) { return F::time (expMap (v, 0.001f, 4)); };
        const auto sD = [] (float v) { return F::time (expMap (v, 0.01f, 6)); };
        const auto sR = [] (float v) { return F::time (expMap (v, 0.01f, 8)); };
        auto handles = [&] (const String& key, int A, int D, int Su, int R, int ci, bool filter)
        {
            auto unlink = [dev, w, filter]
            {
                if (! filter || ! dev->on (Link)) return;
                dev->setRaw (Link, 0);
                dev->set (FAttack, inverse (expMap (dev->get (Attack), 0.001f, 4) * 0.7f, 0.001f, 4));
                dev->set (FDecay, dev->get (Decay)); dev->set (FSustain, dev->get (Sustain) * 0.5f); dev->set (FRelease, dev->get (Release));
                w->say ("filter envelope set free");
            };
            auto st = [dev, unlink, A, D, Su, R] { unlink(); return std::vector<float> { dev->get (A), dev->get (D), dev->get (Su), dev->get (R) }; };
            auto at = [gp, key] (int xi, float yv) -> std::optional<P>
            {
                auto& s = gp->scratch[key];
                if (s.size() < 8) return std::nullopt;
                return P (s[(size_t) xi], s[6] + s[7] - yv * s[7]);
            };
            auto& h1 = g.handle ([at] { return at (1, 1.0f); });
            h1.ci = ci; h1.start = st;
            h1.drag = [dev, A] (float dx, float, const std::vector<float>& s) { dev->set (A, s[0] + dx / 170); };
            h1.tip = [dev, A, sA] { return tipFor (*dev, A, "Attack", sA); };
            auto& h2 = g.handle ([at, dev, Su, filter] { return at (3, dev->get (Su) * (filter && dev->on (Link) ? 0.5f : 1.0f)); });
            h2.ci = ci; h2.start = st;
            h2.drag = [dev, D, Su, filter] (float dx, float dy, const std::vector<float>& s) { dev->set (D, s[1] + dx / 170); dev->set (Su, s[2] - dy / (filter ? 90.0f : 170.0f)); };
            h2.tip = [dev, D, Su, sD] { return tipFor (*dev, D, "Decay", sD) + "   " + tipFor (*dev, Su, "Sustain", F::pct); };
            auto& h3 = g.handle ([at] { return at (5, 0.0f); });
            h3.ci = ci; h3.start = st;
            h3.drag = [dev, R] (float dx, float, const std::vector<float>& s) { dev->set (R, s[3] + dx / 170); };
            h3.tip = [dev, R, sR] { return tipFor (*dev, R, "Release", sR); };
        };
        handles ("m", Attack, Decay, Sustain, Release, 0, false);
        handles ("fm", FAttack, FDecay, FSustain, FRelease, 1, true);
        b.toggle (g, Link, "filter follows amp", 0, 356, true);
        g.draw = [dev, gp, ampEnv, filtEnv] (juce::Graphics& gr, double, float, const Colour* c)
        {
            const auto e = ampEnv(), fe = filtEnv();
            TubeOptions fo; fo.w = 0.8f;
            const auto fm = drawEnv (gr, fe, 0, 232, 410, 90, c[1], fo, true);
            const auto m = drawEnv (gr, e, 0, 20, 410, 172, c[0]);
            auto keep = [] (const EnvPicture& p) { return std::vector<float> { p.xs[0], p.xs[1], p.xs[2], p.xs[3], p.xs[4], p.xs[5], p.y0, p.h }; };
            gp->scratch["m"] = keep (m); gp->scratch["fm"] = keep (fm);
            text (gr, "amp", 250, 52, 20, c[0], 0.7f); text (gr, "filter", 250, 262, 20, c[1], 0.7f);
            for (auto* v : liveVoices (dev->live()))
            {
                const float el = v->age.load(), tr = v->released.load();
                auto q = m.at (e, el, tr); bulb (gr, q.x, q.y, C::white, 1, 3.2f);
                q = fm.at (fe, el, tr);    bulb (gr, q.x, q.y, C::white, 1, 2.4f);
            }
        };
    }

    // Motion: two LFOs and the mod matrix
    {
        auto& g = wall.add (mot, 62, 640, 1.2f, 330, 90, 380);
        static const std::vector<String> shapes { "Sine", "Tri", "Saw", "Square", "S&H" };
        for (int k = 0; k < 2; ++k)
        {
            const float y0 = (float) k * 96;
            const int rate = k == 0 ? L1Rate : L2Rate, shape = k == 0 ? L1Shape : L2Shape;
            auto& a = g.dragArea ({ 0, y0, 270, 66 }, Cursor::LeftRightResizeCursor);
            a.start = [dev, rate] { return std::vector<float> { dev->get (rate) }; };
            a.drag = [dev, rate] (float dx, float, const std::vector<float>& s) { dev->set (rate, s[0] + dx / 300); };
            a.tip = [dev, rate, k] { return "lfo " + String (k + 1) + "   " + String (expMap (dev->get (rate), 0.05f, 20), 2) + " hz"; };
            auto& wd = b.choice (g, shape, shapes, 0, y0 + 86, true);
            wd.size = 16; wd.gap = 11; wd.ci = k;
        }
        static const std::vector<String> srcs { "LFO 1", "LFO 2", "Velocity", "Random" }, dsts { "Cutoff", "Pitch", "Width A", "Resonance" };
        for (int row = 0; row < matrixRows; ++row)
        {
            const float y = 14 + (float) row * 45;
            const int src = MxSrc0 + row * 3, dst = MxDst0 + row * 3, amt = MxAmt0 + row * 3;
            auto& s = g.add (Item::words);
            s.opts = { { 1, "" } }; s.x = 318; s.y = y + 6; s.size = 18; s.gap = 0; s.toggle = true; s.ci = 1;
            s.dyn = [dev, src] { return srcs[(size_t) juce::jlimit (0, 3, dev->choice (src))]; };
            s.getChoice = [] { return 1; };
            s.setChoice = [dev, w, src] (int) { const int v = (dev->choice (src) + 1) % 4; dev->setRaw (src, (float) v); w->say ("source   " + srcs[(size_t) v].toLowerCase()); };
            auto& f = g.fader (410, y, 520, y);
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
            t.opts = { { 1, "" } }; t.x = 536; t.y = y + 6; t.size = 18; t.gap = 0; t.toggle = true; t.ci = 0;
            t.dyn = [dev, dst] { return dsts[(size_t) juce::jlimit (0, 3, dev->choice (dst))]; };
            t.getChoice = [] { return 1; };
            t.setChoice = [dev, w, dst] (int) { const int v = (dev->choice (dst) + 1) % 4; dev->setRaw (dst, (float) v); w->say ("destination   " + dsts[(size_t) v].toLowerCase()); };
        }
        g.draw = [dev] (juce::Graphics& gr, double t, float, const Colour* c)
        {
            for (int k = 0; k < 2; ++k)
            {
                const float y = (float) k * 96 + 34, a = 22, hxp = 216;
                const int rate = k == 0 ? L1Rate : L2Rate, shape = dev->choice (k == 0 ? L1Shape : L2Shape);
                const float hz = lfoHz (*dev, rate), span = juce::jlimit (1.2f, 6.0f, 3 / hz);
                Line pts;
                for (int i = 0; i <= 120; ++i)
                {
                    const float x = (float) i / 120 * 270;
                    pts.push_back ({ x, y - Param::lfoShape (shape, (t + (x - hxp) / 270 * span) * hz) * a });
                }
                TubeOptions o; o.w = 0.9f;
                tube (gr, pts, c[k], o);
                bulb (gr, hxp, y - Param::lfoShape (shape, t * hz) * a, C::white, 1, 3.2f);
                text (gr, String (k + 1), -22, y + 8, 24, c[k]);
            }
            ringMark (gr, 300, -4, c[0]);
        };
    }

    // Voices: how many, how they glide, and a lamp per voice
    {
        auto& g = wall.add (voice, 742, 598, -1, 400, 90, 420);
        b.choice (g, VMode, { "Poly", "Mono", "Legato" }, 450, 18, true);
        auto& u = g.counter (5, 450, 60);
        u.text = "unison"; u.isNew = true;
        u.getChoice = [dev] { return juce::roundToInt (dev->get (Unison) * 4); };
        u.setChoice = [dev] (int i) { dev->set (Unison, (float) i / 4); };
        u.tip = [dev] { const int n = 1 + juce::roundToInt (dev->get (Unison) * 4); return "unison   " + String (n) + (n > 1 ? " voices" : " voice"); };
        b.fader (g, Glide, "Glide", [] (float v) { return v < 0.01f ? String ("off") : String (juce::roundToInt (v * 800)) + " ms"; }, 450, 122, 600, 122, P (525, 110), true);
        b.fader (g, Spread, "Spread", pctF, 650, 122, 800, 122, P (725, 134), true);
        b.fader (g, VelAmp, "Vel amp", pctF, 450, 180, 600, 180, {}, true);
        b.fader (g, Gain, "Gain", [] (float v) { return F::db (v); }, 650, 180, 800, 180);
        g.draw = [dev] (juce::Graphics& gr, double, float, const Colour* c)
        {
            const auto* node = dev->live();
            scope (gr, node, c[0], 0, 0, 400, 112);
            auto vs = liveVoices (node);
            for (int i = 0; i < 8; ++i)
            {
                const float x = 22 + (float) i * 51, y = 146 + std::sin ((float) i * 0.9f) * 5;
                const auto* v = i < (int) vs.size() ? vs[(size_t) i] : nullptr;
                bulb (gr, x, y, c[i % 2 ? 1 : 0], v != nullptr ? v->level.load() : 0.0f, 6);
                if (v != nullptr) label (gr, F::note (v->pitch.load()).toLowerCase(), x, y + 22, Align::centre, Colour (0xffb9b8b2));
            }
        };
    }

    // A keyboard of neon tubes, C3 to C5
    {
        auto& g = wall.add (osc, 170, 836, 0, 630, 34, 560);
        struct Key { int n; float x, w; bool black; };
        auto keys = std::make_shared<std::vector<Key>>();
        int wi = 0;
        for (int n = 48; n <= 72; ++n)
        {
            const int pc = n % 12;
            if (pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10) keys->push_back ({ n, (float) wi * 84 - 24, 48, true });
            else keys->push_back ({ n, (float) wi++ * 84, 84, false });
        }
        auto keyAt = [keys] (float lx, float ly) -> int
        {
            if (ly < 40) for (auto& k : *keys) if (k.black && lx >= k.x && lx < k.x + k.w) return k.n;
            for (auto& k : *keys) if (! k.black && lx >= k.x && lx < k.x + k.w) return k.n;
            return -1;
        };
        auto& a = g.dragArea ({ 0, 0, 1260, 66 }, Cursor::PointingHandCursor);
        a.down = [dev, keyAt] (float lx, float ly) -> std::optional<Gesture>
        {
            auto held = std::make_shared<int> (keyAt (lx, ly));
            if (*held >= 0) dev->note (*held, true);
            Gesture gs;
            gs.move = [dev, keyAt, held] (float x, float y) { const int q = keyAt (x, y); if (q >= 0 && q != *held) { if (*held >= 0) dev->note (*held, false); *held = q; dev->note (q, true); } };
            gs.up = [dev, held] { if (*held >= 0) dev->note (*held, false); };
            return gs;
        };
        g.draw = [dev, keys] (juce::Graphics& gr, double, float, const Colour* c)
        {
            std::set<int> down;
            if (auto* n = dev->live()) for (auto& v : n->views) if (v.pitch.load() >= 0 && v.released.load() < 0) down.insert (v.pitch.load());
            for (auto& k : *keys)
                if (down.count (k.n)) { gr.setColour (rgba (c[k.black ? 1 : 0], 0.3f)); gr.fillRect (k.x + 2, 2.0f, k.w - 4, (k.black ? 40.0f : 66.0f) - 4); }
            TubeOptions frame; frame.closed = true; frame.w = 0.9f; frame.clips = 210;
            tube (gr, roundRect (0, 0, 1260, 66, 7), c[0], frame);
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
    wall.wires = { { { 580, 330, 620, 346, 632, 300, 668, 304 } }, { { 900, 520, 900, 560, 930, 570, 940, 596 } } };
}

//==============================================================================
// Sampler

void samplerWall (Builder& b)
{
    using namespace Param::Sampler;
    auto& d = b.d;
    auto* dev = &d;
    auto& wall = b.wall;
    auto* w = &wall;
    b.out.pal[0] = C::PK; b.out.pal[1] = C::YL; b.out.pal[2] = C::BL;
    b.out.parts = { C::PK, C::PK, C::OR, C::YL, C::BL, C::CY, C::VI };
    enum { wave = 1, pads, pitch, env, col, out };

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

    b.title ("Sampler", 400, [sample, seconds]
    {
        if (*sample == nullptr) return String ("no sample yet   load one");
        return (*sample)->fileName.toLowerCase() + " · " + String (seconds(), 2) + " s";
    });
    {
        auto& tg = *wall.groups.back();
        auto& load = tg.toggle ("load audio", 400, 74, [] { return true; }, [dev] (bool) { if (dev->loadAudio) dev->loadAudio(); });
        load.isNew = true;
        for (auto& it : tg.items) if (it->opts.size() == 1 && it->opts[0].second == "play") it->x = 530;
    }

    const auto pctF = [] (float v) { return F::pct (v); };
    const float WW = 1520;
    auto hover = std::make_shared<int> (-1);

    // The waveform: start and end flags, slice markers, the loop, playheads
    {
        auto& g = wall.add (wave, 40, 176, 0, 760, 180, 760);
        b.choice (g, Mode, { "Classic", "One-shot", "Slice", "Loop" }, 0, 18, true);
        auto& sl = b.choice (g, Slicer, { "Transient", "Grid", "Manual" }, 470, 18, true);
        sl.ci = 1;
        sl.visible = [dev] { return dev->choice (Mode) == Slice; };
        sl.setChoice = [dev, w, markers, setManual] (int v)
        {
            if (v == Manual && dev->get (Marker0) < 0.0f) setManual (markers());
            dev->setRaw (Slicer, (float) v);
            static const char* names[] = { "transient", "grid", "manual" };
            w->say (String ("slice by ") + names[juce::jlimit (0, 2, v)]);
        };
        auto& gr = b.choice (g, Grid, { "4", "8", "16" }, 790, 18);
        gr.ci = 1;
        gr.visible = [dev] { return dev->choice (Mode) == Slice && dev->choice (Slicer) == GridSlices; };
        b.toggle (g, Reverse, "reverse", 920, 18, true).ci = 2;
        b.fader (g, Sens, "Sensitivity", pctF, 1100, 16, 1250, 16, P (1175, 6), true).visible = [dev] { return dev->choice (Mode) == Slice && dev->choice (Slicer) == Transient; };
        b.fader (g, XFade, "Crossfade", [] (float v) { return String (juce::roundToInt (v * 120)) + " ms"; }, 1340, 16, 1490, 16, P (1415, 26), true).visible = [dev] { return dev->choice (Mode) == Loop; };

        // What lies under the pointer: a flag, a loop end or a marker
        struct HitResult { char kind = 0; int index = -1; };
        auto hit = [dev, sEnd, markers, WW] (float n) -> HitResult
        {
            const float a = dev->get (Start), e = sEnd();
            auto px = [WW] (float x, float y) { return std::abs (x - y) * WW; };
            if (dev->choice (Mode) == Loop)
            {
                const float la = a + (e - a) * dev->get (LoopA), lb = a + (e - a) * dev->get (LoopB);
                if (px (la, n) < 9) return { 'l' };
                if (px (lb, n) < 9) return { 'L' };
            }
            if (dev->choice (Mode) == Slice)
            {
                const auto m = markers();
                for (size_t i = 0; i < m.size(); ++i) if (px (m[i], n) < 8) return { 'm', (int) i };
            }
            if (px (a, n) < 10) return { 'a' };
            if (px (e, n) < 10) return { 'b' };
            return {};
        };
        auto& area = g.dragArea ({ -8, 34, WW + 16, 296 }, Cursor::CrosshairCursor);
        area.hover = [hit, hover, WW] (float lx, float)
        {
            const auto h = hit (clamp01 (lx / WW));
            *hover = h.kind == 'm' ? h.index : -1;
            return h.kind != 0 ? Cursor::LeftRightResizeCursor : Cursor::CrosshairCursor;
        };
        area.tip = [dev, sEnd, seconds, slices]
        {
            String s = String ((sEnd() - dev->get (Start)) * seconds(), 2) + " s in play";
            if (dev->choice (Mode) == Slice) s += "   " + String ((int) slices().size()) + " slices";
            return s;
        };
        area.doubleClick = [dev, markers, setManual, WW] (float lx, float)
        {
            if (dev->choice (Mode) != Slice) return;
            const float n = clamp01 (lx / WW);
            auto m = markers();
            auto it = std::find_if (m.begin(), m.end(), [n, WW] (float x) { return std::abs (x - n) * WW < 9; });
            if (it != m.end()) m.erase (it);
            else if ((int) m.size() < maxMarkers) m.push_back (n);
            setManual (m);
            dev->setRaw (Slicer, (float) Manual);
        };
        area.down = [dev, w, hit, sEnd, slices, markers, manual, setManual, WW] (float lx, float) -> std::optional<Gesture>
        {
            const float n = clamp01 (lx / WW);
            const auto h = hit (n);
            Gesture gs;
            if (h.kind == 0)
            {
                // Play what was clicked: its slice, or the sample from its root key
                auto played = std::make_shared<int> (-1);
                if (dev->choice (Mode) == Slice)
                {
                    const auto sl = slices();
                    for (size_t i = 0; i < sl.size(); ++i) if (n >= sl[i].first && n < sl[i].second) *played = 36 + (int) i;
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
                    w->say ("slicing switched to manual so your marker stays where you put it");
                }
                *held = m[(size_t) h.index];
            }
            const char kind = h.kind;
            gs.move = [dev, kind, held, sEnd, manual, setManual, WW] (float x, float)
            {
                const float v = clamp01 (x / WW), a = dev->get (Start), e = sEnd();
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
        g.draw = [dev, sEnd, slices, markers, peakAt, hover, WW] (juce::Graphics& gr, double, float, const Colour* c)
        {
            const float top = 44, bot = 318, mid = 181, amp = 124, a = dev->get (Start), e = sEnd();
            const float step = look().style == 2 ? 6.0f : 5.0f;
            const int nb = (int) std::floor (WW / step);
            const bool rev = dev->on (Reverse);
            const int mode = dev->choice (Mode);
            auto X = [WW] (float n) { return n * WW; };
            Lines lit, dark;
            for (int i = 0; i <= nb; ++i)
            {
                const float n = (float) i / (float) nb, hh = juce::jmax (1.5f, peakAt (rev ? 1 - n : n) * amp);
                (n >= a && n <= e ? lit : dark).push_back ({ { X (n), mid - hh }, { X (n), mid + hh } });
            }
            if (! dark.empty()) { TubeOptions o; o.off = true; o.w = 0.8f; tube (gr, dark, c[0], o); }
            const auto sl = slices();
            const auto* node = dev->live();
            if (mode == Slice && node != nullptr)
                for (auto& v : node->views)
                    if (v.pitch.load() >= 0)
                        if (const int si = v.slice.load(); si >= 0 && si < (int) sl.size())
                        {
                            gr.setColour (rgba (c[1], 0.06f * v.level.load() + 0.03f));
                            gr.fillRect (X (sl[(size_t) si].first), top, X (sl[(size_t) si].second) - X (sl[(size_t) si].first), bot - top);
                        }
            if (! lit.empty()) { TubeOptions o; o.w = 0.85f; o.step = 6; tube (gr, lit, c[0], o); }
            if (mode == Slice)
            {
                const auto m = markers();
                for (size_t i = 0; i < m.size(); ++i)
                {
                    const float x = X (m[i]);
                    TubeOptions o; o.dashOn = 3; o.dashOff = 6; o.w = *hover == (int) i ? 1.15f : 0.8f;
                    tube (gr, Line { { x, top + 6 }, { x, bot } }, c[1], o);
                    TubeOptions f; f.w = 0.7f; f.style = 0;
                    tube (gr, Line { { x, top }, { x + 10, top + 6 }, { x, top + 12 } }, c[1], f);
                    text (gr, String ((int) i + 2), x + 15, top + 12, 17, c[1], 0.8f);
                }
                text (gr, "1", X (a) + 15, top + 12, 17, c[1], 0.8f);
            }
            if (mode == Loop)
            {
                const float la = X (a + (e - a) * dev->get (LoopA)), lb = X (a + (e - a) * dev->get (LoopB));
                gr.setColour (rgba (c[2], 0.06f)); gr.fillRect (la, top, lb - la, bot - top);
                TubeOptions o; o.w = 0.9f; o.style = 0;
                tube (gr, Line { { la, top + 22 }, { la, top + 4 }, { lb, top + 4 }, { lb, top + 22 } }, c[2], o);
                text (gr, "loop", (la + lb) / 2, top + 26, 18, c[2], 0.8f, Align::centre);
            }
            for (auto [n, s] : { std::pair<float, float> { a, 1.0f }, { e, -1.0f } })
            {
                const float x = X (n);
                TubeOptions o; o.w = 1.2f; o.style = 0; tube (gr, Line { { x, top - 4 }, { x, bot + 6 } }, c[2], o);
                TubeOptions f; f.w = 0.7f; f.style = 0; tube (gr, Line { { x, bot + 6 }, { x + 11 * s, bot + 6 }, { x + 11 * s, bot - 6 }, { x, bot - 6 } }, c[2], f);
            }
            if (node != nullptr)
                for (auto& v : node->views)
                {
                    if (v.pitch.load() < 0) continue;
                    const float x = X (v.pos.load()), lv = juce::jlimit (0.0f, 1.0f, v.level.load() + 0.2f);
                    gr.setColour (juce::Colours::white.withAlpha (lv * 0.25f)); gr.drawLine (x, top, x, bot, 7.0f);
                    gr.setColour (juce::Colours::white.withAlpha (lv)); gr.drawLine (x, top, x, bot, 1.6f);
                }
        };
        auto secs = seconds;      // the time ruler under the wave, drawn after it
        g.draw = [old = g.draw, secs, WW] (juce::Graphics& gr, double t, float dt, const Colour* c)
        {
            old (gr, t, dt, c);
            const float sec = secs();
            const int n = sec < 12 ? juce::roundToInt (sec * 2) : 12;
            for (int i = 1; i < n; ++i) label (gr, String (sec * (float) i / (float) n, sec < 12 ? 1 : 0) + "s", (float) i / (float) n * WW, 318 + 18, Align::centre);
        };
    }

    // Pads: sixteen slices (or sixteen notes up from the root)
    {
        auto& g = wall.add (pads, 52, 556, -1.5f, 185, 150, 300);
        const float PW = 84, PH = 64, GP = 10;
        auto padLevel = std::make_shared<std::array<float, 16>>();
        padLevel->fill (0.0f);
        auto at = [=] (float lx, float ly)
        {
            const int cx = (int) std::floor (lx / (PW + GP)), cy = (int) std::floor (ly / (PH + GP));
            return cx >= 0 && cx < 4 && cy >= 0 && cy < 4 && lx - (float) cx * (PW + GP) < PW && ly - (float) cy * (PH + GP) < PH ? cy * 4 + cx : -1;
        };
        auto padNote = [dev] (int i) { return dev->choice (Mode) == Slice ? 36 + i : rootNote (dev->get (Root)) - 12 + i; };
        auto& a = g.dragArea ({ 0, 0, 4 * PW + 3 * GP, 4 * PH + 3 * GP }, Cursor::PointingHandCursor);
        a.down = [dev, at, padLevel, padNote] (float lx, float ly) -> std::optional<Gesture>
        {
            const int i = at (lx, ly);
            auto played = std::make_shared<int> (i >= 0 ? padNote (i) : -1);
            if (i >= 0) { dev->note (*played, true, 0.9f); (*padLevel)[(size_t) i] = 1.0f; }
            Gesture gs;
            gs.up = [dev, played] { if (*played >= 0) dev->note (*played, false); };
            return gs;
        };
        g.draw = [=] (juce::Graphics& gr, double, float dt, const Colour* c)
        {
            const auto sl = slices();
            const bool sm = dev->choice (Mode) == Slice;
            const int root = rootNote (dev->get (Root));
            static const char* keymap = "awsedftgyhujkolp";
            Lines on, off;
            for (int i = 0; i < 16; ++i)
            {
                auto& lv = (*padLevel)[(size_t) i];
                lv *= std::pow (0.9f, dt * 30.0f);      // tuned at 30 frames a second
                const float x = (float) (i % 4) * (PW + GP), y = (float) (i / 4) * (PH + GP);
                const bool has = sm ? i < (int) sl.size() : true;
                (has ? on : off).push_back (roundRect (x, y, PW, PH, 9));
                if (lv > 0.03f) { gr.setColour (rgba (c[1], 0.4f * lv)); gr.fillRoundedRectangle (x + 2, y + 2, PW - 4, PH - 4, 8); }
                if (has)
                {
                    const auto s = sm ? sl[(size_t) i] : sl[0];
                    juce::Path p;
                    for (int j = 0; j < 18; ++j)
                    {
                        const float n = s.first + (s.second - s.first) * (float) j / 18, pk = juce::jmax (1.0f, peakAt (n) * 17);
                        p.startNewSubPath (x + 11 + (float) j * 3.6f, y + 38 - pk); p.lineTo (x + 11 + (float) j * 3.6f, y + 38 + pk);
                    }
                    gr.setColour (mix (c[0], C::white, 0.2f + 0.6f * lv).withMultipliedAlpha (0.55f + 0.45f * lv));
                    gr.strokePath (p, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                }
                const String name = sm ? (has ? String (i + 1) : String()) : F::note (root - 12 + i).toLowerCase();
                label (gr, name + "  " + String::charToString ((juce::juce_wchar) keymap[i]), x + 8, y + 15, Align::left, has ? Colour (0xffb9b8b2) : Colour (0xff55575e));
            }
            if (! off.empty()) { TubeOptions o; o.off = true; o.closed = true; o.w = 0.8f; tube (gr, off, c[0], o); }
            if (! on.empty()) { TubeOptions o; o.closed = true; o.w = 0.75f; o.style = look().style == 1 ? 1 : 0; tube (gr, on, c[0], o); }
        };
    }

    // Pitch: two tape reels that turn with the playback speed
    {
        auto& g = wall.add (pitch, 478, 566, 1, 150, 110, 250);
        const auto pst = [] (float v) { return F::signedVal ((float) juce::roundToInt ((v - 0.5f) * 48), " st", 0); };
        auto& a = g.dragArea ({ 0, 0, 300, 170 }, Cursor::UpDownResizeCursor);
        a.start = [dev] { return std::vector<float> { dev->get (Pitch) }; };
        a.drag = [dev] (float, float dy, const std::vector<float>& s) { dev->set (Pitch, s[0] - dy / 380); };
        a.tip = [dev, pst] { return "pitch   " + pst (dev->get (Pitch)) + "   " + String (std::pow (2.0f, (dev->get (Pitch) - 0.5f) * 4), 2) + "x speed"; };
        b.fader (g, Fine, "Fine", [] (float v) { return F::signedVal ((float) juce::roundToInt ((v - 0.5f) * 100), " ct", 0); }, 0, 214, 130, 214, P (65, 204), true, true);
        b.fader (g, Root, "Root", [] (float v) { return F::note (rootNote (v)); }, 170, 214, 300, 214, P (235, 224), true);
        b.choice (g, Warp, { "Repitch", "Stretch" }, 0, 262, true);
        auto angle = std::make_shared<float> (0.0f);
        g.draw = [dev, angle, pst] (juce::Graphics& gr, double, float dt, const Colour* c)
        {
            const float rate = std::pow (2.0f, ((dev->get (Pitch) - 0.5f) * 48 + (dev->get (Fine) - 0.5f)) / 12);
            const bool play = dev->live() != nullptr && dev->live()->display.load() > 0.5f;
            const bool stretch = dev->choice (Warp) == 1;
            if (play) *angle += dt * (stretch ? 1.0f : rate) * 3.2f * (dev->on (Reverse) ? -1.0f : 1.0f);
            const float r = 62, y = 76, xa = 70, xb = 230;
            for (float x : { xa, xb })
            {
                TubeOptions o; o.w = 0.95f; tube (gr, circle (x, y, r), c[0], o);
                Lines spokes;
                for (int s = 0; s < 3; ++s)
                {
                    const float an = *angle + (float) s * 2.094f;
                    spokes.push_back ({ { x + std::cos (an) * r * 0.24f, y + std::sin (an) * r * 0.24f }, { x + std::cos (an) * r * 0.8f, y + std::sin (an) * r * 0.8f } });
                }
                TubeOptions so; so.w = 0.8f; so.style = 0; tube (gr, spokes, c[1], so);
                bulb (gr, x, y, c[1], play ? 1.0f : 0.3f, 3);
            }
            Line tp;
            for (int i = 0; i <= 40; ++i)
            {
                const float x = xa + (xb - xa) * (float) i / 40;
                tp.push_back ({ x, y + r + 9 + (stretch ? std::sin ((float) i * 0.9f + *angle * 2) * 3.5f * juce::jmax (0.3f, std::abs (std::log2 (rate))) : 0.0f) });
            }
            TubeOptions to; to.w = 0.8f; to.style = 0; tube (gr, tp, c[2], to);
            text (gr, pst (dev->get (Pitch)), 150, 174, 20, c[0], 0.9f, Align::centre);
        };
    }

    // Envelope, with a hold stage
    {
        auto& g = wall.add (env, 826, 566, -1.2f, 150, 90, 240);
        auto* gp = &g;
        auto envNow = [dev] { return AnalogEnv (expMap (dev->get (Attack), 0.001f, 2), expMap (dev->get (Decay), 0.01f, 6), dev->get (Sustain), expMap (dev->get (Release), 0.005f, 4), dev->get (Hold) * 0.5f); };
        auto st = [dev] { return std::vector<float> { dev->get (Attack), dev->get (Hold), dev->get (Decay), dev->get (Sustain), dev->get (Release) }; };
        auto at = [gp] (int xi, float yv, float dx = 0, float dy = 0) -> std::optional<P>
        {
            auto& s = gp->scratch["m"];
            if (s.size() < 8) return std::nullopt;
            return P (s[(size_t) xi] + dx, s[6] + s[7] - yv * s[7] + dy);
        };
        const auto pA = [] (float v) { return F::time (expMap (v, 0.001f, 2)); };
        const auto pD = [] (float v) { return F::time (expMap (v, 0.01f, 6)); };
        const auto pR = [] (float v) { return F::time (expMap (v, 0.005f, 4)); };
        const auto hold = [] (float v) { return String (juce::roundToInt (v * 500)) + " ms"; };
        auto& h1 = g.handle ([at] { return at (1, 1); });
        h1.start = st; h1.drag = [dev] (float dx, float, const std::vector<float>& s) { dev->set (Attack, s[0] + dx / 150); };
        h1.tip = [dev, pA] { return tipFor (*dev, Attack, "Attack", pA); };
        auto& h2 = g.handle ([at] { return at (2, 1, 14, -12); });
        h2.ci = 1; h2.r = 4.5f; h2.start = st; h2.drag = [dev] (float dx, float, const std::vector<float>& s) { dev->set (Hold, s[1] + dx / 150); };
        h2.tip = [dev, hold] { return tipFor (*dev, Hold, "Hold", hold); };
        auto& h3 = g.handle ([at, dev] { return at (3, dev->get (Sustain)); });
        h3.start = st; h3.drag = [dev] (float dx, float dy, const std::vector<float>& s) { dev->set (Decay, s[2] + dx / 150); dev->set (Sustain, s[3] - dy / 150); };
        h3.tip = [dev, pD] { return tipFor (*dev, Decay, "Decay", pD) + "   " + tipFor (*dev, Sustain, "Sustain", F::pct); };
        auto& h4 = g.handle ([at] { return at (5, 0); });
        h4.start = st; h4.drag = [dev] (float dx, float, const std::vector<float>& s) { dev->set (Release, s[4] + dx / 150); };
        h4.tip = [dev, pR] { return tipFor (*dev, Release, "Release", pR); };
        g.draw = [dev, gp, envNow] (juce::Graphics& gr, double, float, const Colour* c)
        {
            const auto e = envNow();
            const auto m = drawEnv (gr, e, 0, 24, 300, 150, c[0]);
            gp->scratch["m"] = { m.xs[0], m.xs[1], m.xs[2], m.xs[3], m.xs[4], m.xs[5], m.y0, m.h };
            text (gr, "shape", 0, 214, 22, c[0], 0.7f);
            ringMark (gr, m.xs[2] + 14, m.Y (1) - 26, c[1]);
            if (auto* n = dev->live())
                for (auto& v : n->views)
                    if (v.pitch.load() >= 0) { const auto q = m.at (e, v.age.load(), v.released.load()); bulb (gr, q.x, q.y, C::white, 1, 3); }
        };
    }

    // Colour: filter, drive, tape and crush
    {
        auto& g = wall.add (col, 1168, 548, 0.8f, 200, 100, 300);
        b.choice (g, FMode, { "LP", "BP", "HP" }, 0, 16, true);
        auto* gp = &g;
        auto& h = g.handle ([gp]() -> std::optional<P> { auto& s = gp->scratch["f"]; if (s.size() < 2) return std::nullopt; return P (s[0], s[1]); });
        h.r = 6; h.ci = 1;
        h.start = [dev] { return std::vector<float> { dev->get (Cutoff), dev->get (Res) }; };
        h.drag = [dev] (float dx, float dy, const std::vector<float>& s) { dev->set (Cutoff, s[0] + dx / 250 * 1.153f); dev->set (Res, s[1] - dy / 110); };
        h.tip = [dev] { return "cutoff " + F::hz (expMap (dev->get (Cutoff), 40, 16000)).toLowerCase() + "   res " + F::pct (dev->get (Res)); };
        auto& dr = g.dragArea ({ 282, 26, 130, 130 }, Cursor::UpDownResizeCursor);
        dr.start = [dev] { return std::vector<float> { dev->get (Drive) }; };
        dr.drag = [dev] (float, float dy, const std::vector<float>& s) { dev->set (Drive, s[0] - dy / 130); };
        dr.tip = [dev] { return "drive   " + F::pct (dev->get (Drive)); };
        b.fader (g, Tape, "Tape", pctF, 0, 206, 180, 206, P (90, 196), true);
        b.fader (g, Crush, "Crush", [] (float v) { return v < 0.02f ? String ("off") : String (16 - juce::roundToInt (v * 12)) + " bit"; }, 225, 206, 405, 206, P (315, 216), true);
        g.draw = [dev, gp] (juce::Graphics& gr, double, float, const Colour* c)
        {
            const float fc = expMap (dev->get (Cutoff), 40, 16000);
            static const int modes[] = { 4, 2, 3 };
            const auto fp = drawFilter (gr, 0, 26, 250, 134, fc, fc, dev->get (Res), modes[juce::jlimit (0, 2, dev->choice (FMode))], c);
            gp->scratch["f"] = { fp.bx, fp.by };
            const float bx = 286, bw = 122, by = 30, bh = 122;
            const float k = 1 + dev->get (Drive) * 6, tp = dev->get (Tape), cr = dev->get (Crush);
            const float bits = cr < 0.02f ? 0.0f : std::pow (2.0f, 16.0f - (float) juce::roundToInt (cr * 12) - 1);
            auto fn = [=] (float x)
            {
                float y = std::tanh (x * k) / std::tanh (k);
                y = y * (1 - tp * 0.25f) + tp * 0.25f * (y - 0.35f * y * y * (y > 0 ? 1.0f : -1.0f) * 0.6f + 0.08f * (1 - y * y));
                y -= tp * 0.02f;
                if (bits > 0 && bits < 4096) { const float q = juce::jmin (bits, 64.0f); y = std::round (y * q) / q; }
                return y;
            };
            juce::Path diag; diag.startNewSubPath (bx, by + bh); diag.lineTo (bx + bw, by);
            dashes (gr, diag);
            Line pts;
            for (int i = 0; i <= 80; ++i) pts.push_back ({ bx + (float) i / 80 * bw, by + bh / 2 - juce::jlimit (-1.0f, 1.0f, fn ((float) i / 40 - 1)) * bh / 2 });
            TubeOptions o; o.w = 0.9f; o.style = look().style == 1 ? 1 : 0; o.step = 5;
            tube (gr, pts, c[1], o);
            const float wd = text (gr, "drive", bx + 34, by + bh + 18, 17, c[1], 0.55f);
            ringMark (gr, bx + 41 + wd, by + bh + 13, c[1]);
        };
    }

    // Out: pan, gain, velocity and how many voices
    {
        auto& g = wall.add (out, 1168, 800, -0.5f, 200, 50, 260);
        b.fader (g, Pan, "Pan", [] (float v) { const int p = juce::roundToInt ((v - 0.5f) * 200); return p == 0 ? String ("centre") : (p < 0 ? "L " : "R ") + String (std::abs (p)); }, 238, 14, 408, 14, {}, true, true).bulbs = 9;
        b.fader (g, Gain, "Gain", [] (float v) { return F::db (v); }, 238, 52, 408, 52);
        b.fader (g, VelGain, "Vel gain", pctF, 238, 90, 408, 90, {}, true);
        auto& v = b.choice (g, Voices, { "1", "4", "8", "16" }, 0, 106, true);
        v.pre = "voices"; v.size = 18;
        g.draw = [dev] (juce::Graphics& gr, double, float, const Colour* c) { scope (gr, dev->live(), c[0], 0, 0, 205, 78); };
    }
    wall.wires = { { { 420, 620, 450, 620, 450, 640, 478, 640 } }, { { 780, 650, 800, 650, 800, 640, 826, 640 } }, { { 1130, 650, 1150, 650, 1150, 640, 1168, 640 } } };
    juce::ignoreUnused (w);
}

//==============================================================================

bool hasView (ModuleType t)
{
    switch (t)
    {
        case ModuleType::Synth: case ModuleType::Sampler: case ModuleType::Drums: case ModuleType::Filter: case ModuleType::Drive:
        case ModuleType::Delay: case ModuleType::Reverb: case ModuleType::EQ: case ModuleType::Compressor: case ModuleType::LFO:
        case ModuleType::GasTube: case ModuleType::CrateDigger: case ModuleType::StringWall:
        case ModuleType::GhostTag: case ModuleType::Crowd: case ModuleType::Transformer: case ModuleType::DrumSeq:
            return true;
        case ModuleType::Input: case ModuleType::Output: case ModuleType::Plugin: case ModuleType::Attenuator: case ModuleType::Mixer:
        case ModuleType::Inverter: case ModuleType::Crossfade: case ModuleType::Slew:
            break;
    }
    return false;
}

std::unique_ptr<Built> build (Device& d)
{
    auto made = std::make_unique<Built>();
    auto& out = *made;
    out.wall = std::make_unique<Wall>();
    auto* o = &out;
    out.wall->colours = [o] (int mod) { return o->colours (mod); };
    Builder b { d, out, *out.wall };
    switch (d.type())
    {
        case ModuleType::Synth:      synthWall (b); break;
        case ModuleType::Sampler:    samplerWall (b); break;
        case ModuleType::Drums:      drumsWall (b); break;
        case ModuleType::Filter:     filterWall (b); break;
        case ModuleType::Drive:      driveWall (b); break;
        case ModuleType::Delay:      delayWall (b); break;
        case ModuleType::Reverb:     reverbWall (b); break;
        case ModuleType::EQ:         eqWall (b); break;
        case ModuleType::Compressor: compWall (b); break;
        case ModuleType::LFO:        lfoWall (b); break;
        case ModuleType::GasTube:    gasWall (b); break;
        case ModuleType::CrateDigger: crateWall (b); break;
        case ModuleType::StringWall: stringsWall (b); break;
        case ModuleType::GhostTag:   ghostWall (b); break;
        case ModuleType::Crowd:      crowdWall (b); break;
        case ModuleType::Transformer: xfmrWall (b); break;
        case ModuleType::DrumSeq:    drumSeqWall (b); break;
        case ModuleType::Input: case ModuleType::Output: case ModuleType::Plugin: case ModuleType::Attenuator: case ModuleType::Mixer:
        case ModuleType::Inverter: case ModuleType::Crossfade: case ModuleType::Slew:
            break;
    }
    if (auto* m = d.module()) out.name = moduleInfo (m->type).title;
    return made;
}
}
