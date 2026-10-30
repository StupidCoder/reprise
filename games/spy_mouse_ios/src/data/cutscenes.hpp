// Scripted scenes of the boss levels, as data: a map's events and cutscenes
// ("map%02d/map_events.xml.brd", read by SceneCutscene::showScene @0x1c98d4 and
// loadCutsceneData @0x1c8670) and the game's dialogues ("dialogue/dialogue.xml.brd",
// DialogueHandler::load).
//
// An event names a cutscene ("intro" -> "boss_intro"); a cutscene is a list of steps the game
// runs one after the other: a dialogue (speech bubbles), a sequence (one of the game's script
// functions, by name) or a path (an actor walks the map path whose start vertex carries the
// tag), plus letterbox markers.
#pragma once

#include "data/game_files.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace doc::data {

struct CutsceneStep {
    enum Kind { Dialogue, Sequence, Path, StartLetterbox, EndLetterbox };
    Kind kind = Sequence;
    std::string name;   // dialogue or sequence name
    int tag = 0;        // path: the start vertex's tag
    std::string actor;  // path: "mouse", "fluffy", "boss" or "henchman"
    // Quick saves (state_io.hpp).
    template <class Io>
    void io(Io& io) { io(kind, name, tag, actor); }
};

struct MapEvents {
    std::map<std::string, std::string> callbacks;                  // event -> cutscene
    std::map<std::string, std::vector<CutsceneStep>> cutscenes;    // name -> steps
    const std::vector<CutsceneStep>* cutsceneFor(const std::string& event) const;
};

// The map's events, or nullopt when the map has none.
std::optional<MapEvents> loadMapEvents(const GameFiles& files, int map);

struct Speech {
    std::string actor;    // who speaks ("fluffy")
    std::string textKey;  // string key of the text ("BOSS_INTRO_01")
    // Quick saves (state_io.hpp).
    template <class Io>
    void io(Io& io) { io(actor, textKey); }
};

// Dialogue name -> its speeches in order.
std::map<std::string, std::vector<Speech>> loadDialogues(const GameFiles& files);

}  // namespace doc::data
