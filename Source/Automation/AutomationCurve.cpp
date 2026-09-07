#include "AutomationCurve.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <cmath>

namespace mashup
{
void AutomationCurve::addPoint (double beat, float value, float curve)
{
    Point p { beat, juce::jlimit (0.0f, 1.0f, value), curve };
    auto it = std::lower_bound (points.begin(), points.end(), beat, [] (const Point& a, double b) { return a.beat < b; });
    if (it != points.end() && std::abs (it->beat - beat) < 1e-6) { it->value = p.value; return; }
    points.insert (it, p);
}
void AutomationCurve::removePointsInRange (double a, double b)
{
    points.erase (std::remove_if (points.begin(), points.end(), [&] (const Point& p) { return p.beat >= a && p.beat <= b; }), points.end());
}
int AutomationCurve::indexNear (double beat, double tol) const
{
    int best = -1; double bd = tol;
    for (size_t i = 0; i < points.size(); ++i) { const double d = std::abs (points[i].beat - beat); if (d <= bd) { bd = d; best = (int) i; } }
    return best;
}
void AutomationCurve::setPoint (int i, double beat, float value)
{
    if (i < 0 || i >= (int) points.size()) return;
    const float curve = points[(size_t) i].curve;
    points.erase (points.begin() + i);
    addPoint (beat, value, curve);
}
float AutomationCurve::valueAt (double beat) const noexcept
{
    if (points.empty()) return 0.0f;
    if (beat <= points.front().beat) return points.front().value;
    if (beat >= points.back().beat) return points.back().value;
    size_t hi = 1; while (hi < points.size() && points[hi].beat < beat) ++hi;
    const auto& a = points[hi - 1]; const auto& b = points[hi];
    double t = (beat - a.beat) / juce::jmax (1e-9, b.beat - a.beat);
    if (std::abs (a.curve) > 1e-3) t = std::pow (t, std::exp (-a.curve * 2.0));
    return (float) (a.value + (b.value - a.value) * t);
}

AutomationCurve AutomationCurve::fromLane (const juce::ValueTree& lane)
{
    AutomationCurve c;
    for (const auto& p : lane) if (p.hasType (ids::POINT)) c.points.push_back ({ (double) p[ids::beat], (float) (double) p[ids::value], (float) (double) p.getProperty (ids::curve, 0.0) });
    std::sort (c.points.begin(), c.points.end(), [] (const Point& a, const Point& b) { return a.beat < b.beat; });
    return c;
}
void AutomationCurve::writeToLane (juce::ValueTree lane, juce::UndoManager* um) const
{
    lane.removeAllChildren (um);
    for (const auto& p : points)
    {
        juce::ValueTree n (ids::POINT); n.setProperty (ids::beat, p.beat, nullptr); n.setProperty (ids::value, (double) p.value, nullptr);
        if (std::abs (p.curve) > 1e-4) n.setProperty (ids::curve, (double) p.curve, nullptr);
        lane.appendChild (n, um);
    }
}
juce::ValueTree AutomationCurve::findLane (const juce::ValueTree& track, const juce::String& param)
{
    for (const auto& l : track.getChildWithName (ids::AUTOMATION)) if (l[ids::param].toString() == param) return l;
    return {};
}
juce::ValueTree AutomationCurve::getOrCreateLane (juce::ValueTree track, const juce::String& param, juce::UndoManager* um)
{
    auto existing = findLane (track, param); if (existing.isValid()) return existing;
    auto auto_ = track.getOrCreateChildWithName (ids::AUTOMATION, um);
    juce::ValueTree lane (ids::LANE); lane.setProperty (ids::param, param, nullptr); auto_.appendChild (lane, um);
    return lane;
}
float AutomationCurve::volumeToNorm (double gain) noexcept { const double db = juce::Decibels::gainToDecibels (gain, -60.0); return (float) juce::jlimit (0.0, 1.0, (db + 60.0) / 72.0); }
double AutomationCurve::normToVolume (float n) noexcept { const double db = -60.0 + 72.0 * n; return db <= -59.9 ? 0.0 : juce::Decibels::decibelsToGain (db); }
} // namespace mashup
