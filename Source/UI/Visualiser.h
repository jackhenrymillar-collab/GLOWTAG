#pragma once

#include <juce_opengl/juce_opengl.h>
#include "AppContext.h"
#include "NeonKit.h"
#include "VisualiserParams.inc"

// The visualiser: a moving picture that follows the song. Four modes (ink, liquid, sand, bloom),
// drawn on the graphics card, shown full window or behind the whole interface. It listens to the
// master output or one channel, and a routing page wires sounds (kick, snare, levels and so on)
// to reactions (pulse, flash, spin and so on). The look was settled in a mockup first:
// tools/visualiser/mockup.html, which the shaders and the settings list are made from.
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
        int style = ink;
        Settings() { reset(); }
        void reset();
        void resetWiring();
        juce::var toVar() const;
        void fromVar (const juce::var&);

        // Each mode keeps its own copy of the settings they all have (colour, spin, trails, look)
        int hueKey() const, tintKey() const, psyKey() const, trailsKey() const, spinKey() const, idleKey() const;
        int tonesKey() const, trailModeKey() const, surfaceKey() const, formKey() const;
        std::array<juce::Colour, 3> palette (float hueShiftDegrees = 0.0f) const;
    };

    // What the drawing thread tells the panel: levels, lamps, recent kicks
    struct Readout
    {
        std::array<std::atomic<float>, numSources> signal {};
        std::array<std::atomic<float>, numDests> reaction {};
        std::array<std::atomic<float>, 3> band {};
        struct Kick { double time; float rel; bool pass; };
        juce::SpinLock lock;
        std::array<Kick, 32> kicks {};
        int numKicks = 0;
        std::atomic<double> clock { 0.0 };
        std::atomic<float> turn { 0.0f }, flow { 0.0f };      // how far the picture has turned and flowed, for the dials
        juce::String error;                    // a shader that would not build (guarded by lock)
    };

    class Renderer;
}

// The component sits over the area under the top bar. In the full view it shows the control panel
// and takes the pointer; in the behind view it is hidden and only the picture is drawn.
class Visualiser : public juce::Component,
                   private juce::OpenGLRenderer
{
public:
    Visualiser (AppContext&, juce::Component& window);
    ~Visualiser() override;

    void setView (Vis::View);
    Vis::View getView() const { return view; }
    std::function<void()> onViewChanged;        // the window lays itself out again
    void setFeed (int slot);                    // -1: the master
    int getFeed() const { return ctx.engine.visFeed.load(); }
    void setLatency (double seconds) { latency.store (seconds); }
    void projectReplaced();                     // a song was opened: take its settings
    void tick();                                // once per screen refresh
    float backdrop() const { return settings.v[Vis::F::back]; }
    void setup (const juce::String& words);     // "full liquid routing": for checking it on a machine with no one at it
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
    void attach (bool ownWindow);               // start drawing: over the app's window, or in a window of its own behind it
    void release();

    // The control panel is drawn afresh each time, the way the mockup did it: every control leaves
    // behind the box it answers to (hits) and the part of it that moves with the music (live).
    struct Hit
    {
        juce::Rectangle<float> box;
        std::function<void (float x, float y)> drag;
        std::function<void()> click, reset, up;
    };
    struct Live { juce::AffineTransform place; std::function<void (juce::Graphics&)> draw; };
    struct Draw;
    friend struct Draw;
    struct Layout { float K = 1.0f; int strip = 0; juce::Rectangle<float> panel, stripBox; juce::AffineTransform column; };
    Layout layout() const;
    const Hit* hitAt (juce::Point<float>) const;

    AppContext& ctx;
    juce::Component& window;
    juce::OpenGLContext gl;
    Vis::View view = Vis::off;
    Vis::Settings settings;                     // the message thread's copy
    juce::SpinLock sharedLock;
    Vis::Settings shared;                       // what the drawing thread reads
    juce::Rectangle<int> sharedArea;            // where the picture goes, in window points
    bool sharedOwnWindow = false;
    double statSince = 0.0, statLast = 0.0, statWorst = 0.0, statWork = 0.0;    // drawing thread: the frame-rate readout
    int statFrames = 0;
    std::atomic<double> latency { 0.02 };
    std::atomic<float> dragX { -1.0f }, dragY { -1.0f };
    Vis::Readout readout;
    std::unique_ptr<Vis::Renderer> renderer;

    Neon::GlowLayer glowLayer;
    std::vector<std::pair<juce::Rectangle<int>, juce::Image>> arts;   // the steady parts of the panel, kept between frames
    float artScale = 0.0f;
    juce::Rectangle<int> artBounds;
    bool artDirty = true;
    juce::uint32 artLook = 0;
    std::vector<Hit> hits;
    std::vector<Live> live;
    std::vector<juce::Rectangle<int>> liveAreas;
    std::function<void (float, float)> activeDrag;
    std::function<void()> activeUp;
    juce::String last;                          // what was last touched, and its value
    bool panelShown = true;
    int page = 0, tab = 0;                      // controls / routing; shape / colour / motion / trails
    bool draggingPicture = false;
    int seenGeneration = -1;
    int cableFrom = -1;                         // a cable being pulled out of a sound's jack
    juce::Point<float> cableEnd;
    double lastPaint = 0.0;
    bool saveDue = false, edited = false;
    int frameCount = 0;
    std::unique_ptr<juce::Component> picture;   // the picture's own window, when it sits behind the app's
    juce::Colour windowColour;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Visualiser)
};
