#include "PhraseDetector.h"
#include "DSP/FFT.h"
#include <cmath>
#include <algorithm>
#include <numeric>

namespace mashup::analysis
{
namespace
{
    struct BarFeature { std::vector<double> chroma = std::vector<double> (12, 0.0); std::vector<double> bands = std::vector<double> (8, 0.0); double energy = 0; };

    double cosineDistance (const std::vector<double>& a, const std::vector<double>& b)
    {
        double na = 1e-12, nb = 1e-12, d = 0;
        for (size_t i = 0; i < a.size(); ++i) { na += a[i] * a[i]; nb += b[i] * b[i]; d += a[i] * b[i]; }
        return 1.0 - d / std::sqrt (na * nb);
    }
    double featureDistance (const BarFeature& a, const BarFeature& b)
    {
        return 0.5 * cosineDistance (a.chroma, b.chroma) + 0.4 * cosineDistance (a.bands, b.bands) + 0.1 * std::abs (std::log ((a.energy + 1e-6) / (b.energy + 1e-6)));
    }
}

std::vector<Phrase> PhraseDetector::detect (const float* mono, int numSamples, double sr, const std::vector<double>& downbeatsIn, double bpm)
{
    std::vector<Phrase> out;
    std::vector<double> downbeats = downbeatsIn;
    const double length = numSamples / sr;
    if (downbeats.size() < 8)
    {
        if (bpm <= 0) return out;
        const double bar = 240.0 / bpm;
        downbeats.clear(); for (double t = 0; t < length; t += bar) downbeats.push_back (t);
        if (downbeats.size() < 8) return out;
    }
    const int numBars = (int) downbeats.size();

    // ---- per-bar features
    const int fftSize = 4096;
    dsp::FFT fft (fftSize);
    std::vector<float> window; dsp::FFT::hann (window, fftSize);
    std::vector<float> frame ((size_t) fftSize), mag ((size_t) fft.getNumBins());
    const int bins = fft.getNumBins();
    std::vector<int> pc ((size_t) bins, -1), band ((size_t) bins, -1);
    for (int b = 1; b < bins; ++b)
    {
        const double f = b * sr / fftSize;
        if (f >= 60 && f <= 4000) pc[(size_t) b] = (((int) std::lround (69.0 + 12.0 * std::log2 (f / 440.0))) % 12 + 12) % 12;
        band[(size_t) b] = std::clamp ((int) std::floor (std::log2 (std::max (f, 40.0) / 40.0) * 8.0 / std::log2 (16000.0 / 40.0)), 0, 7);
    }
    std::vector<BarFeature> feats ((size_t) numBars);
    for (int i = 0; i < numBars; ++i)
    {
        const double t0 = downbeats[(size_t) i], t1 = i + 1 < numBars ? downbeats[(size_t) i + 1] : std::min (length, t0 + 240.0 / std::max (bpm, 40.0));
        auto& F = feats[(size_t) i];
        int frames = 0;
        for (double t = t0; t + fftSize / sr <= t1; t += (t1 - t0) / 4.0)
        {
            const int s = (int) (t * sr);
            if (s + fftSize > numSamples) break;
            for (int k = 0; k < fftSize; ++k) frame[(size_t) k] = mono[s + k] * window[(size_t) k];
            fft.magnitudes (frame.data(), mag.data());
            for (int b = 1; b < bins; ++b)
            {
                const double m = mag[(size_t) b];
                if (pc[(size_t) b] >= 0) F.chroma[(size_t) pc[(size_t) b]] += m;
                F.bands[(size_t) band[(size_t) b]] += m * m;
                F.energy += m * m;
            }
            ++frames;
        }
        if (frames > 0) { for (auto& v : F.bands) v = std::log1p (v / frames); F.energy /= frames; }
    }

    // ---- novelty: checkerboard kernel on the self-similarity matrix (kernel half-size 4 bars)
    std::vector<std::vector<double>> ssm ((size_t) numBars, std::vector<double> ((size_t) numBars, 0.0));
    for (int i = 0; i < numBars; ++i) for (int j = 0; j < numBars; ++j) ssm[(size_t) i][(size_t) j] = 1.0 - featureDistance (feats[(size_t) i], feats[(size_t) j]);
    const int K = 4;
    std::vector<double> novelty ((size_t) numBars, 0.0);
    for (int i = K; i + K <= numBars; ++i)
    {
        double s = 0;
        for (int a = 0; a < K; ++a) for (int b = 0; b < K; ++b)
        {
            s += ssm[(size_t) (i - 1 - a)][(size_t) (i - 1 - b)] + ssm[(size_t) (i + a)][(size_t) (i + b)];
            s -= ssm[(size_t) (i - 1 - a)][(size_t) (i + b)] + ssm[(size_t) (i + a)][(size_t) (i - 1 - b)];
        }
        novelty[(size_t) i] = s / (K * K * 2.0);
    }
    // boundary candidates: novelty peaks, favour positions on 4/8-bar multiples from the first downbeat
    double nmax = 1e-9; for (double v : novelty) nmax = std::max (nmax, v);
    std::vector<std::pair<int, double>> peaks;
    for (int i = 1; i + 1 < numBars; ++i)
        if (novelty[(size_t) i] > novelty[(size_t) i - 1] && novelty[(size_t) i] >= novelty[(size_t) i + 1] && novelty[(size_t) i] > 0.15 * nmax)
        {
            double w = 1.0; if (i % 8 == 0) w = 1.5; else if (i % 4 == 0) w = 1.25; else if (i % 2 == 0) w = 1.0; else w = 0.7;
            peaks.push_back ({ i, novelty[(size_t) i] / nmax * w });
        }
    std::sort (peaks.begin(), peaks.end(), [] (auto& a, auto& b) { return a.second > b.second; });
    std::vector<int> bounds { 0 };
    for (auto& [i, s] : peaks)
    {
        bool tooClose = false; for (int b : bounds) if (std::abs (b - i) < 4) tooClose = true;
        if (! tooClose) bounds.push_back (i);
        if ((int) bounds.size() > numBars / 4 + 1) break;
    }
    std::sort (bounds.begin(), bounds.end());
    bounds.push_back (numBars);

    // ---- segment features & clustering by similarity (agglomerative with a threshold)
    struct Seg { int b0, b1; BarFeature mean; int cluster = -1; double novelty = 0; };
    std::vector<Seg> segs;
    for (size_t i = 0; i + 1 < bounds.size(); ++i)
    {
        Seg s { bounds[i], bounds[i + 1], {}, -1, bounds[i] > 0 ? novelty[(size_t) bounds[i]] / nmax : 1.0 };
        int cnt = 0;
        for (int b = s.b0; b < s.b1; ++b) { auto& f = feats[(size_t) b]; for (int k = 0; k < 12; ++k) s.mean.chroma[(size_t) k] += f.chroma[(size_t) k]; for (int k = 0; k < 8; ++k) s.mean.bands[(size_t) k] += f.bands[(size_t) k]; s.mean.energy += f.energy; ++cnt; }
        if (cnt) { for (auto& v : s.mean.chroma) v /= cnt; for (auto& v : s.mean.bands) v /= cnt; s.mean.energy /= cnt; }
        segs.push_back (s);
    }
    // adaptive threshold: a fraction of the median pairwise distance, so repetitive tracks still split into groups
    std::vector<double> dists;
    for (size_t i = 0; i < segs.size(); ++i) for (size_t j = i + 1; j < segs.size(); ++j) dists.push_back (featureDistance (segs[i].mean, segs[j].mean));
    double thresh = 0.12;
    if (! dists.empty()) { std::sort (dists.begin(), dists.end()); thresh = std::min (0.12, std::max (0.03, dists[dists.size() / 2] * 0.6)); }
    int numClusters = 0;
    for (size_t i = 0; i < segs.size(); ++i)
    {
        if (segs[i].cluster >= 0) continue;
        segs[i].cluster = numClusters++;
        for (size_t j = i + 1; j < segs.size(); ++j)
            if (segs[j].cluster < 0 && featureDistance (segs[i].mean, segs[j].mean) < thresh) segs[j].cluster = segs[i].cluster;
    }
    // cluster stats
    std::vector<int> clusterCount ((size_t) numClusters, 0); std::vector<double> clusterEnergy ((size_t) numClusters, 0.0);
    for (auto& s : segs) { clusterCount[(size_t) s.cluster]++; clusterEnergy[(size_t) s.cluster] += s.mean.energy; }
    for (int c = 0; c < numClusters; ++c) clusterEnergy[(size_t) c] /= std::max (1, clusterCount[(size_t) c]);
    double maxEnergy = 1e-9; for (double e : clusterEnergy) maxEnergy = std::max (maxEnergy, e);
    // chorus = repeated cluster with highest energy; verse = other repeated cluster with the most occurrences
    int chorusCluster = -1, verseCluster = -1; double bestChorus = -1; int bestVerseCount = 0;
    for (int c = 0; c < numClusters; ++c)
        if (clusterCount[(size_t) c] >= 2 && clusterEnergy[(size_t) c] > bestChorus) { bestChorus = clusterEnergy[(size_t) c]; chorusCluster = c; }
    for (int c = 0; c < numClusters; ++c)
        if (c != chorusCluster && clusterCount[(size_t) c] >= 2 && clusterCount[(size_t) c] >= bestVerseCount) { bestVerseCount = clusterCount[(size_t) c]; verseCluster = c; }
    if (chorusCluster < 0)   // no repetition found: loudest segment is the chorus
    {
        double e = -1; for (int c = 0; c < numClusters; ++c) if (clusterEnergy[(size_t) c] > e) { e = clusterEnergy[(size_t) c]; chorusCluster = c; }
    }
    // if the chorus cluster swallowed most of the song, keep only its loudest members as chorus
    {
        int inChorus = 0; for (auto& s : segs) if (s.cluster == chorusCluster) ++inChorus;
        if (inChorus > (int) segs.size() / 2 && inChorus >= 3)
        {
            std::vector<double> e; for (auto& s : segs) if (s.cluster == chorusCluster) e.push_back (s.mean.energy);
            std::sort (e.begin(), e.end());
            const double cut = e[e.size() / 2];
            const int verseC = numClusters++;
            clusterCount.push_back (0); clusterEnergy.push_back (0.0);
            for (auto& s : segs) if (s.cluster == chorusCluster && s.mean.energy < cut) { s.cluster = verseC; clusterCount[(size_t) verseC]++; clusterCount[(size_t) chorusCluster]--; }
            if (clusterCount[(size_t) verseC] >= 1) verseCluster = verseC;
        }
    }

    for (size_t i = 0; i < segs.size(); ++i)
    {
        auto& s = segs[i];
        Phrase ph; ph.start = downbeats[(size_t) s.b0]; ph.end = s.b1 < numBars ? downbeats[(size_t) s.b1] : length;
        const double relEnergy = s.mean.energy / maxEnergy;
        double conf = 0.35 + 0.4 * std::min (1.0, s.novelty) ;
        if (i == 0 && relEnergy < 0.7) { ph.label = "intro"; conf += 0.15; }
        else if (i == segs.size() - 1 && relEnergy < 0.7 && s.cluster != chorusCluster) { ph.label = "outro"; conf += 0.1; }
        else if (s.cluster == chorusCluster) { ph.label = "chorus"; conf += clusterCount[(size_t) s.cluster] >= 2 ? 0.2 : 0.0; }
        else if (s.cluster == verseCluster) { ph.label = "verse"; conf += 0.1; }
        else if (i + 1 < segs.size() && segs[i + 1].cluster == chorusCluster && relEnergy > 0.5) { ph.label = "pre-chorus"; }
        else if (i > 0 && i + 1 < segs.size() && clusterCount[(size_t) s.cluster] == 1 && relEnergy < 0.6) { ph.label = "bridge"; }
        else if (relEnergy < 0.35) ph.label = i < segs.size() / 2 ? "intro" : "breakdown";
        else ph.label = clusterCount[(size_t) s.cluster] >= 2 ? "verse" : "section";
        ph.confidence = (float) std::clamp (conf, 0.0, 1.0);
        out.push_back (ph);
    }
    // merge consecutive identical labels that are short (< 4 bars)
    for (size_t i = 1; i < out.size();)
    {
        const double bars = (out[i].end - out[i].start) / (240.0 / std::max (bpm, 40.0));
        if (out[i].label == out[i - 1].label && bars < 4.0) { out[i - 1].end = out[i].end; out.erase (out.begin() + (long) i); }
        else ++i;
    }
    return out;
}
} // namespace mashup::analysis
