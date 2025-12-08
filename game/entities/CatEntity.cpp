#include "CatEntity.hpp"
#include "../components/GameComponents.hpp"
#include "../../engine/math/Transform.hpp"

namespace CatGame {

CatEngine::Entity CatEntity::create(CatEngine::ECS& ecs, const Engine::vec3& position) {
    return createCustom(
        ecs,
        position,
        DEFAULT_HEALTH,
        DEFAULT_MOVE_SPEED,
        DEFAULT_ATTACK_DAMAGE
    );
}

CatEngine::Entity CatEntity::createCustom(
    CatEngine::ECS& ecs,
    const Engine::vec3& position,
    float maxHealth,
    float moveSpeed,
    float attackDamage
) {
    // Create entity
    CatEngine::Entity entity = ecs.createEntity();

    // Add Transform component
    Engine::Transform transform;
    transform.position = position;
    transform.rotation = Engine::Quaternion::identity();
    transform.scale = Engine::vec3(1.0f, 1.0f, 1.0f);
    ecs.addComponent(entity, transform);

    // Add Health component
    HealthComponent health;
    health.maxHealth = maxHealth;
    health.currentHealth = maxHealth;
    health.invincibilityDuration = 0.5f;

    // Set up death callback
    health.onDeath = [entity]() {
        // TODO: Handle player death (game over, respawn, etc.)
        // For now, just log or trigger game state change
    };

    // Set up damage callback
    health.onDamage = [](float damage) {
        // TODO: Play damage sound, visual effect, etc.
    };

    ecs.addComponent(entity, health);

    // Add Movement component
    MovementComponent movement;
    movement.moveSpeed = moveSpeed;
    movement.maxSpeed = moveSpeed * 2.0f;
    movement.acceleration = 50.0f;
    movement.deceleration = 30.0f;
    movement.jumpForce = 15.0f;
    movement.gravityMultiplier = 2.0f;
    movement.isGrounded = true;
    movement.canMove = true;
    movement.canJump = true;

    ecs.addComponent(entity, movement);

    // Add Combat component
    CombatComponent combat;
    combat.attackDamage = attackDamage;
    combat.attackSpeed = DEFAULT_ATTACK_SPEED;
    combat.attackRange = DEFAULT_ATTACK_RANGE;
    combat.equippedWeapon = WeaponType::Sword;
    combat.damageMultiplier = 1.0f;
    combat.attackSpeedMultiplier = 1.0f;
    combat.canAttack = true;

    ecs.addComponent(entity, combat);

    // TODO: Add Renderer component when rendering system is ready
    // This would include:
    // - Mesh reference (cat model)
    // - Material/texture
    // - Animation controller

    return entity;
}

void CatEntity::loadModel(CatEngine::ECS& ecs, CatEngine::Entity entity, const char* modelPath) {
    // TODO: Implement model loading when asset system is available
    // This function will:
    // 1. Load cat model from file
    // 2. Create/update renderer component
    // 3. Set up mesh and materials
    // 4. Configure LOD levels if needed

    // Placeholder implementation
    (void)ecs;
    (void)entity;
    (void)modelPath;
}

void CatEntity::configureAnimations(CatEngine::ECS& ecs, CatEngine::Entity entity) {
    // TODO: Implement animation setup when animation system is available
    // This function will configure:
    // - Idle animation
    // - Walk/run animations
    // - Jump animation
    // - Attack animations (sword swing, spell cast, bow draw)
    // - Hit/damage reaction animation
    // - Death animation

    // Placeholder implementation
    (void)ecs;
    (void)entity;
}

} // namespace CatGame
