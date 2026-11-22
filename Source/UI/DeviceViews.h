#pragma once

#include "NeonKit.h"
#include "AppContext.h"

// The expanded view of each built-in device: its wall of neon controls. The walls follow the
// "GLOWTAG Expanded Devices" designs; every control reads and writes the module's stored values,
// and the running patch hears each change straight away.
namespace DeviceViews
{
    // A device on a track, as a wall sees it
    struct Device
    {
        AppContext& ctx;
        int track = 0, moduleId = 0;

        Module* module() const { return ctx.project.slot (track).patch.find (moduleId); }
        ModuleType type() const { auto* m = module(); return m != nullptr ? m->type : ModuleType::Input; }
        Node* live() const { return ctx.engine.liveNode (track, moduleId); }

        float get (int i) const;
        int choice (int i) const { return juce::roundToInt (get (i)); }
        bool on (int i) const { return get (i) > 0.5f; }
        void set (int i, float v) { setRaw (i, juce::jlimit (0.0f, 1.0f, v)); }   // a 0..1 control
        void setRaw (int i, float v);                                            // a choice, a bit mask, a marker

        void note (int pitch, bool down, float velocity = 0.85f) const;          // through the track's live input
        std::shared_ptr<const AudioData> sample() const;                         // Sampler: what it plays

        std::function<void()> patchChanged;      // the sample or pads changed: the engine rebuilds the track
        std::function<void()> loadAudio;         // Sampler: ask for a file
    };

    struct Built
    {
        std::unique_ptr<Neon::Wall> wall;
        juce::String name, description;
        juce::Colour pal[3];                    // the device's Rainbow colours
        std::vector<juce::Colour> parts;        // per group `mod` (0 is the title): its Rainbow colour

        // Three colours for a part: Rainbow gives each part its own, otherwise shades of the theme colour
        const juce::Colour* colours (int mod);
        juce::Colour cache[16][3];
    };

    std::unique_ptr<Built> build (Device&);
    bool hasView (ModuleType);
    int deviceIndex (ModuleType);               // the device's place in the wall art (for its colours)
}
