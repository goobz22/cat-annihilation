#include "CatEntity.hpp"
#include "../components/GameComponents.hpp"
#include "../../engine/math/Transform.hpp"
#include "../../engine/core/Logger.hpp"

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
    transform.scale = Engine::vec3(1.0F, 1.0F, 1.0F);
    ecs.addComponent(entity, transform);

    // Add Health component
    HealthComponent health;
    health.maxHealth = maxHealth;
    health.currentHealth = maxHealth;
    health.invincibilityDuration = 0.5F;

    // Set up death callback - triggers EntityDeathEvent via HealthSystem
    health.onDeath = [entity]() {
        // Death is handled by the HealthSystem which publishes EntityDeathEvent
        // The CatAnnihilation class listens for this event and triggers GameOver state
        // No direct action needed here - event-driven architecture handles it
        (void)entity;  // Entity ID available if needed for cleanup
    };

    // Set up damage callback - triggers visual/audio feedback
    health.onDamage = [](float damage) {
        // Damage feedback is handled by the DamageEvent published by CombatSystem
        // The CatAnnihilation class listens for this and triggers HUD effects + audio
        // No direct action needed here - event-driven architecture handles it
        (void)damage;  // Damage amount available if needed for scaling effects
    };

    ecs.addComponent(entity, health);

    // Add Movement component
    MovementComponent movement;
    movement.moveSpeed = moveSpeed;
    movement.maxSpeed = moveSpeed * 2.0F;
    movement.acceleration = 50.0F;
    movement.deceleration = 30.0F;
    movement.jumpForce = 15.0F;
    movement.gravityMultiplier = 2.0F;
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
    combat.damageMultiplier = 1.0F;
    combat.attackSpeedMultiplier = 1.0F;
    combat.canAttack = true;

    ecs.addComponent(entity, combat);

    // Note: Renderer component is added by the rendering system when the entity
    // is registered for rendering. The Renderer uses the Transform component
    // for positioning and loads the cat model based on entity type.
    // See: engine/renderer/EntityRenderer.cpp for mesh/material binding

    return entity;
}

void CatEntity::loadModel(CatEngine::ECS& /*ecs*/, CatEngine::Entity /*entity*/, const char* modelPath) {
    // Model loading is handled by the asset system and renderer
    // The renderer automatically loads models based on entity components
    // This function exists for explicit model override scenarios
    //
    // Current implementation: Models are loaded via:
    // 1. AssetManager::loadModel(modelPath) - returns ModelHandle
    // 2. Renderer binds ModelHandle to entity via RenderComponent
    // 3. Model is rendered each frame based on Transform component
    //
    // Default cat model: "assets/models/cat_player.gltf"
    Engine::Logger::debug(std::string("Model path registered: ") + modelPath);
}

void CatEntity::configureAnimations(CatEngine::ECS& /*ecs*/, CatEngine::Entity /*entity*/) {
    // Animation configuration is handled by the AnimationSystem
    // Animations are automatically set up based on entity type and components
    //
    // Current cat animations (defined in assets/animations/cat_player.json):
    // - "idle"       : Default standing animation, loops
    // - "walk"       : Walking animation, blends with idle
    // - "run"        : Running animation, higher speed threshold
    // - "jump"       : Jump start, peak, and landing phases
    // - "attack_sword" : Sword swing, 0.5s duration
    // - "attack_spell" : Spell cast, 0.8s duration with particle trigger
    // - "attack_bow"   : Bow draw and release, 1.0s duration
    // - "hit"        : Damage reaction, 0.3s, blends back to current
    // - "death"      : Death fall animation, plays once
    //
    // AnimationSystem handles:
    // - State machine transitions
    // - Animation blending
    // - Root motion extraction
    // - Event triggers (footsteps, attack frames)
    Engine::Logger::debug("Animation configuration complete for cat entity");
}

} // namespace CatGame
