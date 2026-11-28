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
}

#if JUCE_MAC
namespace VisNative
{
    void placeBehind (juce::Component& picture, juce::Component& appWindow);   // VisualiserMac.mm
    void hide (juce::Component& picture);
    void letGo (juce::Component& picture);
    void watch (juce::Component& appWindow, std::function<void (int)> changed);
    void unwatch();
}
#endif

Visualiser::Visualiser (AppContext& c, juce::Component& w) : ctx (c), window (w)
{
    setInterceptsMouseClicks (true, false);
    setWantsKeyboardFocus (false);
    renderer = std::make_unique<Renderer>();
    shared = settings;
    seen = settings;
    pic = sharedPic = Picture::builtIn (0);
    picVar = pic->toVar();
    ctx.visParams = [this] { return automatableParams(); };
    ctx.visValue = [this] (const AutoTarget& t, float& out) { return readSetting (settings, t, out); };
}

Visualiser::~Visualiser()
{
    gl.detach();
   #if JUCE_MAC
    VisNative::unwatch();
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
        VisNative::unwatch();
        VisNative::letGo (*picture);
       #endif
        picture.reset();
        resizing = false;
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
    // The panel keeps one picture and one glow per card. The usual allowance (8 million pixels) is less
    // than those come to on a large or Retina window, and a picture pushed out is sent to the card again.
    gl.setImageCacheSize ((size_t) 96 * 1024 * 1024);
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
        VisNative::watch (*top, [this] (int what) { windowChanging (what); });
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

// While the app's window is changing size the picture cannot keep up with it edge for edge, so it
// steps aside: the app turns solid, the picture's window is put away, and both come back at the new size.
void Visualiser::windowChanging (int what)
{
    if (picture == nullptr) return;
    if (what == 0 && ! resizing)
    {
        resizing = true;
        Theme::get().setGlass (0.0f);
       #if JUCE_MAC
        VisNative::hide (*picture);
       #endif
    }
    else if (what == 1 && resizing)
    {
        resizing = false;
        picture->setBounds (window.getScreenBounds());
       #if JUCE_MAC
        if (auto* top = window.getTopLevelComponent()) VisNative::placeBehind (*picture, *top);
       #endif
        if (view == behind) Theme::get().setGlass (0.3f + 0.55f * settings.v[F::back]);
    }
    else if (what == 2 && ! resizing)
        picture->setBounds (window.getScreenBounds());      // a new size in one go (zoom, tiling): follow at once
}

void Visualiser::setView (View v)
{
    if (v == view)
    {
        // Asked for the full view while the picture has the window: the controls come back
        if (v == full && maximised && ! keepMaximised) toggleMaximised();
        return;
    }
    if (! keepMaximised) { maximised = false; cameFrom = full; }
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

// V: the picture alone takes the whole window, from wherever you are; again (or Esc) goes back there
void Visualiser::toggleMaximised()
{
    setMouseCursor (juce::MouseCursor::NormalCursor);
    lastPointerMove = juce::Time::getMillisecondCounter();
    chromeShown = true;
    if (isMaximised())
    {
        const auto back = cameFrom;
        maximised = false;
        cameFrom = full;
        if (back != full) { setView (back); return; }
    }
    else
    {
        cameFrom = view;
        maximised = true;
        if (view != full)
        {
            keepMaximised = true;
            setView (full);
            keepMaximised = false;
            return;
        }
    }
    relayout();
    if (onViewChanged) onViewChanged();
}

void Visualiser::setup (const juce::String& words)
{
    const auto w = juce::StringArray::fromTokens (words, " ,", {});
    static const char* const styles[] = { "ink", "liquid", "sand", "bloom", "kaleido", "lotus", "picture" };
    for (int i = 0; i < numStyles; ++i) if (w.contains (styles[i])) settings.style = i;
    page = w.contains ("routing") ? 1 : 0;
    for (auto& word : w)
        if (word.containsChar ('='))        // look=2, form=1, mirror=4, or any fader by name: marble=0.8
        {
            const auto key = word.upToFirstOccurrenceOf ("=", false, false);
            const auto text = word.fromFirstOccurrenceOf ("=", false, false);
            if (key == "look") settings.w[(size_t) settings.surfaceKey()] = text.getIntValue();
            else if (key == "form") settings.w[(size_t) settings.formKey()] = text.getIntValue();
            else if (key == "mirror") settings.w[(size_t) settings.mirrorKey()] = juce::jlimit (0, 4, text.getIntValue());
            else if (key == "sample") setPicture (Picture::builtIn (text.getIntValue()), false);       // one of the built-in pictures
            else if (key == "file") { const int style = settings.style; loadPicture (juce::File (text)); settings.style = style; }
            else
            {
                auto all = settings.toVar();
                if (all["faders"].hasProperty (key)) { all["faders"].getDynamicObject()->setProperty (key, text.getFloatValue()); settings.fromVar (all); }
            }
        }
    if (w.contains ("scenes")) { scenes[0] = settings; scenes[2] = settings; scene = 2; }
    setView (w.contains ("behind") ? behind : full);
    if (w.contains ("max") && ! isMaximised()) toggleMaximised();
    relayout();
}

void Visualiser::setBackdrop (float amount)
{
    settings.v[F::back] = juce::jlimit (0.0f, 1.0f, amount);
    edited = true;
    if (view == behind && ! resizing) Theme::get().setGlass (0.3f + 0.55f * settings.v[F::back]);
    changed();
}

void Visualiser::setFeed (int slot)
{
    ctx.engine.visFeed = slot;
}

void Visualiser::projectReplaced()
{
    for (auto& s : scenes) s.reset();
    scene = -1;
    storing = false;
    if (ctx.project.visualiser.isObject())
    {
        settings.fromVar (ctx.project.visualiser);
        if (auto* list = ctx.project.visualiser["scenes"].getArray())
            for (int i = 0; i < list->size() && i < (int) scenes.size(); ++i)
                if (list->getReference (i).isObject())
                {
                    Settings look;
                    look.fromVar (list->getReference (i));
                    scenes[(size_t) i] = look;
                }
    }
    ctx.engine.visFeed = -1;
    seen = settings;
    for (int k = 0; k < 3; ++k) ctx.engine.visListener.setBand (k, settings.bandHz[(size_t) k][0], settings.bandHz[(size_t) k][1]);
    // The song's picture, or the first built-in one
    auto kept = Picture::fromVar (ctx.project.visualiser["picture"]);
    if (kept != nullptr) setPicture (kept, false);
    else if (pic == nullptr || pic->sample != 0) setPicture (Picture::builtIn (0), false);
    markAll();
    const juce::SpinLock::ScopedLockType lock (sharedLock);
    shared = settings;
}

//==============================================================================
// The picture the Picture mode works on

void Visualiser::setPicture (std::shared_ptr<const Picture> p, bool byHand)
{
    if (p == nullptr) return;
    pic = std::move (p);
    picVar = pic->toVar();
    {
        const juce::SpinLock::ScopedLockType lock (sharedLock);
        sharedPic = pic;
    }
    if (byHand) { edited = true; saveDue = true; }
    markAll();
    repaint();
}

void Visualiser::loadPicture (const juce::File& file)
{
    auto read = Picture::from (juce::ImageFileFormat::loadFrom (file));
    if (read == nullptr)
    {
        last = "that picture could not be read (it takes PNG, JPEG and GIF)";
        relayout();
        return;
    }
    setPicture (read);
    settings.style = pictureStyle;
    last = "picture: " + file.getFileName();
    edited = true;
    relayout();
}

void Visualiser::choosePicture()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose a picture", juce::File::getSpecialLocation (juce::File::userPicturesDirectory), "*.png;*.jpg;*.jpeg;*.gif");
    juce::Component::SafePointer<Visualiser> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe] (const juce::FileChooser& fc)
    {
        if (safe != nullptr && fc.getResult().existsAsFile()) safe->loadPicture (fc.getResult());
    });
}

bool Visualiser::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files) if (Picture::isPictureFile (f)) return true;
    return false;
}

void Visualiser::filesDropped (const juce::StringArray& files, int, int)
{
    for (auto& f : files)
        if (Picture::isPictureFile (f)) { loadPicture (juce::File (f)); return; }
}

void Visualiser::changed()
{
    if (probing) return;
    reportTouches();
    for (int k = 0; k < 3; ++k) ctx.engine.visListener.setBand (k, settings.bandHz[(size_t) k][0], settings.bandHz[(size_t) k][1]);
    screenBox = layout().screen;
    {
        const juce::SpinLock::ScopedLockType lock (sharedLock);
        shared = settings;
        // Behind the app the picture fills the window; in the full view it has its screen among the controls
        sharedBehind = view == behind;
        sharedArea = sharedBehind ? window.getLocalBounds() : screenBox.toNearestInt() + getPosition();
        sharedOwnWindow = picture != nullptr;
    }
    saveDue = true;
    // A control being dragged only changes its own card; anything else may change them all
    if (activeDrag != nullptr && activeCard >= 0 && activeCard < cChrome && ! wholePanel)
    {
        pieces[(size_t) activeCard].dirty = true;
        repaint (pieces[(size_t) activeCard].area);
    }
    else
    {
        markAll();
        repaint();
    }
}

void Visualiser::tick()
{
    if (seenGeneration != ctx.generation)
    {
        const bool first = seenGeneration < 0;
        seenGeneration = ctx.generation;
        if (! first || ctx.project.visualiser.isObject()) projectReplaced();
    }
    applyAutomation();
    if (view == off) return;
    if (picture != nullptr && ! resizing && picture->getBounds() != window.getScreenBounds())
        picture->setBounds (window.getScreenBounds());     // the picture's window follows the app's
    // The song remembers the look: written back a moment after the last change, not on every drag
    if (saveDue && activeDrag == nullptr)
    {
        saveDue = false;
        auto now = settings.toVar();
        bool anyScene = false;
        for (auto& s : scenes) anyScene = anyScene || s.has_value();
        if (anyScene) now.getDynamicObject()->setProperty ("scenes", scenesVar());
        if (pic != nullptr && pic->sample != 0) now.getDynamicObject()->setProperty ("picture", picVar);     // the first built-in picture needs no keeping
        if (juce::JSON::toString (now, true) != juce::JSON::toString (ctx.project.visualiser, true))
        {
            const bool had = ctx.project.visualiser.isObject();
            ctx.project.visualiser = now;
            if (had || edited) ctx.touched();
        }
    }
    if (view != full) return;
    if ((++frameCount & 1) == 0)
        for (auto& p : pieces)
            if (p.shown)
                for (auto& part : p.live) repaint (part.box);        // the lamps and meters, 30 times a second, and no more than they cover

    // With the picture filling the window, the "show controls" word and the pointer step aside when the pointer rests
    const bool want = ! maximised || juce::Time::getMillisecondCounter() - lastPointerMove < 2500;
    if (want != chromeShown)
    {
        chromeShown = want;
        repaint (pieces[cChrome].area);
        if (maximised && ! want) setMouseCursor (juce::MouseCursor::NoCursor);
    }
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
    std::shared_ptr<const Picture> image;
    juce::Rectangle<int> area;
    bool ownWindow = false, isBehind = false;
    {
        const juce::SpinLock::ScopedLockType lock (sharedLock);
        s = shared;
        image = sharedPic;
        area = sharedArea;
        ownWindow = sharedOwnWindow;
        isBehind = sharedBehind;
    }

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
            const int panels = statPanels.exchange (0);
            std::fprintf (stderr, "visualiser: %s, %.1f frames a second, longest gap %.0f ms, picture takes %.2f ms a frame, %d x %d, panel redrawn %d times at %.2f ms each (slowest %.1f)\n",
                          isBehind ? (ownWindow ? "behind (own window)" : "behind (one window)") : "full window",
                          statFrames * 1000.0 / (frameStart - statSince), statWorst, statWork / statFrames,
                          juce::roundToInt ((float) area.getWidth() * (float) gl.getRenderingScale()), juce::roundToInt ((float) area.getHeight() * (float) gl.getRenderingScale()),
                          panels, statPanel.exchange (0.0) / juce::jmax (1, panels), statPanelWorst.exchange (0.0));
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
    renderer->frame (s, ctx.engine.visListener, latency.load(), out, { dx, dx < 0.0f ? -1.0f : 1.0f - dy }, readout, image);
    glBindFramebuffer (GL_FRAMEBUFFER, (GLuint) framebuffer);
    glViewport (viewport[0], viewport[1], viewport[2], viewport[3]);
    if (stats) { glFlush(); statWork += juce::Time::getMillisecondCounterHiRes() - frameStart; }
}

//==============================================================================
// The controls: the Monitor layout settled in the mockup. The picture has a screen of its own at the
// top left; every section is on show at once as a card, three beside the screen and a row beneath it.

namespace
{
    // Each card's own height when it is 392 wide, and the widest its contents are allowed to spread
    //                              head    pics    scenes  shape   colour  motion  trails  special bands   gates   route
    constexpr float cardHeight[] = { 356.0f, 250.0f, 128.0f, 306.0f, 306.0f, 306.0f, 306.0f, 306.0f, 452.0f, 452.0f, 452.0f };
    constexpr float cardSpread[] = { 470.0f, 560.0f, 560.0f, 560.0f, 560.0f, 560.0f, 560.0f, 560.0f, 560.0f, 560.0f, 840.0f };
    constexpr float designWidth = 1700.0f, designHeight = 894.0f;       // smaller windows shrink the whole panel to fit

    // Kaleidoscope, lotus and picture have more shape and colour controls: those cards are wider, and trails gives up the room
    //                                  shape  colour motion trails special
    constexpr float wideCards[3][5] = { { 1.35f, 1.45f, 0.9f, 0.42f, 0.88f },
                                        { 1.08f, 1.45f, 1.0f, 0.42f, 1.05f },
                                        { 1.05f, 1.4f,  1.1f, 0.42f, 1.03f } };

    float cardScale (int card, juce::Rectangle<float> box, float drawnFor) { return juce::jmin (box.getWidth() / drawnFor, box.getHeight() / cardHeight[card]); }

    // Where a frequency sits along the listening bands' scale, and back
    float hzToT (float hz) { return std::log (hz / Settings::lowestHz) / std::log (Settings::highestHz / Settings::lowestHz); }
    float tToHz (float t)  { return Settings::lowestHz * std::pow (Settings::highestHz / Settings::lowestHz, juce::jlimit (0.0f, 1.0f, t)); }
    juce::String hzText (float hz)
    {
        if (hz < 1000.0f) return juce::String (juce::roundToInt (hz)) + " Hz";
        const int tenths = juce::roundToInt (hz / 100.0f);
        return (tenths % 10 == 0 ? juce::String (tenths / 10) : juce::String ((float) tenths / 10.0f, 1)) + " kHz";
    }
}

Visualiser::Layout Visualiser::layout() const
{
    Layout l;
    l.designWidth.fill (panelWidth);
    const float w = (float) getWidth(), h = (float) getHeight();
    const float size = 19.0f;
    if (maximised || w < 80.0f || h < 80.0f)
    {
        l.screen = getLocalBounds().toFloat();
        const float tw = Neon::textWidth ("show controls", size);
        l.box[cChrome] = { w - 26.0f - tw, 10.0f, tw + 12.0f, 34.0f };
        l.shown[cChrome] = true;
        return l;
    }
    l.Z = juce::jmin (1.0f, w / designWidth, h / designHeight);
    const float W = w / l.Z, H = h / l.Z, g = 12.0f, X0 = 12.0f, Y0 = 12.0f, AW = W - 24.0f, AH = H - 24.0f;
    auto put = [&l] (int n, float x, float y, float cw, float ch) { l.box[(size_t) n] = juce::Rectangle<float> (x, y, cw, ch) * l.Z; l.shown[(size_t) n] = true; };

    // The row of sections is only as tall as its cards need, so the screen gets the rest
    const bool routing = page == 1;
    const float rowHeight = routing ? AH * 0.45f : juce::jmin (AH * 0.44f, (AW - 4.0f * g) / 5.0f / panelWidth * 306.0f + 6.0f);
    const float sh = std::round (AH - g - rowHeight), rw = juce::jlimit (300.0f, 480.0f, AW * 0.25f);
    l.screen = (juce::Rectangle<float> (X0, Y0, AW - rw - g, sh) * l.Z).toNearestInt().toFloat();

    const float total = cardHeight[cHead] + cardHeight[cPics] + cardHeight[cScenes], room = sh - 2.0f * g;
    float y = Y0;
    for (int n : { cHead, cPics, cScenes })
    {
        const float ch = room * cardHeight[n] / total;
        put (n, X0 + AW - rw, y, rw, ch);
        y += ch + g;
    }
    const float ry = Y0 + sh + g;
    if (routing)
    {
        const float w1 = std::round (AW * 0.23f), w2 = std::round (AW * 0.25f);
        put (cBands, X0, ry, w1, rowHeight);
        put (cGates, X0 + w1 + g, ry, w2, rowHeight);
        put (cRoute, X0 + w1 + w2 + 2.0f * g, ry, AW - w1 - w2 - 2.0f * g, rowHeight);
    }
    else if (settings.style >= kaleido)
    {
        const auto& weight = wideCards[settings.style - kaleido];
        const float unit = (AW - 4.0f * g) / 5.0f;
        float x = X0;
        for (int i = 0; i < 5; ++i)
        {
            put (cShape + i, x, ry, unit * weight[i], rowHeight);
            l.designWidth[(size_t) (cShape + i)] = panelWidth * weight[i];
            x += unit * weight[i] + g;
        }
    }
    else
    {
        const float cw = (AW - 4.0f * g) / 5.0f;
        for (int i = 0; i < 5; ++i) put (cShape + i, X0 + (float) i * (cw + g), ry, cw, rowHeight);
    }
    const float fs = 15.0f * l.Z, tw = Neon::textWidth ("fill window", fs);
    l.box[cChrome] = { l.screen.getRight() - 20.0f * l.Z - tw, l.screen.getBottom() - 34.0f * l.Z, tw + 12.0f * l.Z, 28.0f * l.Z };
    l.shown[cChrome] = true;
    return l;
}

// Everything the panel is made of. One of these lives for the drawing of one card.
struct Visualiser::Draw
{
    Visualiser& v;
    juce::Graphics& g;
    Piece& piece;
    int card;
    int ci = 0;
    juce::AffineTransform place {};
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
    // Hits and moving parts are kept in the component's own coordinates; the glow in the card's picture's
    void setPlace (const juce::AffineTransform& t)
    {
        place = t;
        Neon::glow().base = Neon::glow().current = t.translated ((float) -piece.area.getX(), (float) -piece.area.getY());
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
        h.card = card;
        piece.hits.push_back (std::move (h));
    }
    // A control of the panel itself (a page, a scene): not something a lane can drive
    void ui (juce::Rectangle<float> box, std::function<void()> click)
    {
        hit (box, {}, std::move (click));
        piece.hits.back().setting = false;
    }
    void block (juce::Rectangle<float> box) { hit (box, {}); }          // takes the pointer and does nothing
    // A part that moves with the music: `box` is all it ever covers, so only that is drawn again
    void dyn (juce::Rectangle<float> box, std::function<void (juce::Graphics&)> f)
    {
        piece.live.push_back ({ place, box.transformedBy (place).getSmallestIntegerContainer().expanded (2), std::move (f) });
    }

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
    // A card's title: quiet letter-spaced serif capitals, with an optional note beside it
    void heading (const juce::String& t, float x, float y, const juce::String& note = {})
    {
        juce::GlyphArrangement ga;
        ga.addLineOfText (Theme::get().heading (12.5f), t.toUpperCase(), x, y);
        for (int i = 0; i < ga.getNumGlyphs(); ++i) ga.getGlyph (i).moveBy (3.2f * (float) i, 0.0f);
        g.setColour (juce::Colour (0xffa3a5ac));
        ga.draw (g);
        if (note.isNotEmpty()) small (note, ga.getBoundingBox (0, -1, true).getRight() + 14.0f, y - 1.0f, dim);
    }
    void glassBox (juce::Rectangle<float> r, float radius, juce::Colour fill)
    {
        g.setColour (fill);
        g.fillRoundedRectangle (r, radius);
        g.setColour (juce::Colour (0xff3a3c42));
        g.drawRoundedRectangle (r, radius, 1.0f);
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
    float words (const juce::StringArray& list, float x, float y, float size, juce::Colour c, int selected, float gap, std::function<void (int)> pick, bool setting = true)
    {
        float cx = x;
        for (int i = 0; i < list.size(); ++i)
        {
            const float w = word (list[i], cx, y, size, c, i == selected);
            hit ({ cx - 4.0f, y - size, w + 8.0f, size * 1.3f }, {}, [pick, i] { pick (i); });
            piece.hits.back().setting = setting;
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
        dyn ({ x + 8.0f, y - 4.0f, 8.0f, h + 8.0f }, [=] (juce::Graphics& gg) { const float lv = juce::jlimit (0.0f, 1.0f, signal()); Neon::bulb (gg, x + 12.0f, y + h - h * val * lv, c, 0.3f + lv * 0.7f, 3.0f); });
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
        const float inner = R - 9.0f;
        dyn ({ cx - inner, cy - inner, inner * 2.0f, inner * 2.0f }, [=] (juce::Graphics& gg)
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
    void iconRow (int row, float x, float y, float bw, juce::Colour c)
    {
        static const char* const names[numStyles][2][4] = { { { "smoke", "oil", "neon", nullptr }, { "blot", "petals", "rays", "tiles" } },
                                                            { { "lamp", "glass", "neon", nullptr }, { "drift", "lava", "orbit", "mirror" } },
                                                            { { "grains", "dust", "bulbs", nullptr }, { "square", "round", "ripple", nullptr } },
                                                            { { "glow", "lines", "relief", nullptr }, { "coral", "cells", "maze", nullptr } },
                                                            { { "glass", "jewels", "neon", nullptr }, { "wheel", "tiles", "facets", nullptr } },
                                                            { { "soft", "glass", "neon", nullptr }, { "petals", "scales", "spikes", nullptr } },
                                                            { { "smoke", "oil", "neon", nullptr }, { "pours", "beats", "parts", "melts" } } };
        const int style = S().style, key = row == 0 ? S().surfaceKey() : S().formKey();
        for (int i = 0; i < 4 && names[style][row][i] != nullptr; ++i)
        {
            if (Theme::get().isRainbow()) c = Theme::get().pal (i + (row == 1 ? 3 : 0));
            const bool on = S().w[(size_t) key] == i;
            const float bx = x + (float) i * bw, sc = 1.15f, fw = bw - 8.0f, fh = 74.0f, fy = y - 24.0f;
            if (on) tube (Neon::roundRect (bx, fy, fw, fh, 10.0f), c, 0.7f);
            else    glassBox ({ bx, fy, fw, fh }, 10.0f, juce::Colour (0x730a0a0c));
            const float cx = bx + fw * 0.5f, cy = y;
            for (const auto& line : VisIcons::icons[style][row][i])
            {
                Line p;
                for (auto pt : line) p.push_back ({ cx + pt.x * sc, cy + pt.y * sc });
                tube (p, c, 0.85f, ! on);
            }
            word (names[style][row][i], cx, cy + 40.0f, 16.0f, c, on, Neon::Align::centre);
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
    void mirrorRow (float x, float y) { wrow ("mirror", S().mirrorKey(), { "off", "2-way", "4-way", "6-way", "8-way" }, x, y, nc()); }
    // The same, set a little tighter, for the wider cards of kaleidoscope, lotus and picture
    void row (const juce::String& title, int key, const juce::StringArray& options, float x, float y)
    {
        const auto c = nc();
        const float kw = word (title, x, y, 15.0f, c, false);
        words (options, x + kw + 12.0f, y, 17.0f, c, S().w[(size_t) key], 11.0f,
               [this_ = &v, key] (int i) { this_->settings.w[(size_t) key] = i; this_->edited = true; this_->changed(); });
    }
    void cap (const juce::String& t, float x, float y) { small (t, x, y, dim); }

    //==========================================================================
    // The one-of-a-kind controls of each mode

    // Breathe: a ring that swells and shrinks; drag up for more
    void breatheRing (int key, float cx, float cy, float R, juce::Colour c)
    {
        const float val = S().v[(size_t) key], rest = R * (0.28f + 0.5f * val);
        tube (Neon::circle (cx, cy, R), c, 1.0f, true);
        tube (Neon::circle (cx, cy, rest), c, 0.8f, val < 0.02f);
        word (name (key), cx, cy + R + 20.0f, 14.0f, c, true, Neon::Align::centre);
        valTxt (key, cx, cy + R + 34.0f, Neon::Align::centre);
        hit ({ cx - R - 12.0f, cy - R - 12.0f, R * 2.0f + 24.0f, R * 2.0f + 50.0f }, [set = setter (key), cy, R] (float, float py) { set ((cy + R - py) / (2.0f * R)); }, {}, resetOf (key));
        if (val >= 0.02f)
            dyn ({ cx - R, cy - R, R * 2.0f, R * 2.0f }, [=, r = &v.readout] (juce::Graphics& gg)
            {
                const float rr = rest * (1.0f + 0.24f * val * std::sin (r->flow.load() * 0.9f));
                gg.setColour (c.withAlpha (0.5f));
                gg.drawEllipse (cx - rr, cy - rr, rr * 2.0f, rr * 2.0f, 1.2f);
            });
    }

    // Heat: a thermometer; the bubbles beside it rise faster as it warms
    void thermometer (int key, float x, float y, float h, juce::Colour c)
    {
        const float val = S().v[(size_t) key];
        tube ({ { x, y + h }, { x, y } }, c, 1.0f, true);
        if (val > 0.01f) tube ({ { x, y + h }, { x, y + h - h * val } }, c);
        Neon::bulb (g, x, y + h + 9.0f, c, 0.35f + val * 0.65f, 9.0f);
        Neon::bulb (g, x, y + h - h * val, c, 1.0f, 5.0f);
        g.setColour (juce::Colour (0xff4a4d55));
        for (int i = 1; i < 5; ++i) g.drawLine (x + 9.0f, y + h * (float) i / 5.0f, x + 15.0f, y + h * (float) i / 5.0f, 1.0f);
        word (name (key), x, y + h + 40.0f, 14.0f, c, true, Neon::Align::centre);
        valTxt (key, x, y + h + 54.0f, Neon::Align::centre);
        hit ({ x - 24.0f, y - 10.0f, 52.0f, h + 70.0f }, [set = setter (key), y, h] (float, float py) { set ((y + h - py) / h); }, {}, resetOf (key));
        if (val > 0.01f)
            dyn ({ x - 15.0f, y - 4.0f, 10.0f, h + 8.0f }, [=, r = &v.readout] (juce::Graphics& gg)
            {
                const double T = r->clock.load();
                for (int i = 0; i < 3; ++i)
                {
                    const float t = (float) std::fmod (T * (0.15 + val * 0.9) + (double) i / 3.0, 1.0);
                    Neon::bulb (gg, x - 11.0f + (float) (i % 2) * 3.0f, y + h - h * t, c, val * (1.0f - t) * 0.9f, 2.2f);
                }
            });
    }

    // Merge: two blobs that pull together as you drag right
    void mergeBlobs (int key, float x, float y, float w, juce::Colour c)
    {
        const float val = S().v[(size_t) key], cx = x + w * 0.5f, d = 6.0f + 30.0f * (1.0f - val), R = 15.0f;
        word (name (key), x, y - 28.0f, 16.0f, c, true);
        valTxt (key, x + w, y - 29.0f);
        tube ({ { x, y + 30.0f }, { x + w, y + 30.0f } }, c, 0.6f, true);
        if (val > 0.01f) tube ({ { x, y + 30.0f }, { x + w * val, y + 30.0f } }, c, 0.6f);
        Neon::bulb (g, x + w * val, y + 30.0f, c, 1.0f, 5.0f);
        if (d > R * 0.95f)
        {
            tube (Neon::circle (cx - d, y, R), c, 0.8f);
            tube (Neon::circle (cx + d, y, R), c, 0.8f);
        }
        else
        {
            Line p;
            for (int i = 0; i <= 48; ++i)
            {
                const float a = (float) i / 48.0f * juce::MathConstants<float>::twoPi, ca = std::cos (a), sa = std::sin (a);
                const float rad = d * std::abs (ca) + std::sqrt (juce::jmax (0.0f, R * R - d * d * sa * sa)) * (0.86f + 0.14f * std::abs (ca));
                p.push_back ({ cx + ca * rad, y + sa * rad });
            }
            tube (p, c, 0.8f);
        }
        hit ({ x - 10.0f, y - 24.0f, w + 20.0f, 66.0f }, [set = setter (key), x, w] (float px, float) { set ((px - x) / w); }, {}, resetOf (key));
    }

    // Density: a tray of grains; drag across to pour more in
    void grainTray (int key, float x, float y, float w, float h, juce::Colour c)
    {
        const float val = S().v[(size_t) key];
        const int n = 44;
        word (name (key), x, y - 10.0f, 16.0f, c, true);
        valTxt (key, x + w, y - 11.0f);
        glassBox ({ x, y, w, h }, 10.0f, juce::Colour (0x8008080a));
        auto rnd = [] (int i) { const double q = std::sin ((double) i * 127.1 + 311.7) * 43758.5453; return (float) (q - std::floor (q)); };
        for (int i = 0; i < n; ++i)
            Neon::bulb (g, x + 10.0f + rnd (i) * (w - 20.0f), y + 9.0f + rnd (i + 70) * (h - 18.0f), c, (float) i < val * (float) n ? 0.95f : 0.0f, 2.3f);
        hit ({ x, y - 4.0f, w, h + 8.0f }, [set = setter (key), x, w] (float px, float) { set ((px - x - 8.0f) / (w - 16.0f)); }, {}, resetOf (key));
    }

    // Branching: a stem that sprouts more side shoots
    void branchStem (int key, float x, float y, float w, juce::Colour c)
    {
        const float val = S().v[(size_t) key];
        const int n = 9;
        word (name (key), x, y - 26.0f, 16.0f, c, true);
        valTxt (key, x + w, y - 27.0f);
        tube ({ { x, y }, { x + w, y } }, c, 1.0f, true);
        if (val > 0.02f) tube ({ { x, y }, { x + w * val, y } }, c);
        for (int i = 0; i < n; ++i)
        {
            const float t = ((float) i + 0.6f) / (float) n, px = x + w * t, up = i % 2 ? 1.0f : -1.0f, len = 7.0f + 10.0f * (float) ((i * 7) % 4) / 3.0f;
            const bool on = t < val;
            tube ({ { px, y }, { px + len * 0.7f, y + up * len }, { px + len * 1.25f, y + up * len * (on ? 1.5f : 1.0f) } }, c, 0.6f, ! on);
        }
        Neon::bulb (g, x + w * val, y, c, 1.0f, 5.5f);
        hit ({ x - 10.0f, y - 24.0f, w + 20.0f, 50.0f }, [set = setter (key), x, w] (float px, float) { set ((px - x) / w); }, {}, resetOf (key));
    }

    //==========================================================================
    // The sections

    void colourSection (float x, float y, float w)
    {
        wrow ("tones", S().tonesKey(), { "one", "complement", "split", "triad" }, x, y, nc(), true);
        hueWheel (x + 74.0f, y + 98.0f, 62.0f);
        const float rx = x + 172.0f, rw = w - 172.0f;
        sfader (S().psyKey(), rx, y + 70.0f, rw, {}, rainbow);
        const char* const help[] = { "Drag the lamp in the ring:", "round for hue,", "in and out for tint." };
        for (int i = 0; i < 3; ++i) small (help[i], rx, y + 108.0f + (float) i * 15.0f, dim);
    }
    // Kaleidoscope, lotus and picture: the ring, then psychedelic with its slow LFO, then what sways it.
    // Returns where the last column starts; each mode puts its own colour controls under it (218 wide).
    float wideColour (float x, float y, float w, int lfoKey, int rateKey, int swayKey, int swayWithKey)
    {
        wrow ("tones", S().tonesKey(), { "one", "complement", "split", "triad" }, x, y, nc(), true);
        hueWheel (x + 56.0f, y + 98.0f, 48.0f);
        cap ("round for hue,", x + 12.0f, y + 172.0f);
        cap ("in and out for tint", x + 12.0f, y + 186.0f);
        const float bx = x + 140.0f, cw = 218.0f, cx = x + w - cw, bw = cx - 26.0f - bx;
        const int psyKey = S().psyKey();
        sfader (psyKey, bx, y + 56.0f, bw, {}, rainbow);
        sfader (lfoKey, bx, y + 110.0f, bw, nc(), wave);
        sfader (rateKey, bx, y + 164.0f, bw, nc(), plain);
        cap ("a triangle, round the fader", bx, y + 186.0f);
        // A small ring rides the psychedelic fader to show where the LFO and sway have it right now
        dyn ({ bx - 8.0f, y + 48.0f, bw + 16.0f, 16.0f }, [r = &v.readout, set = S().v[(size_t) psyKey], bx, bw, y] (juce::Graphics& gg)
        {
            const float now = r->psyNow.load();
            if (std::abs (now - set) < 0.004f) return;
            gg.setColour (juce::Colours::white.withAlpha (0.8f));
            gg.drawEllipse (bx + juce::jmin (1.0f, std::abs (now)) * bw - 6.0f, y + 50.0f, 12.0f, 12.0f, 1.3f);
        });
        sfader (swayKey, cx, y + 56.0f, cw, nc(), wave);
        row ("with", swayWithKey, { "lows", "mids", "highs", "hits" }, cx, y + 84.0f);
        return cx;
    }
    void trailWords (float x, float y)
    {
        const int key = S().trailModeKey();
        words ({ "smooth", "echo" }, x, y, 17.0f, nc(), S().w[(size_t) key], 12.0f,
               [this_ = &v, key] (int i) { this_->settings.w[(size_t) key] = i; this_->edited = true; this_->changed(); });
    }

    void kaleidoSection (int which, float x, float y, float w)
    {
        const float hw = (w - 28.0f) / 2.0f, rx = x + 172.0f, rw = w - 172.0f;
        if (which == 0)
        {
            const float tw = (w - 48.0f) / 3.0f, x2 = x + tw + 24.0f, x3 = x + 2.0f * (tw + 24.0f);
            bulbCount (F::kseg, 10, x, y + 22.0f, w, nc(), false);
            row ("cut", C::kcut, { "shards", "diamonds", "scales", "rose" }, x, y + 84.0f);
            sfader (F::ksize, x, y + 128.0f, tw, nc(), plain);
            sfader (F::kjag, x2, y + 128.0f, tw, nc(), jag);
            sfader (F::klead, x3, y + 128.0f, tw, nc(), plain);
            sfader (F::ktwist, x, y + 182.0f, tw, nc(), wave);    cap ("the mirrors wind into a spiral", x, y + 204.0f);
            sfader (F::kbulge, x2, y + 182.0f, tw, nc(), rings);  cap ("a lens over the middle", x2, y + 204.0f);
            sfader (F::klayer, x3, y + 182.0f, tw, nc(), plain);  cap ("a second sheet of glass", x3, y + 204.0f);
        }
        else if (which == 1)
        {
            static const char* const says[4][2] = { { "each hit sends a ring of new", "colour out through the glass" }, { "each hit, pieces trade", "to their next colour" },
                                                    { "colours drift round, faster", "when the music is louder" }, { "lows, mids and highs each", "shift their own pieces" } };
            const float cx = wideColour (x, y, w, F::kplfo, F::kprate, F::kpsway, C::kpsyon);
            row ("colours", C::kcolour, { "ring", "swap", "flow", "bands" }, cx, y + 122.0f);
            sfader (F::kwash, cx, y + 164.0f, 218.0f, nc(), rings);
            for (int i = 0; i < 2; ++i) cap (says[juce::jlimit (0, 3, S().w[C::kcolour])][i], cx, y + 186.0f + (float) i * 14.0f);
        }
        else if (which == 2)
        {
            idleWheel (x, y);
            sfader (F::kspin, rx, y + 24.0f, rw, nc(), plain);
            sfader (F::ktumble, rx, y + 80.0f, rw, nc(), wave);
            sfader (F::kswell, x, y + 164.0f, hw, nc(), plain);
            sfader (F::kripple, x + hw + 28.0f, y + 164.0f, hw, nc(), rings);
            cap ("lows swell the shards", x, y + 186.0f);
            cap ("each hit bends the glass", x + hw + 28.0f, y + 186.0f);
        }
        else { trailWords (x, y); sfader (F::ktrails, x, y + 52.0f, w, nc(), comet); }
    }

    void lotusSection (int which, float x, float y, float w)
    {
        const float hw = (w - 28.0f) / 2.0f, x2 = x + hw + 28.0f, rx = x + 172.0f, rw = w - 172.0f;
        if (which == 0)
        {
            bulbCount (F::zpetals, 9, x, y + 22.0f, w, nc(), false);
            sfader (F::ztwist, x, y + 84.0f, hw, nc(), wave);   cap ("winds the rings into a spiral", x, y + 106.0f);
            sfader (F::zpoint, x2, y + 84.0f, hw, nc(), jag);   cap ("from round to pointed", x2, y + 106.0f);
            sfader (F::zopen, x, y + 158.0f, w, nc(), rings);   cap ("how far each ring of petals opens; lows open them wider", x, y + 180.0f);
        }
        else if (which == 1)
        {
            static const char* const says[4][2] = { { "each hit sends a ring of new", "colour out through the petals" }, { "each hit, petals trade", "to their next colour" },
                                                    { "colours drift round, faster", "when the music is louder" }, { "lows, mids and highs each", "shift their own petals" } };
            const float cx = wideColour (x, y, w, F::zplfo, F::zprate, F::zpsway, C::zpsyon);
            row ("colours", C::zcolour, { "ring", "swap", "flow", "bands" }, cx, y + 122.0f);
            sfader (F::zwash, cx, y + 164.0f, 218.0f, nc(), rings);
            for (int i = 0; i < 2; ++i) cap (says[juce::jlimit (0, 3, S().w[C::zcolour])][i], cx, y + 186.0f + (float) i * 14.0f);
        }
        else if (which == 2)
        {
            idleWheel (x, y);
            sfader (F::zspin, rx, y + 24.0f, rw, nc(), plain);
            sfader (F::zspeed, rx, y + 80.0f, rw, nc(), plain);   cap ("how fast you fall inward", rx, y + 102.0f);
            sfader (F::zlunge, x, y + 164.0f, w, nc(), rings);    cap ("each hit pushes you deeper for a moment", x, y + 186.0f);
        }
        else { trailWords (x, y); sfader (F::ztrails, x, y + 52.0f, w, nc(), comet); }
    }

    void pictureSection (int which, float x, float y, float w)
    {
        const float hw = (w - 28.0f) / 2.0f, x2 = x + hw + 28.0f;
        if (which == 0)
        {
            mirrorRow (x, y);
            row ("smoke from", C::psrc, { "shape", "edges", "lights" }, x, y + 34.0f);
            sfader (F::psize, x, y + 84.0f, hw, nc(), plain);     cap ("how big the picture sits", x, y + 106.0f);
            sfader (F::poff, x2, y + 84.0f, hw, nc(), plain);     cap ("slides it off the mirror fold", x2, y + 106.0f);
            sfader (F::pvis, x, y + 146.0f, hw, nc(), plain);     cap ("how clearly the picture shows", x, y + 168.0f);
            sfader (F::phaze, x2, y + 146.0f, hw, nc(), wave);    cap ("smoke bends it like heat", x2, y + 168.0f);
            sfader (F::pamt, x, y + 208.0f, hw, nc(), rings);     cap ("how much of it smokes", x, y + 230.0f);
            sfader (F::pdent, x2, y + 208.0f, hw, nc(), rings);   cap ("how far the shape stands out", x2, y + 230.0f);
        }
        else if (which == 1)
        {
            const float cx = wideColour (x, y, w, F::pplfo, F::pprate, F::ppsway, C::ppsyon);
            row ("smoke", C::pcolour, { "picture", "ring" }, cx, y + 122.0f);
            sfader (F::pash, cx, y + 164.0f, 218.0f, nc(), plain);
            const bool ring = S().w[C::pcolour] != 0;
            cap (ring ? "smoke takes its colours from" : "smoke takes the colours of the", cx, y + 186.0f);
            cap (ring ? "the hue ring and tones here" : "picture, wherever it leaves it", cx, y + 200.0f);
            cap ("Ash: thin smoke fades to grey.", cx, y + 214.0f);
        }
        else if (which == 2)
        {
            const float tw = (w - 40.0f) / 3.0f, xa = x + tw + 20.0f, xb = x + 2.0f * (tw + 20.0f);
            row ("smoke will", C::pdir, { "rise", "spread", "fall" }, x, y);
            sfader (F::prise, x, y + 52.0f, tw, nc(), plain);     cap ("how fast it travels", x, y + 74.0f);
            sfader (F::pswirl, xa, y + 52.0f, tw, nc(), wave);    cap ("mids curl it more", xa, y + 74.0f);
            sfader (F::pwisp, xb, y + 52.0f, tw, nc(), jag);      cap ("highs add threads", xb, y + 74.0f);
            sfader (F::pwind, x, y + 116.0f, tw, nc(), wave);     cap ("a slow side breeze", x, y + 138.0f);
            sfader (F::pspin, xa, y + 116.0f, tw, nc(), plain);   cap ("hits turn it", xa, y + 138.0f);
            sfader (F::pidle, xb, y + 116.0f, tw, nc(), plain);   cap ("a steady turn", xb, y + 138.0f);
            sfader (F::ppuff, x, y + 180.0f, w, nc(), rings);     cap ("each hit puffs smoke off the shape, or sends a ring through the puff", x, y + 202.0f);
        }
        else
        {
            trailWords (x, y);
            sfader (F::ptrails, x, y + 52.0f, w, nc(), comet);
            sfader (F::phang, x, y + 122.0f, w, nc(), plain);
            cap ("how long smoke", x, y + 144.0f);
            cap ("hangs in the air", x, y + 158.0f);
        }
    }

    // The picture in use, how to choose another, and what the shape is doing to the smoke
    void pictureSpecial (float x, float y, float w)
    {
        static const char* const acts[4][3] = { { "smoke pours out from behind the 3D shape", "how much pours", "lows pour more" },
                                                { "a puff of cloud sits on the shape, and every beat punches it in", "how thick the puff is", "lows make it throb" },
                                                { "one stream of smoke wraps round the 3D shape", "how thick the stream", "lows feed the stream" },
                                                { "the picture is 3D smoke, and blows away", "how fast it re-forms", "lows blow it harder" } };
        auto* self = &v;
        const float ts = 104.0f;
        const auto col = nc();
        const auto* act = acts[juce::jlimit (0, 3, S().w[C::pform])];
        const juce::Rectangle<float> frame (x, y - 6.0f, ts, ts);
        {
            juce::Graphics::ScopedSaveState state (g);
            juce::Path round;
            round.addRoundedRectangle (frame, 8.0f);
            g.reduceClipRegion (round);
            g.setColour (juce::Colour (0xff0a0a0c));
            g.fillRect (frame);
            if (v.pic != nullptr && v.pic->small.isValid())
            {
                const float a = v.pic->aspect, dw = a >= 1.0f ? ts : ts * a, dh = a >= 1.0f ? ts / a : ts;
                g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
                g.drawImage (v.pic->small, { x + (ts - dw) * 0.5f, y - 6.0f + (ts - dh) * 0.5f, dw, dh });
            }
        }
        tube (Neon::roundRect (x, y - 6.0f, ts, ts, 8.0f), col, 0.6f);
        ui (frame, [self] { self->choosePicture(); });
        const float rx = x + ts + 22.0f, cw = word ("choose picture", rx, y + 14.0f, 20.0f, col, true);
        ui ({ rx - 4.0f, y - 8.0f, cw + 8.0f, 28.0f }, [self] { self->choosePicture(); });
        cap ("or drop one on the visualiser", rx, y + 34.0f);
        const float tw0 = word ("try", rx, y + 66.0f, 15.0f, col, false);
        words ({ "moon", "flower", "sunset" }, rx + tw0 + 12.0f, y + 66.0f, 17.0f, col, v.pic != nullptr ? v.pic->sample : -1, 11.0f,
               [self] (int i) { self->setPicture (Picture::builtIn (i)); self->last = {}; }, false);
        cap (v.pic != nullptr && ! v.pic->plain ? "fills its frame: what stands out is the shape" : "plain ground found: the shape is cut out", rx, y + 86.0f);
        cap (juce::String ("Now: ") + act[0] + ".", x, y + 124.0f);
        const float tw = (w - 40.0f) / 3.0f, xa = x + tw + 20.0f, xb = x + 2.0f * (tw + 20.0f);
        sfader (F::psmoke, x, y + 166.0f, tw, nc(), plain);   cap (act[1], x, y + 188.0f);
        sfader (F::pfeed, xa, y + 166.0f, tw, nc(), rings);   cap (act[2], xa, y + 188.0f);
        sfader (F::preach, xb, y + 166.0f, tw, nc(), plain);  cap ("how far smoke strays", xb, y + 188.0f);
    }

    void trailsRow (float x, float y) { wrow ("trails", S().trailModeKey(), { "smooth", "echo" }, x, y, nc()); }
    void idleWheel (float x, float y)
    {
        wheel (S().idleKey(), x + 58.0f, y + 50.0f, 44.0f, nc(), [r = &v.readout] { return -r->turn.load(); }, "idle spin");
    }

    // shape (0), colour (1), motion (2) or trails (3) of the mode on show
    void section (int which, float x, float y0, float w)
    {
        static const char* const titles[] = { "shape", "colour", "motion", "trails" };
        heading (titles[which], x, y0 + 28.0f);
        const float y = y0 + 62.0f;
        const int style = S().style;
        const float hw = (w - 28.0f) / 2.0f, rx = x + 172.0f, rw = w - 172.0f;
        if (style == kaleido)      { kaleidoSection (which, x, y, w); return; }
        if (style == lotus)        { lotusSection (which, x, y, w); return; }
        if (style == pictureStyle) { pictureSection (which, x, y, w); return; }
        if (which == 1) { colourSection (x, y, w); return; }
        if (style == ink)
        {
            if (which == 0)
            {
                mirrorRow (x, y);
                bulbCount (F::seg, 10, x, y + 46.0f, w, nc(), false);
                sfader (F::trip, x, y + 100.0f, w, nc(), wave);
                const auto cd = nc();
                layersPic (x, y + 142.0f, cd, F::depth);
                sfader (F::depth, x + 86.0f, y + 158.0f, w - 86.0f, cd, plain);
            }
            else if (which == 2)
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
            if (which == 0)
            {
                bulbCount (F::lcount, 9, x, y + 22.0f, w, nc(), false);
                sfader (F::lsize, x, y + 84.0f, hw, nc(), plain);
                sfader (F::lwob, x + hw + 28.0f, y + 84.0f, hw, nc(), wave);
                sfader (F::ldrops, x, y + 142.0f, w, nc(), comet);
                const auto cd = nc();
                layersPic (x, y + 184.0f, cd, F::ldepth);
                sfader (F::ldepth, x + 86.0f, y + 200.0f, w - 86.0f, cd, plain);
            }
            else if (which == 2)
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
            if (which == 0)
            {
                sfader (F::sgrain, x, y + 22.0f, hw, nc(), jag);
                sfader (F::scomp, x + hw + 28.0f, y + 22.0f, hw, nc(), wave);
                sfader (F::ssharp, x, y + 80.0f, hw, nc(), plain);
                sfader (F::sscatter, x + hw + 28.0f, y + 80.0f, hw, nc(), jag);
                sfader (F::schange, x, y + 138.0f, w, nc(), comet);
            }
            else if (which == 2) { idleWheel (x, y); sfader (F::sspin, rx, y + 40.0f, rw, nc(), plain); }
            else { trailsRow (x, y); sfader (F::strails, x, y + 52.0f, w, nc(), comet); }
        }
        else
        {
            if (which == 0)
            {
                mirrorRow (x, y);
                sfader (F::bspeed, x, y + 56.0f, hw, nc(), comet);
                sfader (F::bcover, x + hw + 28.0f, y + 56.0f, hw, nc(), wave);
                sfader (F::bseeds, x, y + 114.0f, hw, nc(), jag);
                sfader (F::bfresh, x + hw + 28.0f, y + 114.0f, hw, nc(), plain);
            }
            else if (which == 2) { idleWheel (x, y); sfader (F::bspin, rx, y + 40.0f, rw, nc(), plain); }
            else { trailsRow (x, y); sfader (F::btrails, x, y + 52.0f, w, nc(), comet); }
        }
    }

    // The controls only this mode has
    void special (float x, float y0, float w)
    {
        static const char* const styles[] = { "ink", "liquid", "sand", "bloom", "kaleido", "lotus", "picture" };
        heading (juce::String ("only in ") + styles[S().style], x, y0 + 28.0f);
        const float y = y0 + 62.0f, hw = (w - 28.0f) / 2.0f, x2 = x + hw + 28.0f;
        auto* r = &v.readout;
        if (S().style == kaleido)
        {
            sfader (F::klight, x, y + 24.0f, hw, nc(), plain);     cap ("a steady lamp", x, y + 46.0f);
            sfader (F::kbreathe, x2, y + 24.0f, hw, nc(), wave);   cap ("a slow swell with the sound", x2, y + 46.0f);
            sfader (F::kfrost, x, y + 98.0f, hw, nc(), wave);      cap ("clear to seedy glass", x, y + 120.0f);
            sfader (F::kspark, x2, y + 98.0f, hw, nc(), jag);      cap ("slow glints", x2, y + 120.0f);
        }
        else if (S().style == lotus)
        {
            cap ("These three shape the glass look.", x, y + 10.0f);
            sfader (F::zlight, x, y + 50.0f, hw, nc(), plain);     cap ("a steady lamp", x, y + 72.0f);
            sfader (F::zfrost, x2, y + 50.0f, hw, nc(), wave);     cap ("clear to seedy glass", x2, y + 72.0f);
            sfader (F::zlead, x, y + 124.0f, hw, nc(), plain);     cap ("the lines between petals", x, y + 146.0f);
        }
        else if (S().style == pictureStyle) pictureSpecial (x, y, w);
        else if (S().style == ink)
        {
            breatheRing (F::breathe, x + 58.0f, y + 50.0f, 44.0f, nc());
            const float rx = x + 150.0f, rw = w - 150.0f;
            sfader (F::marble, rx, y + 24.0f, rw, nc(), wave);
            small ("veins run through the ink", rx, y + 46.0f, dim);
            sfader (F::bleed, rx, y + 96.0f, rw, nc(), rings);
            small ("colour soaks out past the edges", rx, y + 118.0f, dim);
            small ("Breathe: the whole blot slowly swells and shrinks.", x, y + 170.0f, dim);
        }
        else if (S().style == liquid)
        {
            mirrorRow (x, y);
            thermometer (F::lheat, x + 26.0f, y + 40.0f, 92.0f, nc());
            const float rx = x + 84.0f, rw = w - 84.0f;
            mergeBlobs (F::lmerge, rx, y + 74.0f, rw, nc());
            sfader (F::lshim, rx, y + 156.0f, rw, nc(), jag);
            small ("light rippling through the oil, brighter on highs", rx, y + 178.0f, dim);
        }
        else if (S().style == sand)
        {
            mirrorRow (x, y);
            grainTray (F::sdens, x, y + 46.0f, w - 130.0f, 72.0f, nc());
            wheel (F::sdrift, x + w - 50.0f, y + 76.0f, 38.0f, nc(), [r, amount = S().v[F::sdrift]] { return (float) std::fmod (r->clock.load() * (double) amount * 2.0, 6.283185307); }, "drift");
            sfader (F::sres, x, y + 176.0f, w, nc(), jag);
            small ("hats and highs make the grains shiver", x, y + 198.0f, dim);
        }
        else
        {
            branchStem (F::bbranch, x, y + 34.0f, w, nc());
            small ("from round spots to long branches", x, y + 72.0f, dim);
            sfader (F::brings, x, y + 124.0f, w - 124.0f, nc(), rings);
            small ("each hit sends a ring of light", x, y + 146.0f, dim);
            small ("through the growth", x, y + 160.0f, dim);
            wheel (F::bhalo, x + w - 48.0f, y + 130.0f, 36.0f, nc(), [r] { return (float) std::fmod (r->clock.load() * 0.4, 6.283185307); }, "halo");
        }
    }

    // Modes, meters, the page switch and the main reactions
    void head (float x, float y0, float w)
    {
        auto* self = &v;
        auto* r = &v.readout;
        const bool rainbowOn = Theme::get().isRainbow();
        const auto col = rainbowOn ? juce::Colour (0xffbfeaff) : Theme::get().themeColour();
        float y = y0 + 40.0f;
        words ({ "ink", "liquid", "sand", "bloom", "kaleido", "lotus", "picture" }, x, y, 19.0f, nc(), S().style, 9.0f,
               [self] (int i) { self->settings.style = i; self->last = {}; self->edited = true; self->changed(); });
        y += 34.0f;
        static const char* const rows[] = { "low", "mid", "high" };
        for (int i = 0; i < 3; ++i) word (rows[i], x, y + (float) i * 26.0f + 5.0f, 15.0f, col, false);
        dyn ({ x + 46.0f, y - 6.0f, w - 44.0f, 64.0f }, [=] (juce::Graphics& gg)
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
        words ({ "controls", "routing" }, x, y, 20.0f, nc(), v.page, 20.0f, [self] (int i) { self->page = i; self->last = {}; self->relayout(); }, false);

        const float th = 84.0f, ty = y + 22.0f, gp = juce::jmin (64.0f, (w - 120.0f) / 4.0f + 8.0f);
        vtube (F::pulse, x + 18.0f, ty, th, nc(), [r] { return r->reaction[dPulse].load(); });
        vtube (F::flash, x + 18.0f + gp, ty, th, nc(), [r] { return r->reaction[dFlash].load(); });
        vtube (F::stir, x + 18.0f + gp * 2.0f, ty, th, nc(), [r] { return r->reaction[dStir].load() * 0.6f; });
        vtube (F::react, x + 18.0f + gp * 3.0f, ty, th, nc(), [r] { return r->signal[sLevel].load(); }, "gain");
        wheel (F::speed, x + w - 46.0f, ty + th * 0.46f, 38.0f, nc(), [r] { return r->flow.load() * 1.3f; });
        // What was last touched, and its value: it changes with every drag anywhere, so it is drawn with the moving parts
        const float ly = ty + th + 56.0f;
        dyn ({ x, ly - 12.0f, w, 16.0f }, [self, x, ly] (juce::Graphics& gg) { if (self->last.isNotEmpty()) Neon::label (gg, self->last, x, ly, Neon::Align::left, juce::Colour (0xffb8bac0), 11.5f); });
    }

    void pictures (float x, float y, float w)
    {
        const int style = S().style;
        const float bw = juce::jmin (w / 4.0f, 124.0f);
        heading ("look", x, y + 28.0f);
        iconRow (0, x, y + 66.0f, bw, nc());
        heading (style == bloom ? "kind" : style == sand ? "plate" : style == kaleido ? "mirrors" : style == lotus ? "petals" : style == pictureStyle ? "the shape" : "form", x, y + 146.0f);
        iconRow (1, x, y + 184.0f, bw, nc());
    }

    // Eight lamps that each keep a whole look
    void sceneLamps (float x, float y, float w)
    {
        auto* self = &v;
        heading ("scenes", x, y + 28.0f);
        const auto sc = nc();
        const float sw = word ("store", x + w, y + 30.0f, 17.0f, sc, v.storing, Neon::Align::right);
        ui ({ x + w - sw - 6.0f, y + 10.0f, sw + 12.0f, 28.0f }, [self]
        {
            self->storing = ! self->storing;
            self->last = self->storing ? "pick a lamp to store this look" : juce::String();
            self->pieces[cScenes].dirty = true;
            self->repaint();
        });
        const float gap = juce::jmin (46.0f, (w - 20.0f) / 7.0f);
        for (int i = 0; i < 8; ++i)
        {
            const float bx = x + 12.0f + (float) i * gap, by = y + 66.0f;
            const bool has = v.scenes[(size_t) i].has_value();
            Neon::bulb (g, bx, by, sc, v.scene == i && has ? 1.0f : has ? 0.45f : 0.0f, 8.0f);
            small (juce::String (i + 1), bx, by + 24.0f, has ? juce::Colour (0xffb8bac0) : dim, Neon::Align::centre, 10.5f);
            ui ({ bx - gap * 0.5f, by - 16.0f, gap, 46.0f }, [self, i] { self->sceneClicked (i); });
        }
        small (v.storing ? "Now pick a lamp to keep this look in." : "Click store, then a lamp, to keep a look. Click a lit lamp to bring it back.", x, y + 112.0f, dim);
    }

    //==========================================================================
    // Routing

    // A strip of recent hits or levels with a line you drag: only what reaches the line gets through.
    // which: 0 loudest lows, 1 transients, 2 low, 3 mid, 4 high
    // An upright bar with round ends, for the parts that move. Its body is a plain rectangle, which costs one
    // quad, and its two ends join `caps`, filled once for all the bars: a whole bar as a shape is cut into rows.
    static void pill (juce::Graphics& gg, juce::Path& caps, float x, float y, float w, float h)
    {
        if (h > w)
        {
            gg.fillRect (x, y + w * 0.5f, w, h - w);
            caps.addEllipse (x, y + h - w, w, w);
        }
        caps.addEllipse (x, y, w, w);
    }

    void gateStrip (int which, float x, float y, float w, float h, juce::Colour c)
    {
        static const int keys[] = { F::gate, F::tgate, F::lgate, F::mgate, F::hgate };
        static const char* const names[] = { "loudest lows", "transients", "low", "mid", "high" };
        static const char* const notes[] = { "low hits", "every sharp hit", "level", "level", "level" };
        const int key = keys[which];
        const float val = S().v[(size_t) key];
        const float lw = word (names[which], x, y - 8.0f, 16.0f, c, true);
        small (notes[which], x + lw + 12.0f, y - 9.0f, dim);
        valTxt (key, x + w, y - 9.0f);
        glassBox ({ x, y, w, h }, 8.0f, juce::Colour (0x8008080a));
        const float ly = y + h - 4.0f - (h - 8.0f) * val;
        tube ({ { x + 4.0f, ly }, { x + w - 4.0f, ly } }, c, 0.5f);
        Neon::bulb (g, x + w - 8.0f, ly, c, 1.0f, 5.0f);
        hit ({ x, y - 8.0f, w, h + 16.0f }, [set = setter (key), y, h] (float, float py) { set ((y + h - 4.0f - py) / (h - 8.0f)); }, {}, resetOf (key));
        dyn ({ x + 2.0f, y + 2.0f, w - 4.0f, h - 4.0f }, [=, r = &v.readout] (juce::Graphics& gg)
        {
            const double now = r->clock.load();
            const auto lit = c.interpolatedWith (juce::Colours::white, 0.45f);
            const float base = y + h - 4.0f, span = w - 24.0f;
            const juce::SpinLock::ScopedLockType lock (r->lock);
            if (which < 2)
            {
                juce::Path over, under;         // the round ends of every bar of one colour, as a single shape
                const auto* list = which == 0 ? r->kicks.data() : r->sharps.data();
                const int count = which == 0 ? r->numKicks : r->numSharps;
                for (int i = 0; i < count; ++i)
                {
                    const float age = (float) (now - list[i].time);
                    if (age > 5.0f) continue;
                    const float bx = x + w - 14.0f - age / 5.0f * span, bh = juce::jmax (3.0f, (h - 8.0f) * list[i].rel);
                    const bool counts = list[i].rel >= val - 0.001f;
                    gg.setColour (counts ? lit : juce::Colour (0xff55585f));
                    pill (gg, counts ? over : under, bx - 1.5f, base - bh, 3.0f, bh);
                }
                gg.setColour (juce::Colour (0xff55585f));
                gg.fillPath (under);
                gg.setColour (lit);
                gg.fillPath (over);
            }
            else
            {
                // Plain rectangles one at a time: each is a single quad, where a list of them is cut into rows
                for (int i = 0; i < r->numLevels; ++i)
                {
                    const auto& q = r->levels[(size_t) i];
                    const float age = (float) (now - q.time), lv = q.v[which - 2];
                    if (age > 5.0f) continue;
                    const float bx = x + w - 14.0f - age / 5.0f * span, bh = juce::jmax (1.0f, (h - 8.0f) * juce::jmin (1.0f, lv));
                    gg.setColour (lv >= val ? lit : juce::Colour (0xff4a4d55));
                    gg.fillRect (bx - 1.0f, base - bh, 2.0f, bh);
                }
            }
        });
    }

    void gates (float x, float y, float w)
    {
        heading ("what counts", x, y + 28.0f, "drag a line up to let only the bigger ones through");
        for (int i = 0; i < 5; ++i) gateStrip (i, x, y + 70.0f + (float) i * 76.0f, w, 42.0f, nc());
    }

    // Listening bands: the live spectrum, with a tube for low, mid and high whose ends you drag
    void bands (float x, float y, float w)
    {
        auto* self = &v;
        heading ("listening bands", x, y + 28.0f);
        const float sx = x + 50.0f, sw = w - 58.0f, sy = y + 46.0f, sh = 112.0f;
        const std::array<juce::Colour, 3> bc { nc(), nc(), nc() };
        glassBox ({ sx - 8.0f, sy, sw + 16.0f, sh }, 8.0f, juce::Colour (0x8008080a));
        static const std::pair<float, const char*> ticks[] = { { 50.0f, "50" }, { 100.0f, "100" }, { 200.0f, "200" }, { 500.0f, "500" }, { 1000.0f, "1k" }, { 2000.0f, "2k" }, { 5000.0f, "5k" }, { 10000.0f, "10k" } };
        for (auto& t : ticks) small (t.second, sx + sw * hzToT (t.first), sy + sh + 14.0f, dim, Neon::Align::centre);
        dyn ({ sx - 6.0f, sy + 2.0f, sw + 12.0f, sh - 4.0f }, [=, range = S().bandHz] (juce::Graphics& gg)
        {
            const int n = VisListener::spectrumBars;
            const float bar = juce::jmax (2.0f, sw / (float) n - 3.0f);
            std::array<juce::Path, 4> bars;         // the bars' round ends, one shape a colour: outside every band, then low, mid and high
            const std::array<juce::Colour, 4> colours { juce::Colour (0xff4a4d55), bc[0].interpolatedWith (juce::Colours::white, 0.3f),
                                                        bc[1].interpolatedWith (juce::Colours::white, 0.3f), bc[2].interpolatedWith (juce::Colours::white, 0.3f) };
            for (int i = 0; i < n; ++i)
            {
                const float fc = tToHz (((float) i + 0.5f) / (float) n);
                auto& shown = self->spectrumShown[(size_t) i];
                shown = juce::jmax (self->ctx.engine.visListener.spectrum[(size_t) i].load (std::memory_order_relaxed), shown * 0.86f);
                size_t in = 0;
                for (size_t k = 0; k < 3; ++k)
                    if (fc >= range[k][0] && fc <= range[k][1]) in = k + 1;
                const float bx = sx + sw * ((float) i + 0.5f) / (float) n, bh = juce::jmax (bar, shown * (sh - 16.0f));
                gg.setColour (colours[in]);
                pill (gg, bars[in], bx - bar * 0.5f, sy + sh - 6.0f - bh, bar, bh);
            }
            for (size_t k = 0; k < bars.size(); ++k)
            {
                gg.setColour (colours[k]);
                gg.fillPath (bars[k]);
            }
        });
        static const char* const names[] = { "low", "mid", "high" };
        for (int k = 0; k < 3; ++k)
        {
            const float yy = sy + sh + 50.0f + (float) k * 42.0f;
            const auto cl = bc[(size_t) k];
            const float a = S().bandHz[(size_t) k][0], b = S().bandHz[(size_t) k][1], ta = hzToT (a), tb = hzToT (b), xa = sx + sw * ta, xb = sx + sw * tb;
            word (names[k], x, yy + 5.0f, 16.0f, cl, true);
            tube ({ { sx, yy }, { sx + sw, yy } }, cl, 0.6f, true);
            tube ({ { xa, yy }, { xb, yy } }, cl);
            Neon::bulb (g, xa, yy, cl, 1.0f, 5.5f);
            Neon::bulb (g, xb, yy, cl, 1.0f, 5.5f);
            small (hzText (a) + " to " + hzText (b), sx + sw, yy - 13.0f, dim, Neon::Align::right);
            auto moved = [self, k]
            {
                const auto& o = self->settings.bandHz[(size_t) k];
                self->last = juce::String (names[k]) + " listens to " + hzText (o[0]) + " to " + hzText (o[1]);
                self->edited = true;
                self->changed();
            };
            hit ({ xa - 12.0f, yy - 14.0f, 24.0f, 28.0f }, [self, k, sx, sw, moved] (float px, float)
            {
                auto& o = self->settings.bandHz[(size_t) k];
                o[0] = juce::jlimit (Settings::lowestHz, o[1] / 1.25f, tToHz ((px - sx) / sw));
                moved();
            });
            hit ({ xb - 12.0f, yy - 14.0f, 24.0f, 28.0f }, [self, k, sx, sw, moved] (float px, float)
            {
                auto& o = self->settings.bandHz[(size_t) k];
                o[1] = juce::jlimit (o[0] * 1.25f, Settings::highestHz, tToHz ((px - sx) / sw));
                moved();
            });
            // The middle slides the whole band, keeping its width
            if (xb - xa > 24.0f)
                hit ({ xa + 12.0f, yy - 14.0f, xb - xa - 24.0f, 28.0f }, [self, k, sx, sw, ta, tb, moved, grab = std::optional<float>()] (float px, float) mutable
                {
                    const float t = (px - sx) / sw;
                    if (! grab.has_value()) grab = ta - t;
                    const float t0 = juce::jlimit (0.0f, 1.0f - (tb - ta), t + *grab);
                    self->settings.bandHz[(size_t) k] = { tToHz (t0), tToHz (t0 + tb - ta) };
                    moved();
                });
        }
        small ("Drag the ends of a tube to set what that band listens to,", x, sy + sh + 50.0f + 3.0f * 42.0f - 10.0f, dim);
        small ("or the middle to slide it.", x, sy + sh + 50.0f + 3.0f * 42.0f + 5.0f, dim);
        sfader (F::nudge, x, y + 418.0f, w, nc(), plain, "sync nudge");
    }

    // The matrix: sounds down the side, reactions along the top, a socket where they meet
    void matrix (float x, float y, float w)
    {
        auto* self = &v;
        auto* r = &v.readout;
        heading ("routing", x, y + 28.0f);
        const auto rc = nc();
        const float rw = word ("reset wiring", x + w, y + 30.0f, 15.0f, rc, false, Neon::Align::right);
        ui ({ x + w - rw - 6.0f, y + 12.0f, rw + 12.0f, 26.0f }, [self]
        {
            Settings defaults;
            self->settings.resetWiring();
            self->settings.resetBands();
            for (int k : { F::gate, F::tgate, F::lgate, F::mgate, F::hgate, F::nudge }) self->settings.v[(size_t) k] = defaults.v[(size_t) k];
            self->last = "wiring reset";
            self->edited = true;
            self->changed();
        });

        std::array<juce::Colour, numSources> sc;
        for (auto& c : sc) c = nc();
        const auto dc = Theme::get().isRainbow() ? juce::Colour (0xffd8dae0) : Theme::get().themeColour();
        const float lx = x + 124.0f, cw = (w - 124.0f) / (float) numDests, rh = 34.5f, ty = y + 70.0f;
        for (int d = 0; d < numDests; ++d)
        {
            const float cx = lx + cw * ((float) d + 0.5f);
            word (destNames[d], cx, ty, 17.0f, dc, true, Neon::Align::centre);
            dyn ({ cx - 5.0f, ty + 8.0f, 10.0f, 10.0f }, [=] (juce::Graphics& gg) { Neon::bulb (gg, cx, ty + 13.0f, dc, 0.1f + 0.75f * juce::jmin (1.0f, r->reaction[(size_t) d].load()), 3.0f); });
        }
        for (int s = 0; s < numSources; ++s)
        {
            const float yy = ty + 46.0f + (float) s * rh;
            const auto col = sc[(size_t) s];
            word (sourceNames[s], lx - 24.0f, yy + 5.0f, 15.0f, col, true, Neon::Align::right);
            dyn ({ lx - 16.0f, yy - 5.0f, 10.0f, 10.0f }, [=] (juce::Graphics& gg) { Neon::bulb (gg, lx - 11.0f, yy, col, 0.12f + 0.9f * juce::jmin (1.0f, r->signal[(size_t) s].load()), 3.4f); });
            g.setColour (juce::Colours::white.withAlpha (0.045f));
            g.drawLine (lx, yy, x + w, yy, 1.0f);
            for (int d = 0; d < numDests; ++d)
            {
                const float cx = lx + cw * ((float) d + 0.5f), a = S().route[(size_t) s][(size_t) d];
                g.setColour (juce::Colour (0xff101012));
                g.fillEllipse (cx - 10.0f, yy - 10.0f, 20.0f, 20.0f);
                g.setColour (juce::Colour (a > 0.0f ? 0xff55585f : 0xff3a3c42));
                g.drawEllipse (cx - 10.0f, yy - 10.0f, 20.0f, 20.0f, 1.2f);
                if (a > 0.0f)
                {
                    // The ring round the socket is the strength; the lamp inside flickers with the sound
                    Line p;
                    const int n = juce::jmax (2, juce::roundToInt (40.0f * a));
                    for (int q = 0; q <= n; ++q)
                    {
                        const float an = -juce::MathConstants<float>::halfPi + a * juce::MathConstants<float>::twoPi * (float) q / (float) n;
                        p.push_back ({ cx + std::cos (an) * 14.5f, yy + std::sin (an) * 14.5f });
                    }
                    tube (p, col, 0.6f);
                    const float lamp = 3.0f + a * 3.5f;
                    Neon::bulb (g, cx, yy, col, 0.3f + a * 0.5f, lamp);
                    dyn ({ cx - lamp - 1.0f, yy - lamp - 1.0f, lamp * 2.0f + 2.0f, lamp * 2.0f + 2.0f }, [=] (juce::Graphics& gg)
                    {
                        const float lv = juce::jmin (1.0f, r->signal[(size_t) s].load()) * a;
                        if (lv > 0.3f + a * 0.5f) Neon::bulb (gg, cx, yy, col, lv, lamp);
                    });
                }
                // Click an empty socket to plug it in, drag up or down for strength, click a lit one to unplug
                auto say = [self, s, d]
                {
                    const float now = self->settings.route[(size_t) s][(size_t) d];
                    self->last = sourceNames[s] + " to " + destNames[d] + (now > 0.0f ? "  " + juce::String (juce::roundToInt (now * 100.0f)) + "%" : juce::String ("  unplugged"));
                    self->edited = true;
                    self->changed();
                };
                hit ({ cx - cw * 0.5f, yy - rh * 0.5f, cw, rh },
                     [self, s, d, say] (float, float py)
                     {
                         auto& c = self->cell;
                         if (! c.started) { c.started = true; c.y = py; return; }
                         if (std::abs (c.y - py) > 3.0f) c.moved = true;
                         if (! c.moved) return;
                         const float to = juce::jlimit (0.0f, 1.0f, c.from + (c.y - py) / 70.0f);
                         self->settings.route[(size_t) s][(size_t) d] = to < 0.02f ? 0.0f : to;
                         say();
                     },
                     [self, s, d, say]
                     {
                         float& now = self->settings.route[(size_t) s][(size_t) d];
                         self->cell = { now > 0.0f ? now : 1.0f, 0.0f, now > 0.0f, false, false };
                         if (now <= 0.0f) { now = 1.0f; say(); }
                     },
                     {},
                     [self, s, d, say]
                     {
                         if (self->cell.was && ! self->cell.moved) { self->settings.route[(size_t) s][(size_t) d] = 0.0f; say(); }
                     });
            }
        }
        small ("Click a socket to plug a sound into a reaction. Drag up or down on it for strength. Click a lit one to unplug.", x, ty + 46.0f + (float) numSources * rh, dim);
    }

    //==========================================================================
    // One card: its glass, then its contents in their own coordinates (392 wide less the margins)
    void drawCard()
    {
        const auto box = piece.box;
        const float K = cardScale (card, box, piece.designWidth), w = juce::jmin (box.getWidth() / K - 32.0f, piece.designWidth > panelWidth ? 640.0f : cardSpread[card]);
        const float ox = box.getX() + (box.getWidth() - (w + 32.0f) * K) * 0.5f + 16.0f * K;
        const float oy = box.getY() + juce::jmin ((box.getHeight() - cardHeight[card] * K) * 0.5f, box.getHeight() * 0.06f);
        setPlace ({});
        g.setColour (juce::Colour (0xc20d0d0f));
        g.fillRoundedRectangle (box, 14.0f);
        g.setColour (juce::Colour (0xff34363c));
        g.drawRoundedRectangle (box.reduced (0.5f), 14.0f, 1.0f);
        {
            juce::Graphics::ScopedSaveState state (g);
            g.reduceClipRegion (box.getSmallestIntegerContainer());
            const auto t = juce::AffineTransform::scale (K).translated (ox, oy);
            g.addTransform (t);
            setPlace (t);
            switch (card)
            {
                case cHead:    head (0.0f, 0.0f, w); break;
                case cPics:    pictures (0.0f, 0.0f, w); break;
                case cScenes:  sceneLamps (0.0f, 0.0f, w); break;
                case cShape:   section (0, 0.0f, 0.0f, w); break;
                case cColour:  section (1, 0.0f, 0.0f, w); break;
                case cMotion:  section (2, 0.0f, 0.0f, w); break;
                case cTrails:  section (3, 0.0f, 0.0f, w); break;
                case cSpecial: special (0.0f, 0.0f, w); break;
                case cBands:   bands (0.0f, 0.0f, w); break;
                case cGates:   gates (0.0f, 0.0f, w); break;
                case cRoute:   matrix (0.0f, 0.0f, w); break;
                default: break;
            }
        }
        setPlace ({});
        block (box);
    }

    // The word over the picture that gives it the whole window, or brings the controls back
    void drawChrome (float Z)
    {
        auto* self = &v;
        setPlace ({});
        const auto box = piece.box;
        const auto col = Theme::get().themeColour();
        if (v.maximised) word ("show controls", box.getRight() - 6.0f, box.getY() + 24.0f, 19.0f, col, true, Neon::Align::right);
        else             word ("fill window", box.getRight() - 6.0f * Z, box.getBottom() - 8.0f * Z, 15.0f * Z, col, false, Neon::Align::right);
        ui (box, [self] { self->toggleMaximised(); });
    }
};

void Visualiser::rebuild (int n, const Layout& l, float scale)
{
    auto& p = pieces[(size_t) n];
    p.dirty = false;
    p.area = p.box.getSmallestIntegerContainer().expanded (n == cChrome ? 16 : 8).getIntersection (getLocalBounds());
    if (p.area.isEmpty()) { p.art = {}; p.hits.clear(); p.live.clear(); return; }
    if (p.glow == nullptr) p.glow = std::make_unique<Neon::GlowLayer>();

    auto steady = [&] (juce::Graphics& gg)
    {
        p.hits.clear();
        p.live.clear();
        Draw d { *this, gg, p, n, n * 3 };
        if (n == cChrome) d.drawChrome (l.Z);
        else              d.drawCard();
        Neon::glow().base = Neon::glow().current = {};
    };

    // Once with nothing drawn, to gather the lit shapes for the glow
    auto& glow = Neon::glow();
    glow.sources.clear();
    glow.collecting = true;
    glow.alpha = 1.0f;
    {
        juce::Image none (juce::Image::ARGB, 1, 1, true);
        juce::Graphics dg (none);
        dg.reduceClipRegion (juce::Rectangle<int>());
        steady (dg);
    }
    glow.collecting = false;
    p.glow->build (p.area.getWidth(), p.area.getHeight(), n == cChrome ? l.Z : cardScale (n, p.box, p.designWidth));

    // Then for real, into a picture (a new picture each time: one drawn over in place can show stale on the Mac)
    juce::Image image (juce::Image::ARGB, juce::jmax (1, juce::roundToInt ((float) p.area.getWidth() * scale)), juce::jmax (1, juce::roundToInt ((float) p.area.getHeight() * scale)), true);
    {
        juce::Graphics ig (image);
        ig.addTransform (juce::AffineTransform::translation ((float) -p.area.getX(), (float) -p.area.getY()).scaled (scale));
        p.glow->draw (ig, p.area.toFloat());
        steady (ig);
    }
    p.art = image;
}

void Visualiser::markAll()
{
    for (auto& p : pieces) p.dirty = true;
}

void Visualiser::relayout()
{
    wholePanel = true;
    changed();
    wholePanel = false;
}

void Visualiser::paint (juce::Graphics& g)
{
    struct Timed          // GLOWTAG_VIS_STATS=1 also prints how long the panel takes to draw
    {
        Visualiser& v;
        const double from = juce::Time::getMillisecondCounterHiRes();
        ~Timed()
        {
            const double took = juce::Time::getMillisecondCounterHiRes() - from;
            v.statPanel = v.statPanel.load() + took;
            v.statPanelWorst = juce::jmax (v.statPanelWorst.load(), took);
            ++v.statPanels;
        }
    } timed { *this };
    juce::String error;
    {
        const juce::SpinLock::ScopedLockType lock (readout.lock);
        error = readout.error;
    }

    const auto l = layout();
    const float scale = (float) g.getInternalContext().getPhysicalPixelScaleFactor();
    const auto look = Theme::get().themeColour().getARGB() ^ (Theme::get().isRainbow() ? 0x5a5a5a5au : 0u);
    if (std::abs (scale - artScale) > 0.001f || look != artLook)
    {
        artScale = scale;
        artLook = look;
        markAll();
    }

    // The dark surround, with a rounded window in it for the picture the graphics card draws underneath
    // A shape is worked out against everything being drawn again, so the plain part of the surround is laid
    // as rectangles, and the screen's frame (its round corners and two outlines) only when it needs drawing.
    if (! maximised)
    {
        const auto frame = l.screen.expanded (7.0f).getSmallestIntegerContainer();
        g.setColour (juce::Colour (0xff141416));
        juce::RectangleList<int> outside (getLocalBounds());
        outside.subtract (frame);
        g.fillRectList (outside);
        if (g.clipRegionIntersects (frame))
        {
            juce::Graphics::ScopedSaveState state (g);
            g.reduceClipRegion (frame);
            juce::Path surround;
            surround.addRectangle (frame.toFloat());
            surround.addRoundedRectangle (l.screen, 12.0f * l.Z);
            surround.setUsingNonZeroWinding (false);
            g.fillPath (surround);
            g.setColour (juce::Colour (0xff3a3c42));
            g.drawRoundedRectangle (l.screen.expanded (0.5f), 12.0f * l.Z, 1.0f);
            g.setColour (juce::Colour (0xff26282d));
            g.drawRoundedRectangle (l.screen.expanded (5.5f), 16.0f * l.Z, 1.0f);
        }
    }
    if (error.isNotEmpty())
    {
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.setFont (Theme::get().body (13.0f));
        g.drawFittedText ("The visualiser could not start on this graphics card:\n" + error, l.screen.toNearestInt().reduced (40), juce::Justification::centred, 12);
    }

    // The steady part of each card (glass, tubes, words, their glow) is kept as a picture and only drawn
    // again when something on that card changes. Each frame lays the pictures down and adds the moving parts.
    for (int n = 0; n < numPieces; ++n)
    {
        auto& p = pieces[(size_t) n];
        if (p.shown != l.shown[(size_t) n] || p.box != l.box[(size_t) n] || ! juce::approximatelyEqual (p.designWidth, l.designWidth[(size_t) n]))
        {
            p.shown = l.shown[(size_t) n];
            p.box = l.box[(size_t) n];
            p.designWidth = l.designWidth[(size_t) n];
            p.dirty = true;
        }
        if (! p.shown) { p.hits.clear(); p.live.clear(); p.art = {}; continue; }
        if (p.dirty || ! p.art.isValid()) rebuild (n, l, scale);
        if (n == cChrome && maximised && ! chromeShown) continue;
        if (p.art.isValid() && g.clipRegionIntersects (p.area))
            g.drawImage (p.art, p.area.toFloat());
        for (auto& part : p.live)
        {
            if (! g.clipRegionIntersects (part.box)) continue;
            juce::Graphics::ScopedSaveState state (g);
            g.reduceClipRegion (part.box);          // so each lamp and bar is worked out against its own box, not the whole panel
            g.addTransform (part.place);
            part.draw (g);
        }
    }
}

//==============================================================================
// Scenes

void Visualiser::sceneClicked (int i)
{
    if (i < 0 || i >= (int) scenes.size()) return;
    if (storing)
    {
        scenes[(size_t) i] = settings;
        storing = false;
        scene = i;
        last = "stored in scene " + juce::String (i + 1);
        edited = true;
        relayout();
    }
    else if (scenes[(size_t) i].has_value())
    {
        // A scene is a look: the mode and everything that shapes and colours it. What the picture
        // listens to (wiring, lines, bands, sync) and the backdrop stay as they are.
        const auto& from = *scenes[(size_t) i];
        const auto keep = settings;
        settings.style = from.style;
        settings.v = from.v;
        settings.w = from.w;
        for (int k : { F::gate, F::tgate, F::lgate, F::mgate, F::hgate, F::nudge, F::back }) settings.v[(size_t) k] = keep.v[(size_t) k];
        scene = i;
        last = "scene " + juce::String (i + 1);
        edited = true;
        relayout();
    }
}

juce::var Visualiser::scenesVar() const
{
    juce::Array<juce::var> list;
    for (auto& s : scenes) list.add (s.has_value() ? s->toVar() : juce::var());
    return list;
}

//==============================================================================
// Pointer

const Visualiser::Hit* Visualiser::hitAt (juce::Point<float> p) const
{
    for (int n = numPieces - 1; n >= 0; --n)          // the word over the picture first
    {
        auto& piece = pieces[(size_t) n];
        if (! piece.shown) continue;
        for (auto& h : piece.hits)
            if (h.box.contains (p)) return &h;
    }
    return nullptr;
}

void Visualiser::mouseDown (const juce::MouseEvent& e)
{
    if (auto* h = hitAt (e.position))
    {
        const auto hitCopy = *h;
        if (e.mods.isAltDown() && probeLane (hitCopy, e.position))
            return;            // the lists are rebuilt by the repaint a click can cause
        if (hitCopy.click != nullptr) hitCopy.click();
        activeCard = hitCopy.card;
        activeDrag = hitCopy.drag;
        activeUp = hitCopy.up;
        if (activeDrag != nullptr) activeDrag (e.position.x, e.position.y);
        return;
    }
    // Anywhere on the picture: stir it by hand
    if (screenBox.contains (e.position) && ! screenBox.isEmpty())
    {
        draggingPicture = true;
        dragX = (e.position.x - screenBox.getX()) / screenBox.getWidth();
        dragY = (e.position.y - screenBox.getY()) / screenBox.getHeight();
    }
}

void Visualiser::mouseDrag (const juce::MouseEvent& e)
{
    if (draggingPicture)
    {
        dragX = juce::jlimit (0.0f, 1.0f, (e.position.x - screenBox.getX()) / screenBox.getWidth());
        dragY = juce::jlimit (0.0f, 1.0f, (e.position.y - screenBox.getY()) / screenBox.getHeight());
    }
    else if (activeDrag != nullptr)
        activeDrag (e.position.x, e.position.y);
}

void Visualiser::mouseUp (const juce::MouseEvent&)
{
    if (draggingPicture) { draggingPicture = false; dragX = -1.0f; dragY = -1.0f; }
    activeDrag = nullptr;
    if (activeUp != nullptr) { const auto up = activeUp; activeUp = nullptr; up(); }
    activeCard = -1;
    saveDue = true;
}

void Visualiser::mouseMove (const juce::MouseEvent& e)
{
    lastPointerMove = juce::Time::getMillisecondCounter();
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
    if (screenBox.contains (e.position)) toggleMaximised();                      // the picture takes the window, or gives it back
}

void Visualiser::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) {}

void Visualiser::resized()
{
    changed();
}

//==============================================================================
// Automation

bool Visualiser::readSetting (const Settings& s, const AutoTarget& t, float& out)
{
    if (t.kind != AutoTarget::vis) return false;
    switch (t.module)
    {
        case 0: if (t.param < 0 || t.param >= F::count) return false; out = s.v[(size_t) t.param]; return true;
        case 1: if (t.param < 0 || t.param >= C::count) return false; out = (float) s.w[(size_t) t.param]; return true;
        case 2: if (t.param < 0 || t.param >= (int) numSources * (int) numDests) return false; out = s.route[(size_t) (t.param / numDests)][(size_t) (t.param % numDests)]; return true;
        case 3: out = (float) s.style; return true;
        default: return false;
    }
}

void Visualiser::writeSetting (Settings& s, const AutoTarget& t, float raw)
{
    if (t.kind != AutoTarget::vis) return;
    switch (t.module)
    {
        case 0: if (t.param >= 0 && t.param < F::count) s.v[(size_t) t.param] = juce::jlimit (0.0f, 1.0f, raw); break;
        case 1: if (t.param >= 0 && t.param < C::count) s.w[(size_t) t.param] = juce::jmax (0, juce::roundToInt (raw)); break;
        case 2: if (t.param >= 0 && t.param < (int) numSources * (int) numDests) s.route[(size_t) (t.param / numDests)][(size_t) (t.param % numDests)] = juce::jlimit (0.0f, 1.0f, raw); break;
        case 3: s.style = juce::jlimit (0, numStyles - 1, juce::roundToInt (raw)); break;
        default: break;
    }
}

std::vector<AppContext::ParamInfo> Visualiser::automatableParams() const
{
    using P = AppContext::ParamInfo;
    std::vector<P> out;
    auto fader = [&] (const char* group, int key)
    {
        P p; p.group = group; p.name = juce::String (faderLabel (key)).toLowerCase(); p.target = { AutoTarget::vis, 0, key };
        out.push_back (p);
    };
    auto choice = [&] (const char* group, const char* name, int key, int count)
    {
        P p; p.group = group; p.name = name; p.hi = (float) (count - 1); p.stepped = true; p.target = { AutoTarget::vis, 1, key };
        out.push_back (p);
    };
    {
        P p; p.group = "visualiser"; p.name = "mode"; p.hi = (float) (numStyles - 1); p.stepped = true; p.target = { AutoTarget::vis, 3, 0 };
        out.push_back (p);
    }
    for (int k : { F::react, F::pulse, F::stir, F::flash, F::speed, F::back, F::gate, F::nudge }) fader ("visualiser", k);
    for (int k : { F::tgate, F::lgate, F::mgate, F::hgate }) { fader ("visualiser", k); out.back().name << " line"; }
    for (int k : { F::hue, F::tint, F::psy, F::trip, F::seg, F::depth, F::spin, F::idle, F::scale, F::swirl, F::idetail, F::chaos, F::vort, F::trails, F::drops, F::fade, F::breathe, F::marble, F::bleed })
        fader ("ink", k);
    choice ("ink", "look", C::surface, 3); choice ("ink", "form", C::form, 4); choice ("ink", "mirror", C::mirror, 5);
    choice ("ink", "tones", C::tones, 4); choice ("ink", "trail kind", C::trails, 2);
    for (int k : { F::lhue, F::ltint, F::lpsy, F::lcount, F::lsize, F::lwob, F::ldrops, F::ldepth, F::lspin, F::lidle, F::lflow, F::lswell, F::ltrails, F::lheat, F::lmerge, F::lshim })
        fader ("liquid", k);
    choice ("liquid", "look", C::lsurface, 3); choice ("liquid", "form", C::lform, 4); choice ("liquid", "mirror", C::lmirror, 5);
    choice ("liquid", "tones", C::ltones, 4); choice ("liquid", "trail kind", C::ltrails, 2);
    for (int k : { F::shue, F::stint, F::spsy, F::sgrain, F::scomp, F::ssharp, F::sscatter, F::schange, F::sspin, F::sidle, F::strails, F::sdens, F::sdrift, F::sres })
        fader ("sand", k);
    choice ("sand", "look", C::ssurface, 3); choice ("sand", "plate", C::sform, 3); choice ("sand", "mirror", C::smirror, 5);
    choice ("sand", "tones", C::stones, 4); choice ("sand", "trail kind", C::strails, 2);
    for (int k : { F::bhue, F::btint, F::bpsy, F::bspeed, F::bcover, F::bseeds, F::bfresh, F::bspin, F::bidle, F::btrails, F::bbranch, F::brings, F::bhalo })
        fader ("bloom", k);
    choice ("bloom", "look", C::bsurface, 3); choice ("bloom", "kind", C::btype, 3); choice ("bloom", "mirror", C::bmirror, 5);
    choice ("bloom", "tones", C::btones, 4); choice ("bloom", "trail kind", C::btrails, 2);
    for (int k : { F::khue, F::ktint, F::kpsy, F::kplfo, F::kprate, F::kpsway, F::kwash, F::kseg, F::ksize, F::kjag, F::klead, F::ktwist, F::kbulge, F::klayer,
                   F::kspin, F::kidle, F::ktumble, F::kswell, F::kripple, F::ktrails, F::klight, F::kbreathe, F::kfrost, F::kspark })
        fader ("kaleidoscope", k);
    choice ("kaleidoscope", "look", C::ksurface, 3); choice ("kaleidoscope", "mirrors", C::kform, 3); choice ("kaleidoscope", "cut", C::kcut, 4);
    choice ("kaleidoscope", "tones", C::ktones, 4); choice ("kaleidoscope", "colours", C::kcolour, 4); choice ("kaleidoscope", "sway with", C::kpsyon, 4);
    choice ("kaleidoscope", "trail kind", C::ktrails, 2);
    for (int k : { F::zhue, F::ztint, F::zpsy, F::zplfo, F::zprate, F::zpsway, F::zwash, F::zpetals, F::ztwist, F::zpoint, F::zopen,
                   F::zspin, F::zidle, F::zspeed, F::zlunge, F::ztrails, F::zlight, F::zfrost, F::zlead })
        fader ("lotus", k);
    choice ("lotus", "look", C::zsurface, 3); choice ("lotus", "petals", C::zform, 3);
    choice ("lotus", "tones", C::ztones, 4); choice ("lotus", "colours", C::zcolour, 4); choice ("lotus", "sway with", C::zpsyon, 4);
    choice ("lotus", "trail kind", C::ztrails, 2);
    for (int k : { F::phue, F::ptint, F::ppsy, F::pplfo, F::pprate, F::ppsway, F::pash, F::psize, F::poff, F::pvis, F::phaze, F::pamt, F::pdent,
                   F::prise, F::pswirl, F::pwisp, F::pwind, F::pspin, F::pidle, F::ppuff, F::ptrails, F::phang, F::psmoke, F::pfeed, F::preach })
        fader ("picture", k);
    choice ("picture", "look", C::psurface, 3); choice ("picture", "the shape", C::pform, 4); choice ("picture", "mirror", C::pmirror, 5);
    choice ("picture", "smoke from", C::psrc, 3); choice ("picture", "smoke will", C::pdir, 3);
    choice ("picture", "tones", C::ptones, 4); choice ("picture", "smoke colour", C::pcolour, 2); choice ("picture", "sway with", C::ppsyon, 4);
    choice ("picture", "trail kind", C::ptrails, 2);
    for (int s = 0; s < numSources; ++s)
        for (int d = 0; d < numDests; ++d)
        {
            P p; p.group = "routing"; p.name = sourceNames[s] + " to " + destNames[d];
            p.target = { AutoTarget::vis, 2, s * numDests + d };
            out.push_back (p);
        }
    return out;
}

void Visualiser::applyAutomation()
{
    if (! ctx.project.visTrack || ctx.project.visLanes.empty()) return;
    const double beat = ctx.engine.getPosition();
    bool moved = false;
    for (auto& lane : ctx.project.visLanes)
    {
        if (! lane.plays()) continue;
        const float raw = lane.raw (Auto::valueAt (lane.points, beat));
        float was = 0.0f;
        if (! readSetting (settings, lane.target, was) || std::abs (was - raw) < 1.0e-5f) continue;
        writeSetting (settings, lane.target, raw);
        writeSetting (seen, lane.target, raw);       // the lane moved it, not a hand
        moved = true;
    }
    if (! moved) return;
    {
        const juce::SpinLock::ScopedLockType lock (sharedLock);
        shared = settings;
    }
    markAll();
    if (view == full) repaint();
}

void Visualiser::reportTouches()
{
    if (! ctx.project.visTrack) { seen = settings; return; }
    std::vector<std::pair<AutoTarget, float>> moved;
    for (int i = 0; i < F::count; ++i)
        if (! juce::approximatelyEqual (settings.v[(size_t) i], seen.v[(size_t) i])) moved.push_back ({ { AutoTarget::vis, 0, i }, settings.v[(size_t) i] });
    for (int i = 0; i < C::count; ++i)
        if (settings.w[(size_t) i] != seen.w[(size_t) i]) moved.push_back ({ { AutoTarget::vis, 1, i }, (float) settings.w[(size_t) i] });
    for (int s = 0; s < numSources; ++s)
        for (int d = 0; d < numDests; ++d)
            if (! juce::approximatelyEqual (settings.route[(size_t) s][(size_t) d], seen.route[(size_t) s][(size_t) d]))
                moved.push_back ({ { AutoTarget::vis, 2, s * numDests + d }, settings.route[(size_t) s][(size_t) d] });
    if (settings.style != seen.style) moved.push_back ({ { AutoTarget::vis, 3, 0 }, (float) settings.style });
    seen = settings;
    for (auto& [t, raw] : moved) ctx.paramTouched (AppContext::visSlot, t, raw);
}

bool Visualiser::probeLane (const Hit& h, juce::Point<float> at)
{
    if (! h.setting || (h.drag == nullptr && h.click == nullptr)) return false;
    // Run the control on marked values, see which one it wrote, then put everything back
    const auto before = settings;
    const auto lastBefore = last;
    constexpr float mark = -7.0f;
    for (auto& x : settings.v) x = mark;
    for (auto& x : settings.w) x = (int) mark;
    for (auto& row : settings.route) for (auto& x : row) x = mark;
    probing = true;
    if (h.click != nullptr) h.click();
    if (h.drag != nullptr) h.drag (at.x, at.y);
    probing = false;
    AutoTarget found;
    for (int i = 0; i < F::count && ! found.valid(); ++i) if (! juce::approximatelyEqual (settings.v[(size_t) i], mark)) found = { AutoTarget::vis, 0, i };
    for (int i = 0; i < C::count && ! found.valid(); ++i) if (settings.w[(size_t) i] != (int) mark) found = { AutoTarget::vis, 1, i };
    for (int s = 0; s < numSources && ! found.valid(); ++s)
        for (int d = 0; d < numDests && ! found.valid(); ++d)
            if (! juce::approximatelyEqual (settings.route[(size_t) s][(size_t) d], mark)) found = { AutoTarget::vis, 2, s * numDests + d };
    if (! found.valid() && settings.style != before.style) found = { AutoTarget::vis, 3, 0 };
    settings = before;
    last = lastBefore;
    cell = {};
    {
        const juce::SpinLock::ScopedLockType lock (sharedLock);
        shared = settings;
    }
    repaint();
    if (! found.valid()) return false;
    ctx.showLane (AppContext::visSlot, found);
    return true;
}
