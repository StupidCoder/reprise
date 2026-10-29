// The sixth adventure's texture-swap loop (3160, class "ChangeTexture";
// catalogue type 42: puzzle 068, the music box whose lid shows the
// stained glass of the lower screen). The puzzle itself is a script
// select puzzle; the class only puts a picture - the lower screen captured
// into a render target (5440) - on a mesh of a model.
//
// Reconstructed from the class's command method 0x339070 (vtable 0x3ca1f0,
// commands 0 and 1, the rest to the base 0x230e94):
//   0 (model object, mesh, picture): remembers them (0x28a938 finds the
//     mesh by its name's hash, 068: "n068_01a_output.skin2" the lid; +0x190
//     the mesh, +0x194 the picture, +0x198 a copy of its texture state);
//   1 (u, v, width, height): the mesh draws the picture through a texture
//     state whose coordinates are scaled by the width / height and moved by
//     u / v (0x2172dc builds it into +0x314; the mesh's +0x78).
// The rectangle is in the console's texture: the 320 x 240 capture sits in
// a 512 x 256 one (hypothesis: the power-of-two size the console's render
// target needs; 068's 0.05..0.57 x 0.15..0.79 is then the stained-glass
// panel), so the port scales it to the capture's own size.
#include "puzzles/a5_special.hpp"

namespace picarat::adventure5 {
namespace {

using adventure56::Object;
using adventure56::Resource;
using xq::Call;

int powerOfTwo(int n) {
    int p = 8;
    while (p < n) {
        p *= 2;
    }
    return p;
}

class TexturePuzzle final : public SpecialPuzzle {
public:
    explicit TexturePuzzle(Game& game) : game_(game) {}

    bool command(int number, Call& c, std::size_t first) override {
        const std::size_t count = c.count() - first;
        auto hashOf = [&](std::size_t i) {
            const xq::Value& v = c.at(first + i);
            return v.isText() ? xq::hash(v.text) : v.toUint();
        };
        switch (number) {
        case 0: {
            if (count < 3) {
                return false;
            }
            auto* o = game_.scene.object3D(hashOf(0));
            const Resource* r = game_.scene.resource(hashOf(2), Resource::Kind::Picture);
            if (!o || !r || !r->picture) {
                return false;
            }
            object_ = hashOf(0);
            mesh_ = hashOf(1);
            picture_ = r->picture;
            return true;
        }
        case 1: {
            auto* o = count >= 4 && picture_ ? game_.scene.object3D(object_) : nullptr;
            if (!o) {
                return false;
            }
            const float fw = float(powerOfTwo(picture_->width)) / float(picture_->width);
            const float fh = float(powerOfTwo(picture_->height)) / float(picture_->height);
            Object::MeshTexture t;
            t.texture = picture_;
            t.scale[0] = c.real(first + 2) * fw;
            t.scale[1] = c.real(first + 3) * fh;
            t.offset[0] = c.real(first) * fw;
            t.offset[1] = c.real(first + 1) * fh;
            o->meshTextures[mesh_] = t;
            return true;
        }
        default:
            return false;
        }
    }

private:
    Game& game_;
    std::uint32_t object_ = 0, mesh_ = 0;
    std::shared_ptr<ctr3d::Texture> picture_;
};

} // namespace

std::unique_ptr<SpecialPuzzle> makeTexturePuzzle(Game& game) {
    return std::make_unique<TexturePuzzle>(game);
}

} // namespace picarat::adventure5
