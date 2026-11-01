// =============================================================================================
// AudioFormats.hpp - decoders for the console's sound file formats
// =============================================================================================
//
// The opening's music is a CSTM stream (BGM) and its sound effects come from CWAR wave archives
// holding CWAV sounds. These are the platform's general NW4C binary layouts (block header, typed
// references, DSP-ADPCM channel info) and contain no game-specific logic. Decoding produces
// 16-bit PCM, one sample vector per channel.
// =============================================================================================
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace captain_toad::audio {

// Little-endian reads.
inline uint16_t rd16(const uint8_t* p) {
    return uint16_t(p[0] | p[1] << 8);
}

inline uint32_t rd32(const uint8_t* p) {
    return uint32_t(p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24);
}

// DSP-ADPCM (codec 2) channel parameters: eight predictor coefficient pairs and the decoder
// history at the start and at the loop point.
struct DspAdpcmInfo {
    int16_t coef[16]{};
    uint16_t predScale = 0;
    int16_t hist1 = 0;
    int16_t hist2 = 0;
    uint16_t loopPredScale = 0;
    int16_t loopHist1 = 0;
    int16_t loopHist2 = 0;
};

inline int16_t clamp16(int v) {
    return int16_t(std::min(32767, std::max(-32768, v)));
}

// Decode `samples` DSP-ADPCM samples starting at frame-aligned data. Each 8-byte frame is one
// header byte (predictor index in the high nibble, scale exponent in the low nibble) and 14
// 4-bit samples. Every sample is predicted from the previous two (hist1, hist2) with the chosen
// coefficient pair; hist1 / hist2 carry across calls.
inline void decodeDspAdpcm(const uint8_t* data, size_t bytes, size_t samples, const int16_t coef[16], int& hist1,
                           int& hist2, int16_t* out) {
    size_t produced = 0;
    for (size_t f = 0; produced < samples && f * 8 < bytes; ++f) {
        const uint8_t* frame = data + f * 8;
        const int header = frame[0];
        const int pred = (header >> 4) & 7;
        const int scale = 1 << (header & 15);
        const int c1 = coef[pred * 2];
        const int c2 = coef[pred * 2 + 1];
        for (int i = 0; i < 14 && produced < samples; ++i) {
            int nibble = (frame[1 + i / 2] >> (i & 1 ? 0 : 4)) & 15;
            if (nibble >= 8) {
                nibble -= 16;
            }
            const int s = ((nibble * scale) << 11) + 1024 + c1 * hist1 + c2 * hist2;
            const int16_t v = clamp16(s >> 11);
            hist2 = hist1;
            hist1 = v;
            out[produced++] = v;
        }
    }
}

// IMA-ADPCM (codec 3) as used by CWAV: 4-bit nibbles, low nibble first.
inline void decodeImaAdpcm(const uint8_t* data, size_t samples, int& pred, int& index, int16_t* out) {
    static const int steps[89] = {
        7,    8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,    25,   28,
        31,   34,    37,    41,    45,    50,    55,    60,    66,    73,    80,    88,    97,    107,  118,
        130,  143,   157,   173,   190,   209,   230,   253,   279,   307,   337,   371,   408,   449,  494,
        544,  598,   658,   724,   796,   876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878, 2066,
        2272, 2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845, 8630,
        9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
    static const int indexStep[8] = {-1, -1, -1, -1, 2, 4, 6, 8};
    for (size_t i = 0; i < samples; ++i) {
        const int nibble = (data[i / 2] >> (i & 1 ? 4 : 0)) & 15;
        const int step = steps[index];
        int diff = step >> 3;
        if (nibble & 1) {
            diff += step >> 2;
        }
        if (nibble & 2) {
            diff += step >> 1;
        }
        if (nibble & 4) {
            diff += step;
        }
        pred = nibble & 8 ? std::max(-32768, pred - diff) : std::min(32767, pred + diff);
        index = std::min(88, std::max(0, index + indexStep[nibble & 7]));
        out[i] = int16_t(pred);
    }
}

inline DspAdpcmInfo readDspInfo(const uint8_t* p) {
    DspAdpcmInfo d;
    for (int i = 0; i < 16; ++i) {
        d.coef[i] = int16_t(rd16(p + i * 2));
    }
    d.predScale = rd16(p + 0x20);
    d.hist1 = int16_t(rd16(p + 0x22));
    d.hist2 = int16_t(rd16(p + 0x24));
    d.loopPredScale = rd16(p + 0x26);
    d.loopHist1 = int16_t(rd16(p + 0x28));
    d.loopHist2 = int16_t(rd16(p + 0x2a));
    return d;
}

// Decoded PCM sound: one vector per channel.
struct Pcm {
    uint32_t sampleRate = 0;
    bool loop = false;
    uint32_t loopStart = 0;   // samples
    uint32_t loopEnd = 0;     // samples; also the playable length
    std::vector<std::vector<int16_t>> ch;

    size_t frames() const { return ch.empty() ? 0 : ch[0].size(); }
};

// Block lookup: (id, offset, size) triples after the 0x14-byte file header.
inline const uint8_t* findBlock(const uint8_t* f, size_t n, uint16_t id, uint32_t* size = nullptr) {
    uint16_t count = rd16(f + 0x10);
    for (int i = 0; i < count; ++i) {
        const uint8_t* e = f + 0x14 + i * 12;
        if (rd16(e) == id) {
            uint32_t off = rd32(e + 4);
            if (size) {
                *size = rd32(e + 8);
            }
            if (off >= n) {
                throw std::runtime_error("block outside file");
            }
            return f + off;
        }
    }
    return nullptr;
}

// CSTM stream: the samples are stored as interleaved blocks of `blockSize` bytes per channel
// (the last block is shorter and padded).
inline Pcm decodeCstm(const std::vector<uint8_t>& file) {
    const uint8_t* f = file.data();
    if (file.size() < 0x40 || std::memcmp(f, "CSTM", 4)) {
        throw std::runtime_error("not CSTM");
    }
    const uint8_t* info = findBlock(f, file.size(), 0x4000);
    const uint8_t* data = findBlock(f, file.size(), 0x4002);
    if (!info || !data) {
        throw std::runtime_error("CSTM blocks");
    }
    const uint8_t* infoBody = info + 8;
    const uint8_t* streamInfo = infoBody + rd32(infoBody + 4);
    const int codec = streamInfo[0];
    const int loop = streamInfo[1];
    const int chans = streamInfo[2];

    Pcm pcm;
    pcm.sampleRate = rd32(streamInfo + 4);
    pcm.loop = loop != 0;
    pcm.loopStart = rd32(streamInfo + 8);
    pcm.loopEnd = rd32(streamInfo + 0xc);
    const uint32_t blocks = rd32(streamInfo + 0x10);
    const uint32_t blockSize = rd32(streamInfo + 0x14);
    const uint32_t blockSamples = rd32(streamInfo + 0x18);
    const uint32_t lastSize = rd32(streamInfo + 0x1c);
    const uint32_t lastSamples = rd32(streamInfo + 0x20);
    const uint32_t lastPadded = rd32(streamInfo + 0x24);
    const uint32_t dataOff = rd32(streamInfo + 0x34);
    const uint8_t* samples = data + 8 + dataOff;

    // Channel info table.
    const uint8_t* channelTable = infoBody + rd32(infoBody + 0x14);
    std::vector<DspAdpcmInfo> dsp(chans);
    for (int c = 0; c < chans; ++c) {
        const uint8_t* ref = channelTable + 4 + c * 8;
        const uint8_t* channelInfo = channelTable + rd32(ref + 4);
        if (codec == 2) {
            dsp[c] = readDspInfo(channelInfo + rd32(channelInfo + 4));
        }
    }

    pcm.ch.assign(chans, std::vector<int16_t>(pcm.loopEnd));
    std::vector<int> h1(chans);
    std::vector<int> h2(chans);
    for (int c = 0; c < chans; ++c) {
        h1[c] = dsp[c].hist1;
        h2[c] = dsp[c].hist2;
    }
    size_t pos = 0;
    const uint8_t* p = samples;
    for (uint32_t b = 0; b < blocks; ++b) {
        const bool last = b + 1 == blocks;
        const uint32_t size = last ? lastSize : blockSize;
        const uint32_t stride = last ? lastPadded : blockSize;
        const uint32_t blockSampleCount = last ? lastSamples : blockSamples;
        const size_t take = std::min<size_t>(blockSampleCount, pcm.loopEnd > pos ? pcm.loopEnd - pos : 0);
        for (int c = 0; c < chans; ++c) {
            const uint8_t* src = p + c * stride;
            if (codec == 2) {
                decodeDspAdpcm(src, size, take, dsp[c].coef, h1[c], h2[c], pcm.ch[c].data() + pos);
            } else if (codec == 1) {
                for (size_t i = 0; i < take; ++i) {
                    pcm.ch[c][pos + i] = int16_t(rd16(src + i * 2));
                }
            } else if (codec == 0) {
                for (size_t i = 0; i < take; ++i) {
                    pcm.ch[c][pos + i] = int16_t(int8_t(src[i]) * 256);
                }
            } else {
                throw std::runtime_error("CSTM codec");
            }
        }
        p += stride * chans;
        pos += take;
    }
    return pcm;
}

// CWAV: one sound with per-channel sample references into its DATA block.
inline Pcm decodeCwav(const uint8_t* f, size_t n) {
    if (n < 0x40 || std::memcmp(f, "CWAV", 4)) {
        throw std::runtime_error("not CWAV");
    }
    const uint8_t* info = findBlock(f, n, 0x7000);
    const uint8_t* data = findBlock(f, n, 0x7001);
    if (!info || !data) {
        throw std::runtime_error("CWAV blocks");
    }
    const uint8_t* infoBody = info + 8;
    const int codec = infoBody[0];
    const int loop = infoBody[1];

    Pcm pcm;
    pcm.sampleRate = rd32(infoBody + 4);
    pcm.loop = loop != 0;
    pcm.loopStart = rd32(infoBody + 8);
    pcm.loopEnd = rd32(infoBody + 0xc);
    const uint8_t* channelTable = infoBody + 0x14;   // channel info reference table
    const uint32_t chans = rd32(channelTable);
    pcm.ch.assign(chans, std::vector<int16_t>(pcm.loopEnd));
    for (uint32_t c = 0; c < chans; ++c) {
        const uint8_t* channelInfo = channelTable + rd32(channelTable + 4 + c * 8 + 4);
        const uint8_t* src = data + 8 + rd32(channelInfo + 4);
        const uint8_t* adpcmRef = channelInfo + 8;
        int16_t* out = pcm.ch[c].data();
        const size_t sampleCount = pcm.loopEnd;
        if (codec == 2) {
            DspAdpcmInfo d = readDspInfo(channelInfo + rd32(adpcmRef + 4));
            int a = d.hist1;
            int b = d.hist2;
            decodeDspAdpcm(src, (sampleCount + 13) / 14 * 8, sampleCount, d.coef, a, b, out);
        } else if (codec == 3) {
            const uint8_t* ima = channelInfo + rd32(adpcmRef + 4);
            int pred = int16_t(rd16(ima));
            int index = ima[2];
            decodeImaAdpcm(src, sampleCount, pred, index, out);
        } else if (codec == 1) {
            for (size_t i = 0; i < sampleCount; ++i) {
                out[i] = int16_t(rd16(src + i * 2));
            }
        } else if (codec == 0) {
            for (size_t i = 0; i < sampleCount; ++i) {
                out[i] = int16_t(int8_t(src[i]) * 256);
            }
        } else {
            throw std::runtime_error("CWAV codec");
        }
    }
    return pcm;
}

// CWAR: a table of internal file references to the CWAV files inside its FILE block.
struct Cwar {
    const uint8_t* base = nullptr;
    size_t size = 0;
    std::vector<std::pair<uint32_t, uint32_t>> waves;   // (offset into the archive, size)
};

inline Cwar parseCwar(const uint8_t* f, size_t n) {
    if (n < 0x40 || std::memcmp(f, "CWAR", 4)) {
        throw std::runtime_error("not CWAR");
    }
    const uint8_t* info = findBlock(f, n, 0x6800);
    const uint8_t* file = findBlock(f, n, 0x6801);
    Cwar w{f, n, {}};
    uint32_t count = rd32(info + 8);
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* e = info + 0xc + i * 12;   // (type 0x1f00, offset relative to FILE + 8, size)
        w.waves.push_back({uint32_t(file + 8 - f) + rd32(e + 4), rd32(e + 8)});
    }
    return w;
}

}   // namespace captain_toad::audio
