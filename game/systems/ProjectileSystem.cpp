#include "ProjectileSystem.hpp"
#include "../components/HealthComponent.hpp"
#include "../components/EnemyComponent.hpp"
#include "../../engine/math/Transform.hpp"
#include "../../engine/math/Vector.hpp"
#include <vector>

namespace CatGame {

ProjectileSystem::ProjectileSystem(int priority)
    : System(priority)
{}

void ProjectileSystem::update(float dt) {
    if (!ecs_) return;

    // Query all projectiles
    auto query = ecs_->query<ProjectileComponent, Engine::Transform>();

    // Collect projectiles to destroy (can't destroy during iteration)
    std::vector<CatEngine::Entity> toDestroy;

    for (auto [entity, projectile, transform] : query.view()) {
        updateProjectile(entity, projectile, dt);

        // Check if projectile should be destroyed
        if (projectile.isExpired()) {
            toDestroy.push_back(entity);
        }
    }

    // Destroy expired projectiles
    for (auto entity : toDestroy) {
        ecs_->destroyEntity(entity);
    }
}

void ProjectileSystem::updateProjectile(CatEngine::Entity entity, ProjectileComponent& projectile, float dt) {
    auto* transform = ecs_->getComponent<Engine::Transform>(entity);
    if (!transform) return;

    // Update lifetime
    projectile.lifetimeRemaining -= dt;
    if (projectile.lifetimeRemaining <= 0.0f) {
        return; // Will be destroyed in main update loop
    }

    // Update homing behavior
    if (projectile.isHoming && ecs_->isAlive(projectile.homingTarget)) {
        updateHoming(projectile, transform->position, dt);
    }

    // Move projectile
    transform->position += projectile.velocity * dt;

    // Check collisions based on projectile type
    bool hit = false;

    if (projectile.type == ProjectileType::EnemyAttack) {
        // Enemy projectile - check collision with player
        hit = checkPlayerCollision(entity, projectile, transform->position);
    } else {
        // Player projectile - check collision with enemies
        hit = checkEnemyCollision(entity, projectile, transform->position);
    }

    if (hit) {
        projectile.hasHit = true;
        spawnHitEffect(transform->position, projectile.type);
    }
}

void ProjectileSystem::updateHoming(ProjectileComponent& projectile, const Engine::vec3& position, float dt) {
    auto* targetTransform = ecs_->getComponent<Engine::Transform>(projectile.homingTarget);
    if (!targetTransform) {
        projectile.isHoming = false;
        return;
    }

    // Calculate direction to target
    Engine::vec3 toTarget = (targetTransform->position - position).normalized();

    // Blend current velocity direction with target direction
    Engine::vec3 currentDir = projectile.velocity.normalized();
    float speed = projectile.velocity.length();

    // Lerp toward target direction
    Engine::vec3 newDir = Engine::vec3::lerp(currentDir, toTarget, projectile.homingStrength * dt);
    newDir.normalize();

    // Update velocity
    projectile.velocity = newDir * speed;
}

bool ProjectileSystem::checkEnemyCollision(CatEngine::Entity projectileEntity,
                                           const ProjectileComponent& projectile,
                                           const Engine::vec3& position) {
    // Don't collide with owner
    auto query = ecs_->query<EnemyComponent, Engine::Transform, HealthComponent>();

    for (auto [enemyEntity, enemy, transform, health] : query.view()) {
        // Skip if this is the owner
        if (enemyEntity == projectile.owner) {
            continue;
        }

        // Skip if enemy is already dead
        if (!health.isAlive()) {
            continue;
        }

        // Check collision
        float enemyRadius = 0.5f; // Enemy collision radius
        if (checkSphereCollision(position, projectile.radius, transform.position, enemyRadius)) {
            // Apply damage
            applyDamage(enemyEntity, projectile.damage);
            return true;
        }
    }

    return false;
}

bool ProjectileSystem::checkPlayerCollision(CatEngine::Entity projectileEntity,
                                             const ProjectileComponent& projectile,
                                             const Engine::vec3& position) {
    // Find player entity (assumes there's only one player)
    // You might want to tag the player entity or store a reference
    auto query = ecs_->query<Engine::Transform, HealthComponent>();

    for (auto [entity, transform, health] : query.view()) {
        // Skip if this is the owner
        if (entity == projectile.owner) {
            continue;
        }

        // Skip if entity is an enemy (has EnemyComponent)
        if (ecs_->hasComponent<EnemyComponent>(entity)) {
            continue;
        }

        // Skip if already dead
        if (!health.isAlive()) {
            continue;
        }

        // Check collision
        float playerRadius = 0.5f; // Player collision radius
        if (checkSphereCollision(position, projectile.radius, transform.position, playerRadius)) {
            // Apply damage
            applyDamage(entity, projectile.damage);
            return true;
        }
    }

    return false;
}

void ProjectileSystem::applyDamage(CatEngine::Entity target, float damage) {
    auto* health = ecs_->getComponent<HealthComponent>(target);
    if (!health) return;

    // Skip if invincible
    if (health->isInvincible()) {
        return;
    }

    // Apply damage
    health->currentHealth -= damage;
    health->timeSinceLastDamage = 0.0f;

    // Clamp health
    if (health->currentHealth < 0.0f) {
        health->currentHealth = 0.0f;
    }
}

void ProjectileSystem::spawnHitEffect(const Engine::vec3& position, ProjectileType type) {
    // TODO: Spawn particle effect or visual feedback
    // This would create a particle system entity at the hit position
    // For now, this is a placeholder
}

bool ProjectileSystem::checkSphereCollision(const Engine::vec3& pos1, float radius1,
                                             const Engine::vec3& pos2, float radius2) const {
    float distanceSquared = (pos2 - pos1).lengthSquared();
    float radiusSum = radius1 + radius2;
    return distanceSquared <= (radiusSum * radiusSum);
}

} // namespace CatGame
