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
    if (normalizedDir.lengthSquared() < 0.01f) {
        // If direction is zero, use forward
        normalizedDir = Engine::vec3(0.0f, 0.0f, -1.0f);
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
    transform.lookAt(position + normalizedDir, Engine::vec3(0.0f, 1.0f, 0.0f));

    ecs->addComponent(entity, transform);

    // Add Projectile component
    ProjectileComponent projectile(type, velocity, owner);
    projectile.damage = stats.damage;
    projectile.lifetime = stats.lifetime;
    projectile.lifetimeRemaining = stats.lifetime;
    projectile.radius = stats.radius;
    ecs->addComponent(entity, projectile);

    // TODO: Add Renderer component for visual representation
    // This would specify the mesh, material, and effects for the projectile
    // Different visuals for Spell, Arrow, and EnemyAttack

    return entity;
}

ProjectileEntity::ProjectileStats ProjectileEntity::getStatsForType(ProjectileType type) {
    ProjectileStats stats;

    switch (type) {
        case ProjectileType::Spell:
            stats.damage = 20.0f;
            stats.speed = 30.0f;
            stats.lifetime = 3.0f;
            stats.radius = 0.3f;
            stats.scale = Engine::vec3(0.3f, 0.3f, 0.3f);
            break;

        case ProjectileType::Arrow:
            stats.damage = 30.0f;
            stats.speed = 40.0f;
            stats.lifetime = 2.0f;
            stats.radius = 0.2f;
            stats.scale = Engine::vec3(0.2f, 0.2f, 0.5f); // Longer for arrow shape
            break;

        case ProjectileType::EnemyAttack:
            stats.damage = 15.0f;
            stats.speed = 20.0f;
            stats.lifetime = 1.0f;
            stats.radius = 0.25f;
            stats.scale = Engine::vec3(0.25f, 0.25f, 0.25f);
            break;
    }

    return stats;
}

} // namespace CatGame
