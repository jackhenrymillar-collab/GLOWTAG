#include "PianoRollArt.h"

namespace
{
    using P = juce::Point<float>;
    using Neon::Line;
    using Neon::Lines;

    struct Art
    {
        Lines solid, dashed, faint;      // faint: lit at half strength (grid lines, ghosts)
        std::vector<P> lamps;            // small bulbs
    };

    Line bar (float x, float y, float w, float h) { return Neon::roundRect (x, y, w, h, juce::jmin (2.0f, h * 0.45f)); }
    Line head (P tip, P dir, float size = 3.5f)
    {
        dir = dir / std::sqrt (dir.x * dir.x + dir.y * dir.y);
        const P n (-dir.y, dir.x);
        return { tip - dir * size + n * size * 0.85f, tip, tip - dir * size - n * size * 0.85f };
    }

    // All drawn in a 32 x 32 box
    Art artFor (RollArt a)
    {
        Art r;
        switch (a)
        {
            case RollArt::Draw:
                r.solid = { { { 5.5f, 26.5f }, { 8.5f, 18.5f }, { 21.5f, 5.5f }, { 26.5f, 10.5f }, { 13.5f, 23.5f }, { 5.5f, 26.5f } },
                            { { 18.5f, 8.5f }, { 23.5f, 13.5f } } };
                r.faint = { { { 8.5f, 18.5f }, { 13.5f, 23.5f } } };
                r.solid.push_back (bar (17.0f, 25.0f, 12.0f, 4.0f));
                break;
            case RollArt::Select:
                r.solid = { { { 9, 7 }, { 9, 25 }, { 13.5f, 20.5f }, { 17, 28 }, { 20, 26.5f }, { 16.5f, 19.5f }, { 23, 19.5f }, { 9, 7 } } };
                r.dashed = { Neon::roundRect (3, 3, 26, 26, 3) };
                break;
            case RollArt::Split:
                r.solid = { Neon::circle (8, 9, 4, 24), Neon::circle (8, 23, 4, 24),
                            { { 11.5f, 11.5f }, { 28, 24 } }, { { 11.5f, 20.5f }, { 28, 8 } } };
                r.lamps = { { 17.5f, 16 } };
                break;
            case RollArt::Velocity:
                r.solid = { { { 6, 28 }, { 6, 20 } }, { { 13, 28 }, { 13, 11 } }, { { 20, 28 }, { 20, 16 } }, { { 27, 28 }, { 27, 6 } } };
                r.lamps = { { 6, 18.5f }, { 13, 9.5f }, { 20, 14.5f }, { 27, 4.5f } };
                break;
            case RollArt::Quantise:
                // Notes sitting on the grid lines, and a loose one being pulled onto its line
                r.faint = { { { 5, 3 }, { 5, 29 } }, { { 16, 3 }, { 16, 29 } }, { { 27, 3 }, { 27, 29 } }, bar (11, 21, 9, 5) };
                r.solid = { bar (5, 5, 9, 5), bar (16, 13, 9, 5), { { 20, 23.5f }, { 8.5f, 23.5f } }, head ({ 7.5f, 23.5f }, { -1, 0 }, 3.0f) };
                break;
            case RollArt::Grid:
                r.solid = { Neon::roundRect (4, 4, 24, 24, 3) };
                r.faint = { { { 12, 4 }, { 12, 28 } }, { { 20, 4 }, { 20, 28 } }, { { 4, 12 }, { 28, 12 } }, { { 4, 20 }, { 28, 20 } } };
                break;
            case RollArt::Snap:
            {
                // A magnet with a spark
                Line outer { { 7, 5 } }, inner { { 13, 5 } };
                for (int i = 0; i <= 12; ++i)
                {
                    const float t = juce::MathConstants<float>::pi * (float) i / 12.0f;
                    outer.push_back ({ 16.0f - 9.0f * std::cos (t), 15.0f + 9.0f * std::sin (t) });
                    inner.push_back ({ 16.0f - 3.0f * std::cos (t), 15.0f + 3.0f * std::sin (t) });
                }
                outer.push_back ({ 25, 5 });
                inner.push_back ({ 19, 5 });
                r.solid = { outer, inner, { { 7, 5 }, { 13, 5 } }, { { 19, 5 }, { 25, 5 } } };
                r.faint = { { { 7, 9 }, { 13, 9 } }, { { 19, 9 }, { 25, 9 } } };
                r.lamps = { { 16, 29 } };
                break;
            }
            case RollArt::Shorter:
                r.solid = { bar (3, 10, 15, 12), { { 29, 16 }, { 21, 16 } }, head ({ 20.5f, 16 }, { -1, 0 }) };
                r.dashed = { { { 18, 10 }, { 29, 10 } }, { { 18, 22 }, { 29, 22 } } };
                break;
            case RollArt::Longer:
                r.solid = { bar (3, 10, 13, 12), { { 18, 16 }, { 28, 16 } }, head ({ 28.5f, 16 }, { 1, 0 }) };
                r.dashed = { { { 16, 10 }, { 29, 10 } }, { { 16, 22 }, { 29, 22 } } };
                break;
            case RollArt::Duplicate:
                r.solid = { bar (3, 17, 11, 7), bar (18, 17, 11, 7), Neon::curve ({ 8.5f, 14 }, { 16, 2 }, { 23, 13 }, 16),
                            head ({ 23.3f, 14 }, { 0.45f, 1 }, 3.0f) };
                break;
            case RollArt::OctaveUp:
                r.faint = { bar (3, 23, 11, 5) };
                r.solid = { bar (18, 4, 11, 5), Neon::curve ({ 8.5f, 21 }, { 8.5f, 7 }, { 16, 6.5f }, 16),
                            head ({ 16.5f, 6.5f }, { 1, 0 }, 3.0f) };
                break;
            case RollArt::OctaveDown:
                r.faint = { bar (3, 4, 11, 5) };
                r.solid = { bar (18, 23, 11, 5), Neon::curve ({ 8.5f, 11 }, { 8.5f, 25 }, { 16, 25.5f }, 16),
                            head ({ 16.5f, 25.5f }, { 1, 0 }, 3.0f) };
                break;
            case RollArt::SelectAll:
                r.dashed = { Neon::roundRect (2.5f, 2.5f, 27, 27, 3) };
                r.solid = { bar (8, 8, 9, 4), bar (13, 14.5f, 11, 4), bar (8, 21, 10, 4) };
                break;
        }
        return r;
    }

    Lines placed (const Lines& ls, juce::Rectangle<float> box)
    {
        const float s = juce::jmin (box.getWidth(), box.getHeight()) / 32.0f;
        const P o (box.getCentreX() - 16.0f * s, box.getCentreY() - 16.0f * s);
        Lines out;
        for (auto& l : ls)
        {
            Line m;
            for (auto& p : l) m.push_back (o + p * s);
            out.push_back (std::move (m));
        }
        return out;
    }
}

namespace RollArtDraw
{
void draw (juce::Graphics& g, RollArt a, juce::Rectangle<float> box, juce::Colour c, float lit)
{
    const auto art = artFor (a);
    const float s = juce::jmin (box.getWidth(), box.getHeight()) / 32.0f;
    const float w = juce::jlimit (0.55f, 1.0f, s * 0.75f);
    const bool off = lit <= 0.01f;
    const auto col = off ? c : c.withMultipliedAlpha (0.55f + 0.45f * lit);

    Neon::TubeOptions o;
    o.w = w;
    o.off = off;
    o.style = 0;                                  // always tubes: the small art must stay crisp
    if (! art.faint.empty())
    {
        auto f = o;
        f.w = w * 0.75f;
        Neon::tube (g, placed (art.faint, box), off ? c : col.withMultipliedAlpha (0.55f), f);
    }
    if (! art.dashed.empty())
    {
        auto d = o;
        d.dashOn = 3.0f * s;
        d.dashOff = 2.4f * s;
        Neon::tube (g, placed (art.dashed, box), off ? c : col.withMultipliedAlpha (0.6f), d);
    }
    Neon::tube (g, placed (art.solid, box), col, o);
    const float sc = juce::jmin (box.getWidth(), box.getHeight()) / 32.0f;
    const P orig (box.getCentreX() - 16.0f * sc, box.getCentreY() - 16.0f * sc);
    for (auto p : art.lamps)
    {
        const auto q = orig + p * sc;
        Neon::bulb (g, q.x, q.y, c, off ? 0.15f : lit, 1.9f * sc);
    }
}
}

// The art on top and its word underneath, so a row of them stays narrow
static constexpr float wordH = 16.0f;

int RollButton::idealWidth (float wordSize) const
{
    const int iconW = juce::roundToInt ((float) (getHeight() > 0 ? getHeight() : 52) - wordH);
    if (! showWord) return iconW + 4;
    const auto w = (word.isNotEmpty() ? word : getName()).toLowerCase();
    return juce::jmax (iconW, juce::roundToInt (Neon::textWidth (w, wordSize))) + 8;
}

void RollButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    const auto c = Theme::get().pal (pal);
    const bool toggle = getClickingTogglesState();
    const bool on = toggle ? getToggleState() : (enabled == nullptr || enabled());
    const bool hot = over || down;
    const float lit = on ? (hot ? 1.0f : 0.8f) : (hot ? 0.35f : 0.0f);

    auto r = getLocalBounds().toFloat();
    const auto wordBox = showWord ? r.removeFromBottom (wordH) : juce::Rectangle<float>();
    const auto iconBox = r.withSizeKeepingCentre (r.getHeight(), r.getHeight()).reduced (r.getHeight() * 0.06f);
    const auto w = (word.isNotEmpty() ? word : getName()).toLowerCase();
    const float size = 15.0f;

    Neon::paintLit (g, getWidth(), getHeight(), Neon::scratchGlow(), 0.7f, [&] (juce::Graphics& gg, bool)
    {
        RollArtDraw::draw (gg, art, iconBox, c, lit);
        if (showWord)
            Neon::text (gg, w, wordBox.getCentreX(), wordBox.getBottom() - 3.0f, size, c,
                        on ? (hot ? 1.0f : 0.85f) : (hot ? 0.42f : 0.0f), Neon::Align::centre);
    });
}
