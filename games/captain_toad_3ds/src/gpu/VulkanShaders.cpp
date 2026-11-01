// See VulkanShaders.hpp.
#include "gpu/VulkanShaders.hpp"

#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <SPIRV/GlslangToSpv.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <regex>
#include <set>
#include <stdexcept>

namespace captain_toad::gpu::vulkan {

namespace {

// The samplers a source declares ("uniform sampler2D a, b;").
std::set<std::string> samplerNames(const std::string& source) {
    std::set<std::string> names;
    static const std::regex declaration(R"(uniform\s+sampler2D\s+([^;]+);)");
    static const std::regex name(R"([A-Za-z_][A-Za-z0-9_]*)");
    for (auto it = std::sregex_iterator(source.begin(), source.end(), declaration); it != std::sregex_iterator();
         ++it) {
        const std::string list = (*it)[1];
        for (auto n = std::sregex_iterator(list.begin(), list.end(), name); n != std::sregex_iterator(); ++n) {
            names.insert(n->str());
        }
    }
    return names;
}

// The texture read with the border emulation (see the header). Nearest sampling selects a texel
// outside the image for coordinates below 0 or from 1 on.
const char* const kBorderSample =
    "vec4 deviceSample(sampler2D s,vec2 uv,vec4 border,vec2 axes){\n"
    " if((axes.x>0.5&&(uv.x<0.0||uv.x>=1.0))||(axes.y>0.5&&(uv.y<0.0||uv.y>=1.0)))return border;\n"
    " return texture(s,uv);}\n";

// The stage interface: global "out" declarations of the vertex stage and "in" declarations of
// the fragment stage (one or more names per declaration).
struct Varying {
    std::string type, name;
};
const std::regex& interfaceDeclaration() {
    static const std::regex r(
        R"((^|[;{}\n])\s*(in|out)\s+(float|vec2|vec3|vec4)\s+([A-Za-z_]\w*(\s*,\s*[A-Za-z_]\w*)*)\s*;)");
    return r;
}
std::vector<Varying> varyings(const std::string& source, const char* qualifier) {
    // Searched declaration by declaration (each match consumes the delimiter before it, and the
    // next search may start right after a ';').
    std::vector<Varying> out;
    static const std::regex name(R"([A-Za-z_]\w*)");
    std::smatch m;
    std::string rest = source;
    while (std::regex_search(rest, m, interfaceDeclaration())) {
        if (m[2] == qualifier) {
            const std::string list = m[4];
            for (auto n = std::sregex_iterator(list.begin(), list.end(), name); n != std::sregex_iterator(); ++n) {
                out.push_back({m[3], n->str()});
            }
        }
        rest = m.suffix().str();
    }
    return out;
}

// Gives every varying the location of its name (shared by both stages) and declares the vertex
// outputs the fragment stage reads but the vertex stage does not write (OpenGL leaves those
// undefined; Vulkan needs the output declared).
std::string placeVaryings(const std::string& source, const char* qualifier, const std::map<std::string, int>& locations,
                          const std::vector<Varying>& missing) {
    std::string out;
    std::smatch m;
    std::string rest = source;
    static const std::regex name(R"([A-Za-z_]\w*)");
    while (std::regex_search(rest, m, interfaceDeclaration())) {
        out += m.prefix().str() + m[1].str();
        if (m[2] != qualifier) {
            out += m.str().substr(m[1].length());
        } else {
            const std::string list = m[4];
            for (auto n = std::sregex_iterator(list.begin(), list.end(), name); n != std::sregex_iterator(); ++n) {
                out += "layout(location=" + std::to_string(locations.at(n->str())) + ") " + m[2].str() + " " +
                       m[3].str() + " " + n->str() + ";";
            }
        }
        rest = m.suffix().str();
    }
    out += rest;
    for (const Varying& v : missing) {
        out =
            "layout(location=" + std::to_string(locations.at(v.name)) + ") out " + v.type + " " + v.name + ";\n" + out;
    }
    return out;
}

bool multiview = false;

std::string prepare(std::string_view body, bool vertex) {
    std::string source(body);
    std::string head = "#version 450\n";
    head += multiview ? "#extension GL_EXT_multiview : require\n#define deviceViewIndex gl_ViewIndex\n"
                      : "#define deviceViewIndex 0\n";
    if (vertex) {
        // The shader's main runs first; then clip z from -w..w to 0..w.
        static const std::regex main(R"(void\s+main\s*\(\s*\))");
        source = std::regex_replace(source, main, "void deviceMain()", std::regex_constants::format_first_only);
        if (multiview) {
            head += "uniform mat4 deviceView[2];\n";
            source += "\nvoid main(){deviceMain();gl_Position=deviceView[gl_ViewIndex]*gl_Position;"
                      "gl_Position.z=(gl_Position.z+gl_Position.w)*0.5;gl_Position.y=-gl_Position.y;}\n";
        } else {
            source += "\nvoid main(){deviceMain();gl_Position.z=(gl_Position.z+gl_Position.w)*0.5;gl_Position.y=-gl_Position.y;}\n";
        }
    } else {
        head += "out vec4 fragColor;\n";
    }
    const std::set<std::string> samplers = samplerNames(source);
    if (vertex && !samplers.empty()) {
        // Samplers of the vertex stage (GPU skinning's palette) get explicit bindings from 20: the
        // automatic mapping numbers each stage from 1, so they would share the fragment stage's.
        // Vertex stages without samplers are unaffected.
        int binding = 20;
        for (const std::string& name : samplers) {
            source =
                std::regex_replace(source, std::regex("uniform\\s+sampler2D\\s+" + name + "\\s*;"),
                                   "layout(binding=" + std::to_string(binding++) + ") uniform sampler2D " + name + ";");
        }
    }
    if (!samplers.empty()) {
        head += kBorderSample;
        for (const std::string& s : samplers) {
            head += "uniform vec4 deviceBorder_" + s + ";uniform vec2 deviceBorderAxes_" + s + ";\n";
        }
        head += "#define texture(s,uv) deviceSample(s,uv,deviceBorder_##s,deviceBorderAxes_##s)\n";
    }
    return head + source;
}

void initialize() {
    static std::once_flag once;
    std::call_once(once, [] { glslang::InitializeProcess(); });
}

// ---- the cache -------------------------------------------------------------------------------
// One file per program: the hash of its sources (and of this translation's version) names it.
// Contents: the magic, both SPIR-V streams, the block size and the three tables.

std::filesystem::path cacheDirectory;
constexpr uint32_t kCacheMagic = 0x44565333;   // "DVS3": bump when the translation changes (3: vertex sampler bindings)

uint64_t hashSources(std::string_view a, std::string_view b) {
    uint64_t h = 1469598103934665603ull;
    auto add = [&](std::string_view s) {
        for (unsigned char c : s) {
            h = (h ^ c) * 1099511628211ull;
        }
        h = (h ^ 0xff) * 1099511628211ull;
    };
    add(a);
    add(b);
    add(std::to_string(kCacheMagic));
    if (multiview) {
        add("multiview");
    }
    return h;
}

struct Writer {
    std::string bytes;
    void u32(uint32_t v) { bytes.append(reinterpret_cast<const char*>(&v), 4); }
    void str(const std::string& s) {
        u32(uint32_t(s.size()));
        bytes += s;
    }
    void words(const std::vector<uint32_t>& w) {
        u32(uint32_t(w.size()));
        bytes.append(reinterpret_cast<const char*>(w.data()), w.size() * 4);
    }
};

struct Reader {
    const std::string& bytes;
    size_t at = 0;
    uint32_t u32() {
        if (at + 4 > bytes.size()) {
            throw std::runtime_error("truncated");
        }
        uint32_t v;
        std::memcpy(&v, bytes.data() + at, 4);
        at += 4;
        return v;
    }
    std::string str() {
        const uint32_t n = u32();
        if (at + n > bytes.size()) {
            throw std::runtime_error("truncated");
        }
        std::string s = bytes.substr(at, n);
        at += n;
        return s;
    }
    std::vector<uint32_t> words() {
        const uint32_t n = u32();
        if (at + size_t(n) * 4 > bytes.size()) {
            throw std::runtime_error("truncated");
        }
        std::vector<uint32_t> w(n);
        std::memcpy(w.data(), bytes.data() + at, size_t(n) * 4);
        at += size_t(n) * 4;
        return w;
    }
};

std::string serialize(const CompiledProgram& p) {
    Writer w;
    w.u32(kCacheMagic);
    w.words(p.vertex);
    w.words(p.fragment);
    w.u32(uint32_t(p.blockSize));
    w.u32(uint32_t(p.uniforms.size()));
    // In name order, so a cache file does not depend on the hash map's order.
    const std::map<std::string_view, UniformInfo> sorted(p.uniforms.begin(), p.uniforms.end());
    for (const auto& [n, u] : sorted) {
        w.str(std::string(n));
        w.u32(uint32_t(u.offset));
        w.u32(uint32_t(u.type));
        w.u32(uint32_t(u.arraySize));
        w.u32(uint32_t(u.arrayStride));
    }
    for (const auto* table : {&p.samplers, &p.inputs}) {
        w.u32(uint32_t(table->size()));
        for (const auto& [n, v] : *table) {
            w.str(n);
            w.u32(uint32_t(v));
        }
    }
    return w.bytes;
}

bool deserialize(const std::string& bytes, CompiledProgram& p) {
    try {
        Reader r{bytes};
        if (r.u32() != kCacheMagic) {
            return false;
        }
        p.vertex = r.words();
        p.fragment = r.words();
        p.blockSize = int(r.u32());
        for (uint32_t n = r.u32(); n > 0; n--) {
            const std::string name = r.str();
            UniformInfo u;
            u.offset = int(r.u32());
            u.type = int(r.u32());
            u.arraySize = int(r.u32());
            u.arrayStride = int(r.u32());
            p.uniforms[name] = u;
        }
        for (auto* table : {&p.samplers, &p.inputs}) {
            for (uint32_t n = r.u32(); n > 0; n--) {
                const std::string name = r.str();
                (*table)[name] = int(r.u32());
            }
        }
        return r.at == bytes.size();
    } catch (const std::exception&) {
        return false;
    }
}

std::array<std::string, 2> translate(std::string_view vertex, std::string_view fragment);
CompiledProgram compileUncached(const std::array<std::string, 2>& sources, std::string_view name);

}   // namespace

void setMultiview(bool on) {
    multiview = on;
}

void setCacheDirectory(const std::filesystem::path& directory) {
    cacheDirectory = directory;
}

CompiledProgram compileProgram(std::string_view vertex, std::string_view fragment, std::string_view name) {
    // The cache is keyed by the translated sources (the code glslang compiles): any change of the
    // translation (or of a mode such as multiview) finds no stale program. Keyed by the dialect
    // sources, programs compiled by an older translation survived in the preferences folder (the
    // render regression's Vulkan baseline was made with some).
    const std::array<std::string, 2> sources = translate(vertex, fragment);
    std::filesystem::path file;
    if (!cacheDirectory.empty()) {
        char hex[17];
        std::snprintf(hex, sizeof hex, "%016llx", (unsigned long long)hashSources(sources[0], sources[1]));
        file = cacheDirectory / (std::string(hex) + ".spv");
        std::ifstream in(file, std::ios::binary);
        if (in) {
            const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            CompiledProgram p;
            if (deserialize(bytes, p)) {
                return p;
            }
        }
    }
    CompiledProgram p = compileUncached(sources, name);
    if (!file.empty()) {
        std::error_code e;
        std::filesystem::create_directories(cacheDirectory, e);
        const std::filesystem::path temporary = file.string() + ".tmp";
        {
            std::ofstream out(temporary, std::ios::binary);
            out << serialize(p);
        }
        std::filesystem::rename(temporary, file, e);   // a cache that cannot be written is not an error
    }
    return p;
}

namespace {

// The dialect program as glslang compiles it: varyings placed by name, the stage preludes.
std::array<std::string, 2> translate(std::string_view vertex, std::string_view fragment) {
    std::string vs0(vertex), fs0(fragment);
    const std::vector<Varying> outs = varyings(vs0, "out"), ins = varyings(fs0, "in");
    std::map<std::string, int> locations;
    std::vector<Varying> missing;
    for (const Varying& v : outs) {
        locations.emplace(v.name, int(locations.size()));
    }
    for (const Varying& v : ins) {
        if (locations.emplace(v.name, int(locations.size())).second) {
            missing.push_back(v);
        }
    }
    vs0 = placeVaryings(vs0, "out", locations, missing);
    fs0 = placeVaryings(fs0, "in", locations, {});
    return {prepare(vs0, true), prepare(fs0, false)};
}

CompiledProgram compileUncached(const std::array<std::string, 2>& sources, std::string_view name) {
    initialize();
    const EShLanguage stages[2] = {EShLangVertex, EShLangFragment};
    const EShMessages messages = EShMessages(EShMsgSpvRules | EShMsgVulkanRules);
    glslang::TShader vs(EShLangVertex), fs(EShLangFragment);
    glslang::TShader* shaders[2] = {&vs, &fs};
    for (int k = 0; k < 2; k++) {
        glslang::TShader& s = *shaders[k];
        const char* text = sources[k].c_str();
        s.setStrings(&text, 1);
        s.setEnvInput(glslang::EShSourceGlsl, stages[k], glslang::EShClientVulkan, 100);
        s.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_1);
        s.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_3);
        s.setEnvInputVulkanRulesRelaxed();
        s.setAutoMapBindings(true);
        s.setAutoMapLocations(true);
        s.setGlobalUniformBlockName("DeviceUniforms");
        s.setGlobalUniformSet(0);
        s.setGlobalUniformBinding(0);
        if (!s.parse(GetDefaultResources(), 450, false, messages)) {
            throw std::runtime_error(std::string(name) + (k == 0 ? " vertex shader: " : " fragment shader: ") +
                                     s.getInfoLog());
        }
    }
    glslang::TProgram program;
    program.addShader(&vs);
    program.addShader(&fs);
    if (!program.link(messages) || !program.mapIO()) {
        throw std::runtime_error(std::string(name) + " program: " + program.getInfoLog());
    }
    program.buildReflection(EShReflectionAllBlockVariables | EShReflectionSeparateBuffers);

    CompiledProgram out;
    glslang::GlslangToSpv(*program.getIntermediate(EShLangVertex), out.vertex);
    glslang::GlslangToSpv(*program.getIntermediate(EShLangFragment), out.fragment);
    for (int i = 0; i < program.getNumUniformBlocks(); i++) {
        const auto& b = program.getUniformBlock(i);
        if (b.getBinding() == 0) {
            out.blockSize = b.size;
        }
    }
    for (int i = 0; i < program.getNumUniformVariables(); i++) {
        const auto& u = program.getUniform(i);
        std::string n = u.name;
        if (n.size() > 3 && n.compare(n.size() - 3, 3, "[0]") == 0) {
            n.resize(n.size() - 3);
        }
        if (u.glDefineType == 0x8B5E) {   // GL_SAMPLER_2D
            out.samplers[n] = u.getBinding();
        } else if (u.index >= 0) {
            out.uniforms[n] = {u.offset, u.glDefineType, u.size, u.arrayStride};
        }
    }
    for (int i = 0; i < program.getNumPipeInputs(); i++) {
        const auto& a = program.getPipeInput(i);
        out.inputs[a.name] = int(a.layoutLocation());
    }
    return out;
}

}   // namespace

}   // namespace captain_toad::gpu::vulkan
