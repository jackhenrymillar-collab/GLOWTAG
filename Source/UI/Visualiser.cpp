#include "Visualiser.h"
#include "VisualiserRender.h"
#include "../Theme.h"

using namespace Vis;
using Neon::Line;

namespace VisIcons
{
   #include "VisualiserIcons.inc"
}

namespace
{
    constexpr float panelWidth = 392.0f;
    const juce::StringArray sourceNames { "kick", "snare", "hats", "loudest lows", "transients", "low", "mid", "high", "loudness" };
    const juce::StringArray destNames { "pulse", "flash", "spin", "stir", "burst", "colour" };
    float beadAt (float amount) { return 0.15f + 0.7f * amount; }
}

#if JUCE_MAC
namespace VisNative
{
    void placeBehind (juce::Component& picture, juce::Component& appWindow);   // VisualiserMac.mm
    void letGo (juce::Component& picture);
}
#endif

Visualiser::Visualiser (AppContext& c, juce::Component& w) : ctx (c), window (w)
{
    setInterceptsMouseClicks (true, false);
    setWantsKeyboardFocus (false);
    renderer = std::make_unique<Renderer>();
    shared = settings;
}

Visualiser::~Visualiser()
{
    gl.detach();
   #if JUCE_MAC
    if (picture != nullptr) VisNative::letGo (*picture);
   #endif
    picture.reset();
}

//==============================================================================

// The picture's own window. Behind the app the picture is drawn here, under the app's window,
// which turns to glass. The two are then drawn separately: the picture keeps its full frame rate
// on the graphics card however long the app's own interface takes to draw.
namespace
{
    struct PictureWindow : juce::Component
    {
        PictureWindow() { setOpaque (true); setInterceptsMouseClicks (false, false); }
        void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xff0b0b0d)); }
    };

    // Makes a window again, so it picks up whether it can be seen through
    void remake (juce::Component& top)
    {
        if (auto* peer = top.getPeer())
            top.addToDesktop (peer->getStyleFlags() & ~juce::ComponentPeer::windowIsSemiTransparent);
    }

    bool wantsOwnWindow()
    {
       #if JUCE_MAC
        return ! juce::SystemStats::getEnvironmentVariable ("GLOWTAG_VIS_ONE_WINDOW", {}).getIntValue();
       #else
        return false;
       #endif
    }
}

void Visualiser::release()
{
    gl.detach();
    if (picture != nullptr)
    {
       #if JUCE_MAC
        VisNative::letGo (*picture);
       #endif
        picture.reset();
        // The app's window is solid again
        if (auto* top = dynamic_cast<juce::ResizableWindow*> (window.getTopLevelComponent()))
        {
            top->setBackgroundColour (windowColour);
            remake (*top);
            top->toFront (true);
        }
    }
}

void Visualiser::attach (bool ownWindow)
{
    gl.setRenderer (this);
    gl.setOpenGLVersionRequired (juce::OpenGLContext::openGL3_2);
    gl.setContinuousRepainting (true);
    gl.setSwapInterval (1);
    auto* top = dynamic_cast<juce::ResizableWindow*> (window.getTopLevelComponent());
    if (ownWindow && top != nullptr && top->isOnDesktop())
    {
        // A window can only be seen through if it was made that way, so the app's window is made again
        windowColour = top->getBackgroundColour();
        top->setBackgroundColour (juce::Colours::transparentBlack);
        remake (*top);
        picture = std::make_unique<PictureWindow>();
        picture->addToDesktop (juce::ComponentPeer::windowIgnoresMouseClicks);
        picture->setBounds (window.getScreenBounds());
        picture->setVisible (true);
       #if JUCE_MAC
        VisNative::placeBehind (*picture, *top);
       #endif
        top->toFront (true);
        gl.setComponentPaintingEnabled (false);
        gl.attachTo (*picture);
    }
    else
    {
        gl.setComponentPaintingEnabled (true);
        gl.attachTo (window);
    }
}

void Visualiser::setView (View v)
{
    if (v == view) return;
    view = v;
    ctx.engine.visListener.enabled = v != off;
    const bool ownWindow = v == behind && wantsOwnWindow();
    if (v == off || (gl.isAttached() && ownWindow != (picture != nullptr)))
        release();
    Theme::get().setGlass (v == behind ? 0.3f + 0.55f * settings.v[F::back] : 0.0f);
    if (v != off && ! gl.isAttached())
        attach (ownWindow);
    setVisible (v == full);
    changed();
    if (onViewChanged) onViewChanged();
}

void Visualiser::setup (const juce::String& words)
{
    const auto w = juce::StringArray::fromTokens (words, " ,", {});
    static const char* const styles[] = { "ink", "liquid", "sand", "bloom" };
    for (int i = 0; i < numStyles; ++i) if (w.contains (styles[i])) settings.style = i;
    page = w.contains ("routing") ? 1 : 0;
    for (int i = 0; i < 4; ++i) if (w.contains (juce::StringArray { "shape", "colour", "motion", "trails" }[i])) tab = i;
    for (auto& word : w)
        if (word.containsChar ('='))        // look=2, form=1
        {
            const int value = word.fromFirstOccurrenceOf ("=", false, false).getIntValue();
            if (word.startsWith ("look")) settings.w[(size_t) settings.surfaceKey()] = value;
            if (word.startsWith ("form")) settings.w[(size_t) settings.formKey()] = value;
        }
    setView (w.contains ("behind") ? behind : full);
}

void Visualiser::setBackdrop (float amount)
{
    settings.v[F::back] = juce::jlimit (0.0f, 1.0f, amount);
    edited = true;
    if (view == behind) Theme::get().setGlass (0.3f + 0.55f * settings.v[F::back]);
    changed();
}

void Visualiser::setFeed (int slot)
{
    ctx.engine.visFeed = slot;
}

void Visualiser::projectReplaced()
{
    if (ctx.project.visualiser.isObject())
    {
        settings.fromVar (ctx.project.visualiser);
        tab = 0;
    }
    ctx.engine.visFeed = -1;
    const juce::SpinLock::ScopedLockType lock (sharedLock);
    shared = settings;
}

void Visualiser::changed()
{
    {
        const juce::SpinLock::ScopedLockType lock (sharedLock);
        shared = settings;
        // Behind the app the picture fills the window; in the full view it sits under the top bar
        sharedArea = view == behind ? window.getLocalBounds() : getBounds();
        sharedOwnWindow = picture != nullptr;
    }
    saveDue = true;
    artDirty = true;
    repaint();
}

void Visualiser::tick()
{
    if (seenGeneration != ctx.generation)
    {
        const bool first = seenGeneration < 0;
        seenGeneration = ctx.generation;
        if (! first || ctx.project.visualiser.isObject()) projectReplaced();
    }
    if (view == off) return;
    if (picture != nullptr && picture->getBounds() != window.getScreenBounds())
        picture->setBounds (window.getScreenBounds());     // the picture's window follows the app's
    // The song remembers the look: written back a moment after the last change, not on every drag
    if (saveDue && activeDrag == nullptr)
    {
        saveDue = false;
        const auto now = settings.toVar();
        if (juce::JSON::toString (now, true) != juce::JSON::toString (ctx.project.visualiser, true))
        {
            const bool had = ctx.project.visualiser.isObject();
            ctx.project.visualiser = now;
            if (had || edited) ctx.touched();
        }
    }
    if (view == full && (++frameCount & 1) == 0)
        for (auto& r : liveAreas) repaint (r);                       // the lamps and meters, 30 times a second
}

//==============================================================================
// Drawing thread

void Visualiser::newOpenGLContextCreated()
{
    juce::String error;
    renderer->create (error);
    const juce::SpinLock::ScopedLockType lock (readout.lock);
    readout.error = error;
    if (error.isNotEmpty()) std::fprintf (stderr, "visualiser shaders: %s\n", error.toRawUTF8());
}

void Visualiser::openGLContextClosing()
{
    renderer->destroy();
}

void Visualiser::renderOpenGL()
{
    using namespace juce::gl;
    Settings s;
    juce::Rectangle<int> area;
    bool ownWindow = false;
    {
        const juce::SpinLock::ScopedLockType lock (sharedLock);
        s = shared;
        area = sharedArea;
        ownWindow = sharedOwnWindow;
    }
    const bool isBehind = area.getY() == 0;

    // GLOWTAG_VIS_STATS=1 prints how fast the picture is really being drawn, and on what
    static const bool stats = juce::SystemStats::getEnvironmentVariable ("GLOWTAG_VIS_STATS", {}).getIntValue() != 0;
    const double frameStart = juce::Time::getMillisecondCounterHiRes();
    if (stats)
    {
        if (statFrames == 0 && statSince == 0.0)
            std::fprintf (stderr, "visualiser: drawn by %s (%s)\n", (const char*) glGetString (GL_RENDERER), (const char*) glGetString (GL_VERSION));
        if (statSince == 0.0) statSince = frameStart;
        if (statLast > 0.0) statWorst = juce::jmax (statWorst, frameStart - statLast);
        statLast = frameStart;
        ++statFrames;
        if (frameStart - statSince >= 2000.0)
        {
            std::fprintf (stderr, "visualiser: %s, %.1f frames a second, longest gap %.0f ms, picture takes %.2f ms a frame, %d x %d\n",
                          isBehind ? (ownWindow ? "behind (own window)" : "behind (one window)") : "full window",
                          statFrames * 1000.0 / (frameStart - statSince), statWorst, statWork / statFrames,
                          juce::roundToInt ((float) area.getWidth() * (float) gl.getRenderingScale()), juce::roundToInt ((float) area.getHeight() * (float) gl.getRenderingScale()));
            statSince = frameStart; statFrames = 0; statWorst = 0.0; statWork = 0.0;
        }
    }
    juce::OpenGLHelpers::clear (juce::Colour (0xff0b0b0d));
    GLint framebuffer = 0, viewport[4] = {};
    glGetIntegerv (GL_FRAMEBUFFER_BINDING, &framebuffer);
    glGetIntegerv (GL_VIEWPORT, viewport);
    const float scale = (float) gl.getRenderingScale();
    const int windowHeight = viewport[3];
    Renderer::Out out { (unsigned int) framebuffer,
                        juce::roundToInt ((float) area.getX() * scale),
                        windowHeight - juce::roundToInt ((float) area.getBottom() * scale),
                        juce::roundToInt ((float) area.getWidth() * scale),
                        juce::roundToInt ((float) area.getHeight() * scale),
                        isBehind ? 1.0f - 0.5f * s.v[F::back] : 1.0f };
    const float dx = dragX.load(), dy = dragY.load();
    renderer->frame (s, ctx.engine.visListener, latency.load(), out, { dx, dx < 0.0f ? -1.0f : 1.0f - dy }, readout);
    glBindFramebuffer (GL_FRAMEBUFFER, (GLuint) framebuffer);
    glViewport (viewport[0], viewport[1], viewport[2], viewport[3]);
    if (stats) { glFlush(); statWork += juce::Time::getMillisecondCounterHiRes() - frameStart; }
}

//==============================================================================
// The control panel: neon glass on a dark pane at the right of the picture, laid out as in the mockup

Visualiser::Layout Visualiser::layout() const
{
    Layout l;
    const float W = (float) getWidth(), H = (float) getHeight();
    auto fit = [] (float room) { return room >= 7.0f * 128.0f + 56.0f ? 1 : room >= 4.0f * 128.0f + 40.0f ? 2 : 0; };
    if (! panelShown)
        l.strip = fit (W - 32.0f);
    else
    {
        l.K = juce::jmin (juce::jlimit (0.7f, 1.4f, (H - 28.0f) / (page == 1 ? 700.0f : 668.0f)), (W - 24.0f) / panelWidth);
        l.strip = fit (W - panelWidth * l.K - 56.0f);
        if (l.strip == 0 && page == 0) l.K = juce::jmin (l.K, juce::jlimit (0.6f, 1.35f, (H - 28.0f) / 880.0f));
        const float pw = std::round (panelWidth * l.K);
        l.panel = { W - pw - 12.0f, 12.0f, pw, H - 24.0f };
        l.column = juce::AffineTransform::scale (l.K).translated (l.panel.getX() + 16.0f * l.K, l.panel.getY());
    }
    if (l.strip != 0)
    {
        const bool two = l.strip == 2;
        const float bw = two ? 4.0f * 128.0f + 16.0f : 7.0f * 128.0f + 40.0f, bh = two ? 122.0f : 66.0f;
        l.stripBox = { 12.0f, H - 15.0f - bh, bw, bh };
    }
    return l;
}

// Everything the panel is made of. One of these lives for the length of a paint.
struct Visualiser::Draw
{
    Visualiser& v;
    juce::Graphics& g;
    juce::AffineTransform place;
    int ci = 0;
    const juce::Colour grey { 0xff8d8f96 }, dim { 0xff6d7078 };

    Settings& S() { return v.settings; }
    juce::Colour nc()
    {
        auto& th = Theme::get();
        return th.isRainbow() ? th.pal (ci++) : th.themeColour();
    }
    static juce::Colour hsl (float degrees, float s, float l)
    {
        float h = std::fmod (degrees / 360.0f, 1.0f);
        if (h < 0.0f) h += 1.0f;
        return juce::Colour::fromHSL (h, s, l, 1.0f);
    }
    void setPlace (const juce::AffineTransform& t)
    {
        place = t;
        Neon::glow().base = Neon::glow().current = t;
    }

    void hit (juce::Rectangle<float> box, std::function<void (float, float)> drag, std::function<void()> click = {}, std::function<void()> reset = {}, std::function<void()> up = {})
    {
        Hit h;
        h.box = box.transformedBy (place);
        if (drag != nullptr)
            h.drag = [drag, inv = place.inverted()] (float x, float y) { const auto p = juce::Point<float> (x, y).transformedBy (inv); drag (p.x, p.y); };
        h.click = std::move (click);
        h.reset = std::move (reset);
        h.up = std::move (up);
        v.hits.push_back (std::move (h));
    }
    void block (juce::Rectangle<float> box) { hit (box, {}); }          // takes the pointer and does nothing
    void dyn (std::function<void (juce::Graphics&)> f) { v.live.push_back ({ place, std::move (f) }); }

    void tube (const Line& l, juce::Colour c, float w = 1.0f, bool off = false)
    {
        Neon::TubeOptions o;
        o.w = w; o.off = off; o.style = 0;
        Neon::tube (g, l, c, o);
    }
    static void tubeOn (juce::Graphics& gg, const Line& l, juce::Colour c, float w)
    {
        Neon::TubeOptions o;
        o.w = w; o.style = 0;
        Neon::tube (gg, l, c, o);
    }
    float word (const juce::String& t, float x, float y, float size, juce::Colour c, bool on, Neon::Align al = Neon::Align::left)
    {
        return Neon::text (g, t, x, y, size, c, on ? 1.0f : 0.0f, al);
    }
    void small (const juce::String& t, float x, float y, juce::Colour c, Neon::Align al = Neon::Align::left, float size = 11.0f)
    {
        Neon::label (g, t, x, y, al, c, size);
    }
    void glassBox (juce::Rectangle<float> r, float radius, juce::Colour fill)
    {
        g.setColour (fill);
        g.fillRoundedRectangle (r, radius);
        g.setColour (juce::Colour (0xff3a3c42));
        g.drawRoundedRectangle (r, radius, 1.0f);
    }

    void setV (int key, float value)
    {
        S().v[(size_t) key] = juce::jlimit (0.0f, 1.0f, value);
        v.last = juce::String (faderLabel (key)) + "  " + faderText (key, S().v[(size_t) key]);
        v.edited = true;
        v.changed();
    }
    std::function<void()> resetOf (int key)
    {
        return [this_ = &v, key]
        {
            Settings defaults;
            this_->settings.v[(size_t) key] = defaults.v[(size_t) key];
            this_->last = juce::String (faderLabel (key)) + "  " + faderText (key, defaults.v[(size_t) key]);
            this_->edited = true;
            this_->changed();
        };
    }
    // The closures outlive this object, so they reach the visualiser through a plain pointer
    std::function<void (float)> setter (int key)
    {
        return [this_ = &v, key] (float value)
        {
            auto& s = this_->settings;
            s.v[(size_t) key] = juce::jlimit (0.0f, 1.0f, value);
            this_->last = juce::String (faderLabel (key)) + "  " + faderText (key, s.v[(size_t) key]);
            this_->edited = true;
            this_->changed();
        };
    }
    juce::String name (int key) { return juce::String (faderLabel (key)).toLowerCase(); }
    void valTxt (int key, float x, float y, Neon::Align al = Neon::Align::right) { small (faderText (key, S().v[(size_t) key]), x, y, grey, al); }

    // A row of sign words, one lit
    float words (const juce::StringArray& list, float x, float y, float size, juce::Colour c, int selected, float gap, std::function<void (int)> pick)
    {
        float cx = x;
        for (int i = 0; i < list.size(); ++i)
        {
            const float w = word (list[i], cx, y, size, c, i == selected);
            hit ({ cx - 4.0f, y - size, w + 8.0f, size * 1.3f }, {}, [pick, i] { pick (i); });
            cx += w + gap;
        }
        return cx;
    }

    // Tall tube you drag up and down; a small bead beside it bounces with the music
    void vtube (int key, float x, float y, float h, juce::Colour c, std::function<float()> signal, const juce::String& label = {})
    {
        const float val = S().v[(size_t) key];
        tube ({ { x, y + h }, { x, y } }, c, 1.0f, true);
        if (val > 0.01f) tube ({ { x, y + h }, { x, y + h - h * val } }, c);
        Neon::bulb (g, x, y + h - h * val, c, 1.0f, 5.5f);
        word (label.isEmpty() ? name (key) : label, x, y + h + 22.0f, 14.0f, c, true, Neon::Align::centre);
        hit ({ x - 24.0f, y - 10.0f, 48.0f, h + 38.0f }, [set = setter (key), y, h] (float, float py) { set ((y + h - py) / h); }, {}, resetOf (key));
        dyn ([=] (juce::Graphics& gg) { const float lv = juce::jlimit (0.0f, 1.0f, signal()); Neon::bulb (gg, x + 12.0f, y + h - h * val * lv, c, 0.3f + lv * 0.7f, 3.0f); });
    }

    // Dial: drag round the ring; the spokes keep turning to show what it does
    void wheel (int key, float cx, float cy, float R, juce::Colour c, std::function<float()> angle, const juce::String& label = {})
    {
        const float val = S().v[(size_t) key], a0 = juce::MathConstants<float>::pi * 0.75f, a1 = juce::MathConstants<float>::pi * 2.25f;
        auto arc = [&] (float t0, float t1)
        {
            Line p;
            for (int i = 0; i <= 28; ++i) { const float a = a0 + (a1 - a0) * (t0 + (t1 - t0) * (float) i / 28.0f); p.push_back ({ cx + std::cos (a) * R, cy + std::sin (a) * R }); }
            return p;
        };
        tube (arc (0.0f, 1.0f), c, 1.0f, true);
        if (val > 0.01f) tube (arc (0.0f, val), c);
        const float a = a0 + (a1 - a0) * val;
        Neon::bulb (g, cx + std::cos (a) * R, cy + std::sin (a) * R, c, 1.0f, 5.5f);
        word (label.isEmpty() ? name (key) : label, cx, cy + R + 20.0f, 14.0f, c, true, Neon::Align::centre);
        valTxt (key, cx, cy + R + 34.0f, Neon::Align::centre);
        hit ({ cx - R - 12.0f, cy - R - 12.0f, R * 2.0f + 24.0f, R * 2.0f + 50.0f }, [set = setter (key), cx, cy, a0, a1] (float px, float py)
        {
            float t = std::atan2 (py - cy, px - cx);
            if (t < juce::MathConstants<float>::halfPi) t += juce::MathConstants<float>::twoPi;
            set ((t - a0) / (a1 - a0));
        }, {}, resetOf (key));
        dyn ([=] (juce::Graphics& gg)
        {
            const float th = angle();
            for (int i = 0; i < 3; ++i)
            {
                const float b = th + (float) i * 2.0944f;
                tubeOn (gg, { { cx + std::cos (b) * 5.0f, cy + std::sin (b) * 5.0f }, { cx + std::cos (b) * (R - 10.0f), cy + std::sin (b) * (R - 10.0f) } }, c, 0.6f);
            }
        });
    }

    // Shaped tube fader: the tube itself shows what the control does
    enum Shape { plain, wave, jag, comet, rings, rainbow };
    void sfader (int key, float x, float y, float w, juce::Colour c, Shape kind, const juce::String& label = {})
    {
        const float val = S().v[(size_t) key];
        const int N = 44;
        auto f = [&] (float t) { return kind == wave ? std::sin (t * 20.0f) * 9.0f * t : kind == jag ? ((juce::roundToInt (t * (float) N) % 2) != 0 ? 1.0f : -1.0f) * 7.0f * t : 0.0f; };
        auto pts = [&] (float a, float b)
        {
            Line p;
            const int n = juce::jmax (2, juce::roundToInt ((float) N * (b - a)));
            for (int i = 0; i <= n; ++i) { const float t = a + (b - a) * (float) i / (float) n; p.push_back ({ x + w * t, y + f (t) }); }
            return p;
        };
        if (kind == rainbow) c = hsl (val * 330.0f, 0.9f, 0.6f);
        word (label.isEmpty() ? name (key) : label, x, y - 16.0f, 16.0f, c, true);
        valTxt (key, x + w, y - 17.0f);
        if (kind == rainbow)
        {
            const int n = 14;
            for (int i = 0; i < n; ++i)
            {
                const float t0 = (float) i / (float) n, t1 = (float) (i + 1) / (float) n;
                tube ({ { x + w * t0 + 1.5f, y }, { x + w * t1 - 1.5f, y } }, hsl (t0 * 330.0f, 0.9f, 0.6f), 1.0f, t0 >= val);
            }
        }
        else
        {
            tube (pts (0.0f, 1.0f), c, 1.0f, true);
            if (val > 0.02f) tube (pts (0.0f, val), c);
        }
        if (kind == comet)
            for (int j = 1; j <= 4; ++j) { const float t = val - (float) j * 0.065f; if (t > 0.0f) Neon::bulb (g, x + w * t, y, c, 0.75f - (float) j * 0.15f, 4.6f - (float) j * 0.6f); }
        if (kind == rings)
        {
            const float gap = 10.0f + val * 34.0f;
            int j = 0;
            for (float px = x + w - 6.0f; px > x + w * val + 12.0f; px -= gap, ++j)
            {
                const float R = 7.0f - (float) j * 0.5f;
                if (R < 2.0f) break;
                Line q;
                for (int i = 0; i <= 16; ++i) q.push_back ({ px + std::cos ((float) i * 0.3927f) * R * 0.5f, y + std::sin ((float) i * 0.3927f) * R });
                tube (q, c, 0.6f);
            }
        }
        Neon::bulb (g, x + w * val, y + f (val), c, 1.0f, 5.5f);
        hit ({ x - 10.0f, y - 30.0f, w + 20.0f, 46.0f }, [set = setter (key), x, w] (float px, float) { set ((px - x) / w); }, {}, resetOf (key));
    }

    // A row of lamps you click to pick a count
    void bulbCount (int key, int n, float x, float y, float w, juce::Colour c, bool fromZero)
    {
        const int idx = juce::roundToInt (S().v[(size_t) key] * (float) (n - 1));
        const float gap = (w - 16.0f) / (float) (n - 1);
        word (name (key), x, y - 16.0f, 16.0f, c, true);
        valTxt (key, x + w, y - 17.0f);
        for (int i = 0; i < n; ++i)
        {
            const bool on = fromZero ? (i > 0 && i <= idx) : i <= idx;
            Neon::bulb (g, x + 8.0f + (float) i * gap, y, c, on ? 1.0f : 0.0f, i == 0 && fromZero ? 3.0f : 5.0f);
        }
        hit ({ x - 6.0f, y - 30.0f, w + 12.0f, 46.0f }, [set = setter (key), x, gap, n] (float px, float)
        {
            set (std::round (juce::jlimit (0.0f, (float) (n - 1), (px - x - 8.0f) / gap)) / (float) (n - 1));
        }, {}, resetOf (key));
    }

    // Colour ring: the lamp's angle is the hue, its distance from the middle is the tint
    void hueWheel (float cx, float cy, float R)
    {
        const int n = 30, hueKey = S().hueKey(), tintKey = S().tintKey();
        const float twoPi = juce::MathConstants<float>::twoPi;
        for (int i = 0; i < n; ++i)
        {
            const float a0 = (float) i / (float) n * twoPi + 0.03f, a1 = (float) (i + 1) / (float) n * twoPi - 0.03f;
            Line p;
            for (int j = 0; j <= 3; ++j) { const float a = a0 + (a1 - a0) * (float) j / 3.0f; p.push_back ({ cx + std::cos (a) * R, cy + std::sin (a) * R }); }
            tube (p, hsl (((float) i + 0.5f) / (float) n * 360.0f, 0.9f, 0.58f), 0.8f);
        }
        const auto pal = S().palette();
        const float hue = S().v[(size_t) hueKey], tint = S().v[(size_t) tintKey], h = hue * 360.0f;
        const int tones = S().w[(size_t) S().tonesKey()];
        const float others[2] = { tones == 0 ? h - 60.0f * tint : tones == 2 ? h + 150.0f : tones == 3 ? h + 120.0f : h + 180.0f,
                                  tones == 0 ? h + 22.0f * tint : tones == 2 ? h + 210.0f : tones == 3 ? h + 240.0f : h - 30.0f * tint };
        for (int i = 0; i < 2; ++i)
        {
            const float b = others[i] / 180.0f * juce::MathConstants<float>::pi;
            Neon::bulb (g, cx + std::cos (b) * (R - 11.0f), cy + std::sin (b) * (R - 11.0f), pal[(size_t) i + 1], 0.9f, 3.5f);
        }
        const float rr = R * (0.25f + 0.55f * tint), a = hue * twoPi, lx = cx + std::cos (a) * rr, ly = cy + std::sin (a) * rr;
        tube ({ { cx, cy }, { lx, ly } }, pal[0], 0.6f);
        Neon::bulb (g, cx, cy, pal[0], 0.5f, 3.0f);
        Neon::bulb (g, lx, ly, pal[0], 1.0f, 7.0f);
        hit ({ cx - R - 10.0f, cy - R - 10.0f, R * 2.0f + 20.0f, R * 2.0f + 20.0f }, [this_ = &v, hueKey, tintKey, cx, cy, R] (float px, float py)
        {
            const float dx = px - cx, dy = py - cy;
            auto& s = this_->settings;
            float hh = std::fmod (std::atan2 (dy, dx) / juce::MathConstants<float>::twoPi, 1.0f);
            if (hh < 0.0f) hh += 1.0f;
            s.v[(size_t) hueKey] = hh;
            s.v[(size_t) tintKey] = juce::jlimit (0.0f, 1.0f, (std::hypot (dx, dy) / R - 0.25f) / 0.55f);
            this_->last = "Hue  " + faderText (hueKey, hh) + "   Tint  " + faderText (tintKey, s.v[(size_t) tintKey]);
            this_->edited = true;
            this_->changed();
        });
    }

    // Pad: drag the lamp, the lines inside show the flow you get
    void xyPad (int kx, int ky, float x, float y, float w, float h, juce::Colour c)
    {
        const float vx = S().v[(size_t) kx], vy = S().v[(size_t) ky];
        glassBox ({ x, y, w, h }, 10.0f, juce::Colour (0x80080a0a));
        for (int j = 0; j < 3; ++j)
        {
            Line p;
            for (float px = x + 10.0f; px <= x + w - 10.0f; px += 4.0f)
                p.push_back ({ px, y + h * (0.26f + 0.24f * (float) j) + std::sin ((px - x) * (0.045f + vx * 0.22f) + (float) j * 1.3f) * (1.5f + vy * 8.0f) });
            tube (p, c, 0.6f, j != 1);
        }
        Neon::bulb (g, x + 10.0f + (w - 20.0f) * vx, y + h - 10.0f - (h - 20.0f) * vy, c, 1.0f, 6.5f);
        word (name (ky) + " up", x, y + h + 18.0f, 13.0f, c, true);
        word (name (kx) + " across", x + w, y + h + 18.0f, 13.0f, c, true, Neon::Align::right);
        hit ({ x, y, w, h }, [this_ = &v, kx, ky, x, y, w, h] (float px, float py)
        {
            auto& s = this_->settings;
            s.v[(size_t) kx] = juce::jlimit (0.0f, 1.0f, (px - x - 10.0f) / (w - 20.0f));
            s.v[(size_t) ky] = juce::jlimit (0.0f, 1.0f, (y + h - 10.0f - py) / (h - 20.0f));
            this_->last = juce::String (faderLabel (ky)) + "  " + faderText (ky, s.v[(size_t) ky]) + "   " + faderLabel (kx) + "  " + faderText (kx, s.v[(size_t) kx]);
            this_->edited = true;
            this_->changed();
        });
    }

    void layersPic (float x, float y, juce::Colour c, int key)
    {
        const float d = S().v[(size_t) key] * 11.0f;
        for (int j = 2; j >= 0; --j)
            tube (Neon::roundRect (x + (float) j * d, y - (float) j * d * 0.7f, 46.0f, 28.0f, 6.0f), c, 0.6f, j > 0 && S().v[(size_t) key] < 0.02f);
    }

    // Picture buttons: the look (row 0) and the form (row 1) of the mode
    void iconRow (int row, float x, float y, float bw, juce::Colour c, bool stack)
    {
        static const char* const names[4][2][4] = { { { "smoke", "oil", "neon", nullptr }, { "blot", "petals", "rays", "tiles" } },
                                                    { { "lamp", "glass", "neon", nullptr }, { "drift", "lava", "orbit", "mirror" } },
                                                    { { "grains", "dust", "bulbs", nullptr }, { "square", "round", "ripple", nullptr } },
                                                    { { "glow", "lines", "relief", nullptr }, { "coral", "cells", "maze", nullptr } } };
        const int style = S().style, key = row == 0 ? S().surfaceKey() : S().formKey();
        for (int i = 0; i < 4 && names[style][row][i] != nullptr; ++i)
        {
            if (Theme::get().isRainbow()) c = Theme::get().pal (i + (row == 1 ? 3 : 0));
            const bool on = S().w[(size_t) key] == i;
            const float bx = x + (float) i * bw, sc = stack ? 1.15f : 1.2f, fw = bw - 8.0f, fh = stack ? 74.0f : 46.0f, fy = stack ? y - 24.0f : y - 23.0f;
            if (on) tube (Neon::roundRect (bx, fy, fw, fh, 10.0f), c, 0.7f);
            else    glassBox ({ bx, fy, fw, fh }, 10.0f, juce::Colour (0x730a0a0c));
            const float cx = stack ? bx + fw * 0.5f : bx + 26.0f, cy = y;
            for (const auto& line : VisIcons::icons[style][row][i])
            {
                Line p;
                for (auto pt : line) p.push_back ({ cx + pt.x * sc, cy + pt.y * sc });
                tube (p, c, 0.85f, ! on);
            }
            if (stack) word (names[style][row][i], cx, cy + 40.0f, 16.0f, c, on, Neon::Align::centre);
            else       word (names[style][row][i], bx + 52.0f, cy + 8.0f, 21.0f, c, on);
            hit ({ bx, fy, fw, fh }, {}, [this_ = &v, key, i] { this_->settings.w[(size_t) key] = i; this_->edited = true; this_->changed(); });
        }
    }

    // A named row of words for one of the mode's choices
    void wrow (const juce::String& title, int key, const juce::StringArray& options, float x, float y, juce::Colour c, bool showPalette = false)
    {
        const float kw = word (title, x, y, 15.0f, c, false);
        const float end = words (options, x + kw + 14.0f, y, 17.0f, c, S().w[(size_t) key], 12.0f,
                                 [this_ = &v, key] (int i) { this_->settings.w[(size_t) key] = i; this_->edited = true; this_->changed(); });
        if (showPalette)
        {
            const auto pal = S().palette();
            for (int i = 0; i < 3; ++i) Neon::bulb (g, end + 4.0f + (float) i * 16.0f, y - 6.0f, pal[(size_t) i], 1.0f, 5.0f);
        }
    }

    //==========================================================================
    void colourTab (float x, float y, float w)
    {
        wrow ("tones", S().tonesKey(), { "one", "complement", "split", "triad" }, x, y, nc(), true);
        hueWheel (x + 74.0f, y + 98.0f, 62.0f);
        const float rx = x + 172.0f, rw = w - 172.0f;
        sfader (S().psyKey(), rx, y + 70.0f, rw, {}, rainbow);
        const char* const help[] = { "Drag the lamp in the ring:", "round for hue,", "in and out for tint." };
        for (int i = 0; i < 3; ++i) small (help[i], rx, y + 108.0f + (float) i * 15.0f, dim);
    }
    void trailsRow (float x, float y) { wrow ("trails", S().trailModeKey(), { "smooth", "echo" }, x, y, nc()); }
    void idleWheel (float x, float y)
    {
        wheel (S().idleKey(), x + 58.0f, y + 50.0f, 44.0f, nc(), [r = &v.readout] { return -r->turn.load(); }, "idle spin");
    }

    void tabContent (float x, float y, float w)
    {
        const int style = S().style, section = v.tab;
        const float hw = (w - 28.0f) / 2.0f, rx = x + 172.0f, rw = w - 172.0f;
        const juce::StringArray mirrors { "off", "2-way", "4-way" };
        if (section == 1) { colourTab (x, y, w); return; }
        if (style == ink)
        {
            if (section == 0)
            {
                wrow ("mirror", C::mirror, mirrors, x, y, nc());
                bulbCount (F::seg, 10, x, y + 46.0f, w, nc(), false);
                sfader (F::trip, x, y + 100.0f, w, nc(), wave);
                const auto cd = nc();
                layersPic (x, y + 142.0f, cd, F::depth);
                sfader (F::depth, x + 86.0f, y + 158.0f, w - 86.0f, cd, plain);
            }
            else if (section == 2)
            {
                idleWheel (x, y);
                sfader (F::spin, x, y + 166.0f, 132.0f, nc(), plain);
                sfader (F::idetail, x, y + 218.0f, 132.0f, nc(), plain);
                xyPad (F::scale, F::swirl, rx, y, rw, 82.0f, nc());
                sfader (F::chaos, rx, y + 166.0f, rw, nc(), jag);
                bulbCount (F::vort, 7, rx, y + 218.0f, rw, nc(), true);
            }
            else
            {
                trailsRow (x, y);
                sfader (F::trails, x, y + 52.0f, w, nc(), comet);
                sfader (F::drops, x, y + 108.0f, hw, nc(), plain);
                sfader (F::fade, x + hw + 28.0f, y + 108.0f, hw, nc(), plain);
            }
        }
        else if (style == liquid)
        {
            if (section == 0)
            {
                bulbCount (F::lcount, 9, x, y + 22.0f, w, nc(), false);
                sfader (F::lsize, x, y + 84.0f, hw, nc(), plain);
                sfader (F::lwob, x + hw + 28.0f, y + 84.0f, hw, nc(), wave);
                sfader (F::ldrops, x, y + 142.0f, w, nc(), comet);
                const auto cd = nc();
                layersPic (x, y + 184.0f, cd, F::ldepth);
                sfader (F::ldepth, x + 86.0f, y + 200.0f, w - 86.0f, cd, plain);
            }
            else if (section == 2)
            {
                idleWheel (x, y);
                sfader (F::lspin, rx, y + 24.0f, rw, nc(), plain);
                sfader (F::lflow, rx, y + 80.0f, rw, nc(), wave);
                sfader (F::lswell, x, y + 166.0f, w, nc(), rings);
            }
            else { trailsRow (x, y); sfader (F::ltrails, x, y + 52.0f, w, nc(), comet); }
        }
        else if (style == sand)
        {
            if (section == 0)
            {
                sfader (F::sgrain, x, y + 22.0f, hw, nc(), jag);
                sfader (F::scomp, x + hw + 28.0f, y + 22.0f, hw, nc(), wave);
                sfader (F::ssharp, x, y + 80.0f, hw, nc(), plain);
                sfader (F::sscatter, x + hw + 28.0f, y + 80.0f, hw, nc(), jag);
                sfader (F::schange, x, y + 138.0f, w, nc(), comet);
            }
            else if (section == 2) { idleWheel (x, y); sfader (F::sspin, rx, y + 40.0f, rw, nc(), plain); }
            else { trailsRow (x, y); sfader (F::strails, x, y + 52.0f, w, nc(), comet); }
        }
        else
        {
            if (section == 0)
            {
                wrow ("mirror", C::bmirror, mirrors, x, y, nc());
                sfader (F::bspeed, x, y + 56.0f, hw, nc(), comet);
                sfader (F::bcover, x + hw + 28.0f, y + 56.0f, hw, nc(), wave);
                sfader (F::bseeds, x, y + 114.0f, hw, nc(), jag);
                sfader (F::bfresh, x + hw + 28.0f, y + 114.0f, hw, nc(), plain);
            }
            else if (section == 2) { idleWheel (x, y); sfader (F::bspin, rx, y + 40.0f, rw, nc(), plain); }
            else { trailsRow (x, y); sfader (F::btrails, x, y + 52.0f, w, nc(), comet); }
        }
    }

    // Routing: a patch bay. Pull a cable from a sound to a reaction; slide its bead for strength.
    void routing (float x, float y, float w)
    {
        auto* r = &v.readout;
        auto* self = &v;

        // Loud lows only: each bar is a low hit, only bars above the line count
        const auto gc = nc();
        const float gy = y + 54.0f, gh = 50.0f, gate = S().v[F::gate];
        word ("loudest lows", x, y + 44.0f, 16.0f, gc, true);
        small ("low hits above the line", x + 118.0f, y + 42.0f, dim);
        valTxt (F::gate, x + w, y + 42.0f);
        glassBox ({ x, gy, w, gh }, 8.0f, juce::Colour (0x8008080a));
        const float ly = gy + gh - 4.0f - (gh - 8.0f) * gate;
        tube ({ { x + 4.0f, ly }, { x + w - 4.0f, ly } }, gc, 0.5f);
        Neon::bulb (g, x + w - 8.0f, ly, gc, 1.0f, 5.0f);
        hit ({ x, gy - 8.0f, w, gh + 16.0f }, [set = setter (F::gate), gy, gh] (float, float py) { set ((gy + gh - 4.0f - py) / (gh - 8.0f)); }, {}, resetOf (F::gate));
        dyn ([=] (juce::Graphics& gg)
        {
            const double now = r->clock.load();
            const juce::SpinLock::ScopedLockType lock (r->lock);
            for (int i = 0; i < r->numKicks; ++i)
            {
                const auto& k = r->kicks[(size_t) i];
                const float age = (float) (now - k.time);
                if (age > 5.0f) continue;
                const float bx = x + w - 14.0f - age / 5.0f * (w - 24.0f), bh = juce::jmax (3.0f, (gh - 8.0f) * k.rel);
                gg.setColour (k.pass ? gc.interpolatedWith (juce::Colours::white, 0.45f) : juce::Colour (0xff55585f));
                gg.fillRoundedRectangle (bx - 1.5f, gy + gh - 4.0f - bh, 3.0f, bh, 1.5f);
            }
        });

        small ("Drag from a sound to a reaction to wire it. Slide the bead on a", x, y + 128.0f, dim);
        small ("cable for strength; slide it to zero to unplug.", x, y + 142.0f, dim);
        const float by = y + 172.0f, xs = x + 112.0f, xd = x + w - 74.0f;
        auto src = [=] (int i) { return juce::Point<float> (xs, by + (float) i * 28.0f); };
        auto dst = [=] (int i) { return juce::Point<float> (xd, by + (float) i * 44.8f); };
        auto cable = [] (juce::Point<float> a, juce::Point<float> b)
        {
            const float sag = 16.0f + std::abs (b.y - a.y) * 0.08f;
            Line p;
            for (int i = 0; i <= 24; ++i)
            {
                const float t = (float) i / 24.0f, u = 1.0f - t;
                p.push_back ({ u * u * u * a.x + 3 * u * u * t * (a.x + 70.0f) + 3 * u * t * t * (b.x - 70.0f) + t * t * t * b.x,
                               u * u * u * a.y + 3 * u * u * t * (a.y + sag) + 3 * u * t * t * (b.y + sag) + t * t * t * b.y });
            }
            return p;
        };
        std::array<juce::Colour, numSources> sc;
        for (auto& c : sc) c = nc();
        const auto dc = Theme::get().isRainbow() ? juce::Colour (0xffd8dae0) : Theme::get().themeColour();

        // Beads first, so they are picked before the jacks under them
        for (int s = 0; s < numSources; ++s)
            for (int d = 0; d < numDests; ++d)
            {
                const float amount = S().route[(size_t) s][(size_t) d];
                if (amount <= 0.0f) continue;
                const auto line = cable (src (s), dst (d));
                tube (line, sc[(size_t) s], 0.35f + amount * 0.5f, amount < 0.02f);
                const auto bead = Neon::pointAt (line, beadAt (amount));
                Neon::bulb (g, bead.x, bead.y, sc[(size_t) s], 0.35f + amount * 0.65f, 4.5f);
                hit ({ bead.x - 10.0f, bead.y - 10.0f, 20.0f, 20.0f }, [self, s, d, line] (float mx, float my)
                {
                    float& a = self->settings.route[(size_t) s][(size_t) d];
                    a = juce::jlimit (0.001f, 1.0f, (Neon::nearest (line, { mx, my }).t - 0.15f) / 0.7f);
                    self->last = sourceNames[s] + " to " + destNames[d] + (a < 0.03f ? "  let go to unplug" : "  " + juce::String (juce::roundToInt (a * 100.0f)) + "%");
                    self->edited = true;
                    self->changed();
                }, {}, {}, [self, s, d]
                {
                    float& a = self->settings.route[(size_t) s][(size_t) d];
                    if (a < 0.03f) { a = 0.0f; self->last = "unplugged"; self->changed(); }
                });
            }
        for (int s = 0; s < numSources; ++s)
        {
            const auto j = src (s);
            const auto c = sc[(size_t) s];
            word (sourceNames[s], xs - 16.0f, j.y + 6.0f, 16.0f, c, true, Neon::Align::right);
            g.setColour (juce::Colour (0xff101012)); g.fillEllipse (xs - 8.5f, j.y - 8.5f, 17.0f, 17.0f);
            g.setColour (juce::Colour (0xff4a4d55)); g.drawEllipse (xs - 8.5f, j.y - 8.5f, 17.0f, 17.0f, 1.2f);
            dyn ([=] (juce::Graphics& gg) { Neon::bulb (gg, xs, j.y, c, 0.12f + 0.9f * juce::jmin (1.0f, r->signal[(size_t) s].load()), 4.2f); });
            hit ({ x, j.y - 14.0f, xs - x + 14.0f, 28.0f }, [self] (float mx, float my) { self->cableEnd = { mx, my }; self->artDirty = true; self->repaint(); }, {}, {}, [self, dst]
            {
                for (int d = 0; d < numDests; ++d)
                    if (std::abs (self->cableEnd.y - dst (d).y) < 22.0f && self->cableEnd.x > dst (d).x - 40.0f)
                    {
                        self->settings.route[(size_t) self->cableFrom][(size_t) d] = 1.0f;
                        self->last = sourceNames[self->cableFrom] + " to " + destNames[d];
                        self->edited = true;
                    }
                self->cableFrom = -1;
                self->changed();
            });
            v.hits.back().click = [self, s] { self->cableFrom = s; self->artDirty = true; };
        }
        for (int d = 0; d < numDests; ++d)
        {
            const auto j = dst (d);
            word (destNames[d], xd + 16.0f, j.y + 6.0f, 17.0f, dc, true);
            g.setColour (juce::Colour (0xff101012)); g.fillEllipse (xd - 8.5f, j.y - 8.5f, 17.0f, 17.0f);
            g.setColour (juce::Colour (0xff4a4d55)); g.drawEllipse (xd - 8.5f, j.y - 8.5f, 17.0f, 17.0f, 1.2f);
            dyn ([=] (juce::Graphics& gg) { Neon::bulb (gg, xd, j.y, dc, 0.12f + 0.75f * juce::jmin (1.0f, r->reaction[(size_t) d].load()), 4.2f); });
        }
        if (v.cableFrom >= 0)
            dyn ([=, from = v.cableFrom, end = v.cableEnd] (juce::Graphics& gg) { tubeOn (gg, cable (src (from), end), sc[(size_t) from], 0.6f); });

        const float ny = by + 274.0f;
        sfader (F::nudge, x, ny, w, nc(), plain, "sync nudge");
        small ("Each hit is drawn on the frame you hear it. Nudge it if it feels early or late.", x, ny + 24.0f, dim);
        const auto rc = nc();
        const float rw2 = Neon::textWidth ("reset wiring", 15.0f);
        word ("reset wiring", x + w, ny + 56.0f, 15.0f, rc, false, Neon::Align::right);
        hit ({ x + w - rw2 - 6.0f, ny + 40.0f, rw2 + 12.0f, 24.0f }, {}, [self]
        {
            Settings defaults;
            self->settings.resetWiring();
            self->settings.v[F::gate] = defaults.v[F::gate];
            self->settings.v[F::nudge] = defaults.v[F::nudge];
            self->last = "wiring reset";
            self->edited = true;
            self->changed();
        });
        if (v.last.isNotEmpty()) small (v.last, x, ny + 54.0f, juce::Colour (0xffb8bac0));
    }

    // The whole control section, in its own coordinates (360 wide)
    void column (bool strip)
    {
        const float x = 0.0f, w = 360.0f;
        float y = 36.0f;
        auto* self = &v;
        auto* r = &v.readout;
        const bool rainbowOn = Theme::get().isRainbow();
        const auto col = rainbowOn ? juce::Colour (0xffbfeaff) : Theme::get().themeColour();

        words ({ "ink", "liquid", "sand", "bloom" }, x, y, 27.0f, nc(), S().style, 22.0f,
               [self] (int i) { self->settings.style = i; self->last = {}; self->edited = true; self->changed(); });
        y += 34.0f;
        static const char* const rows[] = { "low", "mid", "high" };
        for (int i = 0; i < 3; ++i) word (rows[i], x, y + (float) i * 26.0f + 5.0f, 15.0f, col, false);
        dyn ([=] (juce::Graphics& gg)
        {
            const int n = 14;
            const float x0 = x + 52.0f, gap = (w - 60.0f) / (float) (n - 1);
            for (int i = 0; i < 3; ++i)
            {
                const float level = r->band[(size_t) i].load();
                const auto c = rainbowOn ? Theme::get().pal (i) : col;
                for (int j = 0; j < n; ++j)
                    Neon::bulb (gg, x0 + (float) j * gap, y + (float) i * 26.0f, c, juce::jlimit (0.0f, 1.0f, level * (float) n - (float) j) * 0.95f, 3.6f);
            }
        });
        y += 3.0f * 26.0f + 28.0f;
        words ({ "controls", "routing" }, x, y, 20.0f, nc(), v.page, 20.0f, [self] (int i) { self->page = i; self->last = {}; self->artDirty = true; self->repaint(); });
        if (v.page == 1) { routing (x, y, w); return; }

        {
            const float th = 84.0f, ty = y + 22.0f, gp = 60.0f;
            vtube (F::pulse, x + 18.0f, ty, th, nc(), [r] { return r->reaction[dPulse].load(); });
            vtube (F::flash, x + 18.0f + gp, ty, th, nc(), [r] { return r->reaction[dFlash].load(); });
            vtube (F::stir, x + 18.0f + gp * 2.0f, ty, th, nc(), [r] { return r->reaction[dStir].load() * 0.6f; });
            vtube (F::react, x + 18.0f + gp * 3.0f, ty, th, nc(), [r] { return r->signal[sLevel].load(); }, "gain");
            wheel (F::speed, x + w - 46.0f, ty + th * 0.46f, 38.0f, nc(), [r] { return r->flow.load() * 1.3f; });
            y = ty + th + 62.0f;
        }
        static const char* const styles[] = { "ink", "liquid", "sand", "bloom" };
        word (styles[S().style], x, y, 20.0f, nc(), true);
        if (! strip)
        {
            y += 45.0f; iconRow (0, x, y, w / 4.0f, nc(), true);
            y += 84.0f; iconRow (1, x, y, w / 4.0f, nc(), true);
            y += 84.0f;
        }
        const auto ct = nc();
        words ({ "shape", "colour", "motion", "trails" }, x + 70.0f, y, 19.0f, ct, v.tab, 16.0f, [self] (int i) { self->tab = i; self->last = {}; self->artDirty = true; self->repaint(); });
        tube ({ { x, y + 12.0f }, { x + w, y + 12.0f } }, ct, 0.6f, true);
        y += 46.0f;
        tabContent (x, y, w);
        y += 250.0f;
        if (v.last.isNotEmpty()) small (v.last, x, y + 4.0f, juce::Colour (0xffb8bac0), Neon::Align::left, 11.5f);
    }
};

void Visualiser::paint (juce::Graphics& g)
{
    juce::String error;
    {
        const juce::SpinLock::ScopedLockType lock (readout.lock);
        error = readout.error;
    }
    if (error.isNotEmpty())
    {
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.setFont (Theme::get().body (13.0f));
        g.drawFittedText ("The visualiser could not start on this graphics card:\n" + error, getLocalBounds().reduced (40), juce::Justification::centred, 12);
    }

    // The steady part of the panel (glass, tubes, words, their glow) is kept as a picture and only
    // drawn again when something on it changes. Each frame just lays it down and adds the moving parts.
    const auto l = layout();
    const float scale = (float) g.getInternalContext().getPhysicalPixelScaleFactor();
    const auto look = Theme::get().themeColour().getARGB() ^ (Theme::get().isRainbow() ? 0x5a5a5a5au : 0u);
    if (artDirty || arts.empty() || scale != artScale || getLocalBounds() != artBounds || look != artLook)
    {
        artDirty = false;
        artLook = look;
        artScale = scale;
        artBounds = getLocalBounds();
        auto drawSteady = [&] (juce::Graphics& gg)
        {
            hits.clear();
            live.clear();
            liveAreas.clear();
            const auto pane = juce::Colour (0xc20d0d0f);
            Draw d { *this, gg, {} };

            // The look and the form float along the bottom of the picture when there is room
            if (l.strip != 0)
            {
                d.setPlace ({});
                gg.setColour (pane);
                gg.fillRoundedRectangle (l.stripBox, 14.0f);
                gg.setColour (juce::Colour (0xff34363c));
                gg.drawRoundedRectangle (l.stripBox, 14.0f, 1.0f);
                const bool two = l.strip == 2;
                const float sx = 24.0f, sy = (float) getHeight() - 48.0f;
                const auto col = Theme::get().themeColour();
                d.iconRow (0, sx, two ? sy - 56.0f : sy, 128.0f, col, false);
                d.iconRow (1, two ? sx : sx + 3.0f * 128.0f + 24.0f, sy, 128.0f, col, false);
                d.block (l.stripBox);
                liveAreas.push_back (l.stripBox.getSmallestIntegerContainer());
            }
            if (panelShown)
            {
                d.setPlace ({});
                gg.setColour (pane);
                gg.fillRoundedRectangle (l.panel, 14.0f);
                gg.setColour (juce::Colour (0xff34363c));
                gg.drawRoundedRectangle (l.panel.reduced (0.5f), 14.0f, 1.0f);
                const auto col = Theme::get().themeColour();
                const float hw = Neon::textWidth ("hide", 15.0f * l.K);
                d.word ("hide", l.panel.getRight() - 16.0f, l.panel.getY() + 12.0f + 18.0f * l.K, 15.0f * l.K, col, false, Neon::Align::right);
                d.hit ({ l.panel.getRight() - 20.0f - hw, l.panel.getY() + 4.0f, hw + 10.0f, 14.0f + 18.0f * l.K }, {}, [this] { panelShown = false; artDirty = true; repaint(); });
                {
                    juce::Graphics::ScopedSaveState state (gg);
                    gg.reduceClipRegion (l.panel.getSmallestIntegerContainer());
                    gg.addTransform (l.column);
                    d.setPlace (l.column);
                    d.column (l.strip != 0);
                }
                d.setPlace ({});
                d.block (l.panel);
                liveAreas.push_back (l.panel.getSmallestIntegerContainer());
            }
            else
            {
                d.setPlace ({});
                const auto col = Theme::get().themeColour();
                const float w = Neon::textWidth ("show controls", 19.0f);
                d.word ("show controls", (float) getWidth() - 20.0f, 34.0f, 19.0f, col, true, Neon::Align::right);
                d.hit ({ (float) getWidth() - 26.0f - w, 10.0f, w + 12.0f, 34.0f }, {}, [this] { panelShown = true; artDirty = true; repaint(); });
            }
            Neon::glow().base = Neon::glow().current = {};
        };

        // Once with nothing drawn, to gather the lit shapes for the glow and find where the pieces are
        auto& glow = Neon::glow();
        glow.sources.clear();
        glow.collecting = true;
        glow.alpha = 1.0f;
        {
            juce::Image none (juce::Image::ARGB, 1, 1, true);
            juce::Graphics dg (none);
            dg.reduceClipRegion (juce::Rectangle<int>());
            drawSteady (dg);
        }
        glow.collecting = false;
        glowLayer.build (getWidth(), getHeight(), l.K);

        // Then for real, into a picture for each piece (a new picture each time: one drawn over in place can show stale on the Mac)
        auto areas = liveAreas;
        if (! panelShown) areas.push_back ({ getWidth() - 280, 0, 280, 56 });
        arts.clear();
        for (auto area : areas)
        {
            area = area.expanded (8).getIntersection (getLocalBounds());
            if (area.isEmpty()) continue;
            juce::Image image (juce::Image::ARGB, juce::jmax (1, juce::roundToInt ((float) area.getWidth() * scale)), juce::jmax (1, juce::roundToInt ((float) area.getHeight() * scale)), true);
            {
                juce::Graphics ig (image);
                ig.addTransform (juce::AffineTransform::translation ((float) -area.getX(), (float) -area.getY()).scaled (scale));
                glowLayer.draw (ig, getLocalBounds().toFloat());
                drawSteady (ig);
            }
            arts.push_back ({ area, image });
        }
    }
    for (auto& piece : arts)
        g.drawImage (piece.second, piece.first.toFloat());

    // The parts that move with the music are drawn on top and leave the steady glow alone
    for (auto& part : live)
    {
        juce::Graphics::ScopedSaveState state (g);
        g.addTransform (part.place);
        part.draw (g);
    }
}

//==============================================================================
// Pointer

const Visualiser::Hit* Visualiser::hitAt (juce::Point<float> p) const
{
    for (auto& h : hits)
        if (h.box.contains (p)) return &h;
    return nullptr;
}

void Visualiser::mouseDown (const juce::MouseEvent& e)
{
    if (auto* h = hitAt (e.position))
    {
        const auto hitCopy = *h;            // the list is rebuilt by the repaint a click can cause
        if (hitCopy.click != nullptr) hitCopy.click();
        activeDrag = hitCopy.drag;
        activeUp = hitCopy.up;
        if (activeDrag != nullptr) activeDrag (e.position.x, e.position.y);
        repaint();
        return;
    }
    // Anywhere on the picture: stir it by hand
    draggingPicture = true;
    dragX = e.position.x / (float) getWidth();
    dragY = e.position.y / (float) getHeight();
}

void Visualiser::mouseDrag (const juce::MouseEvent& e)
{
    if (draggingPicture)
    {
        dragX = juce::jlimit (0.0f, 1.0f, e.position.x / (float) getWidth());
        dragY = juce::jlimit (0.0f, 1.0f, e.position.y / (float) getHeight());
    }
    else if (activeDrag != nullptr)
        activeDrag (e.position.x, e.position.y);
}

void Visualiser::mouseUp (const juce::MouseEvent&)
{
    if (draggingPicture) { draggingPicture = false; dragX = -1.0f; dragY = -1.0f; }
    if (activeUp != nullptr) { const auto up = activeUp; up(); }
    activeDrag = nullptr;
    activeUp = nullptr;
    saveDue = true;
    repaint();
}

void Visualiser::mouseMove (const juce::MouseEvent& e)
{
    auto* h = hitAt (e.position);
    setMouseCursor (h != nullptr && (h->drag != nullptr || h->click != nullptr) ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void Visualiser::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (auto* h = hitAt (e.position))
    {
        if (h->reset != nullptr) { const auto reset = h->reset; reset(); }       // back to where it started
        return;
    }
    panelShown = ! panelShown;
    artDirty = true;
    repaint();
}

void Visualiser::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) {}

void Visualiser::resized()
{
    changed();
}
