#include "tower_of_babel_amiga/data/shape.hpp"

namespace babel::data {
namespace {

class Reader {
public:
    Reader(const Memory& memory, std::uint32_t at) : memory_(memory), at_(at) {}
    std::uint32_t at() const { return at_; }
    void seek(std::uint32_t at) { at_ = at; }
    int u8() { return memory_.u8(at_++); }
    int s8() { return std::int8_t(memory_.u8(at_++)); }
    Vec3 vec() {
        const int x = s8(), y = s8(), z = s8();
        return {2 * x, 2 * y, 2 * z};
    }

private:
    const Memory& memory_;
    std::uint32_t at_;
};

} // namespace

Shape decode_shape(const Memory& main, std::uint32_t address) {
    Shape shape;
    Reader in(main, address);
    const int shadow_length = in.u8();
    const std::uint32_t parts = in.at() + std::uint32_t(shadow_length);
    const int shadows = in.s8();
    for (int i = 0; i < shadows; ++i) {
        const int points = in.s8();
        auto& polygon = shape.shadows.emplace_back();
        for (int k = 0; k < points; ++k) polygon.push_back(in.vec());
    }
    in.seek(parts);
    for (;;) {
        const int next = in.u8();
        ShapePart part;
        part.centre = in.vec();
        const std::uint32_t body = in.at();
        const int faces = in.s8();
        for (int f = 0; f < faces; ++f) {
            ShapeFace face;
            const int points = in.s8();
            face.style = in.u8();
            face.normal = in.vec();
            for (int k = 0; k < points; ++k) face.points.push_back(in.vec());
            part.faces.push_back(std::move(face));
        }
        shape.parts.push_back(std::move(part));
        if (next == 0) break;
        in.seek(body + std::uint32_t(next));
    }
    return shape;
}

} // namespace babel::data
