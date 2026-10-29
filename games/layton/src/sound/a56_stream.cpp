// Sound streams of the 3DS adventures. See a56_stream.hpp.
#include "sound/a56_stream.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>

namespace picarat::adventure56 {

namespace {

void check(bool ok, const char* what) {
    if (!ok) {
        throw std::runtime_error(what);
    }
}

struct Reader {
    std::span<const std::uint8_t> d;

    std::uint32_t u32(std::size_t at) const {
        check(at + 4 <= d.size(), "BCSTM too short");
        return std::uint32_t(d[at]) | std::uint32_t(d[at + 1]) << 8 |
               std::uint32_t(d[at + 2]) << 16 | std::uint32_t(d[at + 3]) << 24;
    }

    std::uint16_t u16(std::size_t at) const {
        check(at + 2 <= d.size(), "BCSTM too short");
        return std::uint16_t(d[at] | d[at + 1] << 8);
    }

    std::uint8_t u8(std::size_t at) const {
        check(at < d.size(), "BCSTM too short");
        return d[at];
    }
};

} // namespace

void decodeDspAdpcm(std::span<const std::uint8_t> data, std::size_t frames,
                    const std::int16_t coef[16], std::int16_t& h1, std::int16_t& h2,
                    std::int16_t* out, int stride) {
    std::size_t done = 0;
    for (std::size_t f = 0; done < frames && f * 8 < data.size(); ++f) {
        const std::uint8_t header = data[f * 8];
        const int index = (header >> 4) & 7, scale = header & 15;
        const int c1 = coef[index * 2], c2 = coef[index * 2 + 1];
        for (int i = 0; i < 14 && done < frames; ++i) {
            const std::size_t at = f * 8 + 1 + std::size_t(i / 2);
            if (at >= data.size()) {
                return;
            }
            int delta = i % 2 == 0 ? data[at] >> 4 : data[at] & 15;
            if (delta >= 8) {
                delta -= 16;
            }
            int s = (c1 * h1 + c2 * h2 + ((delta << scale) << 11) + 1024) >> 11;
            s = std::clamp(s, -32768, 32767);
            h2 = h1;
            h1 = std::int16_t(s);
            out[done * std::size_t(stride)] = std::int16_t(s);
            ++done;
        }
    }
}

StreamInfo readStreamInfo(std::span<const std::uint8_t> file) {
    Reader r{file};
    check(file.size() >= 0x40 && std::memcmp(file.data(), "CSTM", 4) == 0, "not a BCSTM stream");
    const unsigned blockCount = r.u16(0x10);
    std::size_t info = 0;
    for (unsigned i = 0; i < blockCount; ++i) {
        if (r.u16(0x14 + i * 12) == 0x4000) {
            info = r.u32(0x14 + i * 12 + 4);
        }
    }
    check(info != 0, "BCSTM without INFO");
    const std::size_t base = info + 8;
    const std::size_t stream = base + r.u32(base + 4);
    StreamInfo s;
    s.loop = r.u8(stream + 1) != 0;
    s.rate = int(r.u32(stream + 4));
    s.loopStart = r.u32(stream + 8);
    s.frames = r.u32(stream + 12);
    return s;
}

Pcm decodeStream(std::span<const std::uint8_t> file) {
    Reader r{file};
    check(file.size() >= 0x40 && std::memcmp(file.data(), "CSTM", 4) == 0, "not a BCSTM stream");
    const unsigned blockCount = r.u16(0x10);
    std::size_t info = 0, data = 0;
    for (unsigned i = 0; i < blockCount; ++i) {
        const std::uint16_t type = r.u16(0x14 + i * 12);
        const std::size_t offset = r.u32(0x14 + i * 12 + 4);
        if (type == 0x4000) {
            info = offset;
        } else if (type == 0x4002) {
            data = offset;
        }
    }
    check(info && data, "BCSTM without INFO or DATA");
    const std::size_t base = info + 8;
    const std::size_t stream = base + r.u32(base + 4);
    const std::size_t channelTable = base + r.u32(base + 16 + 4);
    Pcm pcm;
    const int codec = r.u8(stream);
    pcm.loop = r.u8(stream + 1) != 0;
    pcm.channels = r.u8(stream + 2);
    pcm.rate = int(r.u32(stream + 4));
    pcm.loopStart = r.u32(stream + 8);
    const std::size_t frames = r.u32(stream + 12);
    const std::size_t blocks = r.u32(stream + 16), blockSize = r.u32(stream + 20),
                      blockSamples = r.u32(stream + 24), lastSize = r.u32(stream + 28),
                      lastSamples = r.u32(stream + 32), lastPadded = r.u32(stream + 36);
    const std::size_t samplesAt = data + 8 + r.u32(stream + 48 + 4);
    check(pcm.channels >= 1 && pcm.channels <= 8 && codec <= 2, "BCSTM of an unknown layout");
    // Per channel: the ADPCM coefficients and start history.
    std::vector<std::array<std::int16_t, 16>> coefs(std::size_t(pcm.channels));
    std::vector<std::int16_t> h1(std::size_t(pcm.channels)), h2(std::size_t(pcm.channels));
    if (codec == 2) {
        for (int c = 0; c < pcm.channels; ++c) {
            const std::size_t channel =
                channelTable + r.u32(channelTable + 4 + std::size_t(c) * 8 + 4);
            const std::size_t adpcm = channel + r.u32(channel + 4);
            for (int k = 0; k < 16; ++k) {
                coefs[std::size_t(c)][std::size_t(k)] =
                    std::int16_t(r.u16(adpcm + std::size_t(k) * 2));
            }
            h1[std::size_t(c)] = std::int16_t(r.u16(adpcm + 34));
            h2[std::size_t(c)] = std::int16_t(r.u16(adpcm + 36));
        }
    }
    pcm.samples.assign(frames * std::size_t(pcm.channels), 0);
    std::size_t at = samplesAt, written = 0;
    for (std::size_t b = 0; b < blocks && written < frames; ++b) {
        const bool last = b + 1 == blocks;
        const std::size_t size = last ? lastSize : blockSize,
                          stride = last ? lastPadded : blockSize;
        const std::size_t count = std::min(last ? lastSamples : blockSamples, frames - written);
        for (int c = 0; c < pcm.channels; ++c) {
            const std::size_t from = at + std::size_t(c) * stride;
            check(from + size <= file.size(), "BCSTM data outside the file");
            std::int16_t* out =
                pcm.samples.data() + written * std::size_t(pcm.channels) + std::size_t(c);
            const auto block = file.subspan(from, size);
            if (codec == 2) {
                decodeDspAdpcm(block, count, coefs[std::size_t(c)].data(), h1[std::size_t(c)],
                               h2[std::size_t(c)], out, pcm.channels);
            } else if (codec == 1) {
                for (std::size_t i = 0; i < count && i * 2 + 1 < size; ++i) {
                    out[i * std::size_t(pcm.channels)] =
                        std::int16_t(block[i * 2] | block[i * 2 + 1] << 8);
                }
            } else {
                for (std::size_t i = 0; i < count && i < size; ++i) {
                    out[i * std::size_t(pcm.channels)] = std::int16_t(std::int8_t(block[i]) * 256);
                }
            }
        }
        at += stride * std::size_t(pcm.channels);
        written += count;
    }
    if (pcm.loopStart >= frames) {
        pcm.loop = false;
    }
    return pcm;
}

Pcm decodeWave(std::span<const std::uint8_t> file) {
    Reader r{file};
    check(file.size() >= 0x40 && std::memcmp(file.data(), "CWAV", 4) == 0, "not a BCWAV wave");
    std::size_t info = 0, data = 0;
    for (unsigned i = 0, n = r.u16(0x10); i < n; ++i) {
        const std::uint16_t type = r.u16(0x14 + i * 12);
        const std::size_t offset = r.u32(0x14 + i * 12 + 4);
        if (type == 0x7000) {
            info = offset;
        } else if (type == 0x7001) {
            data = offset;
        }
    }
    check(info && data, "BCWAV without INFO or DATA");
    const std::size_t base = info + 8;
    Pcm pcm;
    const int codec = r.u8(base);
    pcm.loop = r.u8(base + 1) != 0;
    pcm.rate = int(r.u32(base + 4));
    pcm.loopStart = r.u32(base + 8);
    const std::size_t frames = r.u32(base + 12);
    const std::size_t channels = base + 20;
    pcm.channels = int(r.u32(channels));
    check(pcm.channels >= 1 && pcm.channels <= 2 && codec <= 2, "BCWAV of an unknown layout");
    pcm.samples.assign(frames * std::size_t(pcm.channels), 0);
    for (int c = 0; c < pcm.channels; ++c) {
        const std::size_t channel = channels + r.u32(channels + 4 + std::size_t(c) * 8 + 4);
        const std::size_t from = data + 8 + r.u32(channel + 4);
        check(from <= file.size(), "BCWAV data outside the file");
        const auto bytes = file.subspan(from);
        std::int16_t* out = pcm.samples.data() + c;
        if (codec == 2) {
            const std::size_t adpcm = channel + r.u32(channel + 12);
            std::int16_t coef[16];
            for (int k = 0; k < 16; ++k) {
                coef[k] = std::int16_t(r.u16(adpcm + std::size_t(k) * 2));
            }
            std::int16_t h1 = std::int16_t(r.u16(adpcm + 34)), h2 = std::int16_t(r.u16(adpcm + 36));
            decodeDspAdpcm(bytes, frames, coef, h1, h2, out, pcm.channels);
        } else if (codec == 1) {
            for (std::size_t i = 0; i < frames && i * 2 + 1 < bytes.size(); ++i) {
                out[i * std::size_t(pcm.channels)] =
                    std::int16_t(bytes[i * 2] | bytes[i * 2 + 1] << 8);
            }
        } else {
            for (std::size_t i = 0; i < frames && i < bytes.size(); ++i) {
                out[i * std::size_t(pcm.channels)] = std::int16_t(std::int8_t(bytes[i]) * 256);
            }
        }
    }
    if (pcm.loopStart >= frames) {
        pcm.loop = false;
    }
    return pcm;
}

} // namespace picarat::adventure56
