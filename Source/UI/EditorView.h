#pragma once

#include "AppContext.h"
#include "Widgets.h"
#include "PianoRollArt.h"
#include "Engine/NoteEdit.h"

// Piano roll for the selected clip. Pencil draws notes (drag to set length), any tool moves notes
// and resizes them from their right edge, scissors split, the velocity tool or lane sets velocity,
// and double-click or Delete removes notes. Drag a box on empty space (Select tool, or Shift with
// any tool) to pick several notes; dragging any picked note moves them all. Scroll to move up and
// down the keyboard; Alt + scroll changes the velocity of the picked notes.
class EditorView : public juce::Component, private juce::ChangeListener
{
public:
    explicit EditorView (AppContext&);
    ~EditorView() override;

    void tick();
    void deleteSelectedNote();          // deletes every selected note
    bool keyUsed (const juce::KeyPress&);  // the piano roll's shortcuts, while the Edit tab is open

    // The actions behind the buttons and shortcuts
    void quantise();
    void changeGrid (int direction);    // +1 finer, -1 coarser
    void changeLength (int direction);  // by half a bar
    bool duplicateSelection();
    void transposeSelection (int semitones);
    void selectAll();
    void toggleSnap();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    enum class DragMode { None, Move, Resize, Velocity, VelocityLane, Box };

    AppContext& ctx;
    RollButton pencil    { "Draw", RollArt::Draw, Pal::Green, "Draw notes, drag right to stretch" },
               cursor    { "Select", RollArt::Select, Pal::Sky, "Select: drag a box round notes (Shift-drag with any tool)" },
               scissors  { "Split", RollArt::Split, Pal::Rose, "Split notes on the grid" },
               velocity  { "Velocity", RollArt::Velocity, Pal::Gold, "Drag notes up or down for velocity (or Alt + scroll)" },
               quantiseB { "Quantise", RollArt::Quantise, Pal::Violet, "Quantise to the nearest step (Q)" },
               gridB     { "Grid", RollArt::Grid, Pal::Blue, "Grid size (Cmd+Up finer, Cmd+Down coarser)" },
               snapB     { "Snap", RollArt::Snap, Pal::Amber, "Snap to the grid (Alt+S)" },
               shorterB  { "Shorter", RollArt::Shorter, Pal::Sky, "Clip half a bar shorter (Cmd+Left)" },
               longerB   { "Longer", RollArt::Longer, Pal::Sky, "Clip half a bar longer (Cmd+Right)" },
               duplicateB{ "Duplicate", RollArt::Duplicate, Pal::Green, "Duplicate the selected notes (Cmd+D)" },
               octUpB    { "Oct up", RollArt::OctaveUp, Pal::Rose, "Selected notes up an octave (Cmd+=)" },
               octDownB  { "Oct down", RollArt::OctaveDown, Pal::Rose, "Selected notes down an octave (Cmd+-)" },
               allB      { "All", RollArt::SelectAll, Pal::Ice, "Select every note in the clip (Cmd+A)" };

    juce::Rectangle<int> toolbar, keys, grid, velocityLane, lengthReadout;
    int shownPlayheadX = -1000;
    int numRows = 30;
    static constexpr float targetRowHeight = 25.0f;   // taller keys, easier to pick notes
    int gridIndex = NoteEdit::defaultGrid;
    bool snapOn = true;
    int lowNote = 48;
    double lastPlayhead = -1.0;
    double lastLength = 0.25;

    std::vector<int> selection;                 // sorted note indices
    std::vector<Note> dragFrom;                 // the selection as it was when the drag began
    int anchor = -1;                            // the note under the pointer when the drag began
    juce::Rectangle<float> box;                 // the selection box while it is dragged
    std::vector<int> boxStartSelection;
    int shownTrack = -1, shownClip = -2;
    DragMode drag = DragMode::None;
    double grabOffset = 0.0;
    int lastAuditioned = -1;
    juce::String flash;                         // a word that says what just happened
    double flashTime = -10.0;

    double gridStep() const { return NoteEdit::gridSteps[gridIndex]; }
    double loopLength() const;
    Clip* midiClip() const;
    bool isSelected (int i) const { return std::binary_search (selection.begin(), selection.end(), i); }
    void select (std::vector<int>);
    float rowHeight() const { return (float) grid.getHeight() / (float) numRows; }
    float beatWidth() const { return (float) (grid.getWidth() / juce::jmax (0.25, loopLength())); }
    float beatToX (double b) const { return (float) grid.getX() + (float) b * beatWidth(); }
    double xToBeat (float x) const { return (x - (float) grid.getX()) / beatWidth(); }
    int yToPitch (float y) const { return lowNote + numRows - 1 - (int) std::floor ((y - (float) grid.getY()) / rowHeight()); }
    float pitchToY (int p) const { return (float) grid.getY() + (float) (lowNote + numRows - 1 - p) * rowHeight(); }
    juce::Rectangle<float> noteBounds (const Note&) const;
    int noteAt (juce::Point<float>, bool& onRightEdge) const;
    void audition (int pitch, float velocity = 0.8f);
    void recentre();
    void updateButtons();
    void say (const juce::String&);
    void edited();                              // notes changed: tell the app and redraw
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
};
