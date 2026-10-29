#include "media/sbr_decoder.hpp"

#include "media/aac_tables.hpp"
#include "media/bit_reader.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <cstdlib>
#include <cstring>

namespace doc::media {
namespace {

using Complex = std::complex<float>;
constexpr int kTimeSlots = 16;              // per 1024-sample frame
constexpr int kRate = 2;                    // QMF slots per time slot
constexpr int kSlots = kTimeSlots * kRate;  // 32
constexpr int kHistory = 8;                 // t_HFGen: earlier slots kept for prediction
constexpr int kAdjust = 2;                  // t_HFAdj
constexpr int kMaxSlots = kSlots + 8;       // Y may reach a few slots into the next frame

enum FrameClass { FixFix = 0, FixVar = 1, VarFix = 2, VarVar = 3 };

// The QMF prototype (640 taps): Kaiser-windowed sinc, cutoff and beta chosen for the
// pseudo-QMF power-complementary condition (design notes in the header). Scaled so analysis
// then synthesis has unit gain (the standard's own normalisation, 4.6.18.4).
struct Qmf {
    float window[640];
    float analysisCos[32][64], analysisSin[32][64];
    float synthesisCos[128][64], synthesisSin[128][64];
    Complex noise[512];
    Qmf() {
        const double beta = 8.5, cutoff = 0.60257 * std::numbers::pi / 64.0;
        double i0beta = 0.0;
        auto besselI0 = [](double x) {
            double sum = 1.0, term = 1.0;
            for (int k = 1; k < 60; ++k) {
                term *= (x / (2.0 * k)) * (x / (2.0 * k));
                sum += term;
            }
            return sum;
        };
        i0beta = besselI0(beta);
        // The standard's window is indexed 0..639 and centred on 320 (symmetric about it).
        double shifted[641];
        for (int n = 0; n <= 640; ++n) {
            const double t = n - 320.0;
            const double sinc = t == 0.0 ? cutoff / std::numbers::pi : std::sin(cutoff * t) / (std::numbers::pi * t);
            const double r = double(n) / 320.0 - 1.0;
            shifted[n] = sinc * besselI0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0beta;
        }
        double sum = 0.0;
        for (int n = 0; n < 640; ++n) sum += shifted[n];
        // Unit gain through analysis (factor 2) and synthesis (factor 1/64) needs a sum of
        // 64 sqrt 2; and as in the standard's table, every other block of 128 taps is negated:
        // the analysis folds its input in steps of 64 and the synthesis in blocks of 128, over
        // which the modulation changes sign (checked by media_checks: 71 dB reconstruction).
        for (int n = 0; n < 640; ++n)
            window[n] = float(shifted[n] * 64.0 * std::sqrt(2.0) / sum) * ((n / 128) % 2 ? -1.0f : 1.0f);
        for (int k = 0; k < 32; ++k)
            for (int n = 0; n < 64; ++n) {
                const double a = std::numbers::pi / 64.0 * (k + 0.5) * (2.0 * n - 0.5);
                analysisCos[k][n] = float(2.0 * std::cos(a));
                analysisSin[k][n] = float(2.0 * std::sin(a));
            }
        for (int n = 0; n < 128; ++n)
            for (int k = 0; k < 64; ++k) {
                const double a = std::numbers::pi / 128.0 * (k + 0.5) * (2.0 * n - 255.0);
                synthesisCos[n][k] = float(std::cos(a) / 64.0);
                synthesisSin[n][k] = float(std::sin(a) / 64.0);
            }
        // Noise for the noise floor: uniform, unit mean power, the same every run.
        std::uint32_t s = 0x2545F491u;
        double power = 0.0;
        for (Complex& v : noise) {
            s = s * 1664525u + 1013904223u;
            const float re = float(std::int32_t(s)) / 2147483648.0f;
            s = s * 1664525u + 1013904223u;
            const float im = float(std::int32_t(s)) / 2147483648.0f;
            v = {re, im};
            power += double(re) * re + double(im) * im;
        }
        const float scale = float(std::sqrt(512.0 / power));
        for (Complex& v : noise) v *= scale;
    }
};

const Qmf& qmf() {
    static const Qmf q;
    return q;
}

int huffman(BitReader& bits, const std::int8_t (*tree)[2]) {
    int index = 0;
    for (int depth = 0; depth < 32; ++depth) {
        index = tree[index][bits.bit()];
        if (index < 0) return index + 64;
    }
    return 0;
}

}  // namespace

struct SbrDecoder::Channel {
    // This frame's data (sbr_grid, dtdf, invf, envelope, noise, sinusoidal coding).
    int frameClass = FixFix, numEnv = 1, numNoise = 1, pointer = 0, transient = -1;
    int tE[6] = {0, kTimeSlots}, tQ[3] = {0, kTimeSlots};
    int freqRes[5] = {1, 1, 1, 1, 1};
    int dfEnv[5] = {}, dfNoise[2] = {};
    int invf[5] = {};
    int ampRes = 1;
    int env[5][64] = {}, noise[2][64] = {};
    bool harmonic[64] = {};
    bool harmonicPresent = false;
    bool received = false;
    // Dequantized.
    float eOrig[5][64] = {}, qOrig[2][64] = {};
    // Kept from the previous frame.
    int envPrevious[64] = {}, envPreviousRes = 1, noisePrevious[64] = {};
    int invfPrevious[5] = {};
    float bwPrevious[5] = {};
    bool harmonicPrevious[64] = {};
    int transientPrevious = -1, numEnvPrevious = 1, lastBorderPrevious = kTimeSlots;
    int indexNoise = 0, indexSine = 0;
    float gainHistory[4][64] = {}, noiseHistory[4][64] = {};
    bool historyValid = false;
    // Signals.
    float analysis[320] = {};
    float synthesis[1280] = {};
    Complex low[kHistory + kSlots][32] = {};  // X_low: 8 earlier slots, then this frame's 32
    Complex high[kHistory + kSlots][64] = {}; // X_high (same slot indexing as X_low)
    Complex y[kMaxSlots][64] = {}, yPrevious[kMaxSlots][64] = {};
    bool yPreviousValid = false;
};

SbrDecoder::SbrDecoder(int rate, int channels) : rate_(rate), channelCount_(channels) {
    for (int c = 0; c < channels; ++c) channel_.push_back(std::make_unique<Channel>());
    qmf();
}

SbrDecoder::~SbrDecoder() = default;

void SbrDecoder::beginFrame() {
    for (auto& ch : channel_) ch->received = false;
}

// sbr_header (4.4.2.8, Table 4.63).
bool SbrDecoder::readHeader(BitReader& bits) {
    Header h;
    h.valid = true;
    h.ampRes = int(bits.read(1));
    h.startFreq = int(bits.read(4));
    h.stopFreq = int(bits.read(4));
    h.xoverBand = int(bits.read(3));
    bits.read(2);
    const bool extra1 = bits.read(1) != 0, extra2 = bits.read(1) != 0;
    if (extra1) {
        h.freqScale = int(bits.read(2));
        h.alterScale = int(bits.read(1));
        h.noiseBands = int(bits.read(2));
    }
    if (extra2) {
        h.limiterBands = int(bits.read(2));
        h.limiterGains = int(bits.read(2));
        h.interpolFreq = int(bits.read(1));
        h.smoothingMode = int(bits.read(1));
    }
    const bool reset = !header_.valid || !header_.sameTables(h);
    header_ = h;
    if (reset) {
        tablesValid_ = computeTables();
        for (auto& ch : channel_) {
            std::memset(ch->envPrevious, 0, sizeof ch->envPrevious);
            std::memset(ch->noisePrevious, 0, sizeof ch->noisePrevious);
            std::memset(ch->harmonicPrevious, 0, sizeof ch->harmonicPrevious);
        }
    }
    return tablesValid_;
}

// Frequency band tables (4.6.18.3.2) and the HF generator's patches (4.6.18.6.3).
bool SbrDecoder::computeTables() {
    const int fs = rate_;
    // Start: startMin plus the offset table row for the rate.
    static const int offsets[7][16] = {
        {-8, -7, -6, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6, 7},
        {-5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6, 7, 9, 11, 13},
        {-5, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6, 7, 9, 11, 13, 16},
        {-6, -4, -2, -1, 0, 1, 2, 3, 4, 5, 6, 7, 9, 11, 13, 16},
        {-4, -2, -1, 0, 1, 2, 3, 4, 5, 6, 7, 9, 11, 13, 16, 20},
        {-2, -1, 0, 1, 2, 3, 4, 5, 6, 7, 9, 11, 13, 16, 20, 24},
        {0, 1, 2, 3, 4, 5, 6, 7, 9, 11, 13, 16, 20, 24, 28, 33}};
    int row, startMin;
    switch (fs) {
    case 16000: row = 0; startMin = 24; break;
    case 22050: row = 1; startMin = 17; break;
    case 24000: row = 2; startMin = 16; break;
    case 32000: row = 3; startMin = 16; break;
    case 44100: row = 4; startMin = 12; break;
    case 48000: row = 4; startMin = 11; break;
    case 64000: row = 4; startMin = 10; break;
    default: row = 5; startMin = 7; break;
    }
    k0_ = startMin + offsets[row][header_.startFreq];
    // Stop: stopMin plus the first stopFreq of thirteen logarithmic steps up to 64.
    const double stopBase = fs < 32000 ? 6000.0 : fs < 64000 ? 8000.0 : 10000.0;
    const int stopMin = int(std::lround(stopBase * 128.0 / fs));
    if (header_.stopFreq == 14) k2_ = 2 * k0_;
    else if (header_.stopFreq == 15) k2_ = 3 * k0_;
    else {
        int dk[13];
        for (int p = 0; p < 13; ++p)
            dk[p] = int(std::lround(stopMin * std::pow(64.0 / stopMin, (p + 1) / 13.0))) -
                    int(std::lround(stopMin * std::pow(64.0 / stopMin, p / 13.0)));
        std::sort(dk, dk + 13);
        k2_ = stopMin;
        for (int p = 0; p < header_.stopFreq; ++p) k2_ += dk[p];
    }
    k2_ = std::min(k2_, 64);
    if (k0_ >= k2_ || k2_ - k0_ > 48) return false;

    // Master table.
    master_.clear();
    if (header_.freqScale == 0) {
        const int dk = header_.alterScale ? 2 : 1;
        const int bands = 2 * ((k2_ - k0_) / (dk * 2));
        if (bands <= 0) return false;
        std::vector<int> vdk(std::size_t(bands), dk);
        int diff = k2_ - (k0_ + bands * dk);
        int incr = diff < 0 ? 1 : -1, k = diff < 0 ? 0 : bands - 1;
        while (diff != 0 && k >= 0 && k < bands) {
            vdk[std::size_t(k)] -= incr;
            k += incr;
            diff += incr;
        }
        master_.push_back(k0_);
        for (int v : vdk) master_.push_back(master_.back() + v);
    } else {
        const int bandsPerOctave = header_.freqScale == 1 ? 12 : header_.freqScale == 2 ? 10 : 8;
        const double warp = header_.alterScale ? 1.3 : 1.0;
        const bool twoRegions = double(k2_) / k0_ > 2.2449;
        const int k1 = twoRegions ? 2 * k0_ : k2_;
        auto steps = [](int from, int to, int bands) {
            std::vector<int> d;
            for (int k = 0; k < bands; ++k)
                d.push_back(int(std::lround(from * std::pow(double(to) / from, double(k + 1) / bands))) -
                            int(std::lround(from * std::pow(double(to) / from, double(k) / bands))));
            std::sort(d.begin(), d.end());
            return d;
        };
        const int bands0 = 2 * int(std::lround(bandsPerOctave * std::log(double(k1) / k0_) / (2.0 * std::log(2.0))));
        if (bands0 <= 0) return false;
        std::vector<int> d0 = steps(k0_, k1, bands0);
        master_.push_back(k0_);
        for (int v : d0) master_.push_back(master_.back() + v);
        if (twoRegions) {
            const int bands1 = 2 * int(std::lround(bandsPerOctave * std::log(double(k2_) / k1) / (2.0 * std::log(2.0) * warp)));
            if (bands1 > 0) {
                std::vector<int> d1 = steps(k1, k2_, bands1);
                if (d1.front() < d0.back()) {
                    const int change = d0.back() - d1.front();
                    d1.front() += change;
                    d1.back() -= change;
                    std::sort(d1.begin(), d1.end());
                }
                for (int v : d1) master_.push_back(master_.back() + v);
            }
        }
    }
    const int nMaster = int(master_.size()) - 1;
    if (header_.xoverBand >= nMaster) return false;
    // High and low resolution tables, noise bands.
    high_.assign(master_.begin() + header_.xoverBand, master_.end());
    const int nHigh = int(high_.size()) - 1;
    const int nLow = nHigh / 2 + (nHigh & 1);
    low_.clear();
    for (int k = 0; k <= nLow; ++k) low_.push_back(high_[std::size_t(k == 0 ? 0 : 2 * k - (nHigh & 1))]);
    kx_ = high_.front();
    m_ = high_.back() - kx_;
    if (kx_ > 32 || kx_ + m_ > 64) return false;
    int nQ = std::max(1, int(std::lround(header_.noiseBands * std::log(double(high_.back()) / kx_) / std::log(2.0))));
    nQ = std::min(nQ, 5);
    noise_.clear();
    int i = 0;
    noise_.push_back(low_[0]);
    for (int k = 1; k <= nQ; ++k) {
        i += (nLow - i) / (nQ + 1 - k);
        noise_.push_back(low_[std::size_t(i)]);
    }

    // Patches.
    patchCount_ = 0;
    int msb = k0_, usb = kx_;
    const int goal = int(std::lround(2.048e6 / fs));
    int k = 0;
    if (goal < kx_ + m_) {
        for (int j = 0; master_[std::size_t(j)] < goal; ++j) k = j + 1;
    } else {
        k = nMaster;
    }
    int sb = 0;
    do {
        int j = k + 1, odd = 0;
        do {
            --j;
            sb = master_[std::size_t(j)];
            odd = (sb - 2 + k0_) % 2;
        } while (sb > k0_ - 1 + msb - odd && j > 0);
        const int size = std::max(sb - usb, 0);
        if (patchCount_ >= 6) return false;
        patchSize_[std::size_t(patchCount_)] = size;
        patchStart_[std::size_t(patchCount_)] = k0_ - odd - size;
        if (size > 0) {
            usb = sb;
            msb = sb;
            ++patchCount_;
        } else {
            msb = kx_;
        }
        if (master_[std::size_t(k)] - sb < 3) k = nMaster;
    } while (sb != kx_ + m_);
    if (patchCount_ > 1 && patchSize_[std::size_t(patchCount_ - 1)] < 3) --patchCount_;

    // Limiter bands (4.6.18.3.2.3).
    limiter_.clear();
    if (header_.limiterBands == 0) {
        limiter_ = {low_.front(), low_.back()};
    } else {
        const double perOctave = header_.limiterBands == 1 ? 1.2 : header_.limiterBands == 2 ? 2.0 : 3.0;
        std::vector<int> patchBorders{kx_};
        for (int p = 0; p < patchCount_; ++p) patchBorders.push_back(patchBorders.back() + patchSize_[std::size_t(p)]);
        std::vector<int> table(low_.begin(), low_.end());
        for (std::size_t p = 1; p + 1 < patchBorders.size(); ++p) table.push_back(patchBorders[p]);
        std::sort(table.begin(), table.end());
        auto isPatchBorder = [&](int v) { return std::find(patchBorders.begin(), patchBorders.end(), v) != patchBorders.end(); };
        std::size_t at = 1;
        while (at < table.size()) {
            const double octaves = std::log(double(table[at]) / table[at - 1]) / std::log(2.0);
            if (octaves * perOctave < 0.49) {
                if (table[at] == table[at - 1]) {
                    table.erase(table.begin() + std::ptrdiff_t(at));
                } else if (isPatchBorder(table[at])) {
                    if (isPatchBorder(table[at - 1])) ++at;
                    else table.erase(table.begin() + std::ptrdiff_t(at - 1));
                } else {
                    table.erase(table.begin() + std::ptrdiff_t(at));
                }
            } else {
                ++at;
            }
        }
        limiter_ = table;
    }
    return true;
}

// sbr_grid (Table 4.65) and the time borders it implies (4.6.18.3.3).
bool SbrDecoder::readGrid(BitReader& bits, Channel& ch) {
    ch.frameClass = int(bits.read(2));
    int absLead = 0, absTrail = kTimeSlots, relLead[4] = {}, relTrail[4] = {}, numRelLead = 0, numRelTrail = 0;
    auto ceilLog2 = [](int v) {
        int b = 0;
        while ((1 << b) < v) ++b;
        return b;
    };
    switch (ch.frameClass) {
    case FixFix: {
        ch.numEnv = 1 << bits.read(2);
        const int res = int(bits.read(1));
        for (int e = 0; e < ch.numEnv; ++e) ch.freqRes[e] = res;
        numRelLead = ch.numEnv - 1;
        for (int i = 0; i < numRelLead; ++i) relLead[i] = kTimeSlots / ch.numEnv;
        ch.pointer = 0;
        break;
    }
    case FixVar: {
        absTrail = int(bits.read(2)) + kTimeSlots;
        numRelTrail = int(bits.read(2));
        for (int i = 0; i < numRelTrail; ++i) relTrail[i] = 2 * int(bits.read(2)) + 2;
        ch.numEnv = numRelTrail + 1;
        ch.pointer = int(bits.read(ceilLog2(ch.numEnv + 1)));
        for (int e = 0; e < ch.numEnv; ++e) ch.freqRes[ch.numEnv - 1 - e] = int(bits.read(1));
        break;
    }
    case VarFix: {
        absLead = int(bits.read(2));
        numRelLead = int(bits.read(2));
        for (int i = 0; i < numRelLead; ++i) relLead[i] = 2 * int(bits.read(2)) + 2;
        ch.numEnv = numRelLead + 1;
        ch.pointer = int(bits.read(ceilLog2(ch.numEnv + 1)));
        for (int e = 0; e < ch.numEnv; ++e) ch.freqRes[e] = int(bits.read(1));
        break;
    }
    default: {
        absLead = int(bits.read(2));
        absTrail = int(bits.read(2)) + kTimeSlots;
        numRelLead = int(bits.read(2));
        numRelTrail = int(bits.read(2));
        for (int i = 0; i < numRelLead; ++i) relLead[i] = 2 * int(bits.read(2)) + 2;
        for (int i = 0; i < numRelTrail; ++i) relTrail[i] = 2 * int(bits.read(2)) + 2;
        ch.numEnv = numRelLead + numRelTrail + 1;
        if (ch.numEnv > 5) return false;
        ch.pointer = int(bits.read(ceilLog2(ch.numEnv + 1)));
        for (int e = 0; e < ch.numEnv; ++e) ch.freqRes[e] = int(bits.read(1));
        break;
    }
    }
    if (ch.numEnv > 5 || ch.numEnv < 1) return false;
    ch.numNoise = ch.numEnv > 1 ? 2 : 1;
    ch.tE[0] = absLead;
    ch.tE[ch.numEnv] = absTrail;
    for (int l = 1; l <= numRelLead && l < ch.numEnv; ++l) ch.tE[l] = ch.tE[l - 1] + relLead[l - 1];
    for (int l = ch.numEnv - 1; l > numRelLead; --l) ch.tE[l] = ch.tE[l + 1] - relTrail[ch.numEnv - 1 - l];
    // A FIXFIX frame with a single envelope is coded in 1.5 dB steps whatever the header says.
    ch.ampRes = header_.ampRes;
    if (ch.frameClass == FixFix && ch.numEnv == 1) ch.ampRes = 0;
    // Transient envelope and the noise borders.
    switch (ch.frameClass) {
    case FixFix: ch.transient = -1; break;
    case VarFix: ch.transient = ch.pointer <= 1 ? -1 : ch.pointer - 1; break;
    default: ch.transient = ch.pointer == 0 ? -1 : ch.numEnv + 1 - ch.pointer; break;
    }
    ch.tQ[0] = ch.tE[0];
    if (ch.numEnv == 1) {
        ch.tQ[1] = ch.tE[1];
    } else {
        int middle;
        switch (ch.frameClass) {
        case FixFix: middle = ch.numEnv / 2; break;
        case VarFix: middle = ch.pointer == 0 ? 1 : ch.pointer == 1 ? ch.numEnv - 1 : ch.pointer - 1; break;
        default: middle = ch.pointer > 1 ? ch.numEnv + 1 - ch.pointer : ch.numEnv - 1; break;
        }
        middle = std::clamp(middle, 1, ch.numEnv - 1);
        ch.tQ[1] = ch.tE[middle];
        ch.tQ[2] = ch.tE[ch.numEnv];
    }
    for (int l = 0; l < ch.numEnv; ++l)
        if (ch.tE[l] >= ch.tE[l + 1]) return false;
    return true;
}

void SbrDecoder::readDtdf(BitReader& bits, Channel& ch) {
    for (int e = 0; e < ch.numEnv; ++e) ch.dfEnv[e] = int(bits.read(1));
    for (int n = 0; n < ch.numNoise; ++n) ch.dfNoise[n] = int(bits.read(1));
}

void SbrDecoder::readInvf(BitReader& bits, Channel& ch) {
    for (int n = 0; n + 1 < int(noise_.size()); ++n) ch.invf[n] = int(bits.read(2));
}

// sbr_envelope (Table 4.68): Huffman-coded deltas in time or frequency.
void SbrDecoder::readEnvelope(BitReader& bits, Channel& ch, bool coupled, bool balance) {
    const bool fine = ch.ampRes == 0;
    const std::int8_t(*timeTree)[2];
    const std::int8_t(*freqTree)[2];
    int startBits;
    if (coupled && balance) {
        timeTree = fine ? k_bookSbrEnvBalance10T : k_bookSbrEnvBalance11T;
        freqTree = fine ? k_bookSbrEnvBalance10F : k_bookSbrEnvBalance11F;
        startBits = fine ? 6 : 5;
    } else {
        timeTree = fine ? k_bookSbrEnvLevel10T : k_bookSbrEnvLevel11T;
        freqTree = fine ? k_bookSbrEnvLevel10F : k_bookSbrEnvLevel11F;
        startBits = fine ? 7 : 6;
    }
    const int nHigh = int(high_.size()) - 1, nLow = int(low_.size()) - 1;
    const int shift = (coupled && balance) ? 1 : 0;  // balance values are stored doubled
    for (int e = 0; e < ch.numEnv; ++e) {
        const int bands = ch.freqRes[e] ? nHigh : nLow;
        if (ch.dfEnv[e] == 0) {
            ch.env[e][0] = int(bits.read(startBits)) << shift;
            for (int b = 1; b < bands; ++b) ch.env[e][b] = huffman(bits, freqTree) << shift;
        } else {
            for (int b = 0; b < bands; ++b) ch.env[e][b] = huffman(bits, timeTree) << shift;
        }
    }
}

// sbr_noise (Table 4.69).
void SbrDecoder::readNoise(BitReader& bits, Channel& ch, bool coupled, bool balance) {
    const std::int8_t(*timeTree)[2] = (coupled && balance) ? k_bookSbrNoiseBalance11T : k_bookSbrNoiseLevel11T;
    const std::int8_t(*freqTree)[2] = (coupled && balance) ? k_bookSbrEnvBalance11F : k_bookSbrEnvLevel11F;
    const int shift = (coupled && balance) ? 1 : 0;
    const int bands = int(noise_.size()) - 1;
    for (int n = 0; n < ch.numNoise; ++n) {
        if (ch.dfNoise[n] == 0) {
            ch.noise[n][0] = int(bits.read(5)) << shift;
            for (int b = 1; b < bands; ++b) ch.noise[n][b] = huffman(bits, freqTree) << shift;
        } else {
            for (int b = 0; b < bands; ++b) ch.noise[n][b] = huffman(bits, timeTree) << shift;
        }
    }
}

void SbrDecoder::readSinusoids(BitReader& bits, Channel& ch) {
    const int nHigh = int(high_.size()) - 1;
    for (int b = 0; b < nHigh; ++b) ch.harmonic[b] = bits.read(1) != 0;
}

// Delta decoding (4.6.18.3.3): frequency deltas add up from the first value; time deltas add to
// the previous envelope (mapped between the two resolutions).
void SbrDecoder::decodeEnvelopes(Channel& ch) {
    const int nHigh = int(high_.size()) - 1, nLow = int(low_.size()) - 1;
    for (int e = 0; e < ch.numEnv; ++e) {
        const int res = ch.freqRes[e];
        const int bands = res ? nHigh : nLow;
        const int previousRes = e == 0 ? ch.envPreviousRes : ch.freqRes[e - 1];
        const int* previous = e == 0 ? ch.envPrevious : ch.env[e - 1];
        if (ch.dfEnv[e] == 0) {
            for (int b = 1; b < bands; ++b) ch.env[e][b] += ch.env[e][b - 1];
        } else {
            for (int b = 0; b < bands; ++b) {
                int from = b;
                if (res != previousRes) {
                    if (res) {  // high from low: the low band holding the high band's start
                        from = 0;
                        for (int i = 0; i < nLow; ++i)
                            if (low_[std::size_t(i)] <= high_[std::size_t(b)]) from = i;
                    } else {    // low from high: the high band starting at the low band's start
                        from = 0;
                        for (int i = 0; i < nHigh; ++i)
                            if (high_[std::size_t(i)] == low_[std::size_t(b)]) from = i;
                    }
                }
                ch.env[e][b] += previous[from];
            }
        }
    }
    const int noiseBands = int(noise_.size()) - 1;
    for (int n = 0; n < ch.numNoise; ++n) {
        const int* previous = n == 0 ? ch.noisePrevious : ch.noise[n - 1];
        for (int b = 0; b < noiseBands; ++b) {
            if (ch.dfNoise[n] == 0) {
                if (b > 0) ch.noise[n][b] += ch.noise[n][b - 1];
            } else {
                ch.noise[n][b] += previous[b];
            }
        }
    }
    std::copy(ch.env[ch.numEnv - 1], ch.env[ch.numEnv - 1] + 64, ch.envPrevious);
    ch.envPreviousRes = ch.freqRes[ch.numEnv - 1];
    std::copy(ch.noise[ch.numNoise - 1], ch.noise[ch.numNoise - 1] + 64, ch.noisePrevious);
}

// Dequantization (4.6.18.3.5). With coupling, the first channel holds the level and `balance`
// (the second) the pan, both kept doubled for balance (as read); the pan offset is 12 steps of
// 3 dB or 24 of 1.5 dB.
void SbrDecoder::dequantize(Channel& ch, Channel* balance) {
    const int nHigh = int(high_.size()) - 1, nLow = int(low_.size()) - 1;
    const float alpha = ch.ampRes ? 1.0f : 0.5f;
    const float panOffset = ch.ampRes ? 12.0f : 24.0f;
    const int noiseBands = int(noise_.size()) - 1;
    for (int e = 0; e < ch.numEnv; ++e) {
        const int bands = ch.freqRes[e] ? nHigh : nLow;
        for (int b = 0; b < bands; ++b) {
            if (!balance) {
                ch.eOrig[e][b] = std::exp2(float(ch.env[e][b]) * alpha + 6.0f);
            } else {
                const float level = std::exp2(float(ch.env[e][b]) * alpha + 7.0f);
                const float pan = std::exp2((panOffset - float(balance->env[e][b])) * alpha);
                ch.eOrig[e][b] = level / (1.0f + pan);
                balance->eOrig[e][b] = ch.eOrig[e][b] * pan;
            }
        }
    }
    for (int n = 0; n < ch.numNoise; ++n)
        for (int b = 0; b < noiseBands; ++b) {
            if (!balance) {
                ch.qOrig[n][b] = std::exp2(6.0f - float(ch.noise[n][b]));
            } else {
                const float level = std::exp2(6.0f - float(ch.noise[n][b]) + 1.0f);
                const float pan = std::exp2(12.0f - float(balance->noise[n][b]));
                ch.qOrig[n][b] = level / (1.0f + pan);
                balance->qOrig[n][b] = ch.qOrig[n][b] * pan;
            }
        }
}

void SbrDecoder::parse(BitReader& bits, std::size_t available, bool crc, int first, int count) {
    const std::size_t start = bits.position();
    if (crc) bits.read(10);
    if (bits.read(1) && !readHeader(bits)) return;
    if (!header_.valid || !tablesValid_ || first + count > channelCount_) return;
    Channel& a = *channel_[std::size_t(first)];
    if (count == 1) {
        if (bits.read(1)) bits.read(4);  // bs_data_extra, reserved
        if (!readGrid(bits, a)) return;
        readDtdf(bits, a);
        readInvf(bits, a);
        readEnvelope(bits, a, false, false);
        readNoise(bits, a, false, false);
        a.harmonicPresent = bits.read(1) != 0;
        std::fill(a.harmonic, a.harmonic + 64, false);
        if (a.harmonicPresent) readSinusoids(bits, a);
        decodeEnvelopes(a);
        dequantize(a, nullptr);
        a.received = true;
    } else {
        Channel& b = *channel_[std::size_t(first) + 1];
        if (bits.read(1)) bits.read(8);
        const bool coupling = bits.read(1) != 0;
        if (coupling) {
            if (!readGrid(bits, a)) return;
            b.frameClass = a.frameClass;
            b.numEnv = a.numEnv;
            b.numNoise = a.numNoise;
            b.pointer = a.pointer;
            b.transient = a.transient;
            b.ampRes = a.ampRes;
            std::copy(a.tE, a.tE + 6, b.tE);
            std::copy(a.tQ, a.tQ + 3, b.tQ);
            std::copy(a.freqRes, a.freqRes + 5, b.freqRes);
            readDtdf(bits, a);
            readDtdf(bits, b);
            readInvf(bits, a);
            std::copy(a.invf, a.invf + 5, b.invf);
            readEnvelope(bits, a, true, false);
            readNoise(bits, a, true, false);
            readEnvelope(bits, b, true, true);
            readNoise(bits, b, true, true);
        } else {
            if (!readGrid(bits, a) || !readGrid(bits, b)) return;
            readDtdf(bits, a);
            readDtdf(bits, b);
            readInvf(bits, a);
            readInvf(bits, b);
            readEnvelope(bits, a, false, false);
            readEnvelope(bits, b, false, false);
            readNoise(bits, a, false, false);
            readNoise(bits, b, false, false);
        }
        for (Channel* ch : {&a, &b}) {
            ch->harmonicPresent = bits.read(1) != 0;
            std::fill(ch->harmonic, ch->harmonic + 64, false);
            if (ch->harmonicPresent) readSinusoids(bits, *ch);
        }
        decodeEnvelopes(a);
        decodeEnvelopes(b);
        if (coupling) dequantize(a, &b);
        else {
            dequantize(a, nullptr);
            dequantize(b, nullptr);
        }
        a.received = b.received = true;
    }
    // bs_extended_data (parametric stereo and others): skipped with the rest of the payload.
    const std::size_t used = bits.position() - start;
    if (used < available) bits.skip(available - used);
}

// HF generation (4.6.18.6): linear prediction per low band, chirp per noise band, patches.
void SbrDecoder::generateHighBand(Channel& ch) {
    // Chirp factors from the inverse filtering modes.
    float bw[5] = {};
    const int noiseBands = int(noise_.size()) - 1;
    for (int i = 0; i < noiseBands; ++i) {
        float target;
        switch (ch.invf[i]) {
        case 0: target = ch.invfPrevious[i] == 1 ? 0.6f : 0.0f; break;
        case 1: target = ch.invfPrevious[i] == 0 ? 0.6f : 0.75f; break;
        case 2: target = 0.9f; break;
        default: target = 0.98f; break;
        }
        float b = target < ch.bwPrevious[i] ? 0.75f * target + 0.25f * ch.bwPrevious[i]
                                            : 0.90625f * target + 0.09375f * ch.bwPrevious[i];
        if (b < 0.015625f) b = 0.0f;
        if (b >= 0.99609375f) b = 0.99609375f;
        bw[i] = b;
    }
    std::copy(bw, bw + 5, ch.bwPrevious);
    std::copy(ch.invf, ch.invf + 5, ch.invfPrevious);

    for (auto& row : ch.high) std::fill(row, row + 64, Complex{});
    const int from = kRate * ch.tE[0], to = kRate * ch.tE[ch.numEnv];
    int target = kx_;
    for (int p = 0; p < patchCount_; ++p) {
        for (int x = 0; x < patchSize_[std::size_t(p)]; ++x, ++target) {
            const int source = patchStart_[std::size_t(p)] + x;
            if (source < 0 || source >= 32 || target >= 64) continue;
            // Covariance over the 40 slots (8 earlier + 32) for the predictor.
            Complex phi[3][3] = {};
            for (int i = 0; i < 3; ++i)
                for (int j = i; j < 3; ++j) {
                    Complex sum{};
                    for (int n = 2; n < kHistory + kSlots; ++n) sum += ch.low[n - i][source] * std::conj(ch.low[n - j][source]);
                    phi[i][j] = sum;
                }
            const float d = phi[2][2].real() * phi[1][1].real() - std::norm(phi[1][2]) / (1.0f + 1e-6f);
            Complex alpha0{}, alpha1{};
            if (d != 0.0f) alpha1 = (phi[0][1] * phi[1][2] - phi[0][2] * phi[1][1]) / d;
            if (phi[1][1].real() != 0.0f) alpha0 = -(phi[0][1] + alpha1 * std::conj(phi[1][2])) / phi[1][1].real();
            if (std::abs(alpha0) >= 4.0f || std::abs(alpha1) >= 4.0f) alpha0 = alpha1 = {};
            int band = 0;
            while (band + 1 < noiseBands && noise_[std::size_t(band) + 1] <= target) ++band;
            const float b = bw[band];
            const Complex a0 = alpha0 * b, a1 = alpha1 * (b * b);
            // Slot l of the SBR frame is X_low/X_high index l + t_HFAdj (6 QMF slots behind
            // the analysis, as the core's delay requires).
            for (int l = from; l < to; ++l) {
                const int idx = l + kAdjust;
                if (idx < 2 || idx >= kHistory + kSlots) continue;
                ch.high[idx][target] = ch.low[idx][source] + a0 * ch.low[idx - 1][source] + a1 * ch.low[idx - 2][source];
            }
        }
    }
}

// HF adjustment (4.6.18.7): energies to the transmitted envelopes, noise, sinusoids, limiter.
void SbrDecoder::adjustEnvelope(Channel& ch) {
    const Qmf& q = qmf();
    const int nHigh = int(high_.size()) - 1;
    const int noiseBands = int(noise_.size()) - 1;
    static const float limGains[4] = {0.70795f, 1.0f, 1.41254f, 1e10f};
    const float limGain = limGains[header_.limiterGains];
    const int transientPrevious = ch.transientPrevious == ch.numEnvPrevious ? 0 : -1;
    static const float smooth[5] = {0.33333333f, 0.30150283f, 0.21816949f, 0.11516383f, 0.03183050f};
    const bool smoothing = header_.smoothingMode == 0;

    for (auto& row : ch.y) std::fill(row, row + 64, Complex{});
    for (int l = 0; l < ch.numEnv; ++l) {
        const int start = kRate * ch.tE[l], end = kRate * ch.tE[l + 1];
        const std::vector<int>& table = ch.freqRes[l] ? high_ : low_;
        const int bands = int(table.size()) - 1;
        int noiseEnv = 0;
        while (noiseEnv + 1 < ch.numNoise && ch.tQ[noiseEnv + 1] <= ch.tE[l]) ++noiseEnv;
        const bool noTransient = l != ch.transient && l != transientPrevious;
        float eOrig[64] = {}, qOrig[64] = {}, eCurr[64] = {}, sineAt[64] = {}, sineBand[64] = {};
        for (int m = 0; m < m_; ++m) {
            const int k = m + kx_;
            int b = 0;
            while (b + 1 < bands && table[std::size_t(b) + 1] <= k) ++b;
            eOrig[m] = ch.eOrig[l][b];
            int nb = 0;
            while (nb + 1 < noiseBands && noise_[std::size_t(nb) + 1] <= k) ++nb;
            qOrig[m] = ch.qOrig[noiseEnv][nb];
        }
        // Sinusoids: in the middle of a high-resolution band that has one; "present in the
        // band" for the gains is per envelope band.
        for (int hb = 0; hb < nHigh; ++hb) {
            if (!ch.harmonic[hb]) continue;
            if (!(l >= ch.transient || ch.harmonicPrevious[hb])) continue;
            const int mid = ((high_[std::size_t(hb)] + high_[std::size_t(hb) + 1]) >> 1) - kx_;
            if (mid >= 0 && mid < m_) sineAt[mid] = 1.0f;
        }
        for (int b = 0; b < bands; ++b) {
            float any = 0.0f;
            for (int k = table[std::size_t(b)]; k < table[std::size_t(b) + 1]; ++k) any = std::max(any, sineAt[k - kx_]);
            for (int k = table[std::size_t(b)]; k < table[std::size_t(b) + 1]; ++k) sineBand[k - kx_] = any;
        }
        // Current energy of the generated band, per QMF band or averaged per envelope band.
        for (int m = 0; m < m_; ++m) {
            float sum = 0.0f;
            for (int i = start; i < end; ++i) sum += std::norm(ch.high[i + kAdjust][m + kx_]);
            eCurr[m] = sum / float(end - start);
        }
        if (!header_.interpolFreq)
            for (int b = 0; b < bands; ++b) {
                float sum = 0.0f;
                const int lo = table[std::size_t(b)] - kx_, hi = table[std::size_t(b) + 1] - kx_;
                for (int m = lo; m < hi; ++m) sum += eCurr[m];
                for (int m = lo; m < hi; ++m) eCurr[m] = sum / float(hi - lo);
            }
        float gain[64], qM[64], sM[64];
        for (int m = 0; m < m_; ++m) {
            const float delta = noTransient ? 1.0f : 0.0f;
            if (sineBand[m] == 0.0f) gain[m] = std::sqrt(eOrig[m] / ((1.0f + eCurr[m]) * (1.0f + qOrig[m] * delta)));
            else gain[m] = std::sqrt(eOrig[m] * qOrig[m] / ((1.0f + eCurr[m]) * (1.0f + qOrig[m])));
            qM[m] = std::sqrt(eOrig[m] * qOrig[m] / (1.0f + qOrig[m]));
            sM[m] = std::sqrt(eOrig[m] * sineAt[m] / (1.0f + qOrig[m]));
        }
        // Limiter and boost per limiter band.
        for (std::size_t lb = 0; lb + 1 < limiter_.size(); ++lb) {
            const int lo = limiter_[lb] - kx_, hi = limiter_[lb + 1] - kx_;
            if (lo < 0 || hi > m_ || lo >= hi) continue;
            float sumOrig = 0.0f, sumCurr = 0.0f;
            for (int m = lo; m < hi; ++m) {
                sumOrig += eOrig[m];
                sumCurr += eCurr[m];
            }
            float gainMax = limGain * std::sqrt((1e-12f + sumOrig) / (1e-12f + sumCurr));
            gainMax = std::min(gainMax, 1e5f);
            for (int m = lo; m < hi; ++m) {
                const float qMax = gain[m] > 0.0f ? qM[m] * gainMax / gain[m] : qM[m];
                qM[m] = std::min(qM[m], qMax);
                gain[m] = std::min(gain[m], gainMax);
            }
            float sumAdjusted = 0.0f;
            for (int m = lo; m < hi; ++m) {
                sumAdjusted += eCurr[m] * gain[m] * gain[m];
                if (sM[m] != 0.0f) sumAdjusted += sM[m] * sM[m];
                else if (noTransient) sumAdjusted += qM[m] * qM[m];
            }
            const float boost = std::min(std::sqrt((1e-12f + sumOrig) / (1e-12f + sumAdjusted)), 1.584893192f);
            for (int m = lo; m < hi; ++m) {
                gain[m] *= boost;
                qM[m] *= boost;
                sM[m] *= boost;
            }
        }
        // Assembly, slot by slot (4.6.18.7.5).
        for (int i = start; i < end; ++i) {
            for (int m = 0; m < m_; ++m) {
                float g = gain[m], qf = qM[m];
                if (smoothing && noTransient && ch.historyValid) {
                    g = smooth[0] * gain[m];
                    qf = smooth[0] * qM[m];
                    for (int j = 0; j < 4; ++j) {
                        g += smooth[j + 1] * ch.gainHistory[j][m];
                        qf += smooth[j + 1] * ch.noiseHistory[j][m];
                    }
                }
                Complex v = ch.high[i + kAdjust][m + kx_] * g;
                const int k = m + kx_;
                if (sM[m] != 0.0f) {
                    static const float phiRe[4] = {1.0f, 0.0f, -1.0f, 0.0f}, phiIm[4] = {0.0f, 1.0f, 0.0f, -1.0f};
                    const float sign = (k & 1) ? -1.0f : 1.0f;
                    v += Complex(sM[m] * phiRe[ch.indexSine], sM[m] * sign * phiIm[ch.indexSine]);
                } else if (noTransient) {
                    v += qf * q.noise[(ch.indexNoise + m + 1) & 511];
                }
                ch.y[i][k] = v;
            }
            // Gain history for smoothing (the newest first).
            for (int j = 3; j > 0; --j) {
                std::copy(ch.gainHistory[j - 1], ch.gainHistory[j - 1] + 64, ch.gainHistory[j]);
                std::copy(ch.noiseHistory[j - 1], ch.noiseHistory[j - 1] + 64, ch.noiseHistory[j]);
            }
            std::copy(gain, gain + 64, ch.gainHistory[0]);
            std::copy(qM, qM + 64, ch.noiseHistory[0]);
            ch.historyValid = true;
            ch.indexNoise = (ch.indexNoise + m_) & 511;
            ch.indexSine = (ch.indexSine + 1) & 3;
        }
    }
    ch.transientPrevious = ch.transient;
    ch.numEnvPrevious = ch.numEnv;
    std::copy(ch.harmonic, ch.harmonic + 64, ch.harmonicPrevious);
}

void SbrDecoder::process(int c, const float* core, float* out) {
    Channel& ch = *channel_[std::size_t(c)];
    const Qmf& q = qmf();
    // QMF analysis (4.6.18.4.1): 32 slots of 32 bands, after the 8 kept from before.
    for (int n = 0; n < kHistory; ++n) std::copy(ch.low[kSlots + n], ch.low[kSlots + n] + 32, ch.low[n]);
    for (int l = 0; l < kSlots; ++l) {
        std::memmove(ch.analysis + 32, ch.analysis, sizeof(float) * 288);
        for (int n = 0; n < 32; ++n) ch.analysis[n] = core[l * 32 + 31 - n];
        float u[64];
        for (int n = 0; n < 64; ++n) {
            float sum = 0.0f;
            for (int j = 0; j < 5; ++j) sum += ch.analysis[n + 64 * j] * q.window[2 * (n + 64 * j)];
            u[n] = sum;
        }
        for (int k = 0; k < 32; ++k) {
            float re = 0.0f, im = 0.0f;
            for (int n = 0; n < 64; ++n) {
                re += u[n] * q.analysisCos[k][n];
                im += u[n] * q.analysisSin[k][n];
            }
            ch.low[kHistory + l][k] = {re, im};
        }
    }
    // The high band: generated and adjusted when this frame brought SBR data.
    const bool active = header_.valid && tablesValid_ && ch.received;
    if (active) {
        generateHighBand(ch);
        adjustEnvelope(ch);
    }
    // The synthesis input X (sbr_x_gen): the low band (delayed by t_HFAdj... in X_low's
    // indexing, slot l is low[l + kAdjust]), the high band from Y; slots before this frame's
    // first border come from the previous frame's Y beyond 32.
    const int carried = active ? std::max(kRate * ch.lastBorderPrevious - kSlots, 0) : 0;
    float sample[64];
    for (int l = 0; l < kSlots; ++l) {
        Complex x[64] = {};
        const int lowBands = active ? kx_ : 32;
        for (int k = 0; k < lowBands && k < 32; ++k) x[k] = ch.low[l + kAdjust][k];
        if (active) {
            if (l < carried && ch.yPreviousValid) {
                for (int k = kxPrevious_; k < kxPrevious_ + mPrevious_ && k < 64; ++k) x[k] = ch.yPrevious[l + kSlots][k];
            } else {
                for (int k = kx_; k < kx_ + m_; ++k) x[k] = ch.y[l][k];
            }
        }
        // QMF synthesis (4.6.18.4.2): 64 bands -> 64 samples.
        std::memmove(ch.synthesis + 128, ch.synthesis, sizeof(float) * 1152);
        for (int n = 0; n < 128; ++n) {
            float v = 0.0f;
            for (int k = 0; k < 64; ++k) v += x[k].real() * q.synthesisCos[n][k] - x[k].imag() * q.synthesisSin[n][k];
            ch.synthesis[n] = v;
        }
        for (int k = 0; k < 64; ++k) {
            float s = 0.0f;
            for (int j = 0; j < 5; ++j) {
                s += ch.synthesis[256 * j + k] * q.window[128 * j + k];
                s += ch.synthesis[256 * j + 192 + k] * q.window[128 * j + 64 + k];
            }
            sample[k] = s;
        }
        std::copy(sample, sample + 64, out + l * 64);
    }
    if (active) {
        std::copy(&ch.y[0][0], &ch.y[0][0] + kMaxSlots * 64, &ch.yPrevious[0][0]);
        ch.yPreviousValid = true;
        ch.lastBorderPrevious = ch.tE[ch.numEnv];
        if (c == channelCount_ - 1) {
            kxPrevious_ = kx_;
            mPrevious_ = m_;
        }
    }
}

}  // namespace doc::media
