#pragma once

#include "../../engine/ecs/Entity.hpp"

namespace CatGame {

/**
 * Enemy types with different characteristics
 */
enum class EnemyType {
    Dog,        // Standard enemy
    BigDog,     // High HP, high damage, slow
    FastDog,    // Low HP, low damage, fast
    BossDog     // Boss variant
};

/**
 * AI state machine states
 */
enum class AIState {
    Idle,       // Waiting, patrolling
    Chasing,    // Moving toward target
    Attacking,  // In attack range, dealing damage
    Dead        // Death animation/cleanup
};

/**
 * Enemy AI component
 */
struct EnemyComponent {
    EnemyType type = EnemyType::Dog;
    AIState state = AIState::Idle;

    // Target tracking
    CatEngine::Entity target;

    // Behavior parameters
    float aggroRange = 15.0f;      // Detection range
    float attackRange = 2.0f;       // Range to start attacking
    float attackDamage = 10.0f;     // Damage per attack
    float attackCooldown = 1.0f;    // Seconds between attacks
    float attackCooldownTimer = 0.0f;

    // State timing
    float stateTimer = 0.0f;        // Time in current state
    float idleWaitTime = 0.5f;      // How long to idle before checking again

    // Movement
    float moveSpeed = 6.0f;

    // Score value when killed
    int scoreValue = 10;

    EnemyComponent() = default;

    EnemyComponent(EnemyType type, CatEngine::Entity target)
        : type(type)
        , target(target)
    {
        // Set stats based on type
        switch (type) {
            case EnemyType::Dog:
                moveSpeed = 6.0f;
                attackDamage = 10.0f;
                scoreValue = 10;
                break;
            case EnemyType::BigDog:
                moveSpeed = 4.2f;  // 0.7x speed
                attackDamage = 15.0f; // 1.5x damage
                scoreValue = 25;
                break;
            case EnemyType::FastDog:
                moveSpeed = 9.0f;  // 1.5x speed
                attackDamage = 7.5f; // 0.75x damage
                scoreValue = 15;
                break;
            case EnemyType::BossDog:
                moveSpeed = 5.0f;
                attackDamage = 25.0f;
                aggroRange = 25.0f;
                attackRange = 3.0f;
                scoreValue = 100;
                break;
        }
    }

    bool canAttack() const {
        return state == AIState::Attacking && attackCooldownTimer <= 0.0f;
    }
};

} // namespace CatGame
