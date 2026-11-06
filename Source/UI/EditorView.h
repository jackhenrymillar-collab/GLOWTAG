#pragma once

#include "AppContext.h"
#include "Widgets.h"

// Piano roll for the selected track's one-bar pattern, with a velocity lane.
// Click a cell with the pencil to add or remove a note; the beat changes live.
class EditorView : public juce::Component
{
public:
    explicit EditorView (AppContext&);

    void tick();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    AppContext& ctx;
    IconButton pencil { "Draw", Icon::Pencil, Pal::Green },
               cursor { "Select", Icon::Cursor, Pal::Sky },
               scissors { "Split", Icon::Scissors, Pal::Rose },
               velocity { "Velocity", Icon::Velocity, Pal::Gold };

    juce::Rectangle<int> toolbar, keys, grid, velocityLane;
    static constexpr int numRows = 24;
    int lowNote = 48;
    double lastPlayhead = -1.0;

    std::vector<int> notesForStep (const Track&, int step) const;
    int rowForNote (int note) const { return lowNote + numRows - 1 - note; }
    float rowHeight() const { return (float) grid.getHeight() / (float) numRows; }
    float stepWidth() const { return (float) grid.getWidth() / (float) Project::stepsPerBar; }
    void updateRange();
};
