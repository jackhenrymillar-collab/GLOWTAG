#include "NeonKit.h"
#include "DeviceWallArt.h"
#include <BinaryData.h>

namespace Neon
{
using juce::Colour;
using juce::Point;

Look& look() { static Look l; return l; }

double now() { return juce::Time::getMillisecondCounterHiRes() * 0.001; }

//==============================================================================
// Polylines

float length (const Line& p)
{
    float total = 0.0f;
    for (size_t i = 1; i < p.size(); ++i) total += p[i].getDistanceFrom (p[i - 1]);
    return total;
}

Point<float> pointAt (const Line& p, float t)
{
    if (p.empty()) return {};
    const float target = clamp01 (t) * length (p);
    float done = 0.0f;
    for (size_t i = 1; i < p.size(); ++i)
    {
        const float seg = p[i].getDistanceFrom (p[i - 1]);
        if (target <= done + seg || i == p.size() - 1)
        {
            const float u = seg > 0.0f ? (target - done) / seg : 0.0f;
            return p[i - 1] + (p[i] - p[i - 1]) * u;
        }
        done += seg;
    }
    return p.front();
}

Line part (const Line& p, float a, float b)
{
    Line out;
    for (int i = 0; i <= 16; ++i) out.push_back (pointAt (p, a + (b - a) * (float) i / 16.0f));
    return out;
}

Near nearest (const Line& p, Point<float> q)
{
    const float total = juce::jmax (1e-6f, length (p));
    float best = 1e9f, bestT = 0.0f, done = 0.0f;
    for (size_t i = 1; i < p.size(); ++i)
    {
        const auto a = p[i - 1], d = p[i] - a;
        const float l2 = juce::jmax (1e-6f, d.x * d.x + d.y * d.y);
        const float u = clamp01 (((q.x - a.x) * d.x + (q.y - a.y) * d.y) / l2);
        const float dist = (a + d * u).getDistanceFrom (q);
        const float seg = std::sqrt (l2);
        if (dist < best) { best = dist; bestT = (done + seg * u) / total; }
        done += seg;
    }
    return { best, bestT };
}

std::vector<Step> walk (const Line& line, float step, float from)
{
    std::vector<Step> out;
    float need = from;
    for (size_t i = 1; i < line.size(); ++i)
    {
        const auto a = line[i - 1], b = line[i];
        const float d = a.getDistanceFrom (b);
        if (d <= 0.0f) continue;
        float t = need;
        const auto dir = (b - a) / d;
        while (t <= d) { out.push_back ({ a + dir * t, dir }); t += step; }
        need = t - d;
    }
    return out;
}

Line roundRect (float x, float y, float w, float h, float r)
{
    Line p;
    auto corner = [&] (float cx, float cy, float a0)
    {
        for (int i = 0; i <= 5; ++i)
        {
            const float a = a0 + (float) i / 5.0f * juce::MathConstants<float>::halfPi;
            p.push_back ({ cx + std::cos (a) * r, cy + std::sin (a) * r });
        }
    };
    corner (x + w - r, y + r, -juce::MathConstants<float>::halfPi);
    corner (x + w - r, y + h - r, 0.0f);
    corner (x + r, y + h - r, juce::MathConstants<float>::halfPi);
    corner (x + r, y + r, juce::MathConstants<float>::pi);
    return p;
}

Line circle (float cx, float cy, float r, int n)
{
    Line p;
    for (int i = 0; i <= n; ++i)
    {
        const float a = (float) i / (float) n * juce::MathConstants<float>::twoPi;
        p.push_back ({ cx + r * std::cos (a), cy + r * std::sin (a) });
    }
    return p;
}

Line curve (Point<float> a, Point<float> c, Point<float> b, int n)
{
    Line p;
    for (int i = 0; i <= n; ++i)
    {
        const float t = (float) i / (float) n, u = 1.0f - t;
        p.push_back (a * (u * u) + c * (2.0f * t * u) + b * (t * t));
    }
    return p;
}

//==============================================================================
// Glass

namespace
{
    juce::Path pathOf (const Lines& lines, bool closed)
    {
        juce::Path p;
        for (auto& l : lines)
        {
            if (l.empty()) continue;
            p.startNewSubPath (l[0]);
            for (size_t i = 1; i < l.size(); ++i) p.lineTo (l[i]);
            if (closed) p.closeSubPath();
        }
        return p;
    }

    Lines dashed (const Lines& lines, float on, float off)
    {
        Lines out;
        for (auto& l : lines)
        {
            const float total = length (l);
            for (float s = 0.0f; s < total; s += on + off)
            {
                const float a = s / total, b = juce::jmin (1.0f, (s + on) / total);
                Line seg;
                for (int i = 0; i <= 4; ++i) seg.push_back (pointAt (l, a + (b - a) * (float) i / 4.0f));
                out.push_back (std::move (seg));
            }
        }
        return out;
    }

    void stroke (juce::Graphics& g, const juce::Path& p, float width, Colour c, juce::AffineTransform t = {})
    {
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), t);
    }

    const Colour offGlass (0xff474950);
}

void tube (juce::Graphics& g, const Lines& linesIn, Colour col, const TubeOptions& o)
{
    const float G = look().glow, lw = o.w;
    const int st = o.style >= 0 ? o.style : look().style;
    const bool isDashed = o.dashOn > 0.0f;
    const Lines& lines = linesIn;
    const auto path = pathOf (isDashed ? dashed (lines, o.dashOn, o.dashOff) : lines, o.closed && ! isDashed);

    if (o.off)
    {
        stroke (g, path, 3.0f * lw, juce::Colours::black.withAlpha (0.35f), juce::AffineTransform::translation (1.2f, 2.0f));
        stroke (g, path, 2.2f * lw, mix (col, offGlass, 0.86f));
        stroke (g, path, 0.5f, juce::Colours::white.withAlpha (0.14f), juce::AffineTransform::translation (-0.4f, -0.6f));
        return;
    }

    if (st == 1 && ! isDashed)
    {
        const float r = 1.7f * lw;
        std::vector<Point<float>> pts;
        for (auto& l : lines)
        {
            Line ll = l;
            if (o.closed && ! l.empty()) ll.push_back (l.front());
            for (auto& s : walk (ll, o.step, o.step / 2.0f)) pts.push_back (s.p);
        }
        auto dots = [&] (float rr, float ox, float oy, Colour c)
        {
            juce::Path d;
            for (auto& p : pts) d.addEllipse (p.x + ox - rr, p.y + oy - rr, rr * 2.0f, rr * 2.0f);
            g.setColour (c);
            g.fillPath (d);
        };
        dots (r + 0.6f, 1.2f, 2.0f, juce::Colours::black.withAlpha (0.45f));
        dots (r * 3.4f, 0, 0, rgba (col, 0.07f * G));
        dots (r * 2.0f, 0, 0, rgba (col, 0.18f * G));
        dots (r, 0, 0, mix (col, juce::Colours::white, 0.3f));
        dots (r * 0.45f, 0, 0, juce::Colours::white.withAlpha (0.85f));
        return;
    }

    if (st == 2 && o.base.has_value())
    {
        juce::Path fill;
        for (auto& l : lines)
        {
            if (l.empty()) continue;
            fill.startNewSubPath (l.front().x, *o.base);
            for (auto& p : l) fill.lineTo (p);
            fill.lineTo (l.back().x, *o.base);
            fill.closeSubPath();
        }
        g.setColour (rgba (col, 0.17f));
        g.fillPath (fill);
    }

    // Shadow on the wall, glow, glass, hot core, shine
    if (st == 2) stroke (g, path, 3.2f * lw, mix (col, juce::Colours::black, 0.55f), juce::AffineTransform::translation (2.6f, 2.6f));
    else         stroke (g, path, 3.6f * lw, juce::Colours::black.withAlpha (0.42f), juce::AffineTransform::translation (1.4f, 2.4f));
    if (G > 0.02f)
    {
        const float k = 0.55f + 0.45f * G;
        const float widths[3] = { 22.0f, 14.0f, 8.0f }, alphas[3] = { 0.035f, 0.06f, 0.15f };
        for (int i = 0; i < 3; ++i) stroke (g, path, widths[i] * lw * k, rgba (col, alphas[i] * G));
    }
    stroke (g, path, (st == 2 ? 3.1f : 2.4f) * lw, mix (col, juce::Colours::white, 0.18f));
    stroke (g, path, 0.95f * lw, mix (col, juce::Colours::white, 0.8f));
    stroke (g, path, 0.4f, juce::Colours::white.withAlpha (0.4f), juce::AffineTransform::translation (-0.5f, -0.7f));

    if (st == 0 && o.clips > 0.0f)
    {
        juce::Path clips;
        for (auto& l : lines)
            for (auto& s : walk (l, o.clips, o.clips / 2.0f))
            {
                clips.startNewSubPath (s.p.x - s.dir.y * 4.4f, s.p.y + s.dir.x * 4.4f);
                clips.lineTo (s.p.x + s.dir.y * 4.4f, s.p.y - s.dir.x * 4.4f);
            }
        stroke (g, clips, 1.3f, Colour (0xff7a7d86));
    }
}

void bulb (juce::Graphics& g, float x, float y, Colour col, float lv, float r)
{
    lv = clamp01 (lv);
    if (lv > 0.03f)
    {
        const float m[3] = { 4.0f, 2.6f, 1.7f }, a[3] = { 0.06f, 0.12f, 0.22f };
        for (int i = 0; i < 3; ++i)
        {
            g.setColour (rgba (col, a[i] * lv * look().glow));
            g.fillEllipse (x - r * m[i], y - r * m[i], r * m[i] * 2.0f, r * m[i] * 2.0f);
        }
    }
    g.setColour (mix (Colour (0xff2a2b30), mix (col, juce::Colours::white, 0.4f), lv));
    g.fillEllipse (x - r, y - r, r * 2.0f, r * 2.0f);
    g.setColour (juce::Colours::white.withAlpha (0.18f));
    g.drawEllipse (x - r, y - r, r * 2.0f, r * 2.0f, 0.6f);
    g.setColour (juce::Colours::white.withAlpha (0.12f + 0.7f * lv));
    const float hr = r * 0.32f;
    g.fillEllipse (x - r * 0.3f - hr, y - r * 0.3f - hr, hr * 2.0f, hr * 2.0f);
}

void ringMark (juce::Graphics& g, float x, float y, Colour col)
{
    g.setColour (mix (col, juce::Colours::white, 0.3f));
    g.drawEllipse (x - 2.7f, y - 2.7f, 5.4f, 5.4f, 1.0f);
}

//==============================================================================
// Lettering

namespace
{
    juce::Typeface::Ptr face (int which)
    {
        static juce::Typeface::Ptr faces[3] = {
            juce::Typeface::createSystemTypefaceFor (BinaryData::SedgwickAveDisplayRegular_ttf, BinaryData::SedgwickAveDisplayRegular_ttfSize),
            juce::Typeface::createSystemTypefaceFor (BinaryData::MonotonRegular_ttf, BinaryData::MonotonRegular_ttfSize),
            juce::Typeface::createSystemTypefaceFor (BinaryData::BowlbyOneRegular_ttf, BinaryData::BowlbyOneRegular_ttfSize) };
        return faces[juce::jlimit (0, 2, which)];
    }

    // Glyph outlines at size 100 with the baseline at 0, cached by face and text
    struct Shaped { juce::Path path; float width = 0.0f; };
    const Shaped& shaped (int which, const juce::String& s)
    {
        static std::map<juce::String, Shaped> cache;
        const auto key = juce::String (which) + "|" + s;
        auto it = cache.find (key);
        if (it != cache.end()) return it->second;
        if (cache.size() > 3000) cache.clear();
        Shaped sh;
        const juce::Font font (juce::FontOptions (face (which)).withPointHeight (100.0f));
        juce::GlyphArrangement ga;
        ga.addLineOfText (font, s, 0.0f, 0.0f);
        ga.createPath (sh.path);
        sh.width = ga.getBoundingBox (0, -1, true).getRight();
        if (s.endsWithChar (' ')) sh.width += 25.0f;
        return cache[key] = std::move (sh);
    }

    float placeX (float x, float w, Align al) { return al == Align::left ? x : al == Align::centre ? x - w / 2.0f : x - w; }
}

float textWidth (const juce::String& s, float size) { return shaped (0, s).width * size / 100.0f; }

float text (juce::Graphics& g, const juce::String& s, float x, float y, float size, Colour col, float lit, Align al)
{
    const auto& sh = shaped (0, s);
    const float k = size / 100.0f, w = sh.width * k;
    const auto t = juce::AffineTransform::scale (k).translated (placeX (x, w, al), y);
    if (lit > 0.0f)
    {
        const float G = look().glow;
        const float blur = (5.0f + size * 0.3f) * G * lit;
        if (blur > 0.5f)
        {
            // A soft halo standing in for the glass's light on the bricks
            g.setColour (rgba (col, 0.07f * lit));
            g.strokePath (sh.path, juce::PathStrokeType (blur * 0.55f / k, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), t);
            g.setColour (rgba (col, 0.14f * lit));
            g.strokePath (sh.path, juce::PathStrokeType (blur * 0.22f / k, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), t);
        }
        g.setColour (mix (mix (Colour (0xff60626a), col, lit), juce::Colours::white, 0.5f * lit));
        g.fillPath (sh.path, t);
    }
    else
    {
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.fillPath (sh.path, t.translated (1.0f, 1.5f));
        g.setColour (Colour (0xff575960));
        g.fillPath (sh.path, t);
    }
    return w;
}

void label (juce::Graphics& g, const juce::String& s, float x, float y, Align al, Colour c, float size)
{
    const auto& sh = shaped (0, s);
    const float k = size / 100.0f;
    g.setColour (c);
    g.fillPath (sh.path, juce::AffineTransform::scale (k).translated (placeX (x, sh.width * k, al), y));
}

void bigTag (juce::Graphics& g, const juce::String& s, float x, float y, Colour c)
{
    const float G = look().glow;
    const int st = look().style;
    const auto base = juce::AffineTransform::rotation (-0.055f).translated (x, y);
    auto round = [] (float w) { return juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded); };
    if (st == 1)
    {
        const auto& sh = shaped (1, s.toUpperCase());
        const float k = 0.7f;
        const auto t = juce::AffineTransform::scale (k).translated (0.0f, -6.0f).followedBy (base);
        g.setColour (rgba (c, 0.12f * G)); g.strokePath (sh.path, round (30.0f), t);
        g.setColour (rgba (c, 0.2f * G));  g.strokePath (sh.path, round (12.0f), t);
        g.setColour (mix (c, juce::Colours::white, 0.5f)); g.fillPath (sh.path, t);
    }
    else if (st == 2)
    {
        const auto& sh = shaped (2, s.toUpperCase());
        const float k = 0.72f;
        const auto shear = juce::AffineTransform (1.0f, -0.16f, 0.0f, 0.0f, 1.0f, 0.0f);
        const auto t = juce::AffineTransform::scale (k).followedBy (shear).followedBy (base);
        g.setColour (mix (c, juce::Colours::black, 0.6f)); g.fillPath (sh.path, t.translated (5.0f, -1.0f).followedBy ({}));
        g.setColour (Colour (0xff17181b)); g.fillPath (sh.path, juce::AffineTransform::scale (k).followedBy (shear).translated (0.0f, -6.0f).followedBy (base));
        const auto t2 = juce::AffineTransform::scale (k).followedBy (shear).translated (0.0f, -6.0f).followedBy (base);
        g.setColour (rgba (c, 0.2f)); g.fillPath (sh.path, t2);
        g.setColour (rgba (c, 0.18f * G)); g.strokePath (sh.path, round (12.0f / k), t2);
        g.setColour (mix (c, juce::Colours::white, 0.25f)); g.strokePath (sh.path, round (2.2f / k), t2);
    }
    else
    {
        const auto& sh = shaped (0, s);
        const float k = 1.04f;
        const auto t = juce::AffineTransform::scale (k).followedBy (base);
        g.setColour (juce::Colours::black.withAlpha (0.45f)); g.strokePath (sh.path, round (4.0f / k), juce::AffineTransform::scale (k).translated (2.5f, 4.0f).followedBy (base));
        g.setColour (rgba (c, 0.07f * G)); g.strokePath (sh.path, round (40.0f / k), t);
        g.setColour (rgba (c, 0.12f * G)); g.strokePath (sh.path, round (20.0f / k), t);
        g.setColour (rgba (c, juce::jmin (1.0f, 0.6f * G + 0.1f))); g.strokePath (sh.path, round (6.0f / k), t);
        g.setColour (mix (c, juce::Colours::white, 0.18f)); g.strokePath (sh.path, round (3.2f / k), t);
        g.setColour (mix (c, juce::Colours::white, 0.82f)); g.strokePath (sh.path, round (1.1f / k), t);
    }
}

void paintBricks (juce::Graphics& g, juce::Rectangle<float> area)
{
    static const juce::Image tile = WallArt::brickTile (2.0f, false);
    g.setTiledImageFill (tile, 0, 0, 1.0f);
    g.saveState();
    g.addTransform (juce::AffineTransform::scale (0.5f));
    g.setTiledImageFill (tile, 0, 0, 1.0f);
    g.fillRect (area.withSize (area.getWidth() * 2.0f, area.getHeight() * 2.0f).withPosition (area.getPosition() * 2.0f));
    g.restoreState();
    g.setColour (Colour (0xff0a0a0c).withAlpha (0.56f));
    g.fillRect (area);
}

//==============================================================================
// Groups and items

Item& Group::add (Item::Kind k)
{
    items.push_back (std::make_unique<Item>());
    items.back()->kind = k;
    return *items.back();
}

Item& Group::fader (float x0, float y0, float x1, float y1, std::optional<Point<float>> bend)
{
    auto& it = add (Item::fader);
    it.pts = bend ? curve ({ x0, y0 }, *bend, { x1, y1 }) : Line { { x0, y0 }, { x1, y1 } };
    it.lx = juce::jmin (x0, x1);
    it.ly = juce::jmin (y0, y1) - 12.0f;
    return it;
}

Item& Group::words (std::vector<std::pair<int, juce::String>> opts, float x, float y)
{
    auto& it = add (Item::words);
    it.opts = std::move (opts);
    it.x = x; it.y = y;
    return it;
}

Item& Group::toggle (const juce::String& word, float x, float y, std::function<bool()> get, std::function<void (bool)> set)
{
    auto& it = add (Item::words);
    it.opts = { { 1, word } };
    it.x = x; it.y = y; it.gap = 0.0f; it.toggle = true;
    it.getChoice = [get] { return get() ? 1 : 0; };
    it.setChoice = [set] (int v) { set (v != 0); };
    return it;
}

Item& Group::handle (std::function<std::optional<Point<float>>()> pos)
{
    auto& it = add (Item::handle);
    it.pos = std::move (pos);
    return it;
}

Item& Group::dragArea (juce::Rectangle<float> box, juce::MouseCursor::StandardCursorType cursor)
{
    auto& it = add (Item::area);
    it.box = box;
    it.cursor = cursor;
    return it;
}

Item& Group::counter (int n, float x, float y)
{
    auto& it = add (Item::count);
    it.n = n; it.x = x; it.y = y;
    return it;
}

Group& Wall::add (int mod, float x, float y, float rotDegrees, float px, float py, float pr)
{
    groups.push_back (std::make_unique<Group>());
    auto& g = *groups.back();
    g.mod = mod; g.x = x; g.y = y; g.rot = juce::degreesToRadians (rotDegrees);
    g.poolCentre = { px, py }; g.poolRadius = pr;
    return g;
}

void Wall::say (const juce::String& s) { readout = s; readoutTime = now(); }

void Wall::paintPools (juce::Graphics& g)
{
    for (auto& gr : groups)
    {
        if (gr->poolRadius <= 0.0f) continue;
        const auto c = colours (gr->mod)[0];
        const auto p = gr->toGlobal (gr->poolCentre);
        const float a = 0.15f * look().glow, r = gr->poolRadius;
        juce::ColourGradient grad (rgba (c, a), p.x, p.y, rgba (c, 0.0f), p.x + r, p.y, true);
        grad.addColour (0.55, rgba (c, a * 0.42f));
        g.setGradientFill (grad);
        g.fillEllipse (p.x - r, p.y - r, r * 2.0f, r * 2.0f);
    }
    for (auto& w : wires)
    {
        juce::Path p;
        p.startNewSubPath (w.v[0], w.v[1]);
        p.cubicTo (w.v[2], w.v[3], w.v[4], w.v[5], w.v[6], w.v[7]);
        stroke (g, p, 4.0f, juce::Colours::black.withAlpha (0.5f));
        stroke (g, p, 2.2f, Colour (0xff2b2c31));
        g.setColour (Colour (0xff74767e));
        g.fillEllipse (w.v[0] - 3, w.v[1] - 3, 6, 6);
        g.fillEllipse (w.v[6] - 3, w.v[7] - 3, 6, 6);
    }
}

void Wall::paint (juce::Graphics& g, double t, float dt)
{
    for (auto& gr : groups)
    {
        juce::Graphics::ScopedSaveState s (g);
        g.addTransform (gr->toWall());
        const auto* c = colours (gr->mod);
        if (gr->draw) gr->draw (g, t, dt, c);
        for (auto& it : gr->items)
            if (! it->visible || it->visible())
                drawItem (g, *it, c);
    }
}

void Wall::drawItem (juce::Graphics& g, Item& it, const Colour* c)
{
    const bool on = hot == &it || active == &it;
    const auto col = c[juce::jlimit (0, 2, it.ci)];
    switch (it.kind)
    {
        case Item::fader:
        {
            const float v = it.get ? it.get() : 0.0f;
            if (it.bulbs > 0)
            {
                for (int i = 0; i < it.bulbs; ++i)
                {
                    const float u = (float) i / (float) (it.bulbs - 1);
                    const auto q = pointAt (it.pts, u);
                    bulb (g, q.x, q.y, col, 1.0f - std::abs (u - v) * 3.4f, 4.6f);
                }
            }
            else
            {
                const float a = it.bipolar ? 0.5f : 0.0f, lo = juce::jmin (a, v), hi = juce::jmax (a, v);
                TubeOptions off; off.off = true;
                tube (g, it.pts, col, off);
                if (hi - lo > 0.006f) { TubeOptions o; o.w = on ? 1.12f : 1.0f; tube (g, part (it.pts, lo, hi), col, o); }
                const auto q = pointAt (it.pts, v);
                bulb (g, q.x, q.y, col, 1.0f, on ? 6.4f : 5.0f);
                g.setColour (Colour (0xff6e7078));
                for (auto e : { it.pts.front(), it.pts.back() }) g.fillEllipse (e.x - 1.8f, e.y - 1.8f, 3.6f, 3.6f);
            }
            if (it.text.isNotEmpty())
            {
                const float w = text (g, it.text, it.lx, it.ly, 17.0f, col, on ? 1.0f : 0.55f);
                if (it.isNew) ringMark (g, it.lx + w + 7.0f, it.ly - 5.0f, col);
            }
            break;
        }
        case Item::words:
        {
            float x = it.x;
            it.rects.clear();
            const int cur = it.getChoice ? it.getChoice() : 0;
            if (it.pre.isNotEmpty()) x += text (g, it.pre, x, it.y, it.size * 0.85f, col, 0.3f) + 10.0f;
            for (auto& [value, word] : it.opts)
            {
                const auto s = (it.dyn ? it.dyn() : word).toLowerCase();
                const bool lit = it.dyn || it.always || value == cur;
                const float w = text (g, s, x, it.y, it.size, col, lit ? (on && it.hoverValue == value ? 1.0f : 0.92f) : 0.0f);
                it.rects.push_back ({ { x - 5.0f, it.y - it.size, w + 10.0f, it.size * 1.35f }, value });
                x += w + it.gap;
            }
            if (it.isNew) ringMark (g, x - it.gap + 7.0f, it.y - it.size * 0.3f, col);
            break;
        }
        case Item::count:
        {
            const int cur = it.getChoice ? it.getChoice() : 0;
            float x = it.x;
            if (it.text.isNotEmpty()) x += text (g, it.text, x, it.y + 6.0f, 17.0f, col, 0.55f) + 12.0f;
            it.x0 = x;
            for (int i = 0; i < it.n; ++i) bulb (g, x + (float) i * it.dx, it.y, col, i <= cur ? 1.0f : 0.0f, 5.5f);
            if (it.isNew) ringMark (g, x + (float) it.n * it.dx, it.y, col);
            break;
        }
        case Item::handle:
        {
            const auto p = it.pos ? it.pos() : std::nullopt;
            if (! p) break;
            bulb (g, p->x, p->y, col, 1.0f, on ? it.r + 1.6f : it.r);
            juce::Path ring;
            ring.addEllipse (p->x - it.r - 4.5f, p->y - it.r - 4.5f, (it.r + 4.5f) * 2.0f, (it.r + 4.5f) * 2.0f);
            juce::Path dashedRing;
            const float dash[] = { 2.0f, 3.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (dashedRing, ring, dash, 2);
            g.setColour (rgba (col, on ? 0.8f : 0.35f));
            g.fillPath (dashedRing);
            break;
        }
        case Item::area: break;
    }
}

Wall::Hit Wall::pick (Point<float> p)
{
    Hit best;
    int bestPriority = 0;
    for (auto& gr : groups)
    {
        const auto l = gr->toLocal (p);
        for (auto& itp : gr->items)
        {
            auto& it = *itp;
            if (it.visible && ! it.visible()) continue;
            int pr = 0;
            switch (it.kind)
            {
                case Item::handle:
                    if (auto q = it.pos ? it.pos() : std::nullopt; q && q->getDistanceFrom (l) < it.r + 10.0f) pr = 4;
                    break;
                case Item::fader:
                    if (nearest (it.pts, l).distance < 11.0f) pr = 3;
                    break;
                case Item::words:
                    for (auto& [r, v] : it.rects)
                        if (r.contains (l)) { pr = 3; it.hoverValue = v; break; }
                    break;
                case Item::count:
                    for (int i = 0; i < it.n; ++i)
                        if (Point<float> (it.x0 + (float) i * it.dx, it.y).getDistanceFrom (l) < 11.0f) { pr = 3; it.hoverIndex = i; }
                    break;
                case Item::area:
                    if (it.box.contains (l)) pr = 1;
                    break;
            }
            if (pr > bestPriority) { bestPriority = pr; best = { gr.get(), &it, l }; }
        }
    }
    return best;
}

bool Wall::mouseDown (Point<float> p)
{
    const auto h = pick (p);
    if (h.it == nullptr) return false;
    auto& it = *h.it;
    active = &it; activeGroup = h.g; startLocal = h.local;
    gesture.reset();
    switch (it.kind)
    {
        case Item::fader:
            if (it.set) it.set (clamp01 (nearest (it.pts, h.local).t));
            if (it.tip) say (it.tip());
            break;
        case Item::words:
            if (it.setChoice)
            {
                if (it.toggle) it.setChoice (it.getChoice() ? 0 : 1);
                else it.setChoice (it.hoverValue);
            }
            if (! it.toggle && it.tip == nullptr && ! it.dyn)
                for (auto& [v, w] : it.opts) if (v == it.hoverValue) say (w.toLowerCase());
            if (it.tip) say (it.tip());
            active = nullptr;
            break;
        case Item::count:
            if (it.setChoice) it.setChoice (it.hoverIndex);
            if (it.tip) say (it.tip());
            active = nullptr;
            break;
        case Item::handle:
        case Item::area:
            if (it.down) gesture = it.down (h.local.x, h.local.y);
            else if (it.start) startState = it.start();
            if (it.down && ! gesture) active = nullptr;
            break;
    }
    return true;
}

void Wall::mouseDrag (Point<float> p, bool fine)
{
    if (active == nullptr || activeGroup == nullptr) return;
    const auto l = activeGroup->toLocal (p);
    auto& it = *active;
    if (it.kind == Item::fader)
    {
        if (it.set) it.set (clamp01 (nearest (it.pts, l).t));
    }
    else if (gesture)
    {
        if (gesture->move) gesture->move (l.x, l.y);
    }
    else if (it.drag)
    {
        const float k = fine ? 0.25f : 1.0f;
        it.drag ((l.x - startLocal.x) * k, (l.y - startLocal.y) * k, startState);
    }
    if (it.tip) say (it.tip());
}

void Wall::mouseUp()
{
    if (gesture && gesture->up) gesture->up();
    gesture.reset();
    active = nullptr;
    activeGroup = nullptr;
}

bool Wall::doubleClick (Point<float> p)
{
    const auto h = pick (p);
    if (h.it == nullptr) return false;
    if (h.it->kind == Item::fader)
    {
        if (h.it->set) h.it->set (h.it->def);
        if (h.it->tip) say (h.it->tip());
    }
    else if (h.it->doubleClick)
        h.it->doubleClick (h.local.x, h.local.y);
    return true;
}

bool Wall::wheel (Point<float> p, float delta)
{
    const auto h = pick (p);
    if (h.it == nullptr) return false;
    if (h.it->kind == Item::fader && h.it->get && h.it->set)
        h.it->set (clamp01 (h.it->get() + (delta > 0 ? 0.02f : -0.02f)));
    else if (h.it->wheel)
        h.it->wheel (delta);
    else
        return false;
    if (h.it->tip) say (h.it->tip());
    return true;
}

juce::MouseCursor::StandardCursorType Wall::mouseMove (Point<float> p)
{
    const auto h = pick (p);
    hot = h.it;
    if (h.it == nullptr) return juce::MouseCursor::NormalCursor;
    if (h.it->tip) say (h.it->tip());
    if (h.it->hover) return h.it->hover (h.local.x, h.local.y);
    switch (h.it->kind)
    {
        case Item::fader: case Item::words: case Item::count: return juce::MouseCursor::PointingHandCursor;
        case Item::handle: return juce::MouseCursor::DraggingHandCursor;
        case Item::area: return h.it->cursor;
    }
    return juce::MouseCursor::NormalCursor;
}
}
