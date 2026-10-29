#pragma once
// Lessons share navigation, not a requirement to simulate a game. An article
// or experiment supplies its own SDL canvas and consumes ordinary shell input.
#include "platform/host.hpp"
namespace app {
class Lesson {
  public:
    virtual ~Lesson() = default;
    virtual void update(const platform::Controls &, double now) = 0;
    virtual platform::Image draw() const = 0;
};
} // namespace app
