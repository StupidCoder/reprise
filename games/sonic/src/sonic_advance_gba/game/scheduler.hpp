#pragma once
#include "sonic_advance_gba/game/types.hpp"
#include <array>
#include <list>
#include <memory>
#include <vector>

// The game's running parts (screens, objects, effects) and the order they run
// in each frame. The original keeps them as "tasks" (08001588 creates one,
// 08001620 destroys one, 080016AC runs them): a list sorted by priority, a
// new task going after every task of the same priority. Each frame the list
// runs from the front; the runner takes the next task before running the
// current one, so a task created right after the running one waits for the
// next frame, while one created further on runs in this one. Destroying the
// task that would run next moves the runner on to the one after it.
//
// The original's tasks also own a block of an internal memory heap, which it
// compacts from time to time; here each process is a C++ object. Their task
// records (headers) come from a pool of 128 handed out last-freed first;
// a record keeps its creator (the task running then) until it is freed
// (then none) or handed out again. A few routines reach another task
// through these records after it has ended (see Scheduler::creator_in_slot).
namespace sonic_advance_gba {

class Game;

class Process {
public:
    explicit Process(u16 priority) : priority_(priority) {}

    virtual ~Process() = default;
    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;

    // One frame of this process.
    virtual void update(Game& game) = 0;

    // Runs when the process is destroyed (the original's task destructor,
    // called at once by 08001620; the object itself goes after the update).
    virtual void on_destroy() {}

    u16 priority() const { return priority_; }

    // Flag bit 0 of the original's task: the runner skips it.
    bool paused = false;
    // Flag bit 2: runs while the game is paused (with bit 0 clear).
    bool runs_in_pause = false;

    bool alive() const { return alive_; }

    // The pool record this process holds and the one its creator held.
    u8 slot() const { return slot_; }

    u8 creator_slot() const { return creator_slot_; }

private:
    friend class Scheduler;
    u16 priority_;
    bool alive_ = true;
    u8 slot_{};
    u8 creator_slot_ = 0xFF; // none
};

class Scheduler {
public:
    // Adds a process in priority order and returns it.
    template <class P, class... Args>
    P& create(Args&&... args) {
        auto p = std::make_unique<P>(std::forward<Args>(args)...);
        P& ref = *p;
        take_slot(ref);
        insert(std::move(p));
        return ref;
    }

    // The creator recorded in pool record `slot` now: the creator of the
    // process holding it (another one than before, if the record was
    // handed out again), or none when it is free.
    const Process* creator_in_slot(u8 slot) const {
        return slot < slots_.size() ? slots_[slot].creator : nullptr;
    }

    // The process before `p` in the run list (the task record's +00 link),
    // or null for the first.
    const Process* before(const Process& p) const;

    // Removes a process (its destructor runs once the current update is over).
    void destroy(Process& p);
    // 08001954: removes every process with lo <= priority < hi.
    void destroy_range(u16 lo, u16 hi);
    // 080016AC: runs every process once. In the pause mode only the
    // processes marked runs_in_pause run.
    void run(Game& game, bool pause_mode);

    std::size_t size() const { return list_.size(); }

    template <class F>
    void for_each(F&& f) const {
        for (const auto& p : list_) {
            f(*p);
        }
    }

private:
    using List = std::list<std::unique_ptr<Process>>;
    void insert(std::unique_ptr<Process> p);
    void take_slot(Process& p);

    struct Slot {
        const Process* creator{};
    };

    std::array<Slot, 128> slots_{};
    std::vector<u8> free_slots_ = initial_free_slots(); // the top is the back
    Process* current_{};                                // the process running now

    static std::vector<u8> initial_free_slots() {
        std::vector<u8> v;
        for (int i = 127; i >= 0; --i) {
            v.push_back(u8(i));
        }
        return v;
    }

    List::iterator find(const Process& p);

    List list_;
    List::iterator next_{}; // the runner's next process (valid while running)
    bool running_ = false;
    std::vector<std::unique_ptr<Process>> finished_; // destroyed during an update
};

} // namespace sonic_advance_gba
