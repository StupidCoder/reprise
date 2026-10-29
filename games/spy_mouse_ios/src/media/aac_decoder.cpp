#include "media/aac_decoder.hpp"

#include "media/aac_tables.hpp"
#include "media/bit_reader.hpp"
#include "media/sbr_decoder.hpp"
#include "media/transforms.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <cstdlib>
#include <cstring>

namespace doc::media {
namespace {

constexpr int kSampleRates[13] = {96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350};

// Window sequences and element ids (14496-3, 4.5.2.1).
enum WindowSequence { OnlyLong = 0, LongStart = 1, EightShort = 2, LongStop = 3 };
enum ElementId { Sce = 0, Cpe = 1, Cce = 2, Lfe = 3, Dse = 4, Pce = 5, Fil = 6, End = 7 };
constexpr int kZeroBook = 0, kNoiseBook = 13, kIntensityBook2 = 14, kIntensityBook = 15;
constexpr int kTnsMaxBandsLong[13] = {31, 31, 34, 40, 42, 51, 46, 46, 42, 42, 42, 39, 39};
constexpr int kTnsMaxBandsShort[13] = {9, 9, 10, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14};

// Band offsets (with the leading 0) for long and short windows of a rate.
struct BandTable {
    std::vector<int> longOffsets, shortOffsets;
};

BandTable bandTable(int rateIndex) {
    auto with0 = [](const std::int16_t* t, int n) {
        std::vector<int> v{0};
        v.insert(v.end(), t, t + n);
        return v;
    };
    switch (rateIndex) {
    case 0: case 1: return {with0(k_sfb_96_1024, 41), with0(k_sfb_64_128, 12)};
    case 2: return {with0(k_sfb_64_1024, 47), with0(k_sfb_64_128, 12)};
    case 3: case 4: return {with0(k_sfb_48_1024, 49), with0(k_sfb_48_128, 14)};
    case 5: return {with0(k_sfb_32_1024, 51), with0(k_sfb_48_128, 14)};
    case 6: case 7: return {with0(k_sfb_24_1024, 47), with0(k_sfb_24_128, 15)};
    case 8: case 9: case 10: return {with0(k_sfb_16_1024, 43), with0(k_sfb_16_128, 15)};
    default: return {with0(k_sfb_8_1024, 40), with0(k_sfb_8_128, 15)};
    }
}

// A Huffman codebook as a binary tree (nodes: two children; a leaf holds -1 - symbol).
class HuffmanTree {
public:
    void build(const HuffmanCode* codes, int count) {
        nodes_.assign(1, {0, 0});
        for (int i = 0; i < count; ++i) {
            int node = 0;
            for (int b = codes[i].length - 1; b >= 0; --b) {
                const int bit = (codes[i].code >> b) & 1;
                if (b == 0) {
                    nodes_[std::size_t(node)][std::size_t(bit)] = -1 - int(codes[i].index);
                } else {
                    int next = nodes_[std::size_t(node)][std::size_t(bit)];
                    if (next <= 0) {
                        next = int(nodes_.size());
                        nodes_[std::size_t(node)][std::size_t(bit)] = next;
                        nodes_.push_back({0, 0});
                    }
                    node = next;
                }
            }
        }
    }
    int decode(BitReader& bits) const {
        int node = 0;
        for (int depth = 0; depth < 32; ++depth) {
            const int next = nodes_[std::size_t(node)][bits.bit()];
            if (next < 0) return -1 - next;
            if (next == 0) return -1;  // (no such code)
            node = next;
        }
        return -1;
    }

private:
    std::vector<std::array<int, 2>> nodes_;
};

struct Books {
    HuffmanTree scalefactor;
    HuffmanTree spectral[12];
    float pow43[8192];
    Books() {
        scalefactor.build(kScalefactorCodes, 121);
        for (int b = 1; b <= 11; ++b) spectral[b].build(kSpectralBooks[b].codes, kSpectralBooks[b].count);
        for (int i = 0; i < 8192; ++i) pow43[i] = float(std::pow(double(i), 4.0 / 3.0));
    }
};

const Books& books() {
    static const Books b;
    return b;
}

// Window halves (rising), length n: sine and Kaiser-Bessel-derived (alpha 4 long, 6 short).
std::vector<float> sineWindow(int n) {
    std::vector<float> w(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) w[std::size_t(i)] = float(std::sin(std::numbers::pi / (2.0 * n) * (i + 0.5)));
    return w;
}
double besselI0(double x) {
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 50; ++k) {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
    }
    return sum;
}
std::vector<float> kbdWindow(int n, double alpha) {
    std::vector<double> kaiser(static_cast<std::size_t>(n) + 1);
    double total = 0.0;
    for (int i = 0; i <= n; ++i) {
        const double r = 2.0 * i / n - 1.0;
        kaiser[std::size_t(i)] = besselI0(std::numbers::pi * alpha * std::sqrt(std::max(0.0, 1.0 - r * r)));
        total += kaiser[std::size_t(i)];
    }
    std::vector<float> w(static_cast<std::size_t>(n));
    double acc = 0.0;
    for (int i = 0; i < n; ++i) {
        acc += kaiser[std::size_t(i)];
        w[std::size_t(i)] = float(std::sqrt(acc / total));
    }
    return w;
}

struct Windows {
    std::vector<float> longHalf[2], shortHalf[2];  // [shape]: 0 sine, 1 KBD
    Windows() {
        longHalf[0] = sineWindow(1024);
        longHalf[1] = kbdWindow(1024, 4.0);
        shortHalf[0] = sineWindow(128);
        shortHalf[1] = kbdWindow(128, 6.0);
    }
};
const Windows& windows() {
    static const Windows w;
    return w;
}

}  // namespace

struct AacDecoder::Channel {
    // This frame's stream data.
    int windowSequence = OnlyLong, windowShape = 0, maxSfb = 0;
    int numWindowGroups = 1, numWindows = 1;
    int groupLength[8] = {1};
    int globalGain = 0;
    std::array<std::array<int, 64>, 8> book{};        // per group, band
    std::array<std::array<int, 64>, 8> scalefactor{}; // per group, band (sf, intensity position, noise energy)
    float spectrum[1024] = {};  // dequantized; short windows: 8 x 128
    // TNS filters per window.
    struct TnsFilter {
        int length = 0, order = 0, direction = 0;
        float lpc[21] = {};
    };
    int tnsFilters[8] = {};
    TnsFilter tns[8][4];
    bool tnsPresent = false;
    // Kept between frames.
    float overlap[1024] = {};
    int previousShape = 0;
};

AacDecoder::AacDecoder() = default;
AacDecoder::~AacDecoder() = default;

bool AacDecoder::configure(std::span<const std::uint8_t> config, std::string& error) {
    BitReader bits(config);
    auto objectType = [&]() {
        int t = int(bits.read(5));
        if (t == 31) t = 32 + int(bits.read(6));
        return t;
    };
    auto rate = [&](int& index) {
        index = int(bits.read(4));
        if (index == 15) {
            const int r = int(bits.read(24));
            index = 0;
            while (index < 12 && kSampleRates[index] != r) ++index;
            return r;
        }
        return index < 13 ? kSampleRates[index] : 0;
    };
    int type = objectType();
    coreRate_ = rate(rateIndex_);
    channels_ = int(bits.read(4));
    bool explicitSbr = false;
    if (type == 5 || type == 29) {  // SBR (or parametric stereo) on an AAC core
        explicitSbr = true;
        int extIndex = 0;
        rate(extIndex);
        type = objectType();
    }
    if (type != 2) {
        error = "only AAC-LC cores are supported (object type " + std::to_string(type) + ")";
        return false;
    }
    if (bits.read(1)) {  // frameLengthFlag: 960-sample frames
        error = "960-sample AAC frames are not supported";
        return false;
    }
    if (bits.read(1)) bits.read(14);  // dependsOnCoreCoder
    bits.read(1);                     // extensionFlag
    if (channels_ < 1 || channels_ > 2 || coreRate_ == 0) {
        error = "only mono and stereo AAC at a standard rate are supported";
        return false;
    }
    // Implicit signalling: a low core rate is taken as HE-AAC; frames without SBR data are then
    // only upsampled (the SBR filterbank with nothing above the core).
    if ((explicitSbr || coreRate_ <= 24000)) sbr_ = std::make_unique<SbrDecoder>(coreRate_ * 2, channels_);
    channel_.clear();
    for (int c = 0; c < channels_; ++c) channel_.push_back(std::make_unique<Channel>());
    for (auto& t : time_) t.assign(1024, 0.0f);
    return true;
}

// ics_info (4.4.2.7, Table 4.6).
static bool readIcsInfo(BitReader& bits, AacDecoder::Channel& ch) {
    bits.read(1);  // ics_reserved_bit
    ch.windowSequence = int(bits.read(2));
    ch.windowShape = int(bits.read(1));
    if (ch.windowSequence == EightShort) {
        ch.maxSfb = int(bits.read(4));
        const int grouping = int(bits.read(7));
        ch.numWindows = 8;
        ch.numWindowGroups = 1;
        ch.groupLength[0] = 1;
        for (int i = 0; i < 7; ++i) {
            if (grouping & (1 << (6 - i))) ++ch.groupLength[ch.numWindowGroups - 1];
            else ch.groupLength[ch.numWindowGroups++] = 1;
        }
    } else {
        ch.maxSfb = int(bits.read(6));
        ch.numWindows = 1;
        ch.numWindowGroups = 1;
        ch.groupLength[0] = 1;
        if (bits.read(1)) return false;  // predictor data (not in LC)
    }
    return true;
}

// individual_channel_stream (4.4.2.7): everything up to the dequantized spectrum and TNS data.
static bool readChannelStream(BitReader& bits, AacDecoder::Channel& ch, bool commonWindow, const BandTable& bands,
                              int rateIndex, std::uint32_t& noiseState) {
    ch.globalGain = int(bits.read(8));
    if (!commonWindow && !readIcsInfo(bits, ch)) return false;
    const bool shortWindows = ch.windowSequence == EightShort;
    const std::vector<int>& offsets = shortWindows ? bands.shortOffsets : bands.longOffsets;
    const int numSwb = int(offsets.size()) - 1;
    if (ch.maxSfb > numSwb) return false;
    const Books& tables = books();

    // section_data
    const int sectionBits = shortWindows ? 3 : 5;
    const int escape = (1 << sectionBits) - 1;
    for (int g = 0; g < ch.numWindowGroups; ++g) {
        int k = 0;
        while (k < ch.maxSfb) {
            const int book = int(bits.read(4));
            if (book == 12) return false;
            int length = 0, increment;
            do {
                increment = int(bits.read(sectionBits));
                length += increment;
            } while (increment == escape && !bits.overrun());
            if (k + length > ch.maxSfb) return false;
            for (int i = 0; i < length; ++i) ch.book[std::size_t(g)][std::size_t(k + i)] = book;
            k += length;
            if (bits.overrun()) return false;
        }
        for (int i = ch.maxSfb; i < 64; ++i) ch.book[std::size_t(g)][std::size_t(i)] = kZeroBook;
    }

    // scale_factor_data
    int scale = ch.globalGain, position = 0, noise = ch.globalGain - 90;
    bool firstNoise = true;
    for (int g = 0; g < ch.numWindowGroups; ++g)
        for (int sfb = 0; sfb < ch.maxSfb; ++sfb) {
            const int book = ch.book[std::size_t(g)][std::size_t(sfb)];
            int& value = ch.scalefactor[std::size_t(g)][std::size_t(sfb)];
            if (book == kZeroBook) {
                value = 0;
            } else if (book == kIntensityBook || book == kIntensityBook2) {
                position += tables.scalefactor.decode(bits) - 60;
                value = position;
            } else if (book == kNoiseBook) {
                if (firstNoise) {
                    noise += int(bits.read(9)) - 256;
                    firstNoise = false;
                } else {
                    noise += tables.scalefactor.decode(bits) - 60;
                }
                value = noise;
            } else {
                scale += tables.scalefactor.decode(bits) - 60;
                value = scale;
            }
        }

    // pulse_data (long windows only)
    struct Pulse {
        int offset, amplitude;
    };
    std::vector<Pulse> pulses;
    if (bits.read(1)) {
        if (shortWindows) return false;
        const int count = int(bits.read(2)) + 1;
        int at = offsets[std::size_t(std::min(int(bits.read(6)), numSwb))];
        for (int i = 0; i < count; ++i) {
            at += int(bits.read(5));
            pulses.push_back({at, int(bits.read(4))});
        }
    }

    // tns_data
    ch.tnsPresent = bits.read(1) != 0;
    if (ch.tnsPresent) {
        for (int w = 0; w < ch.numWindows; ++w) {
            ch.tnsFilters[w] = int(bits.read(shortWindows ? 1 : 2));
            if (!ch.tnsFilters[w]) continue;
            const int resolution = int(bits.read(1));
            for (int f = 0; f < ch.tnsFilters[w]; ++f) {
                AacDecoder::Channel::TnsFilter& filter = ch.tns[w][f];
                filter.length = int(bits.read(shortWindows ? 4 : 6));
                filter.order = int(bits.read(shortWindows ? 3 : 5));
                if (!filter.order) continue;
                filter.direction = int(bits.read(1));
                const int compress = int(bits.read(1));
                const int coefBits = resolution + 3 - compress;
                // tns_decode_coef: the transmitted values as reflection coefficients, then the
                // step-up to a direct-form filter.
                const double iqfac = ((1 << (resolution + 2)) - 0.5) / (std::numbers::pi / 2.0);
                const double iqfacM = ((1 << (resolution + 2)) + 0.5) / (std::numbers::pi / 2.0);
                double parcor[20], a[21] = {1.0}, b[21];
                for (int i = 0; i < filter.order; ++i) {
                    int q = int(bits.read(coefBits));
                    if (q & (1 << (coefBits - 1))) q -= 1 << coefBits;  // two's complement
                    parcor[i] = std::sin(q / (q >= 0 ? iqfac : iqfacM));
                }
                for (int m = 1; m <= filter.order; ++m) {
                    for (int i = 1; i < m; ++i) b[i] = a[i] + parcor[m - 1] * a[m - i];
                    for (int i = 1; i < m; ++i) a[i] = b[i];
                    a[m] = parcor[m - 1];
                }
                for (int i = 0; i <= filter.order; ++i) filter.lpc[i] = float(a[i]);
            }
        }
    }
    if (bits.read(1)) return false;  // gain control (SSR only)

    // spectral_data, dequantized into window order.
    std::memset(ch.spectrum, 0, sizeof ch.spectrum);
    int quantized[1024] = {};
    int windowBase = 0;
    for (int g = 0; g < ch.numWindowGroups; ++g) {
        for (int sfb = 0; sfb < ch.maxSfb; ++sfb) {
            const int book = ch.book[std::size_t(g)][std::size_t(sfb)];
            if (book == kZeroBook || book >= kNoiseBook) continue;
            const HuffmanTree& tree = tables.spectral[book];
            const bool quads = book < 5, isSigned = book == 1 || book == 2 || book == 5 || book == 6;
            const int modulo = book <= 2 ? 3 : book <= 4 ? 3 : book <= 6 ? 9 : book <= 8 ? 8 : book <= 10 ? 13 : 17;
            const int offset = isSigned ? (book <= 2 ? 1 : 4) : 0;
            for (int w = 0; w < ch.groupLength[g]; ++w) {
                const int base = (windowBase + w) * 128 * (shortWindows ? 1 : 0);
                for (int k = offsets[std::size_t(sfb)]; k < offsets[std::size_t(sfb) + 1]; k += quads ? 4 : 2) {
                    const int index = tree.decode(bits);
                    if (index < 0) return false;
                    int values[4];
                    if (quads) {
                        values[0] = index / 27 - offset;
                        values[1] = (index / 9) % 3 - offset;
                        values[2] = (index / 3) % 3 - offset;
                        values[3] = index % 3 - offset;
                    } else {
                        values[0] = index / modulo - offset;
                        values[1] = index % modulo - offset;
                    }
                    const int count = quads ? 4 : 2;
                    if (!isSigned)
                        for (int i = 0; i < count; ++i)
                            if (values[i] && bits.read(1)) values[i] = -values[i];
                    if (book == 11)
                        for (int i = 0; i < count; ++i)
                            if (std::abs(values[i]) == 16) {
                                // escape: N ones, a zero, then N + 4 bits.
                                int n = 0;
                                while (bits.read(1) && n < 9) ++n;
                                const int magnitude = (1 << (n + 4)) + int(bits.read(n + 4));
                                values[i] = values[i] < 0 ? -magnitude : magnitude;
                            }
                    for (int i = 0; i < count; ++i) quantized[base + k + i] = values[i];
                }
                if (bits.overrun()) return false;
            }
        }
        windowBase += ch.groupLength[g];
    }
    for (const Pulse& p : pulses) {
        if (p.offset >= 1024) return false;
        int& q = quantized[p.offset];
        q += q >= 0 ? p.amplitude : -p.amplitude;
    }

    // Dequantization: sign(q) |q|^(4/3) 2^(0.25 (sf - 100)); noise bands get scaled noise.
    windowBase = 0;
    for (int g = 0; g < ch.numWindowGroups; ++g) {
        for (int w = 0; w < ch.groupLength[g]; ++w) {
            const int base = shortWindows ? (windowBase + w) * 128 : 0;
            for (int sfb = 0; sfb < ch.maxSfb; ++sfb) {
                const int book = ch.book[std::size_t(g)][std::size_t(sfb)];
                const int start = base + offsets[std::size_t(sfb)], end = base + offsets[std::size_t(sfb) + 1];
                const int sf = ch.scalefactor[std::size_t(g)][std::size_t(sfb)];
                if (book == kNoiseBook) {
                    float energy = 0.0f;
                    for (int k = start; k < end; ++k) {
                        noiseState = noiseState * 1664525u + 1013904223u;
                        const float r = float(std::int32_t(noiseState)) / 2147483648.0f;
                        ch.spectrum[k] = r;
                        energy += r * r;
                    }
                    const float gain = float(std::pow(2.0, 0.25 * sf)) / std::sqrt(std::max(energy, 1e-12f));
                    for (int k = start; k < end; ++k) ch.spectrum[k] *= gain;
                    continue;
                }
                if (book == kZeroBook || book >= kIntensityBook2) continue;
                const float gain = float(std::pow(2.0, 0.25 * (sf - 100)));
                for (int k = start; k < end; ++k) {
                    const int q = quantized[k];
                    const int a = std::min(std::abs(q), 8191);
                    ch.spectrum[k] = (q < 0 ? -tables.pow43[a] : tables.pow43[a]) * gain;
                }
            }
        }
        windowBase += ch.groupLength[g];
    }
    (void)rateIndex;
    return !bits.overrun();
}

// Temporal noise shaping (4.6.9): all-pole filtering of the spectrum over each filter's bands.
static void applyTns(AacDecoder::Channel& ch, const BandTable& bands, int rateIndex) {
    if (!ch.tnsPresent) return;
    const bool shortWindows = ch.windowSequence == EightShort;
    const std::vector<int>& offsets = shortWindows ? bands.shortOffsets : bands.longOffsets;
    const int numSwb = int(offsets.size()) - 1;
    const int maxBands = std::min((shortWindows ? kTnsMaxBandsShort : kTnsMaxBandsLong)[rateIndex], ch.maxSfb);
    for (int w = 0; w < ch.numWindows; ++w) {
        float* spec = ch.spectrum + (shortWindows ? w * 128 : 0);
        int bottom = numSwb;
        for (int f = 0; f < ch.tnsFilters[w]; ++f) {
            const AacDecoder::Channel::TnsFilter& filter = ch.tns[w][f];
            const int top = bottom;
            bottom = std::max(top - filter.length, 0);
            if (!filter.order) continue;
            const int start = offsets[std::size_t(std::min(bottom, maxBands))];
            const int end = offsets[std::size_t(std::min(top, maxBands))];
            const int size = end - start;
            if (size <= 0) continue;
            const int increment = filter.direction ? -1 : 1;
            int at = filter.direction ? end - 1 : start;
            float state[21] = {};
            for (int n = 0; n < size; ++n, at += increment) {
                float y = spec[at];
                for (int i = 0; i < filter.order; ++i) y -= state[i] * filter.lpc[i + 1];
                for (int i = filter.order - 1; i > 0; --i) state[i] = state[i - 1];
                state[0] = y;
                spec[at] = y;
            }
        }
    }
}

bool AacDecoder::singleChannel(BitReader& bits, int c) {
    bits.read(4);  // element_instance_tag
    if (c >= channels_) return false;
    static thread_local BandTable table;
    table = bandTable(rateIndex_);
    Channel& ch = *channel_[std::size_t(c)];
    if (!readChannelStream(bits, ch, false, table, rateIndex_, noiseState_)) return false;
    applyTns(ch, table, rateIndex_);
    synthesize(c);
    return true;
}

bool AacDecoder::channelPair(BitReader& bits, int first) {
    bits.read(4);  // element_instance_tag
    if (first + 1 >= channels_) return false;
    const BandTable table = bandTable(rateIndex_);
    Channel& left = *channel_[std::size_t(first)];
    Channel& right = *channel_[std::size_t(first) + 1];
    const bool common = bits.read(1) != 0;
    int msPresent = 0;
    bool msUsed[8][64] = {};
    if (common) {
        if (!readIcsInfo(bits, left)) return false;
        right.windowSequence = left.windowSequence;
        right.windowShape = left.windowShape;
        right.maxSfb = left.maxSfb;
        right.numWindows = left.numWindows;
        right.numWindowGroups = left.numWindowGroups;
        std::copy(left.groupLength, left.groupLength + 8, right.groupLength);
        msPresent = int(bits.read(2));
        if (msPresent == 1)
            for (int g = 0; g < left.numWindowGroups; ++g)
                for (int sfb = 0; sfb < left.maxSfb; ++sfb) msUsed[g][sfb] = bits.read(1) != 0;
        else if (msPresent == 2)
            for (int g = 0; g < left.numWindowGroups; ++g)
                for (int sfb = 0; sfb < left.maxSfb; ++sfb) msUsed[g][sfb] = true;
    }
    if (!readChannelStream(bits, left, common, table, rateIndex_, noiseState_)) return false;
    if (!readChannelStream(bits, right, common, table, rateIndex_, noiseState_)) return false;

    // Stereo tools (4.6.8.1 M/S, 4.6.8.2 intensity), band by band.
    if (common) {
        const bool shortWindows = left.windowSequence == EightShort;
        const std::vector<int>& offsets = shortWindows ? table.shortOffsets : table.longOffsets;
        int windowBase = 0;
        for (int g = 0; g < left.numWindowGroups; ++g) {
            for (int w = 0; w < left.groupLength[g]; ++w) {
                const int base = shortWindows ? (windowBase + w) * 128 : 0;
                for (int sfb = 0; sfb < left.maxSfb; ++sfb) {
                    const int start = base + offsets[std::size_t(sfb)], end = base + offsets[std::size_t(sfb) + 1];
                    const int rightBook = right.book[std::size_t(g)][std::size_t(sfb)];
                    if (rightBook == kIntensityBook || rightBook == kIntensityBook2) {
                        float scale = float(std::pow(0.5, 0.25 * right.scalefactor[std::size_t(g)][std::size_t(sfb)]));
                        if (rightBook == kIntensityBook2) scale = -scale;
                        if (msPresent == 1 && msUsed[g][sfb]) scale = -scale;
                        for (int k = start; k < end; ++k) right.spectrum[k] = left.spectrum[k] * scale;
                    } else if (msUsed[g][sfb] && left.book[std::size_t(g)][std::size_t(sfb)] != kNoiseBook &&
                               rightBook != kNoiseBook) {
                        for (int k = start; k < end; ++k) {
                            const float l = left.spectrum[k], r = right.spectrum[k];
                            left.spectrum[k] = l + r;
                            right.spectrum[k] = l - r;
                        }
                    }
                }
            }
            windowBase += left.groupLength[g];
        }
    }
    applyTns(left, table, rateIndex_);
    applyTns(right, table, rateIndex_);
    synthesize(first);
    synthesize(first + 1);
    return true;
}

// The filterbank (4.6.11): inverse MDCT, windowing and overlap-add, into time_[channel].
void AacDecoder::synthesize(int c) {
    Channel& ch = *channel_[std::size_t(c)];
    const Windows& win = windows();
    const std::vector<float>& longPrev = win.longHalf[ch.previousShape];
    const std::vector<float>& longCur = win.longHalf[ch.windowShape];
    const std::vector<float>& shortPrev = win.shortHalf[ch.previousShape];
    const std::vector<float>& shortCur = win.shortHalf[ch.windowShape];
    float out[2048];
    if (ch.windowSequence == EightShort) {
        std::fill(out, out + 2048, 0.0f);
        float block[256];
        for (int w = 0; w < 8; ++w) {
            imdct(ch.spectrum + w * 128, block, 256);
            const std::vector<float>& rise = w == 0 ? shortPrev : shortCur;
            for (int i = 0; i < 128; ++i) {
                out[448 + w * 128 + i] += block[i] * rise[std::size_t(i)];
                out[448 + w * 128 + 128 + i] += block[128 + i] * shortCur[std::size_t(127 - i)];
            }
        }
    } else {
        imdct(ch.spectrum, out, 2048);
        // Rising half.
        if (ch.windowSequence == LongStop) {
            for (int i = 0; i < 448; ++i) out[i] = 0.0f;
            for (int i = 0; i < 128; ++i) out[448 + i] *= shortPrev[std::size_t(i)];
        } else {
            for (int i = 0; i < 1024; ++i) out[i] *= longPrev[std::size_t(i)];
        }
        // Falling half.
        if (ch.windowSequence == LongStart) {
            for (int i = 0; i < 128; ++i) out[1024 + 448 + i] *= shortCur[std::size_t(127 - i)];
            for (int i = 1024 + 576; i < 2048; ++i) out[i] = 0.0f;
        } else {
            for (int i = 0; i < 1024; ++i) out[1024 + i] *= longCur[std::size_t(1023 - i)];
        }
    }
    std::vector<float>& t = time_[std::size_t(c)];
    for (int i = 0; i < 1024; ++i) {
        t[std::size_t(i)] = out[i] + ch.overlap[i];
        ch.overlap[i] = out[1024 + i];
    }
    ch.previousShape = ch.windowShape;
}

void AacDecoder::fill(BitReader& bits, int lastElementChannels, int lastChannel) {
    int count = int(bits.read(4));
    if (count == 15) count += int(bits.read(8)) - 1;
    if (count <= 0) return;
    const std::size_t start = bits.position(), length = std::size_t(count) * 8;
    const int type = int(bits.read(4));
    if ((type == 13 || type == 14) && sbr_ && lastElementChannels > 0) {
        sbr_->parse(bits, length - 4, type == 14, lastChannel, lastElementChannels);
        sbrSeen_ = true;
    }
    // (Other extensions: fill bytes, dynamic range: skipped.)
    const std::size_t used = bits.position() - start;
    if (used < length) bits.skip(length - used);
}

bool AacDecoder::decodeElements(BitReader& bits) {
    int next = 0, lastChannels = 0, lastChannel = 0;
    for (int guard = 0; guard < 64; ++guard) {
        const int id = int(bits.read(3));
        if (bits.overrun()) return false;
        switch (id) {
        case Sce:
        case Lfe:
            if (id == Lfe) {  // (not mixed into the game's stereo)
                Channel scratch;
                BandTable table = bandTable(rateIndex_);
                bits.read(4);
                if (!readChannelStream(bits, scratch, false, table, rateIndex_, noiseState_)) return false;
                lastChannels = 0;
                break;
            }
            if (!singleChannel(bits, next)) return false;
            lastChannel = next;
            lastChannels = 1;
            ++next;
            break;
        case Cpe:
            if (!channelPair(bits, next)) return false;
            lastChannel = next;
            lastChannels = 2;
            next += 2;
            break;
        case Dse: {
            bits.read(4);
            const bool align = bits.read(1) != 0;
            int count = int(bits.read(8));
            if (count == 255) count += int(bits.read(8));
            if (align) bits.alignToByte();
            bits.skip(std::size_t(count) * 8);
            break;
        }
        case Fil: fill(bits, lastChannels, lastChannel); break;
        case End: return next > 0;
        default: return false;  // coupling channel or program config: not supported
        }
    }
    return false;
}

bool AacDecoder::decodeFrame(std::span<const std::uint8_t> frame, std::vector<float>& stereo) {
    stereo.assign(std::size_t(frameLength()) * 2, 0.0f);
    if (channel_.empty()) return false;
    BitReader bits(frame);
    if (sbr_) sbr_->beginFrame();
    if (!decodeElements(bits)) return false;
    output(stereo);
    return true;
}

bool AacDecoder::flush(std::vector<float>& stereo) {
    stereo.assign(std::size_t(frameLength()) * 2, 0.0f);
    if (channel_.empty()) return false;
    // A silent frame: the overlap's second half comes out; SBR shapes it with the last frame's
    // envelopes (its data is kept: no beginFrame).
    for (int c = 0; c < channels_; ++c) {
        Channel& ch = *channel_[std::size_t(c)];
        std::copy(ch.overlap, ch.overlap + 1024, time_[std::size_t(c)].begin());
        std::fill(ch.overlap, ch.overlap + 1024, 0.0f);
    }
    output(stereo);
    return true;
}

void AacDecoder::output(std::vector<float>& stereo) {
    const int length = frameLength();
    float sbrOut[2048];
    for (int c = 0; c < std::min(channels_, 2); ++c) {
        const float* samples = time_[std::size_t(c)].data();
        if (sbr_) {
            sbr_->process(c, samples, sbrOut);
            samples = sbrOut;
        }
        for (int i = 0; i < length; ++i) stereo[std::size_t(i) * 2 + std::size_t(c)] = samples[i] / 32768.0f;
    }
    if (channels_ == 1)
        for (int i = 0; i < length; ++i) stereo[std::size_t(i) * 2 + 1] = stereo[std::size_t(i) * 2];
}

}  // namespace doc::media
