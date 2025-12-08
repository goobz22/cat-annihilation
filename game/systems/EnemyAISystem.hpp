#pragma once

#include "../../engine/ecs/System.hpp"
#include "../../engine/ecs/ECS.hpp"
#include "../components/EnemyComponent.hpp"

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
};

} // namespace CatGame
