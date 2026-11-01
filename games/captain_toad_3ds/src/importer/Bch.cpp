// See Bch.hpp.
#include "Bch.hpp"

#include <cmath>

namespace captain_toad {

std::optional<int> Material::shadowMapMode() const {
    if (const json::Value* v = metadata.find("$shadowmap"); v && v->size()) {
        return int((*v)[0].integer());
    }
    return std::nullopt;
}

const Vec4d& Mesh::uniform(int i) const {
    auto it = commands.uniforms.find(i);
    if (it == commands.uniforms.end()) {
        throw ImportError("Missing mesh uniform");
    }
    return it->second;
}

Bch::Bch(Bytes raw) : b_(std::move(raw)) {
    if (b_.size() < 0x40 || std::memcmp(b_.data(), "BCH\0", 4) != 0 || b_[4] != 0x22) {
        throw ImportError("Only BCH 0x22 is supported");
    }
    main_ = u32(8);
    strings_ = u32(12);
    gpu_ = u32(16);
    data_ = u32(20);
    ext_ = u32(24);
    rel_ = u32(28);
    const uint32_t length = u32(0x34);
    checkRange(b_, rel_, length, "BCH relocation table");
    for (uint32_t p = rel_; p < rel_ + length; p += 4) {
        const uint32_t entry = u32(p), kind = entry >> 25, off = entry & 0x1ffffff;
        const size_t loc = kind == 1 ? main_ + off : size_t(kind < 0x20 ? main_ : gpu_) + size_t(off) * 4;
        const uint32_t value = u32(loc);
        uint64_t relocated;
        switch (kind) {
        case 0:
            relocated = uint64_t(value) + main_;
            break;
        case 1:
            relocated = uint64_t(value) + strings_;
            break;
        case 2:
        case 3:
            relocated = uint64_t(value) + gpu_;
            break;
        case 7:
        case 12:
            relocated = uint64_t(value) + data_;
            break;
        case 0x25:
        case 0x26:
        case 0x27:
        case 0x28:
        case 0x2a:   // extended-section texture data (paired with 0x25 as 0x2b-0x2d with 0x26-0x28)
        case 0x2b:
        case 0x2c:
        case 0x2d:
            relocated = (uint64_t(value) + (kind >= 0x2a ? ext_ : data_)) & 0x7fffffff;
            if (kind == 0x27 || kind == 0x2c) {
                relocated |= 0x80000000u;
            }
            break;
        case 0x2e:
            relocated = 0;
            break;   // PICA base register; file addresses use base zero
        default:
            throw ImportError("Unsupported BCH relocation " + std::to_string(kind));
        }
        if (relocated > 0xffffffffu) {
            throw ImportError("BCH relocation overflow");
        }
        const uint32_t v = uint32_t(relocated);
        checkRange(b_, loc, 4);
        std::memcpy(b_.data() + loc, &v, 4);
    }
}

std::optional<std::string> Bch::string(size_t p) const {
    if (p == 0) {
        return std::nullopt;
    }
    checkRange(b_, p, 1, "BCH name");
    const size_t limit = std::min(b_.size(), p + 4096);
    for (size_t e = p; e < limit; ++e) {
        if (b_[e] == 0) {
            return std::string(reinterpret_cast<const char*>(b_.data() + p), e - p);
        }
    }
    throw ImportError("Unterminated BCH name");
}

std::string Bch::nameOf(size_t p) const {
    auto n = name(p);
    if (!n) {
        throw ImportError("Missing BCH name");
    }
    return *n;
}

std::vector<uint32_t> Bch::table(int slot) const {
    const size_t at = main_ + size_t(slot) * 12;
    const uint32_t p = u32(at), n = u32(at + 4);
    if (n > 100000) {
        throw ImportError("Unreasonable BCH table");
    }
    std::vector<uint32_t> out;
    for (uint32_t i = 0; i < n; ++i) {
        out.push_back(u32(p + i * 4));
    }
    return out;
}

uint32_t Bch::find(int slot, const std::string& n) const {
    for (uint32_t p : table(slot)) {
        if (name(p) == n) {
            return p;
        }
    }
    throw ImportError("BCH lacks " + n);
}

namespace {
double f24(uint32_t v) {
    const double sign = (v & 0x800000) ? -1.0 : 1.0;
    const int exponent = (v >> 16) & 127;
    const double mantissa = double(v & 65535);
    if (exponent == 0) {
        return sign * std::ldexp(mantissa / 65536, -62);
    }
    if (exponent == 127) {
        throw ImportError("Nonfinite fixed attribute");
    }
    return sign * std::ldexp(1 + mantissa / 65536, exponent - 63);
}
}   // namespace

CommandState Bch::commands(size_t p, size_t n) const {
    std::vector<uint32_t> words(n);
    for (size_t k = 0; k < n; ++k) {
        words[k] = u32(p + 4 * k);
    }
    CommandState out;
    uint32_t config = 0;
    std::vector<uint32_t> pending, fixedWords;
    int fixedIndex = 0;
    size_t i = 0;
    while (i < words.size()) {
        if (i + 1 >= words.size()) {
            throw ImportError("Truncated PICA command");
        }
        const uint32_t first = words[i], h = words[i + 1];
        i += 2;
        const uint32_t reg = h & 65535;
        uint32_t mask = 0;
        for (int k = 0; k < 4; ++k) {
            if (h & (1u << (16 + k))) {
                mask |= 255u << (8 * k);
            }
        }
        const size_t extra = (h >> 20) & 2047;
        if (i + extra > words.size()) {
            throw ImportError("Truncated PICA command parameters");
        }
        std::vector<uint32_t> values{first};
        values.insert(values.end(), words.begin() + long(i), words.begin() + long(i + extra));
        i += extra;
        for (size_t j = 0; j < values.size(); ++j) {
            const uint32_t rid = (h >> 31) ? reg + uint32_t(j) : reg;
            const uint32_t value = (out.regs.count(rid) ? out.regs[rid] & ~mask : 0) | (values[j] & mask);
            out.regs[rid] = value;
            if (rid == 0x232) {
                fixedIndex = value & 15;
                fixedWords.clear();
            } else if (rid >= 0x233 && rid <= 0x235) {
                fixedWords.push_back(value);
                if (fixedWords.size() == 3) {
                    const unsigned __int128 packed =
                        (unsigned __int128)fixedWords[0] << 64 | (unsigned __int128)fixedWords[1] << 32 | fixedWords[2];
                    Vec4d a;
                    for (int k = 0; k < 4; ++k) {
                        a[size_t(k)] = f24(uint32_t(packed >> (24 * k)) & 0xffffff);
                    }
                    out.fixedAttributes[fixedIndex++] = a;
                    fixedWords.clear();
                }
            }
            if (rid == 0x2c0) {
                if (!pending.empty()) {
                    throw ImportError("Partial uniform");
                }
                config = value;
            } else if (rid >= 0x2c1 && rid <= 0x2c8) {
                if (!(config >> 31)) {
                    throw ImportError("24-bit uniform not supported");
                }
                pending.push_back(value);
                if (pending.size() == 4) {
                    Vec4d u;
                    for (int k = 0; k < 4; ++k) {
                        float f;
                        std::memcpy(&f, &pending[size_t(3 - k)], 4);
                        u[size_t(k)] = f;
                    }
                    out.uniforms[int(config & 127)] = u;
                    config = (config & ~127u) | ((config + 1) & 127);
                    pending.clear();
                }
            }
        }
        if (i & 1) {
            ++i;
        }
    }
    if (!pending.empty()) {
        throw ImportError("Partial uniform at end");
    }
    return out;
}

json::Value Bch::metadata(size_t p) const {
    json::Value result = json::Value::object();
    if (!p) {
        return result;
    }
    const uint32_t entries = u32(p), n = u32(p + 4);
    for (uint32_t i = 0; i < n; ++i) {
        const size_t e = entries + i * 12;
        const uint32_t nameOff = u32(e), data = u32(e + 8);
        const uint16_t kind = u16(e + 4), count = u16(e + 6);
        // Value types (H3D user data, public format documentation): 0 integers, 1 floats,
        // 2 strings; other types (UTF-16 text, boxes, vertex data) are kept as a null entry.
        json::Value list = json::Value::array();
        for (uint16_t k = 0; k < count; ++k) {
            if (kind == 0) {
                list.push(int(captain_toad::s32(b_, data + 4 * k)));
            } else if (kind == 1) {
                list.push(double(captain_toad::f32(b_, data + 4 * k)));
            } else if (kind == 2) {
                const auto text = name(data + 4 * k);
                list.push(text ? json::Value(*text) : json::Value());
            } else {
                list.push(json::Value());
            }
        }
        result[*string(nameOff)] = list;
    }
    return result;
}

std::vector<Sampler> Bch::samplers(size_t p) const {
    if (!p) {
        throw ImportError("Missing texture mapper records");
    }
    std::vector<Sampler> out;
    for (int j = 0; j < 3; ++j) {
        const size_t q = p + size_t(j) * 16;
        checkRange(b_, q, 16);
        if (b_[q + 1] > 3 || b_[q + 2] > 3) {
            throw ImportError("Unsupported texture wrap");
        }
        out.push_back({uint32_t(q),
                       b_[q + 1],
                       b_[q + 2],
                       b_[q + 3],
                       b_[q + 4],
                       f32(q + 8),
                       {b_[q + 12], b_[q + 13], b_[q + 14], b_[q + 15]}});
    }
    return out;
}

std::vector<TextureRecord> Bch::textureRecords() const {
    std::vector<TextureRecord> out;
    for (uint32_t p : table(3)) {
        const Registers r = commands(u32(p), u32(p + 4)).regs;
        out.push_back({nameOf(p + 28), r.at(0x85), r.at(0x82) >> 16, r.at(0x82) & 65535, r.at(0x8e), r});
    }
    return out;
}

std::vector<Model> Bch::models() const {
    std::vector<Model> models;
    for (uint32_t p : table(0)) {
        Model model;
        const uint32_t materialPtr = u32(p + 52), materialN = u32(p + 56), meshPtr = u32(p + 64), meshN = u32(p + 68);
        const uint32_t bonePtr = u32(p + 112), boneN = u32(p + 116), visibility = u32(p + 124);
        for (uint32_t i = 0; i < materialN; ++i) {
            const size_t q = materialPtr + i * 44;
            const uint32_t param = u32(q);
            Material m;
            m.name = nameOf(q + 40);
            for (int j = 0; j < 3; ++j) {
                m.textures[size_t(j)] = name(q + 28 + size_t(j) * 4);
            }
            m.samplers = samplers(u32(q + 24));
            m.parameterOffset = param;
            m.metadata = metadata(u32(param + 268));
            m.registers = commands(u32(q + 16), u32(q + 20)).regs;
            const CommandState fragment = commands(u32(param + 200), u32(param + 204));
            m.fragment = fragment.regs;
            if (auto c10 = fragment.uniforms.find(10); c10 != fragment.uniforms.end()) {
                for (int k = 0; k < 3; ++k) {
                    m.uvSet[size_t(k)] = int(std::lround(c10->second[size_t(k)]));
                }
            }
            m.flags = u16(param + 4);
            m.polygonOffset = f32(param + 196);
            for (int j = 0; j < 3; ++j) {
                const size_t t = param + 12 + size_t(j) * 24;
                m.transforms[size_t(j)].flags = u32(t);
                for (int k = 0; k < 5; ++k) {
                    m.transforms[size_t(j)].v[size_t(k)] = f32(t + 4 + size_t(k) * 4);
                }
            }
            for (int k = 0; k < 48; ++k) {
                m.colors[size_t(k)] = u8(param + 88 + size_t(k));
            }
            for (int st = 0; st < 6; ++st) {
                m.constantAssignment[size_t(st)] = (u8(param + 192 + size_t(st / 2)) >> (4 * (st & 1))) & 15;
            }
            model.materials.push_back(std::move(m));
        }
        for (uint32_t i = 0; i < boneN; ++i) {
            const size_t q = bonePtr + i * 100;
            Bone b;
            b.name = nameOf(q + 92);
            b.parent = s16(q + 4);
            b.flags = u32(q);
            for (int k = 0; k < 3; ++k) {
                b.scale[size_t(k)] = f32(q + 8 + size_t(k) * 4);
                b.rotation[size_t(k)] = f32(q + 20 + size_t(k) * 4);
                b.translation[size_t(k)] = f32(q + 32 + size_t(k) * 4);
            }
            for (int k = 0; k < 12; ++k) {
                b.inverseBind[size_t(k)] = f32(q + 44 + size_t(k) * 4);
            }
            model.bones.push_back(std::move(b));
        }
        for (uint32_t i = 0; i < meshN; ++i) {
            const size_t q = meshPtr + i * 56;
            Mesh mesh;
            mesh.material = u16(q);
            mesh.flags = u16(q + 2);
            mesh.node = u16(q + 4);
            mesh.priority = u16(q + 6);
            mesh.commandOffset = u32(q + 8);
            mesh.commands = commands(u32(q + 8), u32(q + 12));
            const uint32_t facePtr = u32(q + 16), faceN = u32(q + 20);
            if (faceN == 0) {
                throw ImportError("Face-less mesh");
            }
            uint32_t maxIndex = 0;
            for (uint32_t j = 0; j < faceN; ++j) {
                const size_t f = facePtr + j * 52;
                Face face;
                face.skin = u16(f);
                const uint16_t bn = u16(f + 2);
                if (bn > 20) {
                    throw ImportError("Bone palette too large");
                }
                for (uint16_t k = 0; k < bn; ++k) {
                    face.palette.push_back(u16(f + 4 + size_t(k) * 2));
                }
                face.registers = commands(u32(f + 44), u32(f + 48)).regs;
                const uint32_t count = face.registers.at(0x228), idx = face.registers.at(0x227);
                for (uint32_t k = 0; k < count; ++k) {
                    face.indices.push_back(idx >> 31 ? u16((idx & 0x7fffffff) + k * 2) : u8((idx & 0x7fffffff) + k));
                }
                if (count % 3) {
                    throw ImportError("Non-triangle index count");
                }
                for (uint32_t x : face.indices) {
                    maxIndex = std::max(maxIndex, x);
                }
                mesh.faces.push_back(std::move(face));
            }
            const Registers& regs = mesh.commands.regs;
            const uint32_t stride = (regs.at(0x205) >> 16) & 255, total = regs.at(0x205) >> 28;
            const uint64_t fmt = regs.at(0x201) | uint64_t(regs.at(0x202)) << 32;
            const uint64_t perm = regs.at(0x204) | uint64_t(regs.at(0x205) & 65535) << 32;
            const uint64_t semantic = regs.at(0x2bb) | uint64_t(regs.at(0x2bc)) << 32;
            struct Attribute {
                int semantic, type, count;
                uint32_t offset;
            };
            std::vector<Attribute> attrs;
            uint32_t off = 0;
            for (uint32_t a = 0; a < total; ++a) {
                const int slot = int((perm >> (a * 4)) & 15), form = int((fmt >> (slot * 4)) & 15);
                const int type = form & 3, num = (form >> 2) + 1;
                const uint32_t size = type == 3 ? 4 : type == 2 ? 2 : 1;
                off = (off + size - 1) / size * size;
                attrs.push_back({int((semantic >> (slot * 4)) & 15), type, num, off});
                off += size * uint32_t(num);
            }
            if (off > stride) {
                throw ImportError("Attribute stride overflow");
            }
            const uint32_t base = regs.at(0x203);
            checkRange(b_, base, size_t(maxIndex + 1) * stride, "vertex buffer");
            for (int u : {6, 7, 8}) {
                mesh.uniform(u);
            }
            for (uint32_t v = 0; v <= maxIndex; ++v) {
                Vertex vx;
                for (const auto& [slot, value] : mesh.commands.fixedAttributes) {
                    if (!((fmt >> (48 + slot)) & 1)) {
                        continue;
                    }
                    const size_t s = (semantic >> (slot * 4)) & 15;
                    vx.v[s] = value;
                    vx.n[s] = 4;
                }
                for (const auto& a : attrs) {
                    const size_t at = base + size_t(v) * stride + a.offset, s = size_t(a.semantic);
                    for (int k = 0; k < a.count; ++k) {
                        double x;
                        switch (a.type) {
                        case 0:
                            x = int8_t(u8(at + size_t(k)));
                            break;
                        case 1:
                            x = u8(at + size_t(k));
                            break;
                        case 2:
                            x = s16(at + size_t(k) * 2);
                            break;
                        default:
                            x = f32(at + size_t(k) * 4);
                            break;
                        }
                        vx.v[s][size_t(k)] = x;
                    }
                    vx.n[s] = uint8_t(a.count);
                }
                mesh.vertices.push_back(vx);
            }
            mesh.visible = (u32(visibility + (mesh.node / 32) * 4) & (1u << (mesh.node % 32))) != 0;
            model.meshes.push_back(std::move(mesh));
        }
        model.name = nameOf(p + 132);
        for (int k = 0; k < 12; ++k) {
            model.transform[size_t(k)] = f32(p + 4 + size_t(k) * 4);
        }
        models.push_back(std::move(model));
    }
    return models;
}

json::Value registersJson(const Registers& r) {
    json::Value o = json::Value::object();
    for (const auto& [k, v] : r) {
        o[std::to_string(k)] = v;
    }
    return o;
}

json::Value samplersJson(const std::vector<Sampler>& samplers) {
    json::Value list = json::Value::array();
    for (const auto& s : samplers) {
        json::Value o = json::Value::object();
        o["source_offset"] = s.sourceOffset;
        o["wrap_s"] = s.wrapS;
        o["wrap_t"] = s.wrapT;
        o["mag_filter"] = s.magFilter;
        o["min_filter"] = s.minFilter;
        o["lod_bias"] = s.lodBias;
        o["border"] = json::Value::list(std::vector<int>(s.border.begin(), s.border.end()));
        list.push(o);
    }
    return list;
}

json::Value toJson(const Material& m) {
    json::Value o = json::Value::object();
    o["name"] = m.name;
    json::Value textures = json::Value::array();
    for (const auto& t : m.textures) {
        textures.push(t ? json::Value(*t) : json::Value());
    }
    o["textures"] = textures;
    o["texture_mappers"] = samplersJson(m.samplers);
    o["parameter_offset"] = m.parameterOffset;
    o["metadata"] = m.metadata;
    o["registers"] = registersJson(m.registers);
    o["fragment_registers"] = registersJson(m.fragment);
    o["flags"] = m.flags;
    o["polygon_offset"] = m.polygonOffset;
    json::Value transforms = json::Value::array();
    for (const auto& t : m.transforms) {
        json::Value x = json::Value::array();
        x.push(t.flags);
        for (double v : t.v) {
            x.push(v);
        }
        transforms.push(x);
    }
    o["texture_transforms"] = transforms;
    o["colors"] = json::Value::list(std::vector<int>(m.colors.begin(), m.colors.end()));
    return o;
}

json::Value toJson(const Bone& b) {
    json::Value o = json::Value::object();
    o["name"] = b.name;
    o["parent"] = b.parent;
    o["scale"] = json::Value::list(std::vector<double>(b.scale.begin(), b.scale.end()));
    o["rotation"] = json::Value::list(std::vector<double>(b.rotation.begin(), b.rotation.end()));
    o["translation"] = json::Value::list(std::vector<double>(b.translation.begin(), b.translation.end()));
    o["inverse_bind"] = json::Value::list(std::vector<double>(b.inverseBind.begin(), b.inverseBind.end()));
    return o;
}

json::Value toJson(const TextureRecord& t, bool withRegisters) {
    json::Value o = json::Value::object();
    o["name"] = t.name;
    o["offset"] = t.offset;
    o["width"] = t.width;
    o["height"] = t.height;
    o["format"] = t.format;
    o["registers"] = withRegisters ? registersJson(t.registers) : json::Value();
    return o;
}

}   // namespace captain_toad
