#include "Visualiser.h"
#include "VisualiserRender.h"
#include "../Theme.h"

using namespace Vis;
using Neon::Line;

namespace
{
    constexpr float panelWidth = 392.0f, margin = 20.0f, inner = panelWidth - margin * 2.0f;
    const juce::StringArray sourceNames { "kick", "snare", "hats", "loudest lows", "transients", "low", "mid", "high", "loudness" };
    const juce::StringArray destNames { "pulse", "flash", "spin", "stir", "burst", "colour" };
    constexpr float jackSourceX = 150.0f, jackDestX = 286.0f, bayTop = 236.0f, sourceStep = 30.0f, destStep = 45.0f;

    juce::Point<float> sourceJack (int s) { return { jackSourceX, bayTop + (float) s * sourceStep }; }
    juce::Point<float> destJack (int d)   { return { jackDestX, bayTop + (float) d * destStep }; }
    Line cable (juce::Point<float> a, juce::Point<float> b)
    {
        return Neon::curve (a, { (a.x + b.x) * 0.5f, juce::jmax (a.y, b.y) + 16.0f + std::abs (b.y - a.y) * 0.08f }, b, 24);
    }
    float beadAt (float amount) { return 0.15f + 0.7f * amount; }
}

Visualiser::Visualiser (AppContext& c, juce::Component& w) : ctx (c), window (w)
{
    setInterceptsMouseClicks (true, false);
    setWantsKeyboardFocus (false);
    renderer = std::make_unique<Renderer>();
    panel.labelSize = 15.0f;
    panel.colours = [this] (int mod) -> const juce::Colour*
    {
        static juce::Colour set[3];
        auto& th = Theme::get();
        const auto base = th.isRainbow() ? th.pal (mod) : th.themeColour();
        set[0] = set[1] = set[2] = base;
        return set;
    };
    shared = settings;
    rebuildPanel();
}

Visualiser::~Visualiser()
{
    gl.detach();
}

//==============================================================================

void Visualiser::setView (View v)
{
    if (v == view) return;
    view = v;
    ctx.engine.visListener.enabled = v != off;
    if (v == off)
    {
        gl.detach();
        Theme::get().setGlass (0.0f);
    }
    else
    {
        if (! gl.isAttached())
        {
            gl.setRenderer (this);
            gl.setOpenGLVersionRequired (juce::OpenGLContext::openGL3_2);
            gl.setComponentPaintingEnabled (true);
            gl.setContinuousRepainting (true);
            gl.setSwapInterval (1);
            gl.attachTo (window);
        }
        Theme::get().setGlass (v == behind ? 0.3f + 0.55f * settings.v[F::back] : 0.0f);
    }
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
    rebuildPanel();
    resized();
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
        rebuildPanel();
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
    }
    saveDue = true;
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
    // The song remembers the look: written back a moment after the last change, not on every drag
    if (saveDue && ! panel.isDragging())
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
    if (view == full && panelShown && (++frameCount & 1) == 0)
        repaint (panelBounds().getSmallestIntegerContainer());      // the lamps and meters, 30 times a second
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
    {
        const juce::SpinLock::ScopedLockType lock (sharedLock);
        s = shared;
        area = sharedArea;
    }
    const bool isBehind = area.getY() == 0;
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
}

//==============================================================================
// The control panel: neon glass on a dark pane at the right of the picture

juce::Rectangle<float> Visualiser::panelBounds() const
{
    const float w = panelWidth * panelScale, h = panelHeight * panelScale;
    return { (float) getWidth() - w - 12.0f, 12.0f, w, juce::jmin (h, (float) getHeight() - 24.0f) };
}

juce::Point<float> Visualiser::toPanel (juce::Point<float> p) const
{
    const auto b = panelBounds();
    return { (p.x - b.getX()) / panelScale, (p.y - b.getY()) / panelScale };
}

void Visualiser::resized()
{
    panelScale = juce::jlimit (0.7f, 1.4f, ((float) getHeight() - 24.0f) / panelHeight);
    panelScale = juce::jmin (panelScale, ((float) getWidth() - 24.0f) / panelWidth);
    changed();
}

Neon::Item& Visualiser::fader (Neon::Group& g, int key, float x, float y, float w)
{
    auto& it = g.fader (x, y, x + w, y);
    it.text = juce::String (faderLabel (key)).toLowerCase();
    Settings defaults;
    it.def = defaults.v[(size_t) key];
    it.bipolar = key == F::nudge;
    it.get = [this, key] { return settings.v[(size_t) key]; };
    it.set = [this, key] (float v)
    {
        settings.v[(size_t) key] = v;
        edited = true;
        if (key == F::back && view == behind) Theme::get().setGlass (0.3f + 0.55f * v);
        changed();
    };
    it.tip = [this, key] { return juce::String (faderLabel (key)) + "  " + faderText (key, settings.v[(size_t) key]); };
    return it;
}

Neon::Item& Visualiser::choice (Neon::Group& g, const juce::String& title, int key, juce::StringArray options, float x, float y)
{
    std::vector<std::pair<int, juce::String>> opts;
    for (int i = 0; i < options.size(); ++i) opts.push_back ({ i, options[i] });
    auto& it = g.words (opts, x, y);
    it.pre = title;
    it.size = 17.0f;
    it.gap = 11.0f;
    it.getChoice = [this, key] { return settings.w[(size_t) key]; };
    it.setChoice = [this, key] (int v) { settings.w[(size_t) key] = v; edited = true; changed(); };
    return it;
}

void Visualiser::rebuildPanel()
{
    panel.groups.clear();
    auto& g = panel.add (0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);

    // The four modes, and the word that puts the panel away
    {
        auto& modes = g.words ({ { ink, "ink" }, { liquid, "liquid" }, { sand, "sand" }, { bloom, "bloom" } }, margin, 40.0f);
        modes.size = 25.0f; modes.gap = 16.0f;
        modes.getChoice = [this] { return settings.style; };
        modes.setChoice = [this] (int v) { settings.style = v; edited = true; changed(); pendingRebuild = true; };
        auto& hide = g.words ({ { 0, "hide" } }, panelWidth - margin - 34.0f, 34.0f);
        hide.size = 14.0f;
        hide.getChoice = [] { return 1; };
        hide.setChoice = [this] (int) { panelShown = false; repaint(); };
    }
    {
        auto& pages = g.words ({ { 0, "controls" }, { 1, "routing" } }, margin, 132.0f);
        pages.size = 20.0f; pages.gap = 18.0f;
        pages.getChoice = [this] { return page; };
        pages.setChoice = [this] (int v) { page = v; pendingRebuild = true; };
    }
    float y = 150.0f;
    if (page == 0) buildControls (g, y); else buildRouting (g, y);
    panelHeight = y + 34.0f;

    // The live parts (levels, lamps, cables) are drawn every frame and leave the steady glow alone
    g.draw = [this] (juce::Graphics& gg, double, float, const juce::Colour* c)
    {
        auto& glow = Neon::glow();
        const bool was = glow.collecting;
        glow.collecting = false;
        static const char* const rows[] = { "low", "mid", "high" };
        for (int r = 0; r < 3; ++r)
        {
            const float yy = 64.0f + (float) r * 18.0f, level = readout.band[(size_t) r].load();
            Neon::label (gg, rows[r], margin, yy + 4.0f);
            for (int i = 0; i < 14; ++i)
                Neon::bulb (gg, margin + 58.0f + (float) i * 21.0f, yy, c[0], (float) i < level * 14.0f ? 1.0f : 0.12f, 3.2f);
        }
        if (page == 1) paintRouting (gg, c);
        glow.collecting = was;
        if (panel.readout.isNotEmpty() && Neon::now() - panel.readoutTime < 2.5)
            Neon::label (gg, panel.readout, panelWidth - margin, 132.0f, Neon::Align::right, juce::Colour (0xffa9abb2), 12.0f);
    };
}

void Visualiser::buildControls (Neon::Group& g, float& y)
{
    const float half = (inner - 24.0f) / 2.0f, right = margin + half + 24.0f;
    auto row = [&] (std::initializer_list<int> keys)
    {
        // Faders two to a row
        int i = 0;
        for (int key : keys)
        {
            if (i % 2 == 0) y += 46.0f;
            const bool last = i == (int) keys.size() - 1 && i % 2 == 0;
            fader (g, key, i % 2 == 0 ? margin : right, y, last ? inner : half);
            ++i;
        }
    };

    y -= 14.0f;
    row ({ F::pulse, F::flash, F::stir, F::react, F::speed, F::back });

    // The look and the form: what the mockup showed as picture buttons
    const auto& s = settings;
    static const char* const looks[4][3] = { { "smoke", "oil", "neon" }, { "lamp", "glass", "neon" }, { "grains", "dust", "bulbs" }, { "glow", "lines", "relief" } };
    static const char* const forms[4][4] = { { "blot", "petals", "rays", "tiles" }, { "drift", "lava", "orbit", "mirror" }, { "square", "round", "ripple", nullptr }, { "coral", "cells", "maze", nullptr } };
    static const char* const formTitle[4] = { "form", "move", "plate", "kind" };
    y += 40.0f;
    choice (g, "look", s.surfaceKey(), { looks[s.style][0], looks[s.style][1], looks[s.style][2] }, margin, y);
    y += 30.0f;
    juce::StringArray formWords;
    for (auto* f : forms[s.style]) if (f != nullptr) formWords.add (f);
    choice (g, formTitle[s.style], s.formKey(), formWords, margin, y);

    y += 40.0f;
    auto& tabs = g.words ({ { 0, "shape" }, { 1, "colour" }, { 2, "motion" }, { 3, "trails" } }, margin, y);
    tabs.size = 19.0f; tabs.gap = 16.0f;
    tabs.getChoice = [this] { return tab; };
    tabs.setChoice = [this] (int v) { tab = v; pendingRebuild = true; };
    y += 8.0f;

    if (tab == 0)
    {
        if (s.style == ink)
        {
            y += 30.0f; choice (g, "mirror", C::mirror, { "off", "2-way", "4-way" }, margin, y); y -= 6.0f;
            row ({ F::seg, F::trip, F::depth });
        }
        else if (s.style == liquid) row ({ F::lcount, F::lsize, F::lwob, F::ldrops, F::ldepth });
        else if (s.style == sand)   row ({ F::sgrain, F::scomp, F::ssharp, F::sscatter, F::schange });
        else
        {
            y += 30.0f; choice (g, "mirror", C::bmirror, { "off", "2-way", "4-way" }, margin, y); y -= 6.0f;
            row ({ F::bspeed, F::bcover, F::bseeds, F::bfresh });
        }
    }
    else if (tab == 1)
    {
        y += 30.0f; choice (g, "tones", s.tonesKey(), { "one", "complement", "split", "triad" }, margin, y); y -= 6.0f;
        row ({ s.hueKey(), s.tintKey(), s.psyKey() });
    }
    else if (tab == 2)
    {
        if (s.style == ink)         row ({ F::idle, F::spin, F::swirl, F::scale, F::idetail, F::chaos, F::vort });
        else if (s.style == liquid) row ({ F::lidle, F::lspin, F::lflow, F::lswell });
        else                        row ({ s.idleKey(), s.spinKey() });
    }
    else
    {
        y += 30.0f; choice (g, "trails", s.trailModeKey(), { "smooth", "echo" }, margin, y); y -= 6.0f;
        if (s.style == ink) row ({ F::trails, F::drops, F::fade }); else row ({ s.trailsKey() });
    }
}

// Routing: a patch bay. Pull a cable from a sound to a reaction; slide its bead for strength.
void Visualiser::buildRouting (Neon::Group& g, float& y)
{
    y = 196.0f;
    fader (g, F::gate, margin, y, inner).text = "loudest lows: only hits above the line";

    auto& bay = g.dragArea ({ margin, bayTop - 16.0f, inner, sourceStep * 9.0f + 14.0f }, juce::MouseCursor::PointingHandCursor);
    bay.down = [this] (float x, float py) -> std::optional<Neon::Gesture>
    {
        const juce::Point<float> p (x, py);
        // A bead first, then a sound's jack
        for (int s = 0; s < numSources; ++s)
            for (int d = 0; d < numDests; ++d)
            {
                float& amount = settings.route[(size_t) s][(size_t) d];
                if (amount <= 0.0f) continue;
                const auto line = cable (sourceJack (s), destJack (d));
                if (Neon::pointAt (line, beadAt (amount)).getDistanceFrom (p) > 11.0f) continue;
                Neon::Gesture ge;
                ge.move = [this, s, d, line] (float mx, float my)
                {
                    settings.route[(size_t) s][(size_t) d] = juce::jlimit (0.001f, 1.0f, (Neon::nearest (line, { mx, my }).t - 0.15f) / 0.7f);
                    panel.say (sourceNames[s] + " to " + destNames[d] + "  " + juce::String (juce::roundToInt (settings.route[(size_t) s][(size_t) d] * 100.0f)) + "%");
                    edited = true;
                    changed();
                };
                ge.up = [this, s, d]
                {
                    if (settings.route[(size_t) s][(size_t) d] < 0.03f) { settings.route[(size_t) s][(size_t) d] = 0.0f; panel.say ("unplugged"); changed(); }
                };
                return ge;
            }
        for (int s = 0; s < numSources; ++s)
            if (std::abs (py - sourceJack (s).y) < sourceStep * 0.5f && x < jackSourceX + 16.0f)
            {
                cableFrom = s;
                cableEnd = p;
                Neon::Gesture ge;
                ge.move = [this] (float mx, float my) { cableEnd = { mx, my }; };
                ge.up = [this]
                {
                    for (int d = 0; d < numDests; ++d)
                        if (std::abs (cableEnd.y - destJack (d).y) < destStep * 0.5f && cableEnd.x > jackDestX - 40.0f)
                        {
                            settings.route[(size_t) cableFrom][(size_t) d] = 1.0f;
                            panel.say (sourceNames[cableFrom] + " to " + destNames[d]);
                            edited = true;
                            changed();
                        }
                    cableFrom = -1;
                };
                return ge;
            }
        return {};
    };

    y = bayTop + sourceStep * 9.0f + 40.0f;
    fader (g, F::nudge, margin, y, inner).text = "sync nudge";
    y += 40.0f;
    auto& resetWires = g.words ({ { 0, "reset wiring" } }, margin, y);
    resetWires.size = 16.0f;
    resetWires.getChoice = [] { return 1; };
    resetWires.setChoice = [this] (int) { settings.resetWiring(); edited = true; changed(); panel.say ("wiring back to the default"); };
    y += 6.0f;
}

void Visualiser::paintRouting (juce::Graphics& g, const juce::Colour* c)
{
    // Recent low hits as bars; the ones over the line count as the loudest
    {
        const float x0 = margin, w = inner, top = 150.0f, h = 22.0f, gate = settings.v[F::gate];
        const double now = readout.clock.load();
        const juce::SpinLock::ScopedLockType lock (readout.lock);
        for (int i = 0; i < readout.numKicks; ++i)
        {
            const auto& k = readout.kicks[(size_t) i];
            const float age = (float) (now - k.time);
            if (age > 8.0f) continue;
            const float x = x0 + w * (1.0f - age / 8.0f);
            g.setColour (k.pass ? c[0] : juce::Colour (0xff5a5c63));
            g.fillRect (x - 1.5f, top + h * (1.0f - k.rel), 3.0f, h * k.rel);
        }
        g.setColour (c[0].withAlpha (0.6f));
        g.fillRect (x0, top + h * (1.0f - gate), w, 1.0f);
    }

    const auto grey = juce::Colour (0xff8a8c93);
    for (int s = 0; s < numSources; ++s)
    {
        const auto j = sourceJack (s);
        Neon::label (g, sourceNames[s], j.x - 14.0f, j.y + 4.0f, Neon::Align::right, grey, 13.0f);
        Neon::bulb (g, j.x, j.y, c[0], 0.2f + 0.8f * juce::jmin (1.0f, readout.signal[(size_t) s].load()), 4.6f);
    }
    for (int d = 0; d < numDests; ++d)
    {
        const auto j = destJack (d);
        Neon::label (g, destNames[d], j.x + 14.0f, j.y + 4.0f, Neon::Align::left, grey, 13.0f);
        Neon::bulb (g, j.x, j.y, c[0], 0.2f + 0.8f * juce::jmin (1.0f, readout.reaction[(size_t) d].load()), 4.6f);
    }
    for (int s = 0; s < numSources; ++s)
        for (int d = 0; d < numDests; ++d)
        {
            const float amount = settings.route[(size_t) s][(size_t) d];
            if (amount <= 0.0f) continue;
            const auto line = cable (sourceJack (s), destJack (d));
            Neon::TubeOptions o;
            o.w = 0.55f + 0.35f * amount;
            Neon::tube (g, line, c[0].withMultipliedBrightness (0.55f + 0.45f * amount), o);
            const auto bead = Neon::pointAt (line, beadAt (amount));
            Neon::bulb (g, bead.x, bead.y, c[0], 1.0f, 4.2f);
        }
    if (cableFrom >= 0)
    {
        Neon::TubeOptions o;
        o.w = 0.7f;
        Neon::tube (g, cable (sourceJack (cableFrom), cableEnd), c[0], o);
    }
}

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

    const auto colour = panel.colours (0)[0];
    if (! panelShown)
    {
        Neon::paintLit (g, getWidth(), getHeight(), glowLayer, 1.0f, [&] (juce::Graphics& gg, bool)
        {
            Neon::text (gg, "controls", (float) getWidth() - 24.0f, 36.0f, 16.0f, colour, 0.7f, Neon::Align::right);
        });
        return;
    }

    const auto b = panelBounds();
    g.setColour (juce::Colour (0xd20c0c0e));
    g.fillRoundedRectangle (b, 14.0f * panelScale);
    g.setColour (colour.withAlpha (0.28f));
    g.drawRoundedRectangle (b.reduced (0.5f), 14.0f * panelScale, 1.0f);

    const double t = Neon::now();
    const float dt = (float) juce::jlimit (0.0, 0.1, t - lastPaint);
    lastPaint = t;
    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (b.getSmallestIntegerContainer());
    glowLayer.draw (g, getLocalBounds().toFloat());
    auto& glow = Neon::glow();
    glow.sources.clear();
    glow.collecting = true;
    const auto toView = juce::AffineTransform::scale (panelScale).translated (b.getX(), b.getY());
    glow.base = glow.current = toView;
    glow.alpha = 1.0f;
    {
        juce::Graphics::ScopedSaveState inner2 (g);
        g.addTransform (toView);
        panel.paint (g, t, dt);
    }
    glow.base = glow.current = {};
    glow.collecting = false;
    glowLayer.build (getWidth(), getHeight(), panelScale);
}

//==============================================================================
// Pointer

void Visualiser::mouseDown (const juce::MouseEvent& e)
{
    if (! panelShown)
    {
        if (e.position.x > (float) getWidth() - 110.0f && e.position.y < 50.0f) { panelShown = true; repaint(); return; }
    }
    else if (panelBounds().contains (e.position))
    {
        panel.mouseDown (toPanel (e.position));
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
    else if (panel.isDragging())
        panel.mouseDrag (toPanel (e.position), e.mods.isShiftDown());
}

void Visualiser::mouseUp (const juce::MouseEvent&)
{
    if (draggingPicture) { draggingPicture = false; dragX = -1.0f; dragY = -1.0f; }
    panel.mouseUp();
    if (pendingRebuild) { pendingRebuild = false; panel.mouseExit(); rebuildPanel(); resized(); }
    saveDue = true;
    repaint();
}

void Visualiser::mouseMove (const juce::MouseEvent& e)
{
    if (panelShown && panelBounds().contains (e.position))
        setMouseCursor (panel.mouseMove (toPanel (e.position)));
    else
        setMouseCursor (juce::MouseCursor::NormalCursor);
}

void Visualiser::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (panelShown && panelBounds().contains (e.position)) { panel.doubleClick (toPanel (e.position)); return; }
    panelShown = ! panelShown;
    repaint();
}

void Visualiser::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& d)
{
    if (panelShown && panelBounds().contains (e.position)) panel.wheel (toPanel (e.position), d.deltaY);
}
