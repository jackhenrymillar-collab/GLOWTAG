#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "DeviceWallModel.h"

// The device wall's neon: every device has three drawings (styles I, II, III), each a set of tubes in a
// 200 x 140 box, plus lettering. This turns them into layered neon: wall shadow, mounting clips and
// jumper wires, glow, the gas-filled glass, a hot core and a highlight along the glass.
namespace WallArt
{
    // The generated drawing data (DeviceWallArtData.inc, made by tools/wall-art/generate.mjs)
    struct Element { char shape; char cls; unsigned char flags; float dash[2]; float v[5]; const char* d; float m[6]; };
    struct Drawing { const Element* elements; int count; };
    struct Device { Drawing styles[Wall::numStyles]; };
    enum Flags { closedShape = 1, spins = 2, needle = 4 };

    struct Tube
    {
        juce::Path path;                          // centre line, already cut into dashes where the drawing is dashed
        int colour = 0;                           // which of the three tube colours
        bool panel = false;                       // a filled neon panel
        bool bulbs = false;                       // marquee bulbs: a row of dots instead of a tube
        std::vector<juce::Point<float>> dots;     // bulb centres
        juce::Path tips;                          // the last few units of an open tube, which dim slightly
        enum Live { fixed, spin, needleHand } live = fixed;   // parts that move are drawn fresh each frame
    };

    struct Prepared
    {
        std::vector<Tube> tubes;
        juce::Path clips, wires;                  // sign hardware worked out from the tube shapes
        bool hasLive = false;
    };

    const Prepared& prepared (int device, int style);

    struct Colours { juce::Colour c[3]; };
    Colours coloursFor (int device, int style);   // shades of the theme colour, or the style's Rainbow palette

    // Art box -> piece space (a piece is w wide and w * Wall::aspect tall, origin top left)
    juce::AffineTransform artTransform (int style, float w);

    struct Lettering
    {
        juce::Path glyphs;        // placed in piece space
        juce::Path extrusion;     // style III: the offset second outline
        float em = 0.0f;          // font size in piece space
    };
    Lettering lettering (int style, const juce::String& title, float w);

    enum class Part { fixed, live };

    // `toTarget` maps the art box to the target; `unit` is the size of one art unit there.
    void drawTubes (juce::Graphics&, const Prepared&, const Colours&, const juce::AffineTransform& toTarget, float unit,
                    bool lit, Part part, float spinAngle = 0.0f, float needleAngle = 0.0f);
    // What gets blurred into glow: `width` in art units (5 for the tight halo, 8 for the wide bloom)
    void drawGlowSource (juce::Graphics&, const Prepared&, const Colours&, const juce::AffineTransform& toTarget, float unit, float width);
    // Moving parts cannot use the cached blur, so they get a quick stroked glow
    void drawLiveGlow (juce::Graphics&, const Prepared&, const Colours&, const juce::AffineTransform& toTarget, float unit,
                       float amount, float spinAngle, float needleAngle);

    // Where the moving parts are at these angles, with room for their glow and shadow
    juce::Rectangle<float> liveBounds (const Prepared&, const juce::AffineTransform& toTarget, float unit, float spinAngle, float needleAngle);

    // A tube stuttering: paints one tube back towards dark glass by `amount` (0 none, 1 fully out)
    void drawFlicker (juce::Graphics&, const Prepared&, int tubeIndex, const Colours&, const juce::AffineTransform& toTarget, float unit, float amount);

    // `toTarget` maps piece space to the target; `px` is the size of one piece-space pixel there.
    void drawLettering (juce::Graphics&, const Lettering&, int style, const Colours&, const juce::AffineTransform& toTarget, float px, bool lit);
    void drawLetteringGlowSource (juce::Graphics&, const Lettering&, int style, const Colours&, const juce::AffineTransform& toTarget, float px);

    // Soft blur of an ARGB image in place (three box passes each way, close to a gaussian of this sigma)
    void blur (juce::Image&, float sigma);

    // One repeat of the brick wall, 128 x 64 at scale 1. The `lit` version is a mask whose alpha is how
    // strongly each brick takes the light a tube throws on it.
    juce::Image brickTile (float scale, bool lit);
}
