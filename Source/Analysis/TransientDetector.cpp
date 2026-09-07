#include "TransientDetector.h"
#include "OnsetEnvelope.h"
#include <algorithm>
#include <cmath>

namespace mashup::analysis
{
std::vector<double> TransientDetector::detect (const float* mono, int numSamples, double sr, double sensitivity, double minGap)
{
    std::vector<double> out;
    auto env = OnsetEnvelope::compute (mono, numSamples, sr, 1024, 256, false);
    const int n = (int) env.values.size();
    if (n < 8) return out;
    const auto& v = env.values;
    const int half = (int) (0.15 * env.frameRate);          // 300 ms median window
    const double delta = 0.9 - 0.7 * std::clamp (sensitivity, 0.0, 1.0);   // threshold offset factor
    float mx = 1e-9f; for (float x : v) mx = std::max (mx, x);
    const int minGapFrames = std::max (1, (int) (minGap * env.frameRate));
    int last = -minGapFrames;
    std::vector<float> win;
    for (int i = 1; i + 1 < n; ++i)
    {
        if (! (v[(size_t) i] > v[(size_t) i - 1] && v[(size_t) i] >= v[(size_t) i + 1])) continue;
        win.clear();
        for (int k = std::max (0, i - half); k < std::min (n, i + half + 1); ++k) win.push_back (v[(size_t) k]);
        std::nth_element (win.begin(), win.begin() + (long) win.size() / 2, win.end());
        const float median = win[win.size() / 2];
        const float thresh = median * (1.0f + (float) delta * 2.0f) + (float) delta * 0.05f * mx;
        if (v[(size_t) i] > thresh && i - last >= minGapFrames)
        {
            // refine to the sample with the steepest energy rise inside the frame
            const double t = i * env.hopSeconds;
            out.push_back (t);
            last = i;
        }
    }
    return out;
}
} // namespace mashup::analysis
