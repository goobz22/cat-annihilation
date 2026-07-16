#pragma once

#include "../../engine/ecs/System.hpp"
#include "../../engine/ecs/ECS.hpp"
#include "../../engine/math/Vector.hpp"
#include "../components/EnemyComponent.hpp"

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
