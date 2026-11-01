// NW layout (CTR, FLYT/FLAN 7.2.1.0) reader: BFLYT panes/materials/groups and BFLAN pane and
// material animations, as the plain JSON the viewer's layout renderer reads. General container
// format; field meanings follow public layout documentation, checked against this ROM's files.
#pragma once
#include "Binary.hpp"
#include "Json.hpp"

namespace captain_toad {

json::Value readBflyt(const Bytes& d);
json::Value readBflan(const Bytes& d);

}   // namespace captain_toad
