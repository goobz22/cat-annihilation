#include "EnemyAISystem.hpp"
#include "../components/HealthComponent.hpp"
#include "../components/MovementComponent.hpp"
#include "../../engine/math/Transform.hpp"
#include "../../engine/math/Vector.hpp"
#include "../../engine/math/Math.hpp"
#include "../../engine/math/Quaternion.hpp"
#include <cmath>

namespace CatGame {

EnemyAISystem::EnemyAISystem(int priority)
    : System(priority)
{}

void EnemyAISystem::update(float dt) {
    if (!ecs_) return;

    // Query all enemies
    auto query = ecs_->query<EnemyComponent, Engine::Transform>();

    for (auto [entity, enemy, transform] : query.view()) {
        // Skip dead enemies (cleanup is handled by HealthSystem)
        auto* health = ecs_->getComponent<HealthComponent>(entity);
        if (health && !health->isAlive()) {
            if (enemy->state != AIState::Dead) {
                transitionToState(*enemy, AIState::Dead);
            }
        }

        updateEnemyAI(entity, *enemy, dt);
    }
}

void EnemyAISystem::updateEnemyAI(CatEngine::Entity entity, EnemyComponent& enemy, float dt) {
    enemy.stateTimer += dt;

    // Update attack cooldown
    if (enemy.attackCooldownTimer > 0.0f) {
        enemy.attackCooldownTimer -= dt;
    }

    // State machine
    switch (enemy.state) {
        case AIState::Idle:
            updateIdleState(entity, enemy, dt);
            break;
        case AIState::Chasing:
            updateChasingState(entity, enemy, dt);
            break;
        case AIState::Attacking:
            updateAttackingState(entity, enemy, dt);
            break;
        case AIState::Dead:
            updateDeadState(entity, enemy, dt);
            break;
    }
}

void EnemyAISystem::updateIdleState(CatEngine::Entity entity, EnemyComponent& enemy, float dt) {
    // Wait for idle timer
    if (enemy.stateTimer < enemy.idleWaitTime) {
        return;
    }

    // Check if target is valid and in aggro range
    if (!ecs_->isAlive(enemy.target)) {
        return;
    }

    auto* transform = ecs_->getComponent<Engine::Transform>(entity);
    auto* targetTransform = ecs_->getComponent<Engine::Transform>(enemy.target);

    if (!transform || !targetTransform) {
        return;
    }

    float distance = getDistanceToTarget(transform->position, targetTransform->position);

    if (distance <= enemy.aggroRange) {
        transitionToState(enemy, AIState::Chasing);
    }
}

void EnemyAISystem::updateChasingState(CatEngine::Entity entity, EnemyComponent& enemy, float dt) {
    if (!ecs_->isAlive(enemy.target)) {
        transitionToState(enemy, AIState::Idle);
        return;
    }

    auto* transform = ecs_->getComponent<Engine::Transform>(entity);
    auto* targetTransform = ecs_->getComponent<Engine::Transform>(enemy.target);

    if (!transform || !targetTransform) {
        return;
    }

    float distance = getDistanceToTarget(transform->position, targetTransform->position);

    // Check if in attack range
    if (distance <= enemy.attackRange) {
        transitionToState(enemy, AIState::Attacking);
        return;
    }

    // Check if target escaped
    if (distance > enemy.aggroRange * 1.5f) {
        transitionToState(enemy, AIState::Idle);
        return;
    }

    // Move toward target
    moveTowardTarget(entity, targetTransform->position, enemy.moveSpeed, dt);
    faceTarget(entity, targetTransform->position);
}

void EnemyAISystem::updateAttackingState(CatEngine::Entity entity, EnemyComponent& enemy, float dt) {
    if (!ecs_->isAlive(enemy.target)) {
        transitionToState(enemy, AIState::Idle);
        return;
    }

    auto* transform = ecs_->getComponent<Engine::Transform>(entity);
    auto* targetTransform = ecs_->getComponent<Engine::Transform>(enemy.target);

    if (!transform || !targetTransform) {
        return;
    }

    float distance = getDistanceToTarget(transform->position, targetTransform->position);

    // Check if target moved out of attack range
    if (distance > enemy.attackRange * 1.2f) {
        transitionToState(enemy, AIState::Chasing);
        return;
    }

    // Face the target
    faceTarget(entity, targetTransform->position);

    // Attack if cooldown is ready
    if (enemy.canAttack()) {
        auto* targetHealth = ecs_->getComponent<HealthComponent>(enemy.target);
        if (targetHealth && !targetHealth->isInvincible()) {
            // Deal damage
            targetHealth->currentHealth -= enemy.attackDamage;
            targetHealth->timeSinceLastDamage = 0.0f;

            // Apply invincibility frames (brief)
            targetHealth->invincibilityTimer = 0.2f;

            // Reset attack cooldown
            enemy.attackCooldownTimer = enemy.attackCooldown;
        }
    }
}

void EnemyAISystem::updateDeadState(CatEngine::Entity entity, EnemyComponent& enemy, float dt) {
    // Dead state is handled by HealthSystem
    // This is just here for completeness
}

void EnemyAISystem::transitionToState(EnemyComponent& enemy, AIState newState) {
    if (enemy.state == newState) {
        return;
    }

    enemy.state = newState;
    enemy.stateTimer = 0.0f;

    // State-specific initialization
    switch (newState) {
        case AIState::Idle:
            // Reset to idle
            break;
        case AIState::Chasing:
            // Start chasing
            break;
        case AIState::Attacking:
            // Reset attack cooldown
            enemy.attackCooldownTimer = 0.0f;
            break;
        case AIState::Dead:
            // Begin death
            break;
    }
}

bool EnemyAISystem::isTargetInRange(const Engine::vec3& position, const Engine::vec3& targetPos, float range) const {
    return getDistanceToTarget(position, targetPos) <= range;
}

float EnemyAISystem::getDistanceToTarget(const Engine::vec3& position, const Engine::vec3& targetPos) const {
    Engine::vec3 diff = targetPos - position;
    return diff.length();
}

void EnemyAISystem::moveTowardTarget(CatEngine::Entity entity, const Engine::vec3& targetPos, float speed, float dt) {
    auto* transform = ecs_->getComponent<Engine::Transform>(entity);
    if (!transform) return;

    // Calculate direction to target
    Engine::vec3 direction = (targetPos - transform->position).normalized();

    // Move position
    transform->position += direction * speed * dt;
}

void EnemyAISystem::faceTarget(CatEngine::Entity entity, const Engine::vec3& targetPos) {
    auto* transform = ecs_->getComponent<Engine::Transform>(entity);
    if (!transform) return;

    // Calculate direction to target
    Engine::vec3 direction = (targetPos - transform->position).normalized();

    // Only rotate if direction is valid
    if (direction.lengthSquared() > Engine::Math::EPSILON) {
        // Create look rotation (looking along direction, with up vector)
        transform->lookAt(targetPos, Engine::vec3(0.0f, 1.0f, 0.0f));
    }
}

} // namespace CatGame
