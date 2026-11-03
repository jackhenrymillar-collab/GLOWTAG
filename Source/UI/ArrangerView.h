#pragma once

#include "AppContext.h"
#include "Widgets.h"

// The arranger timeline: bar ruler, grid, semi-transparent clips and the playhead.
class ArrangerView : public juce::Component
{
public:
    explicit ArrangerView (AppContext&);

    void tick();
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    std::function<void()> onOpenEditor;

private:
    AppContext& ctx;
    float barWidth() const { return (float) getWidth() / (float) Project::numBars; }
    double lastPlayhead = -1.0;
};
