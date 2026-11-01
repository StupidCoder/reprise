// GLSL generation for the viewer (native graphics service): the PICA six-stage combiner
// translation, PICA fragment lighting compiled from ROM material state, the native PCF shadow
// additions and the actor shader variants. Material lighting rules are interpreted from the
// original material binder (ARM 0x373a10) and colour product (0x4017d4), see docs/lighting-fidelity.md.
#pragma once
#include "Bch.hpp"

#include <optional>
#include <string>
#include <vector>

namespace captain_toad {

std::string replaceAll(std::string s, const std::string& from, const std::string& to);
std::string replaceFirst(std::string s, const std::string& from, const std::string& to);
std::vector<std::string> splitLines(const std::string& s);
bool startsWith(const std::string& s, const std::string& prefix);

extern const char* const kTerrainVertexShader;

struct TevOptions {
    const std::array<uint8_t, 48>* colors = nullptr;   // material colours (diffuse-only lighting placeholder)
    bool sky = false;
    const std::array<TexTransform, 3>* transforms = nullptr;
    bool texture0 = false;
};
std::string generateTev(const Registers& regs, const TevOptions& options = {});

// Native PCF shadow additions to the terrain vertex shader.
std::string shadowVertex(const std::string& source);

// Lighting LUT records of LutCurveData 'lightSet' (28 rows).
struct LutRecord {
    std::string name;
    uint32_t sourceOffset;
    std::vector<uint32_t> words;   // 256 PICA LUT entries
    uint32_t absolute;
};
std::vector<LutRecord> lutRecords(const Bch& b);
// A model's own lighting tables (its LUT sets other than "lightSet", e.g. the water's "Water"),
// each with its set name and the atlas row the exporter gives it.
struct ModelLut {
    std::string table;
    LutRecord record;
    size_t row;
};
std::vector<LutRecord> modelLutRecords(const Bch& b, std::vector<std::string>& tables);
json::Value toJson(const LutRecord& r);

// Material-owned PICA fragment lighting state (tools/pica_material.py).
struct PicaState {
    std::string name;
    uint16_t flags;
    uint32_t configWord, lightingConfig, config0, config1;
    uint32_t lutAbs, lutInput, lutScale;
    std::array<std::optional<int>, 6> lutRows;   // d0 d1 fr rr rg rb
    std::array<std::optional<std::string>, 6> lutNames;
    bool geometric0, geometric1;
    std::optional<int> shadowMode;
    uint16_t lightSet;
    json::Value colorsJson() const;
    json::Value lutNamesJson() const;
    std::array<uint8_t, 48> colors;
};
PicaState picaState(const Bch& b, const Material& m, const std::vector<LutRecord>& luts, bool bump = true,
                    const std::vector<ModelLut>* modelLuts = nullptr);

// Replaces the combiner translation's placeholder lighting with PICA lighting.
std::string picaLightingFragment(const std::string& tevSource, const Material& m, const PicaState& st);

// Projection-mapped texture slots read the camera projection of the fragment (after the
// texture-animation pass, which keeps gl_TexCoord[0] as the base coordinate).
std::string projectionShader(std::string source, const Material& m);
// "$water" materials sample a copy of the scene behind them (the session's water pass).
std::string waterShader(std::string source, const Material& m);
std::string slotTwoCoordinates(std::string source, const Material& m);

// True when a TEV stage reads fragment primary/secondary lighting.
bool usesLighting(const Material& m);
std::string animatedUvShader(std::string source, const std::vector<int>& slots);
std::string cubeShader(std::string source);
std::string animatedConstantShader(const std::string& source, const std::vector<int>& stages,
                                   const std::string& uniform);

// The translation builds its shaders as GLSL 1.20 text (the form the research exporters write);
// packages store them in the graphics device's dialect (gpu/Device.hpp): explicit vertex inputs
// and matrices instead of the fixed-function built-ins. Throws on a built-in it does not map.
enum class ShaderStage { Vertex, Fragment };
std::string deviceShader(const std::string& glsl120, ShaderStage stage);

// Python-style texture coordinate service (tools/texture_transform.py): scale and offset.
void scaleOffset(const TexTransform& t, double scale[2], double offset[2]);

}   // namespace captain_toad
