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
    int generation = 0;       // bumped when the whole project is replaced (new / open) or its slots change

    // The device the mixer last asked to see: the device panel scrolls to it and lights it up
    int shownSlot = -1, shownModule = -1;
    bool showPending = false;
    std::function<void()> beforeRestructure;     // closes plugin windows before slots move

    void showDevice (int slot, int moduleId)
    {
        shownSlot = slot;
        shownModule = moduleId;
        showPending = true;
        if (slot != selectedTrack) selectTrack (slot);
        else sendChangeMessage();
    }

    // Add or remove a bus: the engine is held off while `edit` changes the slot layout, playback carries on
    bool restructure (const std::function<bool()>& edit)
    {
        if (beforeRestructure) beforeRestructure();
        const Track* keep = &selected();
        const bool ok = engine.restructure (edit);
        project.dirty = true;
        ++generation;
        const int kept = project.slotOf (keep);
        selectedTrack = kept >= 0 ? kept : 0;
        selectedClip = selected().clips.empty() ? -1 : 0;
        if (selectedIsTrack()) engine.liveTrack = selectedTrack;
        shownSlot = shownModule = -1;
        sendSynchronousChangeMessage();
        return ok;
    }

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

    //==========================================================================
    // Automation (AutomationHub.cpp)

    static constexpr int visSlot = -2;            // the visualiser's own track, under the song's tracks
    bool lanesShown = false;                      // lanes drawn over the clips (top bar "auto")
    bool stickToClips = true;                     // Settings: moving or copying a clip takes its automation with it
    float autoBrightness = 0.8f;                  // Settings: how bright the lanes glow
    int autoGeneration = 0;                       // bumped when lanes change, so views repaint
    int laneRequests = 0;                         // bumped by showLane: the window brings the arrangement forward

    struct ParamInfo { juce::String group, name; float lo = 0.0f, hi = 1.0f; bool stepped = false; AutoTarget target; };

    int rowHeight() const;                        // track rows are taller while lanes show, to fit the chooser
    int arrangeHeight = 0;                        // the arrangement's height (rows grow only as far as it has room)
    int numRows() const { return (int) project.tracks.size() + (project.visTrack ? 1 : 0); }
    int trackOfRow (int row) const { return row < (int) project.tracks.size() ? row : visSlot; }
    int rowOfTrack (int track) const { return track == visSlot ? (int) project.tracks.size() : track; }
    float currentValue (int track, const AutoTarget&, const ParamInfo&);   // the control as it stands, 0..1
    std::function<bool (const AutoTarget&, float&)> visValue;              // the visualiser's setting, raw
    bool visRow() const { return project.visTrack; }
    std::vector<AutoLane>* lanesOf (int track);   // a song track, or visSlot
    AutoTarget shownTarget (int track);           // the lane the header shows (mixer volume if none chosen)
    void setShown (int track, const AutoTarget&);

    // Every control a track can automate, grouped by device (mixer first), and one control's details
    std::vector<ParamInfo> paramsOf (int track);
    ParamInfo describe (int track, const AutoTarget&);
    std::function<std::vector<ParamInfo>()> visParams;   // the visualiser fills this in

    // Alt-click on any control: its lane comes up on its track in the arrangement
    void showLane (int track, const AutoTarget&);
    // A control was moved by hand. While recording it writes its lane (only the ones moved);
    // otherwise a lane that was playing it rests until "back to automation".
    void paramTouched (int track, const AutoTarget&, float raw);
    // Once a frame: ends touches that have let go, and copies lane values into the stored knobs so walls show them
    void tickAutomation();
    int restingLanes() const;                     // lanes moved by hand and resting
    void backToAutomation();
    void automationChanged() { ++autoGeneration; project.dirty = true; sendChangeMessage(); }

    // The arrangement asks for the chooser (MainComponent shows it)
    std::function<void (int track, juce::Rectangle<int> screenAnchor)> openChooser;
    std::function<void()> openSettings;

    // A clip and its automation on the clipboard
    struct ClipBoard { bool full = false; Clip clip; int fromTrack = -1; std::vector<AutoLane> lanes; } clipBoard;

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
        shownSlot = shownModule = -1;
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
