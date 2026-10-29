#pragma once
#include "marble_madness_amiga/data/assets.hpp"
#include "marble_madness_amiga/data/game_files.hpp"
#include "marble_madness_amiga/game/race.hpp"
#include "marble_madness_amiga/game/view.hpp"
#include "marble_madness_amiga/sound/sound_board.hpp"
#include "platform/image.hpp"
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

// The game around the races, as the program's main loop runs it by its
// mode ($1005EA, $10B48A): the options screen (mode 5, $10CCDC), each
// course's introduction ($106A64), the race (mode 0), a finish (mode 3:
// $10B2AA with the bonus box $10BC28), running out of time (mode 4: $10B368
// with $10BE7C) and the end after Ultimate (mode 6: $10C728). The program
// waits in vertical blanks ($108F90); a world update takes two.
//
// One player: the options screen keeps "Number of Players" at 1 (two
// players racing at once are not rebuilt).
namespace marble_madness::game {

// A line of the program's text lists ($10A46A): a column and a row of the
// playfield's 8 x 8 character grid (the column counts from three cells left
// of the view), the text, and the next entry if any.
struct TextLine {
    int column{}, row{};
    std::string text;

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) { ar(column, row, text); }
};
std::vector<TextLine> read_text_list(const amiga::LoadedImage& program, std::uint32_t list);
std::string read_string(const amiga::LoadedImage& program, std::uint32_t address);
// $10A59A: a number in `digits` places; style 0 pads with spaces, 1 with
// zeros, 2 moves the digits to the left.
std::string number_text(std::int32_t value, int digits, int style);

// The options screen's settings ($10D07E-$10D085).
struct Settings {
    int players{1};
    int difficulty{};         // 0-7: the course times (table $102350)
    std::array<int, 2> device{}; // per player: Joy Stick, Trackball 1, Trackball 2, Mouse
    int port{};               // the red player's: Rear Port, Front Port

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) { ar(players, difficulty, device, port); }
};

// The player's controls as the front end reads them: the joystick for the
// options screen's pointer, and the push the race takes.
struct Controls {
    bool up{}, down{}, left{}, right{}, fire{};
    Push push{};

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) { ar(up, down, left, right, fire, push); }
};

// An owned object that copies with its owner (for quick saves).
template <class T>
class Owned {
public:
    Owned() = default;
    Owned(std::unique_ptr<T> p) : p_(std::move(p)) {}
    Owned(const Owned& o) : p_(o.p_ ? std::make_unique<T>(*o.p_) : nullptr) {}
    Owned& operator=(const Owned& o) {
        if (this != &o) p_ = o.p_ ? std::make_unique<T>(*o.p_) : nullptr;
        return *this;
    }
    Owned(Owned&&) noexcept = default;
    Owned& operator=(Owned&&) noexcept = default;
    T* operator->() const { return p_.get(); }
    T& operator*() const { return *p_; }
    explicit operator bool() const { return p_ != nullptr; }
    T* get() const { return p_.get(); }
    void reset() { p_.reset(); }

private:
    std::unique_ptr<T> p_;
};

// Copies of a GameFlow play on independently from the same point (the
// module's quick save); they share the read-only game files.
class GameFlow {
public:
    GameFlow(const data::GameFiles&, const data::Assets&);

    // One world update's time (two vertical blanks).
    void tick(const Controls&);
    platform::Image picture() const;
    void audio(std::span<float> stereo, int sample_rate);

    const Settings& settings() const { return settings_; }
    // Research and tools: whether a race is running, and the course.
    bool racing() const { return stage_ == Stage::race; }
    // Research: end the running race as a finish (with the time left).
    void finish_now() {
        if (racing()) race_->game_mode = 3;
    }
    data::Course course() const { return course_; }
    // The steering the options screen's input device implies changed (the
    // module's Steering option follows it, and the other way round).
    void set_device(int device) { settings_.device[0] = device; }
    // The crowd race (not the original game, crowd.hpp): on, the seven
    // computer marbles join the race being run and every course after.
    void set_crowd(bool on) {
        crowd_ = on;
        if (race_) {
            race_->set_crowd(on);
        }
    }
    // The crowd's skill (Race::easy, normal, hard).
    void set_crowd_skill(int skill) {
        crowd_skill_ = skill;
        if (race_) {
            race_->crowd_skill = skill;
        }
    }
    // The crowd holds back while ahead of the player (rubber banding).
    void set_crowd_close(bool on) {
        crowd_close_ = on;
        if (race_) {
            race_->crowd_close = on;
        }
    }
    Race::Cheats cheats;
    // Research: the race being run.
    const Race* race() const { return race_.get(); }

private:
    enum class Stage { options, loading, intro, race, finish, out_of_time, ending };

    void start_options();
    void options_tick(const Controls&);
    void click_options();
    void start_game();
    void load_course(data::Course);
    void start_course();
    void intro_tick();
    void race_tick(const Controls&);
    void finish_tick();
    void crowd_results(int x, int y);
    void out_of_time_tick();
    void ending_tick();
    void wait(int blanks) { wait_ = blanks; }
    void play(int sound);
    void playfield_texts(std::uint32_t list, std::vector<View::Text>& out) const;
    void playfield_text(int column, int row, const std::string& text, std::vector<View::Text>& out) const;

    const data::GameFiles* files_;
    const data::Assets* assets_;
    Owned<Race> race_;
    Owned<View> view_;
    Owned<sound::SoundBoard> sounds_;
    // The crowd race (not the original game): each crowd marble's sounds on
    // a board of its own (four more channels each, past the Amiga's four),
    // mixed in by how near the screen it is.
    std::array<Owned<sound::SoundBoard>, 8> crowd_sounds_;
    std::array<float, 8> crowd_gain_{};
    std::vector<float> crowd_mix_;
    void play_race_sounds(bool after_blank);
    Stage stage_{Stage::options};
    int step_{};
    int wait_{};              // vertical blanks to wait before the next step
    Settings settings_;
    data::Course course_{};
    // What the players carry from race to race (their records at $10024E).
    std::int32_t score_{};
    std::int16_t time_{};
    std::int16_t respawns_{};
    // The options screen's pointer ($10D086, $10D088) and the last fire.
    int pointer_x_{0xA0}, pointer_y_{0xBE};
    bool fire_held_{true};
    int title_blanks_{};
    View::Overlay overlay_;
    // The introduction's count ($106A64) and the end's tally ($10C96A).
    int count_{};
    int total_{};
    // The end's bouncing marbles ($100CE2, 10 bytes each, $11E9A8).
    struct Ball {
        std::int16_t x{}, y{}, vx{}, vy{}, ramp{};

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) { ar(x, y, vx, vy, ramp); }
    };
    std::vector<Ball> balls_;
    Random random_;
    bool crowd_{};
    bool crowd_close_{true};
    int crowd_skill_{Race::normal};

  public:
    template <class A> void persist(A &a) {
        // During a loading transition the next course is selected before
        // the old race, renderer and audio bank have been replaced.
        auto loaded_course = race_->course();
        a(course_, loaded_course);
        if constexpr (A::reading) {
            if (int(course_) < 0 || int(course_) > 5 || int(loaded_course) < 0 || int(loaded_course) > 5)
                A::fail();
            const auto next_course = course_;
            load_course(loaded_course);
            course_ = next_course;
        }
        a(*race_, *view_, *sounds_);
        for (auto &board : crowd_sounds_) {
            bool exists = bool(board);
            a(exists);
            if constexpr (A::reading) {
                board.reset();
                if (exists)
                    board = std::make_unique<sound::SoundBoard>(*files_, loaded_course);
            }
            if (exists)
                a(*board);
        }
        a(cheats, crowd_gain_, crowd_mix_, stage_, step_, wait_, settings_, score_, time_, respawns_,
          pointer_x_, pointer_y_, fire_held_, title_blanks_, overlay_, count_, total_, balls_, random_,
          crowd_, crowd_close_, crowd_skill_);
    }
};

} // namespace marble_madness::game
