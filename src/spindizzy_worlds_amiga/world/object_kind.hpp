#pragma once
#include <string_view>

// The kinds of objects a level places (the high byte of an object
// command's second word). Numbers and names follow the original level
// editor's "Select Object" menu, whose text survives unused on the disk
// ($443B6-$444A0); checked against Tutorial level 1 (jewels spin, the
// lift's 8 parameter bytes, the button tests $3616, the starfield's
// "#Stars"/"Seed" pair) and the object code table $148C2.
namespace spindizzy::world {

enum class ObjectKind {
    jewel = 1,
    lift = 2,
    exit_point = 3,
    push_button = 4,
    binary_switch = 5,
    boink = 6,
    timer = 7,
    secret_passage = 8,
    pac = 9,
    pyramid = 10,
    heads = 11,
    spark_generator = 12,
    clue = 13,
    starfield = 14,
    message = 15,
    palette = 16,
};

inline std::string_view object_kind_name(int kind) {
    static constexpr std::string_view names[] = {
        "?", "Jewel", "Lift", "Exit point", "Push button", "Binary switch", "Boink", "Timer", "Secret passage",
        "Pac alien", "Pyramid", "Heads", "Spark generator", "Clue", "Starfield", "Message", "Palette",
    };
    return kind >= 1 && kind <= 16 ? names[kind] : names[0];
}

} // namespace spindizzy::world
