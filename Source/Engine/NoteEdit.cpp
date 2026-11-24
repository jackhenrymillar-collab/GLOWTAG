#include "NoteEdit.h"
#include <algorithm>

namespace NoteEdit
{
juce::String gridName (int i)
{
    i = juce::jlimit (0, numGrids - 1, i);
    const double s = gridSteps[i];
    if (s >= beatsPerBar) return "1 bar";
    return "1/" + juce::String (juce::roundToInt (beatsPerBar / s));
}

int finer (int i)   { return juce::jlimit (0, numGrids - 1, i + 1); }
int coarser (int i) { return juce::jlimit (0, numGrids - 1, i - 1); }

double changedLength (double loop, int direction)
{
    // Land on the half-bar line first when the clip is between two
    const double steps = loop / lengthStep;
    double n = direction > 0 ? std::floor (steps + 1e-6) + 1.0 : std::ceil (steps - 1e-6) - 1.0;
    return juce::jlimit (lengthStep, 64.0 * beatsPerBar, n * lengthStep);
}

juce::String lengthName (double loop)
{
    const double bars = loop / beatsPerBar;
    const bool whole = std::abs (bars - std::round (bars)) < 1e-6;
    const auto num = whole ? juce::String (juce::roundToInt (bars)) : juce::String (bars, 1);
    return num + (whole && juce::roundToInt (bars) == 1 ? " bar" : " bars");
}

std::vector<int> all (const std::vector<Note>& notes)
{
    std::vector<int> s ((size_t) notes.size());
    for (size_t i = 0; i < s.size(); ++i) s[i] = (int) i;
    return s;
}

std::vector<int> inBox (const std::vector<Note>& notes, double a, double b, int lo, int hi)
{
    if (a > b) std::swap (a, b);
    if (lo > hi) std::swap (lo, hi);
    std::vector<int> s;
    for (size_t i = 0; i < notes.size(); ++i)
    {
        const auto& n = notes[i];
        if (n.pitch >= lo && n.pitch <= hi && n.start < b && n.start + n.length > a)
            s.push_back ((int) i);
    }
    return s;
}

static std::vector<int> valid (const std::vector<Note>& notes, const std::vector<int>& sel)
{
    std::vector<int> s;
    for (int i : sel)
        if (i >= 0 && i < (int) notes.size()) s.push_back (i);
    std::sort (s.begin(), s.end());
    s.erase (std::unique (s.begin(), s.end()), s.end());
    return s;
}

void quantise (std::vector<Note>& notes, const std::vector<int>& sel, double step, double loop)
{
    for (int i : sel.empty() ? all (notes) : valid (notes, sel))
    {
        auto& n = notes[(size_t) i];
        double s = std::round (n.start / step) * step;
        if (s >= loop - 1e-9) s = juce::jmax (0.0, loop - step);   // the last step stays inside the loop
        n.start = juce::jmax (0.0, s);
    }
}

std::vector<int> duplicate (std::vector<Note>& notes, const std::vector<int>& selIn, double step, double& loop)
{
    const auto sel = valid (notes, selIn);
    if (sel.empty())
        return {};
    double lo = 1e9, hi = -1e9;
    for (int i : sel)
    {
        lo = juce::jmin (lo, notes[(size_t) i].start);
        hi = juce::jmax (hi, notes[(size_t) i].start + notes[(size_t) i].length);
    }
    const double gap = juce::jmax (step, std::ceil ((hi - lo) / step - 1e-6) * step);
    std::vector<int> copies;
    double furthest = 0.0;
    for (int i : sel)
    {
        Note n = notes[(size_t) i];
        n.start += gap;
        furthest = juce::jmax (furthest, n.start + n.length);
        notes.push_back (n);
        copies.push_back ((int) notes.size() - 1);
    }
    if (furthest > loop + 1e-6)
        loop = juce::jmin (64.0 * beatsPerBar, std::ceil (furthest / beatsPerBar - 1e-6) * beatsPerBar);
    return copies;
}

bool transpose (std::vector<Note>& notes, const std::vector<int>& selIn, int semis)
{
    const auto sel = valid (notes, selIn);
    if (sel.empty()) return false;
    for (int i : sel)
    {
        const int p = notes[(size_t) i].pitch + semis;
        if (p < 0 || p > 127) return false;
    }
    for (int i : sel) notes[(size_t) i].pitch += semis;
    return true;
}

void nudgeVelocity (std::vector<Note>& notes, const std::vector<int>& sel, float delta)
{
    for (int i : valid (notes, sel))
        notes[(size_t) i].velocity = juce::jlimit (0.05f, 1.0f, notes[(size_t) i].velocity + delta);
}

void erase (std::vector<Note>& notes, std::vector<int> sel)
{
    sel = valid (notes, sel);
    for (auto it = sel.rbegin(); it != sel.rend(); ++it)
        notes.erase (notes.begin() + *it);
}
}
