#pragma once

#include "../../engine/ecs/ECS.hpp"
#include "../../engine/ecs/Entity.hpp"
#include "../../engine/math/Vector.hpp"
#include "../components/EnemyComponent.hpp"

namespace CatGame {

/**
 * Dog Entity Factory
 * Creates enemy dog entities with appropriate components
 */
class DogEntity {
public:
    /**
     * Create a standard dog enemy
     * @param ecs ECS instance
     * @param position Spawn position
     * @param target Target entity (usually player)
     * @return Created entity
     */
    static CatEngine::Entity createDog(CatEngine::ECS* ecs,
                                       const Engine::vec3& position,
                                       CatEngine::Entity target);

    /**
     * Create a big dog enemy (high HP, high damage, slow)
     * @param ecs ECS instance
     * @param position Spawn position
     * @param target Target entity (usually player)
     * @return Created entity
     */
    static CatEngine::Entity createBigDog(CatEngine::ECS* ecs,
                                          const Engine::vec3& position,
                                          CatEngine::Entity target);

    /**
     * Create a fast dog enemy (low HP, low damage, fast)
     * @param ecs ECS instance
     * @param position Spawn position
     * @param target Target entity (usually player)
     * @return Created entity
     */
    static CatEngine::Entity createFastDog(CatEngine::ECS* ecs,
                                           const Engine::vec3& position,
                                           CatEngine::Entity target);

    /**
     * Create a boss dog enemy
     * @param ecs ECS instance
     * @param position Spawn position
     * @param target Target entity (usually player)
     * @return Created entity
     */
    static CatEngine::Entity createBossDog(CatEngine::ECS* ecs,
                                           const Engine::vec3& position,
                                           CatEngine::Entity target);

    /**
     * Create dog of specified type
     * @param ecs ECS instance
     * @param type Enemy type
     * @param position Spawn position
     * @param target Target entity (usually player)
     * @param healthMultiplier Health scaling multiplier (for wave progression)
     * @return Created entity
     */
    static CatEngine::Entity create(CatEngine::ECS* ecs,
                                    EnemyType type,
                                    const Engine::vec3& position,
                                    CatEngine::Entity target,
                                    float healthMultiplier = 1.0f);

private:
    /**
     * Get base stats for enemy type
     */
    struct EnemyStats {
        float health;
        float moveSpeed;
        float attackDamage;
        float attackRange;
        float aggroRange;
        int scoreValue;
        Engine::vec3 scale;
    };

    static EnemyStats getStatsForType(EnemyType type);
};

} // namespace CatGame
