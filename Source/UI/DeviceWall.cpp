#include "DeviceWall.h"
#include "Icons.h"
#include "NeonKit.h"

namespace
{
    using juce::AffineTransform;
    using juce::Colour;

    constexpr int rowHeight = 36;
    constexpr int sidePad = 16;
    constexpr float growBy = 0.09f;          // how much a piece grows under the mouse in Always bright

    juce::String dotted (const char* numeral, const char* name)
    {
        return juce::String (numeral) + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + name;
    }

    const char* numerals[Wall::numStyles] = { "I", "II", "III" };

    // Opacity of a tube as it strikes: a couple of stutters, then steady
    float curve (float t, const float (*points)[2], int count)
    {
        if (t >= 1.0f) return 1.0f;
        for (int i = 1; i < count; ++i)
            if (t <= points[i][0])
            {
                const float span = points[i][0] - points[i - 1][0];
                return juce::jmap (span > 0.0f ? (t - points[i - 1][0]) / span : 1.0f, points[i - 1][1], points[i][1]);
            }
        return 1.0f;
    }

    float strikeOpacity (float t)
    {
        static const float points[][2] = { { 0.0f, 0.2f }, { 0.12f, 1.0f }, { 0.22f, 0.3f }, { 0.34f, 1.0f }, { 0.46f, 0.6f }, { 1.0f, 1.0f } };
        return curve (t, points, 6);
    }

    float flickerOpacity (float t)
    {
        static const float points[][2] = { { 0.0f, 0.15f }, { 0.18f, 1.0f }, { 0.30f, 0.3f }, { 0.52f, 1.0f }, { 0.64f, 0.5f }, { 0.80f, 1.0f }, { 1.0f, 1.0f } };
        return curve (t, points, 7);
    }

    float unitOf (const AffineTransform& t)
    {
        return std::sqrt (std::abs (t.mat00 * t.mat11 - t.mat01 * t.mat10));
    }

    void drawImage (juce::Graphics& g, const juce::Image& image, const AffineTransform& t, float opacity)
    {
        if (! image.isValid() || opacity <= 0.003f)
            return;
        g.setOpacity (juce::jmin (1.0f, opacity));
        g.drawImageTransformed (image, t);
        if (opacity > 1.0f)       // glow above 100%: lay it on again
        {
            g.setOpacity (juce::jmin (1.0f, opacity - 1.0f));
            g.drawImageTransformed (image, t);
        }
    }
}

DeviceWall::DeviceWall()
{
    setInterceptsMouseClicks (true, true);
    setMouseClickGrabsKeyboardFocus (false);

    auto setUp = [this] (juce::TextButton& b, Pal pal)
    {
        b.getProperties().set ("pal", (int) pal);
        b.setWantsKeyboardFocus (false);
        b.setMouseClickGrabsKeyboardFocus (false);
        addAndMakeVisible (b);
    };

    for (int s = 0; s < Wall::numStyles; ++s)
    {
        styleButtons[s].setButtonText (dotted (numerals[s], Wall::styleName (s)));
        styleButtons[s].setTooltip (juce::String ("Use style ") + numerals[s] + " for every device");
        styleButtons[s].onClick = [this, s] { setAllStyles (s); };
        setUp (styleButtons[s], Pal::Green);

        pieceStyleButtons[s].setButtonText (numerals[s]);
        pieceStyleButtons[s].setTooltip (Wall::styleName (s));
        pieceStyleButtons[s].onClick = [this, s]
        {
            model.pieces[(size_t) selected].style = s;
            changed();
        };
        setUp (pieceStyleButtons[s], Pal::Green);
        pieceStyleButtons[s].setVisible (false);
    }

    rainbowButton.onClick = [] { Theme::get().setRainbow (! Theme::get().isRainbow()); };
    zoomOutButton.onClick = [this] { zoomBy (1.0f / 1.2f); };
    zoomInButton.onClick = [this] { zoomBy (1.2f); };
    zoomButton.onClick = [this] { setZoom (1.0f); };
    fitButton.onClick = [this] { setFit (! model.fitToScreen); };
    brightButton.onClick = [this] { setAlwaysBright (! model.alwaysBright); };
    editButton.onClick = [this] { setEditing (! editing); };
    doneButton.onClick = [this] { setEditing (false); };
    resetButton.onClick = [this]
    {
        if (! resetArmed)
        {
            resetArmed = true;
            resetButton.setButtonText ("Really reset?");
            return;
        }
        model.resetLayout();
        resetArmed = false;
        resetButton.setButtonText ("Reset wall");
        showToast ("Wall reset to the default layout");
        changed();
    };

    zoomOutButton.setTooltip ("Zoom out (-)");
    zoomInButton.setTooltip ("Zoom in (+)");
    zoomButton.setTooltip ("Back to 100%");
    fitButton.setTooltip ("Show the whole wall on one screen (0)");
    brightButton.setTooltip ("Keep every design lit at full brightness");
    editButton.setTooltip ("Move, resize, retitle and restyle the pieces");

    setUp (rainbowButton, Pal::Gold);
    for (auto* b : { &zoomOutButton, &zoomInButton, &zoomButton, &fitButton, &brightButton, &editButton, &resetButton, &doneButton })
        setUp (*b, Pal::Green);
    resetButton.setVisible (false);
    doneButton.setVisible (false);

    glowSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    glowSlider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    glowSlider.setRange (0.0, 150.0, 5.0);
    glowSlider.setValue (100.0, juce::dontSendNotification);
    glowSlider.setDoubleClickReturnValue (true, 100.0);
    glowSlider.setWantsKeyboardFocus (false);
    glowSlider.setMouseClickGrabsKeyboardFocus (false);
    glowSlider.setTooltip ("How strongly the tubes glow");
    glowSlider.onValueChange = [this]
    {
        model.glow = (float) glowSlider.getValue() / 100.0f;
        if (onClosed) onClosed();      // saved with the rest
        repaint();
    };
    addAndMakeVisible (glowSlider);

    titleEditor.setInputRestrictions (18);
    titleEditor.setSelectAllWhenFocused (true);
    titleEditor.setJustification (juce::Justification::centredLeft);
    titleEditor.setTooltip ("This piece's title");
    titleEditor.onTextChange = [this]
    {
        const auto text = titleEditor.getText().trim();
        auto& p = model.pieces[(size_t) selected];
        p.title = text == Wall::device (selected).name ? juce::String() : text;
        if (onClosed) onClosed();
        repaint();
    };
    auto finishTitle = [this] { if (onReleaseFocus) onReleaseFocus(); else titleEditor.giveAwayKeyboardFocus(); };
    titleEditor.onReturnKey = finishTitle;
    titleEditor.onEscapeKey = finishTitle;
    addChildComponent (titleEditor);

    themeChanged();
    refreshControls();
}

float DeviceWall::displayScale() const
{
    float s = 1.0f;
    if (auto* top = getTopLevelComponent(); top != nullptr && top->getPeer() != nullptr)
        if (auto* d = juce::Desktop::getInstance().getDisplays().getDisplayForRect (top->getScreenBounds()))
            s = (float) d->scale;
    return juce::jlimit (1.0f, 2.0f, s);
}

//==============================================================================
// Opening and closing

void DeviceWall::tabPressed()
{
    if (tabDown)
        return;                       // key repeat while held
    tabDown = true;

    if (! isOpen())
    {
        open (false);
        openedByThisPress = true;
        tabDownAt = juce::Time::getMillisecondCounter();
    }
    else
    {
        openedByThisPress = false;
        if (! editing)
            close();
    }
}

void DeviceWall::tabReleased()
{
    if (! tabDown)
        return;
    tabDown = false;

    if (openedByThisPress && isOpen())
    {
        // A quick tap leaves the wall up; a hold shows it only while held. If drawing the wall kept the
        // key-up waiting, that time is not part of the hold.
        const auto now = juce::Time::getMillisecondCounter();
        float held = (float) (now - tabDownAt);
        if (now - lastPaintEnd < 40 && lastPaintMs > 50.0f)
            held -= lastPaintMs;
        if (held < 260.0f)
            latched = true;
        else if (! editing && ! latched)
            close();
    }
    openedByThisPress = false;
}

void DeviceWall::open (bool stayOpen)
{
    latched = stayOpen;
    if (query.isNotEmpty())
    {
        query.clear();
        invalidate();
    }
    struckAt = juce::Time::getMillisecondCounter();
    nextFlickerAt = struckAt + 2600;
    lastDirty.clear();
    setVisible (true);
    toFront (false);
    refreshControls();
    layoutView();
    repaint();
}

void DeviceWall::close()
{
    if (titleEditor.hasKeyboardFocus (false) && onReleaseFocus)
        onReleaseFocus();
    setEditing (false);
    latched = false;
    drag = Drag::none;
    toast.clear();
    if (query.isNotEmpty())
    {
        query.clear();
        invalidate();       // so it can be redrawn, while closed, the way it will next open
    }
    setVisible (false);
    if (onClosed) onClosed();
}

void DeviceWall::visibilityChanged()
{
    if (isVisible())
    {
        refreshControls();
        placeTools();
    }
}

void DeviceWall::invalidate()
{
    poolsDirty = true;
    frameDirty = true;
    beatDirty = true;
}

void DeviceWall::restoreState (const juce::String& json)
{
    if (json.isNotEmpty())
        model.fromJson (json);
    glowSlider.setValue (model.glow * 100.0f, juce::dontSendNotification);
    invalidate();
    refreshControls();
    layoutView();
}

void DeviceWall::themeChanged()
{
    auto& th = Theme::get();
    glowSlider.setColour (juce::Slider::backgroundColourId, th.line2);
    glowSlider.setColour (juce::Slider::trackColourId, th.themeColour().withAlpha (0.8f));
    glowSlider.setColour (juce::Slider::thumbColourId, th.themeColour());
    titleEditor.setColour (juce::TextEditor::backgroundColourId, th.panel);
    titleEditor.setColour (juce::TextEditor::outlineColourId, th.themeColour());
    titleEditor.setColour (juce::TextEditor::focusedOutlineColourId, th.themeColour());
    titleEditor.setColour (juce::TextEditor::textColourId, th.text);
    titleEditor.setColour (juce::TextEditor::highlightColourId, th.themeColour().withAlpha (0.3f));
    titleEditor.applyFontToAllText (th.label (13.0f));
    titleEditor.setFont (th.label (13.0f));
    invalidate();
    refreshControls();
    repaint();
}

void DeviceWall::changed()
{
    invalidate();
    refreshControls();
    layoutView();
    repaint();
    if (onClosed) onClosed();
}

void DeviceWall::refreshControls()
{
    const int common = model.commonStyle();
    for (int s = 0; s < Wall::numStyles; ++s)
    {
        styleButtons[s].setToggleState (common == s, juce::dontSendNotification);
        pieceStyleButtons[s].setToggleState (model.pieces[(size_t) selected].style == s, juce::dontSendNotification);
    }
    rainbowButton.setToggleState (Theme::get().isRainbow(), juce::dontSendNotification);
    fitButton.setToggleState (model.fitToScreen, juce::dontSendNotification);
    brightButton.setToggleState (model.alwaysBright, juce::dontSendNotification);
    editButton.setToggleState (editing, juce::dontSendNotification);
    doneButton.setToggleState (true, juce::dontSendNotification);
    resetButton.setVisible (editing);
    doneButton.setVisible (editing);
}

//==============================================================================
// View

void DeviceWall::resized()
{
    auto r = getLocalBounds();
    if (r.isEmpty())
        return;
    const auto oldView = viewArea;

    // Top bar: one row when it fits, otherwise it wraps onto a second
    struct Item { juce::Component* c; int width; juce::Rectangle<int>* area; };
    headingArea = styleLabelArea = zoomLabelArea = glowLabelArea = glowValueArea = {};

    const Item items[] = {
        { nullptr, 118, &headingArea }, { nullptr, 150, &findArea }, { nullptr, -1, nullptr },      // -1: stretch
        { nullptr, 44, &styleLabelArea }, { &styleButtons[0], 84, nullptr }, { &styleButtons[1], 102, nullptr }, { &styleButtons[2], 88, nullptr },
        { nullptr, Theme::get().choices().size() * 22 + 6, &swatchArea }, { &rainbowButton, 88, nullptr },
        { nullptr, 42, &zoomLabelArea }, { &zoomOutButton, 30, nullptr }, { &zoomButton, 52, nullptr }, { &zoomInButton, 30, nullptr }, { &fitButton, 46, nullptr },
        { nullptr, 40, &glowLabelArea }, { &glowSlider, 96, nullptr }, { nullptr, 40, &glowValueArea },
        { &brightButton, 120, nullptr }, { &editButton, 92, nullptr } };

    const int left = sidePad, right = r.getWidth() - sidePad, gap = 8;
    int total = 0;
    for (auto& it : items) total += juce::jmax (0, it.width) + gap;
    const bool oneRow = total <= right - left;

    int x = left, y = 8, rows = 1;
    for (auto& it : items)
    {
        if (it.width < 0)
        {
            if (oneRow) x += (right - left) - total;       // push the controls to the right
            else { x = left; y += rowHeight; ++rows; }
            continue;
        }
        if (x + it.width > right && x > left) { x = left; y += rowHeight; ++rows; }
        const juce::Rectangle<int> area (x, y + 4, it.width, rowHeight - 8);
        if (it.c != nullptr) it.c->setBounds (area);
        if (it.area != nullptr) *it.area = area;
        x += it.width + gap;
    }

    barArea = r.removeFromTop (8 + rows * rowHeight + 6);
    footArea = r.removeFromBottom (40);
    editArea = editing ? r.removeFromTop (38) : juce::Rectangle<int>();
    viewArea = r;

    if (editing)
    {
        auto e = editArea.reduced (sidePad, 5);
        doneButton.setBounds (e.removeFromRight (80));
        e.removeFromRight (8);
        resetButton.setBounds (e.removeFromRight (130));
    }

    if (viewArea != oldView || wallImage.getWidth() != juce::roundToInt ((float) getWidth() * displayScale()))
    {
        wallImage = {};
        poolImage = {};
        invalidate();
    }
    layoutView();
}

void DeviceWall::layoutView()
{
    if (viewArea.isEmpty())
        return;
    const float viewW = (float) viewArea.getWidth() - (float) sidePad * 2.0f, viewH = (float) viewArea.getHeight();
    const float base = viewW / Wall::designWidth;
    const float content = model.contentHeight();
    const float topPad = editing ? 46.0f : 14.0f;      // room above the top row for a piece's title box
    const float oldScale = scale;
    const auto oldOrigin = origin;

    scale = model.fitToScreen ? juce::jmin (base, juce::jmax (0.08f, (viewH - topPad - 16.0f) / content))
                              : base * model.zoom;

    const float maxX = juce::jmax (0.0f, Wall::designWidth * scale - viewW);
    const float maxY = juce::jmax (0.0f, content * scale + topPad + 16.0f - viewH);
    scroll.x = juce::jlimit (0.0f, maxX, scroll.x);
    scroll.y = juce::jlimit (0.0f, maxY, scroll.y);

    origin.x = std::round ((float) viewArea.getX() + (float) sidePad + juce::jmax (0.0f, (viewW - Wall::designWidth * scale) * 0.5f) - scroll.x);
    origin.y = std::round ((float) viewArea.getY() + topPad - scroll.y);

    zoomButton.setButtonText (juce::String (juce::roundToInt (scale / base * 100.0f)) + "%");
    if (! juce::approximatelyEqual (scale, oldScale) || origin != oldOrigin)
        invalidate();
    placeTools();
}

void DeviceWall::setZoom (float z)
{
    model.fitToScreen = false;
    model.zoom = juce::jlimit (0.3f, 2.0f, std::round (z * 20.0f) / 20.0f);
    changed();
}

void DeviceWall::zoomBy (float factor)
{
    const float base = ((float) viewArea.getWidth() - (float) sidePad * 2.0f) / Wall::designWidth;
    setZoom ((base > 0.0f ? scale / base : model.zoom) * factor);
}

void DeviceWall::setFit (bool fit)
{
    model.fitToScreen = fit;
    if (! fit) model.zoom = 1.0f;
    scroll = {};
    changed();
}

void DeviceWall::setEditing (bool e)
{
    if (editing == e)
        return;
    if (! e && titleEditor.hasKeyboardFocus (false) && onReleaseFocus)
        onReleaseFocus();
    editing = e;
    if (e) latched = true;
    resetArmed = false;
    resetButton.setButtonText ("Reset wall");
    drag = Drag::none;
    invalidate();
    refreshControls();
    resized();
    repaint();
}

void DeviceWall::setAlwaysBright (bool b)
{
    model.alwaysBright = b;
    changed();
}

void DeviceWall::setAllStyles (int style)
{
    for (auto& p : model.pieces)
        p.style = juce::jlimit (0, (int) Wall::numStyles - 1, style);
    changed();
}

void DeviceWall::select (int index)
{
    index = juce::jlimit (0, Wall::numDevices - 1, index);
    if (index == selected)
        return;
    selected = index;
    struckAt = juce::Time::getMillisecondCounter();
    // With one piece lit, only its light moves; with them all lit, nothing at rest changes
    if (editing) invalidate();
    else if (! model.alwaysBright) poolsDirty = true;
    refreshControls();
    placeTools();
    repaint();
}

void DeviceWall::setQuery (const juce::String& q)
{
    query = q.substring (0, 18);
    if (! model.matches (selected, query))
        if (const int first = model.firstMatch (query); first >= 0)
            selected = first;
    struckAt = juce::Time::getMillisecondCounter();
    invalidate();
    repaint();
}

void DeviceWall::showToast (const juce::String& text)
{
    toast = text;
    toastAt = juce::Time::getMillisecondCounter();
    repaint();
}

void DeviceWall::load (int index)
{
    if (! model.matches (index, query))
        return;
    juce::String where = targetName ? targetName() : juce::String();
    if (onLoad) onLoad (index);
    const auto& d = Wall::device (index);
    const bool fixedModule = d.sequencer && (d.seq == SeqType::Clock || d.seq == SeqType::Out);
    showToast (model.titleOf (index) + (fixedModule ? " is in every rack: opened " : d.sequencer ? " added to the rack on " : " loaded on ")
               + (where.isNotEmpty() ? where : juce::String ("the selected track")) + (fixedModule ? "'s rack" : ""));
}

bool DeviceWall::isLit (int index) const
{
    return model.matches (index, query) && (editing || model.alwaysBright || index == selected);
}

juce::Rectangle<float> DeviceWall::boxOnScreen (int index) const
{
    const auto& p = model.pieces[(size_t) index];
    return { origin.x + p.x * scale, origin.y + p.y * scale, p.w * scale, p.w * Wall::aspect * scale };
}

juce::Rectangle<float> DeviceWall::gripFor (int index) const
{
    const auto b = boxOnScreen (index);
    return juce::Rectangle<float> (16.0f, 16.0f).withCentre (b.getBottomRight());
}

int DeviceWall::pieceAt (juce::Point<float> pos) const
{
    if (! viewArea.toFloat().contains (pos))
        return -1;
    if (model.matches (selected, query) && boxOnScreen (selected).contains (pos))
        return selected;
    for (int i = Wall::numDevices; --i >= 0;)
        if (model.matches (i, query) && boxOnScreen (i).contains (pos))
            return i;
    return -1;
}

AffineTransform DeviceWall::pieceToScreen (int index, float growth) const
{
    const auto& p = model.pieces[(size_t) index];
    const float cx = p.w * 0.5f, cy = p.w * Wall::aspect * 0.5f, g = 1.0f + growBy * growth;
    return AffineTransform::rotation (juce::degreesToRadians (p.rotation), cx, cy)
               .scaled (g, g, cx, cy)
               .scaled (scale)
               .translated (origin.x + p.x * scale, origin.y + p.y * scale);
}

void DeviceWall::placeTools()
{
    bool show = editing && isOpen();
    juce::Rectangle<float> box;
    if (show)
    {
        box = boxOnScreen (selected);
        show = box.intersects (viewArea.toFloat());
    }

    titleEditor.setVisible (show);
    for (auto& b : pieceStyleButtons)
        b.setVisible (show);
    if (! show)
        return;

    const int width = 150 + 3 * 36;
    int x = juce::jlimit (viewArea.getX() + 4, juce::jmax (viewArea.getX() + 4, viewArea.getRight() - width - 4), juce::roundToInt (box.getX()));
    int y = juce::roundToInt (box.getY()) - 32;
    if (y < viewArea.getY() + 2)
        y = juce::roundToInt (box.getY()) + 6;

    titleEditor.setBounds (x, y, 150, 26);
    for (int s = 0; s < Wall::numStyles; ++s)
        pieceStyleButtons[s].setBounds (x + 156 + s * 36, y, 32, 26);
    if (! titleEditor.hasKeyboardFocus (false))
        titleEditor.setText (model.titleOf (selected), juce::dontSendNotification);
}

//==============================================================================
// Painting

float DeviceWall::marginOf (int index) const
{
    return std::ceil ((0.22f * model.pieces[(size_t) index].w + 34.0f) * scale);
}

juce::Rectangle<float> DeviceWall::pieceBounds (int index, float growth) const
{
    const auto box = boxOnScreen (index);
    const float gs = 1.0f + growBy * growth;
    return box.expanded (marginOf (index) + 1.0f).transformedBy (AffineTransform::scale (gs, gs, box.getCentreX(), box.getCentreY()));
}

float DeviceWall::needleAngle (bool lit) const
{
    return juce::degreesToRadians (-38.0f + (lit ? pulse * 56.0f : 0.0f));
}

juce::Rectangle<float> DeviceWall::liveBounds (int index) const
{
    const auto& p = model.pieces[(size_t) index];
    const auto artTo = WallArt::artTransform (p.style, p.w).followedBy (pieceToScreen (index, grow[(size_t) index]));
    return WallArt::liveBounds (WallArt::prepared (index, p.style), artTo, unitOf (artTo), (float) spin, needleAngle (true));
}

juce::Rectangle<float> DeviceWall::poolReach (int index) const
{
    const auto box = boxOnScreen (index);
    const float rx = box.getWidth() * 0.86f, ry = box.getHeight() * 0.92f;
    return { box.getCentreX() - rx, box.getCentreY() - ry, rx * 2.0f, ry * 2.0f };
}

// Cached images sit on whole device pixels so they stay sharp; a growing piece is scaled about its centre
AffineTransform DeviceWall::imagePlacement (int index, float growth, float imageScale) const
{
    const auto box = boxOnScreen (index);
    const float ds = displayScale(), margin = marginOf (index);
    const float tx = std::round ((box.getX() - margin) * ds) / ds, ty = std::round ((box.getY() - margin) * ds) / ds;
    const float gs = 1.0f + growBy * growth;
    return AffineTransform::scale (1.0f / (ds * imageScale)).translated (tx, ty).scaled (gs, gs, box.getCentreX(), box.getCentreY());
}

bool DeviceWall::ensureCache (int index, bool lit)
{
    const auto& p = model.pieces[(size_t) index];
    const auto title = model.titleOf (index);
    const auto colours = WallArt::coloursFor (index, p.style);
    const float ds = displayScale();

    juce::String key;
    key << p.w << '|' << p.rotation << '|' << p.style << '|' << title << '|' << colours.c[0].toString() << colours.c[1].toString()
        << colours.c[2].toString() << '|' << scale << '|' << ds;

    auto& c = cache[(size_t) index];
    if (c.key != key)
    {
        c = {};
        c.key = key;
    }
    if ((lit && c.lit.isValid()) || (! lit && c.unlit.isValid()))
        return false;

    const float w = p.w, h = p.w * Wall::aspect;
    c.margin = marginOf (index);
    const float k = scale * ds;
    const int iw = juce::jmax (1, juce::roundToInt ((w * scale + c.margin * 2.0f) * ds)), ih = juce::jmax (1, juce::roundToInt ((h * scale + c.margin * 2.0f) * ds));

    const auto& art = WallArt::prepared (index, p.style);
    const auto letters = WallArt::lettering (p.style, title, w);
    const auto rotate = AffineTransform::rotation (juce::degreesToRadians (p.rotation), w * 0.5f, h * 0.5f);
    const auto artBox = WallArt::artTransform (p.style, w);

    auto render = [&] (juce::Image& target, bool on)
    {
        target = juce::Image (juce::Image::ARGB, iw, ih, true);
        juce::Graphics g (target);
        const auto pieceTo = rotate.scaled (k).translated (c.margin * ds, c.margin * ds);
        const auto artTo = artBox.followedBy (pieceTo);
        WallArt::drawTubes (g, art, colours, artTo, unitOf (artTo), on, WallArt::Part::fixed);
        WallArt::drawLettering (g, letters, p.style, colours, pieceTo, k, on);
    };

    if (! lit)
    {
        render (c.unlit, false);
        return true;
    }

    render (c.lit, true);

    // Glow: the tubes and letters drawn fat, then blurred. Two spreads, a tight halo and a wide bloom.
    // Blur hides detail, so on a Retina screen it is made at half size and stretched; on a plain screen
    // it is kept at full size, where an unstretched picture is far quicker to lay down.
    c.glowScale = 1.0f / ds;
    const float gk = k * c.glowScale;
    const int gw = juce::jmax (1, juce::roundToInt ((float) iw * c.glowScale)), gh = juce::jmax (1, juce::roundToInt ((float) ih * c.glowScale));
    const auto pieceTo = rotate.scaled (gk).translated (c.margin * ds * c.glowScale, c.margin * ds * c.glowScale);
    const auto artTo = artBox.followedBy (pieceTo);
    const float unit = unitOf (artTo);

    juce::Image halo (juce::Image::ARGB, gw, gh, true), bloom (juce::Image::ARGB, gw, gh, true);
    {
        juce::Graphics g (halo);
        WallArt::drawGlowSource (g, art, colours, artTo, unit, 5.0f);
        WallArt::drawLetteringGlowSource (g, letters, p.style, colours, pieceTo, gk);
    }
    {
        juce::Graphics g (bloom);
        WallArt::drawGlowSource (g, art, colours, artTo, unit, 8.0f);
        WallArt::drawLetteringGlowSource (g, letters, p.style, colours, pieceTo, gk);
    }
    WallArt::blur (halo, juce::jmax (2.6f * unit, 3.0f * gk));
    WallArt::blur (bloom, juce::jmax (9.0f * unit, 11.0f * gk));
    {
        juce::Graphics g (halo);
        g.setOpacity (0.5f);
        g.drawImageAt (bloom, 0, 0);
    }
    c.glow = halo;
    return true;
}

// `resting` draws the piece as the kept picture holds it: nothing grown, struck, flickering or turning,
// and lit only where every piece is. Otherwise it is drawn as it stands this instant.
void DeviceWall::paintPiece (juce::Graphics& g, int index, juce::Rectangle<int> area, bool resting)
{
    const float growth = resting ? 0.0f : grow[(size_t) index];
    if (! pieceBounds (index, growth).intersects (area.toFloat()))
        return;

    const auto& p = model.pieces[(size_t) index];
    const bool lit = isLit (index) && (! resting || manyLit());
    const bool chosen = index == selected;
    const float fade = model.matches (index, query) ? 1.0f : 0.12f;
    const auto now = juce::Time::getMillisecondCounter();
    // Where every piece is lit, the beat's extra light is laid over the whole wall afterwards
    const float beat = (resting || manyLit()) ? 0.0f : pulse;

    float strike = 1.0f;
    if (! resting && lit && chosen && ! manyLit())
        strike = strikeOpacity ((float) (now - struckAt) / 500.0f);

    ensureCache (index, lit);
    if (lit && strike < 1.0f)
        ensureCache (index, false);
    const auto& c = cache[(size_t) index];

    if (! lit || strike < 1.0f)
        drawImage (g, c.unlit, imagePlacement (index, growth, 1.0f), fade);

    const auto artTo = WallArt::artTransform (p.style, p.w).followedBy (pieceToScreen (index, growth));
    const auto& art = WallArt::prepared (index, p.style);
    const auto colours = WallArt::coloursFor (index, p.style);

    if (lit)
    {
        const bool soft = editing && ! chosen;      // Edit mode lights everything, the chosen piece fully
        const float amount = (soft ? 0.55f + 0.15f * beat : 0.7f + 0.3f * beat) * model.glow * strike * fade;
        drawImage (g, c.glow, imagePlacement (index, growth, c.glowScale), amount);
        drawImage (g, c.lit, imagePlacement (index, growth, 1.0f), strike * fade);

        if (resting)
        {
            g.setOpacity (1.0f);
            return;                                 // its moving parts are drawn fresh on every repaint
        }
        if (index == flickerPiece)
        {
            const float out = 1.0f - flickerOpacity ((float) (now - flickerAt) / 420.0f);
            g.setOpacity (1.0f);
            WallArt::drawFlicker (g, art, flickerTube, colours, artTo, unitOf (artTo), out * fade);
        }
        if (art.hasLive)
        {
            // The moving parts set their own colours, so a piece faded by the search fades them as a layer
            if (fade < 1.0f) g.beginTransparencyLayer (fade);
            WallArt::drawLiveGlow (g, art, colours, artTo, unitOf (artTo), amount * 1.6f, (float) spin, needleAngle (true));
            WallArt::drawTubes (g, art, colours, artTo, unitOf (artTo), true, WallArt::Part::live, (float) spin, needleAngle (true));
            if (fade < 1.0f) g.endTransparencyLayer();
        }
    }
    else if (art.hasLive)
    {
        if (fade < 1.0f) g.beginTransparencyLayer (fade);
        WallArt::drawTubes (g, art, colours, artTo, unitOf (artTo), false, WallArt::Part::live, 0.0f, needleAngle (false));
        if (fade < 1.0f) g.endTransparencyLayer();
    }
    g.setOpacity (1.0f);
}

// Bricks, the light on them, then every piece that reaches into `area`
void DeviceWall::paintWall (juce::Graphics& g, juce::Rectangle<int> area, bool resting)
{
    g.setOpacity (1.0f);
    g.drawImage (wallImage, getLocalBounds().toFloat());

    if (! resting || manyLit())
    {
        const float beat = (resting || manyLit()) ? 0.0f : pulse;
        g.setOpacity (juce::jlimit (0.0f, 1.0f, (0.8f + 0.2f * beat) * model.glow));
        g.drawImageAt (poolImage, viewArea.getX(), viewArea.getY());
        g.setOpacity (1.0f);
    }

    for (int i = 0; i < Wall::numDevices; ++i)
        if (i != selected)
            paintPiece (g, i, area, resting);
    paintPiece (g, selected, area, resting);
}

void DeviceWall::rebuildFrame()
{
    frameDirty = false;
    beatDirty = true;
    if (viewArea.isEmpty())
        return;
    const float ds = displayScale();
    frameScale = ds;
    const int w = juce::jmax (1, juce::roundToInt ((float) viewArea.getWidth() * ds)), h = juce::jmax (1, juce::roundToInt ((float) viewArea.getHeight() * ds));
    if (frame.getWidth() != w || frame.getHeight() != h)
        frame = juce::Image (juce::Image::RGB, w, h, false);

    juce::Graphics g (frame);
    g.addTransform (AffineTransform::translation ((float) -viewArea.getX(), (float) -viewArea.getY()).scaled (ds));
    paintWall (g, viewArea, true);
}

// What the beat adds where every piece is lit: a little more light on the wall and round each tube.
// One picture pixel per screen point: it is all blur, and that size lays down quickly on any screen.
void DeviceWall::rebuildBeatLayer()
{
    beatDirty = false;
    const float k = 1.0f;
    const int w = juce::jmax (1, juce::roundToInt ((float) viewArea.getWidth() * k)), h = juce::jmax (1, juce::roundToInt ((float) viewArea.getHeight() * k));
    if (beatLayer.getWidth() != w || beatLayer.getHeight() != h)
        beatLayer = juce::Image (juce::Image::ARGB, w, h, true);
    else
        beatLayer.clear (beatLayer.getBounds());

    juce::Graphics g (beatLayer);
    g.addTransform (AffineTransform::translation ((float) -viewArea.getX(), (float) -viewArea.getY()).scaled (k));
    g.setOpacity (juce::jlimit (0.0f, 1.0f, 0.2f * model.glow));
    g.drawImageAt (poolImage, viewArea.getX(), viewArea.getY());
    for (int i = 0; i < Wall::numDevices; ++i)
    {
        if (! isLit (i))
            continue;
        ensureCache (i, true);
        const auto& c = cache[(size_t) i];
        const bool soft = editing && i != selected;
        drawImage (g, c.glow, imagePlacement (i, 0.0f, c.glowScale), (soft ? 0.15f : 0.3f) * model.glow);
    }
}

// The places where the wall differs from the kept picture right now
void DeviceWall::collectPatches (juce::RectangleList<int>& out) const
{
    auto add = [&] (juce::Rectangle<float> r)
    {
        const auto area = r.getSmallestIntegerContainer().expanded (1).getIntersection (viewArea);
        if (! area.isEmpty())
            out.add (area);
    };

    if (! manyLit())
    {
        if (isLit (selected))
            add (poolReach (selected).getUnion (pieceBounds (selected, 0.0f)));
        return;
    }
    for (int i = 0; i < Wall::numDevices; ++i)
    {
        if (grow[(size_t) i] > 0.004f || i == flickerPiece)
            add (pieceBounds (i, grow[(size_t) i]));
        else if (isLit (i) && WallArt::prepared (i, model.pieces[(size_t) i].style).hasLive)
            add (liveBounds (i));
    }
}

juce::Rectangle<float> DeviceWall::toastBounds() const
{
    juce::GlyphArrangement measure;
    measure.addLineOfText (Theme::get().label (12.5f), toast, 0.0f, 0.0f);
    const float w = measure.getBoundingBox (0, -1, true).getWidth() + 40.0f;
    return juce::Rectangle<float> (w, 32.0f).withCentre ({ (float) getWidth() * 0.5f, (float) footArea.getY() - 34.0f });
}

void DeviceWall::rebuildBackdrop()
{
    const float ds = displayScale();
    const int w = juce::jmax (1, juce::roundToInt ((float) getWidth() * ds)), h = juce::jmax (1, juce::roundToInt ((float) getHeight() * ds));
    wallImage = juce::Image (juce::Image::RGB, w, h, true);
    juce::Graphics g (wallImage);
    g.setTiledImageFill (WallArt::brickTile (ds, false), 0, 0, 1.0f);
    g.fillAll();
    // Darker towards the floor, so the tubes carry the picture
    g.setGradientFill (juce::ColourGradient (Colour (0xff0c0c0e).withAlpha (0.42f), 0.0f, 0.0f, Colour (0xff0c0c0e).withAlpha (0.70f), 0.0f, (float) h, false));
    g.fillAll();
    litTile = WallArt::brickTile (1.0f, true);
}

// The light each lit piece throws on the bricks: a soft pool of its colour, taken up more by the brick
// faces than the mortar so the wall's texture shows through it.
void DeviceWall::rebuildPools()
{
    poolsDirty = false;
    if (viewArea.isEmpty())
        return;
    if (poolImage.getWidth() != viewArea.getWidth() || poolImage.getHeight() != viewArea.getHeight())
        poolImage = juce::Image (juce::Image::ARGB, viewArea.getWidth(), viewArea.getHeight(), true);
    else
        poolImage.clear (poolImage.getBounds());

    juce::Rectangle<int> used;
    {
        juce::Graphics g (poolImage);
        const bool many = editing || model.alwaysBright;
        for (int i = 0; i < Wall::numDevices; ++i)
        {
            if (! isLit (i))
                continue;
            const auto box = boxOnScreen (i).translated ((float) -viewArea.getX(), (float) -viewArea.getY());
            const float rx = box.getWidth() * 0.86f, ry = box.getHeight() * 0.92f, cx = box.getCentreX(), cy = box.getCentreY();
            const juce::Rectangle<float> reach (cx - rx, cy - ry, rx * 2.0f, ry * 2.0f);
            if (! reach.intersects (poolImage.getBounds().toFloat()))
                continue;

            const bool full = ! editing || i == selected;
            const float a0 = full ? (many ? 0.46f : 0.64f) : 0.30f, a1 = full ? (many ? 0.18f : 0.26f) : 0.11f;
            const auto c = WallArt::coloursFor (i, model.pieces[(size_t) i].style).c[0];
            juce::ColourGradient grad (c.withAlpha (a0), cx, cy, c.withAlpha (0.0f), cx + rx, cy, true);
            grad.addColour (0.52, c.withAlpha (a1));
            juce::FillType fill (grad);
            fill.transform = AffineTransform::scale (1.0f, ry / rx, cx, cy);
            g.setFillType (fill);
            g.fillRect (reach);
            used = used.getUnion (reach.getSmallestIntegerContainer());
        }
    }

    used = used.getIntersection (poolImage.getBounds());
    if (used.isEmpty() || ! litTile.isValid())
        return;

    juce::Image::BitmapData pool (poolImage, juce::Image::BitmapData::readWrite);
    juce::Image::BitmapData tile (litTile, juce::Image::BitmapData::readOnly);
    const int tw = tile.width, th = tile.height;
    for (int y = used.getY(); y < used.getBottom(); ++y)
    {
        auto* line = pool.getLinePointer (y);
        const auto* tileLine = tile.getLinePointer ((y + viewArea.getY()) % th);
        for (int x = used.getX(); x < used.getRight(); ++x)
        {
            auto* px = (juce::PixelARGB*) (line + x * pool.pixelStride);
            const int take = ((const juce::PixelARGB*) (tileLine + ((x + viewArea.getX()) % tw) * tile.pixelStride))->getAlpha();
            px->multiplyAlpha (take);
        }
    }
}

juce::Rectangle<float> DeviceWall::swatchBounds (int index) const
{
    return { (float) swatchArea.getX() + 3.0f + (float) index * 22.0f, (float) swatchArea.getCentreY() - 8.0f, 16.0f, 16.0f };
}

void DeviceWall::paint (juce::Graphics& g)
{
    const auto paintStart = juce::Time::getMillisecondCounterHiRes();
    struct Timed
    {
        DeviceWall& wall; double start;
        ~Timed() { wall.lastPaintMs = (float) (juce::Time::getMillisecondCounterHiRes() - start); wall.lastPaintEnd = juce::Time::getMillisecondCounter(); }
    } timed { *this, paintStart };

    auto& th = Theme::get();
    const float ds = displayScale();
    if (! wallImage.isValid() || wallImage.getWidth() != juce::roundToInt ((float) getWidth() * ds))
    {
        rebuildBackdrop();
        invalidate();
    }
    if (poolsDirty || ! poolImage.isValid())
        rebuildPools();
    if (frameDirty || ! frame.isValid() || ! juce::approximatelyEqual (frameScale, ds))
        rebuildFrame();

    {
        // Behind the top bar and the footer the bricks carry on
        juce::Graphics::ScopedSaveState bars (g);
        g.excludeClipRegion (viewArea);
        g.setOpacity (1.0f);
        g.drawImage (wallImage, getLocalBounds().toFloat());
    }

    {
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (viewArea);
        const auto showing = g.getClipBounds();

        g.setOpacity (1.0f);
        g.drawImageTransformed (frame, AffineTransform::scale (1.0f / ds).translated ((float) viewArea.getX(), (float) viewArea.getY()));

        juce::RectangleList<int> patches;
        collectPatches (patches);
        for (const auto& patch : patches)
        {
            if (! patch.intersects (showing))
                continue;
            juce::Graphics::ScopedSaveState one (g);
            g.reduceClipRegion (patch);
            paintWall (g, patch, false);
        }

        if (manyLit() && pulse > 0.004f)
        {
            if (beatDirty || ! beatLayer.isValid())
                rebuildBeatLayer();
            g.setOpacity (juce::jmin (1.0f, pulse));
            g.drawImageAt (beatLayer, viewArea.getX(), viewArea.getY());
            g.setOpacity (1.0f);
        }

        const bool anyMatch = model.firstMatch (query) >= 0;
        if (! anyMatch)
        {
            g.setColour (th.muted);
            g.setFont (th.heading (20.0f));
            g.drawText ("Nothing called \"" + query + "\"", viewArea.withHeight (60), juce::Justification::centred);
        }

        if (editing)
        {
            for (int i = 0; i < Wall::numDevices; ++i)
            {
                const auto box = boxOnScreen (i).reduced (2.0f);
                const bool chosen = i == selected;
                juce::Path outline, dashes;
                outline.addRoundedRectangle (box, 8.0f);
                const float pattern[] = { 4.0f, 4.0f };
                juce::PathStrokeType (1.0f).createDashedStroke (dashes, outline, pattern, 2);
                g.setColour (chosen ? th.themeColour() : th.line.brighter (0.1f));
                g.fillPath (dashes);

                const auto grip = gripFor (i);
                g.setColour (th.bg);
                g.fillRoundedRectangle (grip, 4.0f);
                g.setColour (th.themeColour().withAlpha (chosen ? 1.0f : 0.5f));
                g.drawRoundedRectangle (grip, 4.0f, 1.0f);
            }
        }
    }

    // Top bar
    g.setColour (Colour (0x88101012));
    g.fillRect (barArea);
    g.setColour (th.line2.brighter (0.05f));
    g.drawHorizontalLine (barArea.getBottom() - 1, 0.0f, (float) getWidth());

    g.setColour (th.text);
    Glow::drawSpacedText (g, "DEVICES", headingArea.toFloat(), th.heading (19.0f), 5.0f, juce::Justification::centredLeft);

    auto find = findArea.toFloat().reduced (0.5f, 1.0f);
    g.setColour (Colour (0x40000000));
    g.fillRoundedRectangle (find, find.getHeight() * 0.5f);
    g.setColour (query.isNotEmpty() ? th.themeColour().withAlpha (0.8f) : th.line.brighter (0.1f));
    g.drawRoundedRectangle (find, find.getHeight() * 0.5f, 1.0f);
    g.setFont (th.label (12.5f));
    g.setColour (query.isNotEmpty() ? th.text : th.muted);
    g.drawText (query.isNotEmpty() ? query : juce::String (editing ? "Find a device" : "Type to find"), findArea.reduced (12, 0), juce::Justification::centredLeft);

    g.setColour (th.muted);
    const auto small = th.label (9.5f);
    Glow::drawSpacedText (g, "STYLE", styleLabelArea.toFloat(), small, 1.8f, juce::Justification::centredRight);
    Glow::drawSpacedText (g, "ZOOM", zoomLabelArea.toFloat(), small, 1.8f, juce::Justification::centredRight);
    Glow::drawSpacedText (g, "GLOW", glowLabelArea.toFloat(), small, 1.8f, juce::Justification::centredRight);
    g.setFont (th.label (10.5f));
    g.drawText (juce::String (juce::roundToInt (model.glow * 100.0f)) + "%", glowValueArea, juce::Justification::centredLeft);

    for (int i = 0; i < th.choices().size(); ++i)
    {
        const auto s = swatchBounds (i);
        const bool current = ! th.isRainbow() && i == th.getThemeIndex();
        g.setColour (th.choices()[i].colour.withAlpha (current ? 1.0f : 0.75f));
        g.fillEllipse (s.reduced (1.0f));
        if (current)
        {
            g.setColour (th.choices()[i].colour);
            g.drawEllipse (s.expanded (2.0f), 1.0f);
        }
    }

    if (editing)
    {
        g.setColour (th.themeColour().withAlpha (0.07f));
        g.fillRect (editArea);
        g.setColour (th.themeColour().withAlpha (0.4f));
        g.drawHorizontalLine (editArea.getBottom() - 1, 0.0f, (float) getWidth());
        auto e = editArea.reduced (sidePad, 0);
        g.setColour (th.text);
        Glow::drawSpacedText (g, "EDITING", e.removeFromLeft (96).toFloat(), th.heading (13.0f), 3.2f, juce::Justification::centredLeft);
        g.setColour (th.muted);
        g.setFont (th.label (12.0f));
        g.drawText ("Drag a piece to move it. Pull its corner to resize. Select one to retitle it or change its style.",
                    e.withTrimmedRight (230), juce::Justification::centredLeft);
    }

    // Footer: what is selected, and the keys
    g.setColour (Colour (0x88101012));
    g.fillRect (footArea);
    g.setColour (th.line2.brighter (0.05f));
    g.drawHorizontalLine (footArea.getY(), 0.0f, (float) getWidth());
    auto f = footArea.reduced (sidePad, 0);

    const juce::String keys = editing ? "ARROWS NUDGE     - + ZOOM     0 FIT     ESC DONE"
                                      : "HOLD TAB     TYPE TO FIND     ARROWS     1 2 3 STYLE     - + ZOOM     0 FIT     ENTER LOADS";
    g.setColour (th.muted);
    Glow::drawSpacedText (g, keys, f.removeFromRight (juce::jmin (f.getWidth() / 2, editing ? 380 : 640)).toFloat(), th.label (9.5f), 1.2f, juce::Justification::centredRight);

    if (model.firstMatch (query) >= 0)
    {
        const auto& d = Wall::device (selected);
        const auto title = model.titleOf (selected).toUpperCase();
        const auto heading = th.heading (18.0f);
        juce::GlyphArrangement measure;
        measure.addLineOfText (heading, title, 0.0f, 0.0f);
        const int titleWidth = juce::roundToInt (measure.getBoundingBox (0, -1, true).getWidth() + (float) title.length() * 3.6f) + 18;
        g.setColour (th.text);
        Glow::drawSpacedText (g, title, f.removeFromLeft (juce::jmin (titleWidth, f.getWidth() / 2)).toFloat(), heading, 3.6f, juce::Justification::centredLeft);

        juce::String detail;
        if (model.pieces[(size_t) selected].title.isNotEmpty())
            detail << juce::String (d.name).toUpperCase() << "     ";
        detail << juce::String (Wall::kindName (d.kind)).toUpperCase() << "     " << d.detail;
        detail << "     STYLE " << numerals[model.pieces[(size_t) selected].style] << " " << juce::String (Wall::styleName (model.pieces[(size_t) selected].style)).toUpperCase();
        g.setColour (th.muted);
        g.setFont (th.label (11.5f));
        g.drawText (detail, f, juce::Justification::centredLeft);
    }

    if (toast.isNotEmpty())
    {
        g.setFont (th.label (12.5f));
        const auto r = toastBounds();
        g.setColour (th.panel);
        g.fillRoundedRectangle (r, 16.0f);
        g.setColour (th.themeColour());
        g.drawRoundedRectangle (r, 16.0f, 1.0f);
        g.setColour (th.text);
        g.drawText (toast, r, juce::Justification::centred);
    }
}

//==============================================================================
// Mouse and keys

void DeviceWall::mouseMove (const juce::MouseEvent& e)
{
    if (editing)
    {
        const int i = pieceAt (e.position);
        setMouseCursor (i >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
        return;
    }
    const int i = pieceAt (e.position);
    setMouseCursor (i >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    if (i >= 0)
        select (i);
}

void DeviceWall::mouseDown (const juce::MouseEvent& e)
{
    if (titleEditor.hasKeyboardFocus (false) && onReleaseFocus)
        onReleaseFocus();

    auto& th = Theme::get();
    for (int i = 0; i < th.choices().size(); ++i)
        if (swatchBounds (i).expanded (3.0f).contains (e.position))
        {
            th.setThemeIndex (i);
            return;
        }

    if (! viewArea.toFloat().contains (e.position))
        return;

    if (! editing)
    {
        if (const int i = pieceAt (e.position); i >= 0)
        {
            select (i);
            load (i);
        }
        return;
    }

    // Edit mode: a corner grip resizes, anywhere else on a piece moves it
    int hit = -1;
    bool onGrip = false;
    if (gripFor (selected).expanded (3.0f).contains (e.position)) { hit = selected; onGrip = true; }
    for (int i = Wall::numDevices; hit < 0 && --i >= 0;)
        if (gripFor (i).expanded (3.0f).contains (e.position)) { hit = i; onGrip = true; }
    if (hit < 0)
        hit = pieceAt (e.position);
    if (hit < 0)
        return;

    select (hit);
    drag = onGrip ? Drag::size : Drag::move;
    dragStart = e.position;
    dragFrom = model.pieces[(size_t) hit];
}

void DeviceWall::mouseDrag (const juce::MouseEvent& e)
{
    if (drag == Drag::none || scale <= 0.0f)
        return;
    const auto d = (e.position - dragStart) / scale;
    auto& p = model.pieces[(size_t) selected];

    if (drag == Drag::size)
        p.w = std::round (juce::jlimit (Wall::minPieceWidth, Wall::designWidth - juce::jmax (0.0f, p.x), dragFrom.w + juce::jmax (d.x, d.y / Wall::aspect)));
    else
    {
        p.x = std::round (juce::jlimit (-p.w * 0.3f, Wall::designWidth - p.w * 0.7f, dragFrom.x + d.x));
        p.y = std::round (juce::jmax (0.0f, dragFrom.y + d.y));
    }
    invalidate();
    placeTools();
    repaint();
}

void DeviceWall::mouseUp (const juce::MouseEvent&)
{
    if (drag == Drag::none)
        return;
    drag = Drag::none;
    changed();
}

void DeviceWall::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        if (std::abs (wheel.deltaY) > 0.0001f)
            zoomBy (wheel.deltaY > 0.0f ? 1.1f : 1.0f / 1.1f);
        return;
    }
    scroll.x -= wheel.deltaX * 260.0f;
    scroll.y -= wheel.deltaY * 260.0f;
    layoutView();
    repaint();
}

bool DeviceWall::keyUsed (const juce::KeyPress& key)
{
    if (! isOpen() || titleEditor.hasKeyboardFocus (false))
        return false;

    const auto mods = key.getModifiers();
    if (key == juce::KeyPress::escapeKey)
    {
        if (editing) setEditing (false);
        else if (query.isNotEmpty()) setQuery ({});
        else close();
        return true;
    }

    int dx = 0, dy = 0;
    if (key.isKeyCode (juce::KeyPress::leftKey)) dx = -1;
    else if (key.isKeyCode (juce::KeyPress::rightKey)) dx = 1;
    else if (key.isKeyCode (juce::KeyPress::upKey)) dy = -1;
    else if (key.isKeyCode (juce::KeyPress::downKey)) dy = 1;
    if (dx != 0 || dy != 0)
    {
        if (editing)
        {
            auto& p = model.pieces[(size_t) selected];
            p.x = juce::jlimit (-p.w * 0.3f, Wall::designWidth - p.w * 0.7f, p.x + (float) dx * 10.0f);
            p.y = juce::jmax (0.0f, p.y + (float) dy * 10.0f);
            changed();
        }
        else if (model.firstMatch (query) >= 0)
        {
            select (model.neighbour (selected, dx, dy, query));
            // Bring it into view
            const auto box = boxOnScreen (selected);
            if (box.getY() < (float) viewArea.getY()) scroll.y -= (float) viewArea.getY() - box.getY() + 20.0f;
            else if (box.getBottom() > (float) viewArea.getBottom()) scroll.y += box.getBottom() - (float) viewArea.getBottom() + 20.0f;
            if (box.getX() < (float) viewArea.getX()) scroll.x -= (float) viewArea.getX() - box.getX() + 20.0f;
            else if (box.getRight() > (float) viewArea.getRight()) scroll.x += box.getRight() - (float) viewArea.getRight() + 20.0f;
            layoutView();
            repaint();
        }
        return true;
    }

    if (key == juce::KeyPress::returnKey)
    {
        if (! editing && model.firstMatch (query) >= 0)
            load (selected);
        return true;
    }
    if (key == juce::KeyPress::backspaceKey || key == juce::KeyPress::deleteKey)
    {
        if (! editing && query.isNotEmpty())
            setQuery (query.dropLastCharacters (1));
        return true;
    }

    if (mods.isCommandDown() || mods.isCtrlDown() || mods.isAltDown())
        return false;

    const auto ch = key.getTextCharacter();
    if (ch >= '1' && ch <= '3')
    {
        if (model.firstMatch (query) >= 0)
        {
            model.pieces[(size_t) selected].style = (int) (ch - '1');
            changed();
        }
        return true;
    }
    if (ch == '-' || ch == '_') { zoomBy (1.0f / 1.2f); return true; }
    if (ch == '=' || ch == '+') { zoomBy (1.2f); return true; }
    if (ch == '0')              { setFit (! model.fitToScreen); return true; }

    if (juce::CharacterFunctions::isLetter (ch))
    {
        if (! editing)
            setQuery (query + juce::String::charToString (ch));
        return true;
    }
    return false;
}

void DeviceWall::tick (float newPulse)
{
    if (! isOpen())
        return;

    // Tab let go while something else had the keyboard
    if (tabDown && ! juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::tabKey))
        tabReleased();
    if (! isOpen())
        return;

    // Only what moved is repainted, along with what moved last time so it is wiped
    juce::RectangleList<int> dirty;
    auto add = [&] (juce::Rectangle<float> r)
    {
        const auto area = r.getSmallestIntegerContainer().expanded (2).getIntersection (getLocalBounds());
        if (! area.isEmpty())
            dirty.add (area);
    };

    const auto now = juce::Time::getMillisecondCounter();
    const bool beatMoved = std::abs (newPulse - pulse) > 0.002f;
    pulse = newPulse;
    spin += juce::MathConstants<double>::twoPi / 8.0 * (double) Neon::frameTime();     // one turn in eight seconds

    for (int i = 0; i < Wall::numDevices; ++i)
    {
        const float target = (model.alwaysBright && ! editing && i == selected && model.matches (i, query)) ? 1.0f : 0.0f;
        auto& gr = grow[(size_t) i];
        if (! juce::approximatelyEqual (gr, target))
        {
            add (pieceBounds (i, gr));
            if (std::abs (target - gr) > 0.004f) gr += (target - gr) * (1.0f - Neon::perFrame (0.62f));
            else gr = target;
            add (pieceBounds (i, gr));
        }
    }

    // Now and then one tube stutters, as old neon does
    if (now >= nextFlickerAt)
    {
        nextFlickerAt = now + 2600;
        juce::Array<int> lit;
        for (int i = 0; i < Wall::numDevices; ++i)
            if (isLit (i)) lit.add (i);
        if (! lit.isEmpty() && drag == Drag::none)
        {
            auto& random = juce::Random::getSystemRandom();
            flickerPiece = lit[random.nextInt (lit.size())];
            const auto& art = WallArt::prepared (flickerPiece, model.pieces[(size_t) flickerPiece].style);
            flickerTube = art.tubes.empty() ? -1 : random.nextInt ((int) art.tubes.size());
            flickerAt = now;
        }
    }
    if (flickerPiece >= 0)
    {
        add (pieceBounds (flickerPiece, grow[(size_t) flickerPiece]));
        if (now - flickerAt >= 420)
            flickerPiece = -1;
    }

    if (manyLit())
    {
        if (beatMoved)
            add (viewArea.toFloat());
        for (int i = 0; i < Wall::numDevices; ++i)
            if (isLit (i) && WallArt::prepared (i, model.pieces[(size_t) i].style).hasLive)
                add (liveBounds (i));
    }
    else if (isLit (selected))
    {
        if (beatMoved || now - struckAt < 560)
            add (poolReach (selected).getUnion (pieceBounds (selected, 0.0f)));
        else if (WallArt::prepared (selected, model.pieces[(size_t) selected].style).hasLive)
            add (liveBounds (selected));
    }

    if (toast.isNotEmpty())
    {
        add (toastBounds().expanded (3.0f));
        if (now - toastAt > 1700) toast.clear();
    }

    for (const auto& r : dirty)     repaint (r);
    for (const auto& r : lastDirty) repaint (r);
    lastDirty = dirty;
}

// While the wall is closed its pictures are made ready, one piece per tick, so that Tab shows it at once
void DeviceWall::warm()
{
    if (tabDown && ! juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::tabKey))
        tabDown = false;                                // the key-up went elsewhere
    if (isOpen() || viewArea.isEmpty() || getPeer() == nullptr)
        return;
    if (warmTicks < 45) { ++warmTicks; return; }       // let the app itself start first

    for (int i = 0; i < Wall::numDevices; ++i)
        if (ensureCache (i, isLit (i) && manyLit()))
            return;
    if (! manyLit() && ensureCache (selected, true))
        return;

    const float ds = displayScale();
    if (! wallImage.isValid() || wallImage.getWidth() != juce::roundToInt ((float) getWidth() * ds))
    {
        rebuildBackdrop();
        invalidate();
    }
    if (poolsDirty || ! poolImage.isValid())
        rebuildPools();
    if (frameDirty || ! frame.isValid() || ! juce::approximatelyEqual (frameScale, ds))
        rebuildFrame();
}
