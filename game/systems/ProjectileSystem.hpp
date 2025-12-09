#pragma once

#include "../../engine/ecs/System.hpp"
#include "../../engine/ecs/ECS.hpp"
#include "../components/ProjectileComponent.hpp"
#include <functional>

namespace CatGame {

/**
 * Projectile System
 * Handles projectile movement, collision detection, and lifetime
 */
class ProjectileSystem : public CatEngine::System {
public:
    using HitEffectCallback = std::function<void(const Engine::vec3& position, ProjectileType type)>;

    explicit ProjectileSystem(int priority = 50);
    ~ProjectileSystem() override = default;

    void update(float dt) override;
    const char* getName() const override { return "ProjectileSystem"; }

    /**
     * Set callback for spawning hit effects
     * Called when a projectile hits a target
     */
    void setOnHitEffect(const HitEffectCallback& callback) { onHitEffect_ = callback; }

private:
    // Callback for hit effects
    HitEffectCallback onHitEffect_;

    /**
     * Update individual projectile
     */
    void updateProjectile(CatEngine::Entity entity, ProjectileComponent& projectile, float dt);

    /**
     * Update homing behavior
     */
    void updateHoming(ProjectileComponent& projectile, const Engine::vec3& position, float dt);

    /**
     * Check collision with enemies (for player projectiles)
     */
    bool checkEnemyCollision(CatEngine::Entity projectileEntity, const ProjectileComponent& projectile, const Engine::vec3& position);

    /**
     * Check collision with player (for enemy projectiles)
     */
    bool checkPlayerCollision(CatEngine::Entity projectileEntity, const ProjectileComponent& projectile, const Engine::vec3& position);

    /**
     * Apply damage to entity
     */
    void applyDamage(CatEngine::Entity target, float damage);

    /**
     * Spawn hit effect at position
     */
    void spawnHitEffect(const Engine::vec3& position, ProjectileType type);

    /**
     * Check sphere-sphere collision
     */
    bool checkSphereCollision(const Engine::vec3& pos1, float radius1, const Engine::vec3& pos2, float radius2) const;
};

} // namespace CatGame
