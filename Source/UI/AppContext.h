#pragma once

#include <juce_events/juce_events.h>
#include "../Engine/Project.h"
#include "../Engine/AudioEngine.h"

// Shared state every view reads: the project, the engine and the selected track.
// It broadcasts a change whenever the selection moves.
struct AppContext : public juce::ChangeBroadcaster
{
    AppContext (Project& p, AudioEngine& e) : project (p), engine (e) {}

    Project& project;
    AudioEngine& engine;
    int selectedTrack = 4;

    void selectTrack (int index)
    {
        index = juce::jlimit (0, (int) project.tracks.size() - 1, index);
        if (index != selectedTrack)
        {
            selectedTrack = index;
            sendChangeMessage();
        }
    }

    Track& selected() { return *project.tracks[(size_t) selectedTrack]; }

    // Smooth playhead position in steps since play started (-1 when stopped)
    double playheadSteps() const
    {
        if (! engine.isPlaying())
            return -1.0;
        return engine.getSecondsPlayed() * project.bpm.load() / 15.0;
    }

    // Playhead in bars, wrapped to the arrangement loop (-1 when stopped)
    double playheadBars() const
    {
        const auto steps = playheadSteps();
        if (steps < 0.0)
            return -1.0;
        return std::fmod (steps, (double) (Project::numBars * Project::stepsPerBar)) / Project::stepsPerBar;
    }
};

namespace Layout
{
    constexpr int rulerHeight = 28;
    constexpr int rowHeight = 44;
}
