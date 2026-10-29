// The sixth adventure's base play loop: what every puzzle loop of the
// sixth adventure has whatever its class (the standard loop 3100 too).
// 0x230e94, the base class's command method, takes:
//   10000 (+0x8c, 0x301ce4): room for an undo history, once: N values, or
//         N records of K values (10000 N K);
//   10001 (+0x90, 0x301a84): one record pushed (exactly K operands; ints,
//         unsigned values and floats keep their kind, other operands are
//         skipped);
//   10002 (+0x94, 0x3018f0): the newest record popped into the K operand
//         registers, last value first; the result says whether any was
//         there;
//   10003 (+0x98, 0x301ccc, shared by about 35 loop vtables): the history
//         emptied;
//   12000: taken, nothing done.
// The history is a ring (0x301da0: a value buffer +0x168 and a kind table
// +0x164, write positions +0x180 / +0x184, bytes used +0x174 capped at the
// size +0x178): when full, a push overwrites the oldest values. Thirty of
// the sixth adventure's puzzles keep their undo button's moves in it (020,
// 021, 040 and 041 on the TouchPad loop; most others on the standard
// loop).
//
// TouchPad (loop 3340, vtable 0x3c9c44, constructor 0x287ed8) has no
// commands of its own: its command method is the arrow pad router
// 0x22fd90 (a6_arrow_pad) falling back to 0x230e94, and its update
// 0x32c1c4 redraws the pad when it changed.
#include "puzzles/a5_special.hpp"
#include "puzzles/a6_arrow_pad.hpp"
#include "puzzles/a6_pad_hooks.hpp"

namespace picarat::adventure5 {
namespace {

using xq::Call;
using xq::Value;

class UndoRing {
public:
    // A base-loop command (10000..10003, 12000); false when the number is
    // not one or the operands do not fit.
    bool command(int number, Call& c, std::size_t first) {
        const std::size_t count = c.count() - first;
        switch (number) {
        case 10000: {
            if (count < 1 || count > 2 || !values_.empty()) {
                return false;
            }
            const int records = c.integer(first);
            const int each = count > 1 ? c.integer(first + 1) : 1;
            if (records >= 1 && each >= 1) {
                values_.assign(std::size_t(records * each), {});
                each_ = std::size_t(each);
            }
            return true;
        }
        case 10001:
            if (each_ == 0 || count != each_) {
                return false;
            }
            for (std::size_t i = 0; i < count; ++i) {
                const Value& v = c.at(first + i);
                if (!v.isNumber()) {
                    continue;
                }
                values_[next_] = {v.kind, v.bits, {}, {}};
                next_ = (next_ + 1) % values_.size();
                used_ = std::min(used_ + 1, values_.size());
            }
            return true;
        case 10002: {
            if (each_ == 0 || count != each_) {
                return false;
            }
            bool any = false;
            for (std::size_t i = count; i-- > 0;) {
                if (used_ == 0) {
                    continue;
                }
                --used_;
                next_ = (next_ + values_.size() - 1) % values_.size();
                any = true;
                if (Value* r = c.reg(first + i)) {
                    *r = values_[next_];
                }
            }
            c.result = Value::ofInt(any ? 1 : 0);
            return true;
        }
        case 10003:
            next_ = used_ = 0;
            return true;
        case 12000:
            return true;
        default:
            return false;
        }
    }

private:
    std::vector<Value> values_; // the ring (numbers only)
    std::size_t each_ = 0;      // +0x16c: values per record
    std::size_t next_ = 0;      // where the next value goes
    std::size_t used_ = 0;      // values held
};

// Any sixth-adventure loop: the history, then the loop's own class (none
// on the standard loop).
class SixthLoop final : public SpecialPuzzle {
public:
    SixthLoop(Game& game, std::unique_ptr<SpecialPuzzle> own) : game_(game), own_(std::move(own)) {
        padHooks(game_).clear(); // a new puzzle's scope
    }

    bool command(int number, Call& c, std::size_t first) override {
        return undo_.command(number, c, first) || (own_ && own_->command(number, c, first));
    }

    void input() override {
        runPadHooks(game_);
        if (own_) {
            own_->input();
        }
    }

    void update() override {
        if (own_) {
            own_->update();
        }
    }

    std::string describe() const override { return own_ ? own_->describe() : std::string(); }

private:
    Game& game_;
    UndoRing undo_;
    std::unique_ptr<SpecialPuzzle> own_;
};

class TouchPadPuzzle final : public SpecialPuzzle {
public:
    explicit TouchPadPuzzle(Game& game) : game_(game) {}

    bool command(int number, Call& c, std::size_t first) override {
        return arrows_.command(game_, number, c, first);
    }

private:
    Game& game_;
    ArrowPad arrows_;
};

} // namespace

std::unique_ptr<SpecialPuzzle> makeSixthLoop(Game& game, std::unique_ptr<SpecialPuzzle> own) {
    return std::make_unique<SixthLoop>(game, std::move(own));
}

std::unique_ptr<SpecialPuzzle> makeTouchPadPuzzle(Game& game) {
    return std::make_unique<TouchPadPuzzle>(game);
}

} // namespace picarat::adventure5
