#pragma once
#include "tower_of_babel_amiga/data/assets.hpp"
#include "tower_of_babel_amiga/data/shape.hpp"
#include "ui/model_view.hpp"
#include <memory>
#include <vector>

// The game's 3D shapes as models for the launcher's model viewer (asset
// viewer only; the game draws its shapes with its own renderer).
namespace babel::data {

// A designer object kind and the shape it starts with: the kind table
// $13742 (10 bytes per kind 4-39; +2 the shape's offset from $18200).
struct KindShape {
    int kind{};
    std::uint32_t address{};
};
std::vector<KindShape> kind_shapes(const Memory& program);

// The shape's faces in the colours of `tower`'s palette as a lit cell shows
// them (dithered pens averaged; a see-through pen's visible colour; an
// object's own pens as the first spider's). Shadows are left out.
std::shared_ptr<const ui::Model> shape_model(const Shape&, const Memory& program, const Tower& tower);

} // namespace babel::data
