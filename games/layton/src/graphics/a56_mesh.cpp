// Vertex and index buffers of the 3DS adventures. See a56_mesh.hpp.
#include "graphics/a56_mesh.hpp"
#include "import/asset_codecs.hpp"
#include <cstring>
#include <stdexcept>

namespace picarat::adventure56 {

namespace {

void check(bool ok, const char* what) {
    if (!ok) {
        throw std::runtime_error(what);
    }
}

unsigned u16(std::span<const std::uint8_t> d, std::size_t at) {
    return unsigned(d[at] | d[at + 1] << 8);
}

std::uint32_t u32(std::span<const std::uint8_t> d, std::size_t at) {
    return std::uint32_t(d[at] | d[at + 1] << 8 | d[at + 2] << 16) | std::uint32_t(d[at + 3]) << 24;
}

float f32(const std::uint8_t* p) {
    float f;
    std::memcpy(&f, p, 4);
    return f;
}

} // namespace

VertexBuffer parseVertexBuffer(std::span<const std::uint8_t> data) {
    check(data.size() >= 0x10 && std::memcmp(data.data(), "XPVB", 4) == 0, "not an XPVB buffer");
    const unsigned listAt = u16(data, 4), constantsAt = u16(data, 6), verticesAt = u16(data, 8),
                   stride = u16(data, 10);
    VertexBuffer vb;
    vb.count = u32(data, 12);
    check(listAt < data.size() && constantsAt < data.size() && verticesAt < data.size(),
          "XPVB streams outside the buffer");
    const Bytes list = import::decompressMethodWord(data.subspan(listAt));
    const Bytes constants = import::decompressMethodWord(data.subspan(constantsAt));
    const Bytes vertices = import::decompressMethodWord(data.subspan(verticesAt));
    check(list.size() >= VertexBuffer::kSlots * 4, "XPVB attribute list too short");
    check(vertices.size() >= std::size_t(stride) * vb.count, "XPVB vertices too short");
    for (int slot = 0; slot < VertexBuffer::kSlots; ++slot) {
        const std::uint8_t* a = list.data() + slot * 4;
        const unsigned components = a[0], offset = a[1], kind = a[3];
        if (components == 0 || kind == 0) {
            continue;
        }
        check(components <= 4 && (kind == 1 || kind == 2), "XPVB attribute of an unknown kind");
        vb.components[std::size_t(slot)] = int(components);
        auto& out = vb.values[std::size_t(slot)];
        out.assign(std::size_t(vb.count) * 4, 0.0f);
        for (unsigned v = 0; v < vb.count; ++v) {
            float* o = out.data() + std::size_t(v) * 4;
            o[3] = 1.0f;
            const std::uint8_t* source;
            if (kind == 2) {
                check(offset + components * 4 <= stride, "XPVB attribute outside the vertex");
                source = vertices.data() + std::size_t(v) * stride + offset;
            } else {
                check(offset + components * 4 <= constants.size(), "XPVB constant outside");
                source = constants.data() + offset;
            }
            for (unsigned c = 0; c < components; ++c) {
                o[c] = f32(source + c * 4);
            }
        }
    }
    return vb;
}

std::vector<std::uint16_t> parseIndexBuffer(std::span<const std::uint8_t> data) {
    check(data.size() >= 0x0c && std::memcmp(data.data(), "XPVI", 4) == 0, "not an XPVI buffer");
    const unsigned at = u16(data, 6);
    const std::uint32_t count = u32(data, 8);
    check(at < data.size() || count == 0, "XPVI stream outside the buffer");
    if (count == 0) {
        return {};
    }
    const Bytes raw = import::decompressMethodWord(data.subspan(at));
    check(raw.size() >= std::size_t(count) * 2, "XPVI indices too short");
    std::vector<std::uint16_t> indices(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        indices[i] = std::uint16_t(raw[i * 2] | raw[i * 2 + 1] << 8);
    }
    if (u16(data, 4) != 2) {
        return indices;
    }
    // A strip: triangle i is (i, i+1, i+2), every other one turned to keep
    // the winding; repeated indices join strips (degenerate triangles).
    std::vector<std::uint16_t> list;
    list.reserve(std::size_t(count) * 3);
    for (std::uint32_t i = 0; i + 2 < count; ++i) {
        const std::uint16_t a = indices[i], b = indices[i + 1], c = indices[i + 2];
        if (a == b || b == c || a == c) {
            continue;
        }
        if (i % 2 == 0) {
            list.insert(list.end(), {a, b, c});
        } else {
            list.insert(list.end(), {b, a, c});
        }
    }
    return list;
}

} // namespace picarat::adventure56
