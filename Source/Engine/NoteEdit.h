#pragma once

#include "Project.h"
#include <vector>

// The piano roll's note operations, kept apart from the drawing so the tests can check them.
// A selection is a sorted list of indices into a clip's notes. An empty selection means "every note"
// only where a function says so.
namespace NoteEdit
{
    // Grid choices, coarse to fine, in beats: 1 bar down to a 1/32 note (the finest)
    constexpr double gridSteps[] = { 4.0, 2.0, 1.0, 0.5, 0.25, 0.125 };
    constexpr int numGrids = (int) (sizeof (gridSteps) / sizeof (gridSteps[0]));
    constexpr int defaultGrid = 4;                  // 1/16
    juce::String gridName (int index);              // "1 bar", "1/2", "1/4" ... "1/32"
    int finer (int index);                          // clamped at 1/32
    int coarser (int index);                        // clamped at 1 bar

    // Clip length steps by half a bar, never shorter than half a bar or longer than 64 bars
    constexpr double lengthStep = beatsPerBar * 0.5;
    double changedLength (double loopLength, int direction);
    juce::String lengthName (double loopLength);    // "4 bars", "2.5 bars", "1 bar"

    std::vector<int> all (const std::vector<Note>&);
    // Notes that overlap the box (beats a..b, pitches lo..hi inclusive)
    std::vector<int> inBox (const std::vector<Note>&, double a, double b, int lo, int hi);

    // Starts move to the nearest step. An empty selection quantises every note.
    void quantise (std::vector<Note>&, const std::vector<int>& sel, double step, double loopLength);

    // Copies the selection straight after itself (the gap rounded up to the grid). Copies that would
    // run past the loop make it longer, by whole bars. Returns the copies, to become the selection.
    std::vector<int> duplicate (std::vector<Note>&, const std::vector<int>& sel, double step, double& loopLength);

    // Moves the selection by semitones. Nothing moves if any note would leave 0..127.
    bool transpose (std::vector<Note>&, const std::vector<int>& sel, int semitones);

    // Adds to the velocity of the selection, kept within 0.05..1
    void nudgeVelocity (std::vector<Note>&, const std::vector<int>& sel, float delta);

    // Removes the selection
    void erase (std::vector<Note>&, std::vector<int> sel);

    // A beat snapped to the grid, or left as it is when snap is off
    inline double snap (double beat, double step, bool on) { return on ? std::round (beat / step) * step : beat; }
    constexpr double freeMinLength = 1.0 / 64.0;     // the shortest note when snap is off
}
