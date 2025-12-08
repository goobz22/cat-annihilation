#pragma once

#include "../../engine/ecs/Entity.hpp"
#include "../../engine/math/Vector.hpp"

namespace CatGame {

/**
 * Projectile types with different characteristics
 */
enum class ProjectileType {
    Spell,          // Player magic attack
    Arrow,          // Player ranged attack
    EnemyAttack     // Enemy projectile
};

/**
 * Projectile component for moving damaging entities
 */
struct ProjectileComponent {
    ProjectileType type = ProjectileType::Spell;

    // Movement
    Engine::vec3 velocity;

    // Damage
    float damage = 20.0f;

    // Lifetime
    float lifetime = 3.0f;
    float lifetimeRemaining = 3.0f;

    // Collision
    CatEngine::Entity owner;        // Entity that fired this (avoid self-damage)
    bool hasHit = false;
    float radius = 0.3f;             // Collision radius

    // Homing behavior (optional)
    bool isHoming = false;
    CatEngine::Entity homingTarget;
    float homingStrength = 0.0f;     // 0 = no homing, higher = stronger tracking

    ProjectileComponent() = default;

    ProjectileComponent(ProjectileType type, const Engine::vec3& velocity, CatEngine::Entity owner)
        : type(type)
        , velocity(velocity)
        , owner(owner)
    {
        // Set stats based on type
        switch (type) {
            case ProjectileType::Spell:
                damage = 20.0f;
                lifetime = 3.0f;
                radius = 0.3f;
                break;
            case ProjectileType::Arrow:
                damage = 30.0f;
                lifetime = 2.0f;
                radius = 0.2f;
                break;
            case ProjectileType::EnemyAttack:
                damage = 15.0f;
                lifetime = 1.0f;
                radius = 0.25f;
                break;
        }
        lifetimeRemaining = lifetime;
    }

    bool isExpired() const { return lifetimeRemaining <= 0.0f || hasHit; }
};

} // namespace CatGame
