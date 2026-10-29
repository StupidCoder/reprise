#pragma once
// How sound works in the game, from top to bottom:
//
//   scenes      call GameSound: "play effect 0x6f", "room sound 12",
//               "stream voice 110", "fade the music"...
//   GameSound   the overlay-2 sound object (this file): which sound banks
//               are loaded, which music belongs to a room, effect and voice
//               requests, the music and stream controllers.
//   dse::Driver the game's resident sequencer (dse_driver.hpp): plays music
//               (.SMD sequences) and effects (.SED) with instrument samples
//               from sound banks (.SWD), track by track, note by note.
//   SadStream   streamed audio (.SAD files: voices, jingles, film sound),
//               decoded block by block (sad_audio.hpp).
//   DsSound     the DS sound hardware's 16 channels and mixer (ds_sound.hpp).
//   SoundEngine runs the driver and streams at the right moments and mixes
//               the output for the host (sound_engine.hpp).
//
// All of it except DsSound and SoundEngine is reconstructed from the game's
// own code; those two stand in for the DS hardware and its sound processor.
//
// Recovered overlay-2 sound object (lt2SoundObject, ARM9 overlay 2 loaded at
// 0x02068880): pools, the SY/GE/SI effect banks, effect requests, the room
// sound table (rc/snd_fix.dlz), the BGM controller and the SAD stream
// controller. It drives the recovered resident driver (dse_driver.hpp).
// Validated against the ROM sound object in emulator runs (research notes).
#include "sound/dse_driver.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace picarat {

// Scene-facing API (the overlay-2 entry points the game calls).
struct GameSound {
    virtual ~GameSound() = default;
    virtual void frame() = 0;                              // 0x0207faf4 (BGM controller update)
    virtual void setScene(std::uint8_t scene) = 0;         // game state +0x478 (read by 0x0207dc90)
    virtual void roomSound(int id, bool playBgm) = 0;      // 0x0207f780
    virtual void loadBank(unsigned group, int number) = 0; // 0x0207f884
    virtual void unloadBank(unsigned group) = 0;           // 0x0207f8ac
    virtual void playEffect(int id) = 0;                   // 0x0207f850 + 0x0207f8f0
    virtual void stopEffect(int id, float seconds) = 0;    // 0x0207f850 + 0x0207f918
    virtual bool effectPlaying(int id) = 0;                // 0x0207f850 + 0x0207f944
    virtual bool playTrackedEffect(int id) = 0;            // 0x0207f850 + 0x0207f96c
    virtual bool trackedBusy() = 0;                        // 0x0207f9c0
    virtual bool playUniqueEffect(int id) = 0;             // 0x0207f9e4
    virtual void stopUniqueEffect(float seconds) = 0;      // 0x0207fa50
    virtual void playBgm(int track, float volume, float seconds) = 0; // 0x0207faa4
    virtual void stopBgm(float seconds) = 0;                          // 0x0207fa94
    virtual void switchBgm(int track) = 0;                            // 0x0207fab4
    virtual int currentBgm() = 0;                                     // 0x0207fa88 (BGM bank +0x6a)
    virtual void restartBgm(float volume, float seconds) = 0;         // 0x0207fac4 -> 0x0207dea0
    virtual void fadeOutBgm(float seconds) = 0;                       // 0x0207fad4
    virtual void bgmVolume(float volume, float seconds) = 0;          // 0x0207fae4
    virtual float duration(int id) = 0; // 0x0207d3c0 (time.dlz frames / 60)
    // SAD streams through the overlay-2 stream controller (object +0x74).
    // Routes: 0 ST (nazo/ ST_%04d from id 100), 1 bgm, 2 event voice, 3 movie.
    virtual bool playStream(int route, int id, int part, float volume, float seconds,
                            bool meter) = 0; // 0x0207fb84
    virtual bool playTrackedStream(int route, int id, int part, float volume,
                                   float seconds) = 0; // 0x0207fc3c
    virtual void stopStream(float seconds) = 0;        // 0x0207fbcc
    virtual bool streamBusy() = 0; // 0x0207fb14: open and the driver stream is playing
    virtual bool streamIdle() = 0; // 0x0207fb24: nothing open and no stop pending
    virtual bool streamLoud() = 0; // 0x0207fb34: busy and the metered level span exceeds 6000
    // Movie scene (ov6 0x020f00e0) controller requests; all update at once.
    virtual bool prepareStream(int route, int id, int part,
                               bool meter) = 0;                // 0x0207fb54: open without starting
    virtual void startStream(float volume, float seconds) = 0; // 0x0207fbdc -> 0x0207e7d8
    virtual void stopStreamFade(float seconds) = 0;            // 0x0207fbec -> 0x0207e854
    virtual void restartStream(
        float volume,
        float seconds) = 0; // 0x0207fc00 -> 0x0207e8b4: seek to the status position and start
    virtual void streamVolume(float volume, float seconds) = 0; // 0x0207fc14 -> 0x0207e91c
    virtual void
    seekStream(float seconds) = 0;      // 0x0207fc28 -> 0x0207e964: relative to the status position
    virtual float streamPosition() = 0; // 0x0207fb44 -> 0x0207e5a0 (seconds)
    virtual bool streamOpen() = 0;      // 0x0207fb04 -> 0x0207e4bc
    virtual bool bgmSettled() = 0;      // 0x0207fa78 -> 0x0207dc28

    // 0x0207dc00: the music's sequence is playing (research stand-ins: no).
    virtual bool bgmBusy() { return false; }

    // The stream controller's pending open (flag 0x40) and start (0x100).
    virtual bool streamOpening() { return false; }

    virtual bool streamStarting() { return false; }

    virtual bool canSleep() = 0; // 0x0207f760: no stream stop pending, BGM settled
    // Whole-system pause around sleep mode (ov2 0x0206aebc lid close).
    virtual void pauseSound() = 0;  // 0x0207f6f4
    virtual void resumeSound() = 0; // 0x0207f724
};

// ROM path of a stream request (0x0207eae8 formats; language directory "ge").
std::string streamPath(int route, int id, int part);

int effectGroup(int id); // 0x0207f850: 0 SY (<50), 1 GE (<200), 2 SI (<1000), -1

// Where a game keeps its sound: the driver's tables and the files of the
// effect groups and the music. Both adventures use the same driver and
// formats; they differ in where the files live.
struct SoundLayout {
    // The game's file name (a pattern with the bank number filled in) to
    // the pack's file.
    std::function<std::filesystem::path(const std::string&)> file;
    // The program holding the driver's tables, its load address and the
    // tables' addresses (pitch, envelope times, envelope units, pan curve).
    std::filesystem::path driverProgram;
    std::uint32_t driverLoad = 0;
    std::array<std::uint32_t, 4> tables{};
    // Per group (SY, GE, SI) and for the music: the per-number bank and
    // sequence names (printf patterns of the number) and the main sample
    // bank.
    std::array<std::string, 3> groupBank, groupData, groupMain;
    std::string bgmBank, bgmData, bgmMain;
    // The room sound and effect length tables (empty: none).
    std::filesystem::path roomTable, timeTable;
    // A stream request's file name (route, id, part; empty: none).
    std::function<std::string(int, int, int)> stream;

    static SoundLayout adventure2(const std::filesystem::path& pack);
    // The third adventure: driver in overlay 1, banks in snd/<NAME>/ (the
    // game's .csnd archives unpacked by the importer), main banks
    // snd/WAV_<group>.SWD (overlay 0 names them).
    static SoundLayout adventure3(const std::filesystem::path& pack);
    // The fourth adventure: driver in overlay 0, banks in main/snd/<NAME>/
    // (the game's snd/<G>_%03d.cse unpacked by the importer), main banks
    // wav/<G>.SWD (ov7 strings). Streams: 0 jingles, 1 music, 2 film
    // sound, 3 event voices (id = first << 20 | second, part = third).
    static SoundLayout adventure4(const std::filesystem::path& pack);
};

// Recovered object. Files are read from the native pack's sound/ directory.
class SoundObject final : public GameSound {
public:
    SoundObject(std::filesystem::path assets, dse::SoundCommands& hardware); // 0x0207f384
    SoundObject(SoundLayout layout, dse::SoundCommands& hardware);
    ~SoundObject() override;

    void tick() { driver_.tick(); } // sound-thread iteration (scheduled by the engine)

    dse::Driver& driver() { return driver_; }

    // End of the pool containing `p` (sample reads stop there), or p itself.
    const std::uint8_t* memoryLimit(const std::uint8_t* p) const;

    void frame() override;

    void setScene(std::uint8_t scene) override { scene_ = scene; }

    void roomSound(int id, bool playBgm) override;
    void loadBank(unsigned group, int number) override;
    void unloadBank(unsigned group) override;
    void playEffect(int id) override;
    void stopEffect(int id, float seconds) override;
    bool effectPlaying(int id) override;
    bool playTrackedEffect(int id) override;

    bool trackedBusy() override { return trackedId_ >= 0 || trackedFrames_ < 2; }

    bool playUniqueEffect(int id) override;
    void stopUniqueEffect(float seconds) override;
    void playBgm(int track, float volume, float seconds) override;
    void stopBgm(float seconds) override;
    void switchBgm(int track) override;
    int currentBgm() override;

    void restartBgm(float volume, float seconds) override { bgmRestart(volume, seconds); }

    void fadeOutBgm(float seconds) override;
    void bgmVolume(float volume, float seconds) override;

    float duration(int id) override { return durationSeconds(id); }

    bool playStream(int route, int id, int part, float volume, float seconds, bool meter) override;
    bool playTrackedStream(int route, int id, int part, float volume, float seconds) override;
    void stopStream(float seconds) override;
    bool streamBusy() override;

    bool streamIdle() override { return !(stream_.flags & 1) && !(stream_.flags & 0x80); }

    bool streamLoud() override;
    void pauseSound() override;
    void resumeSound() override;
    bool prepareStream(int route, int id, int part, bool meter) override;
    void startStream(float volume, float seconds) override;

    void stopStreamFade(float seconds) override { streamStopFade(seconds, true); }

    void restartStream(float volume, float seconds) override;
    void streamVolume(float volume, float seconds) override;
    void seekStream(float seconds) override;
    float streamPosition() override;

    bool streamOpen() override { return streamExists(); }

    bool bgmSettled() override;

    bool bgmBusy() override { return bgmPlaying(); }

    bool streamOpening() override { return stream_.flags & 0x40; }

    bool streamStarting() override { return stream_.flags & 0x100; }

    bool canSleep() override { return !(stream_.flags & 0x80) && bgmSettled(); }

    struct Pool;  // overlay-2 two-ended pool (0x0207d434..0x0207d658)
    struct Bank;  // shared part of a group/BGM object (owner +0x10)
    struct Group; // effect group, 0x78 bytes at +0x1a8/+0x220/+0x298
    struct Bgm;   // BGM object at +0x100

private:
    SoundLayout layout_;
    dse::Driver driver_;
    std::unique_ptr<Pool> bgPool_, sePool_, syPool_;
    std::unique_ptr<Group> groups_[3];
    std::unique_ptr<Bgm> bgm_;
    std::vector<std::uint8_t> roomTable_, timeTable_;
    std::uint16_t roomCount_ = 0, roomStep_ = 0, timeCount_ = 0, timeStep_ = 0;
    std::uint8_t scene_ = 0;
    std::int32_t room_ = 0;                         // +0x1c
    std::int32_t uniqueGroup_ = -1, uniqueId_ = -1; // +0x30/+0x34
    // Tracked effect or stream (+0x20 id, +0x24 stream part, +0x28 frames
    // since it ended, +0x2c stream route; -1 for an effect).
    std::int32_t trackedId_ = -1, trackedPart_ = -1, trackedFrames_ = 0, trackedRoute_ = -1;

    // Stream controller (+0x74, 0x0207e41c..0x0207ee1b). Flags +0x0c: 1 open,
    // 2 started, 4 stop issued, 8 playing requested, 0x10 closed while stopping,
    // 0x40 open pending, 0x80 stopping, 0x100 start pending, 0x200 stop fade
    // pending, 0x400 restart, 0x800 volume, 0x1000 seek (the last three come
    // only from the movie overlay ov6).
    struct StreamControl {
        std::int32_t handle = -0x47; // +0x04 driver stream (<0: none)
        std::uint32_t flags = 0;     // +0x0c
        std::int32_t route = -1;
        // The ids are halfwords in the game (+0x18, +0x20). The third
        // adventure's event voices, which its own code opens by path
        // (020d0744: "str/evt/?/%02d_%06d_%04d.SAD"), come through here as
        // group << 16 | event, so they are kept whole; every other id fits.
        std::int32_t id = -1;
        std::int16_t part = -1; // +0x14 +0x18 +0x1a open stream
        std::int32_t reqRoute = -1;
        std::int32_t reqId = -1;
        std::int16_t reqPart = -1;                   // +0x1c +0x20 +0x22 requested
        std::uint8_t meter = 0;                      // +0x24 level metering (02004bd8)
        std::int32_t meterFrames = 0;                // +0x28
        std::int16_t levelMin = 0, levelMax = 0;     // +0x88 +0x8a channel-0 snapshot (0x02004c04)
        float startVolume = 0, startSeconds = 0;     // +0x2c +0x30
        float stopSeconds = 0;                       // +0x38
        float restartVolume = 0, restartSeconds = 0; // +0x3c +0x40
        float volume = 0, volumeSeconds = 0;         // +0x44 +0x48
        float seekSeconds = 0;                       // +0x50
        std::int32_t position = 0; // +0x78 status record +8 (0x02004b60): payload bytes consumed
    } stream_;

    bool streamExists();                                         // 0x0207e4bc
    bool streamRequest(int route, int id, int part, bool meter); // 0x0207e5e0
    bool streamStopRequest(float seconds);                       // 0x0207e73c
    bool streamStopFade(float seconds, bool update);             // 0x0207e854
    void streamFrame();                     // 0x0207e9b0 (per main-loop frame via 0x0207fc98)
    void trackedUpdate();                   // 0x0207fca8 (per main-loop frame)
    float durationSeconds(int id) const;    // 0x0207d3c0
    bool loadGroupBank(Group&, int number); // 0x0207d8c0
    void unloadGroupBank(Group&);           // 0x0207da74
    bool requestEffect(Group&, int id);     // 0x0207dab4
    bool stopGroupEffect(Group&, int id, float seconds); // 0x0207db3c
    // BGM controller (0x0207dc90..0x0207e400).
    bool bgmSwitch(int track);                            // 0x0207dc90
    bool bgmPlay(int track, float volume, float seconds); // 0x0207dd34
    bool bgmStop(float seconds);                          // 0x0207ddf4
    bool bgmRestart(float volume, float seconds);         // 0x0207dea0
    bool bgmFadeOut(float seconds);                       // 0x0207df4c
    bool bgmSetVolume(float volume, float seconds);       // 0x0207dfc0
    void bgmUpdate();                                     // 0x0207e038
    bool bgmPlaying();                                    // 0x0207dc00
};

} // namespace picarat
