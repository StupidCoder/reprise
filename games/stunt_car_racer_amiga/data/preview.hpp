#pragma once
#include "platform/image.hpp"
#include "stunt_car_racer_amiga/data/assets.hpp"
#include "stunt_car_racer_amiga/data/track.hpp"
#include "ui/model_view.hpp"
#include <memory>

// Pictures of the decoded data, for the launcher's viewer and the export tool.
namespace stuntcar::data {

platform::Image to_image(const Picture&);

// A circuit as a 3D model: road surface, red and white walls down to the
// ground (as in the game's own track pictures) and the arena floor. Model
// units are 1/256 of plan units; heights a quarter of that.
std::shared_ptr<const ui::Model> circuit_model(const Circuits&, const Track&);

// The model seen from the onlookers' side, slightly above.
platform::Image circuit_image(const ui::Model&);

} // namespace stuntcar::data
