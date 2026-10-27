#include "marble_madness_amiga/game/display_list.hpp"
#include <utility>

namespace marble_madness::game {
namespace {

std::int16_t sum(std::int16_t a, std::int16_t b, std::int16_t c) { return std::int16_t(a + b + c); }

} // namespace

// $1113DC: by the sums of the boxes' near and far corners (x + y + z grows
// towards the viewer), and where those overlap by the boxes themselves.
bool DisplayList::in_front(const Box& a, const Box& b) {
    if (!(sum(b.z0, b.y0, b.x0) < sum(a.z1, a.y1, a.x1))) {
        return false;
    }
    return sum(b.z1, b.y1, b.x1) <= sum(a.z0, a.y0, a.x0) ||
           (b.y0 < a.y1 && (b.y1 <= a.y0 || (b.x0 < a.x1 && (b.x1 <= a.x0 || b.z0 < a.z1))));
}

void DisplayList::add(std::uint8_t type, std::uint8_t index, const BoxOf& box_of) {
    const Box box = box_of(type, index);
    // Where it goes: before the first entry that belongs in front of it.
    int at = 0;
    while (at < capacity && order_[std::size_t(at)] != 0xFF && !in_front(slots_[order_[std::size_t(at)]].box, box)) {
        ++at;
    }
    if (at >= capacity) {
        return;
    }
    int free = 0;
    while (free < capacity && slots_[std::size_t(free)].type != 0) {
        ++free;
    }
    if (free >= capacity) {
        return;
    }
    slots_[std::size_t(free)].type = type;
    slots_[std::size_t(free)].index = index;
    if (order_[std::size_t(at)] != 0xFF) {
        int end = at + 1;
        while (end < capacity && order_[std::size_t(end)] != 0xFF) {
            ++end;
        }
        int last = end - 1;
        if (last == capacity - 1) {
            last = end - 2;
        }
        for (int i = last; i >= at; --i) {
            order_[std::size_t(i + 1)] = order_[std::size_t(i)];
        }
    }
    order_[std::size_t(at)] = std::uint8_t(free);
}

void DisplayList::remove(std::uint8_t type, std::uint8_t index) {
    int at = 0;
    for (;; ++at) {
        if (at >= capacity || order_[std::size_t(at)] == 0xFF) {
            return;
        }
        const auto& s = slots_[order_[std::size_t(at)]];
        if (s.type == type && s.index == index) {
            break;
        }
    }
    slots_[order_[std::size_t(at)]].type = 0;
    int end = at + 1;
    while (end < capacity && order_[std::size_t(end)] != 0xFF) {
        ++end;
    }
    for (; at < end - 1; ++at) {
        order_[std::size_t(at)] = order_[std::size_t(at + 1)];
    }
    order_[std::size_t(at)] = 0xFF;
}

void DisplayList::sort(const BoxOf& box_of) {
    for (int i = 0; i < capacity && order_[std::size_t(i)] != 0xFF; ++i) {
        auto& s = slots_[order_[std::size_t(i)]];
        s.box = box_of(s.type, s.index);
    }
    // $10E044 with gaps 1, 2, 3.
    for (int gap = 1; gap <= 3; ++gap) {
        for (int i = 0; i + gap < capacity + 1; ++i) {
            auto& a = order_[std::size_t(i)];
            auto& b = order_[std::size_t(i + gap)];
            if (a == 0xFF || b == 0xFF) {
                break;
            }
            if (in_front(slots_[a].box, slots_[b].box)) {
                std::swap(a, b);
            }
        }
    }
}

} // namespace marble_madness::game
