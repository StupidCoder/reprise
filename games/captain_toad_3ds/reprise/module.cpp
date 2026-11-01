// Flat-screen campaign adapter. Imported content is read-only; generated packages
// live in the Library cache. Reprise owns presentation, input and durable saves.
#include "module.hpp"
#include "checkpoint.hpp"
#include "platform/gpu_commands.hpp"
#include "../src/gpu/SdlDevice.hpp"
#include "../src/gpu/VulkanShaders.hpp"
#include "../src/session/Session.hpp"
#include "../src/session/FrontEnd.hpp"
#include "../src/session/CourseUi.hpp"
#include "../src/importer/Stages.hpp"
#include <deque>
#include <cmath>
#include <cstring>
namespace captain_toad::reprise {
namespace {
class Opening final : public ::app::GameModule {
    std::filesystem::path content_, cache_;
    session::Progress progress_;
    std::unique_ptr<session::FrontEnd> book_;
    std::unique_ptr<session::CourseUi> courseUi_;
    std::optional<CourseEntry> course_;
    int courseNumber_{}, courseChoice_{};
    enum class Next { None, Book, Course } next_ = Next::None;
    bool afterOpening_{}, quit_{};
    std::string stopped_;
    Checkpoint checkpoint_;
    bool restored_{};
    uint32_t uiButtons_{};
    bool uiHalf_{};
    std::deque<::platform::PointerEvent> uiPointer_;
    void resetUiInput() {
        uiButtons_ = 0;
        uiHalf_ = false;
        uiPointer_.clear();
    }
    static bool supported(const std::string& stage) { return Checkpoint::supported(stage); }
    bool dualScreen() const { return book_ || (courseUi_ && courseUi_->needsBottom()); }
    void resetAudio() {
        audioReady_ = false;
        audioPhase_ = 0;
        samples_ = {};
    }
    void enterBook(bool opening = false) {
        next_ = Next::Book;
        afterOpening_ = opening;
    }
    void enterCourse(const CourseEntry& course, int number) {
        if (!supported(course.stage)) {
            stopped_ = "This course is not reconstructed yet. The first four courses are supported.";
            return;
        }
        course_ = course;
        courseNumber_ = number;
        next_ = Next::Course;
    }
    // Create and release scene resources only with the module's device selected. Transitions
    // are scheduled by tick; a render materializes them without advancing simulation.
    void transition() {
        if (next_ == Next::Book) {
            checkpoint_.scene = "Book";
            checkpoint_.progress = progress_;
            courseUi_.reset();
            scene_.reset();
            course_.reset();
            book_.reset();
            book_ = std::make_unique<session::FrontEnd>(content_, cache_, progress_, afterOpening_);
        } else if (next_ == Next::Course) {
            const auto package = buildStage(content_, cache_, course_->stage);
            auto ui = std::make_unique<session::CourseUi>(content_, cache_, *course_, courseNumber_);
            if (book_) {
                std::unique_ptr<captain_toad::ui::Instance> card;
                auto package = book_->takeTitleCard(card);
                if (card) {
                    ui->takeTitleCard(std::move(package), std::move(card));
                }
            }
            scene_.reset();
            courseUi_.reset();
            book_.reset();
            scene_ = std::make_unique<session::Session>(package);
            courseUi_ = std::move(ui);
            scene_->onTick([this](const session::Session::StartTick& t) { courseUi_->startTick(t); });
            progress_.played.insert(course_->stage);
            checkpoint_.scene = course_->stage;
            checkpoint_.progress = progress_;
        }
        restored_ = false;
        resetUiInput();
        next_ = Next::None;
        resetAudio();
    }
    void sourceAudio(int16_t* out, int frames) {
        if (courseUi_ && courseUi_->pauseOpen()) {
            std::fill_n(out, frames * 2, 0);
        } else if (book_) {
            book_->fillAudio(out, frames);
        } else if (scene_) {
            scene_->fillAudio(out, frames);
        } else {
            std::fill_n(out, frames * 2, 0);
        }
    }
    ::platform::GpuSurface* surface_{};
    std::unique_ptr<gpu::SdlDevice> device_;
    std::unique_ptr<session::Session> scene_;
    int width_ = 800, height_ = 480, filter_{}, controlScheme_{};
    bool touch_{}, menuRequest_{}, startHeld_{};
    bool interpolate_ = false, wide_ = false;
    double fraction_{};
    std::array<int16_t, 4> samples_{};
    double audioPhase_{};
    bool audioReady_{};

public:
    explicit Opening(std::filesystem::path content) : content_(std::move(content)) {
        cache_ = content_.parent_path().parent_path() / "cache/captain_toad_3ds/eu-00040000001cb200-v1";
        if (!std::filesystem::is_regular_file(content_ / "Opening/scene.json")) {
            throw std::runtime_error("The imported opening scene is missing. Reimport the cartridge image.");
        }
    }
    ~Opening() override {
        gpu::setDevice(device_.get());
        courseUi_.reset();
        book_.reset();
        scene_.reset();
        gpu::setDevice(nullptr);
        device_.reset();
    }
    double step_seconds() const override { return 1.0 / 60; }
    double pixel_aspect() const override { return 1; }
    void attach_gpu(::platform::GpuSurface* surface) override { surface_ = surface; }
    std::optional<std::array<int, 2>> gpu_frame_size() const override { return std::array{width_, height_}; }
    ::platform::Image picture() const override {
        if (!surface_) {
            throw std::runtime_error("No host surface for scene capture");
        }
        return surface_->capture(width_, height_,
                                 [this](const ::platform::GpuFrame& f) { const_cast<Opening*>(this)->draw_gpu(f); });
    }
    void draw_gpu(const ::platform::GpuFrame& f) override {
        ::platform::GpuCommands borrowed(f);
        borrowed.submit();
        if (!device_) {
            gpu::vulkan::setCacheDirectory(cache_ / "materials-v1");
            device_ = std::make_unique<gpu::SdlDevice>(borrowed.device);
            gpu::setDevice(device_.get());
        }
        gpu::setDevice(device_.get());
        device_->begin(borrowed.color, borrowed.format, f.width, f.height);
        if (restored_ && checkpoint_.scene != "Opening" && checkpoint_.scene != "Book") {
            int number = 0;
            bool found = false;
            for (const auto& course : courseList(content_)) {
                if (course.season == 1 && course.type == "通常") {
                    ++number;
                }
                if (course.season == 1 && course.stage == checkpoint_.scene) {
                    enterCourse(course, number);
                    found = true;
                    break;
                }
            }
            if (!found) {
                throw std::runtime_error("Checkpoint course is missing from the imported course list.");
            }
        }
        if (next_ != Next::None) {
            transition();
        }
        if (!scene_ && !book_) {
            scene_ = std::make_unique<session::Session>(content_ / "Opening");
        }
        if (book_) {
            book_->render(0, 0, f.width, f.height);
        } else {
            scene_->set(session::Toggle::Interpolate, interpolate_);
            scene_->set(session::Toggle::WideView, wide_ && !dualScreen());
            scene_->setPresentationFraction(fraction_);
            if (dualScreen()) {
                auto r = session::ScreenRects::fit(0, 0, f.width, f.height, true);
                gpu::Clear clear;
                clear.color = true;
                device_->clear(clear);
                scene_->render(r.top[0], r.top[1], r.top[2], r.top[3]);
                courseUi_->setSmooth(interpolate_, scene_->presentAlpha());
                device_->setViewport({r.top[0], r.top[1], r.top[2], r.top[3]});
                courseUi_->drawTop();
                device_->setViewport({r.bottom[0], r.bottom[1], r.bottom[2], r.bottom[3]});
                courseUi_->drawBottom();
            } else {
                scene_->render(0, 0, f.width, f.height);
                if (courseUi_) {
                    auto r = scene_->gameRect(0, 0, f.width, f.height);
                    device_->setViewport({r[0], r[1], r[2], r[3]});
                    courseUi_->setSmooth(interpolate_, scene_->presentAlpha());
                    courseUi_->drawTop();
                }
            }
        }
        device_->end();
    }
    void tick(const ::app::GameInput& in) override {
        if ((!scene_ && !book_) || next_ != Next::None || !stopped_.empty()) {
            return;
        }
        gpu::setDevice(device_.get());
        if (in.start && !startHeld_) {
            if (courseUi_ && !courseUi_->pauseOpen() && !courseUi_->clearRunning() && !courseUi_->missRunning()) {
                courseUi_->openPause();
            } else if (!courseUi_) {
                menuRequest_ = true;
            }
        }
        startHeld_ = in.start;
        session::Controls c;
        c.stick[0] = float(in.right) - float(in.left);
        c.stick[1] = float(in.up) - float(in.down);
        if (in.analog_stick) {
            auto v = *in.analog_stick;
            if (std::hypot(v[0], v[1]) > .15f) {
                c.stick[0] = v[0];
                c.stick[1] = -v[1];
            }
        }
        if (in.camera_stick) {
            c.cstick[0] = (*in.camera_stick)[0];
            c.cstick[1] = -(*in.camera_stick)[1];
        }
        c.buttons = (in.fire ? 1u : 0u) | (in.button2 ? 2u : 0u) | (in.face_x ? 8u : 0u) | (in.face_y ? 16u : 0u) |
                    (in.shoulder_left ? 0x2000u : 0u) | (in.shoulder_right ? 0x4000u : 0u) |
                    (in.trigger_left ? 4u : 0u) | (in.trigger_right ? 32u : 0u) |
                    (dualScreen() && in.up ? 0x10000u : 0u) | (dualScreen() && in.down ? 0x20000u : 0u) |
                    (dualScreen() && in.left ? 0x40000u : 0u) | (dualScreen() && in.right ? 0x80000u : 0u);
        for (auto& v : in.touch_controls) {
            if (v.id == "move" && v.held) {
                c.stick[0] = v.x;
                c.stick[1] = -v.y;
            }
            if (v.id == "camera" && v.held) {
                c.cstick[0] = v.x;
                c.cstick[1] = -v.y;
            }
            if (v.held || v.pressed) {
                if (v.id == "action") {
                    c.buttons |= 1;
                }
                if (v.id == "run") {
                    c.buttons |= 2;
                }
                if (v.id == "left") {
                    c.buttons |= 0x2000;
                }
                if (v.id == "right") {
                    c.buttons |= 0x4000;
                }
            }
        }
        c.touch = in.pointer && in.mouse_button;
        c.touchView[0] = float(in.pointer_x) / width_;
        c.touchView[1] = float(in.pointer_y) / height_;
        c.touchPos[0] = c.touchView[0] * 320;
        c.touchPos[1] = c.touchView[1] * 240;
        c.viewAspect = float(width_) / height_;
        if (dualScreen()) {
            uiButtons_ |= c.buttons;
            for (const auto& e : in.pointer_events) {
                uiPointer_.push_back(e);
            }
            uiHalf_ = !uiHalf_;
            if (uiHalf_) {
                return;
            }
            c.buttons = uiButtons_;
            uiButtons_ = 0;
            auto r = session::ScreenRects::fit(0, 0, width_, height_, true);
            auto touch = [&](bool down, float x, float y) {
                float tx = (x - r.bottom[0]) * 320 / r.bottom[2];
                float ty = (y - (height_ - r.bottom[1] - r.bottom[3])) * 240 / r.bottom[3];
                down = down && tx >= 0 && tx < 320 && ty >= 0 && ty < 240;
                if (book_) {
                    book_->touch(down, tx, ty);
                } else {
                    courseUi_->touch(down, tx, ty);
                }
            };
            if (!uiPointer_.empty()) {
                auto e = uiPointer_.front();
                uiPointer_.pop_front();
                touch(e.kind != ::platform::PointerEvent::Kind::up, e.x, e.y);
            } else {
                touch(in.pointer && in.mouse_button, in.pointer_x, in.pointer_y);
            }
        }
        if (book_) {
            book_->advance(1.0 / 30, c);
            quit_ = book_->quitRequested();
            if (auto chosen = book_->chosen()) {
                enterCourse(*chosen, book_->courseNumber(*chosen));
            }
        } else if (courseUi_ && courseUi_->pauseOpen()) {
            auto result = courseUi_->pauseAdvance(1.0 / 30, c);
            if (result == session::CourseUi::Result::Exit) {
                progress_.lastPlayed = course_->stage;
                progress_.returnedFromClear = false;
                progress_.returnedFromStage = true;
                enterBook();
            }
        } else {
            if (courseUi_ && (courseUi_->clearRunning() || courseUi_->missRunning())) {
                c = {};
            }
            scene_->advance(step_seconds(), c);
            if (courseUi_) {
                if (!courseUi_->clearRunning() && !courseUi_->missRunning()) {
                    if (scene_->goalReached()) {
                        courseUi_->startClear();
                    } else if (scene_->missed()) {
                        courseUi_->startMiss();
                    }
                }
                if (courseUi_->missRunning() &&
                    courseUi_->missAdvance(step_seconds()) == session::CourseUi::Result::Restart) {
                    progress_.lives = std::max(0, progress_.lives - 1);
                    enterCourse(*course_, courseNumber_);
                } else if (courseUi_->clearRunning() &&
                           courseUi_->clearAdvance(step_seconds()) == session::CourseUi::Result::Cleared) {
                    progress_.cleared.insert(course_->stage);
                    progress_.gems[course_->stage] |= scene_->collectedGems();
                    progress_.lastPlayed = course_->stage;
                    progress_.returnedFromClear = true;
                    progress_.returnedFromStage = true;
                    enterBook();
                }
            } else if (scene_->finished()) {
                progress_.cleared.insert("Season1OpeningStage");
                progress_.lastPlayed = "Season1OpeningStage";
                progress_.returnedFromClear = true;
                progress_.returnedFromStage = true;
                enterBook(true);
            }
        }
    }
    void audio(std::span<float> stereo, int rate) override {
        if ((!scene_ && !book_) || next_ != Next::None || rate <= 0) {
            std::fill(stereo.begin(), stereo.end(), 0);
            return;
        }
        if (!audioReady_) {
            sourceAudio(samples_.data(), 2);
            audioReady_ = true;
        }
        // The DSP's rational output rate, not the standalone API's rounded integer.
        const double ratio = (268111856.0 / 8192.0) / rate;
        for (size_t i = 0; i + 1 < stereo.size(); i += 2) {
            for (int ch = 0; ch < 2; ++ch) {
                stereo[i + ch] = float((samples_[ch] * (1 - audioPhase_) + samples_[2 + ch] * audioPhase_) / 32768.0);
            }
            audioPhase_ += ratio;
            while (audioPhase_ >= 1) {
                samples_[0] = samples_[2];
                samples_[1] = samples_[3];
                sourceAudio(samples_.data() + 2, 1);
                audioPhase_ -= 1;
            }
        }
    }
    void set_canvas(int w, int h) override {
        if (wide_ && w > 0 && h > 0) {
            height_ = 480;
            width_ = int(std::lround(480 * std::clamp(double(w) / h, 4.0 / 3, 21.0 / 9)));
        } else {
            width_ = 800;
            height_ = 480;
        }
    }
    void set_frame_fraction(double f) override { fraction_ = std::clamp(f, 0.0, 1.0); }
    bool take_menu_request() override {
        bool result = menuRequest_;
        menuRequest_ = false;
        return result;
    }
    int vhs_level() const override { return filter_; }
    void set_input_capabilities(::platform::InputCapabilities c) override { touch_ = c.mobile || c.touchscreen; }
    std::vector<::platform::TouchControl> touch_controls() const override {
        if (dualScreen() || controlScheme_ == 1 || (controlScheme_ == 0 && !touch_)) {
            return {};
        }
        using T = ::platform::TouchControl;
        return {{"move", "Move", T::Kind::stick, .02f, .56f, .24f, .40f},
                {"camera", "Camera", T::Kind::stick, .74f, .56f, .24f, .40f},
                {"action", "A", T::Kind::button, .64f, .77f, .09f, .16f},
                {"run", "B", T::Kind::button, .54f, .77f, .09f, .16f},
                {"left", "L", T::Kind::button, .02f, .06f, .10f, .13f},
                {"right", "R", T::Kind::button, .88f, .06f, .10f, .13f}};
    }
    std::vector<::app::GameOption> options() const override {
        return {
            ::app::screen_filter_option(filter_),
            {"Smooth motion",
             {"Original", "Interpolated"},
             int(interpolate_),
             "Interpolate the 30 Hz scene; game timing is unchanged.",
             "Display",
             true},
            {"View", {"Original 5:3", "Wide"}, int(wide_), "Expand the camera view to the window.", "Display", true},
            {"Control scheme",
             {"Automatic", "Keyboard / gamepad", "Touch sticks"},
             controlScheme_,
             "Arrows / left stick move; Z/A acts, X/B runs; IJKL / right stick aims; Q/E / shoulders rotate.",
             "Controls",
             true},
            {"Course selection",
             {"Keep playing", "Course book", "Course 1", "Course 2", "Course 3", "Course 4"},
             courseChoice_,
             "Restart at the book or one of the four reconstructed courses. Later courses are unavailable.",
             "Cheats",
             false}};
    }
    void set_option(size_t i, int v) override {
        if (i == 0) {
            filter_ = std::clamp(v, 0, 4);
        }
        if (i == 1) {
            interpolate_ = v != 0;
        }
        if (i == 2) {
            wide_ = v != 0;
        }
        if (i == 3) {
            controlScheme_ = std::clamp(v, 0, 2);
        }
        if (i == 4 && v > 0 && v <= 5) {
            static constexpr const char* scenes[] = {"Book", "PullUpStage", "RevolverRuinsStage",
                                                     "TouchPuzzleMiniStage", "FreshRuinsStage"};
            Checkpoint next;
            next.scene = scenes[v - 1];
            next.progress = progress_;
            next.progress.cleared.insert("Season1OpeningStage");
            read_state(next.encode());
            courseChoice_ = v;
        } else if (i == 4 && v == 0) {
            courseChoice_ = 0;
        }
    }
    std::vector<std::array<int, 4>> filter_regions() const override {
        if (!dualScreen()) {
            return {};
        }
        auto r = session::ScreenRects::fit(0, 0, width_, height_, true);
        return {{r.top[0], height_ - r.top[1] - r.top[3], r.top[2], r.top[3]},
                {r.bottom[0], height_ - r.bottom[1] - r.bottom[3], r.bottom[2], r.bottom[3]}};
    }
    std::vector<uint8_t> write_state() override { return checkpoint_.encode(); }
    void read_state(std::span<const uint8_t> bytes) override {
        auto decoded = Checkpoint::decode(bytes);   // Validate everything before replacing live state.
        if (device_) {
            gpu::setDevice(device_.get());
        }
        courseUi_.reset();
        book_.reset();
        scene_.reset();
        course_.reset();
        checkpoint_ = std::move(decoded);
        progress_ = checkpoint_.progress;
        restored_ = true;
        next_ = checkpoint_.scene == "Book" ? Next::Book : Next::None;
        afterOpening_ = false;
        quit_ = false;
        stopped_.clear();
        startHeld_ = false;
        fraction_ = 0;
        resetAudio();
        resetUiInput();
    }
    ::platform::Image save_picture() const override {
        // Capture the restart point, not the current in-course position. The temporary
        // module borrows the same host but owns its resources; restore the active device.
        try {
            ::platform::Image result{1, 1};
            {
                Opening preview(content_);
                preview.attach_gpu(surface_);
                preview.read_state(checkpoint_.encode());
                preview.interpolate_ = interpolate_;
                preview.wide_ = wide_;
                preview.width_ = width_;
                preview.height_ = height_;
                result = preview.picture();
            }
            gpu::setDevice(device_.get());
            return result;
        } catch (...) {
            gpu::setDevice(device_.get());
            throw;
        }
    }
    std::string saved_message() const override { return "Saved checkpoint — Continue restarts this scene."; }
    bool finished() const override { return quit_; }
    std::string stopped() const override { return stopped_; }
};
}   // namespace
std::unique_ptr<::app::GameModule> opening(const std::filesystem::path& content) {
    return std::make_unique<Opening>(content);
}
}   // namespace captain_toad::reprise
