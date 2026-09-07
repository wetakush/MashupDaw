#include "BpmDetector.h"
#include "OnsetEnvelope.h"
#include <cmath>
#include <algorithm>
#include <numeric>

namespace mashup::analysis
{
namespace
{
    // smooth + normalise + adaptive mean removal
    std::vector<float> condition (const std::vector<float>& in, double frameRate)
    {
        std::vector<float> out (in.size(), 0.0f);
        if (in.empty()) return out;
        const int half = (int) (frameRate * 0.5);   // 1 s local mean window
        std::vector<double> prefix (in.size() + 1, 0.0);
        for (size_t i = 0; i < in.size(); ++i) prefix[i + 1] = prefix[i] + in[i];
        for (size_t i = 0; i < in.size(); ++i)
        {
            const size_t a = i > (size_t) half ? i - half : 0, b = std::min (in.size(), i + half + 1);
            const float mean = (float) ((prefix[b] - prefix[a]) / (double) (b - a));
            out[i] = std::max (0.0f, in[i] - mean);
        }
        // light smoothing (3-tap)
        std::vector<float> sm (out.size());
        for (size_t i = 0; i < out.size(); ++i) sm[i] = (out[i] + (i ? out[i - 1] : 0) + (i + 1 < out.size() ? out[i + 1] : 0)) / 3.0f;
        float mx = 1.0e-9f; for (float v : sm) mx = std::max (mx, v);
        for (float& v : sm) v /= mx;
        return sm;
    }

    double tempoPrior (double bpm)   // log-normal around 120
    {
        const double x = std::log2 (bpm / 120.0);
        return std::exp (-0.5 * x * x / (0.9 * 0.9));
    }
}

std::vector<double> BpmDetector::gridFrom (double bpm, double first, double length)
{
    std::vector<double> g;
    if (bpm <= 0) return g;
    const double period = 60.0 / bpm;
    double t = first; while (t - period >= 0) t -= period;
    for (; t < length; t += period) g.push_back (t);
    return g;
}

double BpmDetector::bpmFromBeats (const std::vector<double>& beats)
{
    if (beats.size() < 2) return 0.0;
    std::vector<double> iv; for (size_t i = 1; i < beats.size(); ++i) iv.push_back (beats[i] - beats[i - 1]);
    std::sort (iv.begin(), iv.end());
    return 60.0 / iv[iv.size() / 2];
}

BpmDetector::Result BpmDetector::analyse (const float* mono, int numSamples, double sr, const std::function<bool (float)>& progress)
{
    Result r;
    auto env = OnsetEnvelope::compute (mono, numSamples, sr, 2048, 512, true, [&] (float p) { return progress ? progress (p * 0.5f) : true; });
    const int n = (int) env.values.size();
    if (n < 64) return r;
    const double fr = env.frameRate;
    auto oss = condition (env.values, fr);

    // ---- tempo: autocorrelation over lags for 60..200 bpm, weighted by prior; add harmonic support (2x, 0.5x lags)
    const int minLag = (int) std::floor (60.0 / maxBpm * fr), maxLag = (int) std::ceil (60.0 / minBpm * fr);
    std::vector<double> ac ((size_t) maxLag + 1, 0.0);
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        double s = 0; for (int i = lag; i < n; ++i) s += oss[(size_t) i] * oss[(size_t) (i - lag)];
        ac[(size_t) lag] = s / (n - lag);
    }
    double acMax = 1e-12; for (int lag = minLag; lag <= maxLag; ++lag) acMax = std::max (acMax, ac[(size_t) lag]);
    std::vector<std::pair<double, double>> scored;   // bpm, score
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        const double bpm = 60.0 * fr / lag;
        double score = ac[(size_t) lag] / acMax;
        // harmonic support: half-tempo lag (2*lag) and double-tempo (lag/2) partially reinforce
        if (2 * lag <= maxLag) score += 0.5 * ac[(size_t) (2 * lag)] / acMax;
        if (lag / 2 >= minLag) score += 0.3 * ac[(size_t) (lag / 2)] / acMax;
        score *= tempoPrior (bpm);
        scored.push_back ({ bpm, score });
    }
    // pick peaks (local maxima), keep top 5 distinct candidates
    std::vector<std::pair<double, double>> peaks;
    for (size_t i = 1; i + 1 < scored.size(); ++i)
        if (scored[i].second > scored[i - 1].second && scored[i].second >= scored[i + 1].second) peaks.push_back (scored[i]);
    std::sort (peaks.begin(), peaks.end(), [] (auto& a, auto& b) { return a.second > b.second; });
    for (auto& pk : peaks) { bool dup = false; for (auto& c : r.candidates) if (std::abs (c.first - pk.first) < 2.0) dup = true; if (! dup) r.candidates.push_back (pk); if (r.candidates.size() >= 5) break; }
    if (r.candidates.empty()) return r;
    double bpm0 = r.candidates[0].first;
    r.confidence = r.candidates.size() > 1 ? std::min (1.0, (r.candidates[0].second - r.candidates[1].second) / std::max (1e-9, r.candidates[0].second) + 0.5) : 0.9;

    // ---- refine tempo: parabolic interpolation on the autocorrelation around the best lag
    {
        const int lag = (int) std::lround (60.0 * fr / bpm0);
        if (lag > minLag && lag < maxLag)
        {
            const double y0 = ac[(size_t) lag - 1], y1 = ac[(size_t) lag], y2 = ac[(size_t) lag + 1];
            const double denom = (y0 - 2 * y1 + y2);
            const double delta = std::abs (denom) > 1e-12 ? 0.5 * (y0 - y2) / denom : 0.0;
            bpm0 = 60.0 * fr / (lag + std::clamp (delta, -0.5, 0.5));
        }
    }
    if (progress && ! progress (0.6f)) return r;

    // ---- beat tracking: dynamic programming (Ellis 2007) with the period from bpm0
    const double period = 60.0 / bpm0 * fr;   // in frames
    const double alpha = 400.0;                // transition penalty weight
    std::vector<double> cumScore ((size_t) n, 0.0); std::vector<int> back ((size_t) n, -1);
    const int lo = (int) std::floor (period * 0.5), hi = (int) std::ceil (period * 2.0);
    for (int i = 0; i < n; ++i)
    {
        double best = -1e18; int bi = -1;
        for (int p = std::max (0, i - hi); p <= i - lo; ++p)
        {
            const double d = (double) (i - p);
            const double trans = -alpha * std::pow (std::log (d / period), 2.0);
            const double s = cumScore[(size_t) p] + trans;
            if (s > best) { best = s; bi = p; }
        }
        cumScore[(size_t) i] = oss[(size_t) i] + (bi >= 0 ? best : 0.0);
        back[(size_t) i] = bi;
    }
    // backtrack from the best of the last period
    int end = n - 1; double bestEnd = -1e18;
    for (int i = std::max (0, n - (int) period - 1); i < n; ++i) if (cumScore[(size_t) i] > bestEnd) { bestEnd = cumScore[(size_t) i]; end = i; }
    std::vector<int> beatFrames;
    for (int i = end; i >= 0; i = back[(size_t) i]) { beatFrames.push_back (i); if (back[(size_t) i] < 0) break; }
    std::reverse (beatFrames.begin(), beatFrames.end());
    // drop leading beats with essentially no onset energy (silence at the start), keep grid phase
    for (int f : beatFrames) r.beats.push_back (f / fr);
    if (progress && ! progress (0.8f)) return r;

    // ---- final bpm: least-squares fit of tracked beat times vs. beat index (averages out frame quantisation)
    {
        std::vector<std::pair<double, double>> pts;   // (index, time), skipping beats with no onset energy (silence)
        for (size_t i = 0; i < r.beats.size(); ++i) pts.push_back ({ (double) i, r.beats[i] });
        if (pts.size() >= 4)
        {
            double mx = 0, my = 0; for (auto& [x, y] : pts) { mx += x; my += y; } mx /= pts.size(); my /= pts.size();
            double sxx = 0, sxy = 0; for (auto& [x, y] : pts) { sxx += (x - mx) * (x - mx); sxy += (x - mx) * (y - my); }
            const double slope = sxy / sxx;
            const double bpmFit = 60.0 / slope;
            if (std::abs (bpmFit - bpm0) / bpm0 < 0.06) r.bpm = bpmFit; else r.bpm = bpm0;
        }
        else r.bpm = bpm0;
    }
    // snap near-integer tempos (most produced music is on an integer bpm)
    if (std::abs (r.bpm - std::round (r.bpm)) < 0.12) r.bpm = std::round (r.bpm);
    else if (std::abs (r.bpm * 2 - std::round (r.bpm * 2)) < 0.1) r.bpm = std::round (r.bpm * 2) / 2.0;

    // ---- phase: sample-accurate alignment of the straight grid to the audio's energy rises
    {
        const double p = 60.0 / r.bpm;
        // 1 ms energy envelope and its positive derivative
        const int blk = std::max (1, (int) (sr * 0.001));
        const int nb = numSamples / blk;
        std::vector<float> en ((size_t) nb, 0.0f);
        for (int i = 0; i < nb; ++i) { double e = 0; const float* s = mono + (size_t) i * blk; for (int k = 0; k < blk; ++k) e += s[k] * s[k]; en[(size_t) i] = (float) std::sqrt (e / blk); }
        std::vector<float> rise ((size_t) nb, 0.0f);
        for (int i = 3; i < nb; ++i) rise[(size_t) i] = std::max (0.0f, en[(size_t) i] - en[(size_t) i - 3]);
        // coarse phase from the tracked beats (circular mean), then a fine search +-25 ms in 1 ms steps
        double sx = 0, sy = 0; for (double b : r.beats) { const double a = 2 * M_PI * std::fmod (b, p) / p; sx += std::cos (a); sy += std::sin (a); }
        double phase = std::fmod (std::atan2 (sy, sx) / (2 * M_PI) * p + p, p);
        double bestScore = -1, bestPhase = phase;
        for (double off = -0.03; off <= 0.03; off += 0.001)
        {
            const double ph = phase + off;
            double score = 0; int cnt = 0;
            for (double t = ph; t < numSamples / sr; t += p) { const int i = (int) (t / 0.001); if (i >= 0 && i < nb) { score += rise[(size_t) i] + 0.5f * (i + 1 < nb ? rise[(size_t) i + 1] : 0.0f); ++cnt; } }
            if (cnt && score > bestScore) { bestScore = score; bestPhase = ph; }
        }
        phase = std::fmod (bestPhase + p, p);
        r.beats = gridFrom (r.bpm, phase, numSamples / sr);
    }

    // ---- downbeats: score each of the beatsPerBar phases
    {
        const int B = beatsPerBar;
        auto frameAt = [&] (double t) { return std::clamp ((int) std::lround (t * fr), 0, n - 1); };
        // the spectral-flux peak of an onset lands a few frames before the beat's nominal frame (the analysis window
        // is fftSize/hop frames long); find that offset from the beat-synchronous average
        int bestOff = 0; double bestOffScore = -1;
        for (int off = -6; off <= 2; ++off)
        {
            double s = 0; for (double b : r.beats) { const int f = frameAt (b) + off; if (f >= 0 && f < n) s += oss[(size_t) f]; }
            if (s > bestOffScore) { bestOffScore = s; bestOff = off; }
        }
        std::vector<double> score ((size_t) B, 0.0);
        for (size_t i = 0; i + 1 < r.beats.size(); ++i)
        {
            const int f = frameAt (r.beats[i]) + bestOff;
            double s = 0.0;
            for (int k = -1; k <= 1; ++k) { const int idx = std::clamp (f + k, 0, n - 1); s += env.lowBand[(size_t) idx] * 1.0 + oss[(size_t) idx] * 0.5; }
            if (! env.chroma.empty() && i > 0)
            {
                // harmonic change between the previous beat's window and this beat's window
                const int fp = std::clamp (frameAt (r.beats[i - 1]) + bestOff + 2, 0, n - 1), fn = std::clamp (f + 2, 0, n - 1);
                const auto& a = env.chroma[(size_t) fp]; const auto& b = env.chroma[(size_t) fn];
                double na = 1e-9, nb = 1e-9, dot = 0; for (int c = 0; c < 12; ++c) { na += a[(size_t) c] * a[(size_t) c]; nb += b[(size_t) c] * b[(size_t) c]; dot += a[(size_t) c] * b[(size_t) c]; }
                s += (1.0 - dot / std::sqrt (na * nb)) * 2.0;
            }
            score[i % (size_t) B] += s;
        }
        int bestPhase = 0; for (int b = 1; b < B; ++b) if (score[(size_t) b] > score[(size_t) bestPhase]) bestPhase = b;
        for (size_t i = (size_t) bestPhase; i < r.beats.size(); i += (size_t) B) r.downbeats.push_back (r.beats[i]);
    }
    if (progress) progress (1.0f);
    return r;
}
} // namespace mashup::analysis
