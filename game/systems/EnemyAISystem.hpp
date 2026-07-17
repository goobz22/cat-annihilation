#pragma once

#include "../../engine/ecs/System.hpp"
#include "../../engine/ecs/ECS.hpp"
#include "../../engine/math/Vector.hpp"
#include "../components/EnemyComponent.hpp"
#include <cmath>

// Engine::Transform is only referenced (never stored) by the shield helpers
// below, so a forward declaration keeps the heavier Transform.hpp out of this
// header; the .cpp pulls in the full definition where it dereferences it.
namespace Engine { class Transform; }

namespace CatGame {

/**
 * Enemy AI System
 * Handles enemy behavior, state machine, and AI logic
 */
class EnemyAISystem : public CatEngine::System {
public:
    explicit EnemyAISystem(int priority = 100);
    ~EnemyAISystem() override = default;

    void update(float dt) override;
    const char* getName() const override { return "EnemyAISystem"; }

    /**
     * Boid separation contribution from ONE neighbor (web parity).
     *
     * Pure planar (X/Z) math, defined inline + exposed static so it is
     * unit-testable without an ECS: returns the away-from-`otherPos` force
     * applied to the enemy at `selfPos`, with the web's linear falloff
     * strength = (radius-dist)/radius scaled by `force`. Zero when the neighbor
     * is outside `radius` or exactly coincident (dist == 0 — the web guards
     * `otherDistance > 0` to avoid a divide-by-zero). Mirrors
     * LocalEnemySystem.tsx:200-205.
     */
    static Engine::vec3 separationContribution(const Engine::vec3& selfPos,
                                               const Engine::vec3& otherPos,
                                               float radius, float force) {
        const float dx = selfPos.x - otherPos.x;
        const float dz = selfPos.z - otherPos.z;
        const float dist = std::sqrt(dx * dx + dz * dz);
        if (dist <= 0.0f || dist >= radius) {
            return Engine::vec3(0.0f, 0.0f, 0.0f);
        }
        const float strength = (radius - dist) / radius;
        return Engine::vec3((dx / dist) * strength * force,
                            0.0f,
                            (dz / dist) * strength * force);
    }

private:
    /**
     * Update individual enemy AI
     */
    void updateEnemyAI(CatEngine::Entity entity, EnemyComponent& enemy, float dt);

    /**
     * State-specific update functions
     */
    void updateIdleState(CatEngine::Entity entity, EnemyComponent& enemy, float dt);
    void updateChasingState(CatEngine::Entity entity, EnemyComponent& enemy, float dt);
    void updateAttackingState(CatEngine::Entity entity, EnemyComponent& enemy, float dt);
    void updateDeadState(CatEngine::Entity entity, EnemyComponent& enemy, float dt);

    /**
     * Transition to a new state
     */
    void transitionToState(EnemyComponent& enemy, AIState newState);

    /**
     * Check if target is in range
     */
    bool isTargetInRange(const Engine::vec3& position, const Engine::vec3& targetPos, float range) const;

    /**
     * Get distance to target
     */
    float getDistanceToTarget(const Engine::vec3& position, const Engine::vec3& targetPos) const;

    /**
     * Move enemy toward target
     */
    void moveTowardTarget(CatEngine::Entity entity, const Engine::vec3& targetPos, float speed, float dt);

    /**
     * Sum the boid separation force over all OTHER live enemies within
     * kEnemySeparationRadius of `selfPos` (web parity). O(N) per call over the
     * enemy set; the caller adds it to the seek velocity before integrating.
     */
    Engine::vec3 computeSeparationForce(CatEngine::Entity self, const Engine::vec3& selfPos) const;

    /**
     * Rotate enemy to face target
     */
    void faceTarget(CatEngine::Entity entity, const Engine::vec3& targetPos);

    /**
     * Web-parity shield physical barrier (effect 1 of the web shield).
     *
     * When the player is raising the shield, a sphere sits 1.2 units ahead of
     * the player (radius 0.9). This snaps a penetrating enemy back to the
     * sphere surface, so chasing dogs pile up against the shield instead of
     * walking through the player. Called from the chase state AFTER the enemy
     * has advanced, mirroring the web's post-move position validation
     * (src/components/game/LocalEnemySystem.tsx:296-338). The player's
     * raised-shield flag and facing yaw are read off the target's
     * CombatComponent; enemy AI never writes them.
     */
    void applyShieldBarrier(CatEngine::Entity enemyEntity, const EnemyComponent& enemy);

    /**
     * Web-parity shield front-arc damage negation (effect 2 of the web shield).
     *
     * Returns true when the player is raising the shield AND this attacking
     * enemy stands inside the front arc of the player's facing, in which case
     * the attack deals no damage (src/components/game/LocalEnemySystem.tsx:
     * 356-375). The caller still puts the enemy on attack cooldown so a blocked
     * swing does not retry every frame.
     */
    bool isAttackNegatedByShield(const EnemyComponent& enemy,
                                 const Engine::Transform& enemyTransform,
                                 const Engine::Transform& playerTransform) const;
};

} // namespace CatGame
