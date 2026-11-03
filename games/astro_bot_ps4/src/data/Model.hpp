// A model as the renderer needs it, built from a JXMD file: canonical vertices, draws (one per
// sub-mesh) with their node transform and render category, and materials with their textures.
//
// Source structure (chunk / table type / fields by name, see Jxm.hpp):
//   VBDT VertexBufferData   m_vertexFormat/m_vertexSemantics (one nibble per attribute),
//                           m_offset into the VBBD blob, m_vertexCount, m_stride
//   IBDT IndexBufferData    m_offset into IBBD, m_indexCount, m_indexFormat
//   GEOM GeometryData       m_vbIndex, m_ibIndex, m_vbOffset, m_ibOffset, m_indexCount
//   SUBM ModelSubMeshData   m_meshIndex, m_materialIndex, m_categoryName (hash: "Opaque",
//                           "Shadowmap", ...); m_center*, m_width/height/depth and m_00..m_33 form
//                           an oriented bounding box (vertices are already in model space)
//   MESH MeshData           m_geomIndex, m_materialIndex
//   MATI MaterialInstance   m_dataTypeHash (template), m_parameterOffset/Size into MATD
//   MTTB                    material templates (schemas) of the parameter blocks
//   TEXI ModelTextureInfo   m_textureNameIndex; CTEX ConstantTextureInfo m_colorRGBA for
//                           generated single-colour textures ("sytx_..." names)
//   TINT TransformData      node hierarchy (m_transformName, m_parentIndex)
//   SCLT SkinClusterData    m_jointOffset/m_jointCount into JINT JointData (m_localIndex = node);
//                           BIND holds one inverse bind matrix per JINT row. MeshData's
//                           m_modifierIndex selects the cluster; vertex blend indices are
//                           cluster-local. MeshNodeData m_transformIndex gives a mesh's node.
#pragma once

#include "data/Jxm.hpp"
#include "math/Transform.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace beacon::data {

struct ModelVertex {
    float position[3];
    float normal[3];
    float tangent[4];   // xyz, w = bitangent sign
    float uv[2];
    std::uint8_t color[4];
    std::uint16_t joints[4];   // indices into Model::palette
    float weights[4];
};

// One skinning matrix slot. At run time its matrix is World(joint) * bind, where `bind` is the
// inverse bind matrix of a skin-cluster joint (BIND chunk, column-major). Rigid meshes attached to
// a node are in that node's space and use World(node) (hasBind = false).
struct PaletteEntry {
    std::uint32_t jointHash = 0;   // fnv1a of the node/joint name (matches skeleton joint hashes)
    bool hasBind = false;
    math::Mat4 bind = math::identity();
};

struct TextureRef {
    std::string name;              // texture file stem, empty if the slot is unused
    bool constant = false;         // generated single-colour texture
    std::uint32_t colorRgba = 0;   // constant colour, bytes R G B A in memory order (m_colorRGBA)
    std::uint32_t nodeHash = 0;    // fnv1a of the texture node's name (clip channels offsetU/V address it)
};

struct ModelMaterial {
    std::string name;
    std::uint32_t nameHash = 0, groupHash = 0;   // fnv1a of the name and of name + "SG" (shading group)
    // MaterialCommonData (every template's m_common block): render state.
    std::uint64_t blendState = 0;   // m_blendStateMask (decoded in rendering.md "Material render state")
    std::uint32_t materialFlags = 0;
    float depthOffset = 0;
    std::uint8_t cullFace = 0, zWrite = 1, zTest = 1;
    std::int8_t priority = 0;
    std::uint32_t templateHash = 0;              // fnv1a of the template, e.g. "JsPBRSingleData"
    TextureRef base, normal, roughness, metallic, emissive, occlusion;
    TextureRef baseDetail, normalDetail, roughnessDetail, aoMap;   // baseDetail0, normDetail0, roughDetail0, aoMapTexture
    float detailScale = 1;          // detailScale0
    bool detailWorldScaling = false;   // detailMapWorldScaling
    float color[3] = {1, 1, 1};
    float uvScale[2] = {1, 1}, uvOffset[2] = {0, 0};
    float alphaThreshold = 0;
    float normalIntensity = 1;
    // JsPBRSingleData lighting inputs (defaults where a template lacks them).
    float emissiveColor[3] = {0, 0, 0};   // emissiveColor0 x emissiveScale0 x gameEmissiveScale
    float incidence[3] = {1, 1, 1};       // incidenceMin0 / Max0 / Power0
    float gameColor[3] = {1, 1, 1};       // gameColorScale
    float alphaScale = 1;                 // alphaScale x gameAlphaScale
    float emissiveScale = 1;              // emissiveScale0 alone (emissiveColor holds the product)
    float fakeScattering = 0;             // fakeScatteringRatio
    bool unlit = false;                   // JsA_SkyData: the colour texture x colorScale, no lighting
    int glass = 0;                        // 1 JsA_GlassData (body), 2 JsA_GlassSpecularData (specular pass)
    // JsA_CloudData: the colour texture (base), the 3D noise and the template's float fields in
    // template order (kCloudFields).
    bool cloud = false;
    TextureRef noise;
    std::vector<float> cloudParams;
    // A_PT_EffectData (effects: the hover laser, decals): colour (base) and base colour textures,
    // each with Repeat / Offset / Rotate UV; colour scales, Fresnel fade, billboard.
    bool effect = false;
    TextureRef baseColor;
    float uvColor[5] = {1, 1, 0, 0, 0};   // RepeatUV x y, OffsetUV x y, RotateUV
    float uvBase[5] = {1, 1, 0, 0, 0};
    float effectColor[4] = {1, 1, 1, 1};   // colorScale x colorScaleRGB x gameColorScale, alphaScale x gameAlphaScale
    float gameAlphaScale = 1;
    float colorScaleRgb[3] = {1, 1, 1};   // the effect's colorScaleRGB alone (code may replace it)
    float fresnel[2] = {0, 1};            // fresnelBorderL, H
    bool billboard = false, billboardFixRoll = false;
    bool sky = false;   // JsA_LensFlareData isSky: drawn camera-relative in the sky
    float zOffset = 0, softDepth = 0;
};

struct ModelDraw {
    std::uint32_t firstIndex = 0, indexCount = 0;
    std::int32_t vertexOffset = 0;
    std::int32_t material = -1;
    std::uint32_t category = 0;          // fnv1a of the render category name
    std::array<float, 16> transform{};   // column-major for column vectors; identity for now (node
                                         // animation will drive it via m_transformIndex)
};

// A light the model carries (LIGI table, LightInstance rows: m_lightType, m_lightName, m_dataTypeHash,
// m_parameterOffset / m_parameterSize into the LIGD blob). Type 6 is the image-based light of the
// sky models. Its parameters (176 bytes) hold at +0x1c / +0x1e u16 string indices of its name and
// its node path ("/JsCubemapLocator01/<name>"); its cube is the texture (TEXI) placed on the node
// path + "_tex" (checked on four sky models; the light's name is not always the texture's).
struct ModelLight {
    std::uint32_t type = 0;
    std::string name;
    std::string texture;   // type 6: the IBL cube texture, empty if not found
};
constexpr std::uint32_t kModelLightIbl = 6;

struct Model {
    std::string name;
    std::vector<ModelLight> lights;
    std::vector<ModelVertex> vertices;
    // Half-float colour attributes (three per vertex on cloud meshes: 12 values a cloud blends by
    // view angle), 12 floats per vertex; empty when no vertex has them.
    std::vector<float> viewValues;
    std::vector<std::uint32_t> indices;
    std::vector<ModelDraw> draws;
    std::vector<ModelMaterial> materials;
    std::vector<PaletteEntry> palette;   // entry 0 is the identity (unanimated geometry)
    float boundsMin[3] = {0, 0, 0}, boundsMax[3] = {0, 0, 0};
};

std::optional<Model> buildModel(const JxmFile& file, std::string* error = nullptr);

constexpr int kCloudFieldCount = 41;
extern const char* const kCloudFields[kCloudFieldCount];

// The material templates (MTTB) laid out, by template hash (research tools).
std::map<std::uint32_t, Schema> materialTemplates(const JxmFile& file);

}   // namespace data
