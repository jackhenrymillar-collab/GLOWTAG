#pragma once

#include <juce_events/juce_events.h>
#include "../Engine/Project.h"
#include "../Engine/AudioEngine.h"

// Shared state every view reads: the project, the engine, and what is selected.
// It broadcasts a change whenever the selection or the clips change.
// Selection is a project slot: a track, Return A, Return B or the master (see Project::slot).
struct AppContext : public juce::ChangeBroadcaster
{
    AppContext (Project& p, AudioEngine& e) : project (p), engine (e) {}

    Project& project;
    AudioEngine& engine;
    int selectedTrack = 4;
    int selectedClip = 0;     // index into the selected track's clips, or -1
    int generation = 0;       // bumped when the whole project is replaced (new / open)

    bool isTrack (int slot) const { return slot >= 0 && slot < (int) project.tracks.size(); }
    bool selectedIsTrack() const { return isTrack (selectedTrack); }

    void selectTrack (int index)
    {
        index = juce::jlimit (0, project.numSlots() - 1, index);
        if (index != selectedTrack)
        {
            selectedTrack = index;
            selectedClip = selected().clips.empty() ? -1 : 0;
            if (isTrack (index))
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

    Track& selected() { return project.slot (juce::jlimit (0, project.numSlots() - 1, selectedTrack)); }

    Clip* selectedClipPtr()
    {
        auto& clips = selected().clips;
        return selectedClip >= 0 && selectedClip < (int) clips.size() ? &clips[(size_t) selectedClip] : nullptr;
    }

    // Playhead in beats (shown even while stopped)
    double playheadBeats() const { return engine.getPosition(); }

    // Tell every view that clips or notes changed
    void clipsChanged() { project.dirty = true; sendChangeMessage(); }

    // A device patch or rack was edited: the project needs saving
    void touched() { project.dirty = true; }

    // Replace the song (new song or a file opened) with the engine held off, then rebuild every view.
    // `load` swaps the project's tracks and returns whether it worked.
    bool replaceProject (const std::function<bool()>& load)
    {
        const bool ok = engine.replaceProject (load);
        project.dirty = false;
        ++generation;
        selectedTrack = juce::jlimit (0, project.numSlots() - 1, juce::jmin (selectedTrack, (int) project.tracks.size() - 1));
        if (! selectedIsTrack()) selectedTrack = 0;
        selectedClip = selected().clips.empty() ? -1 : 0;
        engine.liveTrack = selectedTrack;
        // Synchronously: views hold references into the old tracks and must rebuild before they paint again
        sendSynchronousChangeMessage();
        return ok;
    }
};

namespace Layout
{
    constexpr int rulerHeight = 28;
    constexpr int rowHeight = 44;
}
