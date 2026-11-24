#pragma once

#include "AppContext.h"
#include "Widgets.h"
#include <map>

// The arranger timeline: ruler (click to move the playhead, drag the lower strip to set the loop),
// grid, clips and the playhead. Clips can be moved, resized, duplicated, copied and deleted, audio files
// can be dropped onto a lane, and the view zooms (Cmd + scroll) and scrolls.
//
// With automation shown, each track's chosen lane lies over its clips as a neon tube with a bulb on every
// point. Drag a point to move it, double-click to add or remove one, drag the small ring between two
// points up or down to bend that stretch, and drag across empty lane to select points: the box around
// them scales (top edge), tilts (top corners), squeezes in time (sides) or moves (inside). Clips are
// then moved by their name strip. When "automation sticks to clips" is on, moving, copying or pasting a
// clip takes its automation with it.
class ArrangerView : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    explicit ArrangerView (AppContext&);

    void tick();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int x, int y) override;

    void deleteSelectedClip();
    void duplicateSelectedClip();
    void copySelectedClip();
    void cutSelectedClip();
    void pasteClip();
    bool deleteSelectedPoints();          // true when there were points selected
    void clearPointSelection();

    std::function<void()> onOpenEditor;

private:
    enum class DragMode { None, Position, Loop, MoveClip, ResizeClip, Point, Curve, Band, Box };
    enum class BoxPart { None, Move, Top, TopLeft, TopRight, Left, Right };

    AppContext& ctx;
    double viewStart = 0.0;          // beats at the left edge
    double pixelsPerBeat = 0.0;      // set on first layout to fit 16 bars
    double lastPlayhead = -1.0;
    int shownPlayheadX = 0;
    float shownPulse = 0.0f;
    int shownAutoGeneration = -1;
    bool shownLanes = false;

    DragMode drag = DragMode::None;
    int dragTrack = -1, dragClip = -1;
    double dragGrabOffset = 0.0, loopAnchor = 0.0;

    // A clip being dragged carries its automation
    struct Carry
    {
        bool active = false, copy = false;
        int fromTrack = -1;
        double a0 = 0.0, b0 = 0.0;
        std::vector<AutoLane> fromSnap, fromBase, carried;
        std::map<int, std::vector<AutoLane>> destSnaps;
    } carry;
    void startCarry (int track, int clip, bool copy);
    void updateCarry (int track, double start);

    // Points: the selection is on one lane
    int selTrack = -100;
    AutoTarget selTarget;
    std::vector<int> selection;
    int dragPoint = -1, dragSegment = -1;
    BoxPart boxPart = BoxPart::None;
    std::vector<AutoPoint> dragFrom;       // the lane as the drag found it
    std::vector<int> dragSel;
    juce::Point<float> dragStartPos;
    juce::Rectangle<float> band;
    int hoverRow = -1;

    float beatToX (double beat) const { return (float) ((beat - viewStart) * pixelsPerBeat); }
    double xToBeat (float x) const    { return viewStart + x / pixelsPerBeat; }
    double snap() const               { return pixelsPerBeat >= 12.0 ? 1.0 : beatsPerBar; }
    double snapBeat (double beat) const { return std::round (beat / snap()) * snap(); }
    double fineSnap (double beat, bool free) const;      // points: a sixteenth when zoomed in, free with Shift
    int rowH() const { return ctx.rowHeight(); }
    int rowTop (int row) const { return Layout::rulerHeight + row * rowH(); }
    int rowAtY (int y) const;                            // -1 above or below the rows
    int trackAtY (int y) const;                          // song tracks only
    bool hitClip (juce::Point<int>, int& track, int& clip, bool& onRightEdge) const;
    juce::Rectangle<float> clipBounds (int track, const Clip&) const;
    void drawClipContent (juce::Graphics&, const Clip&, juce::Rectangle<float>, juce::Colour, float alpha, bool automation) const;
    void showClipMenu (int track, int clip);

    // Lanes
    bool lanesOn() const { return ctx.lanesShown; }
    float laneTop (int row) const    { return (float) rowTop (row) + 17.0f; }
    float laneBottom (int row) const { return (float) (rowTop (row) + rowH()) - 6.0f; }
    float valueToY (int row, float v) const { return laneBottom (row) - v * (laneBottom (row) - laneTop (row)); }
    float yToValue (int row, float y) const { return juce::jlimit (0.0f, 1.0f, (laneBottom (row) - y) / (laneBottom (row) - laneTop (row))); }
    juce::Colour laneColour (int track) const;
    AutoLane* shownLane (int track);                     // null when the shown control has no lane yet
    AutoLane& makeShownLane (int track);
    int pointAt (int row, juce::Point<float>) const;
    int curveHandleAt (int row, juce::Point<float>) const;
    juce::Point<float> curveHandle (int row, const std::vector<AutoPoint>&, int segment) const;
    juce::Rectangle<float> selectionBox (int row) const;
    BoxPart boxPartAt (int row, juce::Point<float>) const;
    bool onClipStrip (juce::Point<int>, int& track, int& clip, bool& edge) const;
    void drawLane (juce::Graphics&, int row, float width);
    void applyBox (juce::Point<float> to, bool free);
    void showLaneMenu (int track, juce::Point<int>);
    void lanesEdited() { ctx.automationChanged(); }
};
