// The first playable integration: the opening scene, ending at its title transition.
// No standalone shell, user-data writes, second audio device or XR mode.
#include "module.hpp"
#include "platform/gpu_commands.hpp"
#include "../src/gpu/SdlDevice.hpp"
#include "../src/gpu/VulkanShaders.hpp"
#include "../src/session/Session.hpp"
#include <cmath>
#include <cstring>
namespace captain_toad::reprise {
namespace {
class Opening final : public ::app::GameModule {
    std::filesystem::path content_;
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
        if (!std::filesystem::is_regular_file(content_ / "Opening/scene.json")) {
            throw std::runtime_error("The imported opening scene is missing. Reimport the cartridge image.");
        }
    }
    ~Opening() override {
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
            gpu::vulkan::setCacheDirectory(::platform::data_directory() / "cache/captain_toad_3ds/materials-v1");
            device_ = std::make_unique<gpu::SdlDevice>(borrowed.device);
            gpu::setDevice(device_.get());
        }
        device_->begin(borrowed.color, borrowed.format, f.width, f.height);
        if (!scene_) {
            scene_ = std::make_unique<session::Session>(content_ / "Opening");
        }
        scene_->set(session::Toggle::Interpolate, interpolate_);
        scene_->set(session::Toggle::WideView, wide_);
        scene_->setPresentationFraction(fraction_);
        scene_->render(0, 0, f.width, f.height);
        device_->end();
    }
    void tick(const ::app::GameInput& in) override {
        if (!scene_) {
            return;
        }
        menuRequest_ |= in.start && !startHeld_;
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
                    (in.trigger_left ? 4u : 0u) | (in.trigger_right ? 32u : 0u);
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
        scene_->advance(step_seconds(), c);
    }
    void audio(std::span<float> stereo, int rate) override {
        if (!scene_ || rate <= 0) {
            std::fill(stereo.begin(), stereo.end(), 0);
            return;
        }
        if (!audioReady_) {
            scene_->fillAudio(samples_.data(), 2);
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
                scene_->fillAudio(samples_.data() + 2, 1);
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
        if (controlScheme_ == 1 || (controlScheme_ == 0 && !touch_)) {
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
             true}};
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
    }
    std::string stopped() const override {
        return scene_ && scene_->finished() ? "Opening complete. The course book is the next integration milestone."
                                            : "";
    }
};
}   // namespace
std::unique_ptr<::app::GameModule> opening(const std::filesystem::path& content) {
    return std::make_unique<Opening>(content);
}
}   // namespace captain_toad::reprise
