// =============================================================================================
// CourseUi.hpp - the game's screens over a course: pause menu, leave confirmation, course clear
// =============================================================================================
//
// The game's in-course UI comes from the Gameplay UI archive:
//   * the pause menu (UI screen "Pause": PauseMenu_TOP over the frozen course with the course's
//     number and name, PauseMenu_BOTTOM on the touch screen with "Continue" and "Exit this course",
//     texts LayoutMessage/PauseMenu Button0 / Button1);
//   * the leave confirmation ("WindowConfirmDouble": WindowDialogs_BOTTOM, texts
//     SystemMessage/WindowConfirmPauseMenu ReturnToMap*);
//   * the course clear: the COURSE CLEAR banner (HeadCourseClear_TOP) and the exit wipe
//     (WipeClear_TOP, Wipes package).
// The layouts, texts and animation names are the game's. The clear follows the goal demo
// (Session::goalReached is GoalPose step 0): the banner at step 96 (0x1df400), the exit wipe at
// step 162 at its natural speed (traced, evidence/pullup/goal-demo), done when it has closed.
// The menus' choreography is not traced.
// =============================================================================================
#pragma once
#include "importer/Stages.hpp"
#include "render/UiLayout.hpp"
#include "session/Session.hpp"

#include <filesystem>
#include <memory>
#include <string>

namespace captain_toad::session {

class CourseUi {
public:
    CourseUi(const std::filesystem::path& dataDirectory, const captain_toad::CourseEntry& course, int courseNumber);
    ~CourseUi();

    enum class Result { None, Resume, Exit, Cleared, Restart };

    // Pause menu: opened by the player; ticks at 30 Hz while open.
    void openPause();
    bool pauseOpen() const { return pauseTop_ != nullptr; }
    // Touch in touch-screen pixels (0..320 x 0..240, y down).
    void touch(bool down, float x, float y);
    Result pauseAdvance(double seconds, const Controls& controls);

    // Course clear: started when the goal demo reaches GoalPose (Session::goalReached); the banner at
    // tick 96 (GoalPose step 96), the exit wipe WipeClear_TOP at 162 (after the demo's end at 160);
    // Cleared when the wipe has closed.
    void startClear();
    bool clearRunning() const { return clearTick_ >= 0; }
    Result clearAdvance(double seconds);

    // Miss (a fall): the MISS banner (HeadMiss_TOP) and the miss wipe (WipeMiss_TOP); Restart
    // when the wipe has closed. Timing not traced (banner at once, wipe after 45 ticks).
    void startMiss();
    bool missRunning() const { return missTick_ >= 0; }
    Result missAdvance(double seconds);

    // The title card the book showed (WipeStart_TOP at Wait): the course ends it with the stage
    // start (startTick, from Session::onTick): End1 when the stage scene starts (the loader's
    // message 0x4d, 0x3dfa4c: the card closes to a circle on the protagonist), End2 when the
    // stage-start demo turns (0x4e, 0x3dfb84: the circle opens); it dies when End2 has played
    // (0x4119b8). The boss stages BossBird1Stage / BossBird2Stage use End4 / End5.
    void takeTitleCard(std::unique_ptr<captain_toad::ui::Package> package,
                       std::unique_ptr<captain_toad::ui::Instance> card);
    void startTick(const Session::StartTick& t);
    bool titleCardShown() const { return card_ != nullptr; }

    // Enhancement (drawing between ticks): draw the layouts between their 30 Hz steps (`cardAlpha`:
    // where this frame lies between the game's last two ticks, for the title card).
    void setSmooth(bool on, float cardAlpha) {
        smooth_ = on;
        cardAlpha_ = cardAlpha;
    }
    // Draws the overlays of the top screen into the current viewport, and the touch screen's.
    void drawTop();
    void drawBottom();
    // True while the touch screen shows something (the pause menu).
    bool needsBottom() const { return pauseTop_ != nullptr; }

private:
    void pauseTick(const Controls& c);
    void select(int button);

    std::unique_ptr<captain_toad::ui::Package> ui_;
    std::unique_ptr<captain_toad::ui::Package> wipes_;   // the Wipes package (the exit wipe WipeClear_TOP)
    std::string season_;
    std::u16string stageNumber_, stageName_;
    std::unique_ptr<captain_toad::ui::Instance> pauseTop_, pauseBottom_, dialog_, clearBanner_, clearWipe_;
    enum class Pause { Appear, Menu, Confirm, Closing, Leaving };
    Pause pause_ = Pause::Appear;
    int cursor_ = 0;    // 0 Continue, 1 Exit this course
    int confirm_ = 0;   // 0 Don't leave (left), 1 Leave (right)
    int t_ = 0;
    double clock_ = 0;
    uint32_t prevButtons_ = 0;
    float prevStick_ = 0;
    bool touchDown_ = false, prevTouch_ = false;
    float touchPos_[2] = {0, 0};
    Result pending_ = Result::None;
    int clearTick_ = -1;
    double clearClock_ = 0;
    bool smooth_ = false;
    float cardAlpha_ = 1;
    std::unique_ptr<captain_toad::ui::Instance> missBanner_, missWipe_;
    int missTick_ = -1;
    double missClock_ = 0;
    std::unique_ptr<captain_toad::ui::Package> cardPackage_;
    std::unique_ptr<captain_toad::ui::Instance> card_;
    bool bossStage_ = false;
    int cardEnd_ = 0;   // 0 Wait, 1 End1 (End4), 2 End2 (End5)
};

}   // namespace captain_toad::session
