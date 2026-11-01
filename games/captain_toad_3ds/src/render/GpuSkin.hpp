// =============================================================================================
// GpuSkin.hpp - skinning the animated models in the vertex stage (enhancement)
// =============================================================================================
//
// The CPU path (CharacterAnimation::skinVertices) writes every animated vertex for every drawn
// frame: the weighted sum of up to four bone transforms (smooth skinning: bone x inverse bind;
// rigid: the bone alone), then the model's placement, for position, normal and tangent. Drawn at
// a headset's rate that, and uploading the result, cost the Quest 3 about a third of its main
// thread. On the GPU the vertex buffer keeps the models' bind data and every vertex gets four
// palette indices and weights (inJoints, inWeights); the palette holds placement x skinning
// matrix per bone and mode (so one matrix per index), as a float texture of three texels (the
// rows of a 3x4 matrix) per entry, 256 entries per row, written once per frame.
//
// skinnedVertexSource turns any vertex source of the dialect into its skinned variant: the reads
// of inPosition, inNormal and inTexCoord1 (the tangent) in its main read the skinned values,
// computed before it runs. The arithmetic differs from the CPU's in the last bits (sums in the
// GPU's order, the placement folded into the palette): pixel-level differences, no game effect.
// =============================================================================================
#pragma once

#include <regex>
#include <string>
#include <string_view>

namespace captain_toad::gpuskin {

inline constexpr int kEntriesPerRow = 256;   // palette entries per texture row (3 texels each)

inline std::string skinnedVertexSource(std::string_view source) {
    std::string text(source);
    const size_t mainAt = text.find("void main");
    if (mainAt == std::string::npos) {
        return text;
    }
    std::string head = text.substr(0, mainAt), body = text.substr(mainAt);
    auto declared = [&](const char* name) {
        return std::regex_search(head, std::regex(std::string(R"(\bin\s+vec[34]\s+)") + name + R"(\s*;)"));
    };
    const bool normal = declared("inNormal"), tangent = declared("inTexCoord1");
    const bool position4 = std::regex_search(head, std::regex(R"(\bin\s+vec4\s+inPosition\s*;)"));
    body = std::regex_replace(body, std::regex(R"(\binPosition\b)"), "skinPosition");
    if (normal) {
        body = std::regex_replace(body, std::regex(R"(\binNormal\b)"), "skinNormal");
    }
    if (tangent) {
        body = std::regex_replace(body, std::regex(R"(\binTexCoord1\b)"), "skinTangent");
    }
    body = std::regex_replace(body, std::regex(R"(void\s+main\s*\(\s*\))"), "void skinnedMain()",
                              std::regex_constants::format_first_only);
    std::string skin = "in vec4 inJoints;in vec4 inWeights;uniform sampler2D skinBones;\n";
    skin += position4 ? "vec4 skinPosition;\n" : "vec3 skinPosition;\n";
    skin += normal ? "vec3 skinNormal;\n" : "";
    skin += tangent ? "vec3 skinTangent;\n" : "";
    skin += "void skinSetup(){vec3 p=vec3(0.0);";
    skin += normal ? "vec3 n=vec3(0.0);" : "";
    skin += tangent ? "vec3 t=vec3(0.0);" : "";
    skin += "vec4 hp=vec4(inPosition.xyz,1.0);\n"
            " for(int j=0;j<4;j++){float w=inWeights[j];if(w==0.0)continue;\n"
            "  int e=int(inJoints[j]+0.5);ivec2 at=ivec2((e%" +
            std::to_string(kEntriesPerRow) + ")*3,e/" + std::to_string(kEntriesPerRow) +
            ");\n"
            "  vec4 r0=texelFetch(skinBones,at,0),r1=texelFetch(skinBones,at+ivec2(1,0),0),"
            "r2=texelFetch(skinBones,at+ivec2(2,0),0);\n"
            "  p+=w*vec3(dot(r0,hp),dot(r1,hp),dot(r2,hp));";
    skin += normal ? "n+=w*vec3(dot(r0.xyz,inNormal),dot(r1.xyz,inNormal),dot(r2.xyz,inNormal));" : "";
    skin += tangent ? "t+=w*vec3(dot(r0.xyz,inTexCoord1),dot(r1.xyz,inTexCoord1),dot(r2.xyz,inTexCoord1));" : "";
    skin += "}\n";
    skin += position4 ? " skinPosition=vec4(p,1.0);" : " skinPosition=p;";
    skin += normal ? "skinNormal=n;" : "";
    skin += tangent ? "skinTangent=t;" : "";
    skin += "}\n";
    return head + skin + body + "\nvoid main(){skinSetup();skinnedMain();}\n";
}

}   // namespace captain_toad::gpuskin
