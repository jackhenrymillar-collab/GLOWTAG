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
    void rebuildPanel();
    void buildControls (Neon::Group&, float& y);
    void buildRouting (Neon::Group&, float& y);
    Neon::Item& fader (Neon::Group&, int key, float x, float y, float w);
    void paintRouting (juce::Graphics&, const juce::Colour*);
    Neon::Item& choice (Neon::Group&, const juce::String& title, int key, juce::StringArray options, float x, float y);
    juce::Point<float> toPanel (juce::Point<float>) const;
    juce::Rectangle<float> panelBounds() const;

    AppContext& ctx;
    juce::Component& window;
    juce::OpenGLContext gl;
    Vis::View view = Vis::off;
    Vis::Settings settings;                     // the message thread's copy
    juce::SpinLock sharedLock;
    Vis::Settings shared;                       // what the drawing thread reads
    juce::Rectangle<int> sharedArea;            // where the picture goes, in window points
    std::atomic<double> latency { 0.02 };
    std::atomic<float> dragX { -1.0f }, dragY { -1.0f };
    Vis::Readout readout;
    std::unique_ptr<Vis::Renderer> renderer;

    Neon::Wall panel;
    Neon::GlowLayer glowLayer;
    bool panelShown = true;
    int page = 0, tab = 0;                      // controls / routing; shape / colour / motion / trails
    float panelScale = 1.0f, panelHeight = 640.0f;
    bool draggingPicture = false;
    int seenGeneration = -1;
    int cableFrom = -1;                         // a cable being pulled out of a sound's jack
    juce::Point<float> cableEnd;
    double lastPaint = 0.0;
    bool saveDue = false, edited = false, pendingRebuild = false;
    int frameCount = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Visualiser)
};
