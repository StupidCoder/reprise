#include "beach_volley_amiga/game/module.hpp"
#include "amiga/paula_channels.hpp"
#include "beach_volley_amiga/data/assets.hpp"
#include "beach_volley_amiga/front/intro.hpp"
#include "beach_volley_amiga/front/venue_card.hpp"
#include "beach_volley_amiga/match/match.hpp"
#include "beach_volley_amiga/sound/soundtracker.hpp"
#include "state/archive.hpp"
#include "ui/vhs.hpp"
#include <algorithm>
#include <memory>
#include <stdexcept>

namespace beach_volley::game {
namespace {

constexpr std::uint32_t save_magic = 0x42564C59, save_version = 4; // "BVLY"

// Blanks the original spends loading between the parts of a tour (its disk
// loads, timed by wall clock on the original): animation.code with the
// winner's picture still up, then the card's own files with the screen
// black (per venue), and after a card (or a loss) the venue's files and a
// pause ($3194C) with the display off.
constexpr int card_load_kept = 75;
constexpr int card_load_black[8] = {369, 419, 401, 392, 360, 421, 418, 373};
constexpr int match_load = 395;
// The chance table's place moves on through the blanks volley.code's
// vertical blank runs between two matches.
constexpr int chance_carry = 5;
constexpr int first_chance_place = 5; // as the intro and the first loads leave it

class Module final : public app::GameModule {
public:
    explicit Module(const std::filesystem::path& content)
        : content_(content), files_(data::load_intro_files(content)), art_(front::decode_intro(files_)),
          song_(sound::Song::parse(files_.song)),
          player_(sound::read_player_tables(files_.code, front::intro_sample_table, front::intro_period_table,
                                            files_.instruments.size()),
                  front::intro_note_channels, front::intro_effect_channels),
          intro_(art_, std::uint32_t(files_.instruments.size())) {
        // One sample memory for Paula: the bank, then the intro's block
        // with the voices.
        memory_.reserve(files_.instruments.size() + files_.block.size());
        for (const auto b : files_.instruments) memory_.push_back(std::int8_t(b));
        for (const auto b : files_.block) memory_.push_back(std::int8_t(b));
        paula_.set_memory(memory_);
        song_.halve_volumes(); // $230E4
        player_.start(song_, paula_); // $24114, before the titles' first step
    }

    double step_seconds() const override { return 1.0 / 50; } // a PAL vertical blank
    double pixel_aspect() const override { return 1.0; }

    void tick(const app::GameInput& input) override {
        switch (stage_) {
        case Stage::intro:
            player_.tick(paula_); // the vertical blank's interrupt ($23D94)
            intro_.tick(players(input), paula_);
            if (intro_.phase() == front::Intro::Phase::chosen && player_.playing()) {
                player_.stop(paula_);
                // volley.code: a one-player tour starts at the first venue's
                // card (side B under a second player is not rebuilt yet).
                if (intro_.players() == 1) {
                    venue_ = 1;
                    wait(card_load_kept, card_load_black[venue_ - 1], Stage::card);
                }
            }
            return;
        case Stage::loading:
            if (--wait_ == wait_black_) black_ = true;
            if (wait_ > 0) return;
            black_ = false;
            if (next_ == Stage::card) start_card();
            else start_match();
            return;
        case Stage::card:
            card_->tick(input.fire || input.button2);
            if (card_->finished()) wait(0, match_load, Stage::match);
            return;
        case Stage::match:
            match_->set_cheat(cheat_);
            match_->tick(match_input(input));
            if (match_->finished()) next_match();
            return;
        case Stage::ending: return;
        }
    }

    platform::Image picture() const override {
        if (black_) return platform::Image(front::VenueCard::width, front::VenueCard::height);
        if (stage_ == Stage::card || (card_ && next_ == Stage::match && stage_ == Stage::loading)) return card_->picture();
        if (match_) return match_->picture();
        return intro_.picture();
    }

    void audio(std::span<float> stereo, int rate) override {
        std::fill(stereo.begin(), stereo.end(), 0.0f);
        paula_.render(stereo, rate);
    }

    std::string stopped() const override {
        if (stage_ == Stage::ending)
            return "Side A has won at every venue. The ending (final.code) is not rebuilt yet.";
        if (intro_.phase() != front::Intro::Phase::chosen || intro_.players() == 1) return {};
        return "The match for two players is not rebuilt yet: Beach Volley plays its one-player match so far.";
    }

    std::vector<app::GameOption> options() const override {
        return {app::screen_filter_option(vhs_),
                {"Win the match with F7",
                 {"Off", "On"},
                 cheat_ ? 1 : 0,
                 "The original's own cheat (typed in as a code, then the HELP key during a rally): side A "
                 "wins the match at once. F7 stands in for HELP.",
                 "Cheats"}};
    }
    void set_option(std::size_t index, int value) override {
        if (index == 0) vhs_ = std::clamp(value, 0, ui::vhs_levels - 1);
        if (index == 1) cheat_ = value != 0;
    }
    int vhs_level() const override { return vhs_; }

    // A quick save holds the intro as it stands (both screens, the copper's
    // palettes, its counters), the music player and Paula's channels.
    std::vector<std::uint8_t> write_state() override {
        state::Writer ar;
        std::uint32_t magic = save_magic, version = save_version;
        ar(magic, version);
        persist(ar);
        return std::move(ar.bytes);
    }
    void read_state(std::span<const std::uint8_t> in) override {
        state::Reader ar(in);
        std::uint32_t magic{}, version{};
        ar(magic, version);
        if (magic != save_magic || version != save_version)
            throw std::runtime_error("Beach Volley: not a save of this version.");
        persist(ar);
        ar.finish();
    }

private:
    enum class Stage : std::uint8_t { intro, loading, card, match, ending };

    template <class A> void persist(A& ar) {
        bool playing = player_.playing();
        ar(playing, player_, paula_, intro_);
        if constexpr (A::reading) {
            player_.bind(playing ? &song_ : nullptr);
            paula_.set_memory(memory_);
        }
        bool in_card = card_ != nullptr, in_match = match_ != nullptr;
        ar(stage_, next_, venue_, wait_, wait_black_, black_, cheat_, in_card, in_match);
        // The match sound outlives the matches (its volume and player go on);
        // it is bound to the venue last loaded.
        int sound_venue = match_data_ ? match_data_->venue : 0;
        ar(sound_venue);
        if constexpr (A::reading) {
            card_.reset();
            match_.reset();
            if (sound_venue) load_venue(sound_venue);
            if (in_match) match_ = std::make_unique<match::Match>(*match_data_, 1, match_sound_);
            if (in_card) { // last: it binds Paula to its own samples
                card_files_ = std::make_unique<front::CardFiles>(front::load_card_files(content_, venue_));
                card_ = std::make_unique<front::VenueCard>(*card_files_, paula_);
            }
            if (!card_ && !sound_venue) paula_.set_memory(memory_);
        }
        if (sound_venue) ar(match_sound_);
        if (match_) ar(*match_);
        if (card_) ar(*card_);
    }

    // A pause of `kept` blanks with the last picture, then `dark` of black.
    void wait(int kept, int dark, Stage next) {
        stage_ = Stage::loading;
        next_ = next;
        wait_ = kept + dark;
        wait_black_ = dark;
        black_ = kept == 0;
    }

    // A venue's files ($3133C): the court, the song, the chance table.
    void load_venue(int venue) {
        if (!match_data_ || match_data_->venue != venue)
            match_data_ = std::make_unique<match::MatchData>(match::load_match_data(content_, venue));
        match_sound_.bind(*match_data_);
    }

    // animation.code with the venue in $80.
    void start_card() {
        card_files_ = std::make_unique<front::CardFiles>(front::load_card_files(content_, venue_));
        card_ = std::make_unique<front::VenueCard>(*card_files_, paula_);
        card_->begin();
        stage_ = Stage::card;
    }

    void start_match() {
        card_.reset();
        card_files_.reset();
        load_venue(venue_);
        if (match_) {
            match_->restart(*match_data_); // volley.code stays loaded
            match_->set_chance_place(match_->chance_place() + chance_carry);
        } else {
            match_ = std::make_unique<match::Match>(*match_data_, 1, match_sound_);
            match_->set_chance_place(first_chance_place);
        }
        stage_ = Stage::match;
    }

    // $25E78: a win takes side A to the next venue's card (after the
    // eighth, the ending), a loss back to the first venue without one.
    void next_match() {
        if (!match_->a_won()) {
            venue_ = 1;
            wait(0, match_load, Stage::match);
            return;
        }
        if (++venue_ == 9) {
            stage_ = Stage::ending;
            black_ = true;
            return;
        }
        wait(card_load_kept, card_load_black[venue_ - 1], Stage::card);
    }

    // The match's joystick: port 1 (side A) from the arrows or the
    // gamepad and its button.
    static match::MatchInput match_input(const app::GameInput& input) {
        match::MatchInput m;
        m.port1 = {input.up, input.down, input.left, input.right, input.fire};
        m.port0 = {input.up2, input.down2, input.left2, input.right2, input.fire2};
        m.help = input.f7;
        return m;
    }

    // The menu's choice: F1 (or 1, or the gamepad's first button) one
    // player, F2 (2, or the second button) two; on a touch screen or with
    // the mouse, the choice pressed once the menu shows. (The host has no
    // F2 key yet: plan.md.)
    int players(const app::GameInput& input) const {
        if (input.f1 || input.fire) return 1;
        if (input.c64_keys[59] || input.button2) return 2;
        if (intro_.phase() == front::Intro::Phase::menu && input.pointer && input.mouse_button) {
            for (int n = 0; n < 2; ++n) {
                const auto& a = front::Intro::choice_areas[std::size_t(n)];
                if (input.pointer_x >= a[0] && input.pointer_x < a[0] + a[2] && input.pointer_y >= a[1] - 4 &&
                    input.pointer_y < a[1] + a[3] + 4)
                    return n + 1;
            }
        }
        return 0;
    }

    std::filesystem::path content_;
    data::IntroFiles files_;
    front::IntroArt art_;
    sound::Song song_;
    sound::Player player_;
    std::vector<std::int8_t> memory_;
    amiga::PaulaChannels paula_;
    front::Intro intro_;
    std::unique_ptr<match::MatchData> match_data_;
    match::MatchSound match_sound_{paula_};
    std::unique_ptr<match::Match> match_;
    std::unique_ptr<front::CardFiles> card_files_;
    std::unique_ptr<front::VenueCard> card_;
    Stage stage_{Stage::intro}, next_{Stage::intro};
    int venue_{};      // $25B70
    int wait_{}, wait_black_{};
    bool black_{};
    bool cheat_{};
    int vhs_{};
};

} // namespace

std::unique_ptr<app::GameModule> start(const std::filesystem::path& content) {
    return std::make_unique<Module>(content);
}

} // namespace beach_volley::game
