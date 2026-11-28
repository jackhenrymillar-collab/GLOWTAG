#include "VisualiserPicture.h"

namespace Vis
{
static float clamp01 (float v) { return juce::jlimit (0.0f, 1.0f, v); }

bool Picture::isPictureFile (const juce::String& path)
{
    return juce::File (path).hasFileExtension ("png;jpg;jpeg;gif");
}

const char* Picture::builtInName (int which)
{
    static const char* const names[] = { "moon", "flower", "sunset" };
    return names[juce::jlimit (0, numBuiltIn - 1, which)];
}

bool Picture::inside (float u, float v) const
{
    const int x = juce::jlimit (0, grid - 1, (int) (u * (float) grid)), y = juce::jlimit (0, grid - 1, (int) (v * (float) grid));
    return distance[(size_t) (y * grid + x) * 2 + 1] > 0.0f;
}

std::shared_ptr<const Picture> Picture::from (const juce::Image& source, int sampleIndex, float aspectGiven)
{
    if (! source.isValid() || source.getWidth() < 2 || source.getHeight() < 2) return {};
    constexpr int n = size;
    auto pic = std::make_shared<Picture>();
    pic->sample = sampleIndex;
    pic->aspect = juce::jlimit (0.4f, 2.5f, aspectGiven > 0.0f ? aspectGiven : (float) source.getWidth() / (float) source.getHeight());

    // Squeezed to a square: the shaders stretch it back to its own shape
    juce::Image square (juce::Image::ARGB, n, n, true);
    {
        juce::Graphics g (square);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (source, juce::Rectangle<float> (0.0f, 0.0f, (float) n, (float) n), juce::RectanglePlacement::stretchToFit);
    }
    pic->whole = square;
    pic->small = square.rescaled (208, 208, juce::Graphics::highResamplingQuality);

    // d: red, green, blue, alpha for every point, top row first
    std::vector<juce::uint8> d ((size_t) n * n * 4);
    {
        const juce::Image::BitmapData bits (square, juce::Image::BitmapData::readOnly);
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x)
            {
                const auto c = bits.getPixelColour (x, y);
                auto* o = &d[(size_t) (y * n + x) * 4];
                o[0] = c.getRed(); o[1] = c.getGreen(); o[2] = c.getBlue(); o[3] = c.getAlpha();
            }
    }
    int clear = 0;
    for (size_t i = 3; i < d.size(); i += 4) if (d[i] < 128) ++clear;
    const bool alpha = clear > (int) ((float) (n * n) * 0.04f);

    // The border's average colour, and how much of the border is close to it
    const int m = juce::roundToInt ((float) n * 0.03f);
    std::vector<size_t> edge;
    double br = 0.0, bg = 0.0, bb = 0.0;
    for (int y = 0; y < n; y += 2)
        for (int x = 0; x < n; x += 2)
            if (x < m || y < m || x >= n - m || y >= n - m)
            {
                const size_t i = (size_t) (y * n + x) * 4;
                edge.push_back (i);
                br += d[i]; bg += d[i + 1]; bb += d[i + 2];
            }
    br /= (double) edge.size(); bg /= (double) edge.size(); bb /= (double) edge.size();
    auto dist = [&] (size_t i) { return (float) std::sqrt (juce::square ((double) d[i] - br) + juce::square ((double) d[i + 1] - bg) + juce::square ((double) d[i + 2] - bb)) / 255.0f; };
    int close = 0;
    for (auto i : edge) if (dist (i) < 0.14f) ++close;
    const bool plainGround = alpha || (float) close / (float) edge.size() > 0.8f;
    if (! plainGround)
    {
        br = bg = bb = 0.0;
        int k = 0;
        for (size_t i = 0; i < d.size(); i += 16) { br += d[i]; bg += d[i + 1]; bb += d[i + 2]; ++k; }
        br /= k; bg /= k; bb /= k;
    }

    pic->pixels.resize ((size_t) n * n * 4);
    auto& o = pic->pixels;
    using Rgb = std::array<float, 3>;
    std::vector<Rgb> shapeColours, allColours;
    double lightSum = 0.0;
    int lightCount = 0;
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
        {
            const size_t i = (size_t) (y * n + x) * 4, j = (size_t) ((n - 1 - y) * n + x) * 4;
            const float ds = dist (i);
            const float a = alpha ? (float) d[i + 3] / 255.0f : plainGround ? clamp01 ((ds - 0.1f) / 0.2f) : clamp01 ((ds - 0.12f) / 0.4f);
            if (a > 0.5f) { lightSum += juce::jmax (d[i], d[i + 1], d[i + 2]); ++lightCount; }
            if ((y & 7) == 0 && (x & 7) == 0)
            {
                const Rgb p { (float) d[i], (float) d[i + 1], (float) d[i + 2] };
                allColours.push_back (p);
                if (a > 0.5f) shapeColours.push_back (p);
            }
            o[j] = d[i]; o[j + 1] = d[i + 1]; o[j + 2] = d[i + 2]; o[j + 3] = (juce::uint8) juce::roundToInt (a * 255.0f);
        }
    pic->plain = plainGround;
    pic->lifted = plainGround && lightCount > 0 && lightSum / lightCount < 76.0;

    // The three main colours of the shape (or of the whole picture, if it fills its frame) colour the loose smoke
    {
        auto& s = plainGround && shapeColours.size() > 24 ? shapeColours : allColours;
        std::sort (s.begin(), s.end(), [] (const Rgb& a, const Rgb& b) { return a[0] * 3.0f + a[1] * 6.0f + a[2] < b[0] * 3.0f + b[1] * 6.0f + b[2]; });
        std::array<Rgb, 3> centres;
        static const float at[] = { 0.17f, 0.5f, 0.83f };
        for (size_t k = 0; k < 3; ++k) centres[k] = s[(size_t) (at[k] * (float) (s.size() - 1))];
        for (int it = 0; it < 8; ++it)
        {
            std::array<std::array<double, 4>, 3> sum {};
            for (auto& p : s)
            {
                size_t best = 0;
                float bestDist = 1.0e9f;
                for (size_t k = 0; k < 3; ++k)
                {
                    const float dd = juce::square (p[0] - centres[k][0]) + juce::square (p[1] - centres[k][1]) + juce::square (p[2] - centres[k][2]);
                    if (dd < bestDist) { bestDist = dd; best = k; }
                }
                sum[best][0] += p[0]; sum[best][1] += p[1]; sum[best][2] += p[2]; sum[best][3] += 1.0;
            }
            for (size_t k = 0; k < 3; ++k)
                if (sum[k][3] > 0.0) centres[k] = { (float) (sum[k][0] / sum[k][3]), (float) (sum[k][1] / sum[k][3]), (float) (sum[k][2] / sum[k][3]) };
        }
        for (size_t k = 0; k < 3; ++k)
        {
            const Rgb c { centres[k][0] / 255.0f, centres[k][1] / 255.0f, centres[k][2] / 255.0f };
            const float b = juce::jmax (c[0], c[1], c[2]), t = clamp01 ((b - 0.05f) / 0.15f), mix = t * t * (3.0f - 2.0f * t), gain = juce::jmax (1.0f, 0.7f / juce::jmax (b, 0.08f));
            static const float pale[] = { 0.7f, 0.74f, 0.8f };
            for (size_t q = 0; q < 3; ++q) pic->palette[k][q] = pale[q] + (c[q] * gain - pale[q]) * mix;
        }
    }

    // How far every point is from the shape's outline, on a coarser grid. If nothing stands out, the whole picture is the shape
    {
        constexpr int m2 = grid;
        std::vector<juce::uint8> ins ((size_t) m2 * m2);
        int count = 0;
        for (int i = 0; i < m2 * m2; ++i)
            if (o[(size_t) (((i / m2) * 2) * n + (i % m2) * 2) * 4 + 3] > 127) { ins[(size_t) i] = 1; ++count; }
        if ((float) count < (float) (m2 * m2) * 0.01f) std::fill (ins.begin(), ins.end(), (juce::uint8) 1);
        const float ax = pic->aspect >= 1.0f ? 1.0f : pic->aspect, ay = pic->aspect >= 1.0f ? 1.0f / pic->aspect : 1.0f;
        const float wx = ax / (float) m2, wy = ay / (float) m2, wd = std::hypot (wx, wy);
        auto spread = [&] (juce::uint8 from, bool border)
        {
            std::vector<float> D ((size_t) m2 * m2);
            for (int i = 0; i < m2 * m2; ++i)
            {
                const int yy = i / m2, xx = i % m2;
                D[(size_t) i] = ins[(size_t) i] == from ? 0.0f
                              : border ? juce::jmin (((float) xx + 0.5f) * wx, ((float) (m2 - xx) - 0.5f) * wx, ((float) yy + 0.5f) * wy, ((float) (m2 - yy) - 0.5f) * wy)
                                       : 1.0e9f;
            }
            for (int yy = 0; yy < m2; ++yy)
                for (int xx = 0; xx < m2; ++xx)
                {
                    const int i = yy * m2 + xx;
                    float v = D[(size_t) i];
                    if (xx > 0) v = juce::jmin (v, D[(size_t) (i - 1)] + wx);
                    if (yy > 0)
                    {
                        v = juce::jmin (v, D[(size_t) (i - m2)] + wy);
                        if (xx > 0) v = juce::jmin (v, D[(size_t) (i - m2 - 1)] + wd);
                        if (xx < m2 - 1) v = juce::jmin (v, D[(size_t) (i - m2 + 1)] + wd);
                    }
                    D[(size_t) i] = v;
                }
            for (int yy = m2 - 1; yy >= 0; --yy)
                for (int xx = m2 - 1; xx >= 0; --xx)
                {
                    const int i = yy * m2 + xx;
                    float v = D[(size_t) i];
                    if (xx < m2 - 1) v = juce::jmin (v, D[(size_t) (i + 1)] + wx);
                    if (yy < m2 - 1)
                    {
                        v = juce::jmin (v, D[(size_t) (i + m2)] + wy);
                        if (xx < m2 - 1) v = juce::jmin (v, D[(size_t) (i + m2 + 1)] + wd);
                        if (xx > 0) v = juce::jmin (v, D[(size_t) (i + m2 - 1)] + wd);
                    }
                    D[(size_t) i] = v;
                }
            return D;
        };
        const auto outside = spread (1, false), within = spread (0, true);
        std::vector<float> g ((size_t) m2 * m2), t ((size_t) m2 * m2);
        for (size_t i = 0; i < g.size(); ++i) g[i] = juce::jmin (4.0f, outside[i]) - juce::jmin (4.0f, within[i]);
        // The grid's stair steps are smoothed away, or they show as a comb along the rim of the shape
        for (int it = 0; it < 4; ++it)
        {
            for (int i = 0; i < m2 * m2; ++i)
            {
                const int xx = i % m2;
                t[(size_t) i] = (g[(size_t) (xx > 0 ? i - 1 : i)] + 2.0f * g[(size_t) i] + g[(size_t) (xx < m2 - 1 ? i + 1 : i)]) / 4.0f;
            }
            for (int i = 0; i < m2 * m2; ++i)
            {
                const int yy = i / m2;
                g[(size_t) i] = (t[(size_t) (yy > 0 ? i - m2 : i)] + 2.0f * t[(size_t) i] + t[(size_t) (yy < m2 - 1 ? i + m2 : i)]) / 4.0f;
            }
        }
        pic->distance.resize ((size_t) m2 * m2 * 2);
        for (size_t i = 0; i < g.size(); ++i)
        {
            pic->distance[i * 2] = juce::jmax (g[i], 0.0f);
            pic->distance[i * 2 + 1] = juce::jmax (-g[i], 0.0f);
        }
    }
    return pic;
}

// Three pictures to try: two shapes on a plain ground and one that fills its frame
std::shared_ptr<const Picture> Picture::builtIn (int which)
{
    which = juce::jlimit (0, numBuiltIn - 1, which);
    juce::Image image (juce::Image::ARGB, size, size, true);
    juce::Graphics g (image);
    const float pi = juce::MathConstants<float>::pi;
    auto circle = [&] (float cx, float cy, float r) { g.fillEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f); };
    if (which == 0)
    {
        g.fillAll (juce::Colour (0xff0c1236));
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xfffff3c4), 140.0f, 110.0f, juce::Colour (0xfff0a23a), 330.0f, 420.0f, false));
        circle (250.0f, 262.0f, 152.0f);
        g.setColour (juce::Colour (0xff0c1236));
        circle (318.0f, 220.0f, 130.0f);
        g.setColour (juce::Colour (0xffbfe0ff));
        static const float stars[4][3] = { { 396, 140, 20 }, { 436, 306, 12 }, { 112, 104, 11 }, { 372, 410, 9 } };
        for (auto& s : stars)
        {
            juce::Path p;
            for (int i = 0; i < 8; ++i)
            {
                const float a = (float) i * pi / 4.0f, rr = i % 2 ? s[2] * 0.3f : s[2];
                const juce::Point<float> pt (s[0] + std::sin (a) * rr, s[1] - std::cos (a) * rr);
                if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
            }
            p.closeSubPath();
            g.fillPath (p);
        }
    }
    else if (which == 1)
    {
        g.fillAll (juce::Colour (0xff10262e));
        auto petals = [&] (int count, float r0, float len, float wide, juce::uint32 c0, juce::uint32 c1, float phase)
        {
            for (int i = 0; i < count; ++i)
            {
                juce::Graphics::ScopedSaveState state (g);
                g.addTransform (juce::AffineTransform::rotation (phase + (float) i * 2.0f * pi / (float) count).translated (256.0f, 256.0f));
                g.setGradientFill (juce::ColourGradient (juce::Colour (c0), 0.0f, -r0, juce::Colour (c1), 0.0f, -r0 - len * 2.0f, false));
                g.fillEllipse (-wide, -r0 - len * 2.0f, wide * 2.0f, len * 2.0f);
            }
        };
        petals (8, 22.0f, 104.0f, 44.0f, 0xffffd27a, 0xffff3f86, 0.0f);
        petals (8, 14.0f, 62.0f, 28.0f, 0xfffff0a8, 0xffff8a2a, pi / 8.0f);
        g.setColour (juce::Colour (0xff55d6f2));
        circle (256.0f, 256.0f, 38.0f);
    }
    else
    {
        juce::ColourGradient sky (juce::Colour (0xff23104f), 0.0f, 0.0f, juce::Colour (0xffffb347), 0.0f, 336.0f, false);
        sky.addColour (0.55, juce::Colour (0xffb83268));
        g.setGradientFill (sky);
        g.fillRect (0, 0, 512, 336);
        g.setColour (juce::Colour (0xfffff1b8));
        circle (256.0f, 300.0f, 96.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a1b5e), 0.0f, 336.0f, juce::Colour (0xff140c30), 0.0f, 512.0f, false));
        g.fillRect (0, 336, 512, 176);
        g.setColour (juce::Colour (0xffffcf7a));
        static const float glints[5][2] = { { 352, 150 }, { 372, 116 }, { 394, 86 }, { 418, 58 }, { 446, 34 } };
        for (auto& r : glints) g.fillRoundedRectangle (256.0f - r[1] * 0.5f, r[0], r[1], 8.0f, 4.0f);
    }
    return from (image, which, 1.0f);
}

juce::var Picture::toVar() const
{
    auto* o = new juce::DynamicObject();
    if (sample >= 0) { o->setProperty ("sample", sample); return juce::var (o); }
    // A picture with see-through parts keeps them (PNG); a photo is kept smaller (JPEG)
    bool seeThrough = false;
    {
        const juce::Image::BitmapData bits (whole, juce::Image::BitmapData::readOnly);
        for (int y = 0; y < bits.height && ! seeThrough; y += 4)
            for (int x = 0; x < bits.width; x += 4)
                if (bits.getPixelColour (x, y).getAlpha() < 250) { seeThrough = true; break; }
    }
    juce::MemoryOutputStream data;
    if (seeThrough) { juce::PNGImageFormat png; png.writeImageToStream (whole, data); }
    else            { juce::JPEGImageFormat jpeg; jpeg.setQuality (0.9f); jpeg.writeImageToStream (whole.convertedToFormat (juce::Image::RGB), data); }
    o->setProperty ("aspect", aspect);
    o->setProperty ("data", juce::Base64::toBase64 (data.getData(), data.getDataSize()));
    return juce::var (o);
}

std::shared_ptr<const Picture> Picture::fromVar (const juce::var& v)
{
    if (! v.isObject()) return {};
    if (v.hasProperty ("sample")) return builtIn ((int) v["sample"]);
    juce::MemoryOutputStream data;
    if (! juce::Base64::convertFromBase64 (data, v["data"].toString())) return {};
    const auto image = juce::ImageFileFormat::loadFrom (data.getData(), data.getDataSize());
    return from (image, -1, (float) v["aspect"]);
}
}
