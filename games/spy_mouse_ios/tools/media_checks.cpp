// media_checks: the AAC decoder's own checks (research and regression tool).
//
//   media_checks                       transforms and the SBR filterbank's reconstruction
//   media_checks FILE.m4a OUT.raw      decode a file (interleaved float32 stereo at its rate)
#include "media/aac_stream.hpp"
#include "media/sbr_decoder.hpp"
#include "media/transforms.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <memory>
#include <iterator>
#include <vector>

using namespace doc::media;

static int failures = 0;
static void check(bool ok, const char* what, double value) {
    std::printf("%s %s (%g)\n", ok ? "ok  " : "FAIL", what, value);
    if (!ok) ++failures;
}

int main(int argc, char** argv) {
    if (argc == 3) {
        std::ifstream in(argv[1], std::ios::binary);
        std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        std::string error;
        auto stream = AacStream::open(std::make_shared<const std::vector<std::uint8_t>>(std::move(bytes)), 44100, error);
        if (!stream) {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }
        std::ofstream out(argv[2], std::ios::binary);
        std::vector<float> buffer(4096 * 2);
        long total = 0;
        for (int n; (n = stream->read(buffer.data(), 4096)) > 0;) {
            out.write(reinterpret_cast<const char*>(buffer.data()), std::streamsize(n) * 8);
            total += n;
        }
        std::printf("%ld frames at %d Hz (decoder %d Hz)\n", total, 44100, stream->decoderRate());
        return 0;
    }
    // IMDCT against the defining sum.
    for (int n : {256, 2048}) {
        std::vector<float> spec(static_cast<std::size_t>(n / 2)), fast(static_cast<std::size_t>(n));
        for (int k = 0; k < n / 2; ++k) spec[std::size_t(k)] = float(std::sin(k * 0.37) * 1000.0 / (k + 1));
        imdct(spec.data(), fast.data(), n);
        double err = 0.0, ref = 0.0;
        const double n0 = (n / 2.0 + 1.0) / 2.0;
        for (int i = 0; i < n; ++i) {
            double s = 0.0;
            for (int k = 0; k < n / 2; ++k) s += spec[std::size_t(k)] * std::cos(2.0 * M_PI / n * (i + n0) * (k + 0.5));
            s *= 2.0 / n;
            err = std::max(err, std::abs(s - fast[std::size_t(i)]));
            ref = std::max(ref, std::abs(s));
        }
        check(err < ref * 1e-4, n == 256 ? "short IMDCT matches its definition" : "long IMDCT matches its definition", err / ref);
    }
    // The SBR filterbank with no SBR data: a 22.05 kHz sine comes out at 44.1 kHz unchanged.
    {
        SbrDecoder sbr(44100, 1);
        std::vector<float> outAll;
        float core[1024], out[2048];
        long t = 0;
        for (int f = 0; f < 20; ++f) {
            for (int i = 0; i < 1024; ++i, ++t) core[i] = float(10000.0 * std::sin(2.0 * M_PI * 1000.0 * t / 22050.0));
            sbr.beginFrame();
            sbr.process(0, core, out);
            outAll.insert(outAll.end(), out, out + 2048);
        }
        // Best delay and the error against the ideal 44.1 kHz sine.
        double best = 1e30;
        int bestDelay = 0;
        double bestGain = 0;
        for (int d = 0; d < 2000; ++d) {
            double num = 0, den = 0;
            for (int i = 10000; i < 30000; ++i) {
                const double ideal = 10000.0 * std::sin(2.0 * M_PI * 1000.0 * (i - d) / 44100.0);
                num += outAll[std::size_t(i)] * ideal;
                den += ideal * ideal;
            }
            const double g = num / den;
            double e = 0;
            for (int i = 10000; i < 30000; i += 7) {
                const double ideal = 10000.0 * std::sin(2.0 * M_PI * 1000.0 * (i - d) / 44100.0);
                e += (outAll[std::size_t(i)] - ideal) * (outAll[std::size_t(i)] - ideal);
            }
            if (e < best) {
                best = e;
                bestDelay = d;
                bestGain = g;
            }
        }
        double sig = 0, err = 0;
        for (int i = 10000; i < 30000; ++i) {
            const double ideal = 10000.0 * std::sin(2.0 * M_PI * 1000.0 * (i - bestDelay) / 44100.0);
            sig += ideal * ideal;
            err += (outAll[std::size_t(i)] - ideal) * (outAll[std::size_t(i)] - ideal);
        }
        const double snr = 10.0 * std::log10(sig / std::max(err, 1e-30));
        std::printf("     filterbank delay %d samples, gain %.5f\n", bestDelay, bestGain);
        check(std::abs(bestGain - 1.0) < 0.01, "the SBR filterbank passes the core at unit gain", bestGain);
        check(snr > 60.0, "... and reconstructs it (SNR dB)", snr);
    }
    std::printf("%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
