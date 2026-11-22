#include "DeviceCards.h"
#include "DeviceViewsCommon.h"
#include "../Engine/Samples.h"

namespace DeviceCards
{
using namespace Neon;
using DeviceViews::Device;
using DeviceViews::Builder;
using juce::Colour;
using juce::String;
using P = juce::Point<float>;
namespace F = DeviceViews::F;
namespace C = DeviceViews::C;
using Cursor = juce::MouseCursor::StandardCursorType;

namespace
{
    constexpr float pi = juce::MathConstants<float>::pi;

    Line arc (float cx, float cy, float r, float a0, float a1, int n = 30)
    {
        Line l;
        for (int i = 0; i <= n; ++i)
        {
            const float a = a0 + (a1 - a0) * (float) i / (float) n;
            l.push_back ({ cx + std::cos (a) * r, cy + std::sin (a) * r });
        }
        return l;
    }

    TubeOptions w (float width, bool off = false)
    {
        TubeOptions o;
        o.w = width;
        o.off = off;
        o.style = 0;
        return o;
    }

    float clampf (float v) { return juce::jlimit (0.0f, 1.0f, v); }
}

//==============================================================================
// The small neon pieces

void art (juce::Graphics& g, ModuleType type, float x, float y, const Colour* c, double tt)
{
    const float t = (float) tt;
    switch (type)
    {
        case ModuleType::Sampler:
        {
            tube (g, roundRect (x, y, 64, 46, 6), c[0], w (0.75f));
            Line wave;
            for (int i = 0; i <= 40; ++i) { const float u = (float) i / 40.0f; wave.push_back ({ x + 6 + u * 52, y + 23 - std::sin (u * 44) * 14 * std::exp (-u * 2.6f) }); }
            tube (g, wave, c[1], w (0.7f));
            break;
        }
        case ModuleType::Synth:
        {
            Line s;
            for (int i = 0; i <= 48; ++i) { const float u = (float) i / 48.0f; s.push_back ({ x + u * 64, y + 22 - std::sin (u * pi * 4 + t * 2) * 13 * (1 - u * 0.4f) }); }
            tube (g, s, c[0], w (0.8f));
            tube (g, Line { { x, y + 44 }, { x + 16, y + 32 }, { x + 16, y + 44 }, { x + 32, y + 32 }, { x + 32, y + 44 } }, c[1], w (0.6f));
            break;
        }
        case ModuleType::Drums:
            for (int i = 0; i < 4; ++i)
                tube (g, roundRect (x + (float) (i % 2) * 34, y + (float) (i / 2) * 25, 28, 20, 4), c[i % 3], w (0.65f));
            break;
        case ModuleType::Filter:
            for (int i = 0; i < 5; ++i) tube (g, Line { { x + 8, y + 4 + (float) i * 9 }, { x + 40, y + 4 + (float) i * 9 } }, c[0], w (0.6f));
            tube (g, Lines { Line { { x + 8, y }, { x + 8, y + 44 } }, Line { { x + 40, y }, { x + 40, y + 44 } } }, c[1], w (0.6f));
            tube (g, circle (x + 54, y + 12, 9, 20), c[2], w (0.6f));
            break;
        case ModuleType::Drive:
        {
            Line valve { { x + 14, y + 46 }, { x + 14, y + 14 } };
            for (auto p : arc (x + 30, y + 14, 16, pi, 2 * pi, 24)) valve.push_back (p);
            valve.push_back ({ x + 46, y + 46 });
            tube (g, valve, c[0], w (0.75f));
            Line fil;
            for (int i = 0; i <= 12; ++i) fil.push_back ({ x + 21 + (float) i * 1.4f, y + 38 - (i % 2 ? 9.0f : 0.0f) - (float) i * 0.6f });
            tube (g, fil, c[1], w (0.6f));
            break;
        }
        case ModuleType::Delay:
            for (float cx : { x + 14.0f, x + 50.0f })
            {
                tube (g, circle (cx, y + 16, 12, 24), c[0], w (0.7f));
                tube (g, Line { { cx, y + 16 }, { cx + std::cos (t * 2.5f) * 10, y + 16 + std::sin (t * 2.5f) * 10 } }, c[1], w (0.55f));
            }
            tube (g, Line { { x + 2, y + 40 }, { x + 62, y + 40 } }, c[2], w (0.6f));
            break;
        case ModuleType::Reverb:
            for (int i = 0; i < 3; ++i) tube (g, arc (x + 32, y + 42, 10.0f + (float) i * 11, pi * 1.05f, pi * 1.95f, 24), c[i % 3], w (0.65f));
            break;
        case ModuleType::EQ:
        {
            tube (g, Line { { x, y + 4 }, { x + 64, y + 4 } }, c[2], w (0.55f));
            const float dx[] = { 10, 32, 54 }, len[] = { 18, 26, 14 };
            for (int i = 0; i < 3; ++i)
            {
                tube (g, Line { { x + dx[i], y + 4 }, { x + dx[i], y + 4 + len[i] } }, c[1], w (0.5f));
                bulb (g, x + dx[i], y + 8 + len[i], c[0], 1.0f, 3.4f);
            }
            break;
        }
        case ModuleType::Compressor:
        {
            tube (g, arc (x + 32, y + 42, 26, pi, 2 * pi), c[0], w (0.7f));
            const float a = pi * (1.25f + 0.25f * std::sin (t * 1.7f));
            tube (g, Line { { x + 32, y + 42 }, { x + 32 + std::cos (a) * 22, y + 42 + std::sin (a) * 22 } }, c[1], w (0.6f));
            break;
        }
        case ModuleType::LFO:
        {
            tube (g, circle (x + 22, y + 22, 18, 30), c[0], w (0.7f));
            const float a = t * 2;
            bulb (g, x + 22 + std::cos (a) * 18, y + 22 + std::sin (a) * 18, c[1], 1.0f, 3.2f);
            Line s;
            for (int i = 0; i <= 20; ++i) s.push_back ({ x + 44 + (float) i, y + 22 - std::sin (a + (float) i * 0.3f) * 10 });
            tube (g, s, c[2], w (0.55f));
            break;
        }
        case ModuleType::GasTube:
            for (int i = 0; i < 4; ++i)
            {
                const float tx = x + 6 + (float) i * 16, flick = 0.75f + 0.25f * std::sin (t * (7.0f + (float) i * 3.1f));
                TubeOptions o = w (0.8f);
                tube (g, roundRect (tx, y + 4, 9, 40, 4.5f), c[i % 3], o);
                bulb (g, tx + 4.5f, y + 24, c[i % 3], flick, 2.0f);
            }
            break;
        case ModuleType::CrateDigger:
        {
            tube (g, circle (x + 24, y + 23, 21, 36), c[0], w (0.75f));
            tube (g, circle (x + 24, y + 23, 6, 16), c[1], w (0.55f));
            const float a = t * 1.4f;
            bulb (g, x + 24 + std::cos (a) * 15, y + 23 + std::sin (a) * 15, c[2], 1.0f, 2.6f);
            tube (g, Line { { x + 58, y + 2 }, { x + 52, y + 30 }, { x + 40, y + 34 } }, c[1], w (0.6f));
            break;
        }
        case ModuleType::StringWall:
            for (int i = 0; i < 5; ++i)
            {
                Line s;
                for (int k = 0; k <= 16; ++k) { const float u = (float) k / 16.0f; s.push_back ({ x + u * 64, y + 6 + (float) i * 9 + std::sin (u * pi) * std::sin (t * 9 + (float) i) * (i == 2 ? 2.5f : 0.6f) }); }
                tube (g, s, c[i % 3], w (0.5f));
            }
            tube (g, Lines { Line { { x + 18, y }, { x + 18, y + 46 } }, Line { { x + 44, y }, { x + 44, y + 46 } } }, c[2], w (0.45f, true));
            break;
        case ModuleType::GhostTag:
            for (int i = 0; i < 3; ++i)
            {
                const float s = 1.0f - (float) i * 0.25f, ox = x + (float) i * 18, oy = y + (float) i * 6;
                Line tag { { ox, oy + 20 * s }, { ox + 8 * s, oy }, { ox + 14 * s, oy + 22 * s }, { ox + 22 * s, oy + 4 * s }, { ox + 26 * s, oy + 18 * s } };
                TubeOptions o = w (0.7f * s);
                tube (g, tag, c[i % 3], o);
            }
            break;
        case ModuleType::Crowd:
            for (int i = 0; i < 4; ++i)
            {
                const float px = x + 8 + (float) i * 16, py = y + 8 + (float) (i % 2) * 6;
                tube (g, circle (px, py, 5, 14), c[i % 3], w (0.55f));
                tube (g, arc (px, py + 26, 10, pi * 1.1f, pi * 1.9f, 12), c[i % 3], w (0.55f));
            }
            break;
        case ModuleType::Transformer:
        {
            tube (g, roundRect (x, y + 6, 64, 36, 3), c[0], w (0.6f));
            tube (g, Line { { x + 8, y + 18 }, { x + 26, y + 18 }, { x + 26, y + 32 }, { x + 46, y + 32 } }, c[1], w (0.5f));
            for (int i = 0; i < 3; ++i) tube (g, Line { { x + 48 + (float) i * 4, y + 12 }, { x + 48 + (float) i * 4, y + 24 } }, c[2], w (0.5f));
            bulb (g, x + 12, y + 32, c[2], 0.6f + 0.4f * std::sin (t * 5), 2.4f);
            break;
        }
        case ModuleType::Plugin:
            tube (g, roundRect (x + 4, y + 4, 56, 38, 8), c[0], w (0.7f));
            tube (g, Lines { Line { { x + 20, y + 42 }, { x + 20, y + 50 } }, Line { { x + 44, y + 42 }, { x + 44, y + 50 } } }, c[1], w (0.6f));
            break;
        case ModuleType::Attenuator: case ModuleType::Mixer: case ModuleType::Inverter:
        case ModuleType::Crossfade: case ModuleType::Slew: case ModuleType::Input: case ModuleType::Output:
            tube (g, Line { { x, y + 14 }, { x + 40, y + 14 } }, c[0], w (0.6f));
            bulb (g, x + 26, y + 14, c[1], 1.0f, 3.0f);
            break;
    }
}

void artScaled (juce::Graphics& g, ModuleType type, float x, float y, float scale, const Colour* c, double t)
{
    // Scale the drawing and the light it gives off together
    auto& gl = glow();
    const auto saved = gl.current;
    const auto tr = juce::AffineTransform::scale (scale).translated (x, y);
    gl.current = tr.followedBy (saved);
    {
        juce::Graphics::ScopedSaveState s (g);
        g.addTransform (tr);
        art (g, type, 0.0f, 0.0f, c, t);
    }
    gl.current = saved;
}

String line (ModuleType t)
{
    switch (t)
    {
        case ModuleType::Sampler:     return "chop, slice and play";
        case ModuleType::Synth:       return "analog voice";
        case ModuleType::Drums:       return "16 pads, step row";
        case ModuleType::Filter:      return "ladder, low to high pass";
        case ModuleType::Drive:       return "tube, tape, fuzz, fold";
        case ModuleType::Delay:       return "tape echoes";
        case ModuleType::Reverb:      return "rooms to halls";
        case ModuleType::EQ:          return "four lamps";
        case ModuleType::Compressor:  return "glue and punch";
        case ModuleType::LFO:         return "moves any control";
        case ModuleType::GasTube:     return "tubes that strike and buzz";
        case ModuleType::CrateDigger: return "slices dug out of a record";
        case ModuleType::StringWall:  return "pluck, bow or feed back";
        case ModuleType::GhostTag:    return "echoes that fade and drift";
        case ModuleType::Crowd:       return "one sound, a crowd singing it";
        case ModuleType::Transformer: return "circuit bending";
        case ModuleType::Plugin:      return "double-click to open";
        case ModuleType::Input: case ModuleType::Output: case ModuleType::Attenuator: case ModuleType::Mixer:
        case ModuleType::Inverter: case ModuleType::Crossfade: case ModuleType::Slew:
            break;
    }
    return {};
}

void palette (ModuleType t, Colour out[3])
{
    auto set = [out] (Colour a, Colour b, Colour c) { out[0] = a; out[1] = b; out[2] = c; };
    switch (t)
    {
        case ModuleType::Sampler:     set (C::PK, C::YL, C::BL); break;
        case ModuleType::Synth:       set (C::VI, C::PK, C::CY); break;
        case ModuleType::Drums:       set (C::OR, C::YL, C::PK); break;
        case ModuleType::Filter:      set (C::CY, C::BL, C::PK); break;
        case ModuleType::Drive:       set (C::OR, C::PK, C::YL); break;
        case ModuleType::Delay:       set (C::BL, C::CY, C::YL); break;
        case ModuleType::Reverb:      set (C::CY, C::VI, C::PK); break;
        case ModuleType::EQ:          set (C::GR, C::YL, C::PK); break;
        case ModuleType::Compressor:  set (C::YL, C::OR, C::CY); break;
        case ModuleType::LFO:         set (C::VI, C::CY, C::PK); break;
        case ModuleType::GasTube:     set (C::OR, C::PK, C::YL); break;
        case ModuleType::CrateDigger: set (C::PK, C::YL, C::CY); break;
        case ModuleType::StringWall:  set (C::CY, C::YL, C::VI); break;
        case ModuleType::GhostTag:    set (C::VI, C::PK, C::CY); break;
        case ModuleType::Crowd:       set (C::YL, C::PK, C::BL); break;
        case ModuleType::Transformer: set (C::GR, C::YL, C::OR); break;
        case ModuleType::Plugin:      set (C::BL, C::VI, C::CY); break;
        case ModuleType::Input: case ModuleType::Output: case ModuleType::Attenuator: case ModuleType::Mixer:
        case ModuleType::Inverter: case ModuleType::Crossfade: case ModuleType::Slew:
            set (C::CY, C::BL, C::VI); break;
    }
}

float width (ModuleType t)
{
    switch (t)
    {
        case ModuleType::Sampler:     return 440;
        case ModuleType::Synth:       return 480;
        case ModuleType::Drums:       return 420;
        case ModuleType::Filter:      return 320;
        case ModuleType::Drive:       return 300;
        case ModuleType::Delay:       return 330;
        case ModuleType::Reverb:      return 310;
        case ModuleType::EQ:          return 350;
        case ModuleType::Compressor:  return 350;
        case ModuleType::LFO:         return 310;
        case ModuleType::GasTube:     return 400;
        case ModuleType::CrateDigger: return 400;
        case ModuleType::StringWall:  return 430;
        case ModuleType::GhostTag:    return 320;
        case ModuleType::Crowd:       return 320;
        case ModuleType::Transformer: return 430;
        case ModuleType::Plugin:      return 260;
        case ModuleType::Input: case ModuleType::Output: return 74;
        case ModuleType::Attenuator:  return 120;
        case ModuleType::Mixer:       return 290;
        case ModuleType::Inverter:    return 110;
        case ModuleType::Crossfade:   return 130;
        case ModuleType::Slew:        return 120;
    }
    return 200;
}

//==============================================================================
// Building a card

namespace
{
    // Body coordinates: (0, 0) is the top-left of the controls, `W` wide, about 170 tall.
    // Rows of faders sit 36 apart; a fader's label is at y and its glass 11 below.
    struct CardBody
    {
        Card& card;
        Builder& b;
        Group& g;
        float W;
        float ox, oy;          // where the body sits on the card
        int knobs;

        std::vector<std::pair<float, float>> cols (int n) const
        {
            const float gap = n > 3 ? 12.0f : 14.0f, cw = (W - gap * (float) (n - 1)) / (float) n;
            std::vector<std::pair<float, float>> out;
            for (int i = 0; i < n; ++i) out.push_back ({ (float) i * (cw + gap), cw });
            return out;
        }

        Item& fader (int param, const String& label, std::function<String (float)> fmt, float x, float y, float len, bool isNew = false, bool bipolar = false)
        {
            auto& it = b.fader (g, param, label, std::move (fmt), x, y + 11, x + len, y + 11, {}, isNew, bipolar);
            note (param, x, y + 11);
            return it;
        }
        // A fader at column `c` of `n`, on row `row` (rows start at `top`)
        Item& at (int n, int c, float top, int row, int param, const String& label, std::function<String (float)> fmt, bool isNew = false, bool bipolar = false)
        {
            const auto cs = cols (n);
            return fader (param, label, std::move (fmt), cs[(size_t) c].first, top + (float) row * 36.0f, cs[(size_t) c].second, isNew, bipolar);
        }
        // A control stored as -1..1 (the EQ's gains and tilt) shown as a centred fader
        Item& signedAt (int n, int c, float top, int row, int param, const String& label)
        {
            auto& it = at (n, c, top, row, param, label, nullptr, false, true);
            auto* dev = &card.dev;
            it.get = [dev, param] { return (dev->get (param) + 1.0f) * 0.5f; };
            it.set = [dev, param] (float v) { dev->setRaw (param, juce::jlimit (-1.0f, 1.0f, v * 2.0f - 1.0f)); };
            it.def = 0.5f;
            it.tip = [dev, param, label] { const float d = dev->get (param) * 12.0f; return label.toLowerCase() + "   " + (d > 0 ? "+" : "") + String (d, 1) + " dB"; };
            return it;
        }
        Item& words (int param, std::vector<String> list, float x, float y, int ci = 0, bool isNew = false)
        {
            for (auto& s : list) s = s.toLowerCase();
            auto& it = b.choice (g, param, list, x, y, isNew);
            it.size = 15.0f; it.gap = 12.0f; it.ci = ci;
            if (param < knobs) note (param, x, y - 5);
            return it;
        }
        // Words choosing a card knob that is stored 0..1 (Drive's type, the LFO's shape)
        Item& knobWords (int param, std::vector<String> list, float x, float y)
        {
            auto& it = words (param, list, x, y);
            auto* dev = &card.dev;
            const int n = (int) list.size() - 1;
            it.getChoice = [dev, param, n] { return juce::roundToInt (dev->get (param) * (float) n); };
            it.setChoice = [dev, param, n] (int v) { dev->set (param, (float) v / (float) n); };
            return it;
        }
        Item& toggle (int param, const String& word, float x, float y, int ci = 2, bool isNew = false)
        {
            auto& it = b.toggle (g, param, word.toLowerCase(), x, y, isNew);
            it.size = 15.0f; it.ci = ci;
            return it;
        }
        void note (int param, float x, float y)
        {
            if (param >= 0 && param < knobs) card.modJack[param] = { ox + x - 9.0f, oy + y };
        }
    };

    String pct (float v) { return F::pct (v); }

    void samplerBody (CardBody& d)
    {
        using namespace Param::Sampler;
        auto* dev = &d.card.dev;
        auto* card = &d.card;
        d.words (Mode, { "classic", "one-shot", "slice", "loop" }, 0, 0);
        d.toggle (Reverse, "reverse", d.W - 62, 0);
        auto& wave = d.g.dragArea ({ 0, 8, d.W, 46 }, Cursor::PointingHandCursor);
        wave.down = [card] (float, float) -> std::optional<Gesture> { if (card->chooseSample) card->chooseSample(); return Gesture {}; };
        wave.tip = [dev] { auto* m = dev->module(); return "click to choose a sample   " + (m != nullptr ? juce::File (m->sampleName).getFileNameWithoutExtension().toLowerCase() : String()); };
        const float W = d.W;
        d.g.draw = [dev, W] (juce::Graphics& g, double, float, const Colour* c)
        {
            const auto data = dev->sample();
            Line pts;
            const float y0 = 8, h = 44;
            for (int i = 0; i <= 120; ++i)
            {
                const float u = (float) i / 120.0f;
                float pk = 0.15f;
                if (data != nullptr && ! data->peaks.empty())
                    pk = data->peaks[(size_t) juce::jlimit (0, (int) data->peaks.size() - 1, (int) (u * (float) data->peaks.size()))];
                pts.push_back ({ u * W, y0 + h * 0.5f - (i % 2 ? 1.0f : -1.0f) * pk * h * 0.45f });
            }
            const float st = dev->get (Start), en = juce::jmin (1.0f, st + juce::jmax (0.01f, dev->get (Length)));
            tube (g, pts, c[0], w (0.55f, true));
            tube (g, part (pts, st, en), c[0], w (0.55f));
            if (dev->choice (Mode) == Slice && data != nullptr)
                for (auto& o : data->onsets)
                {
                    const float u = o.first;
                    if (u <= st || u >= en) continue;
                    TubeOptions dash = w (0.4f);
                    dash.dashOn = 2; dash.dashOff = 4;
                    tube (g, Line { { u * W, y0 + 2 }, { u * W, y0 + h - 2 } }, c[2], dash);
                }
            if (auto* m = dev->module())
                label (g, juce::File (m->sampleName).getFileNameWithoutExtension().toLowerCase(), W, y0 + h + 13, Align::right, Colour (0xff6d7077), 12);
        };
        const float top = 78;
        d.at (3, 0, top, 0, Start, "start", pct);
        d.at (3, 1, top, 0, Length, "length", pct);
        d.at (3, 2, top, 0, Pitch, "pitch", [] (float v) { return F::signedVal ((v - 0.5f) * 48.0f, " st", 1); }, false, true);
        d.at (3, 0, top, 1, Attack, "attack", [] (float v) { return F::time (DeviceViews::expMap (v, 0.001f, 2.0f)); });
        d.at (3, 1, top, 1, Release, "release", [] (float v) { return F::time (DeviceViews::expMap (v, 0.005f, 4.0f)); });
        d.at (3, 2, top, 1, Gain, "gain", pct);
    }

    void synthBody (CardBody& d)
    {
        using namespace Param::Synth;
        auto* dev = &d.card.dev;
        d.words (VMode, { "poly", "mono", "legato" }, 0, 0, 2);
        d.words (FMode, { "lp 24", "lp 12", "bp", "hp" }, d.W - 170, 0, 1);
        const float W = d.W;
        d.g.draw = [dev, W] (juce::Graphics& g, double, float, const Colour* c)
        {
            const float y0 = 12, h = 38, ww = W * 0.46f;
            Line wave;
            for (int i = 0; i <= 80; ++i) { const float u = (float) i / 80.0f; wave.push_back ({ u * ww, y0 + h * 0.5f - DeviceViews::waveShape (dev->get (Wave), u * 2.0f) * h * 0.42f }); }
            tube (g, wave, c[0], w (0.7f));
            const float fx = W * 0.54f, fw = W * 0.46f, fc = DeviceViews::expMap (dev->get (Cutoff), 20, 18000);
            Line f;
            for (int i = 0; i <= 60; ++i)
            {
                const float u = (float) i / 60.0f, hz = 20.0f * std::pow (1000.0f, u);
                const float m = DeviceViews::filterMagnitude (hz, fc, dev->get (Res), juce::jlimit (0, 3, dev->choice (FMode)));
                const float db = juce::jlimit (-30.0f, 12.0f, 20.0f * std::log10 (juce::jmax (1e-5f, m)));
                f.push_back ({ fx + u * fw, y0 + h * 0.35f - db / 30.0f * h * 0.65f });
            }
            tube (g, f, c[1], w (0.7f));
        };
        const float top = 78;
        d.at (4, 0, top, 0, Wave, "wave", [] (float v) { return v < 0.17f ? String ("saw") : v < 0.5f ? String ("square") : v < 0.83f ? String ("triangle") : String ("sine"); });
        d.at (4, 1, top, 0, Sub, "sub", pct);
        d.at (4, 2, top, 0, Detune, "detune", pct);
        d.at (4, 3, top, 0, Drive, "drive", pct);
        d.at (4, 0, top, 1, Cutoff, "cutoff", [] (float v) { return F::hz (DeviceViews::expMap (v, 20, 18000)); });
        d.at (4, 1, top, 1, Res, "res", pct);
        d.at (4, 2, top, 1, Env, "env", pct);
        d.at (4, 3, top, 1, Gain, "gain", pct);
        d.at (4, 0, top, 2, Attack, "attack", [] (float v) { return F::time (DeviceViews::expMap (v, 0.001f, 4.0f)); });
        d.at (4, 1, top, 2, Decay, "decay", [] (float v) { return F::time (DeviceViews::expMap (v, 0.01f, 6.0f)); });
        d.at (4, 2, top, 2, Sustain, "sustain", pct);
        d.at (4, 3, top, 2, Release, "release", [] (float v) { return F::time (DeviceViews::expMap (v, 0.01f, 8.0f)); });
    }

    void drumsBody (CardBody& d)
    {
        using namespace Param::Drums;
        auto* dev = &d.card.dev;
        auto sel = [dev] { return juce::jlimit (0, 15, dev->choice (Selected)); };
        auto padName = [dev] (int i)
        {
            auto* m = dev->module();
            const auto n = m != nullptr && m->padNames[(size_t) i].isNotEmpty() ? m->padNames[(size_t) i] : Samples::defaultPads()[(size_t) i];
            return juce::File (n).getFileNameWithoutExtension().toLowerCase();
        };
        const float pw = 32, ph = 21;
        for (int i = 0; i < 16; ++i)
        {
            const int row = 3 - i / 4, col = i % 4;     // pad 0 bottom-left, like a drum machine
            auto& a = d.g.dragArea ({ (float) col * (pw + 5), -10 + (float) row * (ph + 5), pw, ph }, Cursor::PointingHandCursor);
            a.down = [dev, i] (float, float) -> std::optional<Gesture>
            {
                dev->setRaw (Selected, (float) i);
                dev->note (36 + i, true, 0.9f);
                Gesture gs;
                gs.up = [dev, i] { dev->note (36 + i, false); };
                return gs;
            };
            a.tip = [padName, i] { return "pad " + String (i + 1) + "   " + padName (i); };
        }
        const float rx = 4 * (pw + 5) + 14, rw = d.W - rx;
        // The chosen pad's level and pitch
        {
            const float cw = (rw - 14) / 2;
            auto& lv = d.g.fader (rx, 26 + 11, rx + cw, 26 + 11);
            lv.text = "level";
            lv.def = 0.8f;
            lv.get = [dev, sel] { return dev->get (Level0 + sel()); };
            lv.set = [dev, sel] (float v) { dev->set (Level0 + sel(), v); };
            lv.tip = [dev, sel, padName] { return padName (sel()) + "   level " + F::pct (dev->get (Level0 + sel())); };
            auto& pt = d.g.fader (rx + cw + 14, 26 + 11, rx + rw, 26 + 11);
            pt.text = "pitch";
            pt.bipolar = true;
            pt.def = 0.5f;
            pt.get = [dev, sel] { return dev->get (Pitch0 + sel()); };
            pt.set = [dev, sel] (float v) { dev->set (Pitch0 + sel(), v); };
            pt.tip = [dev, sel, padName] { return padName (sel()) + "   pitch " + F::signedVal ((dev->get (Pitch0 + sel()) - 0.5f) * 24.0f, " st", 1); };
        }
        {
            CardBody r { d.card, d.b, d.g, rw, d.ox + rx, d.oy, d.knobs };
            auto put = [&] (int c, int row, int param, const String& label)
            {
                const float cw = (rw - 14) / 2;
                r.fader (param, label, pct, (float) c * (cw + 14), 64 + (float) row * 36, cw);
            };
            // r's x is relative to rx: shift the items it makes
            const size_t before = d.g.items.size();
            put (0, 0, Tune, "tune"); put (1, 0, Decay, "decay");
            put (0, 1, Snap, "snap"); put (1, 1, Gain, "gain");
            for (size_t i = before; i < d.g.items.size(); ++i)
            {
                auto& it = *d.g.items[i];
                for (auto& p : it.pts) p.x += rx;
                it.lx += rx;
            }
        }
        // The chosen pad's step row, along the bottom
        const float W = d.W, sy = 154;
        for (int i = 0; i < 16; ++i)
        {
            const float bx = 6 + (float) i * ((W - 12) / 15.0f);
            auto& a = d.g.dragArea ({ bx - 8, sy - 9, 16, 18 }, Cursor::PointingHandCursor);
            a.down = [dev, sel, i] (float, float) -> std::optional<Gesture>
            {
                const int p = Steps0 + sel(), mask = juce::roundToInt (dev->get (p));
                dev->setRaw (p, (float) (mask ^ (1 << i)));
                return Gesture {};
            };
            a.tip = [i] { return "step " + String (i + 1); };
        }
        d.g.draw = [dev, sel, padName, rx, W, sy, pw, ph] (juce::Graphics& g, double t, float, const Colour* c)
        {
            const int s = sel();
            auto* node = dev->live();
            for (int i = 0; i < 16; ++i)
            {
                const int row = 3 - i / 4, col = i % 4;
                const bool on = i == s;
                tube (g, roundRect ((float) col * (pw + 5), -10 + (float) row * (ph + 5), pw, ph, 4), c[i % 3], w (0.6f, ! on));
            }
            text (g, padName (s), rx, 4, 19, c[1], 1.0f);
            const int mask = juce::roundToInt (dev->get (Steps0 + s));
            const int step = node != nullptr && dev->ctx.engine.isPlaying() ? (int) std::fmod (dev->ctx.playheadBeats() * 4.0, 16.0) : -1;
            juce::ignoreUnused (t);
            for (int i = 0; i < 16; ++i)
            {
                const float bx = 6 + (float) i * ((W - 12) / 15.0f);
                const bool on = (mask >> i) & 1;
                bulb (g, bx, sy, i == step ? c[1] : c[0], on ? (i == step ? 1.0f : 0.75f) : (i == step ? 0.25f : 0.0f), 4.2f);
            }
        };
    }

    void filterBody (CardBody& d)
    {
        using namespace Param::Filter;
        auto* dev = &d.card.dev;
        d.words (Slope, { "24 db", "12 db" }, 0, 0, 1, true);
        const float W = d.W;
        d.g.draw = [dev, W] (juce::Graphics& g, double, float, const Colour* c)
        {
            const float y0 = 10, h = 44, fc = DeviceViews::expMap (dev->get (Cutoff), 20, 18000), res = dev->get (Res), m = dev->get (Mode);
            Line f;
            for (int i = 0; i <= 70; ++i)
            {
                const float u = (float) i / 70.0f, hz = 20.0f * std::pow (1000.0f, u);
                const float mag = (1 - m) * DeviceViews::filterMagnitude (hz, fc, res, dev->choice (Slope) == 1 ? 1 : 0) + m * DeviceViews::filterMagnitude (hz, fc, res, 3);
                const float db = juce::jlimit (-30.0f, 12.0f, 20.0f * std::log10 (juce::jmax (1e-5f, mag)));
                f.push_back ({ u * W, y0 + h * 0.35f - db / 30.0f * h * 0.65f });
            }
            tube (g, f, c[0], w (0.75f));
        };
        const float top = 78;
        d.at (2, 0, top, 0, Cutoff, "cutoff", [] (float v) { return F::hz (DeviceViews::expMap (v, 20, 18000)); });
        d.at (2, 1, top, 0, Res, "resonance", pct);
        d.at (2, 0, top, 1, Drive, "drive", pct);
        d.at (2, 1, top, 1, Mode, "low to high", [] (float v) { return v < 0.03f ? String ("low pass") : v > 0.97f ? String ("high pass") : F::pct (v) + " high"; });
        d.at (2, 0, top, 2, Mix, "mix", pct, true);
        d.at (2, 1, top, 2, Wobble, "wobble", pct, true);
    }

    void driveBody (CardBody& d)
    {
        using namespace Param::Drive;
        auto* dev = &d.card.dev;
        d.knobWords (Type, { "tube", "tape", "fuzz", "fold" }, 0, 0);
        const float W = d.W;
        d.g.draw = [dev, W] (juce::Graphics& g, double, float, const Colour* c)
        {
            const float cx = W - 60, cy = 32, am = dev->get (Amount), k = 1 + am * 8;
            tube (g, Lines { Line { { cx - 50, cy }, { cx + 50, cy } }, Line { { cx, cy - 22 }, { cx, cy + 22 } } }, c[2], w (0.5f, true));
            Line curve;
            for (int i = 0; i <= 40; ++i) { const float u = (float) i / 40.0f * 2 - 1; curve.push_back ({ cx + u * 50, cy - std::tanh (u * k) / std::tanh (k) * 20 }); }
            tube (g, curve, c[0], w (0.8f));
        };
        const float top = 78;
        d.at (2, 0, top, 0, Amount, "amount", pct);
        d.at (2, 1, top, 0, Tone, "tone", pct);
        d.at (2, 0, top, 1, Mix, "mix", pct);
        d.at (2, 1, top, 1, Bias, "bias", pct, true, true);
        d.at (2, 0, top, 2, Output, "output", pct, true);
        d.at (2, 1, top, 2, Sag, "sag", pct, true);
    }

    void delayBody (CardBody& d)
    {
        using namespace Param::Delay;
        auto* dev = &d.card.dev;
        d.toggle (Sync, "sync", 0, 0, 0, true);
        d.toggle (PingPong, "ping pong", 60, 0, 1, true);
        d.toggle (Freeze, "freeze", d.W - 54, 0, 2, true);
        const float W = d.W;
        d.g.draw = [dev, W] (juce::Graphics& g, double t, float, const Colour* c)
        {
            const float fb = dev->get (Feedback), ph = (float) std::fmod (t * 1.6, 1.0);
            const bool pp = dev->on (PingPong);
            const int n = 10;
            for (int i = 0; i < n; ++i)
            {
                const float bx = 10 + (float) i * (W - 20) / (float) (n - 1), by = 34 + (pp && i % 2 ? 9.0f : -4.0f);
                const float e = std::pow (fb, (float) i);
                bulb (g, bx, by, c[i % 2], clampf (e * 1.1f * (i == (int) (ph * n) ? 1.0f : 0.7f)), 3 + 4 * e);
            }
        };
        const float top = 78;
        d.at (2, 0, top, 0, Time, "time", pct);
        d.at (2, 1, top, 0, Feedback, "feedback", pct);
        d.at (2, 0, top, 1, Tone, "tone", pct);
        d.at (2, 1, top, 1, Mix, "mix", pct);
        d.at (2, 0, top, 2, Flutter, "flutter", pct, true);
        d.at (2, 1, top, 2, Duck, "duck", pct, true);
        d.card.animated = true;
    }

    void reverbBody (CardBody& d)
    {
        using namespace Param::Reverb;
        auto* dev = &d.card.dev;
        d.words (Space, { "room", "hall", "plate", "cave" }, 0, 0, 0, true);
        const float W = d.W;
        d.g.draw = [dev, W] (juce::Graphics& g, double t, float, const Colour* c)
        {
            const float sz = dev->get (Size), cx = W - 46, cy = 58;
            for (int i = 0; i < 4; ++i)
            {
                const float r = 8 + (float) i * (6 + sz * 4);
                TubeOptions o = w (0.6f);
                juce::ignoreUnused (t);
                tube (g, arc (cx, cy, r, pi * 1.1f, pi * 1.9f, 24), c[i % 3], o);
            }
        };
        const float top = 78;
        d.at (2, 0, top, 0, Size, "size", pct);
        d.at (2, 1, top, 0, Damp, "damp", pct);
        d.at (2, 0, top, 1, Width, "width", pct);
        d.at (2, 1, top, 1, Mix, "mix", pct);
        d.at (2, 0, top, 2, PreDelay, "pre-delay", [] (float v) { return String (juce::roundToInt (v * 200)) + " ms"; }, true);
        d.at (2, 1, top, 2, Shimmer, "shimmer", pct, true);
    }

    void eqBody (CardBody& d)
    {
        using namespace Param::EQ;
        auto* dev = &d.card.dev;
        d.toggle (LowCut, "low cut", 0, 0, 2, true);
        d.toggle (HighCut, "high cut", 70, 0, 2, true);
        d.toggle (Analog, "warm", d.W - 44, 0, 1, true);
        const float W = d.W;
        d.g.draw = [dev, W] (juce::Graphics& g, double, float, const Colour* c)
        {
            const float y0 = 12, h = 48;
            const float gains[] = { dev->get (Low), dev->get (LowMid), dev->get (Mid), dev->get (High) };
            const float F0[] = { 0.12f, 0.36f, 0.6f, 0.86f };
            tube (g, Line { { 0, y0 }, { W, y0 } }, c[2], w (0.45f));
            Line pts;
            for (int i = 0; i <= 80; ++i)
            {
                const float u = (float) i / 80.0f;
                float s = 0;
                for (int k = 0; k < 4; ++k) s += gains[k] * std::exp (-std::pow ((u - F0[k]) / (k == 0 || k == 3 ? 0.22f : 0.1f), 2.0f));
                pts.push_back ({ u * W, y0 + h * 0.6f - s * h * 0.4f });
            }
            tube (g, pts, c[0], w (0.75f));
            for (int k = 0; k < 4; ++k)
            {
                const float lx = W * F0[k], ly = y0 + h * 0.6f - gains[k] * h * 0.4f;
                tube (g, Line { { lx, y0 }, { lx, ly - 6 } }, c[1], w (0.4f, true));
                bulb (g, lx, ly, c[k % 2], 1.0f, 4.0f);
            }
        };
        const float top = 80;
        d.signedAt (3, 0, top, 0, Low, "low");
        d.signedAt (3, 1, top, 0, Mid, "mid");
        d.signedAt (3, 2, top, 0, High, "high");
        d.at (3, 0, top, 1, Output, "output", pct);
        d.signedAt (3, 1, top, 1, LowMid, "low mid").isNew = true;
        d.signedAt (3, 2, top, 1, Tilt, "tilt").isNew = true;
    }

    void compBody (CardBody& d)
    {
        using namespace Param::Comp;
        auto* dev = &d.card.dev;
        d.words (Character, { "clean", "glue", "crush" }, 0, 0, 0, true);
        d.toggle (AutoMakeup, "auto makeup", 0, 32, 1, true);
        const float W = d.W;
        d.g.draw = [dev, W] (juce::Graphics& g, double, float, const Colour* c)
        {
            auto* node = dev->live();
            const float gr = node != nullptr ? juce::jlimit (0.0f, 1.0f, node->display.load() / 20.0f) : 0.0f;
            const float cx = W - 56, cy = 54, r = 42, a = pi * (1.1f + 0.8f * gr);
            tube (g, arc (cx, cy, r, pi * 1.1f, pi * 1.9f), c[2], w (0.7f, true));
            if (gr > 0.01f) tube (g, arc (cx, cy, r, pi * 1.1f, a), c[0], w (0.7f));
            tube (g, Line { { cx, cy }, { cx + std::cos (a) * (r - 8), cy + std::sin (a) * (r - 8) } }, c[1], w (0.6f));
            bulb (g, cx, cy, c[1], 1.0f, 3.0f);
            label (g, "-" + String (gr * 20.0f, 1) + " dB", 0, 62, Align::left, mix (c[0], juce::Colours::white, 0.4f), 13);
        };
        const float top = 80;
        d.at (3, 0, top, 0, Thresh, "threshold", [] (float v) { return String (juce::roundToInt (-v * 48)) + " dB"; });
        d.at (3, 1, top, 0, Ratio, "ratio", [] (float v) { return String (1 + v * 19, 1) + ":1"; });
        d.at (3, 2, top, 0, Mix, "mix", pct, true);
        d.at (3, 0, top, 1, Attack, "attack", pct);
        d.at (3, 1, top, 1, Release, "release", pct);
        d.at (3, 2, top, 1, Makeup, "makeup", pct);
        d.card.animated = true;
    }

    void lfoBody (CardBody& d)
    {
        using namespace Param::LFO;
        auto* dev = &d.card.dev;
        d.knobWords (Shape, { "sine", "tri", "saw", "square", "s&h" }, 0, 0);
        const float W = d.W;
        d.g.draw = [dev, W] (juce::Graphics& g, double t, float, const Colour* c)
        {
            auto* node = dev->live();
            const float v = node != nullptr ? juce::jlimit (-1.0f, 1.0f, node->display.load()) : std::sin ((float) t * 2.0f) * dev->get (Depth);
            const float cx = 26, cy = 46, r = 22;
            tube (g, circle (cx, cy, r, 36), c[2], w (0.6f, true));
            const float a = std::asin (juce::jlimit (-1.0f, 1.0f, v)) - pi * 0.5f;
            bulb (g, cx + std::cos (a) * r, cy + std::sin (a) * r, c[1], 1.0f, 4.0f);
            Line s;
            const int shape = juce::roundToInt (dev->get (Shape) * 4);
            for (int i = 0; i <= 60; ++i)
            {
                const float u = (float) i / 60.0f;
                s.push_back ({ 66 + u * (W - 66), cy - Param::lfoShape (shape, u * 2.0f) * dev->get (Depth) * 20 });
            }
            tube (g, s, c[0], w (0.7f));
        };
        const float top = 86;
        d.at (2, 0, top, 0, Rate, "rate", pct);
        d.at (2, 1, top, 0, Depth, "depth", pct);
        d.at (2, 0, top, 1, Fade, "fade in", pct, true);
        d.at (2, 1, top, 1, Chaos, "chaos", pct, true);
        d.card.animated = true;
    }

    void gasBody (CardBody& d)
    {
        using namespace Param::GasTube;
        using Param::GasTube::Glow;   // not the Glow drawing helpers
        auto* dev = &d.card.dev;
        d.words (Gas, { "neon", "argon", "mercury", "xenon" }, 0, 0);
        const float W = d.W;
        d.g.draw = [dev, W] (juce::Graphics& g, double t, float, const Colour* c)
        {
            auto* node = dev->live();
            const float lvl = node != nullptr ? juce::jlimit (0.0f, 1.0f, node->inputLevel.load() * 3.0f) : 0.0f;
            for (int i = 0; i < 6; ++i)
            {
                const float tx = 8 + (float) i * (W - 30) / 5.0f;
                const float flick = 0.35f + 0.65f * juce::jlimit (0.0f, 1.0f, lvl + 0.3f * std::sin ((float) t * (9.0f + (float) i * 2.3f)) * dev->get (Crackle));
                tube (g, Line { { tx, 22 }, { tx + 14, 22 }, { tx + 14, 52 }, { tx, 52 }, { tx, 22 } }, c[i % 3], w (0.6f, lvl < 0.02f && i > 1));
                bulb (g, tx + 7, 37, c[i % 3], flick * dev->get (Glow), 2.5f);
            }
        };
        const float top = 78;
        d.at (3, 0, top, 0, Voltage, "voltage", pct);
        d.at (3, 1, top, 0, Strike, "strike", pct);
        d.at (3, 2, top, 0, Body, "body", pct);
        d.at (3, 0, top, 1, Glow, "glow", pct);
        d.at (3, 1, top, 1, Fade, "fade", pct);
        d.at (3, 2, top, 1, Gain, "gain", pct);
        d.card.animated = true;
    }

    void crateBody (CardBody& d)
    {
        using namespace Param::Crate;
        auto* dev = &d.card.dev;
        auto* card = &d.card;
        d.words (Era, { "60s", "70s", "80s", "90s" }, 0, 0);
        auto& rec = d.g.dragArea ({ d.W - 120, 10, 120, 48 }, Cursor::PointingHandCursor);
        rec.down = [card] (float, float) -> std::optional<Gesture> { if (card->chooseSample) card->chooseSample(); return Gesture {}; };
        rec.tip = [] { return String ("click to choose the record"); };
        const float W = d.W;
        d.g.draw = [dev, W] (juce::Graphics& g, double t, float, const Colour* c)
        {
            const float cx = W - 30, cy = 32, spin = dev->ctx.engine.isPlaying() ? (float) t * 3.5f : 0.0f;
            tube (g, circle (cx, cy, 22, 36), c[0], w (0.7f));
            tube (g, circle (cx, cy, 6, 16), c[1], w (0.5f));
            bulb (g, cx + std::cos (spin) * 15, cy + std::sin (spin) * 15, c[2], 1.0f, 2.6f);
            const int sel = juce::jlimit (0, 15, dev->choice (Selected));
            for (int i = 0; i < 16; ++i) bulb (g, 6 + (float) i * 13, 40, c[i % 2], i == sel ? 1.0f : 0.3f, 3.2f);
        };
        const float top = 78;
        d.at (3, 0, top, 0, Wear, "wear", pct);
        d.at (3, 1, top, 0, Dust, "dust", pct);
        d.at (3, 2, top, 0, Wow, "wow", pct);
        d.at (3, 0, top, 1, Pitch, "pitch", pct, false, true);
        d.at (3, 1, top, 1, Chop, "chop", pct);
        d.at (3, 2, top, 1, Gain, "gain", pct);
        d.card.animated = true;
    }

    void stringsBody (CardBody& d)
    {
        using namespace Param::Strings;
        auto* dev = &d.card.dev;
        d.words (Mode, { "pluck", "bow", "feedback" }, 0, 0);
        d.words (Material, { "nylon", "steel", "wire", "gut" }, d.W - 190, 0, 1);
        const float W = d.W;
        d.g.draw = [dev, W] (juce::Graphics& g, double t, float, const Colour* c)
        {
            auto* node = dev->live();
            const float lvl = node != nullptr ? juce::jlimit (0.0f, 1.0f, node->inputLevel.load() * 3.0f) : 0.0f;
            for (int i = 0; i < 8; ++i)
            {
                Line s;
                const float y = 14 + (float) i * 5.5f;
                for (int k = 0; k <= 24; ++k) { const float u = (float) k / 24.0f; s.push_back ({ u * W, y + std::sin (u * pi) * std::sin ((float) t * 30 + (float) i * 2) * lvl * 2.0f }); }
                tube (g, s, c[i % 3], w (0.4f, lvl < 0.01f));
            }
            const float bar = dev->get (Bar) * W;
            if (dev->on (BarOn)) tube (g, Line { { bar, 8 }, { bar, 60 } }, c[2], w (0.8f));
        };
        const float top = 78;
        d.at (4, 0, top, 0, Tone, "tone", pct);
        d.at (4, 1, top, 0, Sustain, "sustain", pct);
        d.at (4, 2, top, 0, Pick, "pick", pct);
        d.at (4, 3, top, 0, Body, "body", pct);
        d.at (4, 0, top, 1, Sympathy, "sympathy", pct);
        d.at (4, 1, top, 1, Bar, "bar", pct);
        d.at (4, 2, top, 1, Gain, "gain", pct);
        d.at (4, 3, top, 1, BowPressure, "bow", pct, true);
        d.toggle (BarOn, "slide bar", 0, 165, 2);
        d.toggle (PedalA, "pedal a", 100, 165, 1);
        d.toggle (PedalB, "pedal b", 180, 165, 1);
        d.card.animated = true;
    }

    void ghostBody (CardBody& d)
    {
        using namespace Param::Ghost;
        auto* dev = &d.card.dev;
        d.toggle (Hold, "hold", 0, 0, 1);
        d.words (HoldHow, { "freeze", "backwards" }, 58, 0, 2);
        const float W = d.W;
        d.g.draw = [dev, W] (juce::Graphics& g, double, float, const Colour* c)
        {
            const float fade = dev->get (Fade);
            for (int i = 0; i < 5; ++i)
            {
                const float s = 1.0f - (float) i * 0.15f, ox = (float) i * (W - 50) / 4.0f, oy = 14 + (float) i * 3;
                Line tag { { ox, oy + 30 * s }, { ox + 10 * s, oy }, { ox + 18 * s, oy + 32 * s }, { ox + 28 * s, oy + 6 * s }, { ox + 34 * s, oy + 26 * s } };
                glow().alpha = std::pow (fade, (float) i);
                TubeOptions o = w (0.7f * s, std::pow (fade, (float) i) < 0.12f);
                tube (g, tag, c[i % 3], o);
                glow().alpha = 1.0f;
            }
        };
        const float top = 78;
        d.at (2, 0, top, 0, Time, "time", pct);
        d.at (2, 1, top, 0, Fade, "fade", pct);
        d.at (2, 0, top, 1, Wash, "wash", pct);
        d.at (2, 1, top, 1, Mix, "mix", pct);
        d.at (2, 0, top, 2, Drift, "drift", pct);
        d.at (2, 1, top, 2, Blur, "blur", pct);
    }

    void crowdBody (CardBody& d)
    {
        using namespace Param::Crowd;
        auto* dev = &d.card.dev;
        d.words (Preset, { "unison", "choir", "rabble", "stadium" }, 0, 0);
        const float W = d.W;
        d.g.draw = [dev, W] (juce::Graphics& g, double t, float, const Colour* c)
        {
            const int n = 2 + juce::roundToInt (dev->get (Size) * 10.0f);
            const float spread = dev->get (Spread);
            for (int i = 0; i < n; ++i)
            {
                const float u = n > 1 ? (float) i / (float) (n - 1) : 0.5f;
                const float px = W * 0.5f + (u - 0.5f) * (W - 24) * (0.3f + 0.7f * spread), py = 24 + (float) ((i * 7) % 3) * 5 + std::sin ((float) t * 1.5f + (float) i) * dev->get (Timing) * 2;
                tube (g, circle (px, py, 4, 12), c[i % 3], w (0.5f));
                tube (g, arc (px, py + 18, 8, pi * 1.1f, pi * 1.9f, 10), c[i % 3], w (0.5f));
            }
        };
        const float top = 78;
        d.at (2, 0, top, 0, Size, "size", [] (float v) { return String (2 + juce::roundToInt (v * 10)) + " people"; });
        d.at (2, 1, top, 0, Spread, "spread", pct);
        d.at (2, 0, top, 1, Timing, "timing", pct);
        d.at (2, 1, top, 1, Mix, "mix", pct);
        d.at (2, 0, top, 2, Tuning, "tuning", pct, true);
        d.at (2, 1, top, 2, Sway, "sway", pct, true);
        d.card.animated = true;
    }

    void xfmrBody (CardBody& d)
    {
        using namespace Param::Transformer;
        const char* parts[] = { "cap", "pot", "diode", "supply", "clock", "joint", "hum" };
        float x = 0;
        for (int i = 0; i < 7; ++i)
        {
            d.toggle (On0 + i, parts[i], x, 0, i % 3);
            x += textWidth (parts[i], 15.0f) + 12.0f;
        }
        const float top = 40;
        const char* names[] = { "battery", "cap", "pot", "diode", "sag", "clock", "joint", "hum", "mix", "output" };
        for (int k = 0; k < 10; ++k)
            d.at (4, k % 4, top, k / 4, k, names[k], pct);
    }

    void pluginBody (CardBody& d)
    {
        auto* card = &d.card;
        auto& open = d.g.toggle ("open", 0, 0, [] { return true; }, [card] (bool) { if (card->openPlugin) card->openPlugin(); });
        open.size = 17.0f;
        open.tip = [] { return String ("open the plugin's own window"); };
        d.at (2, 0, 78, 1, 0, "mix", pct);
        d.at (2, 1, 78, 1, 1, "gain", pct);
    }

    void utilityBody (CardBody& d, ModuleType t)
    {
        const auto& info = moduleInfo (t);
        const int n = (int) info.knobs.size();
        if (n == 0) return;
        const auto cs = d.cols (n);
        for (int k = 0; k < n; ++k)
        {
            String label = t == ModuleType::Mixer ? "in " + info.knobs[(size_t) k].first : info.knobs[(size_t) k].first.toLowerCase();
            if (t == ModuleType::Crossfade) label = "a  to  b";
            d.fader (k, label, pct, cs[(size_t) k].first, 0, cs[(size_t) k].second, false, t == ModuleType::Attenuator);
        }
    }
}

std::unique_ptr<Card> make (AppContext& ctx, int track, int moduleId, float width, float height)
{
    auto card = std::unique_ptr<Card> (new Card { Device { ctx, track, moduleId, {}, {} }, {}, {}, {}, {}, false });
    auto* m = card->dev.module();
    if (m == nullptr) return card;
    const auto type = m->type;
    const auto& info = moduleInfo (type);

    auto& built = card->built;
    built.wall = std::make_unique<Wall>();
    built.wall->labelSize = 14.0f;
    palette (type, built.pal);
    auto* bp = &built;
    built.wall->colours = [bp] (int mod) { return bp->colours (mod); };

    const bool utility = info.utility;
    const float ox = utility ? 14.0f : side, oy = utility ? height - 30.0f : bodyTop;
    auto& g = built.wall->add (0, ox, oy, 0, 0, 0, 0);
    Builder b { card->dev, built, *built.wall };
    CardBody body { *card, b, g, width - ox * 2.0f, ox, oy, (int) info.knobs.size() };

    switch (type)
    {
        case ModuleType::Sampler:     samplerBody (body); break;
        case ModuleType::Synth:       synthBody (body); break;
        case ModuleType::Drums:       drumsBody (body); break;
        case ModuleType::Filter:      filterBody (body); break;
        case ModuleType::Drive:       driveBody (body); break;
        case ModuleType::Delay:       delayBody (body); break;
        case ModuleType::Reverb:      reverbBody (body); break;
        case ModuleType::EQ:          eqBody (body); break;
        case ModuleType::Compressor:  compBody (body); break;
        case ModuleType::LFO:         lfoBody (body); break;
        case ModuleType::GasTube:     gasBody (body); break;
        case ModuleType::CrateDigger: crateBody (body); break;
        case ModuleType::StringWall:  stringsBody (body); break;
        case ModuleType::GhostTag:    ghostBody (body); break;
        case ModuleType::Crowd:       crowdBody (body); break;
        case ModuleType::Transformer: xfmrBody (body); break;
        case ModuleType::Plugin:      pluginBody (body); break;
        case ModuleType::Attenuator: case ModuleType::Mixer: case ModuleType::Inverter:
        case ModuleType::Crossfade: case ModuleType::Slew:
            utilityBody (body, type); break;
        case ModuleType::Input: case ModuleType::Output:
            break;
    }
    // Words and labels on the cards are a size smaller than on the walls
    for (auto& it : g.items)
        if (it->kind == Item::words && it->size > 15.0f && it->toggle == false) it->size = 15.0f;
    return card;
}
}
