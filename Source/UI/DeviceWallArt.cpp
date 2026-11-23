#include "DeviceWallArt.h"
#include "../Theme.h"
#include <BinaryData.h>

namespace WallArt
{
#include "DeviceWallArtData.inc"

namespace
{
    using juce::Path;
    using juce::Colour;
    using juce::AffineTransform;

    const auto rounded = juce::PathStrokeType::rounded;
    const auto curved = juce::PathStrokeType::curved;

    juce::PathStrokeType stroke (float width) { return juce::PathStrokeType (juce::jmax (0.3f, width), curved, rounded); }

    // Walk a path's centre line. `visit (subPathStart, a, b, distanceAlongSubPath)` for every flattened segment.
    template <typename Fn>
    void walk (const Path& p, Fn&& visit)
    {
        juce::PathFlatteningIterator it (p, {}, 0.3f);
        int sub = -1;
        float along = 0.0f;
        while (it.next())
        {
            const bool start = it.subPathIndex != sub;
            if (start) { sub = it.subPathIndex; along = 0.0f; }
            const juce::Point<float> a (it.x1, it.y1), b (it.x2, it.y2);
            visit (start, a, b, along);
            along += a.getDistanceFrom (b);
        }
    }

    // Cut a centre line into dashes, restarting the pattern on each sub-path as SVG does
    Path dashed (const Path& p, float on, float off)
    {
        Path out;
        const float period = juce::jmax (0.5f, on + off);
        walk (p, [&] (bool, juce::Point<float> a, juce::Point<float> b, float along)
        {
            const float len = a.getDistanceFrom (b);
            if (len <= 0.0f) return;
            float t = 0.0f;                                  // distance into this segment
            while (t < len)
            {
                const float phase = std::fmod (along + t, period);
                const bool drawing = phase < on;
                const float run = juce::jmin (len - t, (drawing ? on : period) - phase);
                if (drawing)
                {
                    const auto s = a + (b - a) * (t / len), e = a + (b - a) * ((t + run) / len);
                    if (phase <= 0.0001f || out.isEmpty() || out.getCurrentPosition().getDistanceFrom (s) > 0.01f)
                        out.startNewSubPath (s);
                    out.lineTo (e);
                }
                t += juce::jmax (0.001f, run);
            }
        });
        return out;
    }

    std::vector<juce::Point<float>> dotsAlong (const Path& p, float spacing)
    {
        std::vector<juce::Point<float>> out;
        float next = 0.0f;
        walk (p, [&] (bool start, juce::Point<float> a, juce::Point<float> b, float along)
        {
            if (start) next = 0.0f;
            const float len = a.getDistanceFrom (b);
            while (len > 0.0f && next <= along + len)
            {
                out.push_back (a + (b - a) * ((next - along) / len));
                next += spacing;
            }
        });
        return out;
    }

    Prepared build (const Drawing& drawing, int style)
    {
        Prepared out;
        // Busy drawings (the sampler's waveform) only get hardware on their long runs
        const float minLength = drawing.count > 30 ? 90.0f : 34.0f;
        bool havePrev = false;
        juce::Point<float> prevEnd;

        for (int i = 0; i < drawing.count; ++i)
        {
            const auto& e = drawing.elements[i];
            Path p;
            if (e.shape == 'P')      p = juce::Drawable::parseSVGPath (e.d);
            else if (e.shape == 'C') p.addEllipse (e.v[0] - e.v[2], e.v[1] - e.v[2], e.v[2] * 2.0f, e.v[2] * 2.0f);
            else                     p.addRoundedRectangle (e.v[0], e.v[1], e.v[2], e.v[3], e.v[4]);
            p.applyTransform (AffineTransform (e.m[0], e.m[2], e.m[4], e.m[1], e.m[3], e.m[5]));

            Tube t;
            t.colour = e.cls == 'b' ? 1 : e.cls == 'c' ? 2 : 0;
            t.panel = e.cls == 'f';
            t.live = (e.flags & spins) ? Tube::spin : (e.flags & needle) ? Tube::needleHand : Tube::fixed;
            t.bulbs = style == Wall::marquee && e.cls == 'b';
            const bool isDashed = e.dash[0] > 0.0f || e.dash[1] > 0.0f;
            const bool closed = (e.flags & closedShape) != 0;
            const float length = p.getLength();

            if (t.bulbs)
                t.dots = dotsAlong (p, 6.3f);
            else if (isDashed)
                t.path = dashed (p, e.dash[0], e.dash[1]);
            else
                t.path = p;

            const bool solid = ! t.bulbs && ! isDashed && t.live == Tube::fixed;
            if (solid && length >= minLength)
            {
                // Mounting clips: a short bar across the tube every so often
                const int n = juce::roundToInt (length / 58.0f);
                for (int k = 0; k < n; ++k)
                {
                    const float l = length * ((float) k + 0.5f) / (float) n;
                    const auto a = p.getPointAlongPath (l), b = p.getPointAlongPath (juce::jmin (length, l + 1.0f));
                    auto d = b - a;
                    const float m = juce::jmax (0.001f, d.getDistanceFromOrigin());
                    const juce::Point<float> nrm (-d.y / m * 3.7f, d.x / m * 3.7f);
                    out.clips.startNewSubPath (a - nrm);
                    out.clips.lineTo (a + nrm);
                }

                if (! closed)
                {
                    const auto a = p.getPointAlongPath (0.0f), b = p.getPointAlongPath (length);
                    t.tips.startNewSubPath (a);
                    t.tips.lineTo (p.getPointAlongPath (2.5f));
                    t.tips.startNewSubPath (p.getPointAlongPath (length - 2.5f));
                    t.tips.lineTo (b);

                    // A jumper wire sags from the last tube's end to the start of this one when they are close
                    if (havePrev)
                    {
                        const float dist = a.getDistanceFrom (prevEnd);
                        if (dist > 7.0f && dist < 44.0f)
                        {
                            out.wires.startNewSubPath (prevEnd);
                            out.wires.quadraticTo ((prevEnd.x + a.x) * 0.5f, (prevEnd.y + a.y) * 0.5f + dist * 0.4f, a.x, a.y);
                        }
                    }
                    prevEnd = b;
                    havePrev = true;
                }
            }

            out.hasLive = out.hasLive || t.live != Tube::fixed;
            out.tubes.push_back (std::move (t));
        }
        return out;
    }

    AffineTransform liveTransform (const Tube& t, float spinAngle, float needleAngle)
    {
        if (t.live == Tube::spin)       return AffineTransform::rotation (spinAngle, 100.0f, 70.0f);
        if (t.live == Tube::needleHand) return AffineTransform::rotation (needleAngle, 100.0f, 108.0f);
        return {};
    }

    void dots (juce::Graphics& g, const Tube& t, const AffineTransform& tr, float radius, juce::Point<float> offset = {})
    {
        for (auto d : t.dots)
        {
            d.applyTransform (tr);
            g.fillEllipse (d.x + offset.x - radius, d.y + offset.y - radius, radius * 2.0f, radius * 2.0f);
        }
    }

    Colour glass (Colour c1) { return Colour (0xff43454c).interpolatedWith (c1, 0.13f); }
    Colour body (Colour c)   { return c.interpolatedWith (juce::Colours::white, 0.18f); }
    Colour hot (Colour c)    { return c.interpolatedWith (juce::Colours::white, 0.78f); }

    juce::Typeface::Ptr typefaceFor (int style)
    {
        static juce::Typeface::Ptr faces[Wall::numStyles] = {
            juce::Typeface::createSystemTypefaceFor (BinaryData::SedgwickAveDisplayRegular_ttf, BinaryData::SedgwickAveDisplayRegular_ttfSize),
            juce::Typeface::createSystemTypefaceFor (BinaryData::MonotonRegular_ttf, BinaryData::MonotonRegular_ttfSize),
            juce::Typeface::createSystemTypefaceFor (BinaryData::BowlbyOneRegular_ttf, BinaryData::BowlbyOneRegular_ttfSize) };
        return faces[juce::jlimit (0, (int) Wall::numStyles - 1, style)];
    }
}

const Prepared& prepared (int deviceIndex, int style)
{
    static std::unique_ptr<Prepared> cache[Wall::numDevices][Wall::numStyles];
    deviceIndex = juce::jlimit (0, Wall::numDevices - 1, deviceIndex);
    style = juce::jlimit (0, (int) Wall::numStyles - 1, style);
    auto& slot = cache[deviceIndex][style];
    if (slot == nullptr)
        slot = std::make_unique<Prepared> (build (wallArtTable[deviceIndex].styles[style], style));
    return *slot;
}

Colours coloursFor (int deviceIndex, int style)
{
    auto& th = Theme::get();
    if (th.isRainbow())
    {
        const auto c = Wall::rainbowColours (deviceIndex, style);
        return { { Colour (c[0]), Colour (c[1]), Colour (c[2]) } };
    }
    const auto t = th.themeColour();
    return { { t, t.interpolatedWith (juce::Colours::white, 0.45f), t.interpolatedWith (juce::Colours::white, 0.22f) } };
}

AffineTransform artTransform (int style, float w)
{
    const float h = w * Wall::aspect;
    // Insets of the drawing inside the piece: top, sides, bottom
    const float top = style == Wall::tubes ? 0.02f : 0.0f;
    const float side = style == Wall::tubes ? 0.08f : style == Wall::marquee ? 0.12f : 0.04f;
    const float bottom = style == Wall::tubes ? 0.22f : style == Wall::marquee ? 0.27f : 0.0f;
    const juce::Rectangle<float> box (w * side, h * top, w * (1.0f - 2.0f * side), h * (1.0f - top - bottom));
    const float s = juce::jmin (box.getWidth() / 200.0f, box.getHeight() / 140.0f);
    return AffineTransform::scale (s).translated (box.getCentreX() - 100.0f * s, box.getCentreY() - 70.0f * s);
}

Lettering lettering (int style, const juce::String& title, float w)
{
    Lettering out;
    const float h = w * Wall::aspect;
    const float n = (float) juce::jmax (4, title.length());
    const auto text = style == Wall::tubes ? title : title.toUpperCase();

    // CSS-style font size (the em), which JUCE's "height" is a little taller than
    out.em = style == Wall::tubes ? juce::jmin (w * 0.27f, w * 1.5f / n)
           : style == Wall::marquee ? juce::jmin (w * 0.2f, w * 1.02f / n)
                                    : juce::jmin (w * 0.3f, w * 1.24f / n);

    juce::Font font (juce::FontOptions (typefaceFor (style)).withHeight (100.0f));
    const float heightPerEm = font.getHeight() / juce::jmax (1.0f, font.getHeightInPoints());
    font = font.withHeight (out.em * heightPerEm);

    juce::GlyphArrangement ga;
    ga.addLineOfText (font, text, 0.0f, 0.0f);            // baseline at y = 0
    Path glyphs;
    ga.createPath (glyphs);
    const float width = ga.getBoundingBox (0, -1, true).getWidth();
    const float em = out.em;

    if (style == Wall::tubes)
    {
        // Bottom-left of the line box sits near the piece's bottom-left corner; the tag leans up to the right
        const juce::Point<float> origin (w * 0.03f, h * 0.99f);
        out.glyphs = glyphs;
        out.glyphs.applyTransform (AffineTransform::translation (0.0f, -em * 0.27f)
                                       .followedBy (AffineTransform::rotation (juce::degreesToRadians (-5.0f)))
                                       .translated (origin));
    }
    else if (style == Wall::marquee)
    {
        out.glyphs = glyphs;
        out.glyphs.applyTransform (AffineTransform::translation ((w - width) * 0.5f, h * 0.97f - em * 0.2f));
    }
    else
    {
        const juce::Point<float> origin (w * 0.04f, h * 0.85f);
        const auto lean = AffineTransform::rotation (juce::degreesToRadians (-4.0f))
                              .sheared (std::tan (juce::degreesToRadians (-9.0f)), 0.0f)
                              .translated (origin);
        out.glyphs = glyphs;
        out.glyphs.applyTransform (AffineTransform::translation (0.0f, -em * 0.2f).followedBy (lean));
        out.extrusion = glyphs;
        out.extrusion.applyTransform (AffineTransform::translation (em * 0.075f, -em * 0.2f + em * 0.075f).followedBy (lean));
    }
    return out;
}

void drawTubes (juce::Graphics& g, const Prepared& art, const Colours& col, const AffineTransform& toTarget, float unit,
                bool lit, Part part, float spinAngle, float needleAngle)
{
    const bool wantLive = part == Part::live;
    struct Item { const Tube* tube; Path path; AffineTransform tr; };
    std::vector<Item> items;
    for (const auto& t : art.tubes)
    {
        if ((t.live != Tube::fixed) != wantLive)
            continue;
        const auto tr = liveTransform (t, spinAngle, needleAngle).followedBy (toTarget);
        auto p = t.path;
        p.applyTransform (tr);
        items.push_back ({ &t, std::move (p), tr });
    }

    // Shadow on the wall: the tube stands off the bricks, so it sits a little down and to the right
    const juce::Point<float> drop (1.8f * unit, 3.0f * unit);
    for (const auto& it : items)
    {
        if (it.tube->bulbs)
        {
            g.setColour (juce::Colours::black.withAlpha (lit ? 0.26f : 0.36f));
            dots (g, *it.tube, it.tr, 1.9f * unit, drop);
            continue;
        }
        auto p = it.path;
        p.applyTransform (AffineTransform::translation (drop));
        g.setColour (juce::Colours::black.withAlpha (lit ? 0.10f : 0.14f));
        g.strokePath (p, stroke (5.4f * unit));
        g.setColour (juce::Colours::black.withAlpha (lit ? 0.20f : 0.30f));
        g.strokePath (p, stroke (3.2f * unit));
    }

    if (! wantLive)
    {
        auto clips = art.clips, wires = art.wires;
        clips.applyTransform (toTarget);
        wires.applyTransform (toTarget);
        g.setColour (Colour (0xff2c2d32));
        g.strokePath (wires, stroke (1.0f * unit));
        g.setColour (Colour (0xff70737c));
        g.strokePath (clips, stroke (1.3f * unit));
    }

    // The glass, full of glowing gas when lit
    for (const auto& it : items)
    {
        const auto c = col.c[it.tube->colour];
        if (it.tube->panel)
        {
            g.setColour (col.c[0].withAlpha (lit ? 0.17f : 0.04f));
            g.fillPath (it.path);
        }
        g.setColour (lit ? body (c) : glass (col.c[0]));
        if (it.tube->bulbs) dots (g, *it.tube, it.tr, 1.8f * unit);
        else                g.strokePath (it.path, stroke (3.0f * unit));
    }

    if (lit)
    {
        // White-hot core
        for (const auto& it : items)
        {
            g.setColour (hot (col.c[it.tube->colour]));
            if (it.tube->bulbs) dots (g, *it.tube, it.tr, 0.85f * unit);
            else                g.strokePath (it.path, stroke (1.05f * unit));
        }
        // Open tubes dim a touch right at the tip
        g.setColour (juce::Colours::black.withAlpha (0.26f));
        for (const auto& it : items)
            if (! it.tube->tips.isEmpty())
            {
                auto tips = it.tube->tips;
                tips.applyTransform (it.tr);
                g.strokePath (tips, stroke (3.1f * unit));
            }
    }

    // Highlight along the top-left of the glass
    const juce::Point<float> shine (-0.55f * unit, -0.8f * unit);
    g.setColour (juce::Colours::white.withAlpha (lit ? 0.55f : 0.32f));
    for (const auto& it : items)
    {
        if (it.tube->bulbs) { dots (g, *it.tube, it.tr, 0.45f * unit, shine); continue; }
        auto p = it.path;
        p.applyTransform (AffineTransform::translation (shine));
        g.strokePath (p, stroke (0.45f * unit));
    }
}

void drawGlowSource (juce::Graphics& g, const Prepared& art, const Colours& col, const AffineTransform& toTarget, float unit, float width)
{
    for (const auto& t : art.tubes)
    {
        if (t.live != Tube::fixed)
            continue;
        g.setColour (col.c[t.colour]);
        if (t.bulbs) { dots (g, t, toTarget, width * 0.65f * unit); continue; }
        auto p = t.path;
        p.applyTransform (toTarget);
        g.strokePath (p, stroke (width * unit));
    }
}

void drawLiveGlow (juce::Graphics& g, const Prepared& art, const Colours& col, const AffineTransform& toTarget, float unit,
                   float amount, float spinAngle, float needleAngle)
{
    for (const auto& t : art.tubes)
    {
        if (t.live == Tube::fixed || t.bulbs)
            continue;
        auto p = t.path;
        p.applyTransform (liveTransform (t, spinAngle, needleAngle).followedBy (toTarget));
        const auto c = col.c[t.colour];
        g.setColour (c.withAlpha (juce::jlimit (0.0f, 1.0f, 0.07f * amount)));
        g.strokePath (p, stroke (13.0f * unit));
        g.setColour (c.withAlpha (juce::jlimit (0.0f, 1.0f, 0.13f * amount)));
        g.strokePath (p, stroke (8.5f * unit));
        g.setColour (c.withAlpha (juce::jlimit (0.0f, 1.0f, 0.22f * amount)));
        g.strokePath (p, stroke (5.5f * unit));
    }
}

juce::Rectangle<float> liveBounds (const Prepared& art, const AffineTransform& toTarget, float unit, float spinAngle, float needleAngle)
{
    juce::Rectangle<float> all;
    bool any = false;
    auto take = [&] (juce::Rectangle<float> r) { all = any ? all.getUnion (r) : r; any = true; };
    for (const auto& t : art.tubes)
    {
        if (t.live == Tube::fixed)
            continue;
        const auto tr = liveTransform (t, spinAngle, needleAngle).followedBy (toTarget);
        if (! t.path.isEmpty())
            take (t.path.getBoundsTransformed (tr));
        for (auto d : t.dots)
        {
            d.applyTransform (tr);
            take ({ d.x, d.y, 0.0f, 0.0f });
        }
    }
    return any ? all.expanded (11.0f * unit) : juce::Rectangle<float>();
}

void drawFlicker (juce::Graphics& g, const Prepared& art, int tubeIndex, const Colours& col, const AffineTransform& toTarget, float unit, float amount)
{
    if (tubeIndex < 0 || tubeIndex >= (int) art.tubes.size() || amount <= 0.0f)
        return;
    const auto& t = art.tubes[(size_t) tubeIndex];
    if (t.live != Tube::fixed)
        return;
    g.setColour (glass (col.c[0]).withAlpha (juce::jlimit (0.0f, 1.0f, amount)));
    if (t.bulbs) { dots (g, t, toTarget, 1.9f * unit); return; }
    auto p = t.path;
    p.applyTransform (toTarget);
    g.strokePath (p, stroke (3.2f * unit));
}

void drawLettering (juce::Graphics& g, const Lettering& l, int style, const Colours& col, const AffineTransform& toTarget, float px, bool lit)
{
    auto glyphs = l.glyphs;
    glyphs.applyTransform (toTarget);
    const float em = l.em * px;
    const auto c1 = col.c[0];

    if (style == Wall::marquee)
    {
        // Sign lettering: the typeface is already made of tube lines, so it is simply filled
        auto shadow = glyphs;
        shadow.applyTransform (AffineTransform::translation (1.8f * px, 3.0f * px));
        g.setColour (juce::Colours::black.withAlpha (lit ? 0.22f : 0.32f));
        g.fillPath (shadow);
        g.setColour (lit ? col.c[2].interpolatedWith (juce::Colours::white, 0.55f) : Colour (0xff3d3f46));
        g.fillPath (glyphs);
        return;
    }

    const float outline = juce::jmax ((style == Wall::tubes ? 1.6f : 1.5f) * px, em * (style == Wall::tubes ? 0.05f : 0.035f));
    auto shadow = glyphs;
    shadow.applyTransform (AffineTransform::translation (1.8f * px, 3.0f * px));
    g.setColour (juce::Colours::black.withAlpha (lit ? 0.18f : 0.28f));
    g.strokePath (shadow, stroke (outline * 1.15f));

    if (style == Wall::block)
    {
        auto ex = l.extrusion;
        ex.applyTransform (toTarget);
        g.setColour (lit ? body (col.c[1]) : Colour (0xff3d3f46).withAlpha (0.6f));
        g.strokePath (ex, stroke (juce::jmax (1.2f * px, em * 0.026f)));
        if (lit)
        {
            g.setColour (c1.withAlpha (0.16f));
            g.fillPath (glyphs);
        }
    }

    g.setColour (lit ? body (c1) : glass (c1));
    g.strokePath (glyphs, stroke (outline));

    if (lit)
    {
        g.setColour (hot (c1));
        g.strokePath (glyphs, stroke (juce::jmax (0.55f * px, em * 0.014f)));
    }
    else
    {
        auto shine = glyphs;
        shine.applyTransform (AffineTransform::translation (-em * 0.012f, -em * 0.018f));
        g.setColour (juce::Colours::white.withAlpha (0.3f));
        g.strokePath (shine, stroke (juce::jmax (0.4f * px, em * 0.008f)));
    }
}

void drawLetteringGlowSource (juce::Graphics& g, const Lettering& l, int style, const Colours& col, const AffineTransform& toTarget, float)
{
    auto glyphs = l.glyphs;
    glyphs.applyTransform (toTarget);
    if (style == Wall::block)
    {
        auto ex = l.extrusion;
        ex.applyTransform (toTarget);
        g.setColour (col.c[1].withAlpha (0.55f));
        g.fillPath (ex);
    }
    // The whole letter shape glows, not just its outline, as a lit sign washes its own backing
    g.setColour (col.c[style == Wall::marquee ? 2 : 0].withAlpha (style == Wall::block ? 0.7f : 0.85f));
    g.fillPath (glyphs);
}

//==============================================================================
namespace
{
    void boxPass (juce::Image::BitmapData& data, int radius, bool horizontal, std::vector<juce::uint8>& line)
    {
        const int length = horizontal ? data.width : data.height;
        const int lines = horizontal ? data.height : data.width;
        const int step = horizontal ? data.pixelStride : data.lineStride;
        const int window = radius * 2 + 1;
        line.resize ((size_t) length * 4);

        for (int n = 0; n < lines; ++n)
        {
            juce::uint8* first = horizontal ? data.getLinePointer (n) : data.getPixelPointer (n, 0);
            for (int i = 0; i < length; ++i)
                std::memcpy (line.data() + (size_t) i * 4, first + (size_t) i * (size_t) step, 4);

            int sum[4] = { 0, 0, 0, 0 };
            for (int i = 0; i <= juce::jmin (radius, length - 1); ++i)
                for (int c = 0; c < 4; ++c)
                    sum[c] += line[(size_t) i * 4 + (size_t) c];

            // Dividing by the window is the slow part: a multiply and a shift does the same
            const juce::uint64 inverse = (juce::uint64) ((1u << 24) / (juce::uint32) window) + 1u;
            for (int i = 0; i < length; ++i)
            {
                juce::uint8* out = first + (size_t) i * (size_t) step;
                for (int c = 0; c < 4; ++c)
                    out[c] = (juce::uint8) (((juce::uint64) sum[c] * inverse) >> 24);
                const int add = i + radius + 1, drop = i - radius;
                if (add < length)  for (int c = 0; c < 4; ++c) sum[c] += line[(size_t) add * 4 + (size_t) c];
                if (drop >= 0)     for (int c = 0; c < 4; ++c) sum[c] -= line[(size_t) drop * 4 + (size_t) c];
            }
        }
    }
}

void blur (juce::Image& image, float sigma)
{
    if (! image.isValid() || image.getFormat() != juce::Image::ARGB || sigma < 0.4f)
        return;
    const int radius = juce::jmax (1, juce::roundToInt ((std::sqrt (4.0f * sigma * sigma + 1.0f) - 1.0f) * 0.5f));
    juce::Image::BitmapData data (image, juce::Image::BitmapData::readWrite);
    std::vector<juce::uint8> line;
    for (int pass = 0; pass < 3; ++pass)
    {
        boxPass (data, radius, true, line);
        boxPass (data, radius, false, line);
    }
}

juce::Image brickTile (float scale, bool lit)
{
    static const juce::uint32 dark[7] = { 0xff1c1b1e, 0xff201e20, 0xff19191c, 0xff221f21, 0xff1d1c20, 0xff18171a, 0xff1f1d1e };
    static const float take[7] = { 0.58f, 0.64f, 0.53f, 0.68f, 0.56f, 0.49f, 0.61f };     // how pale each brick is under light

    const int w = juce::jmax (8, juce::roundToInt (128.0f * scale)), h = juce::jmax (4, juce::roundToInt (64.0f * scale));
    juce::Image tile (juce::Image::ARGB, w, h, true);
    juce::Graphics g (tile);
    g.fillAll (lit ? juce::Colours::white.withAlpha (0.12f) : Colour (0xff0a0a0c));   // mortar
    const float sx = (float) w / 128.0f, sy = (float) h / 64.0f;

    for (int row = 0; row < 4; ++row)
        for (int i = -1; i < 2; ++i)
        {
            const int tone = (row * 2 + ((i + 2) % 2) + (row > 1 ? 3 : 0)) % 7;
            const juce::Rectangle<float> brick (((float) i * 64.0f + (row % 2 ? 33.0f : 1.0f)) * sx, ((float) row * 16.0f + 1.0f) * sy, 62.0f * sx, 14.0f * sy);
            g.setColour (lit ? juce::Colours::white.withAlpha (take[tone]) : Colour (dark[tone]));
            g.fillRoundedRectangle (brick, 1.5f * sx);
        }
    return tile;
}
}
