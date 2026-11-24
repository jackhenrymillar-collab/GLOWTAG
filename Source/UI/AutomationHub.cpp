// The automation side of AppContext: which lane each track shows, what can be automated,
// Alt-click to a lane, recording by touch, and lanes resting after a hand moved their control.
#include "AppContext.h"
#include "DeviceViews.h"
#include <map>

namespace
{
    // What a device of one kind can automate, worked out once from its wall
    struct Known { int param; juce::String name; float lo, hi; bool stepped; };

    const std::vector<Known>& knownFor (AppContext& ctx, int track, const Module& m)
    {
        static std::map<int, std::vector<Known>> cache;
        const int key = (int) m.type;
        if (auto it = cache.find (key); it != cache.end())
            return it->second;

        std::vector<Known> list;
        const auto& info = moduleInfo (m.type);
        std::map<int, Known> byParam;
        if (DeviceViews::hasView (m.type))
        {
            DeviceViews::Device dev { ctx, track, m.id };
            if (auto built = DeviceViews::build (dev); built != nullptr && built->wall != nullptr)
                for (auto& g : built->wall->groups)
                    for (auto& itp : g->items)
                    {
                        auto& it = *itp;
                        if (it.param < 0 || it.param >= info.numParams() || byParam.count (it.param) > 0) continue;
                        Known k { it.param, info.paramName (it.param), 0.0f, 1.0f, false };
                        if (k.name.isEmpty()) k.name = it.text;
                        if (it.kind == Neon::Item::words)
                        {
                            k.stepped = true;
                            if (it.toggle) { k.lo = 0.0f; k.hi = 1.0f; }
                            else if (! it.opts.empty())
                            {
                                k.lo = k.hi = (float) it.opts.front().first;
                                for (auto& o : it.opts) { k.lo = juce::jmin (k.lo, (float) o.first); k.hi = juce::jmax (k.hi, (float) o.first); }
                                if (k.name.isEmpty()) k.name = it.opts.front().second;
                            }
                        }
                        if (k.name.isNotEmpty() && k.hi > k.lo)
                            byParam[it.param] = k;
                    }
        }
        // The card knobs are always 0..1 faders, wall or not
        for (int i = 0; i < (int) info.knobs.size(); ++i)
            if (byParam.count (i) == 0)
                byParam[i] = { i, info.knobs[(size_t) i].first, 0.0f, 1.0f, false };
        for (auto& [p, k] : byParam) list.push_back (k);
        return cache[key] = list;
    }

    std::vector<AppContext::ParamInfo> mixerParams (const Track& t)
    {
        std::vector<AppContext::ParamInfo> out;
        auto add = [&] (int p, const char* name, float lo, float hi)
        {
            AppContext::ParamInfo i;
            i.group = "mixer"; i.name = name; i.lo = lo; i.hi = hi;
            i.target = { AutoTarget::mixer, 0, p };
            out.push_back (i);
        };
        add (MixParam::volume, "volume", 0.0f, 1.25f);
        add (MixParam::pan, "pan", -1.0f, 1.0f);
        if (! t.isBus())
        {
            add (MixParam::sendA, "send a", 0.0f, 1.0f);
            add (MixParam::sendB, "send b", 0.0f, 1.0f);
        }
        add (MixParam::trim, "trim", -12.0f, 12.0f);
        add (MixParam::width, "width", 0.0f, 2.0f);
        return out;
    }

    std::vector<const Module*> devicesOf (const Track& t)
    {
        std::vector<const Module*> list;
        for (auto& m : t.patch.modules)
            if (m.type != ModuleType::Input && m.type != ModuleType::Output && m.type != ModuleType::Plugin)
                list.push_back (&m);
        std::stable_sort (list.begin(), list.end(), [] (auto* a, auto* b)
        {
            const bool ua = moduleInfo (a->type).utility, ub = moduleInfo (b->type).utility;
            return ua != ub ? ub : a->position.x < b->position.x;
        });
        return list;
    }

    double nowSeconds() { return juce::Time::getMillisecondCounterHiRes() * 0.001; }
}

int AppContext::rowHeight() const
{
    if (! lanesShown) return Layout::rowHeight;
    const int rows = juce::jmax (1, numRows());
    return arrangeHeight > 0 ? juce::jlimit (Layout::rowHeight, 60, (arrangeHeight - Layout::rulerHeight) / rows) : 60;
}

float AppContext::currentValue (int track, const AutoTarget& t, const ParamInfo& info)
{
    auto norm = [&] (float raw) { return info.hi > info.lo ? juce::jlimit (0.0f, 1.0f, (raw - info.lo) / (info.hi - info.lo)) : 0.0f; };
    if (track == visSlot)
    {
        float raw = 0.0f;
        return visValue != nullptr && visValue (t, raw) ? norm (raw) : 0.0f;
    }
    if (track < 0 || track >= project.numSlots()) return 0.0f;
    auto& tr = project.slot (track);
    if (t.kind == AutoTarget::mixer)
    {
        switch (t.param)
        {
            case MixParam::volume: return norm (tr.volume.load());
            case MixParam::pan:    return norm (tr.pan.load());
            case MixParam::sendA:  return norm (tr.send[0].load());
            case MixParam::sendB:  return norm (tr.send[1].load());
            case MixParam::trim:   return norm (tr.trimDb.load());
            case MixParam::width:  return norm (tr.width.load());
            default: return 0.0f;
        }
    }
    if (t.kind == AutoTarget::knob)
        if (auto* m = tr.patch.find (t.module); m != nullptr && t.param >= 0 && t.param < (int) m->knobValues.size())
            return norm (m->knobValues[(size_t) t.param]);
    return 0.5f;
}

std::vector<AutoLane>* AppContext::lanesOf (int track)
{
    if (track == visSlot) return project.visTrack ? &project.visLanes : nullptr;
    return track >= 0 && track < project.numSlots() ? &project.slot (track).lanes : nullptr;
}

AutoTarget AppContext::shownTarget (int track)
{
    if (track == visSlot)
    {
        if (project.visShown.valid()) return project.visShown;
        for (auto& l : project.visLanes) if (! l.points.empty()) return l.target;
        return { AutoTarget::vis, 0, 0 };
    }
    if (track < 0 || track >= project.numSlots()) return {};
    auto& t = project.slot (track);
    if (t.shown.valid()) return t.shown;
    for (auto& l : t.lanes) if (! l.points.empty()) return l.target;
    return { AutoTarget::mixer, 0, MixParam::volume };
}

void AppContext::setShown (int track, const AutoTarget& target)
{
    if (track == visSlot) project.visShown = target;
    else if (track >= 0 && track < project.numSlots()) project.slot (track).shown = target;
}

std::vector<AppContext::ParamInfo> AppContext::paramsOf (int track)
{
    if (track == visSlot) return visParams != nullptr ? visParams() : std::vector<ParamInfo>();
    std::vector<ParamInfo> out;
    if (track < 0 || track >= project.numSlots()) return out;
    auto& t = project.slot (track);
    out = mixerParams (t);
    std::map<int, int> seen;       // the same kind twice on a track: "filter 2"
    for (auto* m : devicesOf (t))
    {
        const auto& info = moduleInfo (m->type);
        const int n = ++seen[(int) m->type];
        const auto group = info.title.toLowerCase() + (n > 1 ? " " + juce::String (n) : juce::String());
        for (auto& k : knownFor (*this, track, *m))
        {
            ParamInfo i;
            i.group = group; i.name = k.name.toLowerCase(); i.lo = k.lo; i.hi = k.hi; i.stepped = k.stepped;
            i.target = { AutoTarget::knob, m->id, k.param };
            out.push_back (i);
        }
        if (DeviceViews::hasView (m->type))
        {
            ParamInfo i;
            i.group = group; i.name = "morph"; i.target = { AutoTarget::morph, m->id, 0 };
            out.push_back (i);
        }
    }
    return out;
}

AppContext::ParamInfo AppContext::describe (int track, const AutoTarget& target)
{
    if (track == visSlot)
    {
        if (visParams != nullptr)
            for (auto& p : visParams()) if (p.target == target) return p;
        return {};
    }
    if (track < 0 || track >= project.numSlots()) return {};
    auto& t = project.slot (track);
    if (target.kind == AutoTarget::mixer)
    {
        for (auto& p : mixerParams (t)) if (p.target == target) return p;
        return {};
    }
    if (const auto* m = t.patch.find (target.module))
    {
        ParamInfo i;
        i.group = moduleInfo (m->type).title.toLowerCase();
        i.target = target;
        if (target.kind == AutoTarget::morph) { i.name = "morph"; return i; }
        for (auto& k : knownFor (*this, track, *m))
            if (k.param == target.param)
            {
                i.name = k.name.toLowerCase(); i.lo = k.lo; i.hi = k.hi; i.stepped = k.stepped;
                return i;
            }
        i.name = moduleInfo (m->type).paramName (target.param).toLowerCase();
        return i;
    }
    return {};
}

void AppContext::showLane (int track, const AutoTarget& target)
{
    if (! isTrack (track) && track != visSlot) return;      // returns and the master have no row in the arrangement
    if (track == visSlot && ! project.visTrack) project.visTrack = true;
    ++laneRequests;
    setShown (track, target);
    lanesShown = true;
    if (isTrack (track)) selectTrack (track);
    automationChanged();
}

void AppContext::paramTouched (int track, const AutoTarget& target, float raw)
{
    auto* lanes = lanesOf (track);
    if (lanes == nullptr) return;
    const bool recording = engine.recordArmed.load() && engine.isPlaying();
    if (recording)
    {
        const auto info = describe (track, target);
        const double now = engine.getPosition();
        {
            const juce::SpinLock::ScopedLockType sl (project.lock);
            auto& lane = Auto::get (*lanes, target);
            if (lane.name.isEmpty() || ! lane.writing)
            {
                lane.name = (info.group + "  " + info.name).trim();
                if (info.hi > info.lo) { lane.lo = info.lo; lane.hi = info.hi; lane.stepped = info.stepped; }
            }
            lane.overridden = false;
            const float v = lane.norm (raw);
            if (! lane.writing)
            {
                // A new take: keep the old lane as the ghost, and hold its value up to where the hand came in
                lane.ghost = lane.points;
                lane.writing = true;
                lane.writeFrom = lane.lastBeat = now;
                if (! lane.points.empty() && now > 0.002)
                    Auto::add (lane.points, now - 0.002, Auto::valueAt (lane.points, now - 0.002));
            }
            else if (now < lane.lastBeat - 1.0e-6)
            {
                // The loop went round: tidy what was written and carry on from the top
                Auto::thin (lane.points, lane.writeFrom, lane.lastBeat);
                lane.writeFrom = lane.lastBeat = now;
            }
            Auto::removeRange (lane.points, lane.lastBeat + 1.0e-6, now);
            Auto::removeRange (lane.points, now, now);
            Auto::add (lane.points, now, v);
            lane.lastBeat = now;
            lane.lastTouch = nowSeconds();
        }
        if (! shownTarget (track).valid() || (track >= 0 && ! project.slot (track).shown.valid()))
            setShown (track, target);
        ++autoGeneration;
        project.dirty = true;
        return;
    }

    // Not recording: a lane that plays this control rests, so the hand wins
    bool rested = false;
    {
        const juce::SpinLock::ScopedLockType sl (project.lock);
        if (auto* lane = Auto::find (*lanes, target); lane != nullptr && ! lane->points.empty() && ! lane->overridden)
            lane->overridden = rested = true;
    }
    if (rested) automationChanged();
}

void AppContext::tickAutomation()
{
    const bool recording = engine.recordArmed.load() && engine.isPlaying();
    const double t = nowSeconds();
    bool changed = false;
    auto endTouches = [&] (std::vector<AutoLane>& lanes)
    {
        for (auto& lane : lanes)
            if (lane.writing && (! recording || t - lane.lastTouch > 0.45))
            {
                const juce::SpinLock::ScopedLockType sl (project.lock);
                // Touch: when the hand lets go, the lane goes back to what it was doing
                if (! lane.ghost.empty() && lane.ghost.back().beat > lane.lastBeat + 0.004)
                    Auto::add (lane.points, lane.lastBeat + 0.002, Auto::valueAt (lane.ghost, lane.lastBeat + 0.002));
                Auto::thin (lane.points, lane.writeFrom, lane.lastBeat);
                lane.writing = false;
                changed = true;
            }
    };
    for (auto& tr : project.tracks) endTouches (tr->lanes);
    endTouches (project.visLanes);
    if (changed) automationChanged();

    // The walls and cards read the stored values: show them moving with the lanes
    const double beat = engine.getPosition();
    for (int i = 0; i < (int) project.tracks.size(); ++i)
    {
        auto& tr = *project.tracks[(size_t) i];
        for (auto& lane : tr.lanes)
        {
            if (! lane.plays()) continue;
            const float v = Auto::valueAt (lane.points, beat);
            if (lane.target.kind == AutoTarget::knob)
            {
                if (auto* m = tr.patch.find (lane.target.module); m != nullptr && lane.target.param < (int) m->knobValues.size())
                    m->knobValues[(size_t) lane.target.param] = lane.raw (v);
            }
            else if (lane.target.kind == AutoTarget::morph && ! lane.snaps.empty())
            {
                if (auto* m = tr.patch.find (lane.target.module))
                    for (auto& pv : lane.snaps.front().values)
                    {
                        float out;
                        if (pv.first < (int) m->knobValues.size() && Auto::morphValue (lane, pv.first, v, out))
                            m->knobValues[(size_t) pv.first] = out;
                    }
            }
        }
    }
}

int AppContext::restingLanes() const
{
    int n = 0;
    for (auto& tr : project.tracks) for (auto& l : tr->lanes) if (l.overridden && ! l.points.empty()) ++n;
    if (project.visTrack) for (auto& l : project.visLanes) if (l.overridden && ! l.points.empty()) ++n;
    return n;
}

void AppContext::backToAutomation()
{
    {
        const juce::SpinLock::ScopedLockType sl (project.lock);
        for (auto& tr : project.tracks) for (auto& l : tr->lanes) l.overridden = false;
        for (auto& l : project.visLanes) l.overridden = false;
    }
    automationChanged();
}
