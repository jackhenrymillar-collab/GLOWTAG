#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include "../Engine/Patch.h"
#include "../Engine/Sequencer.h"

// The device wall (hold Tab): what is on it and where each piece sits.
// Everything here is plain data so it can be saved with the app's settings and checked offline.
namespace Wall
{
    constexpr int numDevices = 21;
    constexpr float designWidth = 1600.0f;   // the wall is laid out on a sheet this wide, then scaled to the window
    constexpr float aspect = 0.7f;           // a piece is this tall for its width
    constexpr float minPieceWidth = 110.0f;

    enum class Kind { Instrument, Effect, Modulator, RackModule };
    enum Style { tubes = 0, marquee, block, numStyles };

    struct DeviceInfo
    {
        const char* id;
        const char* name;
        Kind kind;
        const char* detail;
        bool sequencer;              // a rack module (SeqType) rather than a patch device (ModuleType)
        ModuleType module;
        SeqType seq;
        juce::uint32 rainbow[3];     // style I colours in Rainbow mode
    };

    const DeviceInfo& device (int index);
    const char* kindName (Kind);
    const char* styleName (int style);

    // The three tube colours a device uses in Rainbow mode: style I mixed, II hot signage, III the graffiti wall
    std::array<juce::uint32, 3> rainbowColours (int deviceIndex, int style);

    struct Piece
    {
        float x = 0.0f, y = 0.0f, w = 200.0f;   // on the design sheet
        float rotation = 0.0f;                  // degrees
        int style = tubes;
        juce::String title;                     // empty: the device's own name
    };

    std::array<Piece, numDevices> defaultLayout();

    struct Model
    {
        std::array<Piece, numDevices> pieces = defaultLayout();
        bool alwaysBright = false;
        float glow = 1.0f;            // 0 .. 1.5
        float zoom = 1.0f;            // 0.3 .. 2, where 1 fills the window's width
        bool fitToScreen = false;

        void resetLayout();                                   // positions, sizes, titles and styles; keeps the view settings
        juce::String titleOf (int index) const;
        float height (int index) const { return pieces[(size_t) index].w * aspect; }
        float contentHeight() const;
        bool matches (int index, const juce::String& query) const;
        int firstMatch (const juce::String& query) const;     // -1 when nothing matches
        // The piece you reach from `from` by an arrow key (dx, dy is the direction); `from` when there is none
        int neighbour (int from, int dx, int dy, const juce::String& query) const;
        int commonStyle() const;                              // the style every piece shares, or -1

        juce::String toJson() const;
        bool fromJson (const juce::String&);
    };
}
