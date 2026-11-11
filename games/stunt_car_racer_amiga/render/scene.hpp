#pragma once
#include "platform/scene3d.hpp"
#include "stunt_car_racer_amiga/render/race_view.hpp"

namespace stuntcar::render {
// One presentation unit = 256 plan units = 1024 height units. This is not
// a claim about physical metres. Original track and collision data stay intact.
platform::scene3d::Vec3 scene_point(double x,double height,double z);
platform::scene3d::Mesh track_scene(const data::Circuits&,const data::Track&,const Palette&,bool textured=false);
// New world-space interpretation of the original screen-space opponent.
platform::scene3d::Mesh moving_scene(const data::Circuits&,const data::Track&,const data::Scenery&,
                                     const Camera&,const std::optional<Opponent>&);
platform::scene3d::View perspective_camera(const Camera&);
Opponent interpolate_opponent(const data::Track&,const Opponent&,const Opponent&,double fraction);
Camera interpolate_camera(const Camera& previous,const Camera& current,double fraction);
} // namespace stuntcar::render
