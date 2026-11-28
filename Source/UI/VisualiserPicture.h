#pragma once

#include <juce_graphics/juce_graphics.h>
#include <array>
#include <memory>
#include <vector>

// The picture the visualiser's Picture mode works on. A picture is read once, when it is dropped or chosen:
// its border tells us the ground. If the border is nearly one colour (or the picture has see-through parts)
// it is a shape on a plain ground, and the ground is cut away. Otherwise (a photo that fills its frame) the
// shape is whatever differs most from the picture's overall colour. The result never changes afterwards, so
// the drawing thread can share it freely.
namespace Vis
{
struct Picture
{
    static constexpr int size = 512, grid = 256;

    std::vector<juce::uint8> pixels;        // size x size, red green blue and how much each point belongs to the shape; bottom row first
    std::vector<float> distance;            // grid x grid pairs: how far outside the shape's outline, how far inside (in parts of the picture's longer side); bottom row first
    std::array<std::array<float, 3>, 3> palette {};     // the shape's three main colours, bright enough to see as smoke
    float aspect = 1.0f;                    // width over height of the picture as it was
    bool plain = true;                      // a shape on a plain ground (false: it fills its frame)
    bool lifted = false;                    // the shape is dark, so its smoke is lightened
    int sample = -1;                        // which of the built-in pictures it is, or -1 for one of the user's
    juce::Image small;                      // for the panel
    juce::Image whole;                      // squeezed to size x size, as it is kept in the song

    bool inside (float u, float v) const;   // is this point (0 to 1 across and up the picture) in the shape?

    static std::shared_ptr<const Picture> from (const juce::Image&, int sample = -1, float aspect = 0.0f);
    static std::shared_ptr<const Picture> builtIn (int which);
    static constexpr int numBuiltIn = 3;
    static const char* builtInName (int which);

    juce::var toVar() const;                // what the song keeps
    static std::shared_ptr<const Picture> fromVar (const juce::var&);
    static bool isPictureFile (const juce::String& path);
};
}
