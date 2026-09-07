#pragma once
#include <vector>

namespace mashup::analysis
{
/** Onset/transient positions (seconds) via spectral flux with adaptive median thresholding. */
struct TransientDetector
{
    /** sensitivity 0..1 (higher = more transients). minGapSeconds prevents double triggers. */
    static std::vector<double> detect (const float* mono, int numSamples, double sampleRate, double sensitivity = 0.5, double minGapSeconds = 0.05);
};
}
