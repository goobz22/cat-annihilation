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
     * Resolve the spawn-ring CENTER (web parity). Pure + static so it is
     * unit-testable without the ECS/spawn pipeline WaveSystem couples to.
     *
     * Under parity, once a wave has captured its start anchor the ring center is
     * that FROZEN anchor — the web builds the whole encircling ring off a single
     * playerPosition snapshot and only defers the reveal, so a fleeing player is
     * not perpetually re-surrounded by the staggered spawns (2026-07-18 audit).
     * Falls back to the LIVE player position when parity is off or no anchor was
     * captured (e.g. a wave that started with no player).
     */
    static Engine::vec3 spawnRingCenter(bool parityEnabled, bool anchorValid,
                                        const Engine::vec3& anchor,
                                        const Engine::vec3& livePlayer) {
        return (parityEnabled && anchorValid) ? anchor : livePlayer;
    }

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
     * Total enemies a given wave spawns, from the SAME formula spawning
     * uses. The HUD must read this instead of re-deriving a count — its
     * previous linear mirror drifted from the live curve and displayed
     * totals that never matched what actually spawned (docs/parity/
     * PARITY_MATRIX.md, waves domain).
     */
    int getEnemiesTotalForWave(int waveNumber) const { return calculateEnemyCount(waveNumber); }

    /**
     * Start the first wave
     */
    void startWaves();

    /**
     * Reset the wave machine to its pre-first-wave state so a subsequent
     * startWaves() actually re-runs startWave(initialWave_).
     *
     * WHY THIS EXISTS (2026-07-17 correctness audit): the game class's
     * restart()/quitToMenu() wipe entities + stats and re-enter Playing,
     * which calls startWaves() again — but startWaves() early-returns on
     * `wavesStarted_`, which was set true once on the first game and reset
     * NOWHERE. So a restart left this system holding its pre-death
     * currentWave_ and a stale InProgress state referencing now-dead entity
     * handles; the next update() saw "all enemies gone", spuriously
     * completed the stale wave, and advanced the player straight into
     * wave N+1. Observed: die on wave 1 → hit Try Again → thrown into
     * wave 2 (10 dogs) with 0 kills within ~6s. reset() clears the latch
     * and all per-run wave state so restart genuinely starts over at
     * initialWave_. Distinct from the pause/resume path, which must NOT
     * reset (a resume keeps wavesStarted_ so the wave continues).
     */
    void reset();

    /**
     * Override the wave number that startWaves() will spawn first.
     *
     * Why this exists: the user-directive scoreboard's "different dog
     * variants render different GLBs in the same wave" example listed
     * `dog_boss.glb` as one of the four expected silhouettes. The
     * round-robin spawn pattern in spawnEnemy() reserves the boss GLB
     * for boss waves only (every 5th wave by default), and the
     * `--exit-after-seconds 30` nightly playtest budget runs out before
     * wave 5 — so the boss path was never visually confirmed despite
     * the assets being staged at `assets/models/meshy_raw_dogs/rigged/
     * dog_boss.glb` since iter 04c8339. Letting an autoplay run start
     * directly at wave 5 (or any later boss wave) makes the boss
     * silhouette reachable in a 30-45s frame-dump capture WITHOUT
     * disturbing the careful design choice to keep boss reserved for
     * its own wave during regular gameplay.
     *
     * Why a setter rather than a startWaves(int) overload: a plain
     * setter keeps the callers in `CatAnnihilation::onEnterPlaying`
     * untouched (they still call `startWaves()` exactly as before)
     * while making the override addressable from main.cpp's CLI
     * parser via `game->getWaveSystem()->setInitialWave(...)`. The
     * default of 1 means existing nightlies, golden-image PPMs, and
     * the interactive launch are bit-for-bit unchanged.
     *
     * Bounds: a value < 1 is silently clamped to 1 (the only valid
     * starting wave is "wave 1 or later"). No upper bound — the wave
     * system handles arbitrarily high wave numbers via the existing
     * `calculateEnemyCount`/`calculateHealthScaling` formulas, and
     * boss-wave detection (`isBossWave`) is modular so wave 25 is
     * just as legitimate as wave 5 if a viewer wants the late-game
     * difficulty curve.
     */
    void setInitialWave(int wave) { initialWave_ = (wave < 1) ? 1 : wave; }

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

    /**
     * Provide a terrain-height sampler so getSpawnPosition() can snap
     * the spawned dog's Y to the heightfield instead of inheriting the
     * player's Y verbatim.
     *
     * Why this is required: the previous getSpawnPosition() returned
     * `playerTransform.position + (cosθ·r, 0, sinθ·r)` for r in
     * [25 m, 40 m]. That places every dog on a flat plane equal to the
     * player's Y, but the underlying heightfield rolls 5–15 m across
     * those distances — so dogs spawned uphill are buried under the
     * terrain (invisible from any practical camera angle) and dogs
     * downhill float in mid-air. iter 2026-04-25 ~01:50 UTC's playtest
     * log shows wave 1 spawning 3 dogs that allocate skinned VBs but
     * the screenshot shows zero dogs visible — same bug as the buried
     * NPCs, just a different spawn site.
     *
     * Same std::function pattern as NPCSystem so WaveSystem keeps no
     * hard dependency on Terrain/GameWorld.
     */
    using TerrainHeightSampler = std::function<float(float, float)>;
    void setTerrainHeightSampler(TerrainHeightSampler sampler) {
        heightSampler_ = std::move(sampler);
    }

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

    // Web-parity spawn-ring anchor: the player position captured ONCE at
    // wave start. The web builds the whole encircling ring off a single
    // playerPosition snapshot (LocalEnemySystem.tsx spawnWave) and only defers
    // the reveal, so the ring is frozen at the wave-start location; native
    // spawns are staggered (~0.2 s each) and previously re-read the LIVE player
    // position per spawn, so the ring chased a fleeing player (2026-07-18
    // audit). Cached in startWave(), consumed by getSpawnPosition() under parity.
    Engine::vec3 waveAnchorPosition_{0.0f, 0.0f, 0.0f};
    bool waveAnchorValid_ = false;

    // Enemy tracking
    std::vector<CatEngine::Entity> spawnedEnemies_;

    // Player reference
    CatEngine::Entity playerEntity_;

    // Configuration
    WaveConfig config_;

    // Callbacks
    WaveCallback onWaveComplete_;
    WaveCallback onWaveStart_;

    // Terrain-height query, optional. When non-empty, getSpawnPosition()
    // overrides the inherited player-Y with the heightfield value at the
    // generated x,z so each dog stands on the surface instead of in the
    // player's Y-plane. See setTerrainHeightSampler() for the rationale.
    TerrainHeightSampler heightSampler_;

    // System state
    bool wavesStarted_ = false;

    // Override seed for `startWaves()`. Defaults to 1 so the
    // pre-2026-04-25 startWaves() → startWave(1) call site is
    // unchanged when no setter is invoked. setInitialWave() (above)
    // bumps this to skip directly to a later wave for portfolio /
    // CI captures that need the boss GLB on screen without sitting
    // through four cleared waves.
    int initialWave_ = 1;
};

} // namespace CatGame
