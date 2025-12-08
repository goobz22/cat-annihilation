#include "WaveSystem.hpp"
#include "../components/EnemyComponent.hpp"
#include "../components/HealthComponent.hpp"
#include "../../engine/math/Transform.hpp"
#include "../../engine/math/Math.hpp"
#include <cmath>
#include <random>

namespace CatGame {

WaveSystem::WaveSystem(int priority)
    : System(priority)
{}

void WaveSystem::init(CatEngine::ECS* ecs) {
    System::init(ecs);
}

void WaveSystem::update(float dt) {
    if (!ecs_ || !wavesStarted_) {
        return;
    }

    stateTimer_ += dt;

    // Update based on current state
    switch (state_) {
        case WaveState::Spawning:
            updateSpawning(dt);
            break;
        case WaveState::InProgress:
            updateInProgress(dt);
            break;
        case WaveState::Completed:
            // Automatically transition
            transitionToState(WaveState::Transition);
            break;
        case WaveState::Transition:
            updateTransition(dt);
            break;
    }
}

void WaveSystem::updateSpawning(float dt) {
    spawnTimer_ += dt;

    // Spawn enemies at intervals
    if (spawnTimer_ >= config_.spawnDelay && enemiesToSpawn_ > 0) {
        spawnEnemy();
        enemiesToSpawn_--;
        spawnTimer_ = 0.0f;
    }

    // Check if all enemies spawned
    if (enemiesToSpawn_ <= 0) {
        transitionToState(WaveState::InProgress);
    }
}

void WaveSystem::updateInProgress(float dt) {
    // Clean up dead enemies from tracking list
    spawnedEnemies_.erase(
        std::remove_if(spawnedEnemies_.begin(), spawnedEnemies_.end(),
            [this](CatEngine::Entity enemy) {
                return !ecs_->isAlive(enemy);
            }),
        spawnedEnemies_.end()
    );

    // Check if all enemies are defeated
    if (spawnedEnemies_.empty()) {
        completeWave();
    }
}

void WaveSystem::updateTransition(float dt) {
    // Wait for transition delay, then start next wave
    if (stateTimer_ >= config_.transitionDelay) {
        startWave(currentWave_ + 1);
    }
}

void WaveSystem::startWaves() {
    if (wavesStarted_) {
        return;
    }

    wavesStarted_ = true;
    startWave(1);
}

void WaveSystem::forceNextWave() {
    // Destroy all remaining enemies
    for (auto enemy : spawnedEnemies_) {
        if (ecs_->isAlive(enemy)) {
            ecs_->destroyEntity(enemy);
        }
    }
    spawnedEnemies_.clear();

    // Start next wave immediately
    startWave(currentWave_ + 1);
}

void WaveSystem::startWave(int waveNumber) {
    currentWave_ = waveNumber;
    enemiesToSpawn_ = calculateEnemyCount(waveNumber);
    enemiesSpawned_ = 0;
    spawnTimer_ = 0.0f;
    spawnedEnemies_.clear();

    transitionToState(WaveState::Spawning);

    // Trigger callback
    if (onWaveStart_) {
        onWaveStart_(currentWave_);
    }
}

void WaveSystem::completeWave() {
    transitionToState(WaveState::Completed);

    // Trigger callback
    if (onWaveComplete_) {
        onWaveComplete_(currentWave_);
    }
}

void WaveSystem::spawnEnemy() {
    if (!ecs_ || !ecs_->isAlive(playerEntity_)) {
        return;
    }

    // Get spawn position
    Engine::vec3 spawnPos = getSpawnPosition();

    // Determine enemy type
    EnemyType enemyType = EnemyType::Dog;

    if (isBossWave(currentWave_)) {
        // Spawn boss
        enemyType = EnemyType::BossDog;
    } else {
        // Random enemy type based on wave
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_real_distribution<> dis(0.0, 1.0);

        float roll = dis(gen);
        if (currentWave_ >= 3) {
            // After wave 3, introduce variety
            if (roll < 0.2f) {
                enemyType = EnemyType::BigDog;
            } else if (roll < 0.4f) {
                enemyType = EnemyType::FastDog;
            } else {
                enemyType = EnemyType::Dog;
            }
        }
    }

    // Create enemy entity
    auto enemy = ecs_->createEntity();

    // Add transform
    ecs_->emplaceComponent<Engine::Transform>(enemy, spawnPos);

    // Add enemy component
    ecs_->emplaceComponent<EnemyComponent>(enemy, enemyType, playerEntity_);

    // Calculate health with scaling
    float healthScaling = calculateHealthScaling(currentWave_);
    float baseHealth = 50.0f;

    switch (enemyType) {
        case EnemyType::Dog:
            baseHealth = 50.0f;
            break;
        case EnemyType::BigDog:
            baseHealth = 100.0f; // 2x health
            break;
        case EnemyType::FastDog:
            baseHealth = 25.0f; // 0.5x health
            break;
        case EnemyType::BossDog:
            baseHealth = 300.0f; // Boss health
            break;
    }

    float finalHealth = baseHealth * healthScaling;
    ecs_->emplaceComponent<HealthComponent>(enemy, finalHealth);

    // Add movement component
    auto* enemyComp = ecs_->getComponent<EnemyComponent>(enemy);
    if (enemyComp) {
        ecs_->emplaceComponent<MovementComponent>(enemy, enemyComp->moveSpeed);
    }

    // Track spawned enemy
    spawnedEnemies_.push_back(enemy);
    enemiesSpawned_++;
}

Engine::vec3 WaveSystem::getSpawnPosition() const {
    if (!ecs_ || !ecs_->isAlive(playerEntity_)) {
        return Engine::vec3(0.0f, 0.0f, 0.0f);
    }

    auto* playerTransform = ecs_->getComponent<Engine::Transform>(playerEntity_);
    if (!playerTransform) {
        return Engine::vec3(0.0f, 0.0f, 0.0f);
    }

    // Random angle around player
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_real_distribution<> angleDis(0.0, 2.0 * Engine::Math::PI);
    std::uniform_real_distribution<> radiusDis(config_.minSpawnDistance, config_.spawnRadius);

    float angle = angleDis(gen);
    float radius = radiusDis(gen);

    // Calculate position
    Engine::vec3 offset(
        std::cos(angle) * radius,
        0.0f,
        std::sin(angle) * radius
    );

    return playerTransform->position + offset;
}

int WaveSystem::calculateEnemyCount(int waveNumber) const {
    return static_cast<int>(config_.baseEnemyCount +
           (waveNumber - 1) * config_.enemyCountMultiplier);
}

float WaveSystem::calculateHealthScaling(int waveNumber) const {
    return 1.0f + (waveNumber - 1) * config_.healthScalingPerWave;
}

bool WaveSystem::isBossWave(int waveNumber) const {
    return (waveNumber % config_.bossWaveInterval) == 0;
}

int WaveSystem::getEnemiesRemaining() const {
    return static_cast<int>(spawnedEnemies_.size()) + enemiesToSpawn_;
}

void WaveSystem::transitionToState(WaveState newState) {
    if (state_ == newState) {
        return;
    }

    state_ = newState;
    stateTimer_ = 0.0f;
}

} // namespace CatGame
