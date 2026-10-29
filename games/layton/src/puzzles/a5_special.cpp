// The fifth adventure's special puzzle loops by catalogue type. See
// a5_special.hpp.
#include "puzzles/a5_special.hpp"
#include <cmath>

namespace picarat::adventure5 {

bool isSpecialType(int type) {
    switch (type) {
    case 9:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 27:
    case 33:
    case 34:
    case 35:
    case 36:
    case 46:
    case 47:
        return true;
    default:
        return false;
    }
}

adventure56::Object& createQuadObject(Game& game, const std::string& name) {
    adventure56::Object& o = game.createObject(name, 1, 111, 0, {});
    o.content = 2;
    o.quads = {{-1, 0}, {-1, 1}, {0, 1}, {0, 0}};
    return o;
}

void addLineQuad(adventure56::Object& o, Vec2 a, Vec2 b, float width) {
    if (a == b) {
        return;
    }
    const float half = width * 0.5f;
    float nx = b[0] - a[0], ny = b[1] - a[1];
    const float length = nx * nx + ny * ny;
    if (length <= 0.0f) {
        nx = ny = 0.0f;
    } else {
        const float inv = 1.0f / std::sqrt(length);
        nx *= inv;
        ny *= inv;
    }
    // Along the line (al) and across it (ac).
    const Vec2 al{nx * half, ny * half}, ac{-al[1], al[0]};
    o.quads.push_back({a[0] - al[0] - ac[0], a[1] - al[1] - ac[1]});
    o.quads.push_back({a[0] - al[0] + ac[0], a[1] - al[1] + ac[1]});
    o.quads.push_back({b[0] + al[0] + ac[0], b[1] + al[1] + ac[1]});
    o.quads.push_back({b[0] + al[0] - ac[0], b[1] + al[1] - ac[1]});
}

void addDotQuad(adventure56::Object& o, Vec2 p, float half) {
    o.quads.push_back({p[0] - half, p[1] - half});
    o.quads.push_back({p[0] - half, p[1] + half});
    o.quads.push_back({p[0] + half, p[1] + half});
    o.quads.push_back({p[0] + half, p[1] - half});
}

bool hasSpecialRules(int type) {
    return (type >= 9 && type <= 16) || type == 27 || type == 33 || type == 34 || type == 35 ||
           type == 36 || type == 46 || type == 47;
}

std::unique_ptr<SpecialPuzzle> makeSixthPuzzle(Game& game, int loop) {
    switch (loop) {
    case 3140: // Fit
        return makeFitPuzzle(game);
    case 3170:
        return makeTransferPuzzle(game);
    case 3180:
        return makePartitionPuzzle(game);
    case 3230:
        return makeRopePuzzle(game);
    case 3160:
        return makeTexturePuzzle(game);
    case 3260:
        return makeRoutePuzzle(game);
    case 3270:
        return makeSlide2Puzzle(game);
    case 3290:
        return makeBeamPuzzle(game);
    case 3320:
        return makeGuardPuzzle(game);
    case 3330:
        return makeBridgePuzzle(game);
    case 3340:
        return makeTouchPadPuzzle(game);
    default:
        return nullptr;
    }
}

std::unique_ptr<SpecialPuzzle> makeSpecialPuzzle(Game& game, int type) {
    switch (type) {
    case 9:
        return makeLinePuzzle(game);
    case 10:
        return makeGridLinePuzzle(game);
    case 11:
        return makePairPuzzle(game);
    case 12:
        return makeBlockPuzzle(game);
    case 13:
        return makeGlidePuzzle(game);
    case 14:
        return makePlacePuzzle(game);
    case 15:
        return makePilePuzzle(game);
    case 16:
        return makeFloatPuzzle(game);
    case 27:
        return makeVanishPuzzle(game);
    case 33:
        return makeCornPuzzle(game);
    case 34:
        return makeBrickPuzzle(game);
    case 35:
        return makeSlipPuzzle(game);
    case 36:
        return makeFlipPuzzle(game);
    case 46:
        return makeStrokePuzzle(game);
    case 47:
        return makeBalloonPuzzle(game);
    default:
        return nullptr;
    }
}

} // namespace picarat::adventure5
