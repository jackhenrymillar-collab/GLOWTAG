#pragma once

#include <juce_opengl/juce_opengl.h>
#include <optional>
#include "AppContext.h"
#include "NeonKit.h"
#include "VisualiserParams.inc"

// The visualiser: a moving picture that follows the song. Four modes (ink, liquid, sand, bloom),
// drawn on the graphics card, shown full window or behind the whole interface. It listens to the
// master output or one channel, and a routing page wires sounds (kick, snare, levels and so on)
// to reactions (pulse, flash, spin and so on). The look was settled in a mockup first:
// tools/visualiser/mockup.html, which the shaders and the settings list are made from.
// In the full view the picture has a screen of its own with every control laid out round it; V (or a
// double-click on the picture) gives the picture the whole window.
namespace Vis
{
    struct F { enum : int {
       #define X(name, def, label, fmt) name,
        VIS_FADERS (X)
       #undef X
        count }; };
    struct C { enum : int {
       #define Y(name, def) name,
        VIS_CHOICES (Y)
       #undef Y
        count }; };

    enum Style { ink, liquid, sand, bloom, numStyles };
    enum Source { sKick, sSnare, sHat, sBig, sTrans, sLow, sMid, sHigh, sLevel, numSources };
    enum Dest { dPulse, dFlash, dSpin, dStir, dBurst, dHue, numDests };
    enum View { off, full, behind };

    const char* faderLabel (int fader);
    juce::String faderText (int fader, float value);

    // Everything the user can set. Plain data: the drawing thread takes a copy every frame.
    struct Settings
    {
        std::array<float, F::count> v {};
        std::array<int, C::count> w {};
        std::array<std::array<float, numDests>, numSources> route {};     // cable strength, 0 = not wired
        std::array<std::array<float, 2>, 3> bandHz {};                   // what low, mid and high listen to, from and to
        int style = ink;
        Settings() { reset(); }
        void reset();
        void resetWiring();
        void resetBands();
        static constexpr float lowestHz = 30.0f, highestHz = 16000.0f;
        juce::var toVar() const;
        void fromVar (const juce::var&);

        // Each mode keeps its own copy of the settings they all have (colour, spin, trails, look)
        int hueKey() const, tintKey() const, psyKey() const, trailsKey() const, spinKey() const, idleKey() const;
        int tonesKey() const, trailModeKey() const, surfaceKey() const, formKey() const, mirrorKey() const;
        std::array<juce::Colour, 3> palette (float hueShiftDegrees = 0.0f) const;
    };

    // What the drawing thread tells the panel: levels, lamps, recent kicks
    struct Readout
    {
        std::array<std::atomic<float>, numSources> signal {};
        std::array<std::atomic<float>, numDests> reaction {};
        std::array<std::atomic<float>, 3> band {};
        struct Kick { double time; float rel; bool pass; };
        struct Level { double time; float v[3]; };
        juce::SpinLock lock;
        std::array<Kick, 32> kicks {};                // low hits, for the loudest lows strip
        int numKicks = 0;
        std::array<Kick, 96> sharps {};               // every sharp hit, for the transients strip
        int numSharps = 0;
        std::array<Level, 170> levels {};             // low, mid and high about 30 times a second
        int numLevels = 0;
        std::atomic<double> clock { 0.0 };
        std::atomic<float> turn { 0.0f }, flow { 0.0f };      // how far the picture has turned and flowed, for the dials
        juce::String error;                    // a shader that would not build (guarded by lock)
    };

    class Renderer;
}

// The component sits over the area under the top bar (the whole window when the picture is maximised).
// In the full view it shows the controls and takes the pointer; in the behind view it is hidden and
// only the picture is drawn.
class Visualiser : public juce::Component,
                   private juce::OpenGLRenderer
{
public:
    Visualiser (AppContext&, juce::Component& window);
    ~Visualiser() override;

    void setView (Vis::View);
    Vis::View getView() const { return view; }
    void toggleMaximised();                     // the picture alone, filling the whole window; again to go back to where it was
    bool isMaximised() const { return view == Vis::full && maximised; }
    std::function<void()> onViewChanged;        // the window lays itself out again
    void setFeed (int slot);                    // -1: the master
    int getFeed() const { return ctx.engine.visFeed.load(); }
    void setLatency (double seconds) { latency.store (seconds); }
    void projectReplaced();                     // a song was opened: take its settings
    void tick();                                // once per screen refresh
    float backdrop() const { return settings.v[Vis::F::back]; }
    void setup (const juce::String& words);     // "full liquid routing", "full ink max": for checking it on a machine with no one at it
    void setBackdrop (float);                   // how far the picture is dimmed behind the app

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;

    void changed();                             // a setting moved: hand it to the drawing thread, mark the song edited

    // Automation: every setting can have a lane on the visualiser's own track
    std::vector<AppContext::ParamInfo> automatableParams() const;
    void applyAutomation();                     // lanes that play set the settings (once a frame)
    void reportTouches();                       // settings the hand changed since last time: record or rest their lanes
    static bool readSetting (const Vis::Settings&, const AutoTarget&, float& out);
    static void writeSetting (Vis::Settings&, const AutoTarget&, float raw);
    Vis::Settings seen;                         // the settings as last reported
    bool probing = false;
    void attach (bool ownWindow);               // start drawing: over the app's window, or in a window of its own behind it
    void release();
    void windowChanging (int what);             // 0 the window starts changing size, 1 it has finished, 2 it has a new size

    // The control panel is drawn afresh each time, the way the mockup did it: every control leaves
    // behind the box it answers to (hits) and the part of it that moves with the music (live).
    struct Hit
    {
        juce::Rectangle<float> box;
        std::function<void (float x, float y)> drag;
        std::function<void()> click, reset, up;
        int card = -1;
        bool setting = true;                    // false: it works the panel itself (a page, a scene), not a setting
    };
    struct Live { juce::AffineTransform place; juce::Rectangle<int> box; std::function<void (juce::Graphics&)> draw; };
    struct Draw;
    friend struct Draw;

    // The cards of the panel. Each keeps its steady art as a picture of its own, so moving one control
    // only draws its own card again.
    enum Card { cHead, cPics, cScenes, cShape, cColour, cMotion, cTrails, cSpecial, cBands, cGates, cRoute, cChrome, numPieces };
    struct Piece
    {
        juce::Rectangle<float> box;             // where it sits
        juce::Rectangle<int> area;              // what its art covers (the box and the glow round it)
        juce::Image art;
        std::unique_ptr<Neon::GlowLayer> glow;
        std::vector<Hit> hits;
        std::vector<Live> live;                 // the parts that move with the music
        bool shown = false, dirty = true;
    };
    struct Layout
    {
        float Z = 1.0f;                         // the whole panel shrinks to fit a small window
        juce::Rectangle<float> screen;
        std::array<juce::Rectangle<float>, numPieces> box;
        std::array<bool, numPieces> shown {};
    };
    Layout layout() const;
    void rebuild (int piece, const Layout&, float scale);
    void markAll();
    void relayout();                            // the cards moved: a page, the picture filling the window
    void sceneClicked (int index);
    juce::var scenesVar() const;
    const Hit* hitAt (juce::Point<float>) const;
    bool probeLane (const Hit&, juce::Point<float>);   // Alt-click: which setting the control moves; shows its lane

    AppContext& ctx;
    juce::Component& window;
    juce::OpenGLContext gl;
    Vis::View view = Vis::off;
    Vis::Settings settings;                     // the message thread's copy
    juce::SpinLock sharedLock;
    Vis::Settings shared;                       // what the drawing thread reads
    juce::Rectangle<int> sharedArea;            // where the picture goes, in window points
    bool sharedOwnWindow = false, sharedBehind = false;
    double statSince = 0.0, statLast = 0.0, statWorst = 0.0, statWork = 0.0;    // drawing thread: the frame-rate readout
    int statFrames = 0;
    std::atomic<double> latency { 0.02 };
    std::atomic<float> dragX { -1.0f }, dragY { -1.0f };
    Vis::Readout readout;
    std::unique_ptr<Vis::Renderer> renderer;

    std::array<Piece, numPieces> pieces;
    float artScale = 0.0f;
    juce::uint32 artLook = 0;
    juce::Rectangle<float> screenBox;           // the picture's screen, in this component
    std::function<void (float, float)> activeDrag;
    std::function<void()> activeUp;
    int activeCard = -1;                        // the card whose control is being dragged
    juce::String last;                          // what was last touched, and its value
    bool maximised = false, keepMaximised = false, wholePanel = false;
    Vis::View cameFrom = Vis::full;             // where V goes back to
    juce::uint32 lastPointerMove = 0;
    bool chromeShown = true;                    // the "show controls" word fades when the pointer rests
    int page = 0;                               // controls / routing
    bool draggingPicture = false;
    int seenGeneration = -1;
    struct Cell { float from = 1.0f, y = 0.0f; bool was = false, moved = false, started = false; } cell;   // a matrix socket being worked
    std::array<std::optional<Vis::Settings>, 8> scenes;     // whole looks kept on the lamps
    int scene = -1;
    bool storing = false;
    std::array<float, 56> spectrumShown {};
    bool saveDue = false, edited = false;
    int frameCount = 0;
    std::unique_ptr<juce::Component> picture;   // the picture's own window, when it sits behind the app's
    juce::Colour windowColour;
    bool resizing = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Visualiser)
};
