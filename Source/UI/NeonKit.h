#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <optional>
#include <vector>

// Neon on a brick wall, for the expanded device views. Every control is a piece of lit glass:
// tube faders, sign words, lamp handles on curves, bulb counters and drag areas. Pieces sit in
// groups that are placed and tilted on a 1600 x 920 wall, which is scaled to fit the window.
namespace Neon
{
    using Line = std::vector<juce::Point<float>>;
    using Lines = std::vector<Line>;

    // What every drawing reads: the chosen style (0 tubes, 1 marquee bulbs, 2 block) and the glow
    struct Look { int style = 0; float glow = 1.0f; };
    Look& look();

    // Soft light. While a view paints, every lit piece of glass adds its shape here (in the view's
    // coordinates). GlowLayer then blurs the lot into a halo and a wider bloom, the way the Tab wall
    // does, instead of stacking wide strokes that show as bands.
    struct Glow
    {
        struct Source { juce::Path path; juce::Colour colour; float width; };   // width 0: filled
        std::vector<Source> sources;
        juce::AffineTransform base, current;   // wall space to view; current adds the group's placement
        float alpha = 1.0f;                     // a faded piece glows less
        bool collecting = false;
        void add (const juce::Path& p, juce::Colour c, float width, const juce::AffineTransform& t = {});
    };
    Glow& glow();

    class GlowLayer
    {
    public:
        // `remember` is how many different lights the layer keeps. A component's own layer keeps one;
        // a layer shared by many small things (the buttons) keeps several, so each finds its own again.
        explicit GlowLayer (int remember = 1) : capacity (juce::jmax (1, remember)) {}

        // Turns what was collected into blurred light, ready for draw(). `unit` is view pixels per wall unit.
        // When the lit shapes are the same as last time nothing is blurred again: the light is reused.
        void build (int width, int height, float unit);
        void draw (juce::Graphics&, juce::Rectangle<float> area);
        float bloomAmount = 0.55f;          // small things (a button) keep the wide bloom faint

        int blursMade = 0, blursReused = 0; // for the frame-cost check in the tests
    private:
        struct Entry
        {
            juce::uint64 signature = 0;
            juce::Image halo, bloom;
            juce::Image composite;          // halo and bloom laid together at screen resolution
            float compositeGlow = -1.0f, compositeScale = 0.0f;
            juce::Rectangle<float> compositeArea;
            int frames = 0;                 // how many paints in a row have used this light
            juce::uint32 lastUse = 0;
        };
        std::vector<Entry> entries;
        int capacity = 1;
        int current = -1;
        juce::uint32 useCounter = 0;
    };

    // Paints `draw` with its glow, all at once: a first pass only collects the lit shapes (nothing is
    // drawn), they are blurred, laid down, and then the real pass draws the glass over them. For the
    // app's ordinary components; `unit` sets how far the light spreads.
    void paintLit (juce::Graphics&, int width, int height, GlowLayer&, float unit, const std::function<void (juce::Graphics&, bool real)>& draw);
    GlowLayer& scratchGlow();               // a shared layer for one-off paints (buttons)

    // A word that is a button: lit glass when on, grey glass when off, a little light under the pointer
    void wordButton (juce::Graphics&, const juce::String&, juce::Rectangle<float> box, float size, juce::Colour, bool on, bool over, juce::Justification = juce::Justification::centred);

    inline juce::Colour mix (juce::Colour a, juce::Colour b, float t) { return a.interpolatedWith (b, juce::jlimit (0.0f, 1.0f, t)); }
    inline juce::Colour rgba (juce::Colour c, float a) { return c.withAlpha (juce::jlimit (0.0f, 1.0f, a)); }
    inline float clamp01 (float v) { return juce::jlimit (0.0f, 1.0f, v); }

    // Polylines
    float length (const Line&);
    juce::Point<float> pointAt (const Line&, float t);                 // t 0..1 along the line
    Line part (const Line&, float a, float b);                         // the stretch from a to b
    struct Near { float distance, t; };
    Near nearest (const Line&, juce::Point<float>);
    struct Step { juce::Point<float> p, dir; };
    std::vector<Step> walk (const Line&, float step, float from);      // evenly spaced points along a line
    Line roundRect (float x, float y, float w, float h, float r);
    Line circle (float cx, float cy, float r, int n = 40);
    Line curve (juce::Point<float> a, juce::Point<float> control, juce::Point<float> b, int n = 20);

    struct TubeOptions
    {
        float w = 1.0f;
        bool off = false;                 // unlit glass
        bool closed = false;
        std::optional<float> base;        // block style: a soft panel down to this height
        float clips = 0.0f;               // tubes style: mounting clips this far apart
        float dashOn = 0.0f, dashOff = 0.0f;
        int style = -1;                   // -1: the chosen style
        float step = 6.5f;                // marquee: bulb spacing
    };
    void tube (juce::Graphics&, const Lines&, juce::Colour, const TubeOptions& = {});
    inline void tube (juce::Graphics& g, const Line& l, juce::Colour c, const TubeOptions& o = {}) { tube (g, Lines { l }, c, o); }
    void bulb (juce::Graphics&, float x, float y, juce::Colour, float level = 1.0f, float r = 4.0f);
    void ringMark (juce::Graphics&, float x, float y, juce::Colour);   // marks a control that is new to the device

    // Handstyle neon lettering. Lit glows in colour; unlit reads as bare glass. Returns the width.
    enum class Align { left, centre, right };
    float text (juce::Graphics&, const juce::String&, float x, float baseline, float size, juce::Colour, float lit = 1.0f, Align = Align::left);
    float textWidth (const juce::String&, float size);
    void label (juce::Graphics&, const juce::String&, float x, float baseline, Align = Align::left, juce::Colour = juce::Colour (0xff77797f), float size = 13.0f);
    void bigTag (juce::Graphics&, const juce::String&, float x, float y, juce::Colour);   // the device name, in the style's lettering

    // The brick wall with steady pools of light
    void paintBricks (juce::Graphics&, juce::Rectangle<float> area);

    //==========================================================================
    // Controls

    struct Gesture
    {
        std::function<void (float x, float y)> move;
        std::function<void()> up;
    };

    struct Item
    {
        enum Kind { fader, words, handle, area, count } kind = area;
        int ci = 0;                                     // which of the group's three colours
        bool isNew = false;                             // ring mark: new to this device
        std::function<bool()> visible;
        std::function<juce::String()> tip;

        // fader: lit glass up to the value (or bulbs, one lit)
        Line pts;
        juce::String text;
        float lx = 0, ly = 0;
        bool bipolar = false;
        float def = 0.0f;
        int bulbs = 0;
        std::function<float()> get;
        std::function<void (float)> set;

        // words: a row of sign words, one lit; or a toggle; `dyn` shows a changing word
        std::vector<std::pair<int, juce::String>> opts;
        float x = 0, y = 0, size = 19.0f, gap = 14.0f;
        bool toggle = false, always = false;
        juce::String pre;
        std::function<juce::String()> dyn;
        std::function<int()> getChoice;
        std::function<void (int)> setChoice;
        std::vector<std::pair<juce::Rectangle<float>, int>> rects;     // worked out while drawing
        int hoverValue = -1;

        // handle: a lamp riding a curve
        std::function<std::optional<juce::Point<float>>()> pos;
        float r = 5.5f;

        // area: drag anywhere in a box
        juce::Rectangle<float> box;
        juce::MouseCursor::StandardCursorType cursor = juce::MouseCursor::NormalCursor;
        std::function<std::vector<float>()> start;
        std::function<void (float dx, float dy, const std::vector<float>& from)> drag;
        std::function<std::optional<Gesture> (float x, float y)> down;   // handles the press itself (keys, waveform)
        std::function<void (float x, float y)> doubleClick;
        std::function<juce::MouseCursor::StandardCursorType (float x, float y)> hover;
        std::function<void (float delta)> wheel;

        // count: a row of bulbs, the first ones lit
        int n = 0;
        float dx = 24.0f, x0 = 0.0f;
        int hoverIndex = -1;
    };

    struct Group
    {
        int mod = 0;                         // which part of the device (its Rainbow colours)
        float x = 0, y = 0, rot = 0;         // rot in radians
        juce::Point<float> poolCentre;       // the steady light behind it, in group space
        float poolRadius = 0.0f;
        std::vector<std::unique_ptr<Item>> items;
        std::function<void (juce::Graphics&, double t, float dt, const juce::Colour* c)> draw;
        std::map<juce::String, std::vector<float>> scratch;   // positions the drawing leaves for the handles

        Item& add (Item::Kind);
        juce::AffineTransform toWall() const { return juce::AffineTransform::rotation (rot).translated (x, y); }
        juce::Point<float> toLocal (juce::Point<float> p) const { return p.transformedBy (toWall().inverted()); }
        juce::Point<float> toGlobal (juce::Point<float> p) const { return p.transformedBy (toWall()); }

        // The pieces the walls use most, so a wall reads like its picture
        Item& fader (float x0, float y0, float x1, float y1, std::optional<juce::Point<float>> bend = {});
        Item& words (std::vector<std::pair<int, juce::String>>, float x, float y);
        Item& toggle (const juce::String& word, float x, float y, std::function<bool()> get, std::function<void (bool)> set);
        Item& handle (std::function<std::optional<juce::Point<float>>()> pos);
        Item& dragArea (juce::Rectangle<float>, juce::MouseCursor::StandardCursorType);
        Item& counter (int n, float x, float y);
    };

    struct Wire { float v[8]; };             // a jumper wire between groups: a bezier, wall space

    // All the groups of one device, with the pointer handling
    class Wall
    {
    public:
        static constexpr float width = 1600.0f, height = 920.0f;

        std::vector<std::unique_ptr<Group>> groups;
        std::vector<Wire> wires;
        std::function<const juce::Colour* (int mod)> colours;   // three colours for a group
        float labelSize = 17.0f;                                  // fader labels (the rack cards use smaller ones)

        Group& add (int mod, float x, float y, float rotDegrees, float poolX, float poolY, float poolR);

        void paint (juce::Graphics&, double t, float dt);     // in wall space
        void paintPools (juce::Graphics&);

        // Pointer, in wall space. Return true when something took it.
        bool mouseDown (juce::Point<float>);
        void mouseDrag (juce::Point<float>, bool fine);
        void mouseUp();
        bool doubleClick (juce::Point<float>);
        bool wheel (juce::Point<float>, float delta);
        juce::MouseCursor::StandardCursorType mouseMove (juce::Point<float>);
        void mouseExit() { if (active == nullptr) hot = nullptr; }
        bool isDragging() const { return active != nullptr; }

        // The readout: what was last touched
        juce::String readout;
        double readoutTime = -10.0;
        void say (const juce::String& s);

    private:
        struct Hit { Group* g = nullptr; Item* it = nullptr; juce::Point<float> local; };
        Hit pick (juce::Point<float>);
        void drawItem (juce::Graphics&, Item&, const juce::Colour* c);
        Item* hot = nullptr;
        Item* active = nullptr;
        Group* activeGroup = nullptr;
        juce::Point<float> startLocal;
        std::vector<float> startState;
        std::optional<Gesture> gesture;
    };

    double now();      // seconds, for readouts and animation

    // How long the last frame took, in seconds (the app sets it once per screen refresh). Things that
    // ease or fade use it, so they move at the same speed however often the screen is drawn.
    void setFrameTime (double seconds);
    float frameTime();
    inline float perFrame (float factorAt30Hz) { return std::pow (factorAt30Hz, frameTime() * 30.0f); }   // a decay that was tuned at 30 frames a second
}
