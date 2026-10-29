// Game resources of the 3DS adventures. See a56_assets.hpp.
#include "graphics/a56_assets.hpp"
#include "graphics/a56_picture.hpp"
#include "import/asset_codecs.hpp"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace picarat::adventure56 {

namespace fs = std::filesystem;

namespace {

void check(bool ok, const char* what) {
    if (!ok) {
        throw std::runtime_error(what);
    }
}

std::string numbered(unsigned i, const char* extension) {
    char name[32];
    std::snprintf(name, sizeof name, "%03u.%s", i, extension);
    return name;
}

} // namespace

// ------------------------------------------------------------------ pack

Pack::Pack(fs::path root, std::string language)
    : root_(std::move(root)), language_(std::move(language)) {}

fs::path Pack::find(const std::string& path, const std::string& extension) const {
    std::string resolved;
    for (char c : path) {
        if (c == '@') {
            resolved += language_;
        } else {
            resolved += c;
        }
    }
    if (!extension.empty() && fs::path(resolved).extension().empty()) {
        resolved += extension;
    }
    for (const char* archive : {"a", "b", "ge"}) {
        const std::string name = archive == std::string("ge") ? language_ : archive;
        const fs::path candidate = root_ / "arc" / name / resolved;
        if (fs::exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

Bytes Pack::read(const fs::path& file) const {
    std::ifstream in(file, std::ios::binary);
    check(bool(in), "cannot open a pack file");
    return Bytes((std::istreambuf_iterator<char>(in)), {});
}

// ------------------------------------------------------------------- RES

ResContainer::ResContainer(Bytes data) {
    data_ = std::move(data);
    check(data_.size() >= 0x14, "RES container too short");
    auto u16 = [&](std::size_t at) {
        check(at + 2 <= data_.size(), "RES field outside");
        return unsigned(data_[at] | data_[at + 1] << 8);
    };
    strings_ = std::size_t(u16(8)) * 4;
    for (unsigned d = 0; d < 2; ++d) {
        const std::size_t at = std::size_t(u16(0x0c + d * 4)) * 4;
        const unsigned count = u16(0x0e + d * 4);
        for (unsigned i = 0; i < count; ++i) {
            Section s;
            s.offset = std::size_t(u16(at + i * 8)) * 4;
            s.count = u16(at + i * 8 + 2);
            s.type = u16(at + i * 8 + 4);
            s.size = u16(at + i * 8 + 6);
            if (s.type == 9999) {
                break;
            }
            check(s.offset + std::size_t(s.count) * s.size <= data_.size(), "RES section outside");
            sections_.push_back(s);
        }
    }
}

std::string ResContainer::magic() const {
    return std::string(reinterpret_cast<const char*>(data_.data()), 4);
}

const ResContainer::Section* ResContainer::section(unsigned type) const {
    for (const auto& s : sections_) {
        if (s.type == type) {
            return &s;
        }
    }
    return nullptr;
}

std::uint32_t ResContainer::word(const Section& s, unsigned entry, unsigned index) const {
    const std::size_t at = s.offset + std::size_t(entry) * s.size + index * 4;
    check(at + 4 <= data_.size() && index * 4 + 4 <= s.size, "RES entry field outside");
    return std::uint32_t(data_[at] | data_[at + 1] << 8 | data_[at + 2] << 16) |
           std::uint32_t(data_[at + 3]) << 24;
}

float ResContainer::real(const Section& s, unsigned entry, unsigned index) const {
    const std::uint32_t w = word(s, entry, index);
    float f;
    std::memcpy(&f, &w, 4);
    return f;
}

std::string ResContainer::text(std::uint32_t offset) const {
    const std::size_t at = strings_ + offset;
    if (at >= data_.size()) {
        return {};
    }
    const auto* p = reinterpret_cast<const char*>(data_.data() + at);
    return std::string(p, strnlen(p, data_.size() - at));
}

// -------------------------------------------------------------- pictures

void applyTextureMode(ctr3d::Texture& t, std::uint32_t mode) {
    // A texture entry's third word (sections 240 of ANMC / CHRC): bits
    // 8..9 the horizontal wrap, 10..11 the vertical one, in the GPU's order
    // (0 clamp to edge, 1 clamp to border, 2 repeat, 3 mirrored repeat;
    // 0x500 border, 0xa07 repeat as seen); the borders are transparent
    // (puzzle 057's empty grid cell points outside its sprite).
    static const ctr3d::Texture::Wrap kWrap[4] = {
        ctr3d::Texture::Wrap::Clamp, ctr3d::Texture::Wrap::Border, ctr3d::Texture::Wrap::Repeat,
        ctr3d::Texture::Wrap::Mirror};
    t.wrapS = kWrap[(mode >> 8) & 3];
    t.wrapT = kWrap[(mode >> 10) & 3];
}

std::shared_ptr<ctr3d::Texture> loadTexture(const Pack& pack, const fs::path& file) {
    const Bytes bytes = pack.read(file);
    const auto picture = import::adventure56::parseImgc(bytes);
    const platform::Image image = decodePicture(picture, 0);
    auto texture = std::make_shared<ctr3d::Texture>();
    texture->width = image.width;
    texture->height = image.height;
    texture->pixels = image.pixels;
    return texture;
}

// ------------------------------------------------------------ animations

const AnimationSet::Animation* AnimationSet::animation(const std::string& name) const {
    const int i = animationIndex(name);
    return i < 0 ? nullptr : &animations[std::size_t(i)];
}

int AnimationSet::animationIndex(const std::string& name) const {
    for (std::size_t i = 0; i < animations.size(); ++i) {
        if (animations[i].name == name) {
            return int(i);
        }
    }
    return -1;
}

std::shared_ptr<AnimationSet> loadAnimationSet(const Pack& pack, const fs::path& folder) {
    const Bytes raw = pack.read(folder / "RES.bin");
    ResContainer res(import::decompressMethodWord(raw));
    auto set = std::make_shared<AnimationSet>();
    set->popup = res.magic() == "POPC";
    std::map<std::uint32_t, int> textureByHash, materialByHash, bufferByHash, cellByHash,
        compositeByHash;
    if (const auto* s = res.section(240)) {
        for (unsigned i = 0; i < s->count; ++i) {
            textureByHash[res.word(*s, i, 0)] = int(i);
            const fs::path file = folder / numbered(i, "xi");
            set->textures.push_back(fs::exists(file) ? loadTexture(pack, file) : nullptr);
            if (set->textures.back()) {
                applyTextureMode(*set->textures.back(), res.word(*s, i, 2));
            }
        }
    }
    if (const auto* s = res.section(290)) {
        for (unsigned i = 0; i < s->count; ++i) {
            AnimationSet::Material m;
            m.hash = res.word(*s, i, 0);
            const auto t = textureByHash.find(res.word(*s, i, 4));
            m.texture = t == textureByHash.end() ? -1 : t->second;
            m.uvScale[0] = res.real(*s, i, 6);
            m.uvScale[1] = res.real(*s, i, 7);
            // The scales are 1 / the GPU texture's size: the picture padded
            // to powers of two (pica_num_ll: 280 x 144 in 512 x 256).
            // Texture holds the picture alone, so rescale to its size.
            if (m.texture >= 0 && set->textures[std::size_t(m.texture)]) {
                const auto& t = *set->textures[std::size_t(m.texture)];
                auto pow2 = [](int n) {
                    int p = 8;
                    while (p < n) {
                        p *= 2;
                    }
                    return p;
                };
                if (t.width > 0 && t.height > 0) {
                    m.uvScale[0] *= float(pow2(t.width)) / float(t.width);
                    m.uvScale[1] *= float(pow2(t.height)) / float(t.height);
                }
            }
            materialByHash[m.hash] = int(i);
            set->materials.push_back(m);
        }
    }
    if (const auto* s = res.section(100)) {
        for (unsigned i = 0; i < s->count; ++i) {
            bufferByHash[res.word(*s, i, 0)] = int(i);
            const fs::path file = folder / numbered(i, "pvb");
            set->buffers.push_back(fs::exists(file) && fs::file_size(file) > 0
                                       ? parseVertexBuffer(pack.read(file))
                                       : VertexBuffer{});
        }
    }
    if (const auto* s = res.section(200)) {
        for (unsigned i = 0; i < s->count; ++i) {
            AnimationSet::Cell c;
            c.hash = res.word(*s, i, 0);
            const auto m = materialByHash.find(res.word(*s, i, 3));
            c.material = m == materialByHash.end() ? -1 : m->second;
            const auto b = bufferByHash.find(res.word(*s, i, 4));
            c.buffer = b == bufferByHash.end() ? -1 : b->second;
            for (unsigned k = 0; k < 6; ++k) {
                c.box[k] = res.real(*s, i, 5 + k);
            }
            const fs::path file = folder / numbered(i, "pbi");
            if (fs::exists(file) && fs::file_size(file) > 0) {
                c.indices = parseIndexBuffer(pack.read(file));
            }
            cellByHash[c.hash] = int(i);
            set->cells.push_back(std::move(c));
        }
    }
    if (const auto* s = res.section(300)) {
        for (unsigned i = 0; i < s->count; ++i) {
            const auto c = cellByHash.find(res.word(*s, i, 0));
            set->cellList.push_back(c == cellByHash.end() ? -1 : c->second);
        }
    }
    // POPC (pop-up layers) has the same sections with fewer fields: 310 adds
    // a hash before the box (36 bytes), 400 has no offset (8), 410 no
    // flags or origin (32).
    if (const auto* s = res.section(310)) {
        int running = 0;
        const unsigned boxAt = s->size >= 36 ? 3 : 2;
        for (unsigned i = 0; i < s->count; ++i) {
            AnimationSet::Composite c;
            c.hash = res.word(*s, i, 0);
            c.key = boxAt == 3 ? res.word(*s, i, 2) : c.hash;
            const std::uint32_t w = res.word(*s, i, 1);
            c.name = res.text(w & 0xffff);
            c.count = int(w >> 16);
            c.first = running;
            running += c.count;
            for (unsigned k = 0; k < 6; ++k) {
                c.box[k] = res.real(*s, i, boxAt + k);
            }
            compositeByHash[c.hash] = int(i);
            set->composites.push_back(std::move(c));
        }
    }
    if (const auto* s = res.section(400)) {
        for (unsigned i = 0; i < s->count; ++i) {
            AnimationSet::Key k;
            const auto c = compositeByHash.find(res.word(*s, i, 0));
            k.composite = c == compositeByHash.end() ? -1 : c->second;
            k.duration = res.word(*s, i, 1);
            for (unsigned j = 0; j < 3 && s->size >= 20; ++j) {
                k.offset[j] = res.real(*s, i, 2 + j);
            }
            set->keys.push_back(k);
        }
    }
    if (const auto* s = res.section(410)) {
        int running = 0;
        for (unsigned i = 0; i < s->count; ++i) {
            AnimationSet::Animation a;
            a.hash = res.word(*s, i, 0);
            const std::uint32_t w = res.word(*s, i, 1);
            a.name = res.text(w & 0xffff);
            a.count = int(w >> 16);
            a.first = running;
            running += a.count;
            const bool popup = s->size < 48;
            if (!popup) {
                a.flags = res.word(*s, i, 2);
                for (unsigned k = 0; k < 3; ++k) {
                    a.origin[k] = res.real(*s, i, 3 + k);
                }
            }
            for (unsigned k = 0; k < 6; ++k) {
                a.box[k] = res.real(*s, i, (popup ? 2 : 6) + k);
            }
            for (int k = a.first; k < a.first + a.count && k < int(set->keys.size()); ++k) {
                a.length += set->keys[std::size_t(k)].duration;
            }
            set->animations.push_back(std::move(a));
        }
    }
    return set;
}

} // namespace picarat::adventure56
