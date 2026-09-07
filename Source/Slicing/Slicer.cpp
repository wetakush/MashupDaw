#include "Slicer.h"
#include "Project/ProjectModel.h"
#include "Clips/ClipOperations.h"
#include "Analysis/AnalysisData.h"
#include <algorithm>

namespace mashup::slicer
{
std::vector<double> byDivision (const ClipModel& c, double div, double)
{
    std::vector<double> out;
    if (div <= 0) return out;
    const double first = std::ceil (c.getStart() / div + 1e-9) * div;
    for (double b = first; b < c.getEnd() - 1e-6; b += div) if (b > c.getStart() + 1e-6) out.push_back (b);
    return out;
}

std::vector<double> atSourceTimes (const ClipModel& c, const std::vector<double>& times, double bpm, double minGap)
{
    std::vector<double> out;
    const double regionEnd = (double) c.getState().getProperty (ids::sourceEnd, 0.0);
    for (double t : times)
    {
        double beat;
        if (! c.isReversed()) beat = c.getStart() + clipops::sourceSecondsToBeats (c, t - c.getOffset(), bpm);
        else beat = c.getStart() + clipops::sourceSecondsToBeats (c, regionEnd - t, bpm);
        if (beat <= c.getStart() + 1e-4 || beat >= c.getEnd() - 1e-4) continue;
        if (! out.empty() && beat - out.back() < minGap) continue;
        out.push_back (beat);
    }
    std::sort (out.begin(), out.end());
    return out;
}

std::vector<double> atTransients (ProjectModel& p, const ClipModel& c, double minGap)
{
    auto src = ProjectModel::findByIdIn (p.sources(), ids::SOURCE, c.getSourceId());
    return atSourceTimes (c, analysis::readTimes (src, ids::TRANSIENTS), p.getBpm(), minGap);
}

std::vector<double> atPhrases (ProjectModel& p, const ClipModel& c)
{
    auto src = ProjectModel::findByIdIn (p.sources(), ids::SOURCE, c.getSourceId());
    std::vector<double> times;
    for (const auto& ph : analysis::readPhrases (src)) times.push_back (ph.start);
    return atSourceTimes (c, times, p.getBpm(), 0.5);
}

std::vector<ClipModel> slice (ProjectModel& p, ClipModel c, const std::vector<double>& beats) { return clipops::sliceAt (p, c, beats, p.getBpm()); }
} // namespace mashup::slicer
