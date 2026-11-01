// See Animation.hpp.
#include "Animation.hpp"

#include <map>

namespace captain_toad {

using json::Value;

static Value keyList(std::initializer_list<double> v) {
    Value k = Value::array();
    for (double x : v) {
        k.push(x);
    }
    return k;
}

Value characterAnimation(const Bch& b, const std::string& name) {
    const uint32_t p = b.find(8, name);
    const double duration = b.f32(p + 8);
    const uint32_t a = b.u32(p + 12), n = b.u32(p + 16);
    Value tracks = Value::object();
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t q = b.u32(a + i * 4), type = b.u32(q + 4), flags = b.u32(q + 8);
        if (((type >> 16) & 15) != 4) {
            throw ImportError("Unsupported skeletal segment");
        }
        Value channels = Value::array();
        for (int j = 0; j < 9; ++j) {
            if (flags & (1u << (16 + j))) {
                channels.push(nullptr);
                continue;
            }
            if (flags & (1u << (6 + j + (j >= 6 ? 1 : 0)))) {
                Value c = Value::object();
                c["constant"] = b.f32(q + 12 + size_t(j) * 4);
                channels.push(c);
                continue;
            }
            const uint32_t cp = b.u32(q + 12 + size_t(j) * 4);
            const double start = b.f32(cp), end = b.f32(cp + 4);
            const uint32_t seg = b.u32(cp + 12);
            const double vs = b.f32(cp + 16), vo = b.f32(cp + 20), fs = b.f32(cp + 24), inv = b.f32(cp + 28);
            const uint32_t data = b.u32(cp + 32);
            if ((seg & 255) != 2) {
                // Step / linear curves (e.g. the Mini clips): the general decoder with its
                // interpolation (the channel loader evaluates 0 step, 1 linear).
                channels.push(curve(b, cp));
                continue;
            }
            Value keys = Value::array();
            std::vector<double> frames;
            if (((seg >> 8) & 255) == 3) {
                if (!(vs == 1.0 && vo == 0.0 && fs == 1.0)) {
                    throw ImportError("Unexpected quantized Hermite96 curve");
                }
                for (uint32_t k = 0; k < (seg >> 16); ++k) {
                    const size_t at = data + k * 12;
                    keys.push(keyList({b.f32(at), b.f32(at + 4), b.f32(at + 8)}));
                    frames.push_back(b.f32(at));
                }
            } else if (((seg >> 8) & 255) == 1) {
                // Quantized 64-bit keys: frame 12 bits | value 20 bits, then two zero 16-bit slopes.
                for (uint32_t k = 0; k < (seg >> 16); ++k) {
                    // The two signed 16-bit slopes (in, out) / 256 (as the other 16-bit slopes),
                    // in value units: scaled like the value.
                    const uint32_t w0 = b.u32(data + k * 8);
                    const double in = b.s16(data + k * 8 + 4) / 256.0 * vs, out = b.s16(data + k * 8 + 6) / 256.0 * vs;
                    const double f = double(w0 & 0xfff) * fs;
                    if (in != out) {
                        keys.push(keyList({f, vo + double(w0 >> 12) * vs, in, out}));
                    } else {
                        keys.push(keyList({f, vo + double(w0 >> 12) * vs, in}));
                    }
                    frames.push_back(f);
                }
            } else {
                // Other key formats (e.g. the carry clips): the general curve decoder.
                const Value general = curve(b, cp);
                for (const auto& key : general.at("keys").items()) {
                    keys.push(key);
                    frames.push_back(key[0].num());
                }
            }
            for (size_t k = 1; k < frames.size(); ++k) {
                if (frames[k - 1] > frames[k]) {
                    throw ImportError("Unordered curve keys");
                }
            }
            Value c = Value::object();
            c["source_offset"] = cp;
            c["start"] = start;
            c["end"] = end;
            c["inverse_duration"] = inv;
            c["keys"] = keys;
            channels.push(c);
        }
        tracks[b.nameOf(q)] = channels;
    }
    Value out = Value::object();
    out["name"] = name;
    out["source_offset"] = p;
    out["frames"] = duration;
    out["loop"] = bool(b.u32(p + 4) & 1);
    out["tracks"] = tracks;
    return out;
}

Value visibility(const Bch& b, const std::vector<std::string>& names) {
    Value out = Value::object();
    for (const auto& name : names) {
        const uint32_t p = b.find(10, name);
        const uint32_t a = b.u32(p + 12), n = b.u32(p + 16);
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t q = b.u32(a + i * 4);
            if (((b.u32(q + 4) >> 16) & 15) != 8) {
                throw ImportError("Visibility segment");
            }
            const uint32_t cp = b.u32(q + 20), count = b.u32(q + 24);
            Value bits = Value::array();
            for (uint32_t f = 0; f < count; ++f) {
                bits.push(bool(b.u8(cp + f / 8) & (1 << (f % 8))));
            }
            out[b.nameOf(q)] = bits;
        }
    }
    return out;
}

Value curve(const Bch& b, size_t cp) {
    const double start = b.f32(cp), end = b.f32(cp + 4);
    const uint32_t seg = b.u32(cp + 12);
    const double vs = b.f32(cp + 16), vo = b.f32(cp + 20), fs = b.f32(cp + 24);
    const uint32_t ptr = b.u32(cp + 32);
    const uint32_t interp = seg & 255, quant = (seg >> 8) & 255, count = seg >> 16;
    Value keys = Value::array();
    for (uint32_t k = 0; k < count; ++k) {
        double f, v, s;
        if (quant == 0) {   // Hermite128: frame, value, in slope, out slope
            f = b.f32(ptr + 16 * k);
            v = b.f32(ptr + 16 * k + 4);
            s = b.f32(ptr + 16 * k + 8);
            const double o = b.f32(ptr + 16 * k + 12);
            if (o != s) {
                keys.push(keyList({f * fs, v * vs + vo, s * vs, o * vs}));
                continue;
            }
        } else if (quant == 3) {   // UnifiedHermite96
            f = b.f32(ptr + 12 * k);
            v = b.f32(ptr + 12 * k + 4);
            s = b.f32(ptr + 12 * k + 8);
        } else if (quant == 1) {   // Hermite64 with zero slopes
            const uint32_t w = b.u32(ptr + 8 * k);
            f = double(w & 0xfff);
            v = double(w >> 12);
            s = b.s16(ptr + 8 * k + 4) / 256.0;
            const double o = b.s16(ptr + 8 * k + 6) / 256.0;
            if (o != s) {
                keys.push(keyList({f * fs, v * vs + vo, s * vs, o * vs}));
            } else {
                keys.push(keyList({f * fs, v * vs + vo, s * vs}));
            }
            continue;
        } else if (quant == 6) {   // StepLinear64
            f = b.f32(ptr + 8 * k);
            v = b.f32(ptr + 8 * k + 4);
            s = 0.0;
        } else if (quant == 7) {   // StepLinear32
            const uint32_t w = b.u32(ptr + 4 * k);
            f = double(w & 0xfff);
            v = double(w >> 12);
            s = 0.0;
        } else if (quant == 2) {   // Hermite48: u8 frame, u16 value, two signed 12-bit slopes / 32
            f = b.u8(ptr + 6 * k) + start;
            v = b.u16(ptr + 6 * k + 1);
            const uint32_t s0 = b.u8(ptr + 6 * k + 3), s1 = b.u8(ptr + 6 * k + 4), s2 = b.u8(ptr + 6 * k + 5);
            auto signed12 = [](uint32_t x) {
                return double(int32_t(x << 20) >> 20);
            };
            const double in = signed12(s0 | ((s1 & 15) << 8)) / 32, out = signed12((s1 >> 4) | (s2 << 4)) / 32;
            keys.push(keyList({f * fs, v * vs + vo, in, out}));
            continue;
        } else if (quant == 4) {   // UnifiedHermite48: u16 frame / 32, u16 value, s16 slope / 256
            f = b.u16(ptr + 6 * k) / 32.0 + start;
            v = b.u16(ptr + 6 * k + 2);
            s = b.s16(ptr + 6 * k + 4) / 256.0;
        } else if (quant == 5) {   // UnifiedHermite32: u8 frame, 12-bit value, signed 12-bit slope / 32
            const uint32_t w = b.u32(ptr + 4 * k);
            f = double(w & 255) + start;
            v = double((w >> 8) & 0xfff);
            s = double(int32_t((w >> 20) << 20) >> 20) / 32;
        } else {
            throw ImportError("Unsupported curve quantization");
        }
        keys.push(keyList({f * fs, v * vs + vo, s}));
    }
    Value out = Value::object();
    out["source_offset"] = cp;
    out["start"] = start;
    out["end"] = end;
    out["interpolation"] = interp;
    out["keys"] = keys;
    return out;
}

Value actorAnimation(const Bch& b, const std::string& name) {
    const uint32_t p = b.find(8, name);
    const double duration = b.f32(p + 8);
    const uint32_t a = b.u32(p + 12), n = b.u32(p + 16);
    Value tracks = Value::object(), matrices = Value::object();
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t q = b.u32(a + i * 4), type = b.u32(q + 4), flags = b.u32(q + 8), kind = (type >> 16) & 15;
        if (kind == 4) {
            Value channels = Value::array();
            for (int j = 0; j < 9; ++j) {
                if (flags & (1u << (16 + j))) {
                    channels.push(nullptr);
                    continue;
                }
                if (flags & (1u << (6 + j + (j >= 6 ? 1 : 0)))) {
                    Value c = Value::object();
                    c["constant"] = b.f32(q + 12 + size_t(j) * 4);
                    channels.push(c);
                    continue;
                }
                channels.push(curve(b, b.u32(q + 12 + size_t(j) * 4)));
            }
            tracks[b.nameOf(q)] = channels;
        } else if (kind == 9) {
            const uint32_t ptr = b.u32(q + 20), count = b.u32(q + 24);
            Value frames = Value::array();
            for (uint32_t f = 0; f < count; ++f) {
                Value m = Value::array();
                for (int k = 0; k < 12; ++k) {
                    m.push(b.f32(ptr + 48 * f + size_t(k) * 4));
                }
                frames.push(m);
            }
            matrices[b.nameOf(q)] = frames;
        } else {
            throw ImportError("Unsupported skeletal segment");
        }
    }
    Value out = Value::object();
    out["name"] = name;
    out["source_offset"] = p;
    out["frames"] = duration;
    out["loop"] = bool(b.u32(p + 4) & 1);
    out["tracks"] = tracks;
    out["matrices"] = matrices;
    return out;
}

Value materialAnimation(const Bch& b, const std::string& name) {
    static const std::map<int, const char*> targets = {
        {1, "constant0"},        {2, "constant1"},        {3, "constant2"},    {4, "constant3"},
        {5, "constant4"},        {6, "constant5"},        {7, "emission"},     {8, "ambient"},
        {9, "diffuse"},          {10, "specular0"},       {11, "specular1"},   {13, "mapper0_texture"},
        {15, "mapper1_texture"}, {17, "mapper2_texture"}, {18, "blend_color"}, {19, "tc0_scale"},
        {20, "tc0_rot"},         {21, "tc0_trans"},       {22, "tc1_scale"},   {23, "tc1_rot"},
        {24, "tc1_trans"},       {25, "tc2_scale"},       {26, "tc2_rot"},     {27, "tc2_trans"}};
    // Primitive kinds: 0 float, 2 vector2, 3 vector3, 5 colour, 6 texture (an index into the
    // animation's texture name list).
    static const std::map<int, int> axesCount = {{0, 1}, {2, 2}, {3, 3}, {5, 4}, {6, 1}};
    const uint32_t p = b.find(9, name);
    const double duration = b.f32(p + 8);
    const uint32_t a = b.u32(p + 12), n = b.u32(p + 16);
    Value elements = Value::array();
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t q = b.u32(a + i * 4);
        const int target = b.u16(q + 4), primitive = b.u16(q + 6);
        auto t = targets.find(target);
        auto ac = axesCount.find(primitive);
        if (t == targets.end() || ac == axesCount.end()) {
            throw ImportError("Unsupported material animation target");
        }
        const uint32_t flags = b.u32(q + 8);
        Value axes = Value::array();
        for (int axis = 0; axis < ac->second; ++axis) {
            const size_t o = q + 12 + 4 * size_t(axis);
            if (flags & (1u << (8 + axis))) {
                axes.push(nullptr);
                continue;
            }
            if (flags & (1u << axis)) {
                Value c = Value::object();
                c["constant"] = b.f32(o);
                axes.push(c);
            } else {
                axes.push(curve(b, b.u32(o)));
            }
        }
        Value e = Value::object();
        e["material"] = b.nameOf(q);
        e["target"] = t->second;
        e["axes"] = axes;
        elements.push(e);
    }
    Value out = Value::object();
    out["name"] = name;
    out["frames"] = duration;
    out["loop"] = bool(b.u32(p + 4) & 1);
    out["elements"] = elements;
    out["source_offset"] = p;
    // Texture pattern animations (.cmtpa): the texture names their indices select (header +24/+28).
    Value textures = Value::array();
    for (uint32_t i = 0, list = b.u32(p + 24), count = b.u32(p + 28); list && i < count && i < 256; ++i) {
        textures.push(b.nameOf(list + 4 * i));
    }
    out["textures"] = textures;
    return out;
}

std::vector<uint32_t> boneFlags(const Bch& b, size_t index) {
    const uint32_t p = b.table(0).at(index), ptr = b.u32(p + 112), n = b.u32(p + 116);
    std::vector<uint32_t> out;
    for (uint32_t i = 0; i < n; ++i) {
        out.push_back(b.u32(ptr + i * 100));
    }
    return out;
}

}   // namespace captain_toad
