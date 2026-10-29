#pragma once
// The third adventure's sound requests on top of the shared sound object.
//
// The game keeps, beside its sound object, which sound set, effect banks and
// music it has asked for (game state +0x5ad0..+0x5ad8) and turns the
// scripts' requests (48xx) into requests to the sound object: sound sets
// from rc/ge/lt3_rc/lt3_snd (020d0100/020d01a0), the music (BGM object,
// 0x0207217c: load 0205ed10, play 0205edc4, stop 0205ee88, volume 0205ef2c)
// and streams (stream object, 0x020721c8: open 0205f590, start 0205f678,
// stop 0205f738). Picarat's sound object (shared with the second adventure)
// takes the same requests; the times the scripts give in frames become
// seconds (60 frames a second).
#include <array>
#include <map>

namespace picarat {
struct GameSound;

class Adventure3Sound {
public:
    // `rows`: the sound sets (lt3_snd 5301 rows {set, music, GE, SI}).
    Adventure3Sound(GameSound& sound, std::map<int, std::array<int, 3>> sets);

    GameSound& sound() { return sound_; }

    // Effects (4820/4821 -> 020601ec/02060210; 4813 -> 020601a0).
    void playEffect(int id);
    void stopEffect(int id, int frames);
    bool effectPlaying(int id);

    // 4800/4801 (020d0100): a sound set's music (unless flag 0x100) and its
    // GE (unless 0x400) and SI (unless 0x800) banks.
    void selectSet(int set, unsigned flags, int frames);
    // 4830 (020d04fc): loads music `id`, stopping other music in `frames`.
    bool loadMusic(int id, int frames);
    // 4831 (0206027c): plays the loaded music (flag 2: not).
    void playMusic(float volume, int frames, unsigned flags);
    void stopMusic(int frames);                 // 4832 (020602a4)
    void musicVolume(float volume, int frames); // 4835 (020602bc)
    bool musicPlaying();                        // condition 4810 (0205ecf4)

    // Streams: 4851 music, 4852 ordinary, 4853 film, 4854 event (opened,
    // 02060344), 4860 started (02060364; flag 2: not), 4861/4850 stopped.
    enum Route { Music = 0, Ordinary = 1, Film = 2, Event = 3 };

    void openStream(Route route, int id, int part = 0);
    void startStream(float volume, int frames, unsigned flags);
    void stopStream(int frames);
    bool streamOpening(); // condition 4811 (0205f4e4: state 1)
    bool streamPlaying(); // condition 4812 (0205f4f8)

    // 4810..4814 (020d00ec, 020d0260, 020d0274, 020d0288, 020d04e8): the
    // set, the SY, GE and SI banks and the music asked for last.
    int set() const { return set_; }

    int bank(int group) const { return banks_[std::size_t(group)]; }

    int music() const { return music_; }

private:
    bool loadBank(int group, int number); // 020d029c / 020d0350 / 020d0404
    GameSound& sound_;
    std::map<int, std::array<int, 3>> sets_;
    // The SY bank is loaded with the sound object (number 0).
    int set_ = -1, music_ = -1;
    std::array<int, 3> banks_{0, -1, -1};
};

} // namespace picarat
