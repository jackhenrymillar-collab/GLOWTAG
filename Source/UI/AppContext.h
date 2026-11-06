#pragma once

#include <juce_events/juce_events.h>
#include "../Engine/Project.h"
#include "../Engine/AudioEngine.h"

// Shared state every view reads: the project, the engine, and what is selected.
// It broadcasts a change whenever the selection or the clips change.
struct AppContext : public juce::ChangeBroadcaster
{
    AppContext (Project& p, AudioEngine& e) : project (p), engine (e) {}

    Project& project;
    AudioEngine& engine;
    int selectedTrack = 4;
    int selectedClip = 0;     // index into the selected track's clips, or -1

    void selectTrack (int index)
    {
        index = juce::jlimit (0, (int) project.tracks.size() - 1, index);
        if (index != selectedTrack)
        {
            selectedTrack = index;
            selectedClip = selected().clips.empty() ? -1 : 0;
            engine.liveTrack = index;
            sendChangeMessage();
        }
    }

    void selectClip (int track, int clip)
    {
        selectedTrack = juce::jlimit (0, (int) project.tracks.size() - 1, track);
        selectedClip = clip;
        engine.liveTrack = selectedTrack;
        sendChangeMessage();
    }

    Track& selected() { return *project.tracks[(size_t) selectedTrack]; }

    Clip* selectedClipPtr()
    {
        auto& clips = selected().clips;
        return selectedClip >= 0 && selectedClip < (int) clips.size() ? &clips[(size_t) selectedClip] : nullptr;
    }

    // Playhead in beats (shown even while stopped)
    double playheadBeats() const { return engine.getPosition(); }

    // Tell every view that clips or notes changed
    void clipsChanged() { sendChangeMessage(); }
};

namespace Layout
{
    constexpr int rulerHeight = 28;
    constexpr int rowHeight = 44;
}
