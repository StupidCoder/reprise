#include "media/mp4_audio.hpp"

#include <algorithm>
#include <cctype>

namespace doc::media {
namespace {

std::uint32_t be32(const std::uint8_t* p) { return std::uint32_t(p[0]) << 24 | std::uint32_t(p[1]) << 16 | std::uint32_t(p[2]) << 8 | p[3]; }
std::uint64_t be64(const std::uint8_t* p) { return std::uint64_t(be32(p)) << 32 | be32(p + 4); }
std::uint16_t be16(const std::uint8_t* p) { return std::uint16_t(p[0] << 8 | p[1]); }

struct Box {
    std::uint32_t type = 0;
    std::span<const std::uint8_t> body;
};

std::uint32_t fourcc(const char* s) { return std::uint32_t(s[0]) << 24 | std::uint32_t(s[1]) << 16 | std::uint32_t(s[2]) << 8 | std::uint8_t(s[3]); }

// The boxes directly inside `data`; stops at the first malformed one.
std::vector<Box> boxes(std::span<const std::uint8_t> data) {
    std::vector<Box> out;
    std::size_t at = 0;
    while (data.size() - at >= 8) {
        std::uint64_t size = be32(&data[at]);
        const std::uint32_t type = be32(&data[at + 4]);
        std::size_t header = 8;
        if (size == 1) {
            if (data.size() - at < 16) break;
            size = be64(&data[at + 8]);
            header = 16;
        } else if (size == 0) {
            size = data.size() - at;
        }
        if (size < header || size > data.size() - at) break;
        out.push_back({type, data.subspan(at + header, std::size_t(size) - header)});
        at += std::size_t(size);
    }
    return out;
}

const Box* find(const std::vector<Box>& list, const char* type) {
    const std::uint32_t t = fourcc(type);
    for (const Box& b : list)
        if (b.type == t) return &b;
    return nullptr;
}

// An MPEG-4 descriptor's length (one to four bytes of seven bits each).
bool descriptorLength(std::span<const std::uint8_t> d, std::size_t& at, std::uint32_t& length) {
    length = 0;
    for (int i = 0; i < 4; ++i) {
        if (at >= d.size()) return false;
        const std::uint8_t b = d[at++];
        length = (length << 7) | (b & 0x7F);
        if (!(b & 0x80)) return true;
    }
    return true;
}

// The DecoderSpecificInfo inside an esds box (ES_Descriptor > DecoderConfigDescriptor).
bool decoderSpecificInfo(std::span<const std::uint8_t> esds, std::vector<std::uint8_t>& config) {
    if (esds.size() < 4) return false;
    std::span<const std::uint8_t> d = esds.subspan(4);  // version and flags
    std::size_t at = 0;
    std::uint32_t length = 0;
    if (at >= d.size() || d[at++] != 0x03 || !descriptorLength(d, at, length)) return false;
    if (d.size() - at < 3) return false;
    const std::uint8_t flags = d[at + 2];
    at += 3;                               // ES_ID, flags
    if (flags & 0x80) at += 2;             // dependsOn_ES_ID
    if (flags & 0x40) {                    // URL
        if (at >= d.size()) return false;
        at += 1 + d[at];
    }
    if (flags & 0x20) at += 2;             // OCR_ES_Id
    if (at >= d.size() || d[at++] != 0x04 || !descriptorLength(d, at, length)) return false;
    at += 13;  // objectTypeIndication, streamType, bufferSizeDB, maxBitrate, avgBitrate
    if (at >= d.size() || d[at++] != 0x05 || !descriptorLength(d, at, length)) return false;
    if (d.size() - at < length) return false;
    config.assign(d.begin() + std::ptrdiff_t(at), d.begin() + std::ptrdiff_t(at + length));
    return !config.empty();
}

// The iTunSMPB tag (moov/udta/meta/ilst/"----" with name "iTunSMPB"): hexadecimal words, the
// second the priming, the fourth the valid sample count.
void readGapless(const Box& moov, Mp4AudioTrack& track) {
    const auto inMoov = boxes(moov.body);
    const Box* udta = find(inMoov, "udta");
    if (!udta) return;
    const auto inUdta = boxes(udta->body);
    const Box* meta = find(inUdta, "meta");
    if (!meta || meta->body.size() < 4) return;
    const auto inMeta = boxes(meta->body.subspan(4));  // version and flags
    const Box* ilst = find(inMeta, "ilst");
    if (!ilst) return;
    for (const Box& item : boxes(ilst->body)) {
        if (item.type != fourcc("----")) continue;
        const auto parts = boxes(item.body);
        const Box* name = find(parts, "name");
        const Box* data = find(parts, "data");
        if (!name || !data || name->body.size() < 4 || data->body.size() < 8) continue;
        if (std::string(name->body.begin() + 4, name->body.end()) != "iTunSMPB") continue;
        const std::string text(data->body.begin() + 8, data->body.end());
        std::uint64_t words[4] = {};
        std::size_t at = 0;
        for (int w = 0; w < 4; ++w) {
            while (at < text.size() && text[at] == ' ') ++at;
            std::size_t end = at;
            while (end < text.size() && std::isxdigit(static_cast<unsigned char>(text[end]))) ++end;
            if (end == at) return;
            words[w] = std::stoull(text.substr(at, end - at), nullptr, 16);
            at = end;
        }
        track.priming = std::uint32_t(words[1]);
        track.validSamples = words[3];
        return;
    }
}

}  // namespace

std::optional<Mp4AudioTrack> readMp4AudioTrack(std::span<const std::uint8_t> file, std::string& error) {
    const auto top = boxes(file);
    const Box* moov = find(top, "moov");
    if (!moov) {
        error = "not an MP4 file (no movie box)";
        return std::nullopt;
    }
    for (const Box& trak : boxes(moov->body)) {
        if (trak.type != fourcc("trak")) continue;
        const auto inTrak = boxes(trak.body);
        const Box* mdia = find(inTrak, "mdia");
        if (!mdia) continue;
        const auto inMdia = boxes(mdia->body);
        const Box* hdlr = find(inMdia, "hdlr");
        const Box* mdhd = find(inMdia, "mdhd");
        const Box* minf = find(inMdia, "minf");
        if (!hdlr || hdlr->body.size() < 12 || be32(&hdlr->body[8]) != fourcc("soun") || !minf) continue;
        const auto inMinf = boxes(minf->body);
        const Box* stbl = find(inMinf, "stbl");
        if (!stbl) continue;
        const auto inStbl = boxes(stbl->body);
        const Box* stsd = find(inStbl, "stsd");
        const Box* stsz = find(inStbl, "stsz");
        const Box* stsc = find(inStbl, "stsc");
        const Box* stco = find(inStbl, "stco");
        const Box* co64 = find(inStbl, "co64");
        if (!stsd || !stsz || !stsc || (!stco && !co64) || stsd->body.size() < 8) continue;

        Mp4AudioTrack track;
        if (mdhd && mdhd->body.size() >= 24) track.timescale = be32(&mdhd->body[mdhd->body[0] == 1 ? 20 : 12]);
        // The sample entry: 'mp4a' with its esds.
        const auto entries = boxes(stsd->body.subspan(8));
        const Box* mp4a = find(entries, "mp4a");
        if (!mp4a || mp4a->body.size() < 28) continue;
        track.channels = be16(&mp4a->body[16]);
        track.sampleRate = be32(&mp4a->body[24]) >> 16;
        const auto inEntry = boxes(mp4a->body.subspan(28));
        const Box* esds = find(inEntry, "esds");
        if (!esds || !decoderSpecificInfo(esds->body, track.decoderConfig)) {
            error = "the audio track has no AAC configuration";
            return std::nullopt;
        }
        // Frame sizes, chunk offsets and how many frames each chunk holds.
        const auto& z = stsz->body;
        if (z.size() < 12) break;
        const std::uint32_t fixedSize = be32(&z[4]), count = be32(&z[8]);
        if (fixedSize == 0 && z.size() < 12 + std::size_t(count) * 4) break;
        std::vector<std::uint64_t> chunks;
        if (stco) {
            const auto& c = stco->body;
            const std::uint32_t n = c.size() >= 8 ? be32(&c[4]) : 0;
            for (std::uint32_t i = 0; i < n && 8 + std::size_t(i) * 4 + 4 <= c.size(); ++i) chunks.push_back(be32(&c[8 + i * 4]));
        } else {
            const auto& c = co64->body;
            const std::uint32_t n = c.size() >= 8 ? be32(&c[4]) : 0;
            for (std::uint32_t i = 0; i < n && 8 + std::size_t(i) * 8 + 8 <= c.size(); ++i) chunks.push_back(be64(&c[8 + i * 8]));
        }
        const auto& s = stsc->body;
        const std::uint32_t runs = s.size() >= 8 ? be32(&s[4]) : 0;
        std::uint32_t frame = 0;
        for (std::uint32_t r = 0; r < runs && 8 + std::size_t(r) * 12 + 12 <= s.size(); ++r) {
            const std::uint32_t first = be32(&s[8 + r * 12]), perChunk = be32(&s[8 + r * 12 + 4]);
            const std::uint32_t last = r + 1 < runs ? be32(&s[8 + (r + 1) * 12]) : std::uint32_t(chunks.size()) + 1;
            for (std::uint32_t chunk = first; chunk < last && chunk <= chunks.size(); ++chunk) {
                std::uint64_t offset = chunks[chunk - 1];
                for (std::uint32_t k = 0; k < perChunk && frame < count; ++k, ++frame) {
                    const std::uint32_t size = fixedSize ? fixedSize : be32(&z[12 + std::size_t(frame) * 4]);
                    if (offset + size > file.size()) {
                        error = "the file ends inside its audio";
                        return std::nullopt;
                    }
                    track.frames.push_back({offset, size});
                    offset += size;
                }
            }
        }
        if (track.frames.empty()) {
            error = "the audio track is empty";
            return std::nullopt;
        }
        readGapless(*moov, track);
        return track;
    }
    error = "no AAC audio track in the file";
    return std::nullopt;
}

}  // namespace doc::media
