#include "WaveSystem.hpp"
#include "../components/EnemyComponent.hpp"
#include "../components/HealthComponent.hpp"
#include "../components/MovementComponent.hpp"
#include "../entities/DogEntity.hpp"
#include "../../engine/math/Transform.hpp"
#include "../../engine/math/Math.hpp"
#include "../../engine/core/Logger.hpp"
#include <cmath>
#include <random>

namespace CatGame {

// Human-readable name for a wave state. Used only in log lines — lets the
// playtest / portfolio log tell a story ("Spawning → InProgress → Completed
// → Transition") instead of surfacing raw enum ordinals. Kept in-TU because
// no other file needs to stringify WaveState today and we want the mapping
// to stay adjacent to the enum in WaveSystem.hpp.
static const char* waveStateName(WaveState s) {
    switch (s) {
        case WaveState::Spawning:   return "Spawning";
        case WaveState::InProgress: return "InProgress";
        case WaveState::Completed:  return "Completed";
        case WaveState::Transition: return "Transition";
    }
    return "Unknown";
}

// Human-readable name for an enemy archetype. Same rationale as above:
// the log is the only consumer, so we keep the mapping here rather than
// bolting an extra method onto EnemyComponent.
static const char* enemyTypeName(EnemyType t) {
    switch (t) {
        case EnemyType::Dog:      return "Dog";
        case EnemyType::BigDog:   return "BigDog";
        case EnemyType::FastDog:  return "FastDog";
        case EnemyType::BossDog:  return "BossDog";
    }
    return "Unknown";
}

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

    // Log the wave's difficulty budget BEFORE the callback fires so a
    // portfolio / nightly viewer can see "wave 3 is going to push 8
    // enemies with 1.2× health" instead of just "wave 3 started" followed
    // by twenty silent seconds. boss/regular is a user-visible distinction
    // too, so we surface it in the same line.
    Engine::Logger::info(
        std::string("[wave] prepared wave ") + std::to_string(currentWave_) +
        " enemies=" + std::to_string(enemiesToSpawn_) +
        " hp_scale=" + std::to_string(calculateHealthScaling(currentWave_)) +
        " boss=" + (isBossWave(currentWave_) ? "yes" : "no"));

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

    // Delegate the full entity build (Transform + Enemy + Health + Movement
    // + MeshComponent + Animator) to DogEntity::create. Previously this
    // method hand-rolled only the first four components and skipped the
    // mesh/animator entirely — which is why every dog rendered as an
    // invisible collider before the Meshy asset work. Funnelling spawn
    // through the DogEntity factory means:
    //   1. Every variant automatically gets its correct Meshy GLB
    //      (dog_regular / dog_fast / dog_big / dog_boss) via
    //      modelPathForType().
    //   2. Per-variant stats (health / moveSpeed / attackDamage / scale)
    //      come from DogEntity::getStatsForType rather than being
    //      duplicated here — no more drift between two copies.
    //   3. Wave-progression health scaling stays in WaveSystem (the
    //      scaling formula belongs to the wave game mode, not the entity
    //      factory) and is threaded through as the healthMultiplier arg.
    float healthScaling = calculateHealthScaling(currentWave_);
    CatEngine::Entity enemy =
        DogEntity::create(ecs_, enemyType, spawnPos, playerEntity_, healthScaling);
    if (enemy == CatEngine::NULL_ENTITY) {
        return;
    }

    // Compute finalHealth for the log line below. DogEntity::getStatsForType
    // is private, so re-derive the per-variant baseline here — this mirror
    // is narrow (4 cases, no drift risk) and confined to a log string.
    float baseHealth = 50.0f;
    switch (enemyType) {
        case EnemyType::Dog:      baseHealth = 50.0f;  break;
        case EnemyType::BigDog:   baseHealth = 100.0f; break;
        case EnemyType::FastDog:  baseHealth = 25.0f;  break;
        case EnemyType::BossDog:  baseHealth = 300.0f; break;
    }
    float finalHealth = baseHealth * healthScaling;

    // Track spawned enemy
    spawnedEnemies_.push_back(enemy);
    enemiesSpawned_++;

    // Per-spawn log. This is the lowest-level event-triggered signal that
    // the wave system is actually doing something between "wave started"
    // and "enemy died / player died". Without it a silent 20s stretch
    // looked identical whether spawns were failing, spawns were invisible,
    // or enemies just weren't reaching the player yet. Fires at
    // config_.spawnDelay intervals (default 0.5s × N enemies), so a single
    // wave produces a handful of log lines — not a hot-path concern.
    Engine::Logger::info(
        std::string("[wave] spawn type=") + enemyTypeName(enemyType) +
        " hp=" + std::to_string(static_cast<int>(finalHealth)) +
        " pos=(" + std::to_string(spawnPos.x) +
        "," + std::to_string(spawnPos.z) + ")" +
        " wave=" + std::to_string(currentWave_) +
        " remaining=" + std::to_string(enemiesToSpawn_));
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

    // Log BEFORE we mutate state_ so the line captures the real old→new
    // pair. The Completed state in particular only lives for exactly one
    // frame (updateInProgress detects empty spawnedEnemies_, flips to
    // Completed, and updateTransition immediately bumps to Transition on
    // the next frame), so without this log the state would be invisible
    // to any observer polling at a coarser cadence than per-frame.
    const WaveState oldState = state_;
    state_ = newState;
    stateTimer_ = 0.0f;

    Engine::Logger::info(
        std::string("[wave] state ") + waveStateName(oldState) +
        " -> " + waveStateName(newState) +
        " wave=" + std::to_string(currentWave_));
}

} // namespace CatGame
