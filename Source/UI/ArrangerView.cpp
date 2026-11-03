#include "ArrangerView.h"

ArrangerView::ArrangerView (AppContext& c) : ctx (c) {}

void ArrangerView::tick()
{
    const double ph = ctx.playheadBars();
    if (! juce::exactlyEqual (ph, lastPlayhead))
    {
        lastPlayhead = ph;
        repaint();
    }
}

void ArrangerView::mouseDown (const juce::MouseEvent& e)
{
    if (e.y < Layout::rulerHeight)
        return;
    const int i = (e.y - Layout::rulerHeight) / Layout::rowHeight;
    if (i < (int) ctx.project.tracks.size())
        ctx.selectTrack (i);
}

void ArrangerView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.y >= Layout::rulerHeight && onOpenEditor)
        onOpenEditor();
}

void ArrangerView::paint (juce::Graphics& g)
{
    auto& th = Theme::get();
    const float bw = barWidth();
    const float w = (float) getWidth();
    const float h = (float) getHeight();
    const auto accent = th.pal (Pal::Green);

    g.fillAll (th.bg);

    // Ruler
    auto ruler = juce::Rectangle<float> (0.0f, 0.0f, w, (float) Layout::rulerHeight);
    g.setColour (th.panel);
    g.fillRect (ruler);
    g.setColour (accent.withAlpha (0.10f));
    g.fillRect (ruler.withTrimmedTop (ruler.getHeight() - 5.0f));          // loop region
    g.setColour (accent.withAlpha (0.6f));
    g.drawHorizontalLine ((int) ruler.getBottom() - 5, 0.0f, w);

    for (int bar = 0; bar < Project::numBars; ++bar)
    {
        const float x = (float) bar * bw;
        g.setColour (th.muted);
        g.setFont (th.label (9.5f));
        g.drawText (juce::String (bar + 1), juce::Rectangle<float> (x + 5.0f, 2.0f, 30.0f, 16.0f).toNearestInt(),
                    juce::Justification::centredLeft);
        g.setColour (th.line);
        g.drawVerticalLine ((int) x, 4.0f, ruler.getBottom());
    }

    // Lanes and grid
    const int numTracks = (int) ctx.project.tracks.size();
    for (int i = 0; i < numTracks; ++i)
    {
        const float y = (float) (Layout::rulerHeight + i * Layout::rowHeight);
        if (i == ctx.selectedTrack)
        {
            g.setColour (th.pal (ctx.selected().colourIndex).withAlpha (0.05f));
            g.fillRect (0.0f, y, w, (float) Layout::rowHeight);
        }
        g.setColour (th.line2);
        g.drawHorizontalLine ((int) y + Layout::rowHeight - 1, 0.0f, w);
    }

    for (int beat = 0; beat < Project::numBars * 4; ++beat)
    {
        const float x = (float) beat * bw / 4.0f;
        g.setColour (beat % 4 == 0 ? th.line : th.line2.withAlpha (0.6f));
        g.drawVerticalLine ((int) x, ruler.getBottom(), h);
    }

    // Clips: translucent fill, thin outline, name, and a hint of the pattern inside
    for (int i = 0; i < numTracks; ++i)
    {
        auto& t = *ctx.project.tracks[(size_t) i];
        const auto c = th.pal (t.colourIndex);
        const bool audible = ctx.project.isAudible (i);
        const float y = (float) (Layout::rulerHeight + i * Layout::rowHeight);

        for (auto& clip : t.clips)
        {
            auto r = juce::Rectangle<float> ((float) clip.startBar * bw, y, (float) clip.lengthBars * bw,
                                             (float) Layout::rowHeight).reduced (2.0f, 4.0f);
            const float alpha = audible ? 1.0f : 0.4f;

            g.setColour (c.withAlpha (0.16f * alpha));
            g.fillRoundedRectangle (r, 5.0f);
            g.setColour (c.withAlpha (0.75f * alpha));
            g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);

            g.setColour (th.text.withAlpha (0.9f * alpha));
            g.setFont (th.label (9.0f));
            g.drawText (clip.name.toUpperCase(), r.reduced (7.0f, 3.0f).removeFromTop (12.0f).toNearestInt(),
                        juce::Justification::centredLeft, true);

            // Pattern hits as small glowing ticks
            if (t.kind == TrackKind::Automation)
            {
                juce::Path ramp;
                ramp.startNewSubPath (r.getX() + 4.0f, r.getBottom() - 4.0f);
                ramp.lineTo (r.getRight() - 4.0f, r.getY() + 16.0f);
                Glow::strokeGlow (g, ramp, c.withAlpha (0.8f * alpha), 1.0f);
            }
            else
            {
                const float stepW = bw / (float) Project::stepsPerBar;
                g.setColour (c.withAlpha (0.85f * alpha));
                for (int bar = 0; bar < clip.lengthBars; ++bar)
                    for (int s = 0; s < Project::stepsPerBar; ++s)
                        if (t.stepOn (s))
                        {
                            const float x = r.getX() + ((float) bar * Project::stepsPerBar + (float) s) * stepW + 1.0f;
                            if (x < r.getRight() - 2.0f)
                                g.fillRoundedRectangle (x, r.getBottom() - 13.0f, juce::jmax (1.5f, stepW - 2.0f), 8.0f, 1.0f);
                        }
            }
        }
    }

    // Playhead
    const double ph = ctx.playheadBars();
    if (ph >= 0.0)
    {
        const float x = (float) ph * bw;
        juce::Path line;
        line.startNewSubPath (x, 0.0f);
        line.lineTo (x, h);
        Glow::strokeGlow (g, line, accent, 1.0f, ctx.engine.getPulse());
        juce::Path cap;
        cap.addTriangle (x - 5.0f, 0.0f, x + 5.0f, 0.0f, x, 7.0f);
        g.setColour (accent);
        g.fillPath (cap);
    }
}
