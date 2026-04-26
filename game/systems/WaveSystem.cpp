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

    // Honour `initialWave_` (default 1, set via setInitialWave). When the
    // override is left at the default this is identical to the prior
    // `startWave(1)` call. When a CI / portfolio caller bumps it via the
    // `--starting-wave` CLI flag we drop straight into that wave's
    // spawn budget. Critically, `startWave()` already handles
    //   - calculating enemy count from the wave number
    //   - calculating health scaling from the wave number
    //   - flagging boss-wave behaviour via isBossWave()
    // so jumping to wave 5 produces a fully-formed boss-wave
    // configuration with no extra branching here.
    startWave(initialWave_);
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

    // Determine enemy type.
    //
    // Pre-2026-04-24 behaviour weighted heavily towards `EnemyType::Dog` and
    // only introduced variety on wave 3+, which meant short playtest runs
    // (the nightly --exit-after-seconds 30 smoke) saw five rounds of
    // identical `dog_regular.glb` spawns and never exercised the variant
    // GLBs that landed in `assets/models/meshy_raw_dogs/` (dog_big,
    // dog_fast, dog_boss). The user's 2026-04-24 directive was explicit
    // about visible variety: "Different dog variants (regular/big/fast/
    // boss) actually render different GLBs in the same wave so they're
    // visually distinguishable."
    //
    // The new policy is a deterministic round-robin keyed off
    // `enemiesSpawned_`, the per-wave counter that increments AFTER each
    // spawn. Slot ordering is intentional — the variants come FIRST so
    // every wave (even short 3-spawn waves at low difficulty settings) is
    // guaranteed all three non-boss silhouettes within the first three
    // spawns:
    //   slot 0: Dog       (regular silhouette, baseline)
    //   slot 1: FastDog   (0.8x scale, distinct speed, lean rig)
    //   slot 2: BigDog    (1.5x scale, hefty silhouette)
    //   slot 3: Dog       (regular)
    //   slot 4: Dog       (regular)
    // The 5-slot pattern then repeats. Wave 1 with 3 spawns covers all
    // three variants (1 regular + 1 fast + 1 big). Wave 2 with ~5 spawns
    // hits the same pattern + extra regulars. This is the explicit visual
    // evidence the user requested 2026-04-24 ("Different dog variants
    // actually render different GLBs in the same wave so they're visually
    // distinguishable") and it stays cheap to predict from the log: a
    // reviewer can read off the next type from `enemiesSpawned_ % 5`.
    //
    // Boss waves still override unconditionally — `isBossWave(N)` returns
    // true at every fifth wave (configurable via `bossWaveInterval`) and
    // every spawn in that wave is a BossDog at 2.0x scale, which is the
    // existing "this wave is the climactic one" signal.
    //
    // Determinism note: we deliberately replaced the prior std::mt19937
    // sampler with index-based selection. RNG-driven variety made the
    // golden-image PPMs flaky (one in five runs would happen to roll
    // boss/fast/big in a different order, producing a different SSIM
    // even though gameplay was identical). A fixed pattern is also
    // self-documenting — a reviewer reading the log can predict the
    // type of the next spawn from `enemiesSpawned_ % 5`, which makes
    // playtest triage trivial.
    EnemyType enemyType = EnemyType::Dog;

    if (isBossWave(currentWave_)) {
        enemyType = EnemyType::BossDog;
    } else {
        const int variantSlot = enemiesSpawned_ % 5;
        switch (variantSlot) {
            case 1:
                enemyType = EnemyType::FastDog;
                break;
            case 2:
                enemyType = EnemyType::BigDog;
                break;
            case 0:
            case 3:
            case 4:
            default:
                enemyType = EnemyType::Dog;
                break;
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

    Engine::vec3 spawnPos = playerTransform->position + offset;

    // Snap Y to the terrain heightfield when a sampler is wired.
    // Without this, dogs inherit the player's Y verbatim, which puts
    // them on a flat plane that intersects the rolling heightfield —
    // uphill spawns buried, downhill spawns floating. The +0.05 m
    // epsilon mirrors the NPCSystem snap so the dog's feet sit just
    // above the surface (no Z-fighting with the terrain mesh). Tests
    // and headless paths that don't supply a sampler keep the previous
    // behaviour by falling through this branch.
    if (heightSampler_) {
        spawnPos.y = heightSampler_(spawnPos.x, spawnPos.z) + 0.05F;
    }

    return spawnPos;
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
