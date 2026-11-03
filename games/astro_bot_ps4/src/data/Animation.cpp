#include "data/Animation.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace beacon::data {

namespace {

std::size_t relative(ByteView d, std::size_t at) {
    std::uint32_t v = u32(d, at);
    return v ? at + v : 0;
}

math::Quat quat48(ByteView d, std::size_t at) {
    std::uint64_t v = 0;
    for (int i = 0; i < 6; ++i) {
        v = (v << 8) | d[at + i];   // big-endian
    }
    const float scale = 1.0f / (16384.0f * std::sqrt(2.0f));
    float c[3];
    for (int k = 0; k < 3; ++k) {
        c[k] = (float(int((v >> (32 - 15 * k)) & 0x7fff)) - 16384.0f) * scale;
    }
    float largest = std::sqrt(std::max(0.0f, 1 - c[0] * c[0] - c[1] * c[1] - c[2] * c[2]));
    float q[4];
    int idx = int(v & 3), k = 0;
    for (int i = 0; i < 4; ++i) {
        q[i] = i == idx ? largest : c[k++];
    }
    return {q[0], q[1], q[2], q[3]};
}

math::Vec3 vec3(ByteView d, std::size_t at) {
    return {read<float>(d, at), read<float>(d, at + 4), read<float>(d, at + 8)};
}

// Fills frames without a key by interpolating between the nearest keyed frames of the channel.
template <typename T, typename Lerp>
void fillGaps(std::vector<T>& values, const std::vector<std::uint8_t>& have, std::uint32_t frames,
              std::uint32_t channels, Lerp lerp) {
    for (std::uint32_t c = 0; c < channels; ++c) {
        std::int64_t prev = -1;
        for (std::uint32_t f = 0; f <= frames; ++f) {
            if (f < frames && !have[std::size_t(f) * channels + c]) {
                continue;
            }
            if (prev < 0 && f >= frames) {
                break;   // channel without any value: leave the defaults
            }
            for (std::uint32_t g = std::uint32_t(prev + 1); g < f && g < frames; ++g) {
                const T& a = values[std::size_t(prev < 0 ? f : prev) * channels + c];
                const T& b = values[std::size_t(f < frames ? f : prev) * channels + c];
                float t = prev < 0 || f >= frames ? 0.0f : float(g - prev) / float(f - prev);
                values[std::size_t(g) * channels + c] = lerp(a, b, t);
            }
            prev = f;
        }
    }
}

}   // namespace

std::optional<AnimationClip> loadAnimation(ByteView d, std::string* error) {
    auto fail = [&](const char* message) -> std::optional<AnimationClip> {
        if (error) {
            *error = message;
        }
        return std::nullopt;
    };
    if (d.size() < 0x60 || std::memcmp(d.data(), "80AE", 4) != 0) {
        return fail("not an animation clip (EA08)");
    }
    AnimationClip clip;
    clip.duration = read<float>(d, 4);
    clip.sampleRate = read<float>(d, 8);
    const std::uint32_t joints = u16(d, 0x0e), frames = u16(d, 0x10), sets = u16(d, 0x12);
    const std::uint32_t cR = u16(d, 0x16), cT = u16(d, 0x18), cS = u16(d, 0x1a);
    const std::uint32_t aR = u16(d, 0x1e), aT = u16(d, 0x20), aS = u16(d, 0x22), aU = u16(d, 0x24);
    clip.frameCount = frames;
    clip.joints.resize(joints);
    clip.animRotationCount = aR;
    clip.animTranslationCount = aT;
    clip.animScaleCount = aS;

    // Joint lists (u16), in this order: constant R, T, S, user; animated R, T, S, user. Each is
    // padded with the joint count (user lists: the user channel count) to a multiple of 4 entries,
    // the constant rotation list to a multiple of 8. Checked on all 11,799 clips: every list is then
    // strictly increasing and constant/animated lists of a kind are disjoint.
    std::size_t at = 0x60;
    auto list = [&](std::uint32_t n, std::uint32_t multiple) {
        std::vector<std::uint16_t> out(n);
        for (std::uint32_t i = 0; i < n; ++i) {
            out[i] = u16(d, at + i * 2);
        }
        at += (n + multiple - 1) / multiple * multiple * 2;
        return out;
    };
    const std::uint32_t cU = u16(d, 0x1c);
    auto lcR = list(cR, 8), lcT = list(cT, 4), lcS = list(cS, 4);
    auto lcU = list(cU, 4);
    auto laR = list(aR, 4), laT = list(aT, 4), laS = list(aS, 4), laU = list(aU, 4);
    clip.userChannels.resize(u16(d, 0x26));
    for (std::uint32_t i = 0; i < lcU.size(); ++i) {
        if (lcU[i] < clip.userChannels.size()) {
            clip.userChannels[lcU[i]] = {AnimationClip::Source::Constant, i};
        }
    }
    for (std::uint32_t i = 0; i < laU.size(); ++i) {
        if (laU[i] < clip.userChannels.size()) {
            clip.userChannels[laU[i]] = {AnimationClip::Source::Animated, i};
        }
    }
    auto assign = [&](const std::vector<std::uint16_t>& l, AnimationClip::Source src, auto member) {
        for (std::uint32_t i = 0; i < l.size(); ++i) {
            if (l[i] < joints) {
                (clip.joints[l[i]].*member) = {src, i};
            }
        }
    };
    using JT = AnimationClip::JointTracks;
    using Src = AnimationClip::Source;
    assign(lcR, Src::Constant, &JT::rotation);
    assign(lcT, Src::Constant, &JT::translation);
    assign(lcS, Src::Constant, &JT::scale);
    assign(laR, Src::Animated, &JT::rotation);
    assign(laT, Src::Animated, &JT::translation);
    assign(laS, Src::Animated, &JT::scale);

    // Constant values.
    const std::size_t oR = relative(d, 0x40), oT = relative(d, 0x44), oS = relative(d, 0x48);
    if ((cR && oR + cR * 6 > d.size()) || (cT && oT + cT * 12 > d.size()) || (cS && oS + cS * 12 > d.size())) {
        return fail("constant channels out of range");
    }
    for (std::uint32_t i = 0; i < cR; ++i) {
        clip.constRotation.push_back(quat48(d, oR + i * 6));
    }
    for (std::uint32_t i = 0; i < cT; ++i) {
        clip.constTranslation.push_back(vec3(d, oT + i * 12));
    }
    for (std::uint32_t i = 0; i < cS; ++i) {
        clip.constScale.push_back(vec3(d, oS + i * 12));
    }
    const std::size_t oU = relative(d, 0x4c);
    for (std::uint32_t i = 0; i < cU && oU + i * 4 + 4 <= d.size(); ++i) {
        clip.constUser.push_back(read<float>(d, oU + i * 4));
    }

    // Frame sets.
    clip.rotations.resize(std::size_t(frames) * aR);
    clip.translations.resize(std::size_t(frames) * aT);
    clip.scales.resize(std::size_t(frames) * aS);
    clip.animUserCount = aU;
    clip.userValues.resize(std::size_t(frames) * aU);
    std::vector<std::uint8_t> haveU(clip.userValues.size());
    std::vector<std::uint8_t> haveR(clip.rotations.size()), haveT(clip.translations.size()), haveS(clip.scales.size());
    const std::size_t table = relative(d, 0x38), info = relative(d, 0x3c);
    const std::uint32_t channels = aR + aT + aS + aU;
    for (std::uint32_t s = 0; s < sets; ++s) {
        const std::size_t start = u32(d, table + s * 8 + 4);
        const std::uint32_t first = u16(d, info + s * 4), keyed = u16(d, info + s * 4 + 2);
        if (start + 32 > d.size() || first >= frames || first + keyed > frames) {
            return fail("frame set out of range");
        }
        std::uint32_t h[8];
        for (int i = 0; i < 8; ++i) {
            h[i] = u32(d, start + i * 4);
        }
        // Bit-packed channels (a handful of clips) use other sizes; not decoded yet.
        if (h[0] != aR * 6 || h[1] < aT * 12 || h[2] < aS * 12) {
            return fail("bit-packed animation channels are not supported yet");
        }
        std::size_t p = start + 32;
        for (std::uint32_t c = 0; c < aR; ++c) {
            clip.rotations[std::size_t(first) * aR + c] = quat48(d, p + c * 6);
            haveR[std::size_t(first) * aR + c] = 1;
        }
        p += h[0];
        for (std::uint32_t c = 0; c < aT; ++c) {
            clip.translations[std::size_t(first) * aT + c] = vec3(d, p + c * 12);
            haveT[std::size_t(first) * aT + c] = 1;
        }
        p += h[1];
        for (std::uint32_t c = 0; c < aS; ++c) {
            clip.scales[std::size_t(first) * aS + c] = vec3(d, p + c * 12);
            haveS[std::size_t(first) * aS + c] = 1;
        }
        p += h[2];
        for (std::uint32_t c = 0; c < aU; ++c) {
            clip.userValues[std::size_t(first) * aU + c] = read<float>(d, p + c * 4);
            haveU[std::size_t(first) * aU + c] = 1;
        }
        p += h[3];
        const std::size_t mask = p;
        const std::size_t bits = std::size_t(keyed) * channels;
        auto bit = [&](std::size_t n) {
            return (d[mask + n / 8] >> (7 - n % 8)) & 1;
        };
        std::size_t keys = mask + (bits + 7) / 8;
        if (keys + h[4] + h[5] + h[6] > d.size()) {
            return fail("frame set keys out of range");
        }
        // Rotation keys, then translation, then scale; channel by channel.
        std::size_t kR = keys, kT = keys + h[4], kS = keys + h[4] + h[5], kU = keys + h[4] + h[5] + h[6];
        for (std::uint32_t c = 0; c < channels; ++c) {
            for (std::uint32_t k = 0; k < keyed; ++k) {
                if (!bit(std::size_t(c) * keyed + k)) {
                    continue;
                }
                // A set's last key may repeat the next set's first frame; for the final set that is
                // one past the clip end. Such keys are read past, not stored.
                const std::size_t f = std::min<std::size_t>(first + 1 + k, frames - 1);
                const bool store = first + 1 + k < frames;
                if (c < aR) {
                    if (store) {
                        clip.rotations[f * aR + c] = quat48(d, kR);
                        haveR[f * aR + c] = 1;
                    }
                    kR += 6;
                } else if (c < aR + aT) {
                    if (store) {
                        clip.translations[f * aT + (c - aR)] = vec3(d, kT);
                        haveT[f * aT + (c - aR)] = 1;
                    }
                    kT += 12;
                } else if (c < aR + aT + aS) {
                    if (store) {
                        clip.scales[f * aS + (c - aR - aT)] = vec3(d, kS);
                        haveS[f * aS + (c - aR - aT)] = 1;
                    }
                    kS += 12;
                } else {
                    if (store && kU + 4 <= d.size()) {
                        clip.userValues[f * aU + (c - aR - aT - aS)] = read<float>(d, kU);
                        haveU[f * aU + (c - aR - aT - aS)] = 1;
                    }
                    kU += 4;
                }
            }
        }
        if (kR > keys + h[4] || kT > keys + h[4] + h[5] || kS > keys + h[4] + h[5] + h[6]) {
            return fail("frame set keys exceed their sections");
        }
    }
    fillGaps(clip.rotations, haveR, frames, aR, math::nlerp);
    fillGaps(clip.translations, haveT, frames, aT, math::lerp);
    fillGaps(clip.scales, haveS, frames, aS, math::lerp);
    fillGaps(clip.userValues, haveU, frames, aU, [](float a, float b, float t) { return a + (b - a) * t; });
    return clip;
}

bool AnimationClip::sampleUser(std::size_t channel, float time, float& value) const {
    if (channel >= userChannels.size() || userChannels[channel].source == Source::Base) {
        return false;
    }
    const Track& t = userChannels[channel];
    if (t.source == Source::Constant) {
        value = t.index < constUser.size() ? constUser[t.index] : 0;
        return true;
    }
    if (frameCount == 0) {
        return false;
    }
    // User channels hold discrete values (e.g. texture atlas offsets): step, not interpolate.
    std::uint32_t f = std::uint32_t(std::clamp(time * sampleRate + 0.5f, 0.0f, float(frameCount - 1)));
    value = userValues[std::size_t(f) * animUserCount + t.index];
    return true;
}

void AnimationClip::sample(const Skeleton& skeleton, float time, std::vector<JointPose>& out) const {
    out = skeleton.basePose;
    if (frameCount == 0) {
        return;
    }
    float f = std::clamp(time * sampleRate, 0.0f, float(frameCount - 1));
    std::uint32_t f0 = std::uint32_t(f), f1 = std::min(f0 + 1, frameCount - 1);
    float t = f - float(f0);
    const std::size_t n = std::min(out.size(), joints.size());
    for (std::size_t j = 0; j < n; ++j) {
        const JointTracks& jt = joints[j];
        JointPose& p = out[j];
        if (jt.rotation.source == Source::Constant) {
            p.rotation = constRotation[jt.rotation.index];
        } else if (jt.rotation.source == Source::Animated) {
            p.rotation = math::nlerp(rotations[f0 * animRotationCount + jt.rotation.index],
                                     rotations[f1 * animRotationCount + jt.rotation.index], t);
        }
        if (jt.translation.source == Source::Constant) {
            p.translation = constTranslation[jt.translation.index];
        } else if (jt.translation.source == Source::Animated) {
            p.translation = math::lerp(translations[f0 * animTranslationCount + jt.translation.index],
                                       translations[f1 * animTranslationCount + jt.translation.index], t);
        }
        if (jt.scale.source == Source::Constant) {
            p.scale = constScale[jt.scale.index];
        } else if (jt.scale.source == Source::Animated) {
            p.scale = math::lerp(scales[f0 * animScaleCount + jt.scale.index],
                                 scales[f1 * animScaleCount + jt.scale.index], t);
        }
    }
}

}   // namespace data
