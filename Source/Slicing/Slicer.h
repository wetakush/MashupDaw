#pragma once
#include <vector>
#include "Clips/ClipModel.h"

namespace mashup
{
class ProjectModel;

/** Computes cut positions (absolute beats) for a clip from the grid, source transients or phrases. */
namespace slicer
{
    /** Every `divisionBeats` (e.g. 1.0 = 1/4 note, 0.25 = 1/16, beatsPerBar*N = bars). */
    std::vector<double> byDivision (const ClipModel&, double divisionBeats, double projectBpm);
    /** At source transients (seconds) mapped through the clip; `minGapBeats` merges close ones. */
    std::vector<double> atSourceTimes (const ClipModel&, const std::vector<double>& sourceSeconds, double projectBpm, double minGapBeats = 0.0);
    std::vector<double> atTransients (ProjectModel&, const ClipModel&, double minGapBeats = 1.0 / 16.0);
    std::vector<double> atPhrases (ProjectModel&, const ClipModel&);
    /** Slices (undoably) and returns resulting clips. */
    std::vector<ClipModel> slice (ProjectModel&, ClipModel, const std::vector<double>& beats);
}
} // namespace mashup
