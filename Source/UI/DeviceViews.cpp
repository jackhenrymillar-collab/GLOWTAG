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
