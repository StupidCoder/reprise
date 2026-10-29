// Spectral band replication (14496-3, 4.6.18): the upper half of HE-AAC's spectrum rebuilt
// from the AAC core and a few envelope values. The core's 1024 samples of a frame go through a
// 32-band QMF analysis; bands above the core are generated from the ones below (linear
// prediction, chirp), shaped to the transmitted envelopes with noise and sinusoids, and a
// 64-band QMF synthesis gives 2048 samples at twice the core's rate.
//
// The QMF prototype filter is designed here (a Kaiser-windowed 640-tap pseudo-QMF lowpass,
// scaled so analysis followed by synthesis passes the core unchanged) instead of the standard's
// tabulated one; the random noise table is generated. Both change the sound only negligibly.
#pragma once

#include <array>
#include <complex>
#include <cstddef>
#include <memory>
#include <vector>

namespace doc::media {

class BitReader;

class SbrDecoder {
public:
    // `rate`: the output (SBR) sampling rate; `channels`: 1 or 2.
    SbrDecoder(int rate, int channels);
    ~SbrDecoder();

    // Before a frame's elements: no SBR data seen yet for it.
    void beginFrame();
    // An SBR extension payload (`available` bits) for the element whose channels start at
    // `first` (`count` 1 or 2).
    void parse(BitReader& bits, std::size_t available, bool crc, int first, int count);
    // The core's 1024 samples of `channel` -> 2048 output samples.
    void process(int channel, const float* core, float* out);

    struct Channel;

private:
    struct Header {
        bool valid = false;
        int ampRes = 1, startFreq = 0, stopFreq = 0, xoverBand = 0;
        int freqScale = 2, alterScale = 1, noiseBands = 2;
        int limiterBands = 2, limiterGains = 2, interpolFreq = 1, smoothingMode = 1;
        bool sameTables(const Header& o) const {
            return startFreq == o.startFreq && stopFreq == o.stopFreq && xoverBand == o.xoverBand &&
                   freqScale == o.freqScale && alterScale == o.alterScale && noiseBands == o.noiseBands &&
                   limiterBands == o.limiterBands;
        }
    };
    bool readHeader(BitReader& bits);
    bool computeTables();
    bool readGrid(BitReader& bits, Channel& ch);
    void readDtdf(BitReader& bits, Channel& ch);
    void readInvf(BitReader& bits, Channel& ch);
    void readEnvelope(BitReader& bits, Channel& ch, bool coupled, bool balance);
    void readNoise(BitReader& bits, Channel& ch, bool coupled, bool balance);
    void readSinusoids(BitReader& bits, Channel& ch);
    void decodeEnvelopes(Channel& ch);
    void dequantize(Channel& ch, Channel* balance);
    void generateHighBand(Channel& ch);
    void adjustEnvelope(Channel& ch);

    int rate_ = 0, channelCount_ = 0;
    Header header_;
    bool tablesValid_ = false;
    // Frequency tables (4.6.18.3.2).
    int k0_ = 0, k2_ = 0, kx_ = 0, m_ = 0, kxPrevious_ = 0, mPrevious_ = 0;
    std::vector<int> master_, high_, low_, noise_, limiter_;
    int patchCount_ = 0;
    std::array<int, 6> patchStart_{}, patchSize_{};
    std::vector<std::unique_ptr<Channel>> channel_;
};

}  // namespace doc::media
