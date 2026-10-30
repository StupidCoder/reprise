#pragma once
// Lessons share navigation, not a requirement to simulate a game. An article
// or experiment supplies its own SDL canvas and consumes ordinary shell input.
#include "platform/host.hpp"
namespace app {
class Lesson {
  public:
    virtual ~Lesson() = default;
    // Opt-in responsive canvas; older live lessons retain their own design size.
    virtual void set_canvas(int, int) {}
    virtual void update(const platform::Controls &, double now) = 0;
    virtual platform::Image draw() const = 0;
};
} // namespace app
