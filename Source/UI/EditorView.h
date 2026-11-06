#pragma once

#include "AppContext.h"
#include "Widgets.h"

// Piano roll for the selected clip. Pencil draws notes (drag to set length), any tool moves notes
// and resizes them from their right edge, scissors split, the velocity tool or lane sets velocity,
// and double-click or Delete removes a note. Scroll to move up and down the keyboard.
class EditorView : public juce::Component, private juce::ChangeListener
{
public:
    explicit EditorView (AppContext&);
    ~EditorView() override;

    void tick();
    void deleteSelectedNote();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    enum class DragMode { None, Move, Resize, Velocity, VelocityLane };

    AppContext& ctx;
    IconButton pencil { "Draw", Icon::Pencil, Pal::Green },
               cursor { "Select", Icon::Cursor, Pal::Sky },
               scissors { "Split", Icon::Scissors, Pal::Rose },
               velocity { "Velocity", Icon::Velocity, Pal::Gold };
    juce::TextButton loopButton;

    juce::Rectangle<int> toolbar, keys, grid, velocityLane;
    static constexpr int numRows = 36;
    static constexpr double gridStep = 0.25;   // a sixteenth note
    int lowNote = 48;
    double lastPlayhead = -1.0;
    double lastLength = 0.25;

    int selectedNote = -1;
    int shownTrack = -1, shownClip = -2;
    DragMode drag = DragMode::None;
    double grabOffset = 0.0;
    float grabVelocity = 0.8f;
    int lastAuditioned = -1;

    double loopLength() const;
    float rowHeight() const { return (float) grid.getHeight() / (float) numRows; }
    float beatWidth() const { return (float) (grid.getWidth() / juce::jmax (0.25, loopLength())); }
    float beatToX (double b) const { return (float) grid.getX() + (float) b * beatWidth(); }
    double xToBeat (float x) const { return (x - (float) grid.getX()) / beatWidth(); }
    int yToPitch (float y) const { return lowNote + numRows - 1 - (int) ((y - (float) grid.getY()) / rowHeight()); }
    float pitchToY (int p) const { return (float) grid.getY() + (float) (lowNote + numRows - 1 - p) * rowHeight(); }
    juce::Rectangle<float> noteBounds (const Note&) const;
    int noteAt (juce::Point<float>, bool& onRightEdge) const;
    void audition (int pitch, float velocity = 0.8f);
    void recentre();
    void updateLoopButton();
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
};
