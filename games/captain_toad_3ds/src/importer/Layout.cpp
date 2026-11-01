// See Layout.hpp.
#include "Layout.hpp"

#include <cstdio>
#include <vector>

namespace captain_toad {

using json::Value;

namespace {

// Fixed-size field: text up to the first NUL.
std::string fieldString(const Bytes& b, size_t o, size_t n) {
    checkRange(b, o, n);
    std::string s(reinterpret_cast<const char*>(b.data() + o), n);
    return s.substr(0, s.find('\0'));
}

std::string hexBytes(const Bytes& b, size_t o, size_t n) {
    checkRange(b, o, n);
    static const char* hex = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; ++i) {
        s += hex[b[o + i] >> 4];
        s += hex[b[o + i] & 15];
    }
    return s;
}

std::string hexNumber(uint32_t v) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "0x%x", v);
    return buf;
}

Value color(const Bytes& b, size_t o) {
    Value c = Value::array();
    for (int i = 0; i < 4; ++i) {
        c.push(u8(b, o + size_t(i)));
    }
    return c;
}

Value bytesList(const Bytes& b, size_t o, size_t n) {
    Value c = Value::array();
    for (size_t i = 0; i < n; ++i) {
        c.push(u8(b, o + i));
    }
    return c;
}

Value floats(const Bytes& b, size_t o, int n) {
    Value v = Value::array();
    for (int i = 0; i < n; ++i) {
        v.push(double(f32(b, o + size_t(i) * 4)));
    }
    return v;
}

std::string utf16le(const Bytes& b, size_t o, size_t n) {
    checkRange(b, o, n);
    std::string out;
    for (size_t i = 0; i + 1 < n; i += 2) {
        uint32_t c = b[o + i] | b[o + i + 1] << 8;
        if (c >= 0xd800 && c < 0xdc00 && i + 3 < n) {
            const uint32_t lo = b[o + i + 2] | b[o + i + 3] << 8;
            c = 0x10000 + ((c - 0xd800) << 10) + (lo - 0xdc00);
            i += 2;
        }
        if (c < 0x80) {
            out += char(c);
        } else if (c < 0x800) {
            out += char(0xc0 | c >> 6);
            out += char(0x80 | (c & 63));
        } else if (c < 0x10000) {
            out += char(0xe0 | c >> 12);
            out += char(0x80 | ((c >> 6) & 63));
            out += char(0x80 | (c & 63));
        } else {
            out += char(0xf0 | c >> 18);
            out += char(0x80 | ((c >> 12) & 63));
            out += char(0x80 | ((c >> 6) & 63));
            out += char(0x80 | (c & 63));
        }
    }
    while (!out.empty() && out.back() == '\0') {
        out.pop_back();
    }
    return out;
}

Value texcoords(const Bytes& b, size_t at, size_t n) {
    Value list = Value::array();
    for (size_t k = 0; k < n; ++k) {
        Value quad = Value::array();
        for (int i = 0; i < 4; ++i) {
            quad.push(floats(b, at + 32 * k + 8 * size_t(i), 2));
        }
        list.push(quad);
    }
    return list;
}

Value material(const Bytes& b, size_t o) {
    Value m = Value::object();
    m["name"] = fieldString(b, o, 28);
    m["black"] = color(b, o + 28);
    m["white"] = color(b, o + 32);
    const uint32_t flags = u32(b, o + 36);
    m["flags"] = flags;
    size_t p = o + 40;
    Value texmaps = Value::array(), texsrt = Value::array(), texgen = Value::array(), tev = Value::array();
    for (uint32_t i = 0; i < (flags & 3); ++i, p += 4) {
        const int s = u8(b, p + 2), t = u8(b, p + 3);
        Value x = Value::object();
        x["texture"] = u16(b, p);
        x["wrap_s"] = s & 3;
        x["wrap_t"] = t & 3;
        x["min_filter"] = (s >> 2) & 3;
        x["mag_filter"] = (t >> 2) & 3;
        texmaps.push(x);
    }
    for (uint32_t i = 0; i < ((flags >> 2) & 3); ++i, p += 20) {
        Value x = Value::object();
        x["translate"] = floats(b, p, 2);
        x["rotate"] = double(f32(b, p + 8));
        x["scale"] = floats(b, p + 12, 2);
        texsrt.push(x);
    }
    for (uint32_t i = 0; i < ((flags >> 4) & 3); ++i, p += 8) {
        Value x = Value::object();
        x["matrix"] = u8(b, p);
        x["source"] = u8(b, p + 1);
        x["raw"] = hexBytes(b, p, 8);
        texgen.push(x);
    }
    // Combiner stages: 4 bytes each (colour mode, alpha mode, two reserved bytes).
    for (uint32_t i = 0; i < ((flags >> 6) & 7); ++i, p += 4) {
        Value x = Value::object();
        x["color"] = u8(b, p);
        x["alpha"] = u8(b, p + 1);
        tev.push(x);
    }
    m["texmaps"] = texmaps;
    m["texsrt"] = texsrt;
    m["texgen"] = texgen;
    m["tev"] = tev;
    if (flags & (1u << 9)) {
        Value x = Value::object();
        x["func"] = u8(b, p);
        x["ref"] = double(f32(b, p + 4));
        m["alpha_compare"] = x;
        p += 8;
    }
    if (flags & (1u << 10)) {
        m["blend"] = bytesList(b, p, 4);
        p += 4;
    }
    if (flags & (1u << 12)) {
        m["blend_logic"] = bytesList(b, p, 4);
        p += 4;
    }
    if (flags & (1u << 13)) {
        m["indirect"] = hexBytes(b, p, 12);
        p += 12;
    }
    // Projection texture generators: one 20-byte entry when bit 15 is set (every material of the
    // opening's layouts then ends exactly at the next mat1 offset; bit 14 never set).
    for (uint32_t i = 0; i < ((flags >> 15) & 1); ++i, p += 20) {
        if (!m.has("projection")) {
            m["projection"] = Value::array();
        }
        m["projection"].push(hexBytes(b, p, 20));
    }
    if (flags & (1u << 17)) {
        Value x = Value::object();
        x["black"] = bytesList(b, p, 3);
        x["white"] = bytesList(b, p + 3, 4);
        m["font_shadow"] = x;
        p += 8;
    }
    m["size"] = p - o;
    return m;
}

Value pane(const std::string& kind, const Bytes& b, size_t o, size_t size) {
    const int flags = u8(b, o + 8);
    Value q = Value::object();
    q["type"] = kind;
    q["flags"] = flags;
    q["visible"] = bool(flags & 1);
    q["influenced_alpha"] = bool(flags & 2);
    q["origin"] = u8(b, o + 9);
    q["alpha"] = u8(b, o + 10);
    q["part_scale"] = u8(b, o + 11);
    q["name"] = fieldString(b, o + 12, 24);
    q["user"] = hexBytes(b, o + 36, 8);
    q["translate"] = floats(b, o + 44, 3);
    q["rotate"] = floats(b, o + 56, 3);
    q["scale"] = floats(b, o + 68, 2);
    q["size"] = floats(b, o + 76, 2);
    q["children"] = Value::array();
    size_t p = o + 84;
    if (kind == "pic1") {
        Value vc = Value::array();
        for (int i = 0; i < 4; ++i) {
            vc.push(color(b, p + 4 * size_t(i)));
        }
        q["vertex_colors"] = vc;
        q["material"] = u16(b, p + 16);
        const size_t n = u8(b, p + 18);
        p += 20;
        q["texcoords"] = texcoords(b, p, n);
    } else if (kind == "txt1") {
        const size_t nbytes = u16(b, p + 2);
        q["material"] = u16(b, p + 4);
        q["font"] = u16(b, p + 6);
        q["text_position"] = u8(b, p + 8);
        q["text_alignment"] = u8(b, p + 9);
        q["text_flags"] = u16(b, p + 10);
        q["italic"] = double(f32(b, p + 12));
        const size_t toff = u32(b, p + 16);
        q["colors"] = Value::array();
        q["colors"].push(color(b, p + 20));
        q["colors"].push(color(b, p + 24));
        q["font_size"] = floats(b, p + 28, 2);
        q["char_space"] = double(f32(b, p + 36));
        q["line_space"] = double(f32(b, p + 40));
        const size_t idoff = u32(b, p + 44);
        q["shadow_offset"] = floats(b, p + 48, 2);
        q["shadow_scale"] = floats(b, p + 56, 2);
        q["shadow_colors"] = Value::array();
        q["shadow_colors"].push(color(b, p + 64));
        q["shadow_colors"].push(color(b, p + 68));
        q["shadow_italic"] = double(f32(b, p + 72));
        q["text"] = toff ? utf16le(b, o + toff, nbytes) : std::string();
        q["text_id"] = idoff ? cstring(b, o + idoff) : std::string();
    } else if (kind == "wnd1") {
        Value inflation = Value::array(), frameSize = Value::array();
        for (int i = 0; i < 4; ++i) {
            inflation.push(s16(b, p + 2 * size_t(i)));
        }
        for (int i = 0; i < 4; ++i) {
            frameSize.push(s16(b, p + 8 + 2 * size_t(i)));
        }
        const size_t nframes = u8(b, p + 16);
        q["inflation"] = inflation;
        q["frame_size"] = frameSize;
        q["frame_count"] = nframes;
        q["window_flags"] = u8(b, p + 17);
        const size_t c = o + u32(b, p + 20), foff = u32(b, p + 24);
        Value content = Value::object(), vc = Value::array();
        for (int i = 0; i < 4; ++i) {
            vc.push(color(b, c + 4 * size_t(i)));
        }
        content["vertex_colors"] = vc;
        content["material"] = u16(b, c + 16);
        content["texcoords"] = texcoords(b, c + 20, u8(b, c + 18));
        q["content"] = content;
        Value frames = Value::array();
        for (size_t k = 0; k < nframes; ++k) {
            const size_t f = u32(b, o + foff + 4 * k);
            Value x = Value::object();
            x["material"] = u16(b, o + f);
            x["flip"] = u8(b, o + f + 2);
            frames.push(x);
        }
        q["frames"] = frames;
    } else if (kind == "prt1") {
        // Part pane (layout version 7): property count, magnification, then per property a pane
        // name, usage flags and offsets (from the pane start) of an override pane, user data and
        // basic information; the part's layout name follows the property table.
        const uint32_t count = u32(b, p);
        q["part_scale_xy"] = floats(b, p + 4, 2);
        q["properties"] = count;
        Value overrides = Value::array();
        for (uint32_t i = 0; i < count; ++i) {
            const size_t e = p + 12 + 40 * size_t(i);
            Value x = Value::object();
            x["name"] = fieldString(b, e, 24);
            x["usage"] = u8(b, e + 24);
            x["basic_usage"] = u8(b, e + 25);
            x["material_usage"] = u8(b, e + 26);
            const uint32_t prop = u32(b, e + 28), user = u32(b, e + 32), basic = u32(b, e + 36);
            if (prop) {
                const size_t at = o + prop;
                const std::string mag(reinterpret_cast<const char*>(b.data() + at), 4);
                x["pane"] = pane(mag, b, at, u32(b, at + 4));
            }
            x["user_data_offset"] = user;
            x["basic_info_offset"] = basic;
            overrides.push(x);
        }
        q["overrides"] = overrides;
        q["layout"] = cstring(b, p + 12 + 40 * size_t(count));
        q["raw_tail"] = hexBytes(b, p, o + size - p);
    }
    return q;
}

// Builds nested values from a flat node list with child indices (the Python reader shared references).
Value assemble(const std::vector<Value>& nodes, const std::vector<std::vector<size_t>>& children, size_t i) {
    Value v = nodes[i];
    for (size_t c : children[i]) {
        v["children"].push(assemble(nodes, children, c));
    }
    return v;
}

}   // namespace

Value readBflyt(const Bytes& d) {
    if (d.size() < 0x14 || std::memcmp(d.data(), "FLYT", 4) != 0 || d[4] != 0xff || d[5] != 0xfe) {
        throw ImportError("Not a BFLYT");
    }
    const size_t hs = u16(d, 6), sections = u16(d, 16);
    Value out = Value::object();
    out["version"] = hexNumber(u32(d, 8));
    out["textures"] = Value::array();
    out["fonts"] = Value::array();
    out["materials"] = Value::array();
    out["groups"] = Value::array();
    std::vector<Value> panes, groups;
    std::vector<std::vector<size_t>> paneChildren, groupChildren;
    std::vector<size_t> topGroups, paneStack, groupStack;
    long last = -1, root = -1, lastGroup = -1;
    size_t o = hs;
    for (size_t s = 0; s < sections; ++s) {
        checkRange(d, o, 8);
        const std::string mag(reinterpret_cast<const char*>(d.data() + o), 4);
        const size_t sz = u32(d, o + 4);
        if (mag == "lyt1") {
            Value l = Value::object();
            l["center"] = u8(d, o + 8);
            l["width"] = double(f32(d, o + 12));
            l["height"] = double(f32(d, o + 16));
            l["name"] = cstring(d, o + 28);
            out["layout"] = l;
        } else if (mag == "txl1" || mag == "fnl1") {
            Value names = Value::array();
            for (uint32_t i = 0; i < u32(d, o + 8); ++i) {
                names.push(cstring(d, o + 12 + u32(d, o + 12 + 4 * i)));
            }
            out[mag == "txl1" ? "textures" : "fonts"] = names;
        } else if (mag == "mat1") {
            Value mats = Value::array();
            for (uint32_t i = 0; i < u32(d, o + 8); ++i) {
                mats.push(material(d, o + u32(d, o + 12 + 4 * i)));
            }
            out["materials"] = mats;
        } else if (mag == "pan1" || mag == "pic1" || mag == "txt1" || mag == "wnd1" || mag == "bnd1" || mag == "prt1") {
            panes.push_back(pane(mag, d, o, sz));
            paneChildren.emplace_back();
            last = long(panes.size() - 1);
            if (!paneStack.empty()) {
                paneChildren[paneStack.back()].push_back(size_t(last));
            } else {
                root = last;
            }
        } else if (mag == "pas1") {
            if (last < 0) {
                throw ImportError("Pane children without a pane");
            }
            paneStack.push_back(size_t(last));
        } else if (mag == "pae1") {
            if (paneStack.empty()) {
                throw ImportError("Unbalanced pane children");
            }
            paneStack.pop_back();
        } else if (mag == "grp1") {
            Value g = Value::object();
            // Group record (layout version 7): name in 34 bytes, member count, then 24-byte pane
            // names.
            g["name"] = fieldString(d, o + 8, 34);
            Value members = Value::array();
            for (uint16_t i = 0; i < u16(d, o + 42); ++i) {
                members.push(fieldString(d, o + 44 + 24 * size_t(i), 24));
            }
            g["panes"] = members;
            g["children"] = Value::array();
            groups.push_back(g);
            groupChildren.emplace_back();
            lastGroup = long(groups.size() - 1);
            if (!groupStack.empty()) {
                groupChildren[groupStack.back()].push_back(size_t(lastGroup));
            } else {
                topGroups.push_back(size_t(lastGroup));
            }
        } else if (mag == "grs1") {
            if (lastGroup < 0) {
                throw ImportError("Group children without a group");
            }
            groupStack.push_back(size_t(lastGroup));
        } else if (mag == "gre1") {
            if (groupStack.empty()) {
                throw ImportError("Unbalanced group children");
            }
            groupStack.pop_back();
        }
        o += sz;
    }
    for (size_t g : topGroups) {
        out["groups"].push(assemble(groups, groupChildren, g));
    }
    out["root"] = root >= 0 ? assemble(panes, paneChildren, size_t(root)) : Value();
    return out;
}

Value readBflan(const Bytes& d) {
    if (d.size() < 0x14 || std::memcmp(d.data(), "FLAN", 4) != 0) {
        throw ImportError("Not a BFLAN");
    }
    const size_t hs = u16(d, 6), sections = u16(d, 16);
    Value out = Value::object();
    out["version"] = hexNumber(u32(d, 8));
    size_t o = hs;
    for (size_t s = 0; s < sections; ++s) {
        const std::string mag(reinterpret_cast<const char*>(d.data() + o), 4);
        const size_t sz = u32(d, o + 4);
        if (mag == "pat1") {
            const size_t ng = u16(d, o + 10), noff = u32(d, o + 12), goff = u32(d, o + 16);
            Value tag = Value::object();
            tag["order"] = u16(d, o + 8);
            tag["name"] = cstring(d, o + noff);
            // Group names: 28-byte records before version 7, 36-byte records (34-byte name, padded)
            // from version 7 on, as in the layouts' own group sections.
            const size_t stride = u32(d, 8) >= 0x07000000 ? 36 : 28;
            Value groups = Value::array();
            for (size_t i = 0; i < ng; ++i) {
                groups.push(fieldString(d, o + goff + stride * i, stride));
            }
            tag["groups"] = groups;
            tag["start"] = s16(d, o + 20);
            tag["end"] = s16(d, o + 22);
            tag["child_binding"] = u8(d, o + 24);
            out["tag"] = tag;
        } else if (mag == "pai1") {
            out["frames"] = u16(d, o + 8);
            out["loop"] = bool(u8(d, o + 10));
            const size_t ntex = u16(d, o + 12), ncont = u16(d, o + 14), coff = u32(d, o + 16);
            Value textures = Value::array();
            for (size_t i = 0; i < ntex; ++i) {
                textures.push(cstring(d, o + 20 + u32(d, o + 20 + 4 * i)));
            }
            out["textures"] = textures;
            Value contents = Value::array();
            for (size_t ci = 0; ci < ncont; ++ci) {
                const size_t c = o + u32(d, o + coff + 4 * ci);
                const size_t ntag = u8(d, c + 28), ctype = u8(d, c + 29);
                Value tags = Value::array();
                for (size_t ti = 0; ti < ntag; ++ti) {
                    const size_t t = c + u32(d, c + 32 + 4 * ti);
                    const size_t ne = u8(d, t + 4);
                    Value entries = Value::array();
                    for (size_t ei = 0; ei < ne; ++ei) {
                        const size_t e = t + u32(d, t + 8 + 4 * ei);
                        const int kind = u8(d, e + 2);
                        const size_t nkey = u16(d, e + 4), koff = u32(d, e + 8);
                        Value keys = Value::array();
                        for (size_t k = 0; k < nkey; ++k) {
                            if (kind == 2) {
                                keys.push(floats(d, e + koff + 12 * k, 3));
                            } else if (kind == 1) {
                                Value key = Value::array();
                                key.push(double(f32(d, e + koff + 8 * k)));
                                key.push(u16(d, e + koff + 8 * k + 4));
                                keys.push(key);
                            } else {
                                throw ImportError("Unsupported layout curve type");
                            }
                        }
                        Value entry = Value::object();
                        entry["index"] = u8(d, e);
                        entry["target"] = u8(d, e + 1);
                        entry["curve"] = kind == 2 ? "hermite" : "step";
                        entry["keys"] = keys;
                        entries.push(entry);
                    }
                    Value tag = Value::object();
                    tag["tag"] = std::string(reinterpret_cast<const char*>(d.data() + t), 4);
                    tag["entries"] = entries;
                    tags.push(tag);
                }
                Value content = Value::object();
                content["name"] = fieldString(d, c, 28);
                content["type"] = ctype == 1 ? "material" : "pane";
                content["tags"] = tags;
                contents.push(content);
            }
            out["contents"] = contents;
        }
        o += sz;
    }
    return out;
}

}   // namespace captain_toad
