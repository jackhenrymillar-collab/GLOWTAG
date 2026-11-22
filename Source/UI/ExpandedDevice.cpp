#include "ExpandedDevice.h"
#include "../Theme.h"
#include "../Engine/DeviceParams.h"

using namespace Neon;
using juce::Colour;
using juce::String;

namespace
{
    constexpr float stripHeight = 46.0f, footerHeight = 26.0f;
    const char* keymap = "awsedftgyhujkolp";
}

ExpandedDevice::ExpandedDevice (AppContext& c) : ctx (c)
{
    setWantsKeyboardFocus (true);
    chrome.colours = [this] (int) { return chromeColours; };
}

ExpandedDevice::~ExpandedDevice() { stopTimer(); }

juce::var ExpandedDevice::lookToVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("style", look().style);
    o->setProperty ("glow", look().glow);
    return juce::var (o);
}

void ExpandedDevice::lookFromVar (const juce::var& v)
{
    if (! v.isObject()) return;
    look().style = juce::jlimit (0, 2, (int) v.getProperty ("style", 0));
    look().glow = juce::jlimit (0.0f, 1.5f, (float) v.getProperty ("glow", 1.0f));
}

void ExpandedDevice::open (int track, int moduleId)
{
    device = std::make_unique<DeviceViews::Device> (DeviceViews::Device { ctx, track, moduleId, {}, {} });
    device->patchChanged = [this] { if (onPatchChanged && device) onPatchChanged (device->track); rebuild(); };
    device->loadAudio = [this] { loadAudio(); };
    generation = ctx.generation;
    if (ctx.selectedTrack != track) ctx.selectTrack (track);
    rebuild();
    setVisible (true);
    toFront (true);
    grabKeyboardFocus();
    lastFrame = now();
    startTimerHz (30);
}

void ExpandedDevice::close()
{
    if (! isOpen()) return;
    stopTimer();
    for (auto& [key, note] : heldKeys) device->note (note, false);
    heldKeys.clear();
    if (dragging != nullptr) dragging->mouseUp();
    dragging = nullptr;
    built.reset();
    device.reset();
    setVisible (false);
    if (onClose) onClose();
}

void ExpandedDevice::rebuild()
{
    if (device == nullptr || device->module() == nullptr) { close(); return; }
    dragging = nullptr;
    built = DeviceViews::build (*device);
    buildChrome();
    repaint();
}

void ExpandedDevice::step (int direction)
{
    if (device == nullptr) return;
    const auto& mods = ctx.project.slot (device->track).patch.modules;
    std::vector<int> ids;
    for (auto& m : mods) if (DeviceViews::hasView (m.type)) ids.push_back (m.id);
    if (ids.size() < 2) return;
    auto it = std::find (ids.begin(), ids.end(), device->moduleId);
    const int at = it == ids.end() ? 0 : (int) (it - ids.begin());
    const int next = ids[(size_t) (((at + direction) % (int) ids.size() + (int) ids.size()) % (int) ids.size())];
    for (auto& [key, note] : heldKeys) device->note (note, false);
    heldKeys.clear();
    device->moduleId = next;
    rebuild();
}

void ExpandedDevice::loadAudio()
{
    chooser = std::make_unique<juce::FileChooser> ("Load a sample", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    juce::Component::SafePointer<ExpandedDevice> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe] (const juce::FileChooser& fc)
    {
        if (safe == nullptr || safe->device == nullptr) return;
        const auto f = fc.getResult();
        if (! f.existsAsFile()) return;
        if (auto* m = safe->device->module())
        {
            m->sampleName = f.getFullPathName();
            // Old markers belong to the old sample
            for (int i = 0; i < Param::Sampler::maxMarkers; ++i) m->knobValues[(size_t) (Param::Sampler::Marker0 + i)] = -1.0f;
            safe->ctx.touched();
            if (safe->device->patchChanged) safe->device->patchChanged();
        }
    });
}

//==============================================================================
// The strip along the top: devices on this track, style, colour, glow, close

void ExpandedDevice::buildChrome()
{
    chrome.groups.clear();
    if (device == nullptr || built == nullptr) return;
    auto& g = chrome.add (0, 0, 0, 0, 0, 0, 0);
    const float y = 30, w = (float) getWidth();
    auto* self = this;

    auto& prev = g.toggle ("<  prev", 20, y, [] { return true; }, [self] (bool) { self->step (-1); });
    prev.size = 17; prev.tip = [] { return String ("the device before   ["); };
    auto& next = g.toggle ("next  >", 110, y, [] { return true; }, [self] (bool) { self->step (1); });
    next.size = 17; next.tip = [] { return String ("the next device   ]"); };

    // Right side, laid out from the right edge
    float x = w - 20;
    auto place = [&x] (float width) { x -= width; return x; };
    const float ex = place (40);
    auto& esc = g.toggle ("esc", ex, y, [] { return true; }, [self] (bool) { self->close(); });
    esc.size = 17; esc.tip = [] { return String ("close the wall"); };
    place (28);
    const float gx = place (110);
    auto& glow = g.fader (gx, y - 6, gx + 110, y - 6);
    glow.text = "glow"; glow.def = 1.0f / 1.5f;
    glow.get = [] { return look().glow / 1.5f; };
    glow.set = [self] (float v) { look().glow = v * 1.5f; if (self->onLookChanged) self->onLookChanged(); };
    glow.tip = [] { return "glow   " + String (juce::roundToInt (look().glow * 100)) + "%"; };
    place (34);
    const float rx = place (80);
    auto& rb = g.toggle ("rainbow", rx, y, [] { return Theme::get().isRainbow(); }, [] (bool v) { Theme::get().setRainbow (v); });
    rb.size = 17;
    const auto& themes = Theme::get().choices();
    place (16);
    for (int i = themes.size() - 1; i >= 0; --i)
    {
        const float bx = place (22);
        auto& b = g.counter (1, bx, y - 6);
        b.getChoice = [i] { return ! Theme::get().isRainbow() && Theme::get().getThemeIndex() == i ? 0 : -1; };
        b.setChoice = [i] (int) { Theme::get().setRainbow (false); Theme::get().setThemeIndex (i); };
        // Each swatch draws in its own colour, so the strip paints and hits these itself
        b.ci = 100 + i;
        b.visible = [] { return false; };
    }
    place (24);
    const float sx = place (230);
    auto& styles = g.words ({ { 0, "tubes" }, { 1, "marquee" }, { 2, "block" } }, sx, y);
    styles.size = 17; styles.gap = 16;
    styles.getChoice = [] { return look().style; };
    styles.setChoice = [self] (int v) { look().style = v; if (self->onLookChanged) self->onLookChanged(); };
    styles.tip = [] { return String ("how the glass is made"); };
}

//==============================================================================

juce::Point<float> ExpandedDevice::toWall (juce::Point<float> p) const
{
    return { (p.x - wallArea.getX()) / scale, (p.y - wallArea.getY()) / scale };
}

void ExpandedDevice::resized()
{
    const auto area = getLocalBounds().toFloat().withTrimmedTop (stripHeight).withTrimmedBottom (footerHeight);
    scale = juce::jmin (area.getWidth() / Wall::width, area.getHeight() / Wall::height);
    wallArea = juce::Rectangle<float> (Wall::width * scale, Wall::height * scale).withCentre (area.getCentre());
    bricks = {};
    buildChrome();
}

void ExpandedDevice::timerCallback()
{
    if (device == nullptr || device->module() == nullptr || ctx.generation != generation) { close(); return; }
    repaint();
}

void ExpandedDevice::paint (juce::Graphics& g)
{
    if (built == nullptr) return;
    const double t = now();
    const float dt = (float) juce::jlimit (0.0, 0.1, t - lastFrame);
    lastFrame = t;

    // The bricks never change: keep them in an image, at the screen's own resolution
    float ds = 1.0f;
    if (auto* top = getTopLevelComponent(); top != nullptr && top->getPeer() != nullptr)
        if (auto* d = juce::Desktop::getInstance().getDisplays().getDisplayForRect (top->getScreenBounds()))
            ds = juce::jlimit (1.0f, 3.0f, (float) d->scale);
    const int bw = juce::jmax (1, juce::roundToInt ((float) getWidth() * ds)), bh = juce::jmax (1, juce::roundToInt ((float) getHeight() * ds));
    if (bricks.isNull() || bricks.getWidth() != bw || bricks.getHeight() != bh)
    {
        bricks = juce::Image (juce::Image::RGB, bw, bh, false);
        juce::Graphics bg (bricks);
        bg.addTransform (juce::AffineTransform::scale (ds));
        paintBricks (bg, getLocalBounds().toFloat());
    }
    g.drawImage (bricks, getLocalBounds().toFloat());

    // Soft light from the last frame's glass, blurred, under this frame's glass
    glowLayer.draw (g, getLocalBounds().toFloat());
    auto& gl = glow();
    gl.sources.clear();
    gl.collecting = true;
    const auto toView = juce::AffineTransform::scale (scale).translated (wallArea.getX(), wallArea.getY());
    gl.base = gl.current = toView;
    gl.alpha = 1.0f;

    // The strip's colours follow the theme
    const auto* c = built->colours (0);
    for (int i = 0; i < 3; ++i) chromeColours[i] = c[i];

    {
        juce::Graphics::ScopedSaveState s (g);
        g.addTransform (juce::AffineTransform::scale (scale).translated (wallArea.getX(), wallArea.getY()));
        built->wall->paintPools (g);
        built->wall->paint (g, t, dt);
    }
    gl.base = gl.current = {};
    gl.alpha = 1.0f;

    // Strip and footer
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRect (0.0f, 0.0f, (float) getWidth(), stripHeight);
    {
        // Theme swatches draw in their own colour
        const auto& themes = Theme::get().choices();
        for (auto& gr : chrome.groups)
            for (auto& it : gr->items)
                if (it->ci >= 100)
                {
                    const int i = it->ci - 100;
                    const bool on = it->getChoice() == 0;
                    bulb (g, it->x, it->y, themes[i].colour, on ? 1.0f : 0.35f, on ? 6.5f : 5.0f);
                }
    }
    chrome.paint (g, t, dt);
    text (g, built->name.toLowerCase(), 220, 30, 19, c[1], 0.85f);
    const auto& slotName = ctx.project.slot (device->track).name;
    label (g, "on " + slotName.toLowerCase(), 220 + textWidth (built->name.toLowerCase(), 19) + 14, 30);

    label (g, "drag the glass   double-click a fader to reset it   shift drags finely   keys a to p play notes   esc closes",
           (float) getWidth() / 2, (float) getHeight() - 9, Align::centre, Colour (0xff6c6e75), 13);

    gl.collecting = false;
    glowLayer.build (getWidth(), getHeight(), scale);
}

//==============================================================================
// Pointer

Wall* ExpandedDevice::wallAt (juce::Point<float> p, juce::Point<float>& where)
{
    if (p.y < stripHeight) { where = p; return &chrome; }
    where = toWall (p);
    return built != nullptr ? built->wall.get() : nullptr;
}

void ExpandedDevice::mouseDown (const juce::MouseEvent& e)
{
    juce::Point<float> at;
    auto* w = wallAt (e.position, at);
    if (w == nullptr) return;
    // The theme swatches are counters with one bulb: hit them here
    if (w == &chrome)
        for (auto& gr : chrome.groups)
            for (auto& it : gr->items)
                if (it->ci >= 100 && juce::Point<float> (it->x, it->y).getDistanceFrom (at) < 11.0f) { it->setChoice (0); repaint(); return; }
    if (w->mouseDown (at)) dragging = w;
    repaint();
}

void ExpandedDevice::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging == nullptr) return;
    dragging->mouseDrag (dragging == &chrome ? e.position : toWall (e.position), e.mods.isShiftDown());
}

void ExpandedDevice::mouseUp (const juce::MouseEvent&)
{
    if (dragging != nullptr) dragging->mouseUp();
    dragging = nullptr;
}

void ExpandedDevice::mouseMove (const juce::MouseEvent& e)
{
    juce::Point<float> at;
    auto* w = wallAt (e.position, at);
    if (w == nullptr) return;
    if (w == &chrome && built != nullptr) built->wall->mouseExit();
    else chrome.mouseExit();
    setMouseCursor (juce::MouseCursor (w->mouseMove (at)));
}

void ExpandedDevice::mouseExit (const juce::MouseEvent&)
{
    chrome.mouseExit();
    if (built != nullptr) built->wall->mouseExit();
}

void ExpandedDevice::mouseDoubleClick (const juce::MouseEvent& e)
{
    juce::Point<float> at;
    if (auto* w = wallAt (e.position, at)) w->doubleClick (at);
}

void ExpandedDevice::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    juce::Point<float> at;
    if (auto* w = wallAt (e.position, at)) w->wheel (at, wheel.deltaY);
}

//==============================================================================
// Keys: Esc closes, [ and ] step through devices. On the Drum Rack and a slicing Sampler the
// letter keys play pads from C2; elsewhere the window's own keyboard plays notes.

bool ExpandedDevice::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey) { close(); return true; }
    if (key.getTextCharacter() == '[') { step (-1); return true; }
    if (key.getTextCharacter() == ']') { step (1); return true; }
    if (device == nullptr || key.getModifiers().isCommandDown()) return false;
    const auto type = device->type();
    const bool pads = type == ModuleType::Drums || (type == ModuleType::Sampler && device->choice (Param::Sampler::Mode) == Param::Sampler::Slice);
    if (! pads) return false;
    const auto ch = (juce::juce_wchar) juce::CharacterFunctions::toLowerCase (key.getTextCharacter());
    for (int i = 0; i < 16; ++i)
        if (keymap[i] == ch)
        {
            const int code = key.getKeyCode();
            if (heldKeys.count (code) == 0)
            {
                heldKeys[code] = 36 + i;
                if (type == ModuleType::Drums) device->setRaw (Param::Drums::Selected, (float) i);
                device->note (36 + i, true, 0.9f);
            }
            return true;
        }
    return false;
}

bool ExpandedDevice::keyStateChanged (bool)
{
    bool used = false;
    for (auto it = heldKeys.begin(); it != heldKeys.end();)
    {
        if (! juce::KeyPress::isKeyCurrentlyDown (it->first))
        {
            if (device != nullptr) device->note (it->second, false);
            it = heldKeys.erase (it);
            used = true;
        }
        else ++it;
    }
    return used;
}
