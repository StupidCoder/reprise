#pragma once
#include "tower_of_babel_amiga/data/assets.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

// A tower as the designer edits it ($E386 on): its texts and settings, and
// two layers of 4 x 64 cells, floors from 0, rows z from 0 (the front), x
// from 0. The floor layer holds -1 (nothing), 0 a floor section, 1 a
// supporting block, 2 a lift that goes up, 3 one that goes down, and the
// lift's other end ($28 above a lift up, $29 below a lift down); the object
// layer -1 or an object kind 4-39. `from_record` / `to_record` are the
// unpacker $13024 and the packer $12E4A of the 288-byte tower records.
namespace babel::design {

constexpr int empty = -1, floor_section = 0, block = 1, lift_up = 2, lift_down = 3;
constexpr int lift_top = 0x28, lift_bottom = 0x29;
constexpr int zapper = 4, pusher = 5, grabber = 6, klondike = 0x1F;

// The editor's messages ($2D + n among the numbered texts).
enum Message {
    ok = 0, cannot_place_lift = 1, cannot_remove_lift = 2, cannot_place_support = 3, no_floor = 4,
    supported_block = 5, spider_placed = 6, too_many_objects = 7
};

struct TowerDesign {
    // The texts as the record keeps them: 62, 62 and 10 characters, any
    // that are not printable read as 0 ($136FE). A text ends at its first 0;
    // an edit writes the text and a 0 and leaves the characters after it.
    std::array<char, 62> name{}, author{};
    std::array<char, 10> password{};
    template <std::size_t N> static std::string text(const std::array<char, N>& a) {
        std::string s;
        for (const char c : a) {
            if (c == 0) break;
            s += c;
        }
        return s;
    }
    template <std::size_t N> static void set_text(std::array<char, N>& a, const std::string& s) {
        for (std::size_t i = 0; i < s.size() && i < N; ++i) a[i] = s[i];
        if (s.size() < N) a[s.size()] = 0;
    }
    int time_limit{};                   // seconds (14 bits)
    std::array<bool, 10> flags{};       // set = the original's -1: 0-2 spider absent, 3 unlocked, 4 cameras off,
                                        // 5 no time limit, 6 no lightning, 7 no meteors, 8 mines leave no hole
    int bomb_speed{}, klondikes{}, destroy{};
    int stars{}, pattern{}, sky_type{}, moon_heading{}, moon_type{}, moon_x{}, moon_y{}, moon_rotation{};
    std::array<std::int8_t, 256> cells{}, objects{};
    std::array<data::Colour, 18> colours{};
    int background{};
    bool no_footsteps{}, no_effects{};
    // The record it came from: the bits the format leaves unused are kept.
    std::array<std::uint8_t, data::tower_record_size> base{};

    static TowerDesign from_record(std::span<const std::uint8_t> record);
    // False with too many objects (more than 20; $12E4A's error 9).
    bool to_record(std::array<std::uint8_t, data::tower_record_size>& record) const;

    static int index(int floor, int x, int z) { return floor * 64 + z * 8 + x; }

    // The editing rules ($1098E): put a kind at a cell, or take what is there.
    Message place(int cell, int kind);
    Message remove(int cell);
    // $10C60: the spider flags from the object layer (a spider is "absent" until placed).
    void update_spiders();
    // $FFF2: lifts without their other end, blocks without anything to
    // carry, duplicate spiders and objects past the 17th are taken away.
    void repair();
    // The toolbox: one level (0-3) or, with -1, all four.
    void scroll(int level, int dx, int dz);  // $FEBA / $FF08 / $FF56 / $FFA4 (wrapping round)
    void rotate(int level);                  // $FE26: a quarter turn, the objects turned with it
    void copy_level(int from, int to);       // $FBB0
    void swap_levels(int a, int b);          // $FC76
    void clear_level(int level);             // $FD48
    void clear_all();                        // $FDCE

    // Extras.
    struct Statistics {
        int percent_used{}, zappers{}, pushers{}, klondikes{}, others{}, total{};
    };
    Statistics statistics() const;           // $10384
    // $104AE: from the ground floor up through blocks; the cells reached in
    // order (for the animation), and whether every cell was.
    bool support_check(std::vector<int>& reached) const;
    // $10614: from spider `spider`'s cell over the floors and lifts.
    bool path_check(int spider, std::vector<int>& reached, bool& placed) const;
};

} // namespace babel::design
