#include "Icons.h"
#include "../Theme.h"

namespace
{
    using juce::Path;

    Path svg (const char* d) { return juce::Drawable::parseSVGPath (d); }

    Path circle (float cx, float cy, float r)
    {
        Path p;
        p.addEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f);
        return p;
    }

    Path rect (float x, float y, float w, float h, float r)
    {
        Path p;
        p.addRoundedRectangle (x, y, w, h, r);
        return p;
    }

    Path dashed (const Path& p, float dash, float gap, float width)
    {
        Path out;
        const float pattern[] = { dash, gap };
        juce::PathStrokeType (width).createDashedStroke (out, p, pattern, 2);
        return out;
    }

    // Each icon is a list of layers: path, alpha, whether it is a filled dot.
    struct Layer { Path path; float alpha = 1.0f; bool fill = false; };

    std::vector<Layer> layersFor (Icon icon)
    {
        switch (icon)
        {
            case Icon::Sampler: return {
                { rect (7, 15, 50, 34, 6) },
                { svg ("M11 32h4l2-8 3 16 3-12 3 8 2-4h3l2-11 3 21 3-15 2 5h3l2-3 2 3h3") },
                { svg ("M24 18v28M40 18v28"), 0.55f },
                { svg ("M14 56h36"), 0.4f },
                { circle (24, 10, 2.5f), 0.6f, true } };
            case Icon::Synth: return {
                { svg ("M5 30c6-13 10-13 14 0s8 13 14 0 8-13 14 0 8 13 12 3") },
                { svg ("M5 40c6-9 10-9 14 0s8 9 14 0 8-9 14 0 8 9 12 2"), 0.6f },
                { svg ("M5 21c6-15 10-15 14 0s8 15 14 0 8-15 14 0"), 0.4f },
                { circle (32, 54, 5) },
                { svg ("M32 49v4") } };
            case Icon::Drums: return {
                { rect (8, 22, 48, 36, 5) },
                { rect (13, 27, 10, 10, 2), 0.35f, true }, { rect (27, 27, 10, 10, 2) }, { rect (41, 27, 10, 10, 2) },
                { rect (13, 42, 10, 10, 2) }, { rect (27, 42, 10, 10, 2), 0.35f, true }, { rect (41, 42, 10, 10, 2) },
                { circle (32, 11, 6) },
                { svg ("M32 11l3.5-3.5") } };
            case Icon::Filter: return {
                { svg ("M5 24h26c7 0 8-13 13-13s5 17 8 28 4 15 7 17") },
                { svg ("M44 6v52"), 0.55f },
                { circle (44, 11, 3.5f), 0.5f, true },
                { svg ("M5 57h54"), 0.4f } };
            case Icon::Delay: return {
                { circle (13, 32, 6) },
                { svg ("M25 20a17 17 0 0 1 0 24") },
                { svg ("M35 14a25 25 0 0 1 0 36"), 0.6f },
                { svg ("M45 8a33 33 0 0 1 0 48"), 0.4f },
                { circle (54, 24, 1.8f), 1.0f, true }, { circle (57, 32, 1.8f), 1.0f, true }, { circle (54, 40, 1.8f), 1.0f, true } };
            case Icon::Reverb: return {
                { circle (32, 32, 4), 0.5f, true },
                { circle (32, 32, 11) },
                { circle (32, 32, 19), 0.7f },
                { circle (32, 32, 27), 0.45f },
                { svg ("M32 2v6M32 56v6M2 32h6M56 32h6"), 0.6f } };
            case Icon::EQ: return {
                { svg ("M5 38c8 0 8-14 16-14s8 13 13 13 6-21 14-21 8 10 11 10") },
                { circle (21, 24, 3.5f), 0.5f, true }, { circle (34, 37, 3.5f), 0.5f, true }, { circle (48, 16, 3.5f), 0.5f, true },
                { svg ("M5 54h54"), 0.4f },
                { svg ("M14 50v8M27 50v8M40 50v8M53 50v8"), 0.6f } };
            case Icon::Compressor: return {
                { svg ("M9 44a23 23 0 0 1 46 0") },
                { svg ("M13 33l4 2M20 24l3 3M32 20v5M44 24l-3 3M51 33l-4 2"), 0.6f },
                { svg ("M32 44l11-15") },
                { circle (32, 44, 3.5f), 0.5f, true },
                { svg ("M14 54h36"), 0.5f } };
            case Icon::LFO: return {
                { svg ("M4 32h56"), 0.3f },
                { svg ("M4 32c7-17 13-17 20 0s13 17 20 0 12-17 16-8") },
                { circle (44, 32, 3.5f), 0.5f, true },
                { circle (44, 32, 8), 0.6f } };
            case Icon::Pencil: return {
                { svg ("M14 50l4-14 26-26 10 10-26 26z") },
                { svg ("M40 14l10 10M18 36l10 10"), 0.6f },
                { svg ("M8 58h48"), 0.4f } };
            case Icon::Cursor: return {
                { svg ("M18 10l30 22-13 3 8 15-6 3-8-15-11 10z") },
                { circle (18, 10, 7), 0.4f } };
            case Icon::Scissors: return {
                { circle (16, 46, 7) }, { circle (16, 18, 7) },
                { svg ("M22 42L54 14M22 22l32 28") },
                { svg ("M34 32h24"), 0.55f } };
            case Icon::Velocity: return {
                { svg ("M12 54V30M24 54V18M36 54V36M48 54V12") },
                { circle (12, 30, 3), 0.5f, true }, { circle (24, 18, 3), 0.5f, true },
                { circle (36, 36, 3), 0.5f, true }, { circle (48, 12, 3), 0.5f, true },
                { svg ("M6 58h52"), 0.4f } };
            case Icon::Play: return {
                { circle (32, 32, 29), 0.55f },
                { svg ("M23 16L48 32L23 48Z") },
                { svg ("M28 25L38 32L28 39Z"), 0.6f } };
            case Icon::Stop: return {
                { circle (32, 32, 29), 0.55f },
                { rect (17, 17, 30, 30, 5) },
                { rect (24.5f, 24.5f, 15, 15, 2), 0.6f } };
            case Icon::Record: return {
                { circle (32, 32, 29), 0.55f },
                { circle (32, 32, 17) },
                { circle (32, 32, 9), 0.6f },
                { circle (32, 32, 3.5f), 1.0f, true } };
            case Icon::Loop: return {
                { svg ("M14 34a18 18 0 0 1 30-14l4 4") },
                { svg ("M50 30a18 18 0 0 1-30 14l-4-4") },
                { svg ("M48 14v10h-10M16 50V40h10"), 0.8f },
                { circle (32, 32, 3), 0.5f, true } };
            case Icon::Drive: return {
                { svg ("M22 54V22a10 10 0 0 1 20 0v32") },
                { svg ("M22 54h20"), 0.8f },
                { svg ("M28 46V26M36 46V26"), 0.5f },
                { circle (32, 18, 3), 0.5f, true },
                { svg ("M10 30c4 2 4 6 0 8M54 30c-4 2-4 6 0 8"), 0.6f },
                { svg ("M12 58h40"), 0.4f } };
            case Icon::Metronome: return {
                { svg ("M24 10h16l10 44H14z") },
                { svg ("M32 46l12-30"), 0.9f },
                { circle (40, 26, 3.5f), 0.6f, true },
                { svg ("M18 44h28"), 0.45f } };
        }
        return {};
    }

    bool dashedOuterRing (Icon icon, size_t layer)
    {
        return (icon == Icon::Play || icon == Icon::Stop || icon == Icon::Record) && layer == 0;
    }
}

namespace Glow
{
    void strokeGlow (juce::Graphics& g, const juce::Path& path, juce::Colour colour, float width, float intensity)
    {
        const juce::PathStrokeType::JointStyle joint = juce::PathStrokeType::curved;
        const juce::PathStrokeType::EndCapStyle cap = juce::PathStrokeType::rounded;

        g.setColour (colour.withAlpha (0.10f + 0.10f * intensity));
        g.strokePath (path, juce::PathStrokeType (width * 4.0f, joint, cap));
        g.setColour (colour.withAlpha (0.22f + 0.18f * intensity));
        g.strokePath (path, juce::PathStrokeType (width * 2.2f, joint, cap));
        g.setColour (colour);
        g.strokePath (path, juce::PathStrokeType (width, joint, cap));
    }

    void drawIcon (juce::Graphics& g, Icon icon, juce::Rectangle<float> area, juce::Colour colour, float intensity)
    {
        const float scale = juce::jmin (area.getWidth(), area.getHeight()) / 64.0f;
        const auto transform = juce::AffineTransform::scale (scale)
                                   .translated (area.getCentreX() - 32.0f * scale, area.getCentreY() - 32.0f * scale);
        const float width = juce::jmax (1.0f, 2.2f * scale);

        auto layers = layersFor (icon);
        for (size_t i = 0; i < layers.size(); ++i)
        {
            auto& layer = layers[i];
            auto p = layer.path;
            p.applyTransform (transform);
            const auto c = colour.withMultipliedAlpha (layer.alpha);

            if (layer.fill)
            {
                g.setColour (c.withAlpha (0.25f));
                g.fillPath (p, juce::AffineTransform());
                g.setColour (c);
                g.fillPath (p);
            }
            else if (dashedOuterRing (icon, i))
            {
                g.setColour (c);
                g.fillPath (dashed (p, 1.6f * scale, 3.6f * scale, 1.3f * scale));
            }
            else
            {
                strokeGlow (g, p, c, width, intensity);
            }
        }
    }

    void drawPanel (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour accent, float radius, float pulse)
    {
        auto& th = Theme::get();
        g.setColour (th.panel);
        g.fillRoundedRectangle (area, radius);

        if (pulse > 0.01f)
        {
            g.setColour (accent.withAlpha (0.10f * pulse));
            g.drawRoundedRectangle (area.expanded (2.0f), radius + 2.0f, 4.0f);
        }

        g.setColour (accent.withAlpha (0.60f));
        g.drawRoundedRectangle (area.reduced (0.5f), radius, 1.0f);
        g.setColour (accent.withAlpha (0.18f));
        g.drawRoundedRectangle (area.reduced (4.5f), juce::jmax (2.0f, radius - 4.0f), 1.0f);
    }

    void drawSpacedText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                         const juce::Font& font, float spacing, juce::Justification just)
    {
        juce::GlyphArrangement ga;
        ga.addLineOfText (font, text, 0.0f, 0.0f);
        for (int i = 0; i < ga.getNumGlyphs(); ++i)
            ga.getGlyph (i).moveBy (spacing * (float) i, 0.0f);

        const auto bounds = ga.getBoundingBox (0, -1, true);
        float x = area.getX();
        if (just.testFlags (juce::Justification::horizontallyCentred)) x = area.getCentreX() - bounds.getWidth() * 0.5f;
        else if (just.testFlags (juce::Justification::right))          x = area.getRight() - bounds.getWidth();
        const float y = area.getCentreY() - bounds.getHeight() * 0.5f - bounds.getY();

        ga.moveRangeOfGlyphs (0, -1, x - bounds.getX(), y);
        g.setFont (font);
        ga.draw (g);
    }
}
