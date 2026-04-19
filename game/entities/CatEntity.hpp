#pragma once

#include "../../engine/ecs/Entity.hpp"
#include "../../engine/ecs/ECS.hpp"
#include "../../engine/math/Vector.hpp"

namespace CatGame {

/**
 * CatEntity factory - Creates the player cat entity
 *
 * The cat entity is the player character with:
 * - Transform component for position/rotation
 * - Health component (100 HP)
 * - Movement component (speed 10)
 * - Combat component (sword, 25 damage)
 *
 * This factory function sets up all required components
 * and configures them with appropriate initial values.
 */
class CatEntity {
public:
    /**
     * Create a player cat entity with default configuration
     * @param ecs ECS system to create entity in
     * @param position Initial spawn position
     * @return Created entity
     */
    static CatEngine::Entity create(CatEngine::ECS& ecs, const Engine::vec3& position = Engine::vec3(0.0f, 0.0f, 0.0f));

    /**
     * Create a customized player cat entity
     * @param ecs ECS system to create entity in
     * @param position Initial spawn position
     * @param maxHealth Maximum health points
     * @param moveSpeed Movement speed
     * @param attackDamage Base attack damage
     * @return Created entity
     */
    static CatEngine::Entity createCustom(
        CatEngine::ECS& ecs,
        const Engine::vec3& position,
        float maxHealth,
        float moveSpeed,
        float attackDamage
    );

    /**
     * Load cat model from disk and attach it to the entity as a MeshComponent.
     * @param ecs ECS system
     * @param entity Entity to load model for
     * @param modelPath Path to cat model file (glTF or GLB)
     * @return true if the model was loaded and attached
     */
    static bool loadModel(CatEngine::ECS& ecs, CatEngine::Entity entity, const char* modelPath);

    /**
     * Build an Animator for the cat's skeleton and register clips found in the model.
     * Requires loadModel() to have succeeded — this method reads the MeshComponent
     * attached during loading.
     * @param ecs ECS system
     * @param entity Entity whose MeshComponent supplies the skeleton
     * @return true if an animator was attached (even with zero clips)
     */
    static bool configureAnimations(CatEngine::ECS& ecs, CatEngine::Entity entity);

private:
    // Default values
    static constexpr float DEFAULT_HEALTH = 100.0f;
    static constexpr float DEFAULT_MOVE_SPEED = 10.0f;
    static constexpr float DEFAULT_ATTACK_DAMAGE = 25.0f;
    static constexpr float DEFAULT_ATTACK_SPEED = 1.5f;
    static constexpr float DEFAULT_ATTACK_RANGE = 3.0f;
};

} // namespace CatGame
