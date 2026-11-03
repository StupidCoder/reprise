// Behaviour trees of the game's characters: a re-implementation of the engine's process runner
// (Game/AI/SlotProcess.cpp: add 0xb410, follow 0xb5a0, run 0xb8a0 / 0xb970, stop 0xbd00, events
// 0xbd70, channel query 0xbe50; .analysis/docs/enemies.md).
//
// A behaviour owns an ordered list of children. Each child claims a set of channels (bits). Every
// update the parent runs its children in order:
//   - a child returning Running owns its channels for this update: later children that share a
//     channel are not run (they keep their place);
//   - a child returning Waiting lends its channels: a later child that runs on one of them stops
//     it (unless an earlier Running child kept that later child from running);
//   - a child returning Done is stopped and removed.
// Then the parent's own update runs. Started behaviours get onStart before their first update and
// onStop when they end (children first). Events are bits a behaviour (or anything) sets on a
// subtree; behaviours poll them.
#pragma once

#include <array>
#include <cstdint>
#include <list>
#include <memory>

namespace beacon::game::ai {

enum class Status : int {
    Idle = 0,      // ran, claims nothing (not used by the game's behaviours so far)
    Running = 1,   // owns its channels this update
    Waiting = 2,   // keeps running but yields its channels to a later child
    Done = 3,      // finished: stopped and removed
};

template <class Owner>
class Behavior {
public:
    virtual ~Behavior() = default;

    // Appends a child that claims `channels` (0xb410).
    Behavior& add(std::unique_ptr<Behavior> child, std::uint32_t channels) {
        child->channels_ |= channels;
        child->parent_ = this;
        child->root_ = root_ ? root_ : this;
        child->creator_ = this;
        Behavior& r = *child;
        children_.push_back(std::move(child));
        return r;
    }

    // Queues a behaviour after this one in the parent's list, behind the ones this behaviour
    // queued before; it claims this behaviour's channels too (0xb5a0). Used for transitions:
    // the caller then returns Done.
    Behavior& follow(std::unique_ptr<Behavior> next) {
        next->channels_ |= channels_;
        next->parent_ = parent_;
        next->root_ = root_;
        next->creator_ = this;
        auto& list = parent_->children_;
        auto it = list.begin();
        bool seenSelf = false;
        for (; it != list.end(); ++it) {
            if (seenSelf && (*it)->creator_ != this) {
                break;
            }
            if (it->get() == this) {
                seenSelf = true;
            }
        }
        Behavior& r = *next;
        list.insert(it, std::move(next));
        return r;
    }

    // Sets event bit `event` on every descendant whose channels meet `mask` (0xbd70).
    void broadcast(int event, std::uint32_t mask = 0xffffffffu) {
        for (auto& c : children_) {
            if ((c->channels_ & mask) != 0) {
                c->events_ |= std::uint64_t(1) << event;
                c->broadcast(event, mask);
            }
        }
    }

    // True when no child claims any of `mask` (0xbe50); with all bits: no children at all.
    bool channelsFree(std::uint32_t mask = 0xffffffffu) const {
        if (mask == 0xffffffffu) {
            return children_.empty();
        }
        for (const auto& c : children_) {
            if ((c->channels_ & mask) != 0) {
                return false;
            }
        }
        return true;
    }

    // Children claiming any of `mask` (0xbe90; all bits: every child).
    int childCount(std::uint32_t mask = 0xffffffffu) const {
        if (mask == 0xffffffffu) {
            return int(children_.size());
        }
        int n = 0;
        for (const auto& c : children_) {
            n += (c->channels_ & mask) != 0;
        }
        return n;
    }
    // A started child holds one of `mask` (0xbf00).
    bool startedChildHolds(std::uint32_t mask) const {
        std::uint32_t held = 0;
        for (const auto& c : children_) {
            if (c->started_) {
                held |= c->channels_;
            }
        }
        return (held & mask) != 0;
    }
    // The first child (for inspection).
    const Behavior* firstChild() const { return children_.empty() ? nullptr : children_.front().get(); }
    virtual const char* label() const { return "Basic"; }

    Behavior* root() { return root_ ? root_ : this; }
    bool event(int e) const { return (events_ >> e) & 1; }
    std::uint64_t events() const { return events_; }
    std::uint32_t updates() const { return updates_; }   // updates run so far (+0x78)
    bool finished() const { return finished_; }

    // One update of this behaviour and its subtree (0xb8a0).
    Status run(Owner& owner) {
        if (!started_) {
            started_ = true;
            onStart(owner);
        }
        runChildren(owner);
        children_.remove_if([](const std::unique_ptr<Behavior>& c) { return c->finished_; });
        const Status s = onUpdate(owner);
        ++updates_;
        return s;
    }

    // Ends this behaviour and its subtree (0xbd00).
    void stop(Owner& owner) {
        if (started_ && !finished_) {
            for (auto& c : children_) {
                c->stop(owner);
            }
            onStop(owner);
        }
        finished_ = true;
    }

protected:
    virtual void onStart(Owner&) {}
    virtual Status onUpdate(Owner&) { return Status::Running; }
    virtual void onStop(Owner&) {}

private:
    // 0xb970.
    void runChildren(Owner& owner) {
        std::uint32_t owned = 0;     // channels of Running children this update
        std::uint32_t lent = 0;      // channels of Waiting children this update
        std::array<Behavior*, 32> lender{};
        for (auto it = children_.begin(); it != children_.end(); ++it) {
            Behavior& c = **it;
            const std::uint32_t m = c.channels_;
            if ((owned & m) != 0) {
                // Not run this update; a lender sharing a channel keeps it.
                if ((m & lent) != 0) {
                    for (int b = 0; b < 32; ++b) {
                        if (((m >> b) & 1) && lender[std::size_t(b)]) {
                            lender[std::size_t(b)]->active_ |= 1u << b;
                        }
                    }
                }
                owned |= m;
                continue;
            }
            if ((m & lent) != 0) {
                // This child takes lent channels: their lenders lose them, and stop with the last.
                for (int b = 0; b < 32; ++b) {
                    Behavior* l = lender[std::size_t(b)];
                    if (((m >> b) & 1) && l) {
                        l->active_ &= ~(1u << b);
                        if (l->active_ == 0) {
                            l->stop(owner);
                        }
                        lent &= ~(1u << b);
                        lender[std::size_t(b)] = nullptr;
                    }
                }
            }
            switch (c.run(owner)) {
            case Status::Done:
                c.stop(owner);
                c.active_ = 0;
                break;
            case Status::Waiting:
                c.active_ = 0;
                lent |= m;
                for (int b = 0; b < 32; ++b) {
                    if ((m >> b) & 1) {
                        lender[std::size_t(b)] = &c;
                    }
                }
                break;
            case Status::Running:
                owned |= m;
                c.active_ = m;
                break;
            case Status::Idle:
                break;
            }
        }
    }

    std::list<std::unique_ptr<Behavior>> children_;
    Behavior* parent_ = nullptr;
    Behavior* root_ = nullptr;
    Behavior* creator_ = nullptr;   // the behaviour that queued this one (+0x1c)
    std::uint32_t channels_ = 0;    // +0x8
    std::uint32_t active_ = 0;      // +0xc
    std::uint64_t events_ = 0;      // +0x10
    std::uint32_t updates_ = 0;     // +0x78
    bool started_ = false;          // +0x20
    bool finished_ = false;         // +0x21
};

}   // namespace beacon::game::ai
