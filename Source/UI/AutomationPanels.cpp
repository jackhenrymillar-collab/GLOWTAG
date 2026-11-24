#include "AutomationPanels.h"

namespace
{
    const juce::Colour grey { 0xff8a8d94 };
    const juce::Colour dim  { 0xff6d7077 };

    void drawPanel (juce::Graphics& g, juce::Rectangle<float> p, juce::Colour accent)
    {
        auto& th = Theme::get();
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (p.translated (0.0f, 6.0f).expanded (4.0f), 12.0f);
        g.setColour (th.panel);
        g.fillRoundedRectangle (p, 10.0f);
        g.setColour (accent.withAlpha (0.35f));
        g.drawRoundedRectangle (p.reduced (0.5f), 10.0f, 1.0f);
    }
}

//==============================================================================
AutomationChooser::AutomationChooser (AppContext& c) : ctx (c)
{
    setInterceptsMouseClicks (true, true);
}

juce::Rectangle<float> AutomationChooser::panel() const
{
    auto p = juce::Rectangle<float> ((float) anchor.getX(), (float) anchor.getBottom() + 4.0f, (float) width, (float) height);
    if (p.getBottom() > (float) getHeight() - 8.0f) p.setY ((float) anchor.getY() - 4.0f - (float) height);
    p.setX (juce::jlimit (8.0f, juce::jmax (8.0f, (float) getWidth() - 8.0f - p.getWidth()), p.getX()));
    p.setY (juce::jlimit (8.0f, juce::jmax (8.0f, (float) getHeight() - 8.0f - p.getHeight()), p.getY()));
    return p;
}

juce::Rectangle<float> AutomationChooser::groupArea() const
{
    auto p = panel().reduced (12.0f);
    p.removeFromTop (34.0f);
    return p.removeFromLeft (190.0f);
}

juce::Rectangle<float> AutomationChooser::listArea() const
{
    auto p = panel().reduced (12.0f);
    p.removeFromTop (34.0f);
    p.removeFromLeft (202.0f);
    p.removeFromBottom (20.0f);
    return p;
}

void AutomationChooser::open (int t, juce::Rectangle<int> screenAnchor)
{
    track = t;
    params = ctx.paramsOf (track);
    groups.clear();
    for (auto& p : params) groups.addIfNotAlreadyThere (p.group);
    const auto shownNow = ctx.shownTarget (track);
    group = 0;
    for (auto& p : params)
        if (p.target == shownNow) { group = juce::jmax (0, groups.indexOf (p.group)); break; }
    search.clear();
    anchor = getLocalArea (nullptr, screenAnchor);
    scroll = 0;
    refilter();
    for (int i = 0; i < (int) listed.size(); ++i)
        if (params[(size_t) listed[(size_t) i]].target == shownNow) highlight = i;
    if (highlight >= visibleRows()) scroll = highlight - visibleRows() + 1;
    setVisible (true);
    toFront (false);
    repaint();
}

void AutomationChooser::close()
{
    setVisible (false);
}

void AutomationChooser::refilter()
{
    listed.clear();
    const auto q = search.toLowerCase().trim();
    for (int i = 0; i < (int) params.size(); ++i)
    {
        auto& p = params[(size_t) i];
        if (q.isEmpty() ? p.group == groups[group]
                        : (p.group + " " + p.name).toLowerCase().contains (q))
            listed.push_back (i);
    }
    highlight = juce::jlimit (0, juce::jmax (0, (int) listed.size() - 1), highlight);
    scroll = juce::jlimit (0, juce::jmax (0, (int) listed.size() - visibleRows()), scroll);
}

bool AutomationChooser::automated (const AutoTarget& t) const
{
    if (auto* lanes = ctx.lanesOf (track))
        if (auto* l = Auto::find (*lanes, t)) return ! l->points.empty();
    return false;
}

void AutomationChooser::pick (int paramIndex)
{
    if (paramIndex < 0 || paramIndex >= (int) params.size()) return;
    const auto target = params[(size_t) paramIndex].target;
    const int t = track;
    close();
    ctx.showLane (t, target);
}

bool AutomationChooser::keyUsed (const juce::KeyPress& key)
{
    if (! isOpen()) return false;
    if (key == juce::KeyPress::escapeKey) { if (search.isNotEmpty()) { search.clear(); refilter(); repaint(); } else close(); return true; }
    if (key == juce::KeyPress::returnKey) { if (! listed.empty()) pick (listed[(size_t) highlight]); return true; }
    if (key == juce::KeyPress::downKey || key == juce::KeyPress::upKey)
    {
        highlight = juce::jlimit (0, juce::jmax (0, (int) listed.size() - 1), highlight + (key == juce::KeyPress::downKey ? 1 : -1));
        if (highlight < scroll) scroll = highlight;
        if (highlight >= scroll + visibleRows()) scroll = highlight - visibleRows() + 1;
        repaint();
        return true;
    }
    if (key == juce::KeyPress::leftKey || key == juce::KeyPress::rightKey)
    {
        if (search.isEmpty() && ! groups.isEmpty())
        {
            group = (group + (key == juce::KeyPress::rightKey ? 1 : groups.size() - 1)) % groups.size();
            highlight = scroll = 0;
            refilter();
            repaint();
        }
        return true;
    }
    if (key == juce::KeyPress::backspaceKey || key == juce::KeyPress::deleteKey)
    {
        search = search.dropLastCharacters (1);
        highlight = scroll = 0;
        refilter();
        repaint();
        return true;
    }
    if (key.getModifiers().isCommandDown()) return false;
    const auto ch = key.getTextCharacter();
    if (ch >= ' ')
    {
        search << juce::String::charToString (ch);
        highlight = scroll = 0;
        refilter();
        repaint();
    }
    return true;      // the chooser has the keys while it is open (no notes play)
}

void AutomationChooser::mouseDown (const juce::MouseEvent& e)
{
    if (! panel().contains (e.position)) { close(); return; }
    const auto ga = groupArea();
    if (ga.contains (e.position))
    {
        const int i = (int) ((e.position.y - ga.getY()) / (float) rowH);
        if (i >= 0 && i < groups.size())
        {
            group = i;
            search.clear();
            highlight = scroll = 0;
            refilter();
            repaint();
        }
        return;
    }
    const auto la = listArea();
    if (la.contains (e.position))
    {
        const int i = scroll + (int) ((e.position.y - la.getY()) / (float) rowH);
        if (i >= 0 && i < (int) listed.size()) pick (listed[(size_t) i]);
    }
}

void AutomationChooser::mouseMove (const juce::MouseEvent& e)
{
    const auto ga = groupArea(), la = listArea();
    const int g = ga.contains (e.position) ? (int) ((e.position.y - ga.getY()) / (float) rowH) : -1;
    const int it = la.contains (e.position) ? scroll + (int) ((e.position.y - la.getY()) / (float) rowH) : -1;
    if (g != hoverGroup || it != hoverItem)
    {
        hoverGroup = g;
        hoverItem = it;
        if (it >= 0 && it < (int) listed.size()) highlight = it;
        repaint();
    }
}

void AutomationChooser::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (! listArea().contains (e.position)) return;
    scroll = juce::jlimit (0, juce::jmax (0, (int) listed.size() - visibleRows()), scroll - (w.deltaY > 0 ? 1 : w.deltaY < 0 ? -1 : 0) * 2);
    repaint();
}

void AutomationChooser::paint (juce::Graphics& g)
{
    auto& th = Theme::get();
    const auto c = track == AppContext::visSlot || ! ctx.isTrack (track) ? th.themeColour() : th.pal (ctx.project.tracks[(size_t) track]->colourIndex);
    const auto p = panel();
    drawPanel (g, p, c);
    const auto ga = groupArea(), la = listArea();
    g.setColour (th.line2);
    g.drawVerticalLine ((int) la.getX() - 6, la.getY(), p.getBottom() - 12.0f);

    const auto shownNow = ctx.shownTarget (track);
    Neon::paintLit (g, getWidth(), getHeight(), glowLayer, 0.6f, [&] (juce::Graphics& gg, bool)
    {
        // Heading: what it automates, or the search so far
        const float top = p.getY() + 30.0f;
        if (search.isEmpty())
        {
            Neon::text (gg, "automate", p.getX() + 16.0f, top, 18.0f, c, 0.8f);
            Neon::label (gg, "type to search", p.getRight() - 16.0f, top - 2.0f, Neon::Align::right, dim, 11.0f);
        }
        else
        {
            Neon::text (gg, search.toLowerCase(), p.getX() + 16.0f, top, 18.0f, c, 1.0f);
            Neon::label (gg, juce::String ((int) listed.size()) + " found", p.getRight() - 16.0f, top - 2.0f, Neon::Align::right, dim, 11.0f);
        }

        // Devices
        for (int i = 0; i < groups.size(); ++i)
        {
            const float y = ga.getY() + (float) (i * rowH);
            if (y + (float) rowH > ga.getBottom()) break;
            const bool on = search.isEmpty() && i == group;
            bool any = false;
            for (auto& prm : params) if (prm.group == groups[i] && automated (prm.target)) { any = true; break; }
            if (on || i == hoverGroup)
            {
                gg.setColour (c.withAlpha (on ? 0.12f : 0.06f));
                gg.fillRoundedRectangle (ga.getX(), y + 1.0f, ga.getWidth(), (float) rowH - 2.0f, 5.0f);
            }
            if (any) Neon::bulb (gg, ga.getX() + 9.0f, y + (float) rowH * 0.5f, c, 1.0f, 2.6f);
            Neon::label (gg, groups[i], ga.getX() + 20.0f, y + 16.5f, Neon::Align::left, on ? c.brighter (0.3f) : grey, 13.0f);
        }

        // Controls
        const int rows = visibleRows();
        for (int k = 0; k < rows && scroll + k < (int) listed.size(); ++k)
        {
            const int idx = scroll + k;
            auto& prm = params[(size_t) listed[(size_t) idx]];
            const float y = la.getY() + (float) (k * rowH);
            const bool isShown = prm.target == shownNow;
            if (idx == highlight)
            {
                gg.setColour (c.withAlpha (0.10f));
                gg.fillRoundedRectangle (la.getX(), y + 1.0f, la.getWidth(), (float) rowH - 2.0f, 5.0f);
            }
            if (automated (prm.target)) Neon::bulb (gg, la.getX() + 9.0f, y + (float) rowH * 0.5f, c, 1.0f, 2.6f);
            auto name = search.isEmpty() ? prm.name : prm.group + "  >  " + prm.name;
            Neon::label (gg, name, la.getX() + 20.0f, y + 16.5f, Neon::Align::left, isShown ? c.brighter (0.3f) : th.text.withAlpha (0.85f), 13.0f);
            const auto range = prm.target.kind == AutoTarget::morph ? juce::String ("a > b > c")
                             : prm.stepped ? juce::String (juce::roundToInt (prm.hi - prm.lo) + 1) + " steps"
                                           : juce::String (prm.lo, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".")
                                             + " .. " + juce::String (prm.hi, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".");
            Neon::label (gg, range, la.getRight() - 4.0f, y + 16.5f, Neon::Align::right, dim, 11.0f);
        }
        if (listed.empty())
            Neon::label (gg, "nothing matches", la.getX() + 20.0f, la.getY() + 16.5f, Neon::Align::left, dim, 13.0f);
        if ((int) listed.size() > rows)
        {
            // Where the list is
            const float h = la.getHeight() * (float) rows / (float) listed.size();
            const float y = la.getY() + (la.getHeight() - h) * (float) scroll / (float) juce::jmax (1, (int) listed.size() - rows);
            gg.setColour (c.withAlpha (0.35f));
            gg.fillRoundedRectangle (la.getRight() + 3.0f, y, 2.5f, h, 1.2f);
        }
        Neon::label (gg, "return picks   esc closes   arrows move", la.getX() + 2.0f, p.getBottom() - 12.0f, Neon::Align::left, dim, 10.5f);
    });
}

//==============================================================================
SettingsPanel::SettingsPanel (AppContext& c) : ctx (c) {}

juce::Rectangle<float> SettingsPanel::panel() const
{
    return getLocalBounds().toFloat().withSizeKeepingCentre (500.0f, 372.0f);
}

juce::Rectangle<float> SettingsPanel::rowBox (int row) const
{
    auto p = panel().reduced (26.0f, 0.0f);
    return { p.getX(), panel().getY() + 74.0f + (float) row * 58.0f, p.getWidth(), 48.0f };
}

juce::Rectangle<float> SettingsPanel::leverTrack() const
{
    auto r = rowBox (brightness);
    return { r.getX() + 6.0f, r.getBottom() - 12.0f, r.getWidth() - 60.0f, 8.0f };
}

juce::Rectangle<float> SettingsPanel::closeBox() const
{
    auto p = panel();
    return { p.getRight() - 84.0f, p.getY() + 12.0f, 70.0f, 30.0f };
}

void SettingsPanel::open()
{
    setVisible (true);
    toFront (false);
    repaint();
}

void SettingsPanel::close()
{
    setVisible (false);
}

bool SettingsPanel::keyUsed (const juce::KeyPress& key)
{
    if (! isOpen()) return false;
    if (key == juce::KeyPress::escapeKey || key == juce::KeyPress::returnKey) { close(); return true; }
    if (key == juce::KeyPress::leftKey || key == juce::KeyPress::rightKey)
    {
        ctx.autoBrightness = juce::jlimit (0.05f, 1.0f, ctx.autoBrightness + (key == juce::KeyPress::rightKey ? 0.05f : -0.05f));
        ++ctx.autoGeneration;
        if (onChanged) onChanged();
        repaint();
        return true;
    }
    return false;
}

void SettingsPanel::toggle (int row)
{
    if (row == sticky) ctx.stickToClips = ! ctx.stickToClips;
    else if (row == lanes) { ctx.lanesShown = ! ctx.lanesShown; ++ctx.autoGeneration; ctx.sendChangeMessage(); }
    else if (row == visTrack)
    {
        ctx.project.visTrack = ! ctx.project.visTrack;
        ctx.automationChanged();
    }
    if (onChanged) onChanged();
    repaint();
}

void SettingsPanel::setBrightness (float x)
{
    const auto t = leverTrack();
    ctx.autoBrightness = juce::jlimit (0.05f, 1.0f, (x - t.getX()) / t.getWidth());
    ++ctx.autoGeneration;
    repaint();
}

void SettingsPanel::mouseDown (const juce::MouseEvent& e)
{
    if (! panel().contains (e.position) || closeBox().contains (e.position)) { close(); return; }
    for (int r = 0; r < numRows; ++r)
        if (rowBox (r).contains (e.position))
        {
            if (r == brightness) { draggingLever = true; setBrightness (e.position.x); }
            else toggle (r);
            return;
        }
}

void SettingsPanel::mouseDrag (const juce::MouseEvent& e)
{
    if (draggingLever) setBrightness (e.position.x);
}

void SettingsPanel::mouseUp (const juce::MouseEvent&)
{
    if (draggingLever && onChanged) onChanged();
    draggingLever = false;
}

void SettingsPanel::mouseMove (const juce::MouseEvent& e)
{
    int h = -1;
    for (int r = 0; r < numRows; ++r) if (rowBox (r).contains (e.position)) h = r;
    if (closeBox().contains (e.position)) h = 99;
    if (h != hover) { hover = h; repaint(); }
}

void SettingsPanel::paint (juce::Graphics& g)
{
    auto& th = Theme::get();
    g.fillAll (juce::Colours::black.withAlpha (0.45f));
    const auto c = th.themeColour();
    const auto p = panel();
    drawPanel (g, p, c);

    Neon::paintLit (g, getWidth(), getHeight(), glowLayer, 0.6f, [&] (juce::Graphics& gg, bool)
    {
        Neon::text (gg, "settings", p.getX() + 26.0f, p.getY() + 42.0f, 26.0f, c, 0.9f);
        Neon::wordButton (gg, "close", closeBox(), 16.0f, c, false, hover == 99);

        struct Item { const char* title; const char* help; bool on; };
        const Item items[] = {
            { "automation sticks to clips", "moving, copying or pasting a clip takes its automation", ctx.stickToClips },
            { "show automation lanes",      "lanes over the clips  (cmd shift a, or \"auto\" at the top)", ctx.lanesShown },
            { "automation brightness",      "", true },
            { "visualiser automation track", "a track under the song that moves the visualiser", ctx.project.visTrack },
        };
        for (int r = 0; r < numRows; ++r)
        {
            const auto box = rowBox (r);
            if (hover == r)
            {
                gg.setColour (c.withAlpha (0.05f));
                gg.fillRoundedRectangle (box.expanded (8.0f, 2.0f), 6.0f);
            }
            Neon::label (gg, items[r].title, box.getX(), box.getY() + 17.0f, Neon::Align::left, th.text.withAlpha (0.9f), 14.5f);
            if (r == brightness)
            {
                // The lever: a tube that is lit as far as the brightness goes, with a lamp handle
                const auto t = leverTrack();
                const float v = ctx.autoBrightness;
                Neon::TubeOptions off;
                off.w = 0.5f;
                off.off = true;
                Neon::tube (gg, Neon::Line { { t.getX(), t.getCentreY() }, { t.getRight(), t.getCentreY() } }, c, off);
                Neon::TubeOptions on;
                on.w = 0.5f;
                const float x = t.getX() + v * t.getWidth();
                if (x > t.getX() + 2.0f)
                    Neon::tube (gg, Neon::Line { { t.getX(), t.getCentreY() }, { x, t.getCentreY() } }, c.withMultipliedAlpha (0.35f + 0.65f * v), on);
                Neon::bulb (gg, x, t.getCentreY(), c, 0.4f + 0.6f * v, 6.0f);
                Neon::label (gg, juce::String (juce::roundToInt (v * 100.0f)) + "%", box.getRight(), t.getCentreY() + 4.0f, Neon::Align::right, c, 13.0f);
            }
            else
            {
                Neon::label (gg, items[r].help, box.getX(), box.getY() + 36.0f, Neon::Align::left, dim, 11.5f);
                const juce::Rectangle<float> sw (box.getRight() - 56.0f, box.getY() + 2.0f, 56.0f, 24.0f);
                Neon::wordButton (gg, items[r].on ? "on" : "off", sw, 15.0f, c, items[r].on, hover == r);
            }
        }
    });
}
