#include "data/cutscenes.hpp"

#include "data/brd.hpp"

#include <cstdio>

namespace doc::data {

const std::vector<CutsceneStep>* MapEvents::cutsceneFor(const std::string& event) const {
    const auto callback = callbacks.find(event);
    if (callback == callbacks.end()) return nullptr;
    const auto cutscene = cutscenes.find(callback->second);
    return cutscene == cutscenes.end() ? nullptr : &cutscene->second;
}

std::optional<MapEvents> loadMapEvents(const GameFiles& files, int map) {
    char name[64];
    std::snprintf(name, sizeof name, "map%02d/map_events.xml.brd", map);
    const auto root = loadChunkTree(files, name);
    if (!root) return std::nullopt;
    MapEvents events;
    if (const BrdNode* list = root->child("events"))
        for (const BrdNode* e : list->childrenNamed("event"))
            if (auto event = e->getString("name"))
                events.callbacks[*event] = e->getString("callback").value_or(std::string());
    if (const BrdNode* list = root->child("cutscenes"))
        for (const BrdNode* c : list->childrenNamed("cutscene")) {
            std::vector<CutsceneStep>& steps = events.cutscenes[c->getString("name").value_or(std::string())];
            for (const BrdNode& element : c->children) {
                CutsceneStep step;
                if (element.name == "dialogue") step.kind = CutsceneStep::Dialogue;
                else if (element.name == "sequence") step.kind = CutsceneStep::Sequence;
                else if (element.name == "path") step.kind = CutsceneStep::Path;
                else if (element.name == "startLetterbox") step.kind = CutsceneStep::StartLetterbox;
                else if (element.name == "endLetterbox") step.kind = CutsceneStep::EndLetterbox;
                else continue;  // ("Unknown cutscene element")
                step.name = element.getString("name").value_or(std::string());
                step.tag = element.getInt("tag").value_or(0);
                step.actor = element.getString("actor").value_or(std::string());
                steps.push_back(step);
            }
        }
    return events;
}

std::map<std::string, std::vector<Speech>> loadDialogues(const GameFiles& files) {
    std::map<std::string, std::vector<Speech>> dialogues;
    const auto root = loadChunkTree(files, "dialogue/dialogue.xml.brd");
    if (!root) return dialogues;
    for (const BrdNode* d : root->childrenNamed("dialogue")) {
        std::vector<Speech>& speeches = dialogues[d->getString("name").value_or(std::string())];
        for (const BrdNode* s : d->childrenNamed("speech"))
            speeches.push_back({s->getString("actor").value_or(std::string()), s->getString("text").value_or(std::string())});
    }
    return dialogues;
}

}  // namespace doc::data
