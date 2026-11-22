#include "SequencerPanel.h"

namespace
{
    constexpr float titleHeight = 28.0f;
    constexpr float jackRow = 48.0f;          // bottom strip holding the jacks and their labels
    constexpr float jackCentreFromBottom = 30.0f;
    constexpr int knobSize = 34;
    constexpr int stepColumnWidth = 31;

    Icon iconFor (SeqType t)
    {
        switch (t)
        {
            case SeqType::Clock:     return Icon::Clock;
            case SeqType::Steps:     return Icon::Steps;
            case SeqType::Euclid:    return Icon::Euclid;
            case SeqType::Turing:    return Icon::Turing;
            case SeqType::Burst:     return Icon::Burst;
            case SeqType::Chance:    return Icon::Chance;
            case SeqType::Quantise:  return Icon::Quantise;
            case SeqType::Mutate:    return Icon::Mutate;
            case SeqType::Circadian: return Icon::Circadian;
            case SeqType::Tide:      return Icon::Tide;
            case SeqType::Out:       return Icon::Velocity;
        }
        return Icon::LFO;
    }

    // How each module arranges itself: a display on top or to the left, then a grid of knobs
    struct Plan { int displayTop, displayLeft, cols; };
    Plan planFor (SeqType t)
    {
        switch (t)
        {
            case SeqType::Clock:     return { 44, 0, 3 };
            case SeqType::Steps:     return { 0, 0, 2 };
            case SeqType::Euclid:    return { 108, 0, 4 };
            case SeqType::Turing:    return { 44, 0, 4 };
            case SeqType::Burst:     return { 36, 0, 4 };
            case SeqType::Chance:    return { 44, 0, 4 };
            case SeqType::Quantise:  return { 92, 0, 4 };
            case SeqType::Mutate:    return { 36, 0, 3 };
            case SeqType::Circadian: return { 0, 150, 3 };
            case SeqType::Tide:      return { 64, 0, 4 };
            case SeqType::Out:       return { 74, 0, 3 };
        }
        return { 0, 0, 3 };
    }

    void drawScrew (juce::Graphics& g, float x, float y, juce::Colour c)
    {
        g.setColour (c.withAlpha (0.35f));
        g.drawEllipse (x - 3.5f, y - 3.5f, 7.0f, 7.0f, 1.0f);
        g.setColour (c.withAlpha (0.5f));
        g.drawLine (x - 2.2f, y - 1.2f, x + 2.2f, y + 1.2f, 1.0f);
    }

    void drawLed (juce::Graphics& g, juce::Point<float> p, float r, juce::Colour c, float lit)
    {
        g.setColour (Theme::get().display);
        g.fillEllipse (p.x - r, p.y - r, r * 2.0f, r * 2.0f);
        if (lit > 0.02f)
        {
            g.setColour (c.withAlpha (0.25f * lit));
            g.fillEllipse (p.x - r * 2.2f, p.y - r * 2.2f, r * 4.4f, r * 4.4f);
            g.setColour (c.withAlpha (lit));
            g.fillEllipse (p.x - r * 0.7f, p.y - r * 0.7f, r * 1.4f, r * 1.4f);
        }
        g.setColour (c.withAlpha (0.35f + 0.5f * lit));
        g.drawEllipse (p.x - r, p.y - r, r * 2.0f, r * 2.0f, 1.0f);
    }

    void drawDisplayBox (juce::Graphics& g, juce::Rectangle<float> r)
    {
        auto& th = Theme::get();
        g.setColour (th.display);
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (th.line2);
        g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
    }
}

//==============================================================================
SeqModuleView::SeqModuleView (AppContext& c, SeqRack& r, int trackIdx, int id)
    : ctx (c), rack (r), trackIndex (trackIdx), moduleId (id)
{
    const auto& inf = info();
    for (size_t k = 0; k < inf.knobs.size(); ++k)
    {
        const auto& kn = inf.knobs[k];
        auto* s = knobs.add (new juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox));
        const int n = kn.choices.size();
        if (n > 1)
        {
            s->setRange (0.0, n - 1.0, 1.0);
            s->setValue (SeqKnob::choice (module().knobs[k], n), juce::dontSendNotification);
            s->setDoubleClickReturnValue (true, SeqKnob::choice (kn.defaultValue, n));
        }
        else
        {
            s->setRange (0.0, 1.0);
            s->setValue (module().knobs[k], juce::dontSendNotification);
            s->setDoubleClickReturnValue (true, kn.defaultValue);
        }
        s->setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        s->getProperties().set ("pal", inf.pal);
        s->setTooltip (kn.name);
        s->onValueChange = [this, k, s, n]
        {
            const float v = n > 1 ? SeqKnob::fromChoice ((int) s->getValue(), n) : (float) s->getValue();
            if (auto* m = rack.find (moduleId))
                m->knobs[k] = v;
            ctx.engine.setSeqKnob (trackIndex, moduleId, (int) k, v);
            ctx.touched();
            repaint();
        };
        addAndMakeVisible (s);
    }
    setSize ((int) inf.width, SequencerPanel::railHeight);
}

juce::Point<float> SeqModuleView::jackPosition (int port) const
{
    const int n = numPorts();
    const float w = (float) getWidth();
    const float pitch = w / (float) n;
    return { pitch * ((float) port + 0.5f), (float) getHeight() - jackCentreFromBottom };
}

void SeqModuleView::resized()
{
    const auto plan = planFor (module().type);
    auto r = getLocalBounds().withTrimmedTop ((int) titleHeight).withTrimmedBottom ((int) jackRow).reduced (6, 2);
    displayArea = {};
    if (plan.displayTop > 0)  { displayArea = r.removeFromTop (plan.displayTop); r.removeFromTop (6); }
    if (plan.displayLeft > 0) { displayArea = r.removeFromLeft (plan.displayLeft); r.removeFromLeft (4); }

    if (module().type == SeqType::Steps)
    {
        auto knobArea = r.removeFromLeft (104);
        stepArea = r.withTrimmedLeft (6);
        r = knobArea;
    }

    const int n = knobs.size();
    if (n == 0)
        return;
    const int cols = juce::jmin (plan.cols, n);
    const int rows = (n + cols - 1) / cols;
    const int cellW = r.getWidth() / cols;
    const int cellH = juce::jmin (74, r.getHeight() / rows);
    const int top = r.getY() + (r.getHeight() - cellH * rows) / 2;
    for (int i = 0; i < n; ++i)
    {
        auto cell = juce::Rectangle<int> (r.getX() + (i % cols) * cellW, top + (i / cols) * cellH, cellW, cellH);
        cell.removeFromBottom (info().knobs[(size_t) i].choices.size() > 1 ? 24 : 14);
        knobs[i]->setBounds (cell.withSizeKeepingCentre (knobSize, knobSize));
    }
}

juce::Rectangle<float> SeqModuleView::stepButton (int step) const
{
    const float x = (float) stepArea.getX() + (float) step * ((float) stepArea.getWidth() / seqSteps);
    return { x + 7.0f, (float) stepArea.getBottom() - 16.0f, (float) stepArea.getWidth() / seqSteps - 14.0f, 12.0f };
}

void SeqModuleView::pushStep (int step)
{
    const auto& m = module();
    ctx.engine.setSeqStep (trackIndex, moduleId, step, m.stepPitch[(size_t) step], m.stepOn[(size_t) step]);
    ctx.touched();
}

void SeqModuleView::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu menu;
        const bool fixed = moduleId == SeqRack::clockId || moduleId == SeqRack::outId;
        menu.addItem (1, "Move left", ! fixed);
        menu.addItem (2, "Move right", ! fixed);
        menu.addSeparator();
        menu.addItem (3, "Remove " + info().title, ! fixed);
        menu.showMenuAsync (juce::PopupMenu::Options(), [this] (int r)
        {
            if (r == 1 && onMoveLeft) onMoveLeft();
            if (r == 2 && onMoveRight) onMoveRight();
            if (r == 3 && onRemove) onRemove();
        });
        return;
    }

    if (module().type != SeqType::Steps || ! stepArea.contains (e.getPosition()))
        return;
    auto* m = rack.find (moduleId);
    const int step = juce::jlimit (0, seqSteps - 1, (e.x - stepArea.getX()) * seqSteps / juce::jmax (1, stepArea.getWidth()));
    dragStep = -1;
    if (e.y >= stepArea.getBottom() - 20)
    {
        m->stepOn[(size_t) step] = ! m->stepOn[(size_t) step];
        pushStep (step);
        repaint();
        return;
    }
    dragStep = step;
    mouseDrag (e);
}

void SeqModuleView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragStep < 0)
        return;
    auto* m = rack.find (moduleId);
    const int step = dragStep;
    // Drag in the slot sets the pitch (-12..12 semitones)
    const float slotTop = (float) stepArea.getY() + 14.0f, slotBottom = (float) stepArea.getBottom() - 26.0f;
    const float t = juce::jlimit (0.0f, 1.0f, (slotBottom - (float) e.y) / (slotBottom - slotTop));
    m->stepPitch[(size_t) step] = juce::roundToInt (t * 24.0f) - 12;
    pushStep (step);
    repaint();
}

void SeqModuleView::tick()
{
    auto* node = ctx.engine.liveSeqNode (trackIndex, moduleId);
    if (node == nullptr)
        return;
    for (size_t i = 0; i < shown.size(); ++i)
        shown[i] = node->display[i].load();
    // Pulses the engine raises are cleared here and left to fade
    const bool pulsed = (module().type == SeqType::Clock || module().type == SeqType::Steps || module().type == SeqType::Burst
                         || module().type == SeqType::Chance) && shown[1] >= 0.99f;
    if (pulsed) { node->display[1] = 0.0f; clockLed = 1.0f; }
    clockLed *= 0.82f;
    gateLed = juce::jmax (gateLed * 0.8f, module().type == SeqType::Steps ? shown[2] : (module().type == SeqType::Out ? shown[0] : 0.0f));
    repaint();
}

void SeqModuleView::paint (juce::Graphics& g)
{
    auto& th = Theme::get();
    const auto& inf = info();
    const auto c = th.pal (inf.pal);
    auto r = getLocalBounds().toFloat();

    // Front plate
    g.setColour (th.panel);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (c.withAlpha (0.55f));
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
    g.setColour (c.withAlpha (0.12f));
    g.drawRoundedRectangle (r.reduced (4.5f), 3.0f, 1.0f);
    for (auto p : { juce::Point<float> (9.0f, 9.0f), juce::Point<float> (r.getRight() - 9.0f, 9.0f),
                    juce::Point<float> (9.0f, r.getBottom() - 9.0f), juce::Point<float> (r.getRight() - 9.0f, r.getBottom() - 9.0f) })
        drawScrew (g, p.x, p.y, th.muted);

    // Title
    auto head = r.withHeight (titleHeight).reduced (18.0f, 4.0f);
    g.setColour (th.text);
    Glow::drawSpacedText (g, inf.title.toUpperCase(), head, th.heading (12.5f), 2.4f, juce::Justification::centred);
    Glow::drawIcon (g, iconFor (module().type), head.removeFromLeft (20.0f).reduced (1.0f), c, 0.0f);

    paintDisplay (g, c);

    // Knob labels and switch positions
    for (int i = 0; i < knobs.size(); ++i)
    {
        auto b = knobs[i]->getBounds();
        const auto& kn = inf.knobs[(size_t) i];
        g.setColour (th.muted);
        Glow::drawSpacedText (g, kn.name.toUpperCase(),
                              juce::Rectangle<float> ((float) b.getX() - 16.0f, (float) b.getBottom(), (float) b.getWidth() + 32.0f, 11.0f),
                              th.label (7.0f), 1.0f, juce::Justification::centred);
        if (kn.choices.size() > 1)
        {
            g.setColour (c);
            g.setFont (th.label (8.0f));
            g.drawText (kn.choices[(int) knobs[i]->getValue()].toUpperCase(),
                        juce::Rectangle<float> ((float) b.getX() - 16.0f, (float) b.getBottom() + 11.0f, (float) b.getWidth() + 32.0f, 11.0f).toNearestInt(),
                        juce::Justification::centred);
        }
    }

    // Jack labels (the rings themselves are drawn by the cable layer)
    g.setColour (th.muted);
    for (int p = 0; p < numPorts(); ++p)
    {
        const auto jp = jackPosition (p);
        g.setFont (th.label (6.5f));
        g.drawText (inf.ports[(size_t) p].name.toUpperCase(),
                    juce::Rectangle<float> (jp.x - 22.0f, jp.y + 10.0f, 44.0f, 10.0f).toNearestInt(), juce::Justification::centred);
    }
    // A hairline between the in and out jacks
    const int nIn = inf.numInputs();
    if (nIn > 0 && nIn < numPorts())
    {
        const float x = (jackPosition (nIn - 1).x + jackPosition (nIn).x) * 0.5f;
        g.setColour (th.line2);
        g.drawVerticalLine ((int) x, r.getBottom() - jackRow + 6.0f, r.getBottom() - 8.0f);
    }
}

void SeqModuleView::paintDisplay (juce::Graphics& g, juce::Colour c)
{
    auto& th = Theme::get();
    auto a = displayArea.toFloat();
    const auto& m = module();

    switch (m.type)
    {
        case SeqType::Clock:
        {
            drawDisplayBox (g, a);
            g.setColour (c);
            g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 17.0f, juce::Font::plain)));
            g.drawText (juce::String (ctx.project.bpm.load(), 1), a.reduced (10.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft);
            g.setColour (th.muted);
            g.setFont (th.label (7.0f));
            g.drawText ("BPM", a.reduced (10.0f, 4.0f).toNearestInt(), juce::Justification::bottomRight);
            drawLed (g, { a.getRight() - 14.0f, a.getY() + 12.0f }, 3.5f, c, clockLed);
            break;
        }
        case SeqType::Steps:
        {
            const float colW = (float) stepArea.getWidth() / seqSteps;
            const float slotTop = (float) stepArea.getY() + 14.0f, slotBottom = (float) stepArea.getBottom() - 26.0f;
            const int live = (int) shown[0];
            auto* node = ctx.engine.liveSeqNode (trackIndex, moduleId);
            const int length = node != nullptr ? SeqKnob::count (node->knob[4].load(), 1, seqSteps) : seqSteps;
            for (int s = 0; s < seqSteps; ++s)
            {
                const float x = (float) stepArea.getX() + s * colW + colW * 0.5f;
                const bool inLength = s < length;
                const bool on = m.stepOn[(size_t) s];
                const bool current = s == live && gateLed > 0.05f;
                // Slot
                g.setColour (th.display);
                g.fillRoundedRectangle (x - 2.5f, slotTop, 5.0f, slotBottom - slotTop, 2.5f);
                g.setColour (th.line2);
                g.drawHorizontalLine ((int) ((slotTop + slotBottom) * 0.5f), x - 7.0f, x + 7.0f);
                // Cap
                const float t = (float) (m.stepPitch[(size_t) s] + 12) / 24.0f;
                const float y = slotBottom - t * (slotBottom - slotTop);
                auto cap = juce::Rectangle<float> (x - 9.0f, y - 6.0f, 18.0f, 12.0f);
                g.setColour (th.line.brighter (inLength ? 0.1f : -0.1f));
                g.fillRoundedRectangle (cap, 3.0f);
                g.setColour (c.withAlpha (on && inLength ? 0.9f : 0.3f));
                g.drawRoundedRectangle (cap, 3.0f, 1.0f);
                g.drawHorizontalLine ((int) y, cap.getX() + 4.0f, cap.getRight() - 4.0f);
                // Pitch readout
                g.setColour (on && inLength ? th.text : th.muted.withAlpha (0.6f));
                g.setFont (th.label (7.5f));
                const int p = m.stepPitch[(size_t) s];
                g.drawText (p > 0 ? "+" + juce::String (p) : juce::String (p), juce::Rectangle<float> (x - 14.0f, (float) stepArea.getY(), 28.0f, 12.0f).toNearestInt(),
                            juce::Justification::centred);
                // Gate button with LED
                auto b = stepButton (s);
                g.setColour (on ? c.withAlpha (current ? 0.9f : 0.35f) : th.display);
                g.fillRoundedRectangle (b, 3.0f);
                g.setColour (on ? c : c.withAlpha (inLength ? 0.35f : 0.15f));
                g.drawRoundedRectangle (b.reduced (0.5f), 3.0f, 1.0f);
                if (s == live && node != nullptr)
                {
                    g.setColour (c.withAlpha (0.9f));
                    g.drawRoundedRectangle (b.expanded (3.0f), 4.0f, 1.0f);
                }
            }
            g.setColour (th.muted);
            g.setFont (th.label (6.5f));
            g.drawText ("POS", juce::Rectangle<float> ((float) stepArea.getX() - 26.0f, (float) stepArea.getBottom() - 16.0f, 24.0f, 12.0f).toNearestInt(),
                        juce::Justification::centredRight);
            g.drawText ("PITCH", juce::Rectangle<float> ((float) stepArea.getX() - 30.0f, (slotTop + slotBottom) * 0.5f - 6.0f, 28.0f, 12.0f).toNearestInt(),
                        juce::Justification::centredRight);
            break;
        }
        case SeqType::Euclid:
        {
            const float radius = a.getHeight() * 0.5f - 6.0f;
            const auto centre = a.getCentre();
            const int steps = SeqKnob::count ((float) knobs[1]->getValue(), 1, seqSteps);
            const int hits = juce::jmin (steps, SeqKnob::count ((float) knobs[0]->getValue(), 0, seqSteps));
            const int rotate = SeqKnob::count ((float) knobs[2]->getValue(), 0, seqSteps - 1);
            const int pos = (int) shown[0];
            g.setColour (th.line2);
            g.drawEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, 1.0f);
            for (int s = 0; s < steps; ++s)
            {
                const float ang = juce::MathConstants<float>::twoPi * (float) s / (float) steps;
                const auto p = centre.getPointOnCircumference (radius, ang);
                const int rs = ((s + rotate) % steps + steps) % steps;
                const bool hit = hits > 0 && (rs * hits) % steps < hits;
                drawLed (g, p, 3.0f, c, hit ? (s == pos ? 1.0f : 0.55f) : (s == pos ? 0.25f : 0.0f));
            }
            g.setColour (th.text);
            g.setFont (th.heading (15.0f));
            g.drawText (juce::String (hits) + " / " + juce::String (steps), a.toNearestInt(), juce::Justification::centred);
            break;
        }
        case SeqType::Turing:
        {
            drawDisplayBox (g, a);
            const int bits = (int) shown[0];
            const float pitch = a.getWidth() / 8.0f;
            for (int b = 0; b < 8; ++b)
                drawLed (g, { a.getX() + pitch * (7.5f - (float) b), a.getCentreY() }, 4.0f, c, (bits >> b) & 1 ? 1.0f : 0.0f);
            break;
        }
        case SeqType::Burst:
        {
            drawDisplayBox (g, a);
            const int count = SeqKnob::count ((float) knobs[0]->getValue(), 1, 8);
            const int remaining = (int) shown[0];
            for (int i = 0; i < 8; ++i)
                drawLed (g, { a.getX() + 14.0f + (float) i * (a.getWidth() - 28.0f) / 7.0f, a.getCentreY() }, 3.0f, c,
                         i < count ? (i < count - remaining && clockLed > 0.05f ? 1.0f : 0.45f) : 0.0f);
            break;
        }
        case SeqType::Chance:
        {
            drawDisplayBox (g, a);
            drawLed (g, { a.getX() + 22.0f, a.getCentreY() }, 6.0f, c, shown[0] > 0.5f ? clockLed : 0.0f);
            g.setColour (th.muted);
            g.setFont (th.label (7.0f));
            g.drawText (shown[0] > 0.5f ? "PASS" : "HOLD", a.withTrimmedLeft (40.0f).toNearestInt(), juce::Justification::centredLeft);
            g.setColour (c.withAlpha (0.8f));
            g.setFont (th.label (9.0f));
            g.drawText (juce::String (juce::roundToInt (knobs[0]->getValue() * 100.0)) + "%", a.reduced (10.0f, 0.0f).toNearestInt(), juce::Justification::centredRight);
            break;
        }
        case SeqType::Quantise:
        {
            auto box = a;
            auto name = box.removeFromTop (26.0f);
            drawDisplayBox (g, name);
            const int root = SeqKnob::choice (m.knobs[0], 12), scaleIdx = SeqKnob::choice (m.knobs[1], (int) SeqRack::scales().size());
            const auto& scale = SeqRack::scales()[(size_t) scaleIdx];
            g.setColour (c);
            g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::bold)));
            g.drawText (juce::String (SeqRack::noteName (root)) + " " + juce::String (scale.name).toUpperCase(), name.reduced (10.0f, 0.0f).toNearestInt(),
                        juce::Justification::centredLeft);
            g.setColour (th.muted);
            g.setFont (th.label (6.5f));
            g.drawText ("SCALE", name.reduced (8.0f, 0.0f).toNearestInt(), juce::Justification::centredRight);

            // Mini keyboard: scale notes lit, the note playing brighter
            box.removeFromTop (6.0f);
            const float keyW = box.getWidth() / 12.0f;
            const int current = (int) shown[0];
            for (int n = 0; n < 12; ++n)
            {
                const bool black = n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
                auto key = juce::Rectangle<float> (box.getX() + keyW * (float) n + 1.0f, box.getY() + (black ? 0.0f : 10.0f), keyW - 2.0f, box.getHeight() - (black ? 10.0f : 10.0f));
                const bool inScale = scale.notes[(size_t) (((n - root) % 12 + 12) % 12)];
                g.setColour (inScale ? c.withAlpha (n == current ? 0.8f : 0.22f) : th.display);
                g.fillRoundedRectangle (key, 2.0f);
                g.setColour (inScale ? c.withAlpha (0.8f) : th.line2);
                g.drawRoundedRectangle (key.reduced (0.5f), 2.0f, 1.0f);
                g.setColour (inScale ? th.text : th.muted.withAlpha (0.5f));
                g.setFont (th.label (6.0f));
                g.drawText (SeqRack::noteName (n), key.toNearestInt(), juce::Justification::centredBottom);
            }
            break;
        }
        case SeqType::Mutate:
        {
            drawDisplayBox (g, a);
            const float mid = a.getCentreX();
            const float x = mid + juce::jlimit (-1.0f, 1.0f, shown[0] / 12.0f) * (a.getWidth() * 0.45f);
            g.setColour (th.line2);
            g.drawVerticalLine ((int) mid, a.getY() + 6.0f, a.getBottom() - 6.0f);
            g.setColour (c.withAlpha (0.35f));
            g.fillRect (juce::Rectangle<float> (juce::jmin (mid, x), a.getCentreY() - 3.0f, std::abs (x - mid), 6.0f));
            drawLed (g, { x, a.getCentreY() }, 3.5f, c, 0.9f);
            break;
        }
        case SeqType::Circadian:
        {
            const float radius = juce::jmin (a.getWidth(), a.getHeight()) * 0.5f - 27.0f;
            const auto centre = a.getCentre();
            const float phase = shown[0];
            // Dial with the four quarters of the day
            g.setColour (th.line2);
            g.drawEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, 1.0f);
            for (int t = 0; t < 24; ++t)
            {
                const float ang = juce::MathConstants<float>::twoPi * (float) t / 24.0f + juce::MathConstants<float>::pi;
                const auto p1 = centre.getPointOnCircumference (radius + 3.0f, ang), p2 = centre.getPointOnCircumference (radius + (t % 6 == 0 ? 9.0f : 6.0f), ang);
                g.setColour (th.muted.withAlpha (t % 6 == 0 ? 0.8f : 0.35f));
                g.drawLine (p1.x, p1.y, p2.x, p2.y, 1.0f);
            }
            g.setColour (th.muted);
            g.setFont (th.label (6.5f));
            g.drawText ("NOON", juce::Rectangle<float> (centre.x - 20.0f, centre.y - radius - 20.0f, 40.0f, 10.0f).toNearestInt(), juce::Justification::centred);
            g.drawText ("MIDNIGHT", juce::Rectangle<float> (centre.x - 24.0f, centre.y + radius + 10.0f, 48.0f, 10.0f).toNearestInt(), juce::Justification::centred);
            g.drawText ("DAWN", juce::Rectangle<float> (centre.x - radius - 38.0f, centre.y - 5.0f, 26.0f, 10.0f).toNearestInt(), juce::Justification::centredRight);
            g.drawText ("DUSK", juce::Rectangle<float> (centre.x + radius + 12.0f, centre.y - 5.0f, 24.0f, 10.0f).toNearestInt(), juce::Justification::centredLeft);
            // Arc of the day so far, and the sun / moon dot
            juce::Path arc;
            arc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, juce::MathConstants<float>::pi, juce::MathConstants<float>::pi + phase * juce::MathConstants<float>::twoPi, true);
            Glow::strokeGlow (g, arc, c.withAlpha (0.7f), 1.4f, shown[1]);
            const auto dot = centre.getPointOnCircumference (radius, juce::MathConstants<float>::pi + phase * juce::MathConstants<float>::twoPi);
            drawLed (g, dot, 4.0f, c, 0.4f + 0.6f * shown[1]);
            // Readout: wall time or bar, and the part of the day
            const int follow = SeqKnob::choice (m.knobs[4], 3);
            juce::String time;
            if (follow == 0)
                time = "BAR " + juce::String ((int) (ctx.playheadBeats() / beatsPerBar) + 1);
            else
                time = juce::Time::getCurrentTime().formatted ("%H:%M");
            const char* part = phase < 0.2f ? "NIGHT" : phase < 0.3f ? "DAWN" : phase < 0.45f ? "MORNING" : phase < 0.55f ? "NOON"
                             : phase < 0.7f ? "AFTERNOON" : phase < 0.8f ? "DUSK" : "NIGHT";
            g.setColour (th.text);
            g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain)));
            g.drawText (time, juce::Rectangle<float> (centre.x - 40.0f, centre.y - 14.0f, 80.0f, 16.0f).toNearestInt(), juce::Justification::centred);
            g.setColour (c.withAlpha (0.85f));
            g.setFont (th.label (6.5f));
            g.drawText (part, juce::Rectangle<float> (centre.x - 40.0f, centre.y + 3.0f, 80.0f, 10.0f).toNearestInt(), juce::Justification::centred);
            break;
        }
        case SeqType::Tide:
        {
            drawDisplayBox (g, a);
            auto scope = a.reduced (8.0f, 6.0f);
            const int shape = SeqKnob::choice (m.knobs[1], 5);
            juce::Path p;
            for (float x = 0.0f; x <= scope.getWidth(); x += 2.0f)
            {
                const float t = x / scope.getWidth();
                float v;
                switch (shape)
                {
                    case 0:  v = 0.5f - 0.5f * std::cos (t * juce::MathConstants<float>::twoPi); break;
                    case 1:  v = 1.0f - std::abs (t * 2.0f - 1.0f); break;
                    case 2:  v = t; break;
                    case 3:  v = t < 0.5f ? 1.0f : 0.0f; break;
                    default: v = (float) ((int) (t * 4.0f) * 37 % 11) / 10.0f; break;
                }
                const float y = scope.getBottom() - v * scope.getHeight();
                if (x < 0.5f) p.startNewSubPath (scope.getX(), y); else p.lineTo (scope.getX() + x, y);
            }
            Glow::strokeGlow (g, p, c.withAlpha (0.6f), 1.0f);
            const float px = scope.getX() + shown[0] * scope.getWidth();
            drawLed (g, { px, scope.getBottom() - shown[1] * scope.getHeight() }, 3.0f, c, 1.0f);
            break;
        }
        case SeqType::Out:
        {
            drawDisplayBox (g, a);
            auto top = a.removeFromTop (a.getHeight() * 0.5f);
            g.setColour (th.text);
            g.setFont (th.body (12.0f));
            g.drawText (ctx.project.tracks[(size_t) trackIndex]->name, top.reduced (10.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft);
            g.setColour (th.muted);
            g.setFont (th.label (6.5f));
            g.drawText ("TRACK", top.reduced (8.0f, 0.0f).toNearestInt(), juce::Justification::centredRight);
            const int note = 60 + juce::roundToInt (shown[1]);
            g.setColour (c);
            g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain)));
            g.drawText (juce::String (SeqRack::noteName (note)) + juce::String (note / 12 - 2), a.reduced (10.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft);
            drawLed (g, { a.getRight() - 16.0f, a.getCentreY() }, 4.0f, c, gateLed);
            break;
        }
    }
}

//==============================================================================
SeqCableLayer::SeqCableLayer (AppContext& c, std::function<SeqModuleView* (int)> f) : ctx (c), viewFor (std::move (f))
{
    setRepaintsOnMouseActivity (false);
}

bool SeqCableLayer::isInput (int module, int port) const
{
    if (rack != nullptr)
        if (auto* m = rack->find (module))
        {
            const auto& ports = seqInfo (m->type).ports;
            return port >= 0 && port < (int) ports.size() && ports[(size_t) port].input;
        }
    return false;
}

juce::Point<float> SeqCableLayer::jackPos (int module, int port) const
{
    if (auto* v = viewFor (module))
        return v->getPosition().toFloat() + v->jackPosition (port);
    return {};
}

juce::Colour SeqCableLayer::colourFor (int module) const
{
    if (rack != nullptr)
        if (auto* m = rack->find (module))
            return Theme::get().pal (seqInfo (m->type).pal);
    return Theme::get().pal (Pal::Green);
}

SeqCableLayer::JackRef SeqCableLayer::jackAt (juce::Point<float> p, float radius) const
{
    if (rack == nullptr)
        return {};
    JackRef best;
    float bestDist = radius;
    for (auto& m : rack->modules)
        if (auto* v = viewFor (m.id))
            for (int port = 0; port < v->numPorts(); ++port)
            {
                const float d = jackPos (m.id, port).getDistanceFrom (p);
                if (d < bestDist) { bestDist = d; best = { m.id, port }; }
            }
    return best;
}

juce::Path SeqCableLayer::cablePath (juce::Point<float> a, juce::Point<float> b)
{
    // Patch leads hang from the jacks: a short loop between neighbours, a deeper drape across the rack
    const float dx = std::abs (b.x - a.x);
    const float sag = juce::jmin ((float) SequencerPanel::sag + 20.0f, 22.0f + dx * 0.12f);
    juce::Path p;
    p.startNewSubPath (a);
    p.cubicTo (a.translated (0.0f, sag * 1.6f), b.translated (0.0f, sag * 1.6f), b);
    return p;
}

juce::Path SeqCableLayer::cablePath (const SeqCable& c) const
{
    return cablePath (jackPos (c.fromModule, c.fromPort), jackPos (c.toModule, c.toPort));
}

int SeqCableLayer::cableAt (juce::Point<float> p) const
{
    if (rack == nullptr)
        return -1;
    for (int i = (int) rack->cables.size() - 1; i >= 0; --i)
    {
        juce::Point<float> nearest;
        cablePath (rack->cables[(size_t) i]).getNearestPoint (p, nearest);
        if (nearest.getDistanceFrom (p) < 5.0f)
            return i;
    }
    return -1;
}

bool SeqCableLayer::canConnect (JackRef a, JackRef b) const
{
    if (! a.valid() || ! b.valid() || a.module == b.module)
        return false;
    return isInput (a.module, a.port) != isInput (b.module, b.port);
}

bool SeqCableLayer::hitTest (int x, int y)
{
    const auto p = juce::Point<int> (x, y).toFloat();
    return dragging || jackAt (p).valid() || cableAt (p) >= 0;
}

void SeqCableLayer::mouseMove (const juce::MouseEvent& e)
{
    const int c = jackAt (e.position).valid() ? -1 : cableAt (e.position);
    if (c != hoverCable) { hoverCable = c; repaint(); }
    setMouseCursor (jackAt (e.position).valid() ? juce::MouseCursor::CrosshairCursor
                    : (c >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor));
}

void SeqCableLayer::mouseDown (const juce::MouseEvent& e)
{
    if (rack == nullptr)
        return;
    const auto jack = jackAt (e.position);
    if (! jack.valid())
    {
        const int c = cableAt (e.position);
        if (c >= 0 && e.mods.isPopupMenu())
        {
            juce::PopupMenu menu;
            menu.addItem (1, "Remove cable");
            menu.showMenuAsync (juce::PopupMenu::Options(), [this, c] (int result)
            {
                if (result == 1 && rack != nullptr)
                {
                    rack->disconnect ((size_t) c);
                    hoverCable = -1;
                    if (onChanged) onChanged();
                }
            });
        }
        return;
    }
    dragging = true;
    dragEnd = e.position;
    if (isInput (jack.module, jack.port))
    {
        const int existing = rack->cableInto (jack.module, jack.port);
        if (existing >= 0)
        {
            const auto c = rack->cables[(size_t) existing];
            rack->disconnect ((size_t) existing);
            dragFrom = { c.fromModule, c.fromPort };
            if (onChanged) onChanged();
            return;
        }
    }
    dragFrom = jack;
}

void SeqCableLayer::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging)
        return;
    dragEnd = e.position;
    auto j = jackAt (e.position, 14.0f);
    hoverJack = canConnect (dragFrom, j) ? j : JackRef {};
    repaint();
}

void SeqCableLayer::mouseUp (const juce::MouseEvent&)
{
    if (! dragging)
        return;
    dragging = false;
    if (rack != nullptr && hoverJack.valid())
    {
        const bool fromIsOutput = ! isInput (dragFrom.module, dragFrom.port);
        const auto out = fromIsOutput ? dragFrom : hoverJack;
        const auto in  = fromIsOutput ? hoverJack : dragFrom;
        rack->connect (out.module, out.port, in.module, in.port);
    }
    dragFrom = hoverJack = {};
    if (onChanged) onChanged();
    repaint();
}

void SeqCableLayer::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (rack == nullptr || jackAt (e.position).valid())
        return;
    const int c = cableAt (e.position);
    if (c >= 0)
    {
        rack->disconnect ((size_t) c);
        hoverCable = -1;
        if (onChanged) onChanged();
    }
}

void SeqCableLayer::paint (juce::Graphics& g)
{
    if (rack == nullptr)
        return;
    auto& th = Theme::get();
    const bool rolling = ctx.selected().rolling.load();

    for (size_t i = 0; i < rack->cables.size(); ++i)
    {
        const auto& c = rack->cables[(size_t) i];
        const auto path = cablePath (c);
        const auto col = colourFor (c.fromModule);
        const bool hot = (int) i == hoverCable;
        const bool gate = seqInfo (rack->find (c.fromModule)->type).ports[(size_t) c.fromPort].gate;
        if (gate)
        {
            // Gate leads: thinner, with a plug-coloured core
            g.setColour (col.withAlpha (0.16f));
            g.strokePath (path, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (hot ? col.brighter (0.4f) : col.withAlpha (0.9f));
            g.strokePath (path, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        else
            Glow::strokeGlow (g, path, hot ? col.brighter (0.4f) : col, hot ? 2.4f : 2.0f, rolling ? 0.4f : 0.0f);
    }

    if (dragging && dragFrom.valid())
    {
        const auto a = jackPos (dragFrom.module, dragFrom.port);
        const auto b = hoverJack.valid() ? jackPos (hoverJack.module, hoverJack.port) : dragEnd;
        Glow::strokeGlow (g, cablePath (a, b), colourFor (dragFrom.module).withAlpha (0.9f), 1.8f, 1.0f);
    }

    // Jack rings: filled when plugged, lit when a dragged lead can land
    for (auto& m : rack->modules)
    {
        auto* v = viewFor (m.id);
        if (v == nullptr) continue;
        const auto& inf = seqInfo (m.type);
        const auto col = th.pal (inf.pal);
        for (int port = 0; port < v->numPorts(); ++port)
        {
            const auto p = jackPos (m.id, port);
            const float r = inf.ports[(size_t) port].gate ? 5.0f : 6.0f;
            bool plugged = false;
            for (auto& c : rack->cables)
                plugged = plugged || (c.fromModule == m.id && c.fromPort == port) || (c.toModule == m.id && c.toPort == port);
            const bool target = dragging && canConnect (dragFrom, { m.id, port });
            const bool lit = target && hoverJack.module == m.id && hoverJack.port == port;
            g.setColour (th.display);
            g.fillEllipse (p.x - r - 2.0f, p.y - r - 2.0f, (r + 2.0f) * 2.0f, (r + 2.0f) * 2.0f);
            if (lit)
            {
                g.setColour (col.withAlpha (0.35f));
                g.fillEllipse (p.x - r - 5.0f, p.y - r - 5.0f, (r + 5.0f) * 2.0f, (r + 5.0f) * 2.0f);
            }
            g.setColour (target || plugged ? col : col.withAlpha (0.4f));
            g.drawEllipse (p.x - r, p.y - r, r * 2.0f, r * 2.0f, target ? 1.6f : 1.1f);
            g.setColour (th.line.withAlpha (0.8f));
            g.drawEllipse (p.x - r + 2.5f, p.y - r + 2.5f, (r - 2.5f) * 2.0f, (r - 2.5f) * 2.0f, 0.8f);
            if (plugged)
            {
                g.setColour (col);
                g.fillEllipse (p.x - r * 0.5f, p.y - r * 0.5f, r, r);
            }
        }
    }
}

//==============================================================================
void SequencerPanel::Rail::paint (juce::Graphics& g)
{
    auto& th = Theme::get();
    g.fillAll (th.bg);
    // Rails top and bottom, like the case
    for (float y : { 1.0f, (float) SequencerPanel::railHeight + 7.0f })
    {
        g.setColour (th.line2);
        g.fillRect (0.0f, y, (float) getWidth(), 2.0f);
        g.setColour (th.line.withAlpha (0.35f));
        for (float x = 12.0f; x < (float) getWidth(); x += 28.0f)
            g.fillEllipse (x, y - 0.5f, 3.0f, 3.0f);
    }
}

SequencerPanel::SequencerPanel (AppContext& c) : ctx (c)
{
    addButton.getProperties().set ("pal", (int) Pal::Green);
    addButton.onClick = [this]
    {
        juce::PopupMenu menu;
        const SeqType order[] = { SeqType::Steps, SeqType::Euclid, SeqType::Turing, SeqType::Burst, SeqType::Chance,
                                  SeqType::Quantise, SeqType::Mutate, SeqType::Circadian, SeqType::Tide };
        for (auto t : order)
            menu.addItem ((int) t + 1, seqInfo (t).title + "   -  " + seqInfo (t).blurb);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (addButton), [this] (int r)
        {
            if (r > 0) addModule ((SeqType) (r - 1));
        });
    };
    clipsButton.getProperties().set ("pal", (int) Pal::Gold);
    clipsButton.onClick = [this]
    {
        ctx.selected().rolling = false;
        ctx.clipsChanged();
    };
    addAndMakeVisible (addButton);
    addAndMakeVisible (clipsButton);

    cables = std::make_unique<SeqCableLayer> (ctx, [this] (int id) { return viewFor (id); });
    cables->onChanged = [this] { changed(); };
    rail.addAndMakeVisible (*cables);
    viewport.setViewedComponent (&rail, false);
    viewport.setScrollBarsShown (false, true);
    addAndMakeVisible (viewport);
    ctx.addChangeListener (this);
    rebuild();
}

SequencerPanel::~SequencerPanel()
{
    ctx.removeChangeListener (this);
}

void SequencerPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (builtFor != ctx.selectedTrack || builtGeneration != ctx.generation)
        rebuild();
}

SeqModuleView* SequencerPanel::viewFor (int moduleId)
{
    for (auto* v : views)
        if (v->getModuleId() == moduleId) return v;
    return nullptr;
}

void SequencerPanel::changed()
{
    ctx.engine.rebuildSequencer (ctx.selectedTrack);
    ctx.touched();
    rebuild();
}

void SequencerPanel::addModule (SeqType t)
{
    ctx.selected().seq.add (t);
    changed();
}

void SequencerPanel::rebuild()
{
    builtFor = ctx.selectedTrack;
    builtGeneration = ctx.generation;
    auto& track = ctx.selected();
    views.clear();
    for (auto& m : track.seq.modules)
    {
        auto* v = views.add (new SeqModuleView (ctx, track.seq, ctx.selectedTrack, m.id));
        const int id = m.id;
        v->onRemove = [this, id] { ctx.selected().seq.remove (id); changed(); };
        v->onMoveLeft = [this, id] { auto& s = ctx.selected().seq; s.move (id, s.indexOf (id) - 1); changed(); };
        v->onMoveRight = [this, id] { auto& s = ctx.selected().seq; s.move (id, s.indexOf (id) + 1); changed(); };
        rail.addAndMakeVisible (v);
    }
    cables->setRack (&track.seq);
    cables->toFront (false);
    layoutRail();
    repaint();
}

void SequencerPanel::layoutRail()
{
    int x = 10;
    for (auto* v : views)
    {
        v->setTopLeftPosition (x, 4);
        x += v->getWidth() + 8;
    }
    const int w = juce::jmax (x + 10, viewport.getWidth());
    rail.setSize (w, railHeight + sag + 8);
    cables->setBounds (rail.getLocalBounds());
}

void SequencerPanel::resized()
{
    auto r = getLocalBounds();
    auto head = r.removeFromTop (headerHeight).reduced (12, 3);
    clipsButton.setBounds (head.removeFromRight (120));
    head.removeFromRight (8);
    addButton.setBounds (head.removeFromRight (120));
    viewport.setBounds (r);
    layoutRail();
}

void SequencerPanel::tick()
{
    for (auto* v : views)
        v->tick();
    cables->repaint();
}

void SequencerPanel::paint (juce::Graphics& g)
{
    auto& th = Theme::get();
    g.fillAll (th.panel);
    g.setColour (th.line2);
    g.drawHorizontalLine (0, 0.0f, (float) getWidth());
    g.drawHorizontalLine (headerHeight - 1, 0.0f, (float) getWidth());

    auto head = getLocalBounds().removeFromTop (headerHeight).reduced (12, 0).toFloat();
    g.setColour (th.text);
    Glow::drawSpacedText (g, "SEQUENCE", head.removeFromLeft (90.0f), th.heading (13.0f), 2.5f, juce::Justification::centredLeft);
    auto& t = ctx.selected();
    const auto dot = juce::String::fromUTF8 ("  \xc2\xb7  ");
    juce::String sub = t.name + dot + "rolling" + dot + juce::String (t.seq.modules.size()) + " modules" + dot + "patch as you like";
    g.setColour (th.muted);
    g.setFont (th.body (11.5f));
    g.drawText (sub, head.toNearestInt(), juce::Justification::centredLeft);
}
