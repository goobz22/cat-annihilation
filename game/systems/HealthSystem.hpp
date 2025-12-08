#pragma once

#include "../../engine/ecs/System.hpp"
#include "../../engine/ecs/ECS.hpp"
#include <functional>

namespace CatGame {

/**
 * Health System
 * Manages health, damage, death, and regeneration for all entities
 */
class HealthSystem : public CatEngine::System {
public:
    using DeathCallback = std::function<void(CatEngine::Entity entity, bool isEnemy)>;
    using DamageCallback = std::function<void(CatEngine::Entity entity, float damage)>;

    explicit HealthSystem(int priority = 10);
    ~HealthSystem() override = default;

    void update(float dt) override;
    const char* getName() const override { return "HealthSystem"; }

    /**
     * Set callback for entity death
     */
    void setOnEntityDeath(DeathCallback callback) { onEntityDeath_ = callback; }

    /**
     * Set callback for damage taken
     */
    void setOnDamageTaken(DamageCallback callback) { onDamageTaken_ = callback; }

    /**
     * Apply damage to entity
     * Returns true if damage was applied
     */
    bool applyDamage(CatEngine::Entity entity, float damage);

    /**
     * Heal entity
     * Returns actual amount healed
     */
    float heal(CatEngine::Entity entity, float amount);

    /**
     * Kill entity instantly
     */
    void kill(CatEngine::Entity entity);

    /**
     * Revive entity with specified health
     */
    void revive(CatEngine::Entity entity, float health);

private:
    /**
     * Update health for single entity
     */
    void updateHealth(CatEngine::Entity entity, float dt);

    /**
     * Handle entity death
     */
    void handleDeath(CatEngine::Entity entity);

    /**
     * Handle enemy death (spawn loot, add score)
     */
    void handleEnemyDeath(CatEngine::Entity entity);

    /**
     * Handle player death (trigger game over)
     */
    void handlePlayerDeath(CatEngine::Entity entity);

    /**
     * Update regeneration
     */
    void updateRegeneration(CatEngine::Entity entity, float dt);

    /**
     * Update invincibility frames
     */
    void updateInvincibility(CatEngine::Entity entity, float dt);

    /**
     * Update death animation timer
     */
    void updateDeathAnimation(CatEngine::Entity entity, float dt);

    // Callbacks
    DeathCallback onEntityDeath_;
    DamageCallback onDamageTaken_;
};

} // namespace CatGame
