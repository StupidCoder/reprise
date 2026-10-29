#pragma once
// The movie lists of the games' bonus "movie theatre" pages, with the
// thumbnails those pages show. Neither game names its movies; the pages show
// a small picture of each.
//
//  * Second adventure, bonus menu overlay 23: 23 movies (the page's two
//    loops end at 18 and 23, 020f2ca4 / 020f2d2c), button n showing frame
//    "<n>" of menu/secret/movie_icon.spr (020f2c2c..020f2c54). A movie
//    replays through its story event, entry n of the table at 020f3e3c
//    (020f2f1c; src/menus/a2_bonus_menus.cpp, MovieTheatre).
//  * Third adventure, loop lt3CLoopSecretMovie (overlay 29): 37 buttons
//    btn_movie1..37 (script evt/prg/lt3_scrt_movie, 20 on the first page and
//    17 on the second), button n showing animation "i<nn>_off" of
//    menu/?/s_movie.cani and playing the movie in entry n of the table at
//    0211e130 (1..19, 37, 20..36).
//  * First adventure, the bonus movie theatre (scene 0xf, 0202f944): 16
//    movies, button n showing animation n of movie_file1.spr and playing
//    m<n>.mods with its script event/m<n>.gds (0202fb80).
#include "platform/image.hpp"
#include <filesystem>
#include <vector>

namespace picarat::shell {

struct MovieEntry {
    int id = 0;                // the movie's number on the page (third adventure: m<id>.mods)
    unsigned event = 0;        // second adventure: the event that plays it
    platform::Image thumbnail; // RGBA, transparent where the picture has no pixel
};

// Empty when the pack lacks the data (an older pack).
std::vector<MovieEntry> adventure2Movies(const std::filesystem::path& pack);
std::vector<MovieEntry> adventure3Movies(const std::filesystem::path& pack);
std::vector<MovieEntry> adventure1Movies(const std::filesystem::path& pack);

} // namespace picarat::shell
