#pragma once
// The fourth adventure's sound requests on top of the shared sound object.
//
// The game's sound manager (ov7, game +0x9174: lt4CSndBGM, lt4CSndEff,
// lt4CSndData) keeps three effect groups (SY, GE, SI: banks
// snd/<G>_%03d.cse, effect numbers below 50 / 200 / 1000), the music
// (snd/BG_%03d.cse; music 0 is silence) and a stream (jingles, music
// streams, film sound, event voices). Requests are queued and carried out
// in order once whatever they wait for is done (020da704 music, 020dab64
// stream). The scripts reach it through services 4900..4962 (ov12
// 020f8404..020f9058); a sound set (lt4_def 2111 {set, music, GE bank, SI
// bank}) chooses music and banks at once (4900, 020da39c).
//
// Picarat's sound object (shared with the second and third adventures)
// takes the same requests; the times the scripts give in frames become
// seconds (60 frames a second).
#include <array>
#include <map>
#include <vector>

namespace picarat {
struct GameSound;

class Adventure4Sound {
public:
    // `sets`: lt4_def 2111 rows {set -> music, GE bank, SI bank}.
    Adventure4Sound(GameSound& sound, std::map<int, std::array<int, 3>> sets);

    GameSound& sound() { return sound_; }

    // 4900 (020da39c) {set, [frames = 60], [flags]}: the set's music and
    // its GE and SI banks.
    bool selectSet(int set, int frames, unsigned flags);

    int set() const { return set_; } // 4901 (+0x9204)

    // Banks: 4924 {group, bank} load (020da560), 4923 {group} unload,
    // 4913/4914 the GE/SI bank (-1 none).
    bool loadBank(int group, int bank);
    int bank(int group) const;

    // Effects: 4925 play (020da688), 4926 {id, [frames]} stop (020da6ac),
    // 4921 playing (020da628), 4922 any playing (020da64c), 4927 {frames}
    // all stop (020da6d8).
    void playEffect(int id);
    void stopEffect(int id, int frames);
    bool effectPlaying(int id);
    bool anyEffectPlaying();
    void stopEffects(int frames);

    // Music: 4935 {music, [frames], [flags]} select (020da914; flag 2: not
    // started), 4934 {frames} silence, 4936 {volume, [frames], [flags]}
    // start (020da9d4), 4937 {[frames], [flag]} stop (020daa6c), 4938
    // {volume, [frames]} volume (020daaf0), 4931/4932 playing.
    bool selectMusic(int music, int frames, unsigned flags);
    void playMusic(float volume, int frames, unsigned flags);
    void stopMusic(int frames);
    void musicVolume(float volume, int frames);
    bool musicPlaying();
    // 4933 (020d8dd4 on the music's volume tween, +0x1ac +0x34 < 0x20):
    // the music's volume is still changing.
    bool musicFading();

    int music() const { return music_; }

    // Streams: 4956..4959 open (route 0 jingle, 1 music, 2 film, 3 event
    // voice {a, b, c}), 4960 {volume, [frames], [flags]} start, 4961
    // {[frames]} stop, 4962 {volume, [frames]} volume, 4954 {frames} stop
    // and close, 4955 close; 4950 nothing loading, 4951 loading, 4952 playing.
    enum Route { Jingle = 0, Music = 1, Film = 2, Event = 3 };

    void openStream(Route route, int id, int a = 0, int c = 0);
    void startStream(float volume, int frames, unsigned flags);
    void stopStream(int frames);
    void streamVolume(float volume, int frames);
    void closeStream(int frames);
    bool streamLoading();
    bool streamReady();
    bool streamPlaying();

    // 4911 (020d9e2c) {frames}: banks unloaded, music and stream faded out.
    void releaseAll(int frames);

private:
    GameSound& sound_;
    std::map<int, std::array<int, 3>> sets_;
    int set_ = -1, music_ = -1;
    std::array<int, 3> banks_{-1, -1, -1};
    // Effects started and maybe still playing (the manager's per-group list
    // of up to 16, 020d8a70).
    std::vector<int> playing_;
    bool streamOpen_ = false;
};

} // namespace picarat
