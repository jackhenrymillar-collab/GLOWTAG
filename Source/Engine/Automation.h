#pragma once

#include <juce_core/juce_core.h>
#include <vector>

// Arrangement automation. Each track keeps lanes; a lane moves one control over the song. Points sit on
// beats with values 0..1 (the control's range is kept with the lane), and every stretch between two
// points can be bent into a curve. A lane plays until the control is moved by hand; then it rests until
// "back to automation". Recording only writes the controls that are actually moved.

// What a lane moves
struct AutoTarget
{
    enum Kind : int { none, knob, mixer, vis, morph };
    int kind = none;
    int module = 0;     // knob, morph: the module's id.  vis: 0 fader, 1 choice, 2 cable strength, 3 mode
    int param = 0;      // knob: the module's control.  mixer: MixParam.  vis: the setting's index

    bool operator== (const AutoTarget& o) const { return kind == o.kind && module == o.module && param == o.param; }
    bool operator!= (const AutoTarget& o) const { return ! (*this == o); }
    bool valid() const { return kind != none; }
};

// The channel's own controls
namespace MixParam { enum { volume, pan, sendA, sendB, trim, width, count }; }

struct AutoPoint
{
    double beat = 0.0;
    float value = 0.0f;     // 0..1
    float curve = 0.0f;     // bends the stretch from this point to the next: -1 .. 1, 0 is a straight line
};

// A morph lane's picture of a device: each control's value at one moment
struct MorphSnap { std::vector<std::pair<int, float>> values; };

struct AutoLane
{
    AutoTarget target;
    juce::String name;                  // what the track header shows, e.g. "filter  cutoff"
    float lo = 0.0f, hi = 1.0f;         // the control's own range
    bool stepped = false;               // a choice: whole numbers only
    std::vector<AutoPoint> points;      // in beat order
    std::vector<AutoPoint> ghost;       // the lane as it was before the last recording over it
    bool overridden = false;            // moved by hand: rests until "back to automation"

    // Morph lanes: two or three snapshots of the whole device; the lane slides between them
    std::vector<MorphSnap> snaps;
    std::vector<int> morphStepped;      // the device's choices: they switch half way, never blend

    // While a recording writes this lane (message thread only, not saved)
    bool writing = false;
    double writeFrom = 0.0, lastBeat = 0.0, lastTouch = 0.0;

    float raw (float v) const;          // 0..1 to the control's value
    float norm (float raw) const;       // the control's value to 0..1
    bool plays() const { return ! points.empty() && ! overridden && ! writing; }
};

namespace Auto
{
    // The bend: 0..1 in, 0..1 out. curve 0 is straight; positive starts slow, negative starts fast.
    float shape (float u, float curve);
    float curveFor (float midFraction);                       // the curve whose middle lands at this fraction of the rise
    float valueAt (const std::vector<AutoPoint>&, double beat);   // -1 when there are no points

    void sort (std::vector<AutoPoint>&);
    int add (std::vector<AutoPoint>&, double beat, float value);  // returns its index
    void removeRange (std::vector<AutoPoint>&, double from, double to);        // points with from <= beat <= to

    // Clip edits that carry automation: a clip's stretch [a, b) cut out, copied or laid in somewhere
    std::vector<AutoPoint> slice (const std::vector<AutoPoint>&, double a, double b);   // beats from a; ends held
    void paste (std::vector<AutoPoint>&, const std::vector<AutoPoint>& rel, double at, double length);   // overwrites [at, at + length)
    void moveRange (std::vector<AutoPoint>&, double a, double b, double to);
    void copyRange (std::vector<AutoPoint>&, double a, double b, double to);

    // A recorded stretch, thinned to the points that matter (straight runs become one line)
    void thin (std::vector<AutoPoint>&, double from, double to, float tolerance = 0.01f);

    // Lanes
    AutoLane* find (std::vector<AutoLane>&, const AutoTarget&);
    const AutoLane* find (const std::vector<AutoLane>&, const AutoTarget&);
    AutoLane& get (std::vector<AutoLane>&, const AutoTarget&);   // made if missing

    // A clip moved, was copied, or was taken to another track: its lanes go with it
    void clipMoved (std::vector<AutoLane>&, double a, double b, double to);
    void clipCopied (std::vector<AutoLane>&, double a, double b, double to);

    // A morph lane's value for one control: its snapshots blended at v (0..1). False when it has no say.
    bool morphValue (const AutoLane&, int param, float v, float& out);

    juce::var toVar (const AutoTarget&);
    AutoTarget targetFromVar (const juce::var&);
    juce::var toVar (const std::vector<AutoLane>&);
    std::vector<AutoLane> lanesFromVar (const juce::var&);
}
