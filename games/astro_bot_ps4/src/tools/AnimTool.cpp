// beacon-anim: inspects skeletons and animation clips (research and verification tool).
//   beacon-anim info FILE.skel|FILE.anim
//   beacon-anim check DIR     loads every .skel and .anim below DIR; pairs clips with their
//                             skeleton ("<model>_anim_<name>.anim" -> "<model>.skel")
#include "anim/Pose.hpp"
#include "data/Animation.hpp"
#include "data/Hash.hpp"
#include "data/Skeleton.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <cstdlib>
#include <map>
#include <vector>

using namespace beacon;

namespace fs = std::filesystem;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: beacon-anim info FILE | check DIR\n");
        return 2;
    }
    std::string cmd = argv[1];
    if (cmd == "info") {
        data::Bytes bytes;
        data::loadFile(argv[2], bytes);
        std::string error;
        if (fs::path(argv[2]).extension() == ".skel") {
            auto s = data::loadSkeleton(bytes, &error);
            if (!s) {
                std::fprintf(stderr, "%s\n", error.c_str());
                return 1;
            }
            std::printf("joints %zu, user channels %u\n", s->jointCount(), s->userChannelCount);
            for (std::size_t j = 0; j < s->jointCount(); ++j) {
                const auto& p = s->basePose[j];
                std::printf("  %2zu parent %3d hash %08x rot (%.3f %.3f %.3f %.3f) pos (%.3f %.3f %.3f)\n", j,
                            s->parent[j], s->nameHash[j], p.rotation.x, p.rotation.y, p.rotation.z, p.rotation.w,
                            p.translation.x, p.translation.y, p.translation.z);
            }
            for (std::size_t c = 0; c < s->userChannelName.size(); ++c) {
                std::printf("  user channel %zu name %08x node %08x\n", c, s->userChannelName[c],
                            s->userChannelNode[c]);
            }
            return 0;
        }
        auto a = data::loadAnimation(bytes, &error);
        if (!a) {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }
        for (std::size_t c = 0; c < a->userChannels.size(); ++c) {
            std::printf("user channel %zu:", c);
            for (std::uint32_t f = 0; f < a->frameCount; ++f) {
                float v;
                if (a->sampleUser(c, float(f) / a->sampleRate, v)) {
                    std::printf(" %g", v);
                } else {
                    std::printf(" -");
                    break;
                }
            }
            std::printf("\n");
        }
        std::printf("duration %.3f s, %u frames at %.1f Hz, joints %zu, animated R/T/S %u/%u/%u\n", a->duration,
                    a->frameCount, a->sampleRate, a->joints.size(), a->animRotationCount, a->animTranslationCount,
                    a->animScaleCount);
        return 0;
    }
    if (cmd == "joints" && argc >= 5) {   // local translation / rotation of joints over a clip
        data::Bytes sb, ab;
        data::loadFile(argv[2], sb);
        data::loadFile(argv[3], ab);
        auto s = data::loadSkeleton(sb);
        auto a = data::loadAnimation(ab);
        if (!s || !a) {
            std::fprintf(stderr, "cannot load\n");
            return 1;
        }
        std::vector<data::JointPose> pose;
        for (std::uint32_t f = 0; f < a->frameCount; ++f) {
            a->sample(*s, float(f) / a->sampleRate, pose);
            std::printf("%3u", f);
            for (int i = 4; i < argc; ++i) {
                const auto& p = pose[std::size_t(std::atoi(argv[i]))];
                std::printf("  T(%.4f %.4f %.4f) R(%.4f %.4f %.4f %.4f)", p.translation.x, p.translation.y,
                            p.translation.z, p.rotation.x, p.rotation.y, p.rotation.z, p.rotation.w);
            }
            std::printf("\n");
        }
        return 0;
    }
    if (cmd == "model" && argc >= 5) {   // model-space frame of a joint (by name) over a clip
        data::Bytes sb, ab;
        data::loadFile(argv[2], sb);
        data::loadFile(argv[3], ab);
        auto s = data::loadSkeleton(sb);
        auto a = data::loadAnimation(ab);
        if (!s || !a) {
            std::fprintf(stderr, "cannot load\n");
            return 1;
        }
        const std::uint32_t h = data::fnv1a(argv[4]);
        int joint = -1;
        for (std::size_t j = 0; j < s->nameHash.size(); ++j) {
            if (s->nameHash[j] == h) {
                joint = int(j);
            }
        }
        std::vector<data::JointPose> pose;
        std::vector<math::Mat4> model;
        anim::jointWorldMatrices(*s, s->basePose, model);
        const auto print = [&](const char* label) {
            const math::Mat4& m = model[std::size_t(joint)];
            std::printf("%s T(%.5f %.5f %.5f) X(%.4f %.4f %.4f) Y(%.4f %.4f %.4f)\n", label, m[12], m[13], m[14], m[0], m[1],
                        m[2], m[4], m[5], m[6]);
        };
        if (joint < 0) {
            std::fprintf(stderr, "no joint %s\n", argv[4]);
            return 1;
        }
        print("base");
        for (std::uint32_t f = 0; f < a->frameCount; f += std::max(1u, a->frameCount / 8)) {
            a->sample(*s, float(f) / a->sampleRate, pose);
            anim::jointWorldMatrices(*s, pose, model);
            char label[16];
            std::snprintf(label, sizeof label, "%3u", f);
            print(label);
        }
        return 0;
    }
    if (cmd == "check") {
        std::map<std::string, data::Skeleton> skeletons;
        std::map<std::string, int> failures;
        std::map<std::string, std::string> example;
        int skelOk = 0, animOk = 0, paired = 0, mismatched = 0, orphan = 0;
        double worstNorm = 0;
        std::vector<fs::path> anims;
        for (const auto& e : fs::recursive_directory_iterator(argv[2])) {
            data::Bytes bytes;
            std::string error;
            if (e.path().extension() == ".skel") {
                data::loadFile(e.path(), bytes);
                if (auto s = data::loadSkeleton(bytes, &error)) {
                    skeletons[e.path().stem().string()] = std::move(*s);
                    ++skelOk;
                } else {
                    ++failures["skel: " + error];
                    example.emplace("skel: " + error, e.path().string());
                }
            } else if (e.path().extension() == ".anim") {
                anims.push_back(e.path());
            }
        }
        for (const auto& path : anims) {
            data::Bytes bytes;
            std::string error;
            data::loadFile(path, bytes);
            auto a = data::loadAnimation(bytes, &error);
            if (!a) {
                ++failures["anim: " + error];
                example.emplace("anim: " + error, path.string());
                continue;
            }
            ++animOk;
            for (const auto& q : a->rotations) {
                worstNorm =
                    std::max(worstNorm, std::fabs(1.0 - std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w)));
            }
            std::string stem = path.stem().string();
            auto cut = stem.find("_anim_");
            auto it = cut == std::string::npos ? skeletons.end() : skeletons.find(stem.substr(0, cut));
            if (it == skeletons.end()) {
                ++orphan;
            } else if (it->second.jointCount() == a->joints.size()) {
                ++paired;
            } else {
                ++mismatched;
            }
        }
        std::printf("skeletons %d, clips %d (paired %d, joint count mismatch %d, no skeleton by name %d)\n", skelOk,
                    animOk, paired, mismatched, orphan);
        std::printf("largest rotation key norm error %.2e\n", worstNorm);
        for (const auto& [error, n] : failures) {
            std::printf("failed %d: %s (e.g. %s)\n", n, error.c_str(), example[error].c_str());
        }
        return 0;
    }
    return 2;
}
