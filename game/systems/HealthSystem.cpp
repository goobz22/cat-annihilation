#include "HealthSystem.hpp"
#include "../components/HealthComponent.hpp"
#include "../components/EnemyComponent.hpp"
#include "../../engine/math/Transform.hpp"
#include <vector>

namespace CatGame {

HealthSystem::HealthSystem(int priority)
    : System(priority)
{}

void HealthSystem::update(float dt) {
    if (!ecs_) return;

    // Query all entities with health
    auto query = ecs_->query<HealthComponent>();

    // Track entities to destroy
    std::vector<CatEngine::Entity> toDestroy;

    for (auto [entity, health] : query.view()) {
        updateHealth(entity, dt);

        // Remove dead entities after death animation
        if (health->isDead && health->deathTimer >= health->deathAnimationDuration) {
            toDestroy.push_back(entity);
        }
    }

    // Destroy dead entities
    for (auto entity : toDestroy) {
        ecs_->destroyEntity(entity);
    }
}

void HealthSystem::updateHealth(CatEngine::Entity entity, float dt) {
    auto* health = ecs_->getComponent<HealthComponent>(entity);
    if (!health) return;

    // Update invincibility
    updateInvincibility(entity, dt);

    // Check for death
    if (health->currentHealth <= 0.0f && !health->isDead) {
        health->isDead = true;
        health->currentHealth = 0.0f;
        handleDeath(entity);
    }

    // Update death animation
    if (health->isDead) {
        updateDeathAnimation(entity, dt);
        return; // Don't process regeneration if dead
    }

    // Update regeneration
    updateRegeneration(entity, dt);
}

void HealthSystem::updateInvincibility(CatEngine::Entity entity, float dt) {
    auto* health = ecs_->getComponent<HealthComponent>(entity);
    if (!health) return;

    if (health->invincibilityTimer > 0.0f) {
        health->invincibilityTimer -= dt;
        if (health->invincibilityTimer < 0.0f) {
            health->invincibilityTimer = 0.0f;
        }
    }
}

void HealthSystem::updateRegeneration(CatEngine::Entity entity, float dt) {
    auto* health = ecs_->getComponent<HealthComponent>(entity);
    if (!health) return;

    if (!health->canRegenerate || health->regenerationRate <= 0.0f) {
        return;
    }

    // Update time since last damage
    health->timeSinceLastDamage += dt;

    // Only regenerate after delay
    if (health->timeSinceLastDamage >= health->regenerationDelay) {
        float regenAmount = health->regenerationRate * dt;
        heal(entity, regenAmount);
    }
}

void HealthSystem::updateDeathAnimation(CatEngine::Entity entity, float dt) {
    auto* health = ecs_->getComponent<HealthComponent>(entity);
    if (!health) return;

    health->deathTimer += dt;
}

void HealthSystem::handleDeath(CatEngine::Entity entity) {
    // Check if entity is an enemy
    bool isEnemy = ecs_->hasComponent<EnemyComponent>(entity);

    if (isEnemy) {
        handleEnemyDeath(entity);
    } else {
        handlePlayerDeath(entity);
    }

    // Trigger callback
    if (onEntityDeath_) {
        onEntityDeath_(entity, isEnemy);
    }
}

void HealthSystem::handleEnemyDeath(CatEngine::Entity entity) {
    auto* enemy = ecs_->getComponent<EnemyComponent>(entity);
    if (!enemy) return;

    // Loot spawning, score adding, and death effects are handled by the game layer
    // via the onEntityDeath_ callback registered during game initialization.
    // This allows the CatAnnihilation class to coordinate with:
    // - LootSystem for item drops
    // - ScoreSystem/GameState for score tracking
    // - GameAudio for death sounds
    // - ParticleSystem for death visual effects
}

void HealthSystem::handlePlayerDeath(CatEngine::Entity /*entity*/) {
    // Game over and death effects are handled by the game layer
    // via the onEntityDeath_ callback registered during game initialization.
    // This allows the CatAnnihilation class to:
    // - Trigger game over state transition
    // - Play player death animation and sounds
    // - Show game over UI
}

bool HealthSystem::applyDamage(CatEngine::Entity entity, float damage) {
    auto* health = ecs_->getComponent<HealthComponent>(entity);
    if (!health) {
        return false;
    }

    // Check if invincible
    if (health->isInvincible()) {
        return false;
    }

    // Check if already dead
    if (health->isDead) {
        return false;
    }

    // Apply damage
    health->currentHealth -= damage;
    health->timeSinceLastDamage = 0.0f;

    // Clamp to 0
    if (health->currentHealth < 0.0f) {
        health->currentHealth = 0.0f;
    }

    // Trigger callback
    if (onDamageTaken_) {
        onDamageTaken_(entity, damage);
    }

    return true;
}

float HealthSystem::heal(CatEngine::Entity entity, float amount) {
    auto* health = ecs_->getComponent<HealthComponent>(entity);
    if (!health) {
        return 0.0f;
    }

    // Can't heal if dead
    if (health->isDead) {
        return 0.0f;
    }

    // Calculate actual heal amount
    float actualHeal = std::min(amount, health->maxHealth - health->currentHealth);
    if (actualHeal <= 0.0f) {
        return 0.0f;
    }

    // Apply heal
    health->currentHealth += actualHeal;

    // Clamp to max
    if (health->currentHealth > health->maxHealth) {
        health->currentHealth = health->maxHealth;
    }

    return actualHeal;
}

void HealthSystem::kill(CatEngine::Entity entity) {
    auto* health = ecs_->getComponent<HealthComponent>(entity);
    if (!health) {
        return;
    }

    health->currentHealth = 0.0f;
    health->isDead = true;
    handleDeath(entity);
}

void HealthSystem::revive(CatEngine::Entity entity, float health_amount) {
    auto* health = ecs_->getComponent<HealthComponent>(entity);
    if (!health) {
        return;
    }

    health->isDead = false;
    health->deathTimer = 0.0f;
    health->currentHealth = std::min(health_amount, health->maxHealth);
    health->invincibilityTimer = 1.0f; // Brief invincibility after revive
}

} // namespace CatGame
