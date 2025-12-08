#pragma once

#include "../../engine/ecs/ECS.hpp"
#include "../../engine/ecs/Entity.hpp"
#include "../../engine/math/Vector.hpp"
#include "../components/ProjectileComponent.hpp"

namespace CatGame {

/**
 * Projectile Entity Factory
 * Creates projectile entities with appropriate components
 */
class ProjectileEntity {
public:
    /**
     * Create a spell projectile (player magic attack)
     * @param ecs ECS instance
     * @param position Starting position
     * @param direction Movement direction (will be normalized)
     * @param owner Entity that fired this projectile
     * @return Created entity
     */
    static CatEngine::Entity createSpell(CatEngine::ECS* ecs,
                                         const Engine::vec3& position,
                                         const Engine::vec3& direction,
                                         CatEngine::Entity owner);

    /**
     * Create an arrow projectile (player ranged attack)
     * @param ecs ECS instance
     * @param position Starting position
     * @param direction Movement direction (will be normalized)
     * @param owner Entity that fired this projectile
     * @return Created entity
     */
    static CatEngine::Entity createArrow(CatEngine::ECS* ecs,
                                         const Engine::vec3& position,
                                         const Engine::vec3& direction,
                                         CatEngine::Entity owner);

    /**
     * Create an enemy attack projectile
     * @param ecs ECS instance
     * @param position Starting position
     * @param direction Movement direction (will be normalized)
     * @param owner Entity that fired this projectile
     * @return Created entity
     */
    static CatEngine::Entity createEnemyAttack(CatEngine::ECS* ecs,
                                               const Engine::vec3& position,
                                               const Engine::vec3& direction,
                                               CatEngine::Entity owner);

    /**
     * Create a homing projectile
     * @param ecs ECS instance
     * @param type Projectile type
     * @param position Starting position
     * @param direction Initial movement direction
     * @param owner Entity that fired this projectile
     * @param target Target to home in on
     * @param homingStrength How strongly to track target (0-1+)
     * @return Created entity
     */
    static CatEngine::Entity createHoming(CatEngine::ECS* ecs,
                                          ProjectileType type,
                                          const Engine::vec3& position,
                                          const Engine::vec3& direction,
                                          CatEngine::Entity owner,
                                          CatEngine::Entity target,
                                          float homingStrength = 5.0f);

    /**
     * Create projectile of specified type
     * @param ecs ECS instance
     * @param type Projectile type
     * @param position Starting position
     * @param direction Movement direction (will be normalized)
     * @param owner Entity that fired this projectile
     * @return Created entity
     */
    static CatEngine::Entity create(CatEngine::ECS* ecs,
                                    ProjectileType type,
                                    const Engine::vec3& position,
                                    const Engine::vec3& direction,
                                    CatEngine::Entity owner);

private:
    /**
     * Get projectile stats for type
     */
    struct ProjectileStats {
        float damage;
        float speed;
        float lifetime;
        float radius;
        Engine::vec3 scale;
    };

    static ProjectileStats getStatsForType(ProjectileType type);
};

} // namespace CatGame
