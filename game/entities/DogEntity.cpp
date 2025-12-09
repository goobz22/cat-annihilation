#include "DogEntity.hpp"
#include "../components/HealthComponent.hpp"
#include "../components/MovementComponent.hpp"
#include "../../engine/math/Transform.hpp"
#include "../../engine/math/Quaternion.hpp"

namespace CatGame {

CatEngine::Entity DogEntity::createDog(CatEngine::ECS* ecs,
                                       const Engine::vec3& position,
                                       CatEngine::Entity target) {
    return create(ecs, EnemyType::Dog, position, target);
}

CatEngine::Entity DogEntity::createBigDog(CatEngine::ECS* ecs,
                                          const Engine::vec3& position,
                                          CatEngine::Entity target) {
    return create(ecs, EnemyType::BigDog, position, target);
}

CatEngine::Entity DogEntity::createFastDog(CatEngine::ECS* ecs,
                                           const Engine::vec3& position,
                                           CatEngine::Entity target) {
    return create(ecs, EnemyType::FastDog, position, target);
}

CatEngine::Entity DogEntity::createBossDog(CatEngine::ECS* ecs,
                                           const Engine::vec3& position,
                                           CatEngine::Entity target) {
    return create(ecs, EnemyType::BossDog, position, target);
}

CatEngine::Entity DogEntity::create(CatEngine::ECS* ecs,
                                    EnemyType type,
                                    const Engine::vec3& position,
                                    CatEngine::Entity target,
                                    float healthMultiplier) {
    if (!ecs) {
        return CatEngine::NULL_ENTITY;
    }

    // Get stats for this enemy type
    EnemyStats stats = getStatsForType(type);

    // Create entity
    auto entity = ecs->createEntity();

    // Add Transform component
    Engine::Transform transform;
    transform.position = position;
    transform.rotation = Engine::Quaternion::identity();
    transform.scale = stats.scale;
    ecs->addComponent(entity, transform);

    // Add Enemy component
    EnemyComponent enemy(type, target);
    enemy.moveSpeed = stats.moveSpeed;
    enemy.attackDamage = stats.attackDamage;
    enemy.attackRange = stats.attackRange;
    enemy.aggroRange = stats.aggroRange;
    enemy.scoreValue = stats.scoreValue;
    ecs->addComponent(entity, enemy);

    // Add Health component
    float finalHealth = stats.health * healthMultiplier;
    HealthComponent health(finalHealth);
    ecs->addComponent(entity, health);

    // Add Movement component
    MovementComponent movement(stats.moveSpeed);
    ecs->addComponent(entity, movement);

    // Note: Renderer component is added automatically by the EntityRenderer system
    // when this entity is registered for rendering. The renderer selects the
    // appropriate dog model based on EnemyType:
    // - Dog:     "assets/models/enemies/dog_normal.gltf"
    // - BigDog:  "assets/models/enemies/dog_big.gltf"
    // - FastDog: "assets/models/enemies/dog_fast.gltf"
    // - BossDog: "assets/models/enemies/dog_boss.gltf"
    //
    // Materials and animations are configured per-model in the asset manifest.

    return entity;
}

DogEntity::EnemyStats DogEntity::getStatsForType(EnemyType type) {
    EnemyStats stats;

    switch (type) {
        case EnemyType::Dog:
            stats.health = 50.0F;
            stats.moveSpeed = 6.0F;
            stats.attackDamage = 10.0F;
            stats.attackRange = 2.0F;
            stats.aggroRange = 15.0F;
            stats.scoreValue = 10;
            stats.scale = Engine::vec3(1.0F, 1.0F, 1.0F);
            break;

        case EnemyType::BigDog:
            stats.health = 100.0F;         // 2x health
            stats.moveSpeed = 4.2F;        // 0.7x speed
            stats.attackDamage = 15.0F;    // 1.5x damage
            stats.attackRange = 2.5F;      // Slightly larger range
            stats.aggroRange = 15.0F;
            stats.scoreValue = 25;
            stats.scale = Engine::vec3(1.5F, 1.5F, 1.5F); // Bigger visually
            break;

        case EnemyType::FastDog:
            stats.health = 25.0F;          // 0.5x health
            stats.moveSpeed = 9.0F;        // 1.5x speed
            stats.attackDamage = 7.5F;     // 0.75x damage
            stats.attackRange = 1.8F;      // Slightly smaller range
            stats.aggroRange = 18.0F;      // Spots player from farther
            stats.scoreValue = 15;
            stats.scale = Engine::vec3(0.8F, 0.8F, 0.8F); // Smaller visually
            break;

        case EnemyType::BossDog:
            stats.health = 300.0F;         // Boss health
            stats.moveSpeed = 5.0F;        // Moderate speed
            stats.attackDamage = 25.0F;    // High damage
            stats.attackRange = 3.0F;      // Larger range
            stats.aggroRange = 25.0F;      // Large aggro range
            stats.scoreValue = 100;
            stats.scale = Engine::vec3(2.0F, 2.0F, 2.0F); // Much bigger
            break;
    }

    return stats;
}

} // namespace CatGame
