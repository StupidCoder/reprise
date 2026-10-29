#include "story/a2_staff_roll.hpp"
#include <cstdio>
#include <stdexcept>

namespace picarat {
namespace {
std::string localizedPath(const char* f, const std::string& language, int n) {
    char b[96];
    std::snprintf(b, sizeof b, f, language.c_str(), n);
    return b;
}

std::string numberedPath(const char* f, int n) {
    char b[96];
    std::snprintf(b, sizeof b, f, n);
    return b;
}

// DS 2D alpha blending (BLDALPHA) on the 6-bit colour pipeline: 2D colours
// expand to 2c; c6 = min(63, (a6*EVA + b6*EVB + 8) >> 4). Measured against
// the reference capture of this scene (all blended picture pixels); the
// RGB555 surface keeps the upper five bits.
std::uint16_t alphaBlend(std::uint16_t a, std::uint16_t b, int eva, int evb) {
    std::uint16_t out = 0x8000;
    for (int shift : {0, 5, 10}) {
        const int c6 =
            std::min(63, (2 * ((a >> shift) & 31) * eva + 2 * ((b >> shift) & 31) * evb + 8) >> 4);
        out |= std::uint16_t((c6 >> 1) << shift);
    }
    return out;
}
} // namespace

StaffRoll::StaffRoll(GraphicsAssets& assets, PresentFrame present, AudioService* audio)
    : kit_(assets, std::move(present), audio, SceneKind::Staff) {
    // 020eff04: display setup (0206dabc/0206d9c0/0206e0a8), both master
    // brightness registers at -16, the paper sprite (staff_bg.sbj, 2D upper),
    // the upper BG3 staffroll_sub (never enabled by DISPCNT below), three
    // loaders and their first requests, then six streaming steps.
    kit_.setBrightness(-16);
    upper_ = lower_ = -16;
    kit_.painter = [this](Surface& image) {
        paint(image);
    };
    picture_ = credit_ = 0;
    blend_ = blendTarget_ = blendSpeed_ = 0;
    pictureClock_ = creditClock_ = -1;
    for (auto& l : loaders_) {
        reset(l);
    }
    request(loaders_[0], 0, localizedPath("data_lt2/bg/ending/%s/00.arc", kit_.language, 0));
    request(loaders_[1], 1, localizedPath("data_lt2/bg/ending/%s/01.arc", kit_.language, 0));
    request(loaders_[2], 0, "data_lt2/bg/ending/00.arc");
    for (int i = 0; i < 6; ++i) {
        streamOne();
    }
}

std::string StaffRoll::asset(const std::string& romPath) {
    // "data_lt2/bg/ending/ge/03.arc" -> packed background "ending/ge/03".
    const std::string prefix = "data_lt2/bg/";
    if (romPath.rfind(prefix, 0) != 0 || romPath.size() < prefix.size() + 4) {
        throw std::runtime_error("staff roll path");
    }
    return romPath.substr(prefix.size(), romPath.size() - prefix.size() - 4);
}

void StaffRoll::reset(Loader& l) {
    l.id = -1;
    l.requested = l.opened = l.loaded = false;
}

void StaffRoll::request(Loader& l, int id, const std::string& path) {
    reset(l);
    if (path.empty()) {
        return;
    }
    l.id = id;
    l.path = path;
    l.requested = true;
}

bool StaffRoll::stream(Loader& l) {
    // 020efe24: returns 1 while it did work for a pending load. Step one opens
    // the file (0206c10c; failure resets the record); step two marks a ".arb"
    // as loaded directly, otherwise it expands the LZ data (0206c318).
    if (!l.requested || l.loaded) {
        return false;
    }
    if (!l.opened) {
        if (!fileExists(kit_.assets.root / "bg" / (asset(l.path) + ".l2bg"))) {
            reset(l);
        } else {
            l.opened = true;
        }
    } else {
        l.loaded = true;
    }
    return true;
}

void StaffRoll::streamOne() {
    for (auto& l : loaders_) {
        if (stream(l)) {
            return;
        }
    }
}

void StaffRoll::update() {
    // 020f0290. Lower screen: page cycle of 192 steps; at 0 the upper layer
    // takes loader A, at 1 the lower layer takes loader B; at 64/128 the next
    // pages are requested. The main BG scroll (+0x74/+0x78) is the position.
    const int r = creditClock_ % 192;
    if (r == 0) {
        ++credit_;
        if (loaders_[0].loaded) {
            lowerA_ = asset(loaders_[0].path); // 0206ffec(…,3,…)
        }
        reset(loaders_[0]);
    } else if (r == 1) {
        if (loaders_[1].loaded) {
            lowerB_ = asset(loaders_[1].path); // 020702c8(…,3,…)
        }
        reset(loaders_[1]);
    } else if (r == 64) {
        if (credit_ < 36) {
            request(loaders_[0], credit_,
                    localizedPath("data_lt2/bg/ending/%s/%02d.arc", kit_.language, credit_));
        }
    } else if (r == 128) {
        if (credit_ < 35) {
            request(loaders_[1], credit_ + 1,
                    localizedPath("data_lt2/bg/ending/%s/%02d.arc", kit_.language, credit_ + 1));
        }
    }
    scroll_ = r;
    // Upper screen: picture cycle of 362 steps; fade out at 0, next picture
    // and fade in at 40, request the following picture at 103 (03, 07 and 18
    // are stored uncompressed as .arb).
    const int q = pictureClock_ % 362;
    if (blendSpeed_) {
        blend_ += blendSpeed_;
        if (blendSpeed_ < 0 ? blend_ <= blendTarget_ : blend_ >= blendTarget_) {
            blend_ = blendTarget_;
            blendSpeed_ = 0;
        }
    }
    if (q == 0) {
        blendTarget_ = 0;
        blendSpeed_ = -1;
    } else if (q == 40) {
        ++picture_;
        if (loaders_[2].loaded) {
            picture_image_ = asset(loaders_[2].path); // 0207064c(…,2,…)
        }
        reset(loaders_[2]);
        blendTarget_ = 15;
        blendSpeed_ = 1;
    } else if (q == 103) {
        if (picture_ < 19) {
            const bool raw = picture_ == 3 || picture_ == 7 || picture_ == 18;
            request(
                loaders_[2], picture_,
                numberedPath(raw ? "data_lt2/bg/ending/%02d.arb" : "data_lt2/bg/ending/%02d.arc",
                             picture_));
        }
        blendTarget_ = 15;
        blendSpeed_ = 1;
    }
    // 0201c08c(0x04001050, BG2, OBJ, EVA, 15-EVA) and DISPCNT: BG2 is shown
    // only while EVA is nonzero (OBJ always).
}

void StaffRoll::step() {
    // 020f01f0: counters, update, frame (poll, draw, present), one loader
    // step, two more frames.
    ++creditClock_;
    ++pictureClock_;
    update();
    if (researchStep) {
        researchStep(creditClock_, scroll_, blend_, picture_, credit_);
    }
    kit_.poll();
    kit_.present();
    shownScroll_ = scroll_; // the BG scroll appears with the swap
    streamOne();
    kit_.wait(1);
    kit_.wait(1);
}

void StaffRoll::fade(bool in, int interval) {
    // 02071790 (in) / 02071e84 (out) with step 1: at most 17 iterations of
    // `interval` frames; the fade in stops early once both screens reach 0.
    for (int i = 0; i < 17; ++i) {
        kit_.wait(interval);
        if (in) {
            upper_ = std::min(0, upper_ + 1);
            lower_ = std::min(0, lower_ + 1);
        } else {
            upper_ = std::max(-16, upper_ - 1);
            lower_ = std::max(-16, lower_ - 1);
        }
        if (in && upper_ == 0 && lower_ == 0) {
            break;
        }
    }
}

void StaffRoll::run() {
    // 020f0148: one step, fade in (interval 3), credits music BG_002 on
    // stream route 1 (0207fb84), steps until 3 * steps >= 20280, then the
    // music stops over 1 s (0207fbcc) and both screens fade out.
    step();
    fade(true, 3);
    if (auto s = kit_.sound()) {
        s->playStream(1, 2, -1, 1.0f, 0.0f, false);
    }
    for (;;) {
        step();
        if (creditClock_ * 3 >= 20280) {
            break;
        }
    }
    if (auto s = kit_.sound()) {
        s->stopStream(1.0f);
    }
    fade(false, 3);
}

void StaffRoll::paint(Surface& image) {
    // Upper: 020f05a4 draws the 64x64 paper sprite on a 4x3 grid; BG2 (the
    // picture, colour 0 transparent) is blended over it while EVA > 0.
    const auto& paper = kit_.assets.sprite("ending/staff_bg.arj");
    const auto& steps = paper.animations.at(paper.animationNames.at("gfx"));
    if (!steps.empty()) {
        for (int y = 0; y < 192; y += 64) {
            for (int x = 0; x < 256; x += 64) {
                blit(image, paper.images.at(steps[0].image), x, y);
            }
        }
    }
    if (blend_ && !picture_image_.empty()) {
        const auto& p = kit_.assets.background(picture_image_);
        for (unsigned y = 0; y < 192 && y < p.height; ++y) {
            for (unsigned x = 0; x < 256 && x < p.width; ++x) {
                const auto c = p.pixels[y * p.width + x];
                if (!(c & 0x8000)) {
                    continue;
                }
                auto& d = image.pixels[y * 256 + x];
                d = alphaBlend(c, d, blend_, 15 - blend_);
            }
        }
    }
    // Lower: page A above page B, scrolled up by the latched position.
    for (int y = 0; y < 192; ++y) {
        const int row = y + shownScroll_;
        const std::string& page = row < 192 ? lowerA_ : lowerB_;
        if (page.empty()) {
            continue;
        }
        const auto& p = kit_.assets.background(page);
        const int sy = row % 192;
        for (int x = 0; x < 256; ++x) {
            image.pixels[std::size_t(192 + y) * 256 + std::size_t(x)] =
                p.pixels[std::size_t(sy) * p.width + std::size_t(x)];
        }
    }
    applyBrightness(image, upper_, lower_);
}
} // namespace picarat
