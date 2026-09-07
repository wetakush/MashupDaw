#include "FFT.h"
#include <cmath>
#include <cstring>

namespace mashup::dsp
{
std::mutex& FFT::planLock() { static std::mutex m; return m; }

FFT::FFT (int size) : n (size)
{
    inBuf = (float*) fftwf_malloc (sizeof (float) * (size_t) n);
    outBuf = (fftwf_complex*) fftwf_malloc (sizeof (fftwf_complex) * (size_t) (n / 2 + 1));
    std::lock_guard<std::mutex> l (planLock());
    fwd = fftwf_plan_dft_r2c_1d (n, inBuf, outBuf, FFTW_ESTIMATE);
    inv = fftwf_plan_dft_c2r_1d (n, outBuf, inBuf, FFTW_ESTIMATE);
}

FFT::~FFT()
{
    { std::lock_guard<std::mutex> l (planLock()); fftwf_destroy_plan (fwd); fftwf_destroy_plan (inv); }
    fftwf_free (inBuf); fftwf_free (outBuf);
}

void FFT::forward (const float* in, std::complex<float>* out) noexcept
{
    std::memcpy (inBuf, in, sizeof (float) * (size_t) n);
    fftwf_execute (fwd);
    std::memcpy (out, outBuf, sizeof (std::complex<float>) * (size_t) (n / 2 + 1));
}

void FFT::magnitudes (const float* in, float* outMag) noexcept
{
    std::memcpy (inBuf, in, sizeof (float) * (size_t) n);
    fftwf_execute (fwd);
    for (int i = 0; i < n / 2 + 1; ++i) outMag[i] = std::sqrt (outBuf[i][0] * outBuf[i][0] + outBuf[i][1] * outBuf[i][1]);
}

void FFT::inverse (const std::complex<float>* in, float* out) noexcept
{
    std::memcpy (outBuf, in, sizeof (std::complex<float>) * (size_t) (n / 2 + 1));
    fftwf_execute (inv);
    std::memcpy (out, inBuf, sizeof (float) * (size_t) n);
}

void FFT::hann (std::vector<float>& w, int size)
{
    w.resize ((size_t) size);
    for (int i = 0; i < size; ++i) w[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * 3.14159265358979f * i / (size - 1));
}
} // namespace mashup::dsp
