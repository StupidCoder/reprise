#pragma once
// The first adventure's native story screens around the new game and its
// movies.
//
//  * The opening text (ARM9 02035d90 / 02035f30): over a letter on the lower
//    screen, a line with the player's name fades in and out, then five
//    lines of text appear one by one while a narrator speaks.
//  * Movies (02018fa4 single screen, 020191d8 both screens): Mobiclip
//    films with their own sound stream, captions and music cues set by the
//    movie's script.
//  * The intro conversation (0202b1d0 / 0202c8ac): the two heroes talk in
//    their car, line by line with voices, over painted pictures; it comes in
//    two parts, before and after the first puzzle.
//  * The title logo (0201bd50 / 0201c108): the game's logo rises from the
//    lower screen to the upper one; a touch moves on.
#include <cstdint>
#include <vector>

namespace picarat::adventure1 {
class Story;

void openingText(Story& story);
void introTalk(Story& story, int part);
void titleLogo(Story& story);

// A movie's description (ARM9 02019580 / 020195c8 / 020195fc fill it from
// the movie scripts): music cues and captions.
struct MovieCues {
    bool upper = false; // byte +0: shown on the upper screen (display swap)

    struct Music {
        int frame = -1, frames = 0, volume = 0, sequence = -1;
    };

    struct Caption {
        int text = -1;
        float start = 0, end = 0; // seconds
    };

    std::vector<Music> music;      // +2 count, +4 / +0x1c / +0x34 / +0x4c
    std::vector<Caption> captions; // +3 count, +0x6c (text, start, start + length)
};

// 02018fa4 (one screen, movie m<N>) / 020191d8 (both screens, emt<N> and
// emb<N>); N is the state's movie number.
void playMovie(Story& story, const MovieCues& cues, bool bothScreens);

} // namespace picarat::adventure1
