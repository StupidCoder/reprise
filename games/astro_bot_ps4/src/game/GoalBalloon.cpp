#include "game/GoalBalloon.hpp"

#include "data/Bytes.hpp"
#include "data/Xml.hpp"
#include "game/LevelDocument.hpp"
#include "game/LevelRuntime.hpp"

#include <algorithm>

namespace beacon::game {

GoalTuning loadGoalTuning(const std::filesystem::path& root) {
    GoalTuning t;
    data::Bytes bytes;
    if (!data::loadFile(root / "data" / "multi_platformer" / "multi_platformer.xml", bytes)) {
        return t;
    }
    auto xml = data::parseXml(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    const data::XmlNode* goal = xml ? xml->child("GoalBalloon") : nullptr;
    if (!goal) {
        return t;
    }
    auto read = [&](const char* name, float& value) {
        if (auto text = goal->childText(name)) {
            data::parseFloatList(*text, &value, 1);
        }
    };
    read("PumpSpeed", t.pumpSpeed);
    read("PumpSpring", t.pumpSpring);
    read("PumpDamper", t.pumpDamper);
    read("PumpDepth", t.pumpDepth);
    read("VrPlayerZPos", t.vrPlayerZPos);
    return t;
}

GoalSetup goalSetup(const Level& level, const LevelRuntime& runtime, std::size_t index) {
    GoalSetup s;
    s.object = index;
    s.world = runtime.world(index);
    if (!level.objects[index].odx.empty()) {
        s.definition = level.objects[index].odx;
    }
    return s;
}

void GoalBalloon::update(float dt, bool heroOnStage) {
    landed_ = heroOnStage && !heroOn_;
    heroOn_ = heroOnStage;
    if (landed_) {
        // 0x4883f0: the pump starts at PumpSpeed (+0x960) from the top.
        pumping_ = true;
        speed_ = tuning_.pumpSpeed;
        depth_ = 0;
    }
    if (!pumping_) {
        return;
    }
    speed_ += (-depth_ * tuning_.pumpSpring - speed_ * tuning_.pumpDamper) * dt;
    const float d = depth_ + speed_ * dt;
    if (speed_ < 0 && d < 0) {
        pumping_ = false;
        depth_ = 0;
        return;
    }
    if (speed_ > 0 && d > 1) {
        speed_ = 0;
    }
    depth_ = std::clamp(d, 0.0f, 1.0f);
}

}   // namespace game
