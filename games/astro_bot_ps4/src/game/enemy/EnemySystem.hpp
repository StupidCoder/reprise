// The level's enemies: creates the classes ported so far from the placements and updates them
// once per game step after the level objects (the original's update order between objects is not
// known; .analysis/docs/enemies.md).
#pragma once

#include "data/GameFiles.hpp"
#include "data/ObjectDefinition.hpp"
#include "game/LevelSchema.hpp"
#include "data/PhysicsFile.hpp"
#include "game/enemy/BasicEnemy.hpp"

#include <memory>
#include <string>
#include <vector>

namespace beacon::game::enemy {

class EnemySystem {
public:
    // Creates the enemies of `level` (classes: enemy_standard); with `physics`, each gets its root
    // dynamics body there (BasicEnemy::attachBody).
    void load(const Level& level, data::ObjectDefinitionLibrary& defs, const data::GameFiles& files,
              const LevelSchemaLibrary& schemas, physics::PhysicsWorld* physics = nullptr);
    void update(const EnemyWorld& world);
    // The hero's punch: a hand sphere swept from `from` to `to` (0x674c80: the module's sphere
    // query, masks self 0x20000 / target 0x91041a0f without the hero's bits) meets its nearest
    // body; when that is an enemy's (once per punch `window`), the enemy gets a punch hit pushing
    // away from `heroFeet`. A wall in between takes the punch. Returns the number of enemies hit
    // (0 or 1).
    int punch(const math::Vec3& from, const math::Vec3& to, float radius, int window, const math::Vec3& heroFeet);

    // A hit on the enemy built from `placement` (its body met by a query, e.g. the hover laser,
    // 0x4cd6b0: type 4); false when no live enemy belongs to it.
    bool hitPlacement(std::size_t placement, const EnemyHit& hit);

    std::size_t size() const { return enemies_.size(); }
    BasicEnemy& operator[](std::size_t i) { return *enemies_[i]; }
    const BasicEnemy& operator[](std::size_t i) const { return *enemies_[i]; }
    // The enemy for a placement, or null.
    const BasicEnemy* forPlacement(std::size_t placement) const;
    // The object definition enemy i is built from (model, skeleton, clips, physics).
    const std::string& definition(std::size_t i) const { return definitions_[i]; }

private:
    std::vector<std::unique_ptr<BasicEnemy>> enemies_;
    std::vector<float> contactRadius_;
    std::vector<std::string> definitions_;
    std::vector<int> punchedBy_;   // the last punch window that hit each enemy
    std::vector<std::shared_ptr<const data::PhysicsFile>> physicsFiles_;   // kept for the bodies' shapes
    physics::PhysicsWorld* physics_ = nullptr;
};

}   // namespace beacon::game::enemy
