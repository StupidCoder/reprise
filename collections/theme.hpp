#pragma once
#include "platform/image.hpp"
namespace app {
// Collection membership is optional and independent of the original machine.
enum class Collection { none, commodore, sonic, layton };
struct Theme {
    platform::Color paper, card, ink, muted, line, accent, pale, detail;
};
inline constexpr Theme theme_for(Collection collection) {
    switch (collection) {
    case Collection::sonic: return {{231,239,249},{250,249,239},{17,47,85},{66,96,132},{190,207,226},{24,86,217},{210,227,251},{244,200,76}};
    case Collection::layton: return {{241,235,221},{250,246,234},{35,76,80},{95,109,100},{210,199,177},{35,76,80},{228,221,199},{174,136,80}};
    default: return {{220,222,215},{243,242,233},{21,59,71},{76,101,108},{193,201,195},{37,78,207},{209,220,239},{200,117,54}};
    }
}
}
