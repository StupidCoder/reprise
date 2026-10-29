// The DS 3D hardware model. See gx.hpp.
//
// The pipeline, as on the DS: geometry commands arrive through the FIFO
// (packed() / command()); matrices transform vertices into clip space;
// lighting colours them; finished polygons are collected; at SWAP_BUFFERS
// each polygon is clipped to the view volume, culled, projected to the
// screen and handed to the rasterizer (raster.hpp), which produces the
// 256x192 frame the scene composes with its 2D layers.
#include "graphics/nds3d/gx.hpp"
#include "graphics/nds3d/raster.hpp"
#include <algorithm>
#include <cmath>
#include <memory>

namespace picarat::nds3d {

namespace {
// Sign-extends the low `bits` bits of v.
std::int32_t signExtend(std::uint32_t v, int bits) {
    const std::uint32_t mask = 1u << (bits - 1);
    v &= (1u << bits) - 1;
    return std::int32_t((v ^ mask) - mask);
}

float toFloat(fx32 v) {
    return float(v) / 4096.0f;
}

// The 3x3 part of a matrix applied to a direction (row vector).
std::array<float, 3> turn(const std::array<float, 3>& v, const Matrix& m) {
    std::array<float, 3> out{};
    for (int c = 0; c < 3; ++c) {
        out[std::size_t(c)] = v[0] * toFloat(m.m[std::size_t(c)]) +
                              v[1] * toFloat(m.m[std::size_t(4 + c)]) +
                              v[2] * toFloat(m.m[std::size_t(8 + c)]);
    }
    return out;
}

float dot(const std::array<float, 3>& a, const std::array<float, 3>& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

int component(std::uint16_t color, int shift) {
    return (color >> shift) & 31;
}
} // namespace

Matrix Matrix::identity() {
    Matrix m;
    m.m[0] = m.m[5] = m.m[10] = m.m[15] = 0x1000;
    return m;
}

Matrix multiply(const Matrix& a, const Matrix& b) {
    // 20.12 fixed point; each result keeps the integer part of the sum.
    Matrix r;
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            std::int64_t sum = 0;
            for (int k = 0; k < 4; ++k) {
                sum += std::int64_t(a.m[std::size_t(row * 4 + k)]) * b.m[std::size_t(k * 4 + col)];
            }
            r.m[std::size_t(row * 4 + col)] = fx32(sum >> 12);
        }
    }
    return r;
}

int parameterCount(unsigned command) {
    switch (command) {
    case 0x00:
    case MTX_PUSH:
    case MTX_IDENTITY:
    case END_VTXS:
        return 0;
    case MTX_MODE:
    case MTX_POP:
    case MTX_STORE:
    case MTX_RESTORE:
    case COLOR:
    case NORMAL:
    case TEXCOORD:
    case VTX_10:
    case VTX_XY:
    case VTX_XZ:
    case VTX_YZ:
    case VTX_DIFF:
    case POLYGON_ATTR:
    case TEXIMAGE_PARAM:
    case PLTT_BASE:
    case DIF_AMB:
    case SPE_EMI:
    case LIGHT_VECTOR:
    case LIGHT_COLOR:
    case BEGIN_VTXS:
    case SWAP_BUFFERS:
    case VIEWPORT:
    case VEC_TEST:
        return 1;
    case VTX_16:
    case POS_TEST:
        return 2;
    case MTX_SCALE:
    case MTX_TRANS:
    case BOX_TEST:
        return 3;
    case MTX_MULT_3x3:
        return 9;
    case MTX_LOAD_4x3:
    case MTX_MULT_4x3:
        return 12;
    case MTX_LOAD_4x4:
    case MTX_MULT_4x4:
        return 16;
    case SHININESS:
        return 32;
    default:
        return -1;
    }
}

Gpu3D::Gpu3D() : textureVram(512 * 1024), paletteVram(96 * 1024) {
    projection_ = position_ = vector_ = texture_ = projectionSaved_ = textureSaved_ =
        Matrix::identity();
}

Matrix Gpu3D::clipMatrixNow() const {
    return multiply(position_, projection_);
}

Matrix& Gpu3D::target() {
    switch (matrixMode_) {
    case 0:
        return projection_;
    case 3:
        return texture_;
    default:
        return position_;
    }
}

// Multiplies a matrix into the current one(s): new = m * current. In mode 2
// the vector matrix follows the position matrix, except for scaling.
void Gpu3D::apply(const Matrix& m, bool scaleOnly) {
    target() = multiply(m, target());
    if (matrixMode_ == 2 && !scaleOnly) {
        vector_ = multiply(m, vector_);
    }
}

void Gpu3D::packed(std::span<const std::uint32_t> words) {
    // A command word holds up to four command bytes (lowest first); their
    // parameters follow in order. A zero byte is padding.
    std::size_t i = 0;
    while (i < words.size()) {
        const std::uint32_t header = words[i++];
        for (int k = 0; k < 4; ++k) {
            const unsigned command = (header >> (8 * k)) & 0xff;
            if (command == 0) {
                continue;
            }
            const int count = parameterCount(command);
            if (count < 0 || i + std::size_t(count) > words.size()) {
                return;
            }
            this->command(command, words.subspan(i, std::size_t(count)));
            i += std::size_t(count);
        }
    }
}

void Gpu3D::command(unsigned c, std::span<const std::uint32_t> p) {
    auto param = [&](std::size_t i) -> std::uint32_t {
        return i < p.size() ? p[i] : 0;
    };
    auto loadRows = [&](int rows) {
        // `rows` rows of 3 (4x3) or 4 (4x4) values; the rest is identity.
        Matrix m = Matrix::identity();
        const int columns = p.size() >= 16 ? 4 : 3;
        for (int r = 0; r < rows; ++r) {
            for (int col = 0; col < columns; ++col) {
                m.m[std::size_t(r * 4 + col)] = fx32(param(std::size_t(r * columns + col)));
            }
        }
        return m;
    };
    switch (c) {
    case MTX_MODE:
        matrixMode_ = int(param(0) & 3);
        break;
    case MTX_PUSH:
        if (matrixMode_ == 0) {
            projectionSaved_ = projection_;
        } else if (matrixMode_ == 3) {
            textureSaved_ = texture_;
        } else if (stackPointer_ < 31) {
            positionStack_[std::size_t(stackPointer_)] = position_;
            vectorStack_[std::size_t(stackPointer_)] = vector_;
            ++stackPointer_;
        }
        break;
    case MTX_POP:
        if (matrixMode_ == 0) {
            projection_ = projectionSaved_;
        } else if (matrixMode_ == 3) {
            texture_ = textureSaved_;
        } else {
            stackPointer_ = std::clamp(stackPointer_ - signExtend(param(0), 6), 0, 31);
            position_ = positionStack_[std::size_t(stackPointer_)];
            vector_ = vectorStack_[std::size_t(stackPointer_)];
        }
        break;
    case MTX_STORE:
        if (matrixMode_ == 0) {
            projectionSaved_ = projection_;
        } else if (matrixMode_ == 3) {
            textureSaved_ = texture_;
        } else {
            const auto index = std::size_t(param(0) & 31);
            positionStack_[index] = position_;
            vectorStack_[index] = vector_;
        }
        break;
    case MTX_RESTORE:
        if (matrixMode_ == 0) {
            projection_ = projectionSaved_;
        } else if (matrixMode_ == 3) {
            texture_ = textureSaved_;
        } else {
            const auto index = std::size_t(param(0) & 31);
            position_ = positionStack_[index];
            vector_ = vectorStack_[index];
        }
        break;
    case MTX_IDENTITY:
        target() = Matrix::identity();
        if (matrixMode_ == 2) {
            vector_ = Matrix::identity();
        }
        break;
    case MTX_LOAD_4x4:
    case MTX_LOAD_4x3:
        target() = loadRows(4);
        if (matrixMode_ == 2) {
            vector_ = target();
        }
        break;
    case MTX_MULT_4x4:
    case MTX_MULT_4x3:
        apply(loadRows(4), false);
        break;
    case MTX_MULT_3x3:
        apply(loadRows(3), false);
        break;
    case MTX_SCALE: {
        Matrix m = Matrix::identity();
        m.m[0] = fx32(param(0));
        m.m[5] = fx32(param(1));
        m.m[10] = fx32(param(2));
        apply(m, true);
        break;
    }
    case MTX_TRANS: {
        Matrix m = Matrix::identity();
        m.m[12] = fx32(param(0));
        m.m[13] = fx32(param(1));
        m.m[14] = fx32(param(2));
        apply(m, false);
        break;
    }
    case COLOR:
        red_ = component(std::uint16_t(param(0)), 0);
        green_ = component(std::uint16_t(param(0)), 5);
        blue_ = component(std::uint16_t(param(0)), 10);
        break;
    case NORMAL:
        normal(param(0));
        break;
    case TEXCOORD:
        texcoord(param(0));
        break;
    case VTX_16:
        addVertex(signExtend(param(0), 16), signExtend(param(0) >> 16, 16),
                  signExtend(param(1), 16));
        break;
    case VTX_10:
        addVertex(signExtend(param(0), 10) << 6, signExtend(param(0) >> 10, 10) << 6,
                  signExtend(param(0) >> 20, 10) << 6);
        break;
    case VTX_XY:
        addVertex(signExtend(param(0), 16), signExtend(param(0) >> 16, 16), lastZ_);
        break;
    case VTX_XZ:
        addVertex(signExtend(param(0), 16), lastY_, signExtend(param(0) >> 16, 16));
        break;
    case VTX_YZ:
        addVertex(lastX_, signExtend(param(0), 16), signExtend(param(0) >> 16, 16));
        break;
    case VTX_DIFF:
        addVertex(lastX_ + (signExtend(param(0), 10) << 3),
                  lastY_ + (signExtend(param(0) >> 10, 10) << 3),
                  lastZ_ + (signExtend(param(0) >> 20, 10) << 3));
        break;
    case POLYGON_ATTR:
        nextAttributes_ = param(0);
        break;
    case TEXIMAGE_PARAM:
        textureParameters_ = param(0);
        break;
    case PLTT_BASE:
        paletteBase_ = param(0) & 0x1fff;
        break;
    case DIF_AMB:
        diffuse_ = std::uint16_t(param(0) & 0x7fff);
        ambient_ = std::uint16_t((param(0) >> 16) & 0x7fff);
        if (param(0) & 0x8000) {
            red_ = component(diffuse_, 0);
            green_ = component(diffuse_, 5);
            blue_ = component(diffuse_, 10);
        }
        break;
    case SPE_EMI:
        specular_ = std::uint16_t(param(0) & 0x7fff);
        emission_ = std::uint16_t((param(0) >> 16) & 0x7fff);
        useShininessTable_ = (param(0) & 0x8000) != 0;
        break;
    case LIGHT_VECTOR: {
        // A direction (1.9 fixed point per axis), turned by the vector matrix.
        const std::array<float, 3> d{float(signExtend(param(0), 10)) / 512.0f,
                                     float(signExtend(param(0) >> 10, 10)) / 512.0f,
                                     float(signExtend(param(0) >> 20, 10)) / 512.0f};
        lightDirection_[std::size_t(param(0) >> 30)] = turn(d, vector_);
        break;
    }
    case LIGHT_COLOR:
        lightColor_[std::size_t(param(0) >> 30)] = std::uint16_t(param(0) & 0x7fff);
        break;
    case SHININESS:
        for (std::size_t i = 0; i < 32; ++i) {
            for (std::size_t k = 0; k < 4; ++k) {
                shininess_[i * 4 + k] = std::uint8_t(param(i) >> (8 * k));
            }
        }
        break;
    case BEGIN_VTXS:
        primitive_ = int(param(0) & 3);
        attributes_ = nextAttributes_;
        pending_.clear();
        stripPolygons_ = 0;
        open_ = true;
        break;
    case END_VTXS:
        open_ = false;
        pending_.clear();
        break;
    case SWAP_BUFFERS:
        render();
        manualSort_ = (param(0) & 1) != 0;
        wBuffer_ = (param(0) & 2) != 0;
        break;
    case VIEWPORT:
        viewX1_ = int(param(0) & 0xff);
        viewY1_ = int((param(0) >> 8) & 0xff);
        viewX2_ = int((param(0) >> 16) & 0xff);
        viewY2_ = int((param(0) >> 24) & 0xff);
        break;
    default:
        break; // tests (BOX/POS/VEC) and unknown commands have no visible effect here
    }
}

void Gpu3D::texcoord(std::uint32_t v) {
    // Texture coordinates in texels * 16. In "TexCoord" mode they pass
    // through the texture matrix: (s, t, 1/16, 1/16) * matrix.
    rawS_ = signExtend(v, 16);
    rawT_ = signExtend(v >> 16, 16);
    texS_ = rawS_;
    texT_ = rawT_;
    if ((textureParameters_ >> 30) == 1) {
        const auto& m = texture_.m;
        texS_ = std::int32_t(
            (std::int64_t(rawS_) * m[0] + std::int64_t(rawT_) * m[4] + m[8] + m[12]) >> 12);
        texT_ = std::int32_t(
            (std::int64_t(rawS_) * m[1] + std::int64_t(rawT_) * m[5] + m[9] + m[13]) >> 12);
    }
}

void Gpu3D::normal(std::uint32_t v) {
    const std::array<float, 3> n{float(signExtend(v, 10)) / 512.0f,
                                 float(signExtend(v >> 10, 10)) / 512.0f,
                                 float(signExtend(v >> 20, 10)) / 512.0f};
    // "Normal" texture-coordinate mode: the normal through the texture
    // matrix, added to the given coordinates.
    if ((textureParameters_ >> 30) == 2) {
        const auto& m = texture_.m;
        texS_ =
            rawS_ +
            std::int32_t((n[0] * float(m[0]) + n[1] * float(m[4]) + n[2] * float(m[8])) / 256.0f);
        texT_ =
            rawT_ +
            std::int32_t((n[0] * float(m[1]) + n[1] * float(m[5]) + n[2] * float(m[9])) / 256.0f);
    }
    if (!(attributes_ & 15)) {
        return; // no light enabled for this polygon
    }
    // Lighting: emission, plus for each enabled light its ambient part, its
    // diffuse part (by the angle between light and normal) and its specular
    // part (by the angle between the normal and the half-way vector between
    // the light and the line of sight, (0, 0, -1)), squared.
    const auto normal = turn(n, vector_);
    float color[3]{float(component(emission_, 0)), float(component(emission_, 5)),
                   float(component(emission_, 10))};
    for (std::size_t light = 0; light < 4; ++light) {
        if (!(attributes_ & (1u << light))) {
            continue;
        }
        const auto& l = lightDirection_[light];
        const float diffuseLevel = std::max(0.0f, -dot(l, normal));
        const std::array<float, 3> half{l[0] / 2, l[1] / 2, (l[2] - 1) / 2};
        float shine = std::max(0.0f, -dot(half, normal));
        shine = std::min(1.0f, shine * shine);
        if (useShininessTable_) {
            shine = float(shininess_[std::size_t(std::min(127, int(shine * 127)))]) / 255.0f;
        }
        for (int c = 0; c < 3; ++c) {
            const float lightC = float(component(lightColor_[light], 5 * c)) / 31.0f;
            color[c] += float(component(specular_, 5 * c)) * lightC * shine +
                        float(component(diffuse_, 5 * c)) * lightC * diffuseLevel +
                        float(component(ambient_, 5 * c)) * lightC;
        }
    }
    red_ = std::clamp(int(color[0]), 0, 31);
    green_ = std::clamp(int(color[1]), 0, 31);
    blue_ = std::clamp(int(color[2]), 0, 31);
}

void Gpu3D::addVertex(fx32 x, fx32 y, fx32 z) {
    lastX_ = x;
    lastY_ = y;
    lastZ_ = z;
    if (!open_) {
        return;
    }
    const Matrix clip = clipMatrixNow();
    auto row = [&](int c) {
        return fx32((std::int64_t(x) * clip.m[std::size_t(c)] +
                     std::int64_t(y) * clip.m[std::size_t(4 + c)] +
                     std::int64_t(z) * clip.m[std::size_t(8 + c)] +
                     std::int64_t(0x1000) * clip.m[std::size_t(12 + c)]) >>
                    12);
    };
    Vertex vertex{row(0), row(1), row(2), row(3), red_, green_, blue_, texS_, texT_};
    // "Vertex" texture-coordinate mode: the position through the texture matrix.
    if ((textureParameters_ >> 30) == 3) {
        const auto& m = texture_.m;
        vertex.s = rawS_ + std::int32_t((std::int64_t(x) * m[0] + std::int64_t(y) * m[4] +
                                         std::int64_t(z) * m[8]) >>
                                        20);
        vertex.t = rawT_ + std::int32_t((std::int64_t(x) * m[1] + std::int64_t(y) * m[5] +
                                         std::int64_t(z) * m[9]) >>
                                        20);
    }
    pending_.push_back(vertex);
    // Primitive assembly: separate triangles (0), separate quads (1),
    // triangle strips (2; every second triangle reversed to keep the
    // winding) and quad strips (3; vertices 0,1,3,2 then 2,3,5,4, ...).
    switch (primitive_) {
    case 0:
        if (pending_.size() == 3) {
            submit(pending_);
            pending_.clear();
        }
        break;
    case 1:
        if (pending_.size() == 4) {
            submit(pending_);
            pending_.clear();
        }
        break;
    case 2:
        if (pending_.size() == 3) {
            if (stripPolygons_ % 2 == 0) {
                submit(pending_);
            } else {
                submit({pending_[1], pending_[0], pending_[2]});
            }
            ++stripPolygons_;
            pending_.erase(pending_.begin());
        }
        break;
    case 3:
        if (pending_.size() == 4) {
            submit({pending_[0], pending_[1], pending_[3], pending_[2]});
            pending_.erase(pending_.begin(), pending_.begin() + 2);
        }
        break;
    }
}

void Gpu3D::submit(std::vector<Vertex> vertices) {
    polygons_.push_back(
        {std::move(vertices), attributes_, textureParameters_, paletteBase_, wBuffer_});
}

namespace {
// A texture in video memory, as described by TEXIMAGE_PARAM and PLTT_BASE.
class VramTexture final : public TextureSampler {
public:
    VramTexture(const std::vector<std::uint8_t>& texels, const std::vector<std::uint8_t>& palettes,
                std::uint32_t parameters, std::uint32_t paletteBase)
        : texels_(texels), palettes_(palettes), parameters_(parameters), paletteBase_(paletteBase) {
    }

    int format() const { return int((parameters_ >> 26) & 7); }

    Texel sample(int s, int t) const override {
        const int width = 8 << ((parameters_ >> 20) & 7);
        const int height = 8 << ((parameters_ >> 23) & 7);
        s = wrap(s, width, (parameters_ >> 16) & 1, (parameters_ >> 18) & 1);
        t = wrap(t, height, (parameters_ >> 17) & 1, (parameters_ >> 19) & 1);
        const std::size_t base = std::size_t(parameters_ & 0xffff) * 8;
        const std::size_t index = std::size_t(t) * std::size_t(width) + std::size_t(s);
        const bool color0Transparent = (parameters_ >> 29) & 1;
        switch (format()) {
        case 1: { // A3I5: 3-bit alpha, 32 colours
            const int byte = texel(base + index);
            const int a3 = byte >> 5;
            return palette(16, byte & 31, a3 * 4 + a3 / 2);
        }
        case 2: { // 4 colours
            const int index2 = (texel(base + index / 4) >> ((s & 3) * 2)) & 3;
            return color0Transparent && index2 == 0 ? Texel{} : palette(8, index2, 31);
        }
        case 3: { // 16 colours
            const int index4 = (texel(base + index / 2) >> ((s & 1) * 4)) & 15;
            return color0Transparent && index4 == 0 ? Texel{} : palette(16, index4, 31);
        }
        case 4: { // 256 colours
            const int index8 = texel(base + index);
            return color0Transparent && index8 == 0 ? Texel{} : palette(16, index8, 31);
        }
        case 5:
            return compressed(base, s, t, width);
        case 6: { // A5I3: 5-bit alpha, 8 colours
            const int byte = texel(base + index);
            return palette(16, byte & 7, byte >> 3);
        }
        case 7: { // direct colour
            const int c = texel(base + index * 2) | texel(base + index * 2 + 1) << 8;
            return (c & 0x8000) ? color(std::uint16_t(c), 31) : Texel{};
        }
        default:
            return {};
        }
    }

private:
    const std::vector<std::uint8_t>& texels_;
    const std::vector<std::uint8_t>& palettes_;
    std::uint32_t parameters_, paletteBase_;

    static int wrap(int v, int size, bool repeat, bool flip) {
        if (!repeat) {
            return std::clamp(v, 0, size - 1);
        }
        if (!flip) {
            return ((v % size) + size) % size;
        }
        const int period = ((v % (2 * size)) + 2 * size) % (2 * size);
        return period < size ? period : 2 * size - 1 - period;
    }

    int texel(std::size_t at) const { return at < texels_.size() ? texels_[at] : 0; }

    static Texel color(std::uint16_t c, int alpha) {
        return {std::uint8_t(widen(c & 31)), std::uint8_t(widen((c >> 5) & 31)),
                std::uint8_t(widen((c >> 10) & 31)), std::uint8_t(alpha)};
    }

    std::uint16_t paletteColor(std::size_t at) const {
        return at + 1 < palettes_.size() ? std::uint16_t(palettes_[at] | palettes_[at + 1] << 8)
                                         : 0;
    }

    // Palette entry `index`; the palette base counts in units of `unit` bytes.
    Texel palette(std::size_t unit, int index, int alpha) const {
        if (alpha == 0) {
            return {};
        }
        return color(paletteColor(std::size_t(paletteBase_) * unit + std::size_t(index) * 2),
                     alpha);
    }

    // 4x4 compressed textures: each 4x4 block is a 32-bit word of 2-bit
    // texel indices; a 16-bit word per block (stored in the texture slot 1
    // area) selects the block's palette offset and how its four colours
    // are made from the palette.
    Texel compressed(std::size_t base, int s, int t, int width) const {
        const std::size_t block = std::size_t(t / 4) * std::size_t(width / 4) + std::size_t(s / 4);
        const int bits = (texel(base + block * 4 + std::size_t(t & 3)) >> ((s & 3) * 2)) & 3;
        const std::size_t slot = base / 0x20000; // 0 or 2
        const std::size_t infoAt =
            0x20000 + (base & 0x1ffff) / 2 + (slot == 2 ? 0x10000 : 0) + block * 2;
        const int info = texel(infoAt) | texel(infoAt + 1) << 8;
        const std::size_t paletteAt =
            std::size_t(paletteBase_) * 16 + std::size_t(info & 0x3fff) * 4;
        const int mode = info >> 14;
        auto entry = [&](int i) {
            return color(paletteColor(paletteAt + std::size_t(i) * 2), 31);
        };
        auto blend = [](const Texel& a, const Texel& b, int wa, int wb) {
            const int sum = wa + wb;
            return Texel{std::uint8_t((a.r * wa + b.r * wb) / sum),
                         std::uint8_t((a.g * wa + b.g * wb) / sum),
                         std::uint8_t((a.b * wa + b.b * wb) / sum), 31};
        };
        switch (bits) {
        case 0:
        case 1:
            return entry(bits);
        case 2:
            switch (mode) {
            case 1:
                return blend(entry(0), entry(1), 1, 1);
            case 3:
                return blend(entry(0), entry(1), 5, 3);
            default:
                return entry(2);
            }
        default:
            switch (mode) {
            case 2:
                return entry(3);
            case 3:
                return blend(entry(0), entry(1), 3, 5);
            default:
                return {}; // transparent
            }
        }
    }
};

// A clip-space vertex with float attributes, for clipping.
struct ClipVertex {
    float x, y, z, w, r, g, b, s, t;
};

ClipVertex between(const ClipVertex& a, const ClipVertex& b, float u) {
    auto mix = [u](float p, float q) {
        return p + (q - p) * u;
    };
    return {mix(a.x, b.x), mix(a.y, b.y), mix(a.z, b.z), mix(a.w, b.w), mix(a.r, b.r),
            mix(a.g, b.g), mix(a.b, b.b), mix(a.s, b.s), mix(a.t, b.t)};
}

// Keeps the part of a convex polygon where distance(v) >= 0
// (Sutherland-Hodgman clipping against one plane).
template <class Distance>
std::vector<ClipVertex> clipAgainst(const std::vector<ClipVertex>& in, Distance distance) {
    std::vector<ClipVertex> out;
    const std::size_t n = in.size();
    for (std::size_t i = 0; i < n; ++i) {
        const ClipVertex& a = in[i];
        const ClipVertex& b = in[(i + 1) % n];
        const float da = distance(a);
        const float db = distance(b);
        if (da >= 0) {
            out.push_back(a);
        }
        if ((da >= 0) != (db >= 0)) {
            out.push_back(between(a, b, da / (da - db)));
        }
    }
    return out;
}
} // namespace

void Gpu3D::render() {
    RasterSettings settings;
    settings.alphaTest = disp3dcnt & 4;
    settings.alphaBlend = disp3dcnt & 8;
    settings.antialias = disp3dcnt & 0x10;
    settings.edgeMarking = disp3dcnt & 0x20;
    settings.alphaTestReference = alphaTestReference & 31;
    settings.manualTranslucentSort = manualSort_;
    settings.edgeColors = edgeColors;
    settings.toonTable = toonTable;
    settings.clearColor = clearColor;
    settings.clearAlpha = clearAlpha & 31;
    settings.clearId = clearPolygonId & 63;
    settings.clearDepth = std::uint32_t(clearDepth & 0x7fff) * 0x200 + 0x1ff;
    const bool texturing = disp3dcnt & 1;
    const bool highlight = disp3dcnt & 2;

    std::vector<std::unique_ptr<VramTexture>> textures;
    std::vector<RasterPolygon> polygons;
    const float viewWidth = float(viewX2_ - viewX1_ + 1);
    const float viewHeight = float(viewY2_ - viewY1_ + 1);
    for (const auto& polygon : polygons_) {
        const std::uint32_t attributes = polygon.attributes;
        const int mode = int((attributes >> 4) & 3);
        if (mode == 3) {
            continue; // shadow polygons are not modelled
        }
        std::vector<ClipVertex> v;
        for (const auto& p : polygon.vertices) {
            v.push_back({float(p.x), float(p.y), float(p.z), float(p.w), float(p.r), float(p.g),
                         float(p.b), float(p.s) / 16.0f, float(p.t) / 16.0f});
        }
        // Polygons reaching past the far plane are dropped unless their
        // attributes ask for them to be clipped (bit 12).
        const bool beyondFar = std::any_of(v.begin(), v.end(), [](auto& p) { return p.z > p.w; });
        if (beyondFar && !(attributes & (1u << 12))) {
            continue;
        }
        // Clip to the view volume: -w <= x, y, z <= w.
        for (int axis = 0; axis < 3 && !v.empty(); ++axis) {
            auto coordinate = [axis](const ClipVertex& p) {
                return axis == 0 ? p.x : axis == 1 ? p.y : p.z;
            };
            v = clipAgainst(v, [&](const ClipVertex& p) { return p.w - coordinate(p); });
            v = clipAgainst(v, [&](const ClipVertex& p) { return p.w + coordinate(p); });
        }
        if (v.size() < 3) {
            continue;
        }
        // Project to the screen (the viewport's y counts from the bottom).
        RasterPolygon out;
        for (const auto& p : v) {
            if (p.w <= 0) {
                continue;
            }
            RasterVertex r;
            r.x = (p.x / p.w + 1) * 0.5f * viewWidth + float(viewX1_);
            r.y = 192.0f - ((p.y / p.w + 1) * 0.5f * viewHeight + float(viewY1_));
            const float z = std::clamp((p.z / p.w + 1) * 0.5f, 0.0f, 1.0f);
            r.depth = polygon.wBuffer ? std::uint32_t(std::min(p.w, 16777215.0f))
                                      : std::uint32_t(z * 16777215.0f);
            r.w = p.w;
            r.r = p.r;
            r.g = p.g;
            r.b = p.b;
            r.s = p.s;
            r.t = p.t;
            out.vertices.push_back(r);
        }
        if (out.vertices.size() < 3) {
            continue;
        }
        // Culling: attribute bit 6 draws back faces, bit 7 front faces.
        // On screen (y pointing down) front faces wind clockwise.
        float area = 0;
        for (std::size_t i = 0; i < out.vertices.size(); ++i) {
            const auto& a = out.vertices[i];
            const auto& b = out.vertices[(i + 1) % out.vertices.size()];
            area += a.x * b.y - b.x * a.y;
        }
        const bool front = area < 0;
        if ((front && !(attributes & 0x80)) || (!front && !(attributes & 0x40))) {
            continue;
        }
        out.alpha = int((attributes >> 16) & 31);
        out.id = int((attributes >> 24) & 63);
        out.depthEqual = attributes & (1u << 14);
        out.updateDepthWhenTranslucent = attributes & (1u << 11);
        out.wBuffer = polygon.wBuffer;
        out.shading = mode == 0   ? Shading::Modulate
                      : mode == 1 ? Shading::Decal
                      : highlight ? Shading::Highlight
                                  : Shading::Toon;
        int format = 0;
        if (texturing && ((polygon.textureParameters >> 26) & 7) != 0) {
            textures.push_back(std::make_unique<VramTexture>(
                textureVram, paletteVram, polygon.textureParameters, polygon.paletteBase));
            out.texture = textures.back().get();
            format = textures.back()->format();
        }
        out.translucent = (out.alpha > 0 && out.alpha < 31) || format == 1 || format == 6;
        polygons.push_back(std::move(out));
    }

    const auto pixels = rasterize(polygons, settings);
    for (std::size_t i = 0; i < frame_.size(); ++i) {
        frame_[i] = {pixels[i].r, pixels[i].g, pixels[i].b, pixels[i].a};
    }
    lastPolygonCount_ = unsigned(polygons_.size());
    polygons_.clear();
}

} // namespace picarat::nds3d
