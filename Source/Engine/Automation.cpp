#include "Automation.h"
#include <algorithm>
#include <cmath>

namespace
{
    constexpr double edge = 0.001;      // how close a held edge sits to a cut (beats)
}

float AutoLane::raw (float v) const
{
    const float r = lo + juce::jlimit (0.0f, 1.0f, v) * (hi - lo);
    return stepped ? std::round (r) : r;
}

float AutoLane::norm (float r) const
{
    return hi > lo ? juce::jlimit (0.0f, 1.0f, (r - lo) / (hi - lo)) : 0.0f;
}

namespace Auto
{
float shape (float u, float curve)
{
    u = juce::jlimit (0.0f, 1.0f, u);
    if (std::abs (curve) < 1.0e-4f) return u;
    const float e = std::pow (2.0f, juce::jlimit (-1.0f, 1.0f, curve) * 3.0f);
    return std::pow (u, e);
}

float curveFor (float mid)
{
    mid = juce::jlimit (0.01f, 0.99f, mid);
    const float e = std::log (mid) / std::log (0.5f);
    return juce::jlimit (-1.0f, 1.0f, std::log2 (e) / 3.0f);
}

float valueAt (const std::vector<AutoPoint>& p, double beat)
{
    if (p.empty()) return -1.0f;
    if (beat <= p.front().beat) return p.front().value;
    if (beat >= p.back().beat) return p.back().value;
    auto it = std::upper_bound (p.begin(), p.end(), beat, [] (double b, const AutoPoint& q) { return b < q.beat; });
    const auto& b = *it;
    const auto& a = *(it - 1);
    const double span = b.beat - a.beat;
    const float u = span > 1.0e-9 ? (float) ((beat - a.beat) / span) : 1.0f;
    return a.value + (b.value - a.value) * shape (u, a.curve);
}

void sort (std::vector<AutoPoint>& p)
{
    std::stable_sort (p.begin(), p.end(), [] (const AutoPoint& a, const AutoPoint& b) { return a.beat < b.beat; });
}

int add (std::vector<AutoPoint>& p, double beat, float value)
{
    AutoPoint q { juce::jmax (0.0, beat), juce::jlimit (0.0f, 1.0f, value), 0.0f };
    auto it = std::upper_bound (p.begin(), p.end(), q.beat, [] (double b, const AutoPoint& x) { return b < x.beat; });
    const auto index = it - p.begin();      // before inserting: the insert may move the points
    p.insert (it, q);
    return (int) index;
}

void removeRange (std::vector<AutoPoint>& p, double from, double to)
{
    p.erase (std::remove_if (p.begin(), p.end(), [&] (const AutoPoint& q) { return q.beat >= from - 1.0e-9 && q.beat <= to + 1.0e-9; }), p.end());
}

std::vector<AutoPoint> slice (const std::vector<AutoPoint>& p, double a, double b)
{
    std::vector<AutoPoint> out;
    if (p.empty() || b <= a) return out;
    // The curve the stretch starts in, so the copy bends the same way
    float startCurve = 0.0f;
    for (auto& q : p) if (q.beat <= a) startCurve = q.curve;
    out.push_back ({ 0.0, valueAt (p, a), startCurve });
    for (auto& q : p)
        if (q.beat > a + edge && q.beat < b - edge)
            out.push_back ({ q.beat - a, q.value, q.curve });
    out.push_back ({ b - a - edge, valueAt (p, b - edge), 0.0f });
    // Thinning removes the end points a straight lane does not need
    if (out.size() == 2 && std::abs (out[0].value - out[1].value) < 1.0e-6f && std::abs (out[0].curve) < 1.0e-6f)
        out.pop_back();
    return out;
}

void paste (std::vector<AutoPoint>& p, const std::vector<AutoPoint>& rel, double at, double length)
{
    if (rel.empty()) return;
    const bool had = ! p.empty();
    const float before = had ? valueAt (p, at - edge) : 0.0f;
    const float after  = had ? valueAt (p, at + length + edge) : 0.0f;
    const bool anyBefore = had && p.front().beat < at;
    const bool anyAfter  = had && p.back().beat > at + length;
    removeRange (p, at, at + length);
    if (anyBefore && at > edge) add (p, at - edge, before);
    for (auto& q : rel)
        if (q.beat < length + 1.0e-9)
        {
            const int i = add (p, at + q.beat, q.value);
            p[(size_t) i].curve = q.curve;
        }
    // Whatever came after picks up where it was; a lane that only had this stretch holds its last value
    if (anyAfter) add (p, at + length + edge, after);
}

void moveRange (std::vector<AutoPoint>& p, double a, double b, double to)
{
    if (p.empty() || std::abs (to - a) < 1.0e-9) return;
    const auto rel = slice (p, a, b);
    removeRange (p, a, b - edge * 0.5);
    paste (p, rel, to, b - a);
}

void copyRange (std::vector<AutoPoint>& p, double a, double b, double to)
{
    if (p.empty()) return;
    const auto rel = slice (p, a, b);
    paste (p, rel, to, b - a);
}

void thin (std::vector<AutoPoint>& p, double from, double to, float tolerance)
{
    // Ramer-Douglas-Peucker over the points inside the stretch, measured in value
    std::vector<size_t> idx;
    for (size_t i = 0; i < p.size(); ++i)
        if (p[i].beat >= from - 1.0e-9 && p[i].beat <= to + 1.0e-9) idx.push_back (i);
    if (idx.size() < 3) return;
    std::vector<bool> keep (idx.size(), false);
    keep.front() = keep.back() = true;
    std::vector<std::pair<size_t, size_t>> stack { { 0, idx.size() - 1 } };
    while (! stack.empty())
    {
        auto [s, e] = stack.back();
        stack.pop_back();
        const auto& A = p[idx[s]];
        const auto& B = p[idx[e]];
        float worst = 0.0f;
        size_t at = s;
        for (size_t k = s + 1; k < e; ++k)
        {
            const auto& q = p[idx[k]];
            const double span = B.beat - A.beat;
            const float lineV = span > 1.0e-9 ? A.value + (B.value - A.value) * (float) ((q.beat - A.beat) / span) : A.value;
            const float d = std::abs (q.value - lineV);
            if (d > worst) { worst = d; at = k; }
        }
        if (worst > tolerance && at > s)
        {
            keep[at] = true;
            stack.push_back ({ s, at });
            stack.push_back ({ at, e });
        }
    }
    std::vector<AutoPoint> out;
    size_t k = 0;
    for (size_t i = 0; i < p.size(); ++i)
    {
        if (k < idx.size() && idx[k] == i) { if (keep[k]) out.push_back (p[i]); ++k; }
        else out.push_back (p[i]);
    }
    p.swap (out);
}

AutoLane* find (std::vector<AutoLane>& lanes, const AutoTarget& t)
{
    for (auto& l : lanes) if (l.target == t) return &l;
    return nullptr;
}

const AutoLane* find (const std::vector<AutoLane>& lanes, const AutoTarget& t)
{
    for (auto& l : lanes) if (l.target == t) return &l;
    return nullptr;
}

AutoLane& get (std::vector<AutoLane>& lanes, const AutoTarget& t)
{
    if (auto* l = find (lanes, t)) return *l;
    AutoLane l;
    l.target = t;
    lanes.push_back (l);
    return lanes.back();
}

void clipMoved (std::vector<AutoLane>& lanes, double a, double b, double to)
{
    for (auto& l : lanes) moveRange (l.points, a, b, to);
}

void clipCopied (std::vector<AutoLane>& lanes, double a, double b, double to)
{
    for (auto& l : lanes) copyRange (l.points, a, b, to);
}

bool morphValue (const AutoLane& l, int param, float v, float& out)
{
    const int n = (int) l.snaps.size();
    if (n < 2) return false;
    const float x = juce::jlimit (0.0f, 1.0f, v) * (float) (n - 1);
    const int i = juce::jlimit (0, n - 2, (int) std::floor (x));
    const float f = x - (float) i;
    auto lookup = [param] (const MorphSnap& s, float& r) { for (auto& [k, val] : s.values) if (k == param) { r = val; return true; } return false; };
    float a = 0.0f, b = 0.0f;
    if (! lookup (l.snaps[(size_t) i], a) || ! lookup (l.snaps[(size_t) i + 1], b)) return false;
    const bool stepped = std::find (l.morphStepped.begin(), l.morphStepped.end(), param) != l.morphStepped.end();
    out = stepped ? (f < 0.5f ? a : b) : a + (b - a) * f;
    return true;
}

juce::var toVar (const AutoTarget& t)
{
    return juce::Array<juce::var> { t.kind, t.module, t.param };
}

AutoTarget targetFromVar (const juce::var& v)
{
    AutoTarget t;
    if (auto* a = v.getArray(); a != nullptr && a->size() == 3)
    {
        t.kind = juce::jlimit (0, (int) AutoTarget::morph, (int) (*a)[0]);
        t.module = (int) (*a)[1];
        t.param = (int) (*a)[2];
    }
    return t;
}

namespace
{
    juce::var pointsToVar (const std::vector<AutoPoint>& p)
    {
        juce::Array<juce::var> a;
        for (auto& q : p) a.add (juce::Array<juce::var> { q.beat, q.value, q.curve });
        return a;
    }
    std::vector<AutoPoint> pointsFromVar (const juce::var& v)
    {
        std::vector<AutoPoint> p;
        if (auto* a = v.getArray())
            for (auto& q : *a)
                if (auto* x = q.getArray(); x != nullptr && x->size() >= 2)
                    p.push_back ({ (double) (*x)[0], juce::jlimit (0.0f, 1.0f, (float) (*x)[1]), x->size() > 2 ? juce::jlimit (-1.0f, 1.0f, (float) (*x)[2]) : 0.0f });
        sort (p);
        return p;
    }
}

juce::var toVar (const std::vector<AutoLane>& lanes)
{
    juce::Array<juce::var> out;
    for (auto& l : lanes)
    {
        if (l.points.empty() && l.ghost.empty() && l.snaps.empty()) continue;
        auto* o = new juce::DynamicObject();
        o->setProperty ("target", toVar (l.target));
        o->setProperty ("name", l.name);
        o->setProperty ("lo", l.lo);
        o->setProperty ("hi", l.hi);
        o->setProperty ("stepped", l.stepped);
        o->setProperty ("points", pointsToVar (l.points));
        if (! l.ghost.empty()) o->setProperty ("ghost", pointsToVar (l.ghost));
        if (l.overridden) o->setProperty ("overridden", true);
        if (! l.snaps.empty())
        {
            juce::Array<juce::var> snaps;
            for (auto& s : l.snaps)
            {
                juce::Array<juce::var> vals;
                for (auto& [k, val] : s.values) vals.add (juce::Array<juce::var> { k, val });
                snaps.add (vals);
            }
            o->setProperty ("snaps", snaps);
            juce::Array<juce::var> st;
            for (int k : l.morphStepped) st.add (k);
            o->setProperty ("morphStepped", st);
        }
        out.add (juce::var (o));
    }
    return out;
}

std::vector<AutoLane> lanesFromVar (const juce::var& v)
{
    std::vector<AutoLane> lanes;
    if (auto* a = v.getArray())
        for (auto& lv : *a)
        {
            AutoLane l;
            l.target = targetFromVar (lv["target"]);
            if (! l.target.valid()) continue;
            l.name = lv["name"].toString();
            l.lo = (float) lv.getProperty ("lo", 0.0f);
            l.hi = (float) lv.getProperty ("hi", 1.0f);
            l.stepped = (bool) lv["stepped"];
            l.points = pointsFromVar (lv["points"]);
            l.ghost = pointsFromVar (lv["ghost"]);
            l.overridden = (bool) lv["overridden"];
            if (auto* snaps = lv["snaps"].getArray())
                for (auto& sv : *snaps)
                {
                    MorphSnap s;
                    if (auto* vals = sv.getArray())
                        for (auto& pv : *vals)
                            if (auto* pr = pv.getArray(); pr != nullptr && pr->size() == 2)
                                s.values.push_back ({ (int) (*pr)[0], (float) (*pr)[1] });
                    l.snaps.push_back (s);
                }
            if (auto* st = lv["morphStepped"].getArray())
                for (auto& k : *st) l.morphStepped.push_back ((int) k);
            lanes.push_back (l);
        }
    return lanes;
}
}
