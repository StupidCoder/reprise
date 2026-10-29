#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <vector>

// A model of the DS 3D hardware as the game drives it: the geometry engine
// behind the command FIFO (0x04000400, packed command words and the direct
// command ports 0x04000440..) and the rendering engine (DISP3DCNT, clear
// plane, edge colours, toon table). Written for this project from the
// hardware's documented behaviour; it renders with the rasterizer in
// raster.hpp and aims to look right rather than match the hardware pixel
// for pixel. The game-side model library (nds3d/g3d.*) only issues commands,
// as the game's own 3D runtime does.
namespace picarat::nds3d {

using fx32 = std::int32_t; // 20.12 fixed point

struct Matrix {
    std::array<fx32, 16> m{};
    static Matrix identity();
}; // row-vector convention: v * M; m[row*4+col]

enum Command : std::uint8_t {
    MTX_MODE = 0x10,
    MTX_PUSH = 0x11,
    MTX_POP = 0x12,
    MTX_STORE = 0x13,
    MTX_RESTORE = 0x14,
    MTX_IDENTITY = 0x15,
    MTX_LOAD_4x4 = 0x16,
    MTX_LOAD_4x3 = 0x17,
    MTX_MULT_4x4 = 0x18,
    MTX_MULT_4x3 = 0x19,
    MTX_MULT_3x3 = 0x1a,
    MTX_SCALE = 0x1b,
    MTX_TRANS = 0x1c,
    COLOR = 0x20,
    NORMAL = 0x21,
    TEXCOORD = 0x22,
    VTX_16 = 0x23,
    VTX_10 = 0x24,
    VTX_XY = 0x25,
    VTX_XZ = 0x26,
    VTX_YZ = 0x27,
    VTX_DIFF = 0x28,
    POLYGON_ATTR = 0x29,
    TEXIMAGE_PARAM = 0x2a,
    PLTT_BASE = 0x2b,
    DIF_AMB = 0x30,
    SPE_EMI = 0x31,
    LIGHT_VECTOR = 0x32,
    LIGHT_COLOR = 0x33,
    SHININESS = 0x34,
    BEGIN_VTXS = 0x40,
    END_VTXS = 0x41,
    SWAP_BUFFERS = 0x50,
    VIEWPORT = 0x60,
    BOX_TEST = 0x70,
    POS_TEST = 0x71,
    VEC_TEST = 0x72,
};

int parameterCount(unsigned command); // -1 for unknown commands

class Gpu3D {
public:
    Gpu3D();
    // --- Geometry engine.
    void command(unsigned command, std::span<const std::uint32_t> parameters);
    // A FIFO stream of packed command words (display lists, 0x04000400 writes).
    void packed(std::span<const std::uint32_t> words);
    // --- VRAM as the texture image / palette slots see it.
    std::vector<std::uint8_t> textureVram, paletteVram; // 512 KiB, 96 KiB
    // --- Rendering engine registers.
    std::uint16_t disp3dcnt = 0; // 0x04000060
    std::uint16_t clearColor = 0;
    std::uint8_t clearAlpha = 0, clearPolygonId = 0;
    std::uint16_t clearDepth = 0x7fff;         // 0x04000350/0x04000354
    std::array<std::uint16_t, 8> edgeColors{}; // 0x04000330
    std::array<std::uint16_t, 32> toonTable{}; // 0x04000380
    std::uint8_t alphaTestReference = 0;       // 0x04000340

    // Rendered frame (after SWAP_BUFFERS): 6-bit colour components and 5-bit alpha per pixel.
    struct Pixel {
        std::uint8_t r = 0, g = 0, b = 0, a = 0;
    };

    const std::array<Pixel, 256 * 192>& frame() const { return frame_; }

    // CLIPMTX_RESULT (0x04000640) / VECMTX_RESULT (0x04000680) as the runtime reads them back.
    Matrix clipMatrix() { return clipMatrixNow(); }

    Matrix vectorMatrix() const { return vector_; }

    unsigned polygonsLastFrame() const { return lastPolygonCount_; }

private:
    // A vertex after transformation: clip-space position, colour (5-bit
    // components) and texture coordinates (texels * 16).
    struct Vertex {
        fx32 x = 0, y = 0, z = 0, w = 0;
        int r = 0, g = 0, b = 0;
        int s = 0, t = 0;
    };

    // A polygon as submitted, with the state that was current for it.
    struct Polygon {
        std::vector<Vertex> vertices; // clip space, before clipping
        std::uint32_t attributes = 0, textureParameters = 0, paletteBase = 0;
        bool wBuffer = false;
    };

    // Matrices (row-vector convention).
    int matrixMode_ = 0; // 0 projection, 1 position, 2 position & vector, 3 texture
    Matrix projection_, position_, vector_, texture_;
    Matrix projectionSaved_, textureSaved_;
    std::array<Matrix, 32> positionStack_{}, vectorStack_{};
    int stackPointer_ = 0;
    // Vertex state.
    std::uint32_t nextAttributes_ = 0, attributes_ = 0, textureParameters_ = 0, paletteBase_ = 0;
    int primitive_ = 0;
    bool open_ = false;
    std::vector<Vertex> pending_; // vertices of the primitive being built
    int stripPolygons_ = 0;
    fx32 lastX_ = 0, lastY_ = 0, lastZ_ = 0;
    int red_ = 31, green_ = 31, blue_ = 31;
    std::int32_t texS_ = 0, texT_ = 0; // current texture coordinates (texels * 16)
    std::int32_t rawS_ = 0, rawT_ = 0; // as given by TEXCOORD
    // Lighting.
    std::uint16_t diffuse_ = 0, ambient_ = 0, specular_ = 0, emission_ = 0;
    bool useShininessTable_ = false;
    std::array<std::uint8_t, 128> shininess_{};
    std::array<std::array<float, 3>, 4> lightDirection_{};
    std::array<std::uint16_t, 4> lightColor_{};
    // Viewport (x1, y1 from the bottom-left, x2, y2 inclusive).
    int viewX1_ = 0, viewY1_ = 0, viewX2_ = 255, viewY2_ = 191;
    // Frame.
    std::vector<Polygon> polygons_;
    std::array<Pixel, 256 * 192> frame_{};
    unsigned lastPolygonCount_ = 0;
    bool manualSort_ = false, wBuffer_ = false;

    Matrix clipMatrixNow() const;
    Matrix& target();
    void apply(const Matrix& m, bool scaleOnly);
    void addVertex(fx32 x, fx32 y, fx32 z);
    void normal(std::uint32_t parameter);
    void texcoord(std::uint32_t parameter);
    void submit(std::vector<Vertex> vertices);
    void render();
};

Matrix multiply(const Matrix& a, const Matrix& b); // a * b with 20.12 rounding as the hardware does
} // namespace picarat::nds3d
