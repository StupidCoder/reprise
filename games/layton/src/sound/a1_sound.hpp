#pragma once
// The first adventure's sound: the game's sound object (ARM9 02004718..
// 02005670, one instance at 02104618) on top of the SDK archive player
// (sdat_player.hpp) and the ARM7 sequencer (sdat_sequencer.hpp), and the
// clock that runs them against the DS sound hardware (DsSound).
//
// How the game uses it
// --------------------
// Two sound heaps: one for the music (a sequence with its bank), one for
// the effects (two sequence archives and their banks, loaded as a group,
// plus a voice archive). A scene says what it needs by its kind (02004b08:
// music, effect archives and group from tables in the program) or, for a
// puzzle, by the puzzle's number (02004a50). Effects are played by number:
// the first effect archive is tried, then the second (020053ac); number 0
// is the voice line of the voice archive (02005380).
//
// Loading new music (02004dc4) or effects (02004cb4) first fades the music,
// stops the looped effects and waits, frame by frame, until nothing from
// the old files plays; then the heap level is restored and the new files
// are loaded. This is why a puzzle's result waits for the "submit" sound.
//
// Rooms and story pick their sounds by room or event number (02004990,
// 02004bbc); voice lines and the movies' sound are streams (020045f8,
// a1_stream.hpp) on two channels the driver plays directly.
//
// Timing: the game calls frame() once per main-loop frame (the archive
// player's update and the command flush); the audio side renders samples at
// the hardware rate, running a driver pass on every alarm (174,592 clocks)
// and when the game wakes the driver after freeing a heap level. A pass
// takes the batches that have arrived by its commands step; its register
// writes land when the ARM7 reaches them (driverPass).
#include "sound/a1_stream.hpp"
#include "sound/ds_sound.hpp"
#include "sound/sdat.hpp"
#include "sound/sdat_player.hpp"
#include "sound/sdat_sequencer.hpp"
#include "system/scene_kit.hpp"
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace picarat {

class Adventure1Sound final : public SceneKit::SceneSound {
public:
    // A frame of the game (the scene presents one) while the sound object
    // waits for sounds to end.
    using WaitFrame = std::function<void()>;

    explicit Adventure1Sound(const std::filesystem::path& pack);
    ~Adventure1Sound() override;

    // --- the game's calls -------------------------------------------------
    void frame() override; // the archive player's frame (020964e0)
    void idle() override;  // a loading pause: only time passes
    // Without a sound device: every frame (and idle frame) also renders a
    // frame's time of sound (560,190 clocks), so waits for sounds to end
    // run in step with the game (tests, research runs).
    void clockWithFrames(bool on);
    // Research: where in a frame the game's sound frame runs (clocks after
    // the frame starts; about 290,000 in a reference run) when clocked by
    // frames.
    void setFrameFlush(std::uint64_t clocks);
    void effect(int id) override; // 020053ac
    void voice();                 // 02005380
    // 02004b08: music, effects and group for a kind of scene (the puzzle's
    // title card is 5, the hint screen 9, a result 0xa or 0xb).
    void setupScene(int kind, const WaitFrame& wait);
    // 02004a50: the effects of puzzle `number` (negative: none chosen).
    void setupPuzzle(int number, int engineKind, const WaitFrame& wait);
    void loadMusic(int sequence, const WaitFrame& wait); // 02004dc4
    void loadVoice(int group, const WaitFrame& wait);    // 02004f3c
    void playMusic(int sequence);                        // 0200530c
    void stopMusic(int frames);                          // 0200517c
    void stopMusicSoftly();                              // 02005354: over 30 frames
    void fadeOutMusic();                                 // 02005114: to 0 over 60
    void fadeInMusic();                                  // 02005144: to 127 over 60
    void restoreMusicVolume();                           // 0200527c: 127 at once
    void moveMusicVolume(int target, int frames);        // 020052f4
    void rememberMusic();                                // 02004f10
    // The text box's typing tick (0200e158): 02005414 (effect 0x29 of the
    // first effect archive unless one still sounds, then pending) and an
    // archive player frame, again and again while the tick is pending, so
    // the game stands still until the previous tick has ended; `wait` runs
    // each frame the loop holds up.
    void typingSound(const WaitFrame& wait);
    void retryTyping();                      // 0200546c: start a pending tick
    void startLoopedEffect(int id);          // 0200552c
    void stopLoopedEffect();                 // 02005594: out over 5 frames
    void startLoopedEffect15();              // 020055d0
    void stopLoopedEffect15();               // 0200562c: out over 30 frames
    void reloadMusic(const WaitFrame& wait); // 02004f28
    int loadedMusic() const;
    // 02004cb4: two effect archives and the group that loads them.
    void loadEffects(int archiveA, int archiveB, int group, const WaitFrame& wait);

    // Rooms and story (no caller in the port yet).
    // 02004990: music and effects of room `room` (the game state's byte +0xe).
    void setupRoom(int room, const WaitFrame& wait);
    // 02004bbc: music and effects of story event `event` (the game state's
    // word +0); event 0x154 keeps what is loaded.
    void setupEvent(int event, const WaitFrame& wait);
    // 02004768: start the loaded music unless it already plays (always the
    // first time); `enabled` is the game state's byte +0xf.
    void startLoadedMusic(bool enabled);
    void forgetLoadedMusic();                                   // 02004d80: load again next time
    void loadAndPlayMusic(int sequence, const WaitFrame& wait); // 02004d94
    void fadeOutMusicSlowly();  // 020051a4, 0200520c: to 0 over 100 frames (+0xe752c)
    void fadeOutMusicQuickly(); // 02005244: to 0 over 10 frames (+0xe7530)
    void fadeOutMusic30();      // 020051dc: to 0 over 30 frames
    void fadeInMusicSlowly();   // 020052b4: to 127 over 100 frames (+0xe7528)
    void startRoomLoop();       // 02005490: effect 0x2bd of either archive, looped
    // 020054f4: the caller's second argument passes through as the frames.
    void stopRoomLoop(int frames);

    // The object's heap levels: free back to level 0 (music, 02004830), 1
    // (effects, 02004874) or 4 (effects, 02004918); save level 2 (music,
    // 020048bc with 6), 3, 4, 5 (effects, 020048e0 / 020048fc / 02004958)
    // or 6 (music, 02004974).
    void freeLevel(int level);
    void saveLevel(int level);

    // Streams (voice lines, the movies' sound; a1_stream.hpp).
    // 020045f8: play stream/<name> on channels `first` (pan 0) and `second`
    // (pan 127) at `volume` (0..127), replacing any stream.
    void playStream(const std::string& name, int first, int second, int volume);
    void closeStream();                       // 020046ac
    void pauseStream();                       // 02004594
    void resumeStream(const WaitFrame& wait); // 020045b8: after 20 frames
    bool streamPlaying() const;               // 02074ab8
    void setStreamVolume(int volume);         // 02074b60: the playing stream's volume
    // The typing tick's effect (+0xe7504): 0x29 by default; the talk
    // windows pick 0x28..0x2a by speaker ("@1".."@3" in the texts).
    void setTypingEffect(int id);

    // Research: where the driver's alarm falls within its period (clocks),
    // to line ticks up with a reference recording.
    void setAlarmPhase(std::uint64_t clocks);
    // Research: clocks from the alarm to the pass's first register write.
    void setWriteLatency(std::uint64_t clocks);
    // Clocks from the archive player's flush until the driver has the batch.
    void setPostLatency(std::uint64_t clocks);
    // Clocks from the end of a pass's write loops to where its commands
    // step writes directly used channels.
    void setCommandStep(std::uint64_t clocks);
    // Research: the clock of the next call (finer than the output samples).
    void setCallClock(std::uint64_t clock);
    // Research: every driver pass on an alarm, with its clock and its work.
    std::function<void(std::uint64_t, const sdat::Sequencer::Work&)> passTrace;
    // Research: one try of the typing tick's loop (replays step the loop
    // with the recording), and whether the tick is still pending.
    void typingTry();
    bool typingPending() const;
    // Research: samples mixed so far (the stamp of a register write).
    std::uint64_t mixedSamples() const;
    // Research: the 16-bit mix before the DAC (DsSound::dac10 off).
    void setRawOutput(bool raw);
    // Research: the hardware's register writes (DsSound::registerTrace).
    void setRegisterTrace(std::function<void(std::uint32_t, unsigned, std::uint32_t)> trace);

    // --- the audio side ----------------------------------------------------
    // Interleaved stereo at the hardware rate (32,728.5 Hz).
    void render(std::span<std::int16_t> stereo);

private:
    struct Tables;
    mutable std::recursive_mutex mutex_;
    std::unique_ptr<Tables> tables_;
    sdat::Archive archive_;
    DsSound hardware_;
    sdat::Sequencer sequencer_;
    sdat::ArchivePlayer player_;
    std::uint64_t clocks_ = 0, nextAlarm_ = 0, frameClocks_ = 0, flushAt_ = 0;
    bool frameClock_ = false;
    std::vector<std::int16_t> scratch_;
    void renderLocked(std::span<std::int16_t> stereo);
    // Output mixed ahead of the caller (a pass's writes fall up to two
    // samples after its alarm), and the pass in progress.
    std::deque<std::int16_t> mixed_;
    std::uint64_t writeLatency_ = 0;
    // The players-running word as the game sees it: the pass writes it at
    // the end of its player step (`playersAt_`), before then the old value.
    std::uint32_t playersBefore_ = 0;
    std::uint64_t playersAt_ = 0;
    std::uint32_t runningPlayers() const;
    // Batches handed to the driver: when each arrives (clock), and how many
    // the driver has.
    std::deque<std::pair<std::uint32_t, std::uint64_t>> arrivals_;
    std::uint32_t sentBatches_ = 0, arrivedBatches_ = 0;
    // From the archive player's frame call until the batch can be taken
    // (the frame's work, the flush, the PXI transfer, the ARM7's interrupt).
    // Fitted to a reference run, where the commands step falls 1,580 to
    // 1,640 clocks after the calls of the closest cases: every batch of the
    // run goes to the ROM's pass with 1,450..1,550 in this pass model.
    std::uint64_t postLatency_ = 1500;
    std::uint64_t callClock_ = 0;     // when the game's current call happens
    std::uint64_t commandStep_ = 500; // fitted: 466..550 time 138 of 154 stream writes
    void playerFrame();
    void flushCommands(std::uint64_t clock); // 020a0428 outside the frame
    std::uint64_t now() const;
    void mixOne();
    // A pass of the driver's thread from `entry` (clock): on the alarm with
    // `tick`, or `woken` by the game (all its batches have arrived).
    void driverPass(std::uint64_t entry, bool tick, bool woken);
    // A wake-up the game asked for (clock; 0: none), and its latency from
    // the heap call: measured once (a reference run's only wake-up with
    // writes): 14 samples of the game freeing, the wake message, the first
    // stop 2,465 clocks later, 18,200 clocks after the call in all, less
    // this model's 724 from a pass's start to its first command.
    std::uint64_t wakeAt_ = 0, wakeLatency_ = 17500;
    void sync();
    void renderUntil(std::uint64_t clock);

    // The sound object's fields (+0xe74b0 ..).
    int musicHeap_ = -1, effectHeap_ = -1;
    int levels_[7] = {-1, -1, -1, -1, -1, -1, -1}; // its saved heap levels (+0 .. +0x18)
    // Handles: music, effects, voice, the looped effects (+0xe74d4, +0xe74d0).
    sdat::ArchivePlayer::Handle music_, effects_, voice_, typing_, loop1_, loop2_, loop3_;
    int effectArchiveA_ = -1, effectArchiveB_ = -1, voiceArchive_ = -1;
    int music_seq_ = 0, previousMusic_ = -1, rememberedMusic_ = -1; // +0xe74f8, +0xe74e4
    bool loop1On_ = false;                                          // +0xe74df (handle +0xe74cc)
    bool loop2On_ = false, loop3On_ = false;                        // +0xe74e1, +0xe74e0
    // Fade lengths the constructor (02004488) sets: +0xe7528 in, +0xe752c
    // out, +0xe7530 short out.
    int fadeInFrames_ = 100, fadeOutFrames_ = 100, shortFadeFrames_ = 10;
    int typingEffect_ = 0x29, loopedEffect_ = -1; // +0xe7504, +0xe7500
    bool typingPending_ = false;                  // +0xe7508
    // 020cea2c: +0 nothing started yet, +4 the music that should play, +8
    // the music loaded (the constructor sets +4 to 0, the program's data
    // starts the others at 1 and -1).
    bool firstStart_ = true;
    int wantedMusic_ = 0, loadedMusic_ = -1;

    // The stream: decoder, channels, volume, whether its channels play
    // (+6), and its alarm (next expiry and period, clocks; 0: none).
    std::filesystem::path pack_;
    std::unique_ptr<Adventure1Stream> stream_;
    bool streamOpen_ = false, streamPlaying_ = false;
    std::array<unsigned, 2> streamChannels_{10, 11};
    int streamVolume_ = 127;
    std::uint64_t streamAlarm_ = 0, streamPeriod_ = 0;
    void startStream();
    void stopStream();
    void streamAlarm();

    void freeLevelLocked(int level);
    void stopLoops();
    void typingLocked();
    void waitWhile(const std::function<bool()>& busy, const WaitFrame& wait);
    void loadEffectsLocked(int archiveA, int archiveB, int group, const WaitFrame& wait);
};

} // namespace picarat
