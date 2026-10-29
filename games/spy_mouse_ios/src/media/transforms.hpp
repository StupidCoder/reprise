// Transforms for the decoder: a complex FFT, DCT-IV and the AAC inverse MDCT.
#pragma once

#include <complex>

namespace doc::media {

// In-place forward FFT (exp(-i...)) of n complex values; n a power of two (up to 4096).
void fft(std::complex<float>* data, int n);
// DCT-IV of n values (n a power of two, >= 4): out[k] = sum x[i] cos(pi/n (i + 1/2)(k + 1/2)).
void dct4(const float* in, float* out, int n);
// The AAC inverse MDCT (14496-3, 4.6.11.3.1): n output samples from n/2 coefficients,
// x[i] = 2/n sum spec[k] cos(2 pi/n (i + n0)(k + 1/2)), n0 = (n/2 + 1)/2.
void imdct(const float* spectrum, float* out, int n);

}  // namespace doc::media
