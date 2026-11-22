#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "DeviceWallModel.h"
#include "DeviceWallArt.h"
#include "../Theme.h"

// The device wall: hold Tab and every instrument, effect and rack module appears as a neon piece on a
// brick wall. Hover or use the arrows to pick one; click or Enter loads it on the selected track.
// Edit wall lets you drag pieces about, resize them, retitle them and choose each one's style.
class DeviceWall : public juce::Component
{
public:
    DeviceWall();

    // A device was chosen (index into Wall::device)
    std::function<void (int)> onLoad;
    // Name of the track a device would load on, for the confirmation line
    std::function<juce::String()> targetName;
    // The wall closed, or its saved state changed
    std::function<void()> onClosed;
    // Hand keyboard focus back to the window after typing a title
    std::function<void()> onReleaseFocus;

    // Tab: hold to show, let go to hide. A quick tap leaves it open until Tab or Escape.
    void tabPressed();
    void tabReleased();
    void open (bool stayOpen);
    void close();
    bool isOpen() const { return isVisible(); }

    // Keys while the wall is open. True when the key was used.
    bool keyUsed (const juce::KeyPress&);

    // 30 times a second from the main window; `pulse` is the engine's beat pulse (0..1)
    void tick (float pulse);
    // The same clock while the wall is closed: draws its pictures a few at a time so it opens at once
    void warm();

    juce::String state() const { return model.toJson(); }
    void restoreState (const juce::String& json);
    void themeChanged();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void visibilityChanged() override;

    // For checks and screenshots: put the wall in a given state without a keyboard
    void setEditing (bool);
    void setAlwaysBright (bool);
    void setAllStyles (int style);
    void select (int index);
    void zoomBy (float factor);
    void setFit (bool);

private:
    struct Cache
    {
        juce::Image unlit, lit, glow;
        juce::String key;                 // what the images were drawn for
        float margin = 0.0f;              // extra room round the piece, in screen pixels
        float glowScale = 0.5f;           // the glow image is drawn smaller and stretched
    };

    Wall::Model model;
    std::array<Cache, Wall::numDevices> cache;
    std::array<float, Wall::numDevices> grow {};      // 0..1 hover growth in Always bright

    int selected = 0;
    juce::String query;
    bool editing = false;
    bool latched = false;                 // stays open without Tab held
    bool tabDown = false;
    juce::uint32 tabDownAt = 0;
    bool openedByThisPress = false;
    juce::uint32 lastPaintEnd = 0;        // a slow first paint must not turn a quick tap into a hold
    float lastPaintMs = 0.0f;
    int warmTicks = 0;

    // View
    float scale = 1.0f;                   // design sheet -> screen
    juce::Point<float> origin;            // where the sheet's top left lands on screen
    juce::Point<float> scroll;
    juce::Rectangle<int> barArea, editArea, viewArea, footArea, findArea, swatchArea,
                         headingArea, styleLabelArea, zoomLabelArea, glowLabelArea, glowValueArea;

    // Animation
    float pulse = 0.0f;
    double spin = 0.0;                    // radians, for the parts that turn
    juce::uint32 struckAt = 0;            // when the selected piece lit up
    int flickerPiece = -1, flickerTube = -1;
    juce::uint32 flickerAt = 0, nextFlickerAt = 0;
    juce::String toast;
    juce::uint32 toastAt = 0;
    juce::RectangleList<int> lastDirty;   // what moved on the last tick, to be wiped on this one

    // Dragging in Edit mode
    enum class Drag { none, move, size };
    Drag drag = Drag::none;
    juce::Point<float> dragStart;
    Wall::Piece dragFrom;
    bool resetArmed = false;

    // Backdrop
    juce::Image wallImage, poolImage, litTile;
    bool poolsDirty = true;

    // The wall at rest is kept as one picture. Each repaint lays that down and redraws only the parts
    // that differ from it just now: the piece under the pointer, turning parts, a flickering tube.
    juce::Image frame, beatLayer;         // beatLayer: the extra light on the beat, laid over the frame
    bool frameDirty = true, beatDirty = true;
    float frameScale = 0.0f;              // display scale the frame was drawn for

    juce::TextButton styleButtons[Wall::numStyles], pieceStyleButtons[Wall::numStyles];
    juce::TextButton rainbowButton { "Rainbow" }, zoomOutButton { "-" }, zoomInButton { "+" }, zoomButton { "100%" }, fitButton { "Fit" },
                     brightButton { "Always bright" }, editButton { "Edit wall" }, resetButton { "Reset wall" }, doneButton { "Done" };
    juce::Slider glowSlider;
    juce::TextEditor titleEditor;

    void layoutView();                                // scale, origin and scroll limits from the model and window
    void placeTools();
    void refreshControls();
    void changed();                                   // the saved state changed
    void setZoom (float);
    void setQuery (const juce::String&);
    void load (int index);
    void showToast (const juce::String&);

    void invalidate();                                // the resting picture is out of date
    bool manyLit() const { return editing || model.alwaysBright; }
    bool isLit (int index) const;
    float marginOf (int index) const;
    juce::Rectangle<float> pieceBounds (int index, float growth) const; // everything a piece's images cover
    juce::Rectangle<float> liveBounds (int index) const;                // its moving parts, as they stand
    juce::Rectangle<float> poolReach (int index) const;                 // the light it throws on the wall
    juce::Rectangle<float> toastBounds() const;
    float needleAngle (bool lit) const;
    juce::AffineTransform imagePlacement (int index, float growth, float imageScale) const;
    void collectPatches (juce::RectangleList<int>&) const;
    void paintWall (juce::Graphics&, juce::Rectangle<int> area, bool resting);
    void rebuildFrame();
    void rebuildBeatLayer();
    juce::Rectangle<float> boxOnScreen (int index) const;               // the piece's unrotated box
    int pieceAt (juce::Point<float>) const;
    juce::Rectangle<float> gripFor (int index) const;
    juce::AffineTransform pieceToScreen (int index, float growth) const;
    bool ensureCache (int index, bool lit);           // true when it had to draw
    void paintPiece (juce::Graphics&, int index, juce::Rectangle<int> area, bool resting);
    void rebuildBackdrop();
    void rebuildPools();
    juce::Rectangle<float> swatchBounds (int index) const;
    float displayScale() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DeviceWall)
};
