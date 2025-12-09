#include "ProjectileEntity.hpp"
#include "../../engine/math/Transform.hpp"
#include "../../engine/math/Quaternion.hpp"

namespace CatGame {

CatEngine::Entity ProjectileEntity::createSpell(CatEngine::ECS* ecs,
                                                const Engine::vec3& position,
                                                const Engine::vec3& direction,
                                                CatEngine::Entity owner) {
    return create(ecs, ProjectileType::Spell, position, direction, owner);
}

CatEngine::Entity ProjectileEntity::createArrow(CatEngine::ECS* ecs,
                                                const Engine::vec3& position,
                                                const Engine::vec3& direction,
                                                CatEngine::Entity owner) {
    return create(ecs, ProjectileType::Arrow, position, direction, owner);
}

CatEngine::Entity ProjectileEntity::createEnemyAttack(CatEngine::ECS* ecs,
                                                      const Engine::vec3& position,
                                                      const Engine::vec3& direction,
                                                      CatEngine::Entity owner) {
    return create(ecs, ProjectileType::EnemyAttack, position, direction, owner);
}

CatEngine::Entity ProjectileEntity::createHoming(CatEngine::ECS* ecs,
                                                 ProjectileType type,
                                                 const Engine::vec3& position,
                                                 const Engine::vec3& direction,
                                                 CatEngine::Entity owner,
                                                 CatEngine::Entity target,
                                                 float homingStrength) {
    // Create base projectile
    auto entity = create(ecs, type, position, direction, owner);

    // Add homing behavior
    auto* projectile = ecs->getComponent<ProjectileComponent>(entity);
    if (projectile) {
        projectile->isHoming = true;
        projectile->homingTarget = target;
        projectile->homingStrength = homingStrength;
    }

    return entity;
}

CatEngine::Entity ProjectileEntity::create(CatEngine::ECS* ecs,
                                           ProjectileType type,
                                           const Engine::vec3& position,
                                           const Engine::vec3& direction,
                                           CatEngine::Entity owner) {
    if (!ecs) {
        return CatEngine::NULL_ENTITY;
    }

    // Get stats for this projectile type
    ProjectileStats stats = getStatsForType(type);

    // Normalize direction
    Engine::vec3 normalizedDir = direction.normalized();
    if (normalizedDir.lengthSquared() < 0.01F) {
        // If direction is zero, use forward
        normalizedDir = Engine::vec3(0.0F, 0.0F, -1.0F);
    }

    // Calculate velocity
    Engine::vec3 velocity = normalizedDir * stats.speed;

    // Create entity
    auto entity = ecs->createEntity();

    // Add Transform component
    Engine::Transform transform;
    transform.position = position;
    transform.scale = stats.scale;

    // Rotate to face movement direction
    transform.lookAt(position + normalizedDir, Engine::vec3(0.0F, 1.0F, 0.0F));

    ecs->addComponent(entity, transform);

    // Add Projectile component
    ProjectileComponent projectile(type, velocity, owner);
    projectile.damage = stats.damage;
    projectile.lifetime = stats.lifetime;
    projectile.lifetimeRemaining = stats.lifetime;
    projectile.radius = stats.radius;
    ecs->addComponent(entity, projectile);

    // Note: Renderer component is added automatically by the ProjectileRenderer system
    // based on ProjectileType. Each type has different visuals:
    // - Spell:       Glowing orb with particle trail, emissive material
    // - Arrow:       Arrow mesh with motion blur shader
    // - EnemyAttack: Red projectile with aggressive particle effect
    //
    // Particle effects are spawned from the projectile's position each frame
    // and are cleaned up when the projectile is destroyed.

    return entity;
}

ProjectileEntity::ProjectileStats ProjectileEntity::getStatsForType(ProjectileType type) {
    ProjectileStats stats;

    switch (type) {
        case ProjectileType::Spell:
            stats.damage = 20.0F;
            stats.speed = 30.0F;
            stats.lifetime = 3.0F;
            stats.radius = 0.3F;
            stats.scale = Engine::vec3(0.3F, 0.3F, 0.3F);
            break;

        case ProjectileType::Arrow:
            stats.damage = 30.0F;
            stats.speed = 40.0F;
            stats.lifetime = 2.0F;
            stats.radius = 0.2F;
            stats.scale = Engine::vec3(0.2F, 0.2F, 0.5F); // Longer for arrow shape
            break;

        case ProjectileType::EnemyAttack:
            stats.damage = 15.0F;
            stats.speed = 20.0F;
            stats.lifetime = 1.0F;
            stats.radius = 0.25F;
            stats.scale = Engine::vec3(0.25F, 0.25F, 0.25F);
            break;
    }

    return stats;
}

} // namespace CatGame
