#include "import/asset_codecs.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
#include <set>

namespace picarat::import {

namespace {

// The codec bodies after a 4-byte header: `kind` is the BIOS type byte
// (0 stored, 0x10 LZ10, 0x30 RL, 0x24/0x28 Huffman 4/8), `size` the
// expanded size.
Bytes expandStream(std::span<const std::uint8_t> data, unsigned kind, std::size_t size,
                   std::size_t limit) {
    ByteReader r(data, 4);
    check(size > 0 && size <= limit, "invalid or excessive expanded size");
    if (kind == 0) {
        // Stored: the bytes follow the header unchanged (the third and
        // fourth adventures' files that do not shrink; the ROM pads them).
        const auto stored = r.take(size);
        return Bytes(stored.begin(), stored.end());
    }
    Bytes out;
    out.reserve(size);
    if (kind == 0x10) {
        while (out.size() < size) {
            const std::uint8_t flags = r.u8();
            for (int bit = 7; bit >= 0 && out.size() < size; --bit) {
                if (flags & (1 << bit)) {
                    const std::uint8_t a = r.u8(), b = r.u8();
                    const std::size_t count = (a >> 4) + 3, distance = ((a & 15) << 8 | b) + 1;
                    check(distance <= out.size(), "LZ back-reference precedes output");
                    check(count <= size - out.size(), "LZ token exceeds expanded size");
                    for (std::size_t i = 0; i < count; ++i) {
                        out.push_back(out[out.size() - distance]);
                    }
                } else {
                    out.push_back(r.u8());
                }
            }
        }
    } else if (kind == 0x30) {
        while (out.size() < size) {
            const std::uint8_t flag = r.u8();
            const std::size_t count = (flag & 127) + ((flag & 128) ? 3 : 1);
            check(count <= size - out.size(), "RL token exceeds expanded size");
            if (flag & 128) {
                out.insert(out.end(), count, r.u8());
            } else {
                auto run = r.take(count);
                out.insert(out.end(), run.begin(), run.end());
            }
        }
    } else {
        const std::size_t treeEnd = 4 + (r.u8() + 1u) * 2;
        check(treeEnd <= data.size(), "Huffman tree outside data");
        r.take(treeEnd - r.position());
        std::size_t node = 5;
        int remaining = 0;
        std::uint32_t word = 0;
        int lowNibble = -1;
        while (out.size() < size) {
            check(node >= 5 && node < treeEnd, "Huffman node outside tree");
            if (remaining == 0) {
                word = r.u32();
                remaining = 32;
            }
            --remaining;
            const unsigned branch = (word >> remaining) & 1;
            const std::uint8_t descriptor = data[node];
            const std::size_t child = (node & ~std::size_t(1)) + (descriptor & 63) * 2 + 2 + branch;
            check(child >= 5 && child < treeEnd, "Huffman child outside tree");
            if (descriptor & (0x80 >> branch)) {
                const std::uint8_t symbol = data[child];
                if (kind == 0x28) {
                    out.push_back(symbol);
                } else {
                    check(symbol < 16, "4-bit Huffman leaf exceeds nibble");
                    if (lowNibble < 0) {
                        lowNibble = symbol;
                    } else {
                        out.push_back(std::uint8_t(lowNibble | symbol << 4));
                        lowNibble = -1;
                    }
                }
                node = 5;
            } else {
                node = child;
            }
        }
    }
    return out;
}

} // namespace

Bytes decompress(std::span<const std::uint8_t> data, std::size_t limit) {
    const std::uint32_t header = ByteReader(data).u32();
    const unsigned kind = header & 255;
    check(kind == 0 || kind == 0x10 || kind == 0x30 || kind == 0x24 || kind == 0x28,
          "unsupported compression");
    return expandStream(data, kind, header >> 8, limit);
}

Bytes decompressMethodWord(std::span<const std::uint8_t> data, std::size_t limit) {
    static constexpr unsigned kKinds[] = {0, 0x10, 0x24, 0x28, 0x30};
    const std::uint32_t header = ByteReader(data).u32();
    check((header & 7) < 5, "unsupported compression method");
    if (header >> 3 == 0) {
        return {}; // an empty table (the 3DS adventures' scripts have some)
    }
    return expandStream(data, kKinds[header & 7], header >> 3, limit);
}

static bool endsWith(std::string_view text, std::string_view suffix) {
    return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

Bytes expandAsset(std::span<const std::uint8_t> data, std::string_view path) {
    if (endsWith(path, ".arc") || endsWith(path, ".arj")) {
        ByteReader r(data);
        const std::uint32_t tag = r.u32();
        static constexpr std::uint8_t expected[] = {0, 0x30, 0x10, 0x24, 0x28};
        check(tag >= 1 && tag <= 4 && data.size() > 4 && data[4] == expected[tag],
              "unknown or inconsistent graphic compression wrapper");
        return decompress(data.subspan(4));
    }
    if (endsWith(path, ".plz") || endsWith(path, ".dlz")) {
        return decompress(data);
    }
    // The third adventure: a 'c' before a four-letter extension (.cimg,
    // .cani, .cbin, ...) marks a bare stream, possibly stored.
    const auto dot = path.rfind('.');
    if (dot != std::string_view::npos && path.size() - dot == 5 && path[dot + 1] == 'c' &&
        path.starts_with("lt3/")) {
        return decompress(data, 32u << 20);
    }
    return Bytes(data.begin(), data.end());
}

static std::string folded(std::string text) {
    for (char& c : text) {
        c = char(std::tolower(std::uint8_t(c)));
    }
    return text;
}

std::vector<ArchiveMember> archiveMembers(std::span<const std::uint8_t> data) {
    ByteReader r(data);
    const std::uint32_t headerSize = r.u32(), total = r.u32();
    const auto signature = r.take(4);
    check(std::equal(signature.begin(), signature.end(), "PCK2"), "missing PCK2 signature");
    check(headerSize >= 16 && headerSize <= total && total == data.size(), "invalid PCK2 extent");
    r.take(headerSize - r.position());
    std::vector<ArchiveMember> members;
    std::set<std::string> names;
    while (r.position() < total) {
        const std::size_t start = r.position();
        const std::uint32_t head = r.u32(), extent = r.u32();
        r.u32();
        const std::uint32_t size = r.u32();
        check(head >= 17 && head <= extent && extent <= total - start,
              "invalid PCK2 member extent");
        check(size <= extent - head, "PCK2 payload exceeds member");
        const auto nameRegion = r.take(head - 16);
        const auto end = std::find(nameRegion.begin(), nameRegion.end(), 0);
        check(end != nameRegion.end(), "unterminated PCK2 name");
        check(std::all_of(nameRegion.begin(), end, [](std::uint8_t c) { return c < 128; }),
              "non-ASCII PCK2 name");
        std::string name = safeName(std::string(nameRegion.begin(), end));
        check(names.insert(folded(name)).second, "duplicate/case-colliding PCK2 name");
        const auto payload = r.take(size);
        r.take(extent - head - size);
        members.push_back({std::move(name), Bytes(payload.begin(), payload.end())});
    }
    return members;
}

Background decodeBackground(std::span<const std::uint8_t> data) {
    ByteReader r(data);
    Background bg;
    const std::uint32_t colorCount = r.u32();
    check(colorCount >= 1 && colorCount <= 256, "invalid background palette count");
    for (std::uint32_t i = 0; i < colorCount; ++i) {
        bg.palette.push_back(r.u16());
    }
    const std::uint32_t tileCount = r.u32();
    check(tileCount >= 1 && tileCount <= 1024, "invalid background tile count");
    bg.tileOffset = r.position();
    const auto tiles = r.take(tileCount * 64);
    const unsigned widthTiles = r.u16(), heightTiles = r.u16();
    check(widthTiles >= 1 && widthTiles <= 128 && heightTiles >= 1 && heightTiles <= 128,
          "invalid background dimensions");
    bg.width = widthTiles * 8;
    bg.height = heightTiles * 8;
    bg.indices.assign(std::size_t(bg.width) * bg.height, 0);
    for (unsigned cell = 0; cell < widthTiles * heightTiles; ++cell) {
        const std::uint16_t entry = r.u16();
        const unsigned tile = entry & 1023;
        check(tile < tileCount && (entry & 0xf000) == 0, "unknown background tile attribute");
        const unsigned x = cell % widthTiles * 8, y = cell / widthTiles * 8;
        for (unsigned py = 0; py < 8; ++py) {
            for (unsigned px = 0; px < 8; ++px) {
                const unsigned sx = (entry & 1024) ? 7 - px : px, sy = (entry & 2048) ? 7 - py : py;
                const std::uint8_t index = tiles[tile * 64 + sy * 8 + sx];
                check(index < colorCount, "background palette index outside palette");
                bg.indices[(y + py) * bg.width + x + px] = index;
            }
        }
    }
    check(r.remaining() == 0, "unparsed background tail");
    return bg;
}

// Fixed-size ASCII label: text up to the first NUL. Other bytes are spelled
// as \xNN so that names stay printable and stable.
static std::string fixedString(std::span<const std::uint8_t> raw) {
    std::string text;
    for (std::uint8_t c : raw) {
        if (c == 0) {
            break;
        }
        if (c < 128) {
            text += char(c);
        } else {
            char escaped[5];
            std::snprintf(escaped, sizeof escaped, "\\x%02x", c);
            text += escaped;
        }
    }
    return text;
}

Sprite decodeSprite(std::span<const std::uint8_t> data, bool arj) {
    ByteReader r(data);
    Sprite sprite;
    const unsigned count = r.u16(), depthCode = r.u16();
    check(count >= 1 && count <= 4096 && (depthCode == 3 || depthCode == 4),
          "invalid sprite header");
    const unsigned bpp = 1u << (depthCode - 1);
    std::uint32_t colorCount = arj ? r.u32() : 0;
    for (unsigned f = 0; f < count; ++f) {
        SpriteFrame frame;
        frame.width = r.u16();
        frame.height = r.u16();
        const std::uint32_t chunkCount = r.u32();
        check(frame.width > 0 && frame.width <= 1024 && frame.height > 0 && frame.height <= 1024 &&
                  chunkCount <= 4096,
              "invalid sprite dimensions/chunk count");
        frame.indices.assign(std::size_t(frame.width) * frame.height, 0);
        for (std::uint32_t c = 0; c < chunkCount; ++c) {
            if (arj) {
                r.u16();
                r.u16();
            }
            const unsigned x = r.u16(), y = r.u16(), ex = r.u16(), ey = r.u16();
            check(ex <= 7 && ey <= 7, "excessive sprite chunk dimensions");
            const unsigned cw = 8u << ex, ch = 8u << ey;
            const auto packed = r.take(cw * ch * bpp / 8);
            // Chunks are clipped to the frame. One lies wholly outside it
            // (first adventure, ani/en/mm_btn_memo.arc: x 64 in a frame 58
            // wide) and draws nothing.
            const unsigned visibleW = x < frame.width ? std::min(cw, frame.width - x) : 0;
            const unsigned visibleH = y < frame.height ? std::min(ch, frame.height - y) : 0;
            auto pixel = [&](unsigned i) -> std::uint8_t {
                return bpp == 8 ? packed[i] : (i & 1) ? packed[i / 2] >> 4 : packed[i / 2] & 15;
            };
            for (unsigned py = 0; py < visibleH; ++py) {
                for (unsigned px = 0; px < visibleW; ++px) {
                    const unsigned source =
                        arj ? (py / 8 * (cw / 8) + px / 8) * 64 + py % 8 * 8 + px % 8
                            : py * cw + px;
                    frame.indices[(y + py) * frame.width + x + px] = pixel(source);
                }
            }
            frame.chunks.push_back({int(x), int(y), int(cw), int(ch)});
        }
        sprite.frames.push_back(std::move(frame));
    }
    if (!arj) {
        colorCount = r.u32();
    }
    check(colorCount >= 1 && colorCount <= (1u << bpp), "invalid sprite palette size");
    for (std::uint32_t i = 0; i < colorCount; ++i) {
        sprite.palette.push_back(r.u16());
    }
    r.take(30);
    const std::uint32_t animationCount = r.u32();
    check(animationCount <= 4096, "excessive animation count");
    for (std::uint32_t i = 0; i < animationCount; ++i) {
        sprite.animations.push_back({fixedString(r.take(30)), {}, {}, {}});
    }
    for (SpriteAnimation& animation : sprite.animations) {
        const std::uint32_t steps = r.u32();
        check(steps <= 65536, "excessive animation steps");
        for (auto* array : {&animation.next, &animation.durations, &animation.images}) {
            for (std::uint32_t s = 0; s < steps; ++s) {
                array->push_back(r.u32());
            }
        }
        check(std::all_of(animation.images.begin(), animation.images.end(),
                          [&](std::uint32_t i) { return i < count; }),
              "animation image index outside frame table");
    }
    const auto extension = r.take(r.remaining());
    if (extension.size() >= 514 && extension[0] == 0x34 && extension[1] == 0x12) {
        ByteReader e(extension, 2);
        sprite.hasVariables = true;
        for (int i = 0; i < 16; ++i) {
            sprite.variables.push_back(fixedString(e.take(16)));
        }
        for (auto& plane : sprite.planes) {
            for (auto& value : plane) {
                value = e.i16();
            }
        }
        const auto tail = e.take(e.remaining());
        sprite.tail.assign(tail.begin(), tail.end());
    }
    for (const SpriteFrame& frame : sprite.frames) {
        check(std::all_of(frame.indices.begin(), frame.indices.end(),
                          [&](std::uint8_t i) { return i < colorCount; }),
              "sprite pixel index outside palette");
    }
    return sprite;
}

Font decodeFont(std::span<const std::uint8_t> data) {
    ByteReader r(data);
    const auto signature = r.take(4);
    check(std::equal(signature.begin(), signature.end(), "RTFN"), "missing NFTR signature");
    const std::uint16_t bom = r.u16(), version = r.u16();
    const std::uint32_t size = r.u32();
    const std::uint16_t head = r.u16(), blockCount = r.u16();
    check(bom == 0xfeff && version == 0x100 && size == data.size() && head == 16,
          "unsupported NFTR header");

    struct Block {
        std::string tag;
        std::span<const std::uint8_t> payload;
    };

    std::map<std::uint32_t, Block> blocks; // keyed by payload offset, as pointers refer to it
    for (unsigned i = 0; i < blockCount; ++i) {
        const std::size_t offset = r.position();
        const auto tag = r.take(4);
        const std::uint32_t extent = r.u32();
        check(extent >= 8, "invalid NFTR block size");
        blocks[std::uint32_t(offset + 8)] = {std::string(tag.begin(), tag.end()),
                                             r.take(extent - 8)};
    }
    check(r.remaining() == 0, "unparsed NFTR tail");
    const Block* info = nullptr;
    for (const auto& [offset, block] : blocks) {
        if (block.tag == "FNIF") {
            check(!info, "missing/duplicate font info");
            info = &block;
        }
    }
    check(info != nullptr, "missing/duplicate font info");
    ByteReader infoReader(info->payload);
    infoReader.take(8);
    const std::uint32_t glyphPointer = infoReader.u32();
    std::uint32_t widthPointer = infoReader.u32(), mapPointer = infoReader.u32();
    auto block = [&](std::uint32_t pointer, const char* tag) {
        const auto found = blocks.find(pointer);
        check(found != blocks.end() && found->second.tag == tag, "invalid NFTR block pointer");
        return ByteReader(found->second.payload);
    };
    ByteReader glyphs = block(glyphPointer, "PLGC");
    const unsigned width = glyphs.u8(), height = glyphs.u8(), cellSize = glyphs.u16();
    glyphs.u8();
    glyphs.u8();
    const unsigned bpp = glyphs.u8(), rotation = glyphs.u8();
    check(width > 0 && height > 0 && bpp >= 1 && bpp <= 4 && rotation == 0 &&
              cellSize == (width * height * bpp + 7) / 8,
          "unsupported NFTR glyph layout");
    std::map<unsigned, std::pair<std::int8_t, std::uint8_t>> metrics; // left, advance
    std::set<std::uint32_t> visited;
    while (widthPointer) {
        check(visited.insert(widthPointer).second, "cyclic NFTR width chain");
        ByteReader w = block(widthPointer, "HDWC");
        const unsigned first = w.u16(), last = w.u16();
        widthPointer = w.u32();
        check(first <= last, "invalid NFTR width range");
        for (unsigned index = first; index <= last; ++index) {
            const std::int8_t left = std::int8_t(w.u8());
            w.u8();
            const std::uint8_t advance = w.u8();
            check(metrics.emplace(index, std::pair{left, advance}).second,
                  "duplicate NFTR glyph metrics");
        }
        check(w.remaining() < 4, "extra NFTR width data");
    }
    const unsigned count = unsigned(metrics.size());
    check(count > 0 && metrics.rbegin()->first == count - 1, "non-contiguous NFTR glyphs");
    const auto cells = glyphs.take(std::size_t(cellSize) * count);
    check(glyphs.remaining() < 4, "extra NFTR glyph data");
    Font font;
    font.cellWidth = int(width);
    font.cellHeight = int(height);
    const unsigned maximum = (1u << bpp) - 1;
    auto alpha = [&](unsigned glyph) {
        Bytes values(width * height);
        const auto cell = cells.subspan(std::size_t(glyph) * cellSize, cellSize);
        for (unsigned pixel = 0; pixel < width * height; ++pixel) {
            unsigned value = 0;
            for (unsigned b = 0; b < bpp; ++b) {
                const unsigned bit = pixel * bpp + b; // MSB first across the cell
                value = value << 1 | ((cell[bit / 8] >> (7 - bit % 8)) & 1);
            }
            values[pixel] = std::uint8_t(value * 255 / maximum);
        }
        return values;
    };
    visited.clear();
    while (mapPointer) {
        check(visited.insert(mapPointer).second, "cyclic NFTR map chain");
        ByteReader m = block(mapPointer, "PAMC");
        const unsigned first = m.u16(), last = m.u16();
        const std::uint32_t method = m.u32();
        mapPointer = m.u32();
        check(first <= last, "invalid NFTR character range");
        std::vector<std::pair<unsigned, unsigned>> pairs;
        if (method == 0) {
            const unsigned base = m.u16();
            for (unsigned c = first; c <= last; ++c) {
                pairs.emplace_back(c, (base + c - first) & 0xffff);
            }
        } else if (method == 1) {
            for (unsigned c = first; c <= last; ++c) {
                pairs.emplace_back(c, m.u16());
            }
        } else if (method == 2) {
            const unsigned n = m.u16();
            for (unsigned i = 0; i < n; ++i) {
                const unsigned c = m.u16();
                pairs.emplace_back(c, m.u16());
            }
        } else {
            throw FormatError("unknown NFTR mapping method");
        }
        check(m.remaining() < 4, "extra NFTR map data");
        for (const auto& [character, index] : pairs) {
            if (index == 0xffff) {
                continue;
            }
            check(index < count, "NFTR mapping references missing glyph");
            const auto& [left, advance] = metrics.at(index);
            font.mapped.push_back({character, left, advance, alpha(index)});
        }
    }
    return font;
}

std::string ScriptToken::text() const {
    std::size_t end = bytes.size();
    while (end && bytes[end - 1] == 0) {
        --end;
    }
    return std::string(bytes.begin(), bytes.begin() + std::ptrdiff_t(end));
}

std::vector<ScriptToken> scriptTokens(std::span<const std::uint8_t> data) {
    ByteReader r(data);
    check(data.size() >= 4 && r.u32() == data.size() - 4, "GDS size mismatch");
    std::vector<ScriptToken> tokens;
    bool ended = false;
    while (r.remaining()) {
        ScriptToken token;
        token.tag = r.u16();
        if (token.tag == 0) {
            token.opcode = r.u16();
        } else if (token.tag == 1 || token.tag == 6 || token.tag == 7) {
            token.integer = r.i32();
        } else if (token.tag == 2) {
            auto raw = r.take(4);
            token.bytes.assign(raw.begin(), raw.end());
        } else if (token.tag == 3 || token.tag == 4) {
            auto raw = r.take(r.u16());
            token.bytes.assign(raw.begin(), raw.end());
        } else if (token.tag == 5 || (token.tag >= 8 && token.tag <= 11)) {
            // a marker without an operand
        } else if (token.tag == 12) {
            check(r.remaining() == 0, "GDS data after end token");
            ended = true;
        } else {
            throw FormatError("unknown GDS token");
        }
        tokens.push_back(std::move(token));
    }
    check(ended, "GDS lacks end token");
    return tokens;
}

} // namespace picarat::import
