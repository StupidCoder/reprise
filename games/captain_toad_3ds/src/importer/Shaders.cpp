// See Shaders.hpp.
#include "Shaders.hpp"

#include <cmath>
#include <cstdio>
#include <map>

namespace captain_toad {

using json::formatFloat;

std::string replaceAll(std::string s, const std::string& from, const std::string& to) {
    if (from.empty()) {
        return s;
    }
    size_t at = 0;
    while ((at = s.find(from, at)) != std::string::npos) {
        s.replace(at, from.size(), to);
        at += to.size();
    }
    return s;
}

std::string replaceFirst(std::string s, const std::string& from, const std::string& to) {
    const size_t at = s.find(from);
    if (at != std::string::npos) {
        s.replace(at, from.size(), to);
    }
    return s;
}

std::vector<std::string> splitLines(const std::string& s) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start < s.size()) {
        const size_t e = s.find('\n', start);
        if (e == std::string::npos) {
            out.push_back(s.substr(start));
            break;
        }
        out.push_back(s.substr(start, e - start));
        start = e + 1;
    }
    return out;
}

bool startsWith(const std::string& s, const std::string& prefix) {
    return s.compare(0, prefix.size(), prefix) == 0;
}

static std::string join(const std::vector<std::string>& lines, const std::string& sep) {
    std::string out;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i) {
            out += sep;
        }
        out += lines[i];
    }
    return out;
}

static std::string fixed(double v, int digits) {
    char b[64];
    std::snprintf(b, sizeof b, "%.*f", digits, v);
    return b;
}

const char* const kTerrainVertexShader = "#version 120\n"
                                         "varying vec4 vertexColor;\n"
                                         "varying vec3 normal;\n"
                                         "void main(){gl_Position=ftransform();gl_TexCoord[0]=gl_MultiTexCoord0;\n"
                                         "vertexColor=gl_Color;normal=normalize(gl_NormalMatrix*gl_Normal);}\n";

static const char* const kShadowVertexExtra = "uniform mat4 shadowMatrix;\nvarying vec4 shadowPosition;\n";

static const char* const kShadowGlsl = R"GLSL(
uniform sampler2D shadowDepth;
uniform int useShadows;
uniform int filterShadows;
uniform int shadowKernel;
uniform int showShadowMask;
uniform float shadowTexel;
uniform float shadowBias;
uniform vec4 shadowTint;
varying vec4 shadowPosition;
float shadowVisibility(){
 if(useShadows==0)return 1.0;
 vec3 p=shadowPosition.xyz/shadowPosition.w*0.5+0.5;
 if(any(lessThan(p,vec3(0.0)))||any(greaterThan(p,vec3(1.0))))return 1.0;
 vec3 dx=dFdx(p),dy=dFdy(p);
 float det=dx.x*dy.y-dx.y*dy.x;
 vec2 gradient=abs(det)>1e-12?vec2(dy.y*dx.z-dx.y*dy.z,dx.x*dy.z-dy.x*dx.z)/det:vec2(0.0);
 float reference=p.z-shadowBias;
 if(filterShadows==0)return step(reference,texture2D(shadowDepth,p.xy).r);
 // shadowKernel: the filter's radius in texels (2: 5 x 5 taps; a headset uses 1, see Session::setShadowFilter).
 float sum=0.0;
 for(int y=-shadowKernel;y<=shadowKernel;y++)for(int x=-shadowKernel;x<=shadowKernel;x++){
  // Bilinear interpolation of depth comparisons (PCF, not blurred depths).
  vec2 q=p.xy/shadowTexel+vec2(float(x),float(y))-0.5;
  vec2 f=fract(q);vec2 uv=(floor(q)+0.5)*shadowTexel;
  float a=step(reference+dot(gradient,uv-p.xy),texture2D(shadowDepth,uv).r);
  float b=step(reference+dot(gradient,uv+vec2(shadowTexel,0.0)-p.xy),texture2D(shadowDepth,uv+vec2(shadowTexel,0.0)).r);
  float c=step(reference+dot(gradient,uv+vec2(0.0,shadowTexel)-p.xy),texture2D(shadowDepth,uv+vec2(0.0,shadowTexel)).r);
  float d=step(reference+dot(gradient,uv+vec2(shadowTexel)-p.xy),texture2D(shadowDepth,uv+vec2(shadowTexel)).r);
  sum+=mix(mix(a,b,f.x),mix(c,d,f.x),f.y);
 }
 return sum/float((2*shadowKernel+1)*(2*shadowKernel+1));
}
)GLSL";

static const char* const kLightingUniforms = R"GLSL(
uniform sampler2D lightingLuts;
uniform float lutRows;
uniform int useNormalMaps;
uniform int lightCount;
uniform vec3 lightVector[4];
uniform vec3 lightSpotDirection[4];
uniform vec3 lightAmbient[4];
uniform vec3 lightDiffuse[4];
uniform vec3 lightSpecular0[4];
uniform vec3 lightSpecular1[4];
uniform vec4 lightFlags[4];
uniform vec4 lightDistance[4];
uniform vec4 lightSpot[4];
uniform vec3 globalAmbient;
varying vec3 eyePosition;
varying vec3 surfaceTangent;
// PICA LUT: 256 (value, difference) entries; absolute inputs use [0,1] -> 0..255,
// signed inputs use two's-complement indices of floor(x*128).
float picaLut(float row,float x,bool absolute){
 float index,delta;
 if(absolute){index=clamp(floor(x*256.0),0.0,255.0);delta=x*256.0-index;}
 else{index=clamp(floor(x*128.0),-128.0,127.0);delta=x*128.0-index;if(index<0.0)index+=256.0;}
 vec2 e=texture2D(lightingLuts,vec2((index+0.5)/256.0,(row+0.5)/lutRows)).rg;
 return e.x+e.y*delta;
}
vec3 picaQuantize(vec3 c){return floor(clamp(c,0.0,1.0)*255.0)/255.0;}
)GLSL";

// The fixed-function built-ins of the GLSL 1.20 text and what replaces them. Vertex: the
// position, normal, colour and texture coordinates become the dialect's inputs, the matrices
// uniforms (ftransform: the model-view-projection product, see gpu/Matrix.hpp), and the
// gl_TexCoord outputs plain outputs (texture coordinate 2 is always written: the fragment side may
// read it). Fragment: the fixed-function light (the stand-in lighting of untranslated materials)
// and the projection become uniforms the session sets.
std::string deviceShader(const std::string& glsl120, ShaderStage stage) {
    std::string s = glsl120;
    const std::string version = "#version 120\n";
    if (startsWith(s, version)) {
        s = s.substr(version.size());
    }
    std::string head;
    if (stage == ShaderStage::Vertex) {
        s = replaceAll(s, "varying ", "out ");
        s = replaceAll(s, "gl_Position=ftransform();", "gl_Position=modelViewProjectionMatrix*vec4(inPosition,1.0);");
        s = replaceAll(s, "gl_Vertex", "vec4(inPosition,1.0)");
        s = replaceAll(s, "gl_ModelViewMatrix", "modelViewMatrix");
        s = replaceAll(s, "gl_NormalMatrix", "normalMatrix");
        s = replaceAll(s, "gl_Normal", "inNormal");
        s = replaceAll(s, "gl_Color", "inColor");
        s = replaceAll(s, "gl_MultiTexCoord1.xyz", "inTexCoord1");
        s = replaceAll(s, "gl_TexCoord[0]=gl_MultiTexCoord0;", "texCoord0=vec4(inTexCoord0,0.0,1.0);");
        s = replaceAll(s, "gl_TexCoord[2]=gl_MultiTexCoord2;", "");
        s = replaceFirst(s, "void main(){", "void main(){texCoord2=vec4(inTexCoord2,0.0,1.0);");
        head = "in vec3 inPosition;in vec3 inNormal;in vec4 inColor;in vec2 inTexCoord0;in vec3 inTexCoord1;"
               "in vec2 inTexCoord2;\n"
               "uniform mat4 modelViewProjectionMatrix;uniform mat4 modelViewMatrix;uniform mat3 normalMatrix;\n"
               "out vec4 texCoord0;out vec4 texCoord2;\n";
    } else {
        s = replaceAll(s, "varying ", "in ");
        s = replaceAll(s, "texture2D(", "texture(");
        s = replaceAll(s, "gl_FragColor", "fragColor");
        s = replaceAll(s, "gl_TexCoord[0]", "texCoord0");
        s = replaceAll(s, "gl_TexCoord[1]", "texCoord1");
        s = replaceAll(s, "gl_TexCoord[2]", "texCoord2");
        s = replaceAll(s, "gl_LightModel.ambient", "fixedLightAmbient");
        s = replaceAll(s, "gl_LightSource[0].diffuse", "fixedLightDiffuse");
        s = replaceAll(s, "gl_LightSource[0].position", "fixedLightPosition");
        s = replaceAll(s, "gl_ProjectionMatrix", "projectionMatrix");
        head = std::string("in vec4 texCoord0;in vec4 texCoord2;") +
               (s.find("texCoord1") != std::string::npos ? "in vec4 texCoord1;" : "") +
               "\n"
               "uniform vec4 fixedLightAmbient;uniform vec4 fixedLightDiffuse;uniform vec4 fixedLightPosition;"
               "uniform mat4 projectionMatrix;\n";
    }
    // Every built-in must have been mapped (gl_Position and gl_FragCoord exist in the dialect).
    for (size_t at = s.find("gl_"); at != std::string::npos; at = s.find("gl_", at + 3)) {
        if (s.compare(at, 11, "gl_Position") != 0 && s.compare(at, 12, "gl_FragCoord") != 0) {
            throw ImportError("Shader built-in without a device mapping: " + s.substr(at, 24));
        }
    }
    return head + s;
}

void scaleOffset(const TexTransform& t, double scale[2], double offset[2]) {
    if ((t.flags >> 8) & 255 || t.v[2] != 0) {
        throw ImportError("Unsupported texture transform convention/rotation");
    }
    for (int k = 0; k < 2; ++k) {
        scale[k] = t.v[size_t(k)];
        offset[k] = -t.v[size_t(k)] * t.v[size_t(3 + k)];
    }
}

static std::string vec4Word(uint32_t word) {
    std::string s = "vec4(";
    for (int i = 0; i < 4; ++i) {
        if (i) {
            s += ",";
        }
        s += fixed(double((word >> (8 * i)) & 255) / 255, 9);
    }
    return s + ")";
}

static uint32_t reg(const Registers& r, uint32_t k) {
    auto it = r.find(k);
    if (it == r.end()) {
        throw ImportError("Missing combiner register");
    }
    return it->second;
}
static uint32_t regOr(const Registers& r, uint32_t k) {
    auto it = r.find(k);
    return it == r.end() ? 0 : it->second;
}

std::string generateTev(const Registers& regs, const TevOptions& o) {
    std::string light = "vec3(1.0)";
    if (o.colors) {
        light = "vec3(";
        for (int i = 4; i < 7; ++i) {
            if (i > 4) {
                light += ",";
            }
            light += formatFloat(double((*o.colors)[size_t(i)]) / 255);
        }
        light += ")";
    }
    std::vector<std::string> lines = {
        "#version 120",
        "varying vec4 vertexColor; varying vec3 normal;",
        "uniform sampler2D albedo; uniform sampler2D detail; uniform sampler2D textureZero; uniform int useLighting;",
        "void main(){",
        "vec4 tex1=texture2D(albedo,gl_TexCoord[0].xy);",
        "vec4 tex2=texture2D(detail,gl_TexCoord[0].xy);",
        "vec3 light=gl_LightModel.ambient.rgb*" + light +
            "+gl_LightSource[0].diffuse.rgb*max(0.0,dot(normalize(normal),normalize(gl_LightSource[0].position.xyz)));",
        "vec4 primary=vec4(useLighting==0?vec3(1.0):clamp(light,0.0,1.0),1.0); vec4 secondary=vec4(0.0);",
        "vec4 previous=vec4(0.0);"};
    if (!o.colors) {
        for (auto& x : lines) {
            if (startsWith(x, "vec3 light=")) {
                x = "float "
                    "oldLight=useLighting==0?1.0:0.62+0.38*max(0.0,dot(normalize(normal),normalize(gl_LightSource[0]."
                    "position.xyz)));";
            } else if (startsWith(x, "vec4 primary=")) {
                x = "vec4 primary=vec4(vec3(oldLight),1.0); vec4 secondary=vec4(0.0);";
            }
        }
    }
    if (o.sky) {
        auto uv = [](const TexTransform& t) {
            if (t.v[2] != 0) {
                // A rotated transform: the full 2x3 texture matrix, as MaterialAnimation.hpp builds
                // it for the first authoring convention.
                if ((t.flags >> 8) & 255) {
                    throw ImportError("Sky texture rotation convention");
                }
                const double sx = t.v[0], sy = t.v[1], r = t.v[2], tx = t.v[3], ty = t.v[4];
                const double ca = std::cos(r), sa = std::sin(r);
                const double m41 = sx * ((0.5 * sa - 0.5 * ca) + 0.5 - tx);
                const double m42 = sy * ((0.5 * -sa - 0.5 * ca) + 0.5 - ty);
                return "vec2(dot(vec3(gl_TexCoord[0].xy,1.0),vec3(" + formatFloat(sx * ca) + "," +
                       formatFloat(sx * -sa) + "," + formatFloat(m41) + ")),dot(vec3(gl_TexCoord[0].xy,1.0),vec3(" +
                       formatFloat(sy * sa) + "," + formatFloat(sy * ca) + "," + formatFloat(m42) + ")))";
            }
            double s[2], off[2];
            scaleOffset(t, s, off);
            return "gl_TexCoord[0].xy*vec2(" + formatFloat(t.v[0]) + "," + formatFloat(t.v[1]) + ")+vec2(" +
                   formatFloat(off[0]) + "," + formatFloat(off[1]) + ")";
        };
        lines.insert(lines.begin() + 4, "vec4 tex0=texture2D(textureZero," + uv((*o.transforms)[0]) + ");");
        for (auto& x : lines) {
            if (startsWith(x, "vec4 tex1=")) {
                x = "vec4 tex1=texture2D(albedo," + uv((*o.transforms)[1]) + ");";
            }
        }
    }
    lines.push_back("vec4 combinerBuffer=vec4(0.0); vec4 nextBuffer=" + vec4Word(regOr(regs, 0xfd)) + ";");
    std::map<uint32_t, std::string> sources = {{0, "vertexColor"},    {1, "primary"},  {2, "secondary"},
                                               {4, "tex1"},           {5, "tex2"},     {13, "combinerBuffer"},
                                               {14, "constantColor"}, {15, "previous"}};
    if (o.texture0) {
        lines.insert(lines.begin() + 4, "vec4 tex0=texture2D(textureZero,gl_TexCoord[0].xy);");
    }
    if (o.sky || o.texture0) {
        sources[3] = "tex0";
    }
    const uint32_t bases[6] = {0xc0, 0xc8, 0xd0, 0xd8, 0xf0, 0xf8};
    for (int stage = 0; stage < 6; ++stage) {
        const uint32_t base = bases[stage];
        const uint32_t source = reg(regs, base), op = reg(regs, base + 1), mode = reg(regs, base + 2),
                       scale = reg(regs, base + 4);
        auto operand = [&](uint32_t src, uint32_t opv, bool alpha) {
            if (base == 0xc0 && src == 15) {
                src = (source >> (alpha ? 24 : 8)) & 15;
            }
            auto it = sources.find(src);
            if (it == sources.end()) {
                throw ImportError("Unsupported TEV source " + std::to_string(src));
            }
            const std::string& s = it->second;
            if (alpha) {
                static const char* comps[8] = {"a", "a", "r", "r", "g", "g", "b", "b"};
                if (opv >= 8) {
                    throw ImportError("Alpha operand");
                }
                const std::string expr = s + "." + comps[opv];
                return (opv & 1) ? "(1.0-" + expr + ")" : expr;
            }
            static const std::map<uint32_t, std::string> comps = {{0, "rgb"},  {1, "rgb"}, {2, "aaa"}, {3, "aaa"},
                                                                  {4, "rrr"},  {5, "rrr"}, {8, "ggg"}, {9, "ggg"},
                                                                  {12, "bbb"}, {13, "bbb"}};
            auto c = comps.find(opv);
            if (c == comps.end()) {
                throw ImportError("Color operand");
            }
            const std::string expr = s + "." + c->second;
            return (opv & 1) ? "(vec3(1.0)-" + expr + ")" : expr;
        };
        // PICA combiner operations (public PICA200 register documentation): 0 replace, 1 modulate,
        // 2 add, 3 add signed, 4 interpolate, 5 subtract, 6/7 dot3 (RGB / RGBA), 8 multiply-add,
        // 9 add-multiply.
        auto combine = [](uint32_t m, const std::string& a, const std::string& b, const std::string& c,
                          bool alpha) -> std::string {
            switch (m) {
            case 3:
                return "(" + a + "+" + b + "-0.5)";
            case 5:
                return "(" + a + "-" + b + ")";
            case 6:
            case 7:
                return alpha ? "(4.0*(" + a + "-0.5)*(" + b + "-0.5))"
                             : "vec3(4.0*dot(" + a + "-vec3(0.5)," + b + "-vec3(0.5)))";
            case 0:
                return a;
            case 1:
                return "(" + a + "*" + b + ")";
            case 2:
                return "(" + a + "+" + b + ")";
            case 4:
                return "(" + a + "*" + c + "+" + b + "*(1.0-" + c + "))";
            case 8:
                return "(" + a + "*" + b + "+" + c + ")";
            case 9:
                return "(min(" + a + "+" + b + ",1.0)*" + c + ")";
            }
            throw ImportError("Unsupported TEV operation");
        };
        // Only the operands a mode reads (replace: one; interpolate, multiply-add, add-multiply:
        // three; the others two): an unread operand may name a source the material lacks (an
        // untextured material's texture 0, RevolverRuinsStep's grass_cave).
        auto used = [](uint32_t m) {
            return m == 0 ? 1 : (m == 4 || m == 8 || m == 9) ? 3 : 2;
        };
        std::string rgb[3] = {"vec3(0.0)", "vec3(0.0)", "vec3(0.0)"}, alpha[3] = {"0.0", "0.0", "0.0"};
        for (int i = 0; i < used(mode & 15); ++i) {
            rgb[i] = operand((source >> (i * 4)) & 15, (op >> (i * 4)) & 15, false);
        }
        for (int i = 0; i < used((mode >> 16) & 15); ++i) {
            alpha[i] = operand((source >> (16 + i * 4)) & 15, (op >> (12 + i * 4)) & 7, true);
        }
        auto scaleText = [](uint32_t s) {
            return formatFloat(double(s < 3 ? (1 << s) : 1));
        };
        lines.push_back("{vec4 constantColor=" + vec4Word(reg(regs, base + 3)) + ";");
        lines.push_back("vec3 rgb=floor(clamp(" + combine(mode & 15, rgb[0], rgb[1], rgb[2], false) +
                        ",0.0,1.0)*255.0+0.5)/255.0*" + scaleText(scale & 3) + ";");
        lines.push_back("float a=floor(clamp(" + combine((mode >> 16) & 15, alpha[0], alpha[1], alpha[2], true) +
                        ",0.0,1.0)*255.0+0.5)/255.0*" + scaleText((scale >> 16) & 3) + ";");
        lines.push_back("previous=clamp(vec4(rgb,a),0.0,1.0); }");
        lines.push_back("combinerBuffer=nextBuffer;");
        if (stage < 4 && (regOr(regs, 0xe0) & (1u << (8 + stage)))) {
            lines.push_back("nextBuffer.rgb=previous.rgb;");
        }
        if (stage < 4 && (regOr(regs, 0xe0) & (1u << (12 + stage)))) {
            lines.push_back("nextBuffer.a=previous.a;");
        }
    }
    const uint32_t test = regOr(regs, 0x104);
    if (test & 1) {
        const std::string ref = fixed(double((test >> 8) & 255) / 255, 9);
        const char* ops[8] = {"", "", "==", "!=", "<", "<=", ">", ">="};
        const uint32_t func = (test >> 4) & 7;
        const std::string cond = func == 0 ? "false" : func == 1 ? "true" : "previous.a" + std::string(ops[func]) + ref;
        lines.push_back("if(!(" + cond + "))discard;");
    }
    lines.push_back("gl_FragColor=previous;}");
    return join(lines, "\n") + "\n";
}

std::string shadowVertex(const std::string& source) {
    return replaceAll(replaceAll(source, "void main()", std::string(kShadowVertexExtra) + "void main()"),
                      "gl_Position=ftransform();", "gl_Position=ftransform();shadowPosition=shadowMatrix*gl_Vertex;");
}

std::vector<LutRecord> modelLutRecords(const Bch& b, std::vector<std::string>& tables) {
    std::vector<LutRecord> result;
    for (uint32_t p : b.table(4)) {
        const std::optional<std::string> table = b.name(p + 8);
        if (!table || *table == "lightSet") {
            continue;
        }
        for (uint32_t i = 0; i < b.u32(p + 4); ++i) {
            const size_t q = p + 12 + i * 16;
            const uint32_t cp = b.u32(q + 4), cn = b.u32(q + 8);
            if (cn != 260 || b.u32(cp + 4) != 0x0fff01c8) {
                throw ImportError("Unsupported LUT command stream");
            }
            LutRecord r{b.nameOf(q + 12), cp, {}, b.u32(q)};
            r.words.push_back(b.u32(cp));
            for (int k = 2; k < 257; ++k) {
                r.words.push_back(b.u32(cp + size_t(k) * 4));
            }
            result.push_back(std::move(r));
            tables.push_back(*table);
        }
    }
    return result;
}

std::vector<LutRecord> lutRecords(const Bch& b) {
    std::vector<LutRecord> result;
    for (uint32_t p : b.table(4)) {
        if (b.name(p + 8) != std::optional<std::string>("lightSet")) {
            continue;
        }
        for (uint32_t i = 0; i < b.u32(p + 4); ++i) {
            const size_t q = p + 12 + i * 16;
            const uint32_t cp = b.u32(q + 4), cn = b.u32(q + 8);
            if (cn != 260 || b.u32(cp + 4) != 0x0fff01c8) {
                throw ImportError("Unsupported LUT command stream");
            }
            LutRecord r{b.nameOf(q + 12), cp, {}, b.u32(q)};
            r.words.push_back(b.u32(cp));
            for (int k = 2; k < 257; ++k) {
                r.words.push_back(b.u32(cp + size_t(k) * 4));
            }
            result.push_back(std::move(r));
        }
    }
    if (result.size() != 28) {
        throw ImportError("Unexpected lighting LUT count");
    }
    return result;
}

json::Value toJson(const LutRecord& r) {
    json::Value o = json::Value::object();
    o["name"] = r.name;
    o["source_offset"] = r.sourceOffset;
    o["words"] = json::Value::list(r.words);
    o["absolute"] = r.absolute;
    return o;
}

namespace {
// Sampler order of the material's six LUT names; shifts in 0x1d0..0x1d2; 0x1c4 disable bits.
enum { D0, D1, FR, RR, RG, RB };
const char* const kSamplerNames[6] = {"d0", "d1", "fr", "rr", "rg", "rb"};
const int kShift[6] = {0, 4, 12, 24, 20, 16};
const int kDisableBit[6] = {16, 17, 19, 20, 21, 22};
}   // namespace

json::Value PicaState::colorsJson() const {
    json::Value o = json::Value::object();
    auto range = [&](int a) {
        return json::Value::list(std::vector<int>(colors.begin() + a, colors.begin() + a + 3));
    };
    o["emission"] = range(0);
    o["ambient"] = range(4);
    o["diffuse"] = range(8);
    o["specular0"] = range(12);
    o["specular1"] = range(16);
    return o;
}

json::Value PicaState::lutNamesJson() const {
    json::Value o = json::Value::object();
    for (int s = 0; s < 6; ++s) {
        o[kSamplerNames[s]] = lutNames[size_t(s)] ? json::Value(*lutNames[size_t(s)]) : json::Value();
    }
    return o;
}

PicaState picaState(const Bch& b, const Material& m, const std::vector<LutRecord>& luts, bool bump,
                    const std::vector<ModelLut>* modelLuts) {
    const size_t p = m.parameterOffset;
    PicaState st;
    st.name = m.name;
    st.flags = b.u16(p + 6);
    st.configWord = b.u32(p + 164);
    const Registers regs = b.commands(p + 168, 6).regs;
    for (int j = 0; j < 6; ++j) {
        st.lutNames[size_t(j)] = b.name(p + 236 + size_t(j) * 4);
    }
    const uint32_t config = st.configWord, flags = st.flags;
    st.lightingConfig = config & 15;
    const uint32_t bumpMode =
        bump ? (config >> 16) & 255 : 0;   // bump=false: observed runtime override (the companion)
    const uint32_t bumpSelector = (config >> 24) & 255, fresnel = (config >> 8) & 3;
    st.shadowMode = m.shadowMapMode();
    // 0x37434c: $shadowmap 0 primary+secondary, 1 secondary, 2 primary; absent -> none.
    const bool shadowPrimary = st.shadowMode && (*st.shadowMode == 0 || *st.shadowMode == 2);
    const bool shadowSecondary = st.shadowMode && (*st.shadowMode == 0 || *st.shadowMode == 1);
    uint32_t c0 = 0x80000400u | uint32_t(st.shadowMode.has_value());
    c0 |= (fresnel & 1) << 2 | ((fresnel >> 1) & 1) << 3;
    c0 |= st.lightingConfig << 4;
    c0 |= uint32_t(shadowPrimary) << 16 | uint32_t(shadowSecondary) << 17;
    c0 |= (bumpSelector & 3) << 22;
    c0 |= uint32_t(bool(flags & 2)) << 27;
    c0 |= (bumpMode & 3) << 28;
    if (bumpMode) {
        c0 |= uint32_t(!(flags & 1)) << 30;
    }
    st.config0 = c0;
    const bool enabled[6] = {bool(flags & 4),  bool(flags & 8),  bool(fresnel),
                             bool(flags & 16), bool(flags & 16), bool(flags & 16)};
    uint32_t c1 = 1u << 18;
    for (int s = 0; s < 6; ++s) {
        if (!enabled[s]) {
            c1 |= 1u << kDisableBit[s];
        }
    }
    st.config1 = c1;
    uint32_t absolute = 0;
    for (int s = 0; s < 6; ++s) {
        const auto& n = st.lutNames[size_t(s)];
        if (!n) {
            continue;
        }
        int row = -1;
        bool rowAbsolute = false;
        // The sampler's LUT set (material parameters +212, one name per sampler): the model's own
        // set when it has one of that name, else the shared "lightSet" by sampler name.
        const std::optional<std::string> table = b.name(p + 212 + size_t(s) * 4);
        if (modelLuts && table) {
            for (const ModelLut& l : *modelLuts) {
                if (l.table == *table && l.record.name == *n) {
                    row = int(l.row);
                    rowAbsolute = l.record.absolute != 0;
                }
            }
        }
        if (row < 0) {
            for (size_t r = 0; r < luts.size(); ++r) {
                if (luts[r].name == *n) {
                    row = int(r);
                    rowAbsolute = luts[r].absolute != 0;
                }
            }
        }
        if (row < 0) {
            throw ImportError("Unknown lighting LUT " + *n);
        }
        st.lutRows[size_t(s)] = row;
        if (!rowAbsolute) {
            absolute |= 2u << kShift[s];
        }
    }
    st.lutAbs = absolute;
    st.lutInput = regOr(regs, 0x1d1);
    st.lutScale = regOr(regs, 0x1d2);
    st.geometric0 = flags & 32;
    st.geometric1 = flags & 64;
    st.lightSet = b.u16(p + 84);
    st.colors = m.colors;
    return st;
}

namespace {

bool supported(uint32_t config, int s) {
    switch (s) {
    case D0:
        return config != 1;
    case D1:
        return config != 0 && config != 1 && config != 5;
    case FR:
        return config != 0 && config != 2 && config != 4;
    case RR:
        return config != 3;
    default:
        return config == 4 || config == 5 || config == 7;
    }
}

std::vector<std::string> lightingBlock(const PicaState& st) {
    const uint32_t c0 = st.config0, c1 = st.config1, config = st.lightingConfig;
    if (config == 7) {
        throw ImportError("LightingConfig 7 not implemented");
    }
    static const std::map<uint32_t, std::string> inputs = {{0, "dot(n,halfN)"},
                                                           {1, "dot(v,halfN)"},
                                                           {2, "dot(n,v)"},
                                                           {3, "dot(l,n)"},
                                                           {4, "dot(l,lightSpotDirection[i])"}};
    static const std::map<uint32_t, double> scales = {{0, 1.0}, {1, 2.0}, {2, 4.0}, {3, 8.0}, {6, 0.25}, {7, 0.5}};
    auto lut = [&](int s) {
        const auto row = st.lutRows[size_t(s)];
        if (!row) {
            throw ImportError(st.name + ": enabled LUT has no table");
        }
        auto in = inputs.find((st.lutInput >> kShift[s]) & 7);
        if (in == inputs.end()) {
            throw ImportError("Unsupported LUT input");
        }
        const bool absolute = !((st.lutAbs >> (kShift[s] + 1)) & 1);
        std::string x = in->second;
        if (absolute) {
            x = "(lightFlags[i].y>0.5?abs(" + x + "):max(" + x + ",0.0))";
        }
        auto sc = scales.find((st.lutScale >> kShift[s]) & 7);
        if (sc == scales.end()) {
            throw ImportError("Unsupported LUT scale");
        }
        return "(" + fixed(sc->second, 4) + "*picaLut(" + fixed(double(*row), 1) + "," + x + "," +
               (absolute ? "true" : "false") + "))";
    };
    auto enabled = [&](int s) {
        return !((c1 >> kDisableBit[s]) & 1) && supported(config, s);
    };
    // A mesh without tangents (the default (1, 0, 0) parallel to the normal): any perpendicular.
    std::vector<std::string> lines = {
        "vec3 n=normalize(normal);", "vec3 tb=surfaceTangent-n*dot(n,surfaceTangent);",
        "if(dot(tb,tb)<1e-8)tb=abs(n.x)<0.9?vec3(1.0,0.0,0.0)-n*n.x:vec3(0.0,1.0,0.0)-n*n.y;", "vec3 t=normalize(tb);"};
    const uint32_t bumpMode = (c0 >> 28) & 3;
    if (bumpMode == 1) {
        lines.push_back("if(useNormalMaps!=0){vec3 perturb=tex" + std::to_string((c0 >> 22) & 3) +
                        ".rgb*(255.0/127.5)-1.0;");
        if (!((c0 >> 30) & 1)) {
            lines.push_back("perturb.z=sqrt(max(0.0,1.0-dot(perturb.xy,perturb.xy)));");
        }
        lines.push_back("n=normalize(t*perturb.x+cross(n,t)*perturb.y+n*perturb.z);}");
    } else if (bumpMode) {
        throw ImportError("Tangent-map bump mode not implemented");
    }
    lines.push_back(std::string("vec3 shadowTerm=vec3(") + ((c0 & 1) ? "shadowVisibility()" : "1.0") + ");");
    for (const char* x : {"vec3 v=normalize(-eyePosition);",
                          "vec3 diffuseSum=vec3(0.0),specularSum=vec3(0.0);float primaryAlpha=1.0,secondaryAlpha=1.0;",
                          "for(int i=0;i<4;i++){if(i>=lightCount)break;",
                          " vec3 lv=lightFlags[i].x>0.5?lightVector[i]:lightVector[i]-eyePosition;",
                          " float lightLength=length(lv);vec3 l=lv/lightLength;",
                          " vec3 halfV=v+l;vec3 halfN=normalize(halfV);", " float attenuation=1.0;",
                          " if(lightDistance[i].x>0.5){float "
                          "x=clamp(lightDistance[i].y*lightLength+lightDistance[i].z,0.0,1.0);attenuation=picaLut("
                          "lightDistance[i].w,x,true);}"}) {
        lines.push_back(x);
    }
    if (config != 2 && config != 3) {
        lines.push_back(" if(lightSpot[i].x>0.5){float x=dot(l,lightSpotDirection[i]);"
                        "if(lightSpot[i].z>0.5)x=lightFlags[i].y>0.5?abs(x):max(x,0.0);"
                        "attenuation*=lightSpot[i].w*picaLut(lightSpot[i].y,x,lightSpot[i].z>0.5);}");
    }
    lines.push_back(" vec3 spec0=" + (enabled(D0) ? lut(D0) : std::string("1.0")) + "*lightSpecular0[i];");
    const std::string rr = enabled(RR) ? lut(RR) : "1.0";
    const std::string rg = enabled(RG) ? lut(RG) : "reflR";
    const std::string rb = enabled(RB) ? lut(RB) : "reflR";
    const std::string d1 = enabled(D1) ? lut(D1) : "1.0";
    lines.push_back(" float reflR=" + rr + ";");
    lines.push_back(" vec3 spec1=" + d1 + "*vec3(reflR," + rg + "," + rb + ")*lightSpecular1[i];");
    if (enabled(FR)) {
        lines.push_back(" if(i==lightCount-1){float fresnel=" + lut(FR) + ";");
        if ((c0 >> 2) & 1) {
            lines.push_back("  primaryAlpha=fresnel;");
        }
        if ((c0 >> 3) & 1) {
            lines.push_back("  secondaryAlpha=fresnel;");
        }
        lines.push_back(" }");
    }
    lines.push_back(" float ndl=dot(l,n);ndl=lightFlags[i].y>0.5?abs(ndl):max(ndl,0.0);");
    const std::string clampText = ((c0 >> 27) & 1) ? "(ndl==0.0?0.0:1.0)" : "1.0";
    if (st.geometric0 || st.geometric1) {
        lines.push_back(" float geo=dot(halfV,halfV);geo=geo==0.0?0.0:min(ndl/geo,1.0);");
        if (st.geometric0) {
            lines.push_back(" spec0*=geo;");
        }
        if (st.geometric1) {
            lines.push_back(" spec1*=geo;");
        }
    }
    const std::string sp = ((c0 >> 16) & 1) ? "shadowTerm" : "vec3(1.0)";
    const std::string ss = ((c0 >> 17) & 1) ? "shadowTerm" : "vec3(1.0)";
    lines.push_back(" vec3 primaryShadow=lightFlags[i].z>0.5?" + sp + ":vec3(1.0);");
    lines.push_back(" vec3 secondaryShadow=lightFlags[i].z>0.5?" + ss + ":vec3(1.0);");
    lines.push_back(" diffuseSum+=(lightDiffuse[i]*ndl*primaryShadow+lightAmbient[i])*attenuation;");
    lines.push_back(" specularSum+=(spec0+spec1)*" + clampText + "*attenuation*secondaryShadow;");
    lines.push_back("}");
    lines.push_back("diffuseSum+=globalAmbient;");
    lines.push_back("vec4 "
                    "primary=vec4(useLighting==0?vec3(1.0):picaQuantize(diffuseSum),floor(clamp(primaryAlpha,0.0,1.0)*"
                    "255.0)/255.0);");
    lines.push_back("vec4 "
                    "secondary=vec4(useLighting==0?vec3(0.0):picaQuantize(specularSum),floor(clamp(secondaryAlpha,0.0,"
                    "1.0)*255.0)/255.0);");
    return lines;
}

// Slot 0/2 coordinates relative to slot one (Python int 0 when slot one's scale is zero).
std::string uvFixups(std::string source, const Material& m) {
    const TexTransform& t1 = m.transforms[1];
    if (t1.v[2] != 0 || m.transforms[2].v[2] != 0) {
        throw ImportError("Rotated texture transform");
    }
    struct Slot {
        int slot;
        const TexTransform* t;
        const char* sampler;
    };
    for (const Slot& s : {Slot{2, &m.transforms[2], "detail"}, Slot{0, &m.transforms[0], "textureZero"}}) {
        if (((s.t->flags >> 16) & 255) == 3) {
            continue;   // a projection map (projectionShader): not derived from the vertex coordinates
        }
        double so[2], ss[2], so1[2], ss1[2];
        scaleOffset(*s.t, ss, so);
        scaleOffset(t1, ss1, so1);
        // Without a slot-one transform the vertex coordinates are the raw ones (transformUv), so
        // the slot's own transform applies as it is (e.g. the coin stack's side, whose normal map
        // is its only texture).
        const bool rawBase = !(t1.flags & 255) && (s.t->flags & 255);
        if (rawBase) {
            ss1[0] = ss1[1] = 1.0;
            so1[0] = so1[1] = 0.0;
        }
        std::string scaleText, offsetText;
        for (int k = 0; k < 2; ++k) {
            const bool zero = !rawBase && t1.v[size_t(k)] == 0;
            const double scale = rawBase ? ss[k] : zero ? 0.0 : s.t->v[size_t(k)] / t1.v[size_t(k)];
            const double offset = so[k] - so1[k] * scale;
            if (k) {
                scaleText += ",";
                offsetText += ",";
            }
            scaleText += zero ? "0" : formatFloat(scale);
            offsetText += formatFloat(offset);
        }
        const std::string uv = "gl_TexCoord[0].xy*vec2(" + scaleText + ")+vec2(" + offsetText + ")";
        const std::string tex = "vec4 tex" + std::to_string(s.slot) + "=texture2D(" + s.sampler;
        source = replaceAll(source, tex + ",gl_TexCoord[0].xy);", tex + "," + uv + ");");
    }
    return source;
}

}   // namespace

std::string picaLightingFragment(const std::string& tevSource, const Material& m, const PicaState& st) {
    std::string source = uvFixups(tevSource, m);
    std::vector<std::string> kept;
    for (const auto& x : splitLines(source)) {
        if (!(startsWith(x, "vec3 light=") || startsWith(x, "vec4 primary=") || startsWith(x, "float oldLight="))) {
            kept.push_back(x);
        }
    }
    source = join(kept, "\n") + "\n";
    source = replaceAll(source, "void main()", std::string(kLightingUniforms) + kShadowGlsl + "void main()");
    source =
        replaceAll(source, "vec4 previous=vec4(0.0);", join(lightingBlock(st), "\n") + "\nvec4 previous=vec4(0.0);");
    return replaceAll(source, "gl_FragColor=previous;",
                      "if(showShadowMask!=0){gl_FragColor=vec4(shadowTerm,1.0);return;}\ngl_FragColor=previous;");
}

bool usesLighting(const Material& m) {
    static const std::map<uint32_t, int> operands = {{0, 1}, {1, 2}, {2, 2}, {3, 2}, {4, 3},
                                                     {5, 2}, {6, 2}, {7, 2}, {8, 3}, {9, 3}};
    auto count = [](uint32_t mode) {
        auto it = operands.find(mode);
        return it == operands.end() ? 3 : it->second;
    };
    for (uint32_t base : {0xc0u, 0xc8u, 0xd0u, 0xd8u, 0xf0u, 0xf8u}) {
        const uint32_t src = regOr(m.fragment, base), mode = regOr(m.fragment, base + 2);
        std::vector<int> used;
        for (int k = 0; k < count(mode & 15); ++k) {
            used.push_back(k);
        }
        for (int k = 0; k < count((mode >> 16) & 15); ++k) {
            used.push_back(4 + k);
        }
        for (int k : used) {
            const uint32_t s = (src >> (4 * k)) & 15;
            if (s == 1 || s == 2) {
                return true;
            }
        }
    }
    return false;
}

std::string animatedUvShader(std::string source, const std::vector<int>& slots) {
    static const char* names[3] = {"textureZero", "albedo", "detail"};
    for (int slot : slots) {
        const std::string prefix = "vec4 tex" + std::to_string(slot) + "=texture2D(" + names[slot] + ",";
        std::string line;
        for (const auto& x : splitLines(source)) {
            if (startsWith(x, prefix)) {
                line = x;
                break;
            }
        }
        if (line.empty()) {
            throw ImportError("Shader lacks a texture slot");
        }
        const std::string n = std::to_string(slot);
        source =
            replaceAll(source, line,
                       "vec4 tex" + n + "=texture2D(" + names[slot] + ",vec2(dot(vec3(gl_TexCoord[0].xy,1.0),texU" + n +
                           "),dot(vec3(gl_TexCoord[0].xy,1.0),texV" + n + ")));");
        source =
            replaceFirst(source, "void main()", "uniform vec3 texU" + n + ";uniform vec3 texV" + n + ";\nvoid main()");
    }
    return source;
}

// Texture slots whose coordinates are a projection map (texture coordinator mapping 3, flags bits
// 16-23; reference camera 0xff: the current camera): the fragment's eye-space position projected
// by the camera, as a [0, 1] screen coordinate, takes the place of the vertex coordinate in the
// slot's (animated) texture matrix. Hypothesis: the projection's mapping to [0, 1]; the water
// (RevolverRuinsStepWater) uses it for its moving specular pattern.
std::string projectionShader(std::string source, const Material& m) {
    static const char* names[3] = {"textureZero", "albedo", "detail"};
    bool any = false;
    for (int slot = 0; slot < 3; ++slot) {
        if (((m.transforms[size_t(slot)].flags >> 16) & 255) != 3) {
            continue;
        }
        const std::string prefix = "vec4 tex" + std::to_string(slot) + "=texture2D(" + names[slot] + ",";
        for (const auto& x : splitLines(source)) {
            if (startsWith(x, prefix)) {
                source = replaceAll(source, x, replaceAll(x, "gl_TexCoord[0].xy", "picaProjection()"));
                any = true;
                break;
            }
        }
    }
    if (any) {
        source = replaceFirst(source, "void main()",
                              "vec2 picaProjection(){vec4 c=gl_ProjectionMatrix*vec4(eyePosition,1.0);"
                              "return c.xy/c.w*0.5+0.5;}\nvoid main()");
    }
    return source;
}

// A "$water" material (metadata; RevolverRuinsStepWater's River_00 / RiverSide_00): its slot 0 is
// a projection map from the current camera turned by -90 degrees, the way the 3DS frame buffer is
// stored, and slots 0 and 2 hold the same placeholder texture. Hypothesis: the runtime binds a copy
// of the scene drawn so far to them, so the water shows what lies behind it (the drawer category
// "Water" is drawn after the scene). Both slots read the copy at the fragment's screen position.
// A texture in slot 2 without a coordinate transform (flags 0, scale 0): the GPU trace of the
// RevolverRuinsStage river shows unit 2 reading texcoord 2 equal to texcoord 1, scroll included,
// so the slot samples at slot 1's coordinates (hypothesis for other materials: the same vertex
// shader output). Run after the texture-animation rewrite so the copy is animated too.
std::string slotTwoCoordinates(std::string source, const Material& m) {
    const TexTransform& t = m.transforms[2];
    if (!m.textures[2] || *m.textures[2] == "shadowmap" || (t.flags & 255) || t.v[0] != 0 || t.v[1] != 0) {
        return source;
    }
    std::string two, one;
    for (const auto& x : splitLines(source)) {
        if (startsWith(x, "vec4 tex2=texture2D(detail,")) {
            two = x;
        } else if (startsWith(x, "vec4 tex1=texture2D(albedo,")) {
            one = x;
        }
    }
    if (two.empty() || one.empty()) {
        return source;
    }
    return replaceAll(source, two, "vec4 tex2=texture2D(detail," + one.substr(27));
}

std::string waterShader(std::string source, const Material& m) {
    if (!m.metadata.isObject() || !m.metadata.has("$water")) {
        return source;
    }
    // Slot 0 projects the frame's colour copy (GPU trace of RevolverRuinsStage, river draw: unit 0
    // reads the 256x512 copy at projected coordinates); slots 1 and 2 keep their own textures
    // (unit 2 reads SeaPattern00_J_spc at the mesh coordinates, grey ~196).
    for (const auto& x : splitLines(source)) {
        if (startsWith(x, "vec4 tex0=texture2D(textureZero,")) {
            source = replaceAll(source, x, "vec4 tex0=texture2D(sceneColor,sceneUv());");
            break;
        }
    }
    // A headset drawing both eyes at once copies them side by side (sceneViews 2): each eye reads its
    // half (deviceViewIndex, the device's view of the drawn layer; 0 elsewhere).
    return replaceFirst(source, "void main()",
                        "uniform sampler2D sceneColor;uniform vec4 sceneViewport;uniform float sceneViews;\n"
                        "vec2 sceneUv(){return ((gl_FragCoord.xy-sceneViewport.xy)/sceneViewport.zw"
                        "+vec2(float(deviceViewIndex),0.0))/vec2(sceneViews,1.0);}\nvoid main()");
}

std::string cubeShader(std::string source) {
    static const char* fn = "vec4 picaCube(vec3 d){vec3 a=abs(d);float x,y,z;float face;\n"
                            " if(a.x>a.y&&a.x>a.z){if(d.x>0.0){face=0.0;y=-d.y;}else{face=1.0;y=d.y;}x=-d.z;z=d.x;}\n"
                            " else if(a.y>a.z){if(d.y>0.0){face=2.0;x=d.x;}else{face=3.0;x=-d.x;}y=d.z;z=d.y;}\n"
                            " else{if(d.z>0.0){face=4.0;y=-d.y;}else{face=5.0;y=d.y;}x=d.x;z=d.z;}\n"
                            " vec2 st=vec2(x,y)/z*0.5+0.5;st=clamp(st,0.5/16.0,1.0-0.5/16.0);\n"
                            " return texture2D(textureZero,vec2(st.x,(5.0-face+st.y)/6.0));}\n";
    std::string line;
    for (const auto& x : splitLines(source)) {
        if (startsWith(x, "vec4 tex0=texture2D(textureZero,")) {
            line = x;
            break;
        }
    }
    if (line.empty()) {
        throw ImportError("Shader lacks texture slot 0");
    }
    source = replaceAll(source, line, "vec4 tex0=picaCube(reflect(normalize(eyePosition),normalize(normal)));");
    return replaceFirst(source, "void main()", std::string(fn) + "void main()");
}

std::string animatedConstantShader(const std::string& source, const std::vector<int>& stages,
                                   const std::string& uniform) {
    const std::string sep = "{vec4 constantColor=";
    std::vector<std::string> parts;
    size_t start = 0;
    while (true) {
        const size_t at = source.find(sep, start);
        if (at == std::string::npos) {
            parts.push_back(source.substr(start));
            break;
        }
        parts.push_back(source.substr(start, at - start));
        start = at + sep.size();
    }
    for (int stage : stages) {
        std::string& part = parts.at(size_t(stage + 1));
        part = uniform + ";" + part.substr(part.find(';') + 1);
    }
    return replaceFirst(join(parts, sep), "void main()", "uniform vec4 " + uniform + ";\nvoid main()");
}

}   // namespace captain_toad
