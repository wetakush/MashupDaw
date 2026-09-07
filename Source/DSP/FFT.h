#pragma once
#include <vector>
#include <complex>
#include <mutex>
#include <fftw3.h>

namespace mashup::dsp
{
/** Real-to-complex FFT using FFTW (single precision). Plan creation is serialised (FFTW planner is not thread-safe). */
class FFT
{
public:
    explicit FFT (int size);
    ~FFT();
    int getSize() const noexcept { return n; }
    int getNumBins() const noexcept { return n / 2 + 1; }
    /** in: n real samples; out: n/2+1 complex bins. */
    void forward (const float* in, std::complex<float>* out) noexcept;
    /** Convenience: magnitudes of `in` (windowed by caller). */
    void magnitudes (const float* in, float* outMag) noexcept;
    /** out: n real samples (unnormalised, divide by n). */
    void inverse (const std::complex<float>* in, float* out) noexcept;

    static void hann (std::vector<float>& window, int size);

private:
    int n;
    float* inBuf; fftwf_complex* outBuf;
    fftwf_plan fwd, inv;
    static std::mutex& planLock();
};
} // namespace mashup::dsp
