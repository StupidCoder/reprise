#include "media/transforms.hpp"

#include <cmath>
#include <numbers>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace doc::media {
namespace {

struct FftPlan {
    std::vector<std::complex<float>> twiddle;  // exp(-2 pi i k / n), k < n/2
    std::vector<int> reversed;
};

const FftPlan& plan(int n) {
    static std::mutex mutex;
    static std::map<int, std::unique_ptr<FftPlan>> plans;
    std::lock_guard<std::mutex> lock(mutex);
    auto& p = plans[n];
    if (!p) {
        p = std::make_unique<FftPlan>();
        p->twiddle.resize(std::size_t(n / 2));
        for (int k = 0; k < n / 2; ++k) {
            const double a = -2.0 * std::numbers::pi * k / n;
            p->twiddle[std::size_t(k)] = {float(std::cos(a)), float(std::sin(a))};
        }
        int bits = 0;
        while ((1 << bits) < n) ++bits;
        p->reversed.resize(std::size_t(n));
        for (int i = 0; i < n; ++i) {
            int r = 0;
            for (int b = 0; b < bits; ++b)
                if (i & (1 << b)) r |= 1 << (bits - 1 - b);
            p->reversed[std::size_t(i)] = r;
        }
    }
    return *p;
}

struct Dct4Plan {
    std::vector<std::complex<float>> pre, post;
};

const Dct4Plan& dctPlan(int n) {
    static std::mutex mutex;
    static std::map<int, std::unique_ptr<Dct4Plan>> plans;
    std::lock_guard<std::mutex> lock(mutex);
    auto& p = plans[n];
    if (!p) {
        p = std::make_unique<Dct4Plan>();
        for (int k = 0; k < n / 2; ++k) {
            const double a = -std::numbers::pi * (4.0 * k + 1.0) / (4.0 * n), b = -std::numbers::pi * k / n;
            p->pre.push_back({float(std::cos(a)), float(std::sin(a))});
            p->post.push_back({float(std::cos(b)), float(std::sin(b))});
        }
    }
    return *p;
}

}  // namespace

void fft(std::complex<float>* data, int n) {
    const FftPlan& p = plan(n);
    for (int i = 0; i < n; ++i) {
        const int r = p.reversed[std::size_t(i)];
        if (r > i) std::swap(data[i], data[r]);
    }
    for (int size = 2; size <= n; size <<= 1) {
        const int half = size / 2, step = n / size;
        for (int start = 0; start < n; start += size)
            for (int k = 0; k < half; ++k) {
                const std::complex<float> t = data[start + k + half] * p.twiddle[std::size_t(k * step)];
                data[start + k + half] = data[start + k] - t;
                data[start + k] += t;
            }
    }
}

// DCT-IV through an n/2-point complex FFT: pre-twiddle, FFT, post-twiddle.
void dct4(const float* in, float* out, int n) {
    const Dct4Plan& p = dctPlan(n);
    std::complex<float> z[2048];
    for (int k = 0; k < n / 2; ++k) z[k] = std::complex<float>(in[2 * k], in[n - 1 - 2 * k]) * p.pre[std::size_t(k)];
    fft(z, n / 2);
    for (int k = 0; k < n / 2; ++k) {
        const std::complex<float> u = z[k] * p.post[std::size_t(k)];
        out[2 * k] = u.real();
        out[n - 1 - 2 * k] = -u.imag();
    }
}

void imdct(const float* spectrum, float* out, int n) {
    const int m = n / 2;
    float y[2048];
    dct4(spectrum, y, m);
    const float scale = 2.0f / float(n);
    // Unfolding (n0 = m/2 + 1/2): the DCT-IV basis continued over 2m samples.
    for (int i = 0; i < m / 2; ++i) out[i] = y[m / 2 + i] * scale;
    for (int i = m / 2; i < 3 * m / 2; ++i) out[i] = -y[3 * m / 2 - 1 - i] * scale;
    for (int i = 3 * m / 2; i < 2 * m; ++i) out[i] = -y[i - 3 * m / 2] * scale;
}

}  // namespace doc::media
