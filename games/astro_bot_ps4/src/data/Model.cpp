#include "data/Model.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <map>

namespace beacon::data {

namespace {

// Vertex attribute formats (one nibble per attribute in m_vertexFormat).
enum : std::uint32_t {
    kFormatFloat3 = 2,
    kFormatFloat2 = 3,
    kFormatHalf4 = 5,
    kFormatUint16x4 = 9,   // blend indices
    kFormatUnorm8x4 = 11,
    kFormatPackedNormal = 14,   // unsigned 11:11:10 floats holding n * 0.5 + 0.5
};
// Vertex attribute meanings (m_vertexSemantics).
enum : std::uint32_t {
    kSemanticBlendIndices = 2,
    kSemanticBlendWeights = 3,
    kSemanticColor = 4,
    kSemanticNormal = 5,
    kSemanticPosition = 6,
    kSemanticTangent = 9,
    kSemanticTexcoord = 10,
};

std::uint32_t formatSize(std::uint32_t format) {
    switch (format) {
    case kFormatFloat3:
        return 12;
    case kFormatFloat2:
    case kFormatHalf4:
    case kFormatUint16x4:
        return 8;
    case kFormatUnorm8x4:
    case kFormatPackedNormal:
        return 4;
    default:
        return 0;
    }
}

float halfToFloat(std::uint16_t h) {
    std::uint32_t sign = (h >> 15) & 1, exp = (h >> 10) & 0x1f, mant = h & 0x3ff;
    float v;
    if (exp == 0) {
        v = std::ldexp(float(mant), -24);
    } else if (exp == 31) {
        v = mant ? NAN : INFINITY;
    } else {
        v = std::ldexp(float(mant | 0x400), int(exp) - 25);
    }
    return sign ? -v : v;
}

// Unsigned small float with a 5-bit exponent (bias 15) and `mantissaBits` mantissa bits.
float smallFloat(std::uint32_t bits, int mantissaBits) {
    std::uint32_t exp = bits >> mantissaBits, mant = bits & ((1u << mantissaBits) - 1);
    if (exp == 0) {
        return std::ldexp(float(mant), -14 - mantissaBits);
    }
    return std::ldexp(float(mant | (1u << mantissaBits)), int(exp) - 15 - mantissaBits);
}

}   // namespace

// JsA_CloudData float fields in template order (names matched against eboot strings).
const char* const kCloudFields[kCloudFieldCount] = {
    "colorScaleX", "colorScaleY", "colorScaleZ", "alphaScaleX", "gameColorScaleX", "gameColorScaleY",
    "gameColorScaleZ", "gameAlphaScaleX", "scatteringDirectionX", "dirLightNormalDecayX",
    "dirLightNormalDecayInfluenceX", "dirLightIncidenseTransmittanceX", "dirLightIncidenseBeginX", "ambLightMinX",
    "noiseUvOffsetDirectionX", "noiseUvOffsetDirectionY", "noiseUvOffsetDirectionZ", "noiseUvOffsetX",
    "noiseUvScaleX", "raymarchUvDistanceX", "shadingBaseNoiseScaleX", "shadingNoiseOctaveScaleX",
    "shadingNoiseOctaveScaleY", "shadingNoiseOctaveScaleZ", "noiseBumpX", "noiseBumpContrastMinX",
    "noiseBumpContrastMaxX", "edgeBaseNoiseScaleX", "edgeNoiseOctaveScaleX", "edgeNoiseOctaveScaleY",
    "edgeNoiseOctaveScaleZ", "edgeAlphaContrastMinX", "edgeAlphaContrastMaxX", "edgeThicknessX",
    "softDepthDistanceX", "vertexDisplacementOffsetX", "vertexDisplacementDistanceX",
    "vertexDisplacementMipLevelX", "distanceFadeEnableX", "distanceFadeBeginX", "distanceFadeEndX"};

namespace {

struct Attribute {
    std::uint32_t format, semantic, offset;
};

// Material templates: MTTB = u32 count, 12 bytes zero, then per template
// { u32 typeHash, u32 hash, u32 fieldCount, u32 0, fieldCount x 16-byte field record }.
std::map<std::uint32_t, Schema> parseTemplates(ByteView mttb) {
    std::map<std::uint32_t, Schema> out;
    std::uint32_t count = u32(mttb, 0);
    std::size_t at = 16;
    for (std::uint32_t t = 0; t < count && at + 16 <= mttb.size(); ++t) {
        Schema s;
        s.typeHash = u32(mttb, at);
        std::uint32_t fields = u32(mttb, at + 8);
        at += 16;
        for (std::uint32_t i = 0; i < fields && at + 16 <= mttb.size(); ++i, at += 16) {
            s.fields.push_back({u32(mttb, at), u32(mttb, at + 4), u16(mttb, at + 8), u32(mttb, at + 12)});
        }
        out[s.typeHash] = std::move(s);
    }
    // Lay out embedded structures first (the common block), then everything else.
    auto structSize = [](std::uint32_t typeHash, const void* ctx) -> std::uint32_t {
        const auto& all = *static_cast<const std::map<std::uint32_t, Schema>*>(ctx);
        auto it = all.find(typeHash);
        if (it == all.end()) {
            return 0;
        }
        Schema copy = it->second;
        std::uint32_t end = copy.layOut();
        return (end + 7) / 8 * 8;
    };
    for (auto& [hash, schema] : out) {
        schema.layOut(structSize, &out);
    }
    return out;
}

void identity(std::array<float, 16>& m) {
    m.fill(0);
    m[0] = m[5] = m[10] = m[15] = 1;
}

}   // namespace

std::map<std::uint32_t, Schema> materialTemplates(const JxmFile& file) {
    return parseTemplates(file.blob("MTTB"));
}

std::optional<Model> buildModel(const JxmFile& file, std::string* error) {
    auto fail = [&](const std::string& message) -> std::optional<Model> {
        if (error) {
            *error = message;
        }
        return std::nullopt;
    };
    const Table* vbdt = file.table("VBDT");
    const Table* ibdt = file.table("IBDT");
    const Table* geom = file.table("GEOM");
    const Table* subm = file.table("SUBM");
    const Table* mesh = file.table("MESH");
    if (!vbdt || !ibdt || !geom || !subm || !mesh) {
        return fail("model has no meshes");
    }
    ByteView vbbd = file.blob("VBBD"), ibbd = file.blob("IBBD");
    Model model;
    if (const Table* header = file.table("JXMD")) {
        model.name = std::string(file.string(header->integer(0, fnv1a("m_modelString"))));
    }

    // Vertex buffers -> canonical vertices.
    std::vector<std::uint32_t> vbBase;
    std::vector<std::uint8_t> skinnedVertex;   // has blend attributes (indices still cluster-local)
    for (std::uint32_t v = 0; v < vbdt->rowCount; ++v) {
        auto formats = static_cast<std::uint64_t>(vbdt->integer(v, fnv1a("m_vertexFormat"), 0));
        auto semantics = static_cast<std::uint64_t>(vbdt->integer(v, fnv1a("m_vertexSemantics"), 0));
        auto offset = static_cast<std::size_t>(vbdt->integer(v, fnv1a("m_offset"), 0));
        auto count = static_cast<std::uint32_t>(vbdt->integer(v, fnv1a("m_vertexCount"), 0));
        auto stride = static_cast<std::uint32_t>(vbdt->integer(v, fnv1a("m_stride"), 0));
        // Attribute offsets: one byte per attribute (in 4-byte words) in m_vertexOffsetInWords0
        // (attributes 0-7) and ...1 (8-15). Zero means the attributes are packed in order. Explicit
        // offsets may repeat: attributes can share data (e.g. two UV sets from one stream).
        auto words0 = static_cast<std::uint64_t>(vbdt->integer(v, fnv1a("m_vertexOffsetInWords0"), 0));
        auto words1 = static_cast<std::uint64_t>(vbdt->integer(v, fnv1a("m_vertexOffsetInWords1"), 0));
        std::vector<Attribute> attrs;
        std::uint32_t pos = 0;
        for (std::uint32_t a = 0; formats; formats >>= 4, semantics >>= 4, ++a) {
            std::uint32_t f = formats & 0xf, s = semantics & 0xf, size = formatSize(f);
            if (!size) {
                return fail("unknown vertex attribute format " + std::to_string(f));
            }
            std::uint32_t at = pos;
            if (words0 != 0) {
                at = std::uint32_t(((a < 8 ? words0 >> (a * 8) : words1 >> ((a - 8) * 8)) & 0xff) * 4);
            }
            if (at + size > stride) {
                return fail("vertex attribute outside its stride");
            }
            attrs.push_back({f, s, at});
            pos += size;
        }
        if (std::getenv("BEACON_MODEL_VERTEX_LAYOUT")) {   // research aid: the raw attribute list
            std::fprintf(stderr, "vertex buffer %u: %u vertices, stride %u:", v, count, stride);
            for (const auto& a : attrs) {
                std::fprintf(stderr, " [fmt %u sem %u @%u]", a.format, a.semantic, a.offset);
            }
            std::fprintf(stderr, "\n");
        }
        if ((words0 == 0 && pos != stride) || offset + std::size_t(count) * stride > vbbd.size()) {
            return fail("vertex buffer layout does not match its stride");
        }
        vbBase.push_back(std::uint32_t(model.vertices.size()));
        for (std::uint32_t i = 0; i < count; ++i) {
            ByteView src = vbbd.subspan(offset + std::size_t(i) * stride, stride);
            ModelVertex out{};
            out.normal[2] = 1;
            out.tangent[0] = out.tangent[3] = 1;
            std::memset(out.color, 255, 4);
            out.weights[0] = 1;   // palette entry 0 (identity) until skinning assigns joints
            bool haveUv = false, haveTangent = false, haveBlend = false;
            float halfColors[12] = {};
            int halfColorCount = 0;
            for (const auto& a : attrs) {
                if (a.semantic == kSemanticPosition && a.format == kFormatFloat3) {
                    std::memcpy(out.position, src.data() + a.offset, 12);
                } else if (a.semantic == kSemanticNormal && a.format == kFormatPackedNormal) {
                    std::uint32_t w = u32(src, a.offset);
                    out.normal[0] = smallFloat(w & 0x7ff, 6) * 2 - 1;
                    out.normal[1] = smallFloat((w >> 11) & 0x7ff, 6) * 2 - 1;
                    out.normal[2] = smallFloat(w >> 22, 5) * 2 - 1;
                } else if (a.semantic == kSemanticTangent && a.format == kFormatHalf4 && !haveTangent) {
                    for (int c = 0; c < 4; ++c) {
                        out.tangent[c] = halfToFloat(u16(src, a.offset + c * 2));
                    }
                    haveTangent = true;
                } else if (a.semantic == kSemanticTexcoord && a.format == kFormatFloat2 && !haveUv) {
                    std::memcpy(out.uv, src.data() + a.offset, 8);
                    haveUv = true;
                } else if (a.semantic == kSemanticColor && a.format == kFormatUnorm8x4) {
                    std::memcpy(out.color, src.data() + a.offset, 4);
                } else if (a.semantic == kSemanticColor && a.format == kFormatHalf4 && halfColorCount < 3) {
                    for (int c = 0; c < 4; ++c) {
                        halfColors[halfColorCount * 4 + c] = halfToFloat(u16(src, a.offset + c * 2));
                    }
                    ++halfColorCount;
                } else if (a.semantic == kSemanticBlendIndices && a.format == kFormatUint16x4) {
                    for (int c = 0; c < 4; ++c) {
                        out.joints[c] = u16(src, a.offset + c * 2);   // cluster-local for now
                    }
                    haveBlend = true;
                } else if (a.semantic == kSemanticBlendWeights && a.format == kFormatHalf4) {
                    for (int c = 0; c < 4; ++c) {
                        out.weights[c] = halfToFloat(u16(src, a.offset + c * 2));
                    }
                }
            }
            if (halfColorCount > 0 || !model.viewValues.empty()) {
                model.viewValues.resize(model.vertices.size() * 12, 0.0f);   // earlier vertices without
                model.viewValues.insert(model.viewValues.end(), halfColors, halfColors + 12);
            }
            if (!haveBlend) {
                std::memset(out.joints, 0, sizeof out.joints);
                std::memset(out.weights, 0, sizeof out.weights);
                out.weights[0] = 1;
            }
            skinnedVertex.push_back(haveBlend);
            model.vertices.push_back(out);
        }
    }

    // Index buffers -> one 32-bit index list.
    std::vector<std::uint32_t> ibBase;
    for (std::uint32_t b = 0; b < ibdt->rowCount; ++b) {
        auto offset = static_cast<std::size_t>(ibdt->integer(b, fnv1a("m_offset"), 0));
        auto count = static_cast<std::uint32_t>(ibdt->integer(b, fnv1a("m_indexCount"), 0));
        // m_indexFormat: 2 = 16-bit, 1 = 32-bit (all 5,497 index buffers agree with their byte size).
        std::uint32_t size = ibdt->integer(b, fnv1a("m_indexFormat"), 2) == 1 ? 4 : 2;
        if (offset + std::size_t(count) * size > ibbd.size()) {
            return fail("index buffer truncated");
        }
        ibBase.push_back(std::uint32_t(model.indices.size()));
        for (std::uint32_t i = 0; i < count; ++i) {
            model.indices.push_back(size == 4 ? u32(ibbd, offset + i * 4) : u16(ibbd, offset + i * 2));
        }
    }

    // Materials.
    const Table* mati = file.table("MATI");
    const Table* texi = file.table("TEXI");
    const Table* ctex = file.table("CTEX");
    ByteView matd = file.blob("MATD");
    auto templates = parseTemplates(file.blob("MTTB"));
    auto textureRef = [&](std::int64_t slot) {
        TextureRef ref;
        if (!texi || slot < 0 || slot >= texi->rowCount) {
            return ref;
        }
        std::int64_t nameIndex = texi->integer(std::uint32_t(slot), fnv1a("m_textureNameIndex"));
        ref.name = std::string(file.string(nameIndex));
        if (const std::int64_t node = texi->integer(std::uint32_t(slot), fnv1a("m_textureNodeIndex")); node >= 0) {
            ref.nodeHash = fnv1a(std::string(file.string(node)));
        }
        for (std::uint32_t c = 0; ctex && c < ctex->rowCount; ++c) {
            if (ctex->integer(c, fnv1a("m_name")) == nameIndex) {
                ref.constant = true;
                ref.colorRgba = std::uint32_t(ctex->integer(c, fnv1a("m_colorRGBA"), 0));
            }
        }
        return ref;
    };
    for (std::uint32_t m = 0; mati && m < mati->rowCount; ++m) {
        ModelMaterial mat;
        mat.name = std::string(file.string(mati->integer(m, fnv1a("m_materialName"))));
        mat.templateHash = std::uint32_t(mati->integer(m, fnv1a("m_dataTypeHash"), 0));
        mat.nameHash = fnv1a(mat.name);
        mat.groupHash = fnv1a(mat.name + "SG");
        auto offset = static_cast<std::size_t>(mati->integer(m, fnv1a("m_parameterOffset"), 0));
        auto size = static_cast<std::size_t>(mati->integer(m, fnv1a("m_parameterSize"), 0));
        auto it = templates.find(mat.templateHash);
        if (auto common = templates.find(fnv1a("MaterialCommonData"));
            common != templates.end() && offset + size <= matd.size()) {
            const Schema& cs = common->second;
            ByteView params = matd.subspan(offset, size);
            auto field = [&](const char* name) -> const SchemaField* {
                const SchemaField* f = cs.find(fnv1a(name));
                return f && f->offset + f->size <= params.size() ? f : nullptr;
            };
            if (const SchemaField* f = field("m_blendStateMask")) {
                mat.blendState = read<std::uint64_t>(params, f->offset);
            }
            if (const SchemaField* f = field("m_materialFlags")) {
                mat.materialFlags = std::uint32_t(readNumber(params, *f));
            }
            if (const SchemaField* f = field("m_depthOffset")) {
                mat.depthOffset = float(readNumber(params, *f));
            }
            if (const SchemaField* f = field("m_cullFace")) {
                mat.cullFace = std::uint8_t(readNumber(params, *f));
            }
            if (const SchemaField* f = field("m_zWrite")) {
                mat.zWrite = std::uint8_t(readNumber(params, *f));
            }
            if (const SchemaField* f = field("m_zTest")) {
                mat.zTest = std::uint8_t(readNumber(params, *f));
            }
            if (const SchemaField* f = field("m_priority")) {
                mat.priority = std::int8_t(readNumber(params, *f));
            }
        }
        if (it != templates.end() && offset + size <= matd.size()) {
            const Schema& s = it->second;
            ByteView params = matd.subspan(offset, size);
            auto num = [&](const char* name, double fallback) {
                const SchemaField* f = s.find(fnv1a(name));
                return f && f->offset + f->size <= params.size() ? readNumber(params, *f) : fallback;
            };
            auto slot = [&](std::initializer_list<const char*> names) {
                for (const char* n : names) {
                    double v = num(n, -1);
                    if (v >= 0 && v < 65535) {
                        return textureRef(std::int64_t(v));
                    }
                }
                return TextureRef{};
            };
            mat.base = slot({"base0", "colorTex", "baseColor", "texture0"});
            mat.normal = slot({"normal0"});
            mat.roughness = slot({"roughness0"});
            mat.metallic = slot({"metallic0"});
            mat.emissive = slot({"emissive0"});
            mat.occlusion = slot({"occlusion0"});
            mat.baseDetail = slot({"baseDetail0"});
            mat.normalDetail = slot({"normDetail0"});
            mat.roughnessDetail = slot({"roughDetail0"});
            mat.aoMap = slot({"aoMapTexture"});
            mat.detailScale = float(num("detailScale0X", 1));
            mat.detailWorldScaling = num("detailMapWorldScalingX", 0) != 0;
            mat.color[0] = float(num("color0X", 1));
            mat.color[1] = float(num("color0Y", 1));
            mat.color[2] = float(num("color0Z", 1));
            mat.uvScale[0] = float(num("uvScale0X", 1));
            mat.uvScale[1] = float(num("uvScale0Y", 1));
            mat.uvOffset[0] = float(num("uvOffset0X", 0));
            mat.uvOffset[1] = float(num("uvOffset0Y", 0));
            mat.alphaThreshold = float(num("alphaThresholdX", 0));
            mat.normalIntensity = float(num("normalmapIntensity0X", 1));
            const float emissiveScale = float(num("emissiveScale0X", 1) * num("gameEmissiveScaleX", 1));
            mat.emissiveColor[0] = float(num("emissiveColor0X", 0)) * emissiveScale;
            mat.emissiveColor[1] = float(num("emissiveColor0Y", 0)) * emissiveScale;
            mat.emissiveColor[2] = float(num("emissiveColor0Z", 0)) * emissiveScale;
            mat.incidence[0] = float(num("incidenceMin0X", 1));
            mat.incidence[1] = float(num("incidenceMax0X", 1));
            mat.incidence[2] = float(num("incidencePower0X", 1));
            mat.gameColor[0] = float(num("gameColorScaleX", 1));
            mat.gameColor[1] = float(num("gameColorScaleY", 1));
            mat.gameColor[2] = float(num("gameColorScaleZ", 1));
            mat.alphaScale = float(num("alphaScaleX", 1) * num("gameAlphaScaleX", 1));
            // The parts clip channels replace (alphaScale, emissiveScale0).
            mat.gameAlphaScale = float(num("gameAlphaScaleX", 1));
            mat.emissiveScale = float(num("emissiveScale0X", 1));
            mat.fakeScattering = float(num("fakeScatteringRatioX", 0));
            if (mat.templateHash == fnv1a("A_PT_EffectData") || mat.templateHash == fnv1a("JsA_PT_EffectData")) {
                mat.effect = true;
                mat.base = slot({"color"});
                mat.baseColor = slot({"baseColor"});
                auto uv = [&](const char* tex, float out[5]) {
                    const std::string t(tex);
                    out[0] = float(num(("RepeatUV" + t + "X").c_str(), 1));
                    out[1] = float(num(("RepeatUV" + t + "Y").c_str(), 1));
                    out[2] = float(num(("OffsetUV" + t + "X").c_str(), 0));
                    out[3] = float(num(("OffsetUV" + t + "Y").c_str(), 0));
                    out[4] = float(num(("RotateUV" + t + "X").c_str(), 0));
                };
                uv("color", mat.uvColor);
                uv("baseColor", mat.uvBase);
                const float intensity = float(num("colorScaleX", 1));
                mat.effectColor[0] = intensity * float(num("colorScaleRX", 1) * num("gameColorScaleX", 1));
                mat.effectColor[1] = intensity * float(num("colorScaleGX", 1) * num("gameColorScaleY", 1));
                mat.effectColor[2] = intensity * float(num("colorScaleBX", 1) * num("gameColorScaleZ", 1));
                mat.effectColor[3] = float(num("alphaScaleX", 1) * num("gameAlphaScaleX", 1));
                mat.gameAlphaScale = float(num("gameAlphaScaleX", 1));
                mat.colorScaleRgb[0] = float(num("colorScaleRX", 1));
                mat.colorScaleRgb[1] = float(num("colorScaleGX", 1));
                mat.colorScaleRgb[2] = float(num("colorScaleBX", 1));
                mat.gameColor[0] = float(num("gameColorScaleX", 1));
                mat.gameColor[1] = float(num("gameColorScaleY", 1));
                mat.gameColor[2] = float(num("gameColorScaleZ", 1));
                mat.fresnel[0] = float(num("fresnelBorderLX", 0));
                mat.fresnel[1] = float(num("fresnelBorderHX", 1));
                mat.billboard = num("billboardFaceEyeX", 0) != 0;
                mat.billboardFixRoll = num("billboardFixRollX", 0) != 0;
                mat.zOffset = float(num("zOffsetX", 0));
                mat.softDepth = float(num("softDepthDistanceX", 0));
            }
            if (mat.templateHash == fnv1a("JsA_GlassData")) {
                // Glass body: incidence = (thicknessMax, lightAbsorption, lightScattering,
                // normalScale), the thickness map in the occlusion slot.
                mat.glass = 1;
                mat.base = slot({"baseMap"});
                mat.normal = slot({"normalMap"});
                mat.occlusion = slot({"thicknessMap"});
                const float i = float(num("colScaleIX", 1));
                mat.color[0] = float(num("colorScaleX", 1) * num("colScaleRX", 1) * num("gameColorScaleX", 1)) * i;
                mat.color[1] = float(num("colorScaleY", 1) * num("colScaleGX", 1) * num("gameColorScaleY", 1)) * i;
                mat.color[2] = float(num("colorScaleZ", 1) * num("colScaleBX", 1) * num("gameColorScaleZ", 1)) * i;
                mat.incidence[0] = float(num("thicknessMaxX", 1));
                mat.incidence[1] = float(num("lightAbsorptionX", 0));
                mat.incidence[2] = float(num("lightScatteringX", 0));
                mat.normalIntensity = float(num("normalScaleX", 1));
                mat.uvScale[0] = float(num("RepeatUVbaseMapX", 1));
                mat.uvScale[1] = float(num("RepeatUVbaseMapY", 1));
                mat.uvOffset[0] = float(num("OffsetUVbaseMapX", 0));
                mat.uvOffset[1] = float(num("OffsetUVbaseMapY", 0));
            }
            if (mat.templateHash == fnv1a("JsA_GlassSpecularData")) {
                mat.glass = 2;
                mat.base = TextureRef{};
                mat.normal = slot({"normalMap"});
                mat.roughness = slot({"roughnessMap"});
                mat.metallic = slot({"metallicMap"});
                mat.incidence[0] = float(num("incidenceMinX", 1));
                mat.incidence[1] = float(num("incidenceMaxX", 1));
                mat.incidence[2] = float(num("incidencePowerX", 1));
                mat.normalIntensity = float(num("normalIntensityX", 1));
                mat.uvScale[0] = float(num("RepeatUVnormalMapX", 1));
                mat.uvScale[1] = float(num("RepeatUVnormalMapY", 1));
                mat.uvOffset[0] = float(num("OffsetUVnormalMapX", 0));
                mat.uvOffset[1] = float(num("OffsetUVnormalMapY", 0));
            }
            if (mat.templateHash == fnv1a("JsA_LensFlareData")) {
                // The flare shader (fs 0x15df1c73): the effect shader without the Fresnel fade
                // and the base colour texture.
                mat.effect = true;
                mat.base = slot({"color"});
                mat.uvColor[0] = float(num("RepeatUVcolorX", 1));
                mat.uvColor[1] = float(num("RepeatUVcolorY", 1));
                mat.uvColor[2] = float(num("OffsetUVcolorX", 0));
                mat.uvColor[3] = float(num("OffsetUVcolorY", 0));
                mat.uvColor[4] = float(num("RotateUVcolorX", 0));
                const float intensity = float(num("colorScaleX", 1));
                mat.effectColor[0] = intensity * float(num("colorScaleRX", 1) * num("gameColorScaleX", 1));
                mat.effectColor[1] = intensity * float(num("colorScaleGX", 1) * num("gameColorScaleY", 1));
                mat.effectColor[2] = intensity * float(num("colorScaleBX", 1) * num("gameColorScaleZ", 1));
                mat.effectColor[3] = float(num("alphaScaleX", 1) * num("gameAlphaScaleX", 1));
                mat.gameAlphaScale = float(num("gameAlphaScaleX", 1));
                mat.fresnel[0] = -1;   // no fade: the factor is 1 for every N.V >= 0
                mat.fresnel[1] = 0;
                mat.billboard = num("billboardFaceEyeX", 0) != 0;
                mat.billboardFixRoll = num("billboardFixRollX", 0) != 0;
                mat.sky = num("isSkyX", 0) != 0;
            }
            if (mat.templateHash == fnv1a("JsA_CloudData")) {
                mat.cloud = true;
                mat.base = slot({"color"});
                mat.noise = slot({"noiseMap"});
                for (const char* f : kCloudFields) {
                    mat.cloudParams.push_back(float(num(f, 0)));
                }
            }
            if (mat.templateHash == fnv1a("JsA_SkyData")) {
                // The sky: its "color" texture times colorScaleRGB x colorScaleI, unlit.
                mat.unlit = true;
                mat.base = slot({"color"});
                const float intensity = float(num("colorScaleIX", 1));
                mat.color[0] = float(num("colorScaleRX", 1)) * intensity;
                mat.color[1] = float(num("colorScaleGX", 1)) * intensity;
                mat.color[2] = float(num("colorScaleBX", 1)) * intensity;
                mat.uvScale[0] = float(num("RepeatUVcolorX", 1));
                mat.uvScale[1] = float(num("RepeatUVcolorY", 1));
                mat.uvOffset[0] = float(num("OffsetUVcolorX", 0));
                mat.uvOffset[1] = float(num("OffsetUVcolorY", 0));
            }
        }
        model.materials.push_back(std::move(mat));
    }

    std::vector<std::uint32_t> drawMesh;   // MeshData row of each draw
    // Draws: one per sub-mesh. Vertices are stored in model space; the sub-mesh's m_00..m_33 matrix
    // with m_center*/m_width/m_height/m_depth describes an oriented bounding box, not a placement.
    for (std::uint32_t s = 0; s < subm->rowCount; ++s) {
        auto meshIndex = subm->integer(s, fnv1a("m_meshIndex"));
        if (meshIndex < 0 || meshIndex >= mesh->rowCount) {
            continue;
        }
        auto geomIndex = mesh->integer(std::uint32_t(meshIndex), fnv1a("m_geomIndex"));
        if (geomIndex < 0 || geomIndex >= geom->rowCount) {
            continue;
        }
        auto g = std::uint32_t(geomIndex);
        auto vb = geom->integer(g, fnv1a("m_vbIndex"), 0), ib = geom->integer(g, fnv1a("m_ibIndex"), 0);
        if (vb < 0 || vb >= std::int64_t(vbBase.size()) || ib < 0 || ib >= std::int64_t(ibBase.size())) {
            continue;
        }
        ModelDraw d;
        d.firstIndex = ibBase[ib] + std::uint32_t(geom->integer(g, fnv1a("m_ibOffset"), 0));
        d.indexCount = std::uint32_t(geom->integer(g, fnv1a("m_indexCount"), 0));
        d.vertexOffset = std::int32_t(vbBase[vb] + geom->integer(g, fnv1a("m_vbOffset"), 0));
        d.material = std::int32_t(subm->integer(s, fnv1a("m_materialIndex")));
        d.category = std::uint32_t(subm->integer(s, fnv1a("m_categoryName"), 0));
        identity(d.transform);
        if (d.firstIndex + d.indexCount > model.indices.size()) {
            return fail("draw range outside the index buffer");
        }
        model.draws.push_back(d);
        drawMesh.push_back(std::uint32_t(meshIndex));
    }

    // Skinning palette. Entry 0 is the identity; each skin cluster contributes its joints with their
    // inverse bind matrices; rigid meshes get one entry for the node they hang on.
    model.palette.push_back({});
    const Table* tint = file.table("TINT");
    const Table* jint = file.table("JINT");
    const Table* sclt = file.table("SCLT");
    const Table* mesn = file.table("MESN");
    ByteView bind = file.blob("BIND");
    auto nodeHash = [&](std::int64_t node) -> std::uint32_t {
        if (!tint || node < 0 || node >= tint->rowCount) {
            return 0;
        }
        return fnv1a(file.string(tint->integer(std::uint32_t(node), fnv1a("m_transformName"))));
    };
    std::vector<std::uint32_t> clusterBase;
    for (std::uint32_t c = 0; sclt && jint && c < sclt->rowCount; ++c) {
        clusterBase.push_back(std::uint32_t(model.palette.size()));
        auto first = sclt->integer(c, fnv1a("m_jointOffset"), 0), count = sclt->integer(c, fnv1a("m_jointCount"), 0);
        for (std::int64_t k = first; k < first + count && k < jint->rowCount; ++k) {
            PaletteEntry e;
            e.jointHash = nodeHash(jint->integer(std::uint32_t(k), fnv1a("m_localIndex")));
            if (std::size_t(k + 1) * 64 <= bind.size()) {
                e.hasBind = true;
                std::memcpy(e.bind.data(), bind.data() + k * 64, 64);
            }
            model.palette.push_back(e);
        }
    }
    std::map<std::uint32_t, std::uint16_t> rigidEntry;   // node hash -> palette index
    std::vector<std::int32_t> assigned(model.vertices.size(), -1);
    for (std::size_t i = 0; i < model.draws.size(); ++i) {
        const ModelDraw& d = model.draws[i];
        const std::uint32_t m = drawMesh[i];
        auto cluster = mesh->integer(m, fnv1a("m_modifierIndex"), -1);
        std::int32_t base = -1;   // palette base for skinned vertices
        std::uint16_t rigid = 0;
        if (cluster >= 0 && cluster < std::int64_t(clusterBase.size())) {
            base = std::int32_t(clusterBase[cluster]);
        } else if (mesn) {
            // Rigid mesh: find its mesh node by shape name and attach it to that node.
            auto shape = mesh->integer(m, fnv1a("m_shapeName"), -1);
            for (std::uint32_t n = 0; n < mesn->rowCount; ++n) {
                if (mesn->integer(n, fnv1a("m_meshNameIndex"), -2) == shape) {
                    std::uint32_t h = nodeHash(mesn->integer(n, fnv1a("m_transformIndex"), -1));
                    if (h) {
                        auto [it, inserted] = rigidEntry.emplace(h, std::uint16_t(model.palette.size()));
                        if (inserted) {
                            model.palette.push_back({h, false, math::identity()});
                        }
                        rigid = it->second;
                    }
                    break;
                }
            }
        }
        for (std::uint32_t k = 0; k < d.indexCount; ++k) {
            std::size_t vi = std::size_t(d.vertexOffset) + model.indices[d.firstIndex + k];
            if (vi >= model.vertices.size() || assigned[vi] >= 0) {
                continue;
            }
            ModelVertex& v = model.vertices[vi];
            if (base >= 0 && skinnedVertex[vi]) {
                for (int c = 0; c < 4; ++c) {
                    v.joints[c] = v.weights[c] > 0 ? std::uint16_t(base + v.joints[c]) : 0;
                }
            } else {
                std::memset(v.joints, 0, sizeof v.joints);
                std::memset(v.weights, 0, sizeof v.weights);
                v.joints[0] = rigid;
                v.weights[0] = 1;
            }
            assigned[vi] = base >= 0 ? base : rigid;
        }
    }
    for (std::size_t vi = 0; vi < model.vertices.size(); ++vi) {
        if (assigned[vi] < 0) {   // not drawn: keep it on the identity entry
            ModelVertex& v = model.vertices[vi];
            std::memset(v.joints, 0, sizeof v.joints);
            std::memset(v.weights, 0, sizeof v.weights);
            v.weights[0] = 1;
        }
        for (std::uint16_t j : model.vertices[vi].joints) {
            if (j >= model.palette.size()) {
                return fail("blend index outside the skin cluster");
            }
        }
    }

    // Bounds over the transformed vertices of the opaque draws.
    bool first = true;
    for (const auto& d : model.draws) {
        for (std::uint32_t i = 0; i < d.indexCount; ++i) {
            std::size_t vi = std::size_t(d.vertexOffset) + model.indices[d.firstIndex + i];
            if (vi >= model.vertices.size()) {
                return fail("index outside the vertex buffer");
            }
            const float* p = model.vertices[vi].position;
            const auto& m = d.transform;
            float w[3];
            for (int r = 0; r < 3; ++r) {
                w[r] = m[r] * p[0] + m[4 + r] * p[1] + m[8 + r] * p[2] + m[12 + r];
            }
            for (int c = 0; c < 3; ++c) {
                model.boundsMin[c] = first ? w[c] : std::min(model.boundsMin[c], w[c]);
                model.boundsMax[c] = first ? w[c] : std::max(model.boundsMax[c], w[c]);
            }
            first = false;
        }
    }
    if (const Table* ligi = file.table("LIGI")) {
        const ByteView params = file.blob("LIGD");
        const Table* textures = file.table("TEXI");
        for (std::uint32_t r = 0; r < ligi->rowCount; ++r) {
            ModelLight light{std::uint32_t(ligi->integer(r, fnv1a("m_lightType"))),
                             std::string(file.string(ligi->integer(r, fnv1a("m_lightName")))), {}};
            const auto at = std::size_t(ligi->integer(r, fnv1a("m_parameterOffset"), 0));
            if (light.type == kModelLightIbl && textures && at + 0x20 <= params.size()) {
                const std::uint16_t node = std::uint16_t(params[at + 0x1e] | params[at + 0x1f] << 8);
                const std::string path = std::string(file.string(node)) + "_tex";
                for (std::uint32_t t = 0; t < textures->rowCount; ++t) {
                    if (file.string(textures->integer(t, fnv1a("m_textureNodeIndex"))) == path) {
                        light.texture = std::string(file.string(textures->integer(t, fnv1a("m_textureNameIndex"))));
                    }
                }
            }
            model.lights.push_back(std::move(light));
        }
    }
    return model;
}

}   // namespace data
