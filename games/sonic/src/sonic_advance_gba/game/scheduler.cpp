#include "sonic_advance_gba/game/scheduler.hpp"
#include <algorithm>

namespace sonic_advance_gba {

void Scheduler::insert(std::unique_ptr<Process> p) {
    // 08001588: before the first process of a higher priority.
    auto it = std::find_if(list_.begin(), list_.end(), [&](const std::unique_ptr<Process>& q) {
        return p->priority() < q->priority();
    });
    list_.insert(it, std::move(p));
}

void Scheduler::take_slot(Process& p) {
    // 08001588: the record on top of the free ones (none left: a dummy
    // record that is never run; not met).
    if (free_slots_.empty()) {
        p.slot_ = 0xFF;
        return;
    }
    p.slot_ = free_slots_.back();
    free_slots_.pop_back();
    p.creator_slot_ = current_ ? current_->slot_ : 0xFF;
    slots_[p.slot_].creator = current_;
}

Scheduler::List::iterator Scheduler::find(const Process& p) {
    return std::find_if(list_.begin(), list_.end(),
                        [&](const std::unique_ptr<Process>& q) { return q.get() == &p; });
}

const Process* Scheduler::before(const Process& p) const {
    const Process* previous = nullptr;
    for (const auto& q : list_) {
        if (q.get() == &p) {
            return previous;
        }
        previous = q.get();
    }
    return nullptr;
}

void Scheduler::destroy(Process& p) {
    // 08001620
    if (!p.alive_) {
        return;
    }
    const auto it = find(p);
    if (it == list_.end()) {
        return;
    }
    p.alive_ = false;
    p.on_destroy();
    if (p.slot_ != 0xFF) {
        slots_[p.slot_].creator = nullptr;
        free_slots_.push_back(p.slot_);
    }
    if (running_ && it == next_) {
        ++next_;
    }
    finished_.push_back(std::move(*it));
    list_.erase(it);
    if (!running_) {
        finished_.clear();
    }
}

void Scheduler::destroy_range(u16 lo, u16 hi) {
    // 08001954 walks the list from the front and stops at the first process
    // of priority hi or more.
    std::vector<Process*> doomed;
    for (const auto& p : list_) {
        if (p->priority() >= hi) {
            break;
        }
        if (p->priority() >= lo) {
            doomed.push_back(p.get());
        }
    }
    for (Process* p : doomed) {
        destroy(*p);
    }
}

void Scheduler::run(Game& game, bool pause_mode) {
    running_ = true;

    // An update that throws (an unrecovered part) leaves the list usable.
    struct Done {
        Scheduler& s;

        ~Done() {
            s.running_ = false;
            s.current_ = nullptr;
            s.finished_.clear();
        }
    } done{*this};

    auto it = list_.begin();
    while (it != list_.end()) {
        next_ = std::next(it);
        Process& p = **it;
        const bool runs = pause_mode ? (!p.paused && p.runs_in_pause) : !p.paused;
        if (runs) {
            current_ = &p;
            p.update(game);
        }
        it = next_;
    }
}

} // namespace sonic_advance_gba
