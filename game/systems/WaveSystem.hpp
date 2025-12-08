#pragma once

#include "../../engine/ecs/System.hpp"
#include "../../engine/ecs/ECS.hpp"
#include "../../engine/ecs/Entity.hpp"
#include "../../engine/math/Vector.hpp"
#include <functional>
#include <vector>

namespace CatGame {

/**
 * Wave state enumeration
 */
enum class WaveState {
    Spawning,       // Currently spawning enemies
    InProgress,     // Wave active, all enemies spawned
    Completed,      // All enemies defeated
    Transition      // Between waves
};

/**
 * Wave configuration
 */
struct WaveConfig {
    int baseEnemyCount = 5;
    float enemyCountMultiplier = 1.5f;
    float healthScalingPerWave = 0.1f;  // 10% more health per wave
    float spawnDelay = 0.5f;             // Delay between enemy spawns
    float transitionDelay = 3.0f;        // Delay between waves
    int bossWaveInterval = 5;            // Boss every N waves

    // Spawn area
    float spawnRadius = 20.0f;           // Radius around player to spawn enemies
    float minSpawnDistance = 10.0f;      // Minimum distance from player
};

/**
 * Wave System
 * Manages wave progression, enemy spawning, and wave completion
 */
class WaveSystem : public CatEngine::System {
public:
    using WaveCallback = std::function<void(int waveNumber)>;

    explicit WaveSystem(int priority = 200);
    ~WaveSystem() override = default;

    void init(CatEngine::ECS* ecs) override;
    void update(float dt) override;
    const char* getName() const override { return "WaveSystem"; }

    /**
     * Set the player entity (target for enemy spawning)
     */
    void setPlayer(CatEngine::Entity player) { playerEntity_ = player; }

    /**
     * Get current wave number
     */
    int getCurrentWave() const { return currentWave_; }

    /**
     * Get current wave state
     */
    WaveState getState() const { return state_; }

    /**
     * Get number of enemies remaining
     */
    int getEnemiesRemaining() const;

    /**
     * Start the first wave
     */
    void startWaves();

    /**
     * Force start next wave
     */
    void forceNextWave();

    /**
     * Set wave configuration
     */
    void setConfig(const WaveConfig& config) { config_ = config; }

    /**
     * Get wave configuration
     */
    const WaveConfig& getConfig() const { return config_; }

    /**
     * Set callback for wave completion
     */
    void setOnWaveComplete(WaveCallback callback) { onWaveComplete_ = callback; }

    /**
     * Set callback for wave start
     */
    void setOnWaveStart(WaveCallback callback) { onWaveStart_ = callback; }

private:
    /**
     * Update spawning state
     */
    void updateSpawning(float dt);

    /**
     * Update in-progress state
     */
    void updateInProgress(float dt);

    /**
     * Update transition state
     */
    void updateTransition(float dt);

    /**
     * Start a new wave
     */
    void startWave(int waveNumber);

    /**
     * Complete current wave
     */
    void completeWave();

    /**
     * Spawn a single enemy
     */
    void spawnEnemy();

    /**
     * Get spawn position for enemy
     */
    Engine::vec3 getSpawnPosition() const;

    /**
     * Calculate number of enemies for wave
     */
    int calculateEnemyCount(int waveNumber) const;

    /**
     * Calculate health scaling for wave
     */
    float calculateHealthScaling(int waveNumber) const;

    /**
     * Is this a boss wave?
     */
    bool isBossWave(int waveNumber) const;

    /**
     * Transition to new state
     */
    void transitionToState(WaveState newState);

    // Wave tracking
    int currentWave_ = 0;
    WaveState state_ = WaveState::Transition;
    float stateTimer_ = 0.0f;

    // Spawning
    int enemiesToSpawn_ = 0;
    int enemiesSpawned_ = 0;
    float spawnTimer_ = 0.0f;

    // Enemy tracking
    std::vector<CatEngine::Entity> spawnedEnemies_;

    // Player reference
    CatEngine::Entity playerEntity_;

    // Configuration
    WaveConfig config_;

    // Callbacks
    WaveCallback onWaveComplete_;
    WaveCallback onWaveStart_;

    // System state
    bool wavesStarted_ = false;
};

} // namespace CatGame
