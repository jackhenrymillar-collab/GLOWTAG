#pragma once

#include "AppContext.h"
#include "Widgets.h"

// The arranger timeline: ruler (click to move the playhead, drag the lower strip to set the loop),
// grid, clips and the playhead. Clips can be moved, resized, duplicated and deleted, audio files
// can be dropped onto a lane, and the view zooms (Cmd + scroll) and scrolls.
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
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int x, int y) override;

    void deleteSelectedClip();
    void duplicateSelectedClip();

    std::function<void()> onOpenEditor;

private:
    enum class DragMode { None, Position, Loop, MoveClip, ResizeClip };

    AppContext& ctx;
    double viewStart = 0.0;          // beats at the left edge
    double pixelsPerBeat = 0.0;      // set on first layout to fit 16 bars
    double lastPlayhead = -1.0;
    int shownPlayheadX = 0;
    float shownPulse = 0.0f;

    DragMode drag = DragMode::None;
    int dragTrack = -1, dragClip = -1;
    double dragGrabOffset = 0.0, loopAnchor = 0.0;

    float beatToX (double beat) const { return (float) ((beat - viewStart) * pixelsPerBeat); }
    double xToBeat (float x) const    { return viewStart + x / pixelsPerBeat; }
    double snap() const               { return pixelsPerBeat >= 12.0 ? 1.0 : beatsPerBar; }
    double snapBeat (double beat) const { return std::round (beat / snap()) * snap(); }
    int trackAtY (int y) const;
    bool hitClip (juce::Point<int>, int& track, int& clip, bool& onRightEdge) const;
    juce::Rectangle<float> clipBounds (int track, const Clip&) const;
    void drawClipContent (juce::Graphics&, const Clip&, juce::Rectangle<float>, juce::Colour, float alpha, bool automation) const;
    void showClipMenu (int track, int clip);
};
