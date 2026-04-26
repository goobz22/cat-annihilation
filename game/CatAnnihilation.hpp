#pragma once

#include "../engine/ecs/ECS.hpp"
#include "../engine/ecs/Entity.hpp"
#include "../engine/core/Input.hpp"
#include "../engine/core/Window.hpp"
#include "../engine/core/touch_input.hpp"
#include "../engine/renderer/Renderer.hpp"
#include "../engine/audio/AudioEngine.hpp"
#include "../engine/ui/ImGuiLayer.hpp"
#include "../engine/cuda/physics/PhysicsWorld.hpp"
#include "../engine/cuda/particles/ParticleSystem.hpp"
#include "systems/PlayerControlSystem.hpp"
#include "systems/CombatSystem.hpp"
#include "systems/EnemyAISystem.hpp"
#include "systems/WaveSystem.hpp"
#include "systems/HealthSystem.hpp"
#include "systems/ProjectileSystem.hpp"
#include "systems/story_mode.hpp"
#include "systems/quest_system.hpp"
#include "systems/elemental_magic.hpp"
#include "systems/day_night_cycle.hpp"
#include "systems/cat_customization.hpp"
#include "systems/leveling_system.hpp"
#include "systems/NPCSystem.hpp"
#include "systems/DialogSystem.hpp"
#include "systems/MerchantSystem.hpp"
#include "systems/mobile_controls.hpp"
#include "ui/HUD.hpp"
#include "ui/MainMenu.hpp"
#include "ui/PauseMenu.hpp"
#include "ui/GameUI.hpp"
#include "audio/GameAudio.hpp"
#include "config/GameConfig.hpp"
#include "world/GameWorld.hpp"
#include "game_events.hpp"
#include "../engine/core/save_system.hpp"
#include <memory>

namespace CatGame {

/**
 * Game mode enumeration
 */
enum class GameMode {
    Menu,           // Main menu
    Playing,        // Active gameplay
    Paused,         // Game paused
    Cutscene,       // Story cutscene playing
    Dialog,         // NPC dialog active
    GameOver,       // Player died
    Victory         // Player won
};

/**
 * Game states for Cat Annihilation
 */
enum class GameState {
    MainMenu,       // Main menu screen
    Playing,        // Active gameplay
    Paused,         // Game paused
    GameOver,       // Player died
    Victory         // Player won
};

/**
 * CatAnnihilation - Main game class
 *
 * This class manages the overall game state, systems, and game loop.
 * It coordinates all game systems and handles state transitions.
 *
 * Features:
 * - Complete ECS management
 * - Game state machine with all game modes
 * - All game systems (combat, quests, NPCs, magic, etc.)
 * - Event-driven architecture
 * - Asset loading and management
 * - Save/load system integration
 * - Full UI system
 */
class CatAnnihilation {
public:
    /**
     * Construct the game
     * @param input Pointer to input system
     * @param renderer Pointer to renderer
     * @param audioEngine Pointer to audio engine
     */
    /**
     * Construct the game.
     *
     * @param input       Keyboard/mouse input system
     * @param window      Host window — needed so touch input can hook GLFW
     *                    callbacks and so settings UI can toggle fullscreen.
     *                    May be null when running in offscreen/test harnesses.
     * @param renderer    Renderer (used for VSync toggle + draw)
     * @param audioEngine Audio engine (used for volume routing)
     * @param imguiLayer  ImGui layer used for menu / HUD drawing
     */
    explicit CatAnnihilation(Engine::Input* input,
                            Engine::Window* window,
                            CatEngine::Renderer::Renderer* renderer,
                            CatEngine::AudioEngine* audioEngine,
                            Engine::ImGuiLayer* imguiLayer);

    /**
     * Destructor
     */
    ~CatAnnihilation();

    // Prevent copying
    CatAnnihilation(const CatAnnihilation&) = delete;
    CatAnnihilation& operator=(const CatAnnihilation&) = delete;

    /**
     * Initialize the game
     * Loads assets, creates systems, and sets up initial game state
     * @return true if initialization succeeded
     */
    bool initialize();

    /**
     * Shutdown the game
     * Cleanup all systems and resources
     */
    void shutdown();

    /**
     * Update game logic
     * @param dt Delta time in seconds
     */
    void update(float dt);

    /**
     * Render the game
     */
    void render();

    /**
     * Get current game state
     * @return Current game state
     */
    GameState getGameState() const { return currentState_; }

    /**
     * Get current game mode
     * @return Current game mode
     */
    GameMode getGameMode() const { return currentMode_; }

    /**
     * Transition to a new game state
     * @param newState The state to transition to
     */
    void setState(GameState newState);

    /**
     * Set game mode
     * @param newMode The mode to transition to
     */
    void setGameMode(GameMode newMode);

    /**
     * Pause the game
     */
    void pause();

    /**
     * Unpause the game
     */
    void unpause();

    /**
     * Toggle pause state
     */
    void togglePause();

    /**
     * Restart the game
     * Resets all entities and game state
     */
    void restart();

    /**
     * Start a new game
     * @param storyMode Whether to start in story mode
     */
    void startNewGame(bool storyMode = false);

    /**
     * Continue from save game
     */
    void continueGame();

    /**
     * Quit to main menu
     */
    void quitToMenu();

    /**
     * Save game to slot
     * @param slotIndex Save slot (0-4)
     */
    bool saveGame(int slotIndex);

    /**
     * Load game from slot
     * @param slotIndex Save slot (0-4)
     */
    bool loadGame(int slotIndex);

    /**
     * Get the ECS instance
     * @return Reference to ECS
     */
    CatEngine::ECS& getECS() { return ecs_; }

    /**
     * Get the player entity
     * @return Player entity
     */
    CatEngine::Entity getPlayerEntity() const { return playerEntity_; }

    /**
     * Get player control system
     * @return Pointer to player control system
     */
    PlayerControlSystem* getPlayerControlSystem() const { return playerControlSystem_; }

    /**
     * Get combat system
     * @return Pointer to combat system
     */
    CombatSystem* getCombatSystem() const { return combatSystem_; }

    /**
     * Get wave system.
     *
     * Non-owning; the WaveSystem itself is registered with the ECS (which
     * owns it). Exposed so the main-loop heartbeat (main.cpp) and any future
     * headless / CI probe can read currentWave / enemiesRemaining without
     * having to discover the system by type through the ECS registry.
     *
     * @return Pointer to wave system, or nullptr before initialize()
     */
    WaveSystem* getWaveSystem() const { return waveSystem_; }

    /**
     * Get leveling system.
     *
     * LevelingSystem is owned by this class as a unique_ptr because it holds
     * the player's accumulated stats (level, xp, unlock state) across waves
     * — if it lived in the ECS registry it would be re-constructed on every
     * restart(). The raw pointer returned here is non-owning.
     *
     * @return Pointer to leveling system, or nullptr before initialize()
     */
    LevelingSystem* getLevelingSystem() const { return levelingSystem_.get(); }

    /**
     * Total enemies killed in the current session.
     *
     * Incremented by onEnemyKilled(); resets on restart() via startNewGame().
     * Exposed so the heartbeat log and HUD can read it from outside without
     * friending those call sites.
     */
    int getEnemiesKilled() const { return enemiesKilled_; }

    /**
     * Get event bus
     * @return Reference to event bus
     */
    GameEventBus& getEventBus() { return eventBus_; }

    /**
     * Check if game is paused
     * @return true if game is paused
     */
    bool isPaused() const { return currentState_ == GameState::Paused; }

    /**
     * Check if game is playing
     * @return true if game is in playing state
     */
    bool isPlaying() const { return currentState_ == GameState::Playing; }

    /**
     * Check if in story mode
     * @return true if story mode is active
     */
    bool isStoryMode() const { return isStoryMode_; }

    /**
     * Attach a GameConfig to the menus so the Settings panel's sliders and
     * checkboxes actually mutate (and persist) engine settings instead of
     * editing dead file-local state. Must be called before initializeUI so
     * the menus receive the bindings at construction time.
     */
    void setGameConfig(Game::GameConfig* gameConfig) { gameConfig_ = gameConfig; }

private:
    // ========================================================================
    // Initialization
    // ========================================================================

    /**
     * Initialize all game systems in correct order
     */
    void initializeSystems();

    /**
     * Initialize UI systems
     */
    void initializeUI();

    /**
     * Connect all event handlers between systems
     */
    void connectSystemEvents();

    /**
     * Load game data (assets, configs, etc.)
     */
    void loadGameData();

    /**
     * Load game assets
     */
    void loadAssets();

    /**
     * Create the player entity
     */
    void createPlayer();

    /**
     * Re-populate world-persistent entities (NPCs today, merchants/props as
     * the world grows) into the freshly-cleared ECS.
     *
     * Why this exists: every game-start path (`restart`, `loadGame`, autoplay
     * via `startNewGame`) calls `ecs_.clearEntities()` to wipe stale state.
     * That sweep also obliterates the 16 NPC entities spawned during
     * `loadGameData()` at engine startup, leaving the world map empty for
     * the rest of the session — the visible-progress disaster surfaced
     * 2026-04-25 (4 entities skinning instead of the expected 20 because
     * `loadNPCsFromFile` ran exactly once, before the first ECS wipe).
     *
     * Called immediately after `createPlayer()` on each start path. Safe
     * to call repeatedly because `NPCSystem::clearAll()` resets its
     * internal "already exists" guard before re-spawning. No-op when
     * `npcSystem_` is null (the system is optional in test harnesses
     * that don't link the JSON loader).
     */
    void repopulateWorldEntities();

    /**
     * Create and configure the death particle emitter
     */
    void createDeathParticleEmitter();

    /**
     * Spawn death effect particles at position.
     *
     * `damageType` selects a per-element tuning profile (Physical = orange-red,
     * Fire = orange-yellow with strong upward velocity, Ice = pale-cyan with
     * slow downward drift, Poison = yellow-green lingering cloud, Magic =
     * white-purple radial, True = pure white-yellow). Defaults to Physical so
     * legacy callers (and the few non-combat death paths like scripted kills)
     * still produce the original orange-red burst.
     *
     * The dispatcher is a small inline lookup table inside spawnDeathParticles
     * that mutates the existing dormant emitter (deathEmitterId_) in place
     * before triggerBurst — single-emitter parametrization rather than a pool
     * of per-element emitters. Trade-off: one map probe + a struct copy per
     * call (cheap at <2 Hz peak death cadence) in exchange for ~80% less code
     * + zero new emitter slots in the ParticleSystem map. The visual delta
     * (per-element colour, velocity arc, lifetime) lives in the tuning table,
     * not in distinct emitters.
     */
    void spawnDeathParticles(const Engine::vec3& position,
                             DamageType damageType = DamageType::Physical);

    /**
     * Create and configure the non-killing-hit particle emitter.
     *
     * Companion to createDeathParticleEmitter() but tuned for the much higher
     * call frequency of mid-combat impacts: smaller burst (~10 particles),
     * tighter sphere radius (so the burst reads as a localised "thwack"
     * rather than a death-shroud), neutral white-yellow albedo (so impact
     * reads as generic kinetic feedback regardless of the killing-damage
     * element), and a shorter lifetime (so the burst clears the screen
     * before the next swing in a 2-3 hit/sec combat cadence).
     *
     * Mirrors the dormant-emitter / re-enable-per-shot pattern of the
     * death emitter — see createDeathParticleEmitter() and
     * spawnHitParticles() for the rationale on why a single shared
     * dormant OneShot emitter is preferable to spawning a fresh emitter
     * per hit (allocation cost + emitter-map fragmentation if hits land
     * at >10 Hz during combos).
     */
    void createHitParticleEmitter();

    /**
     * Spawn non-killing-hit particles at the impact position.
     *
     * Called from the CombatSystem onHitCallback wired in
     * connectSystemEvents(). Skipped on killing blows: the death-burst
     * path (spawnDeathParticles) already produces a much larger burst at
     * the same position, and stacking the smaller hit burst on top adds
     * visual noise without delta. The gate lives in the lambda, not in
     * this function, so callers that want unconditional hit-bursts (a
     * future CombatSystem trace replay, debug overlay, etc.) can call
     * this directly.
     */
    void spawnHitParticles(const Engine::vec3& position,
                           DamageType damageType = DamageType::Physical);

    /**
     * Trigger a camera-shake envelope of the given amplitude (peak metres
     * of camPos jitter) for the given duration (seconds). Latches into
     * cameraShakeRemaining_ / cameraShakeDuration_ / cameraShakeAmplitude_
     * which are then consumed by sampleCameraShakeOffset() each frame.
     *
     * Re-trigger semantics: if a shake is already active, the new request
     * MERGES rather than replacing — duration is reset to the larger of
     * (remaining, requested) so the new event extends the envelope, and
     * amplitude is set to the larger of (current, requested) so a bigger
     * impact during a smaller shake escalates the jitter without a smaller
     * later impact dampening the in-progress one. This mirrors how AAA
     * combat games stack camera-shake during multi-kill chains: the player
     * sees ONE compound shake whose amplitude tracks the most violent
     * event in the window, not a chaotic sum-of-shakes that goes wild.
     *
     * Cap: amplitude is clamped to a hard ceiling so a runaway boss-fight
     * scenario (8+ simultaneous kills via AOE spell) cannot push the
     * camera so far it disengages from the player; see implementation
     * for the chosen ceiling and rationale.
     */
    void triggerCameraShake(float amplitudeMeters, float durationSeconds);

    /**
     * Sample the current camera-shake offset for the given game time.
     *
     * Returns vec3(0) if no shake is active (cameraShakeRemaining_ <= 0).
     * Otherwise composes a 3D pseudo-noise from sums of decoupled sin
     * waves at non-commensurate frequencies (so x, y, z each look like
     * independent random walks rather than a single coherent shake
     * along one axis), envelope-modulated by (remaining/duration) so the
     * jitter decays smoothly to zero rather than popping off at the
     * envelope boundary. Time-driven (no per-frame state mutation, no
     * RNG seeded inside the sampler), so the same gameTime + same shake
     * params always produce the same offset — useful for debugging
     * frame dumps and for keeping the sampler safe to call from
     * arbitrary frame paths (no thread-local state, no allocation).
     */
    Engine::vec3 sampleCameraShakeOffset() const;

    // ========================================================================
    // Update Logic
    // ========================================================================

    /**
     * Update all game systems
     */
    void updateSystems(float dt);

    /**
     * Update UI systems
     */
    void updateUI(float dt);

    /**
     * Handle player input
     */
    void handleInput();

    // ========================================================================
    // State Management
    // ========================================================================

    /**
     * Handle state transitions
     */
    void onStateEnter(GameState state);
    void onStateExit(GameState state);

    /**
     * Update different game states
     */
    void updateMainMenu(float dt);
    void updatePlaying(float dt);
    void updatePaused(float dt);
    void updateGameOver(float dt);
    void updateVictory(float dt);

    // ========================================================================
    // Event Handlers
    // ========================================================================

    /**
     * Setup all event handlers
     */
    void setupEventHandlers();

    void onEnemyKilled(const EnemyKilledEvent& event);
    void onQuestCompleted(const QuestCompletedEvent& event);
    void onLevelUp(const LevelUpEvent& event);
    void onDamage(const DamageEvent& event);
    void onEntityDeath(const EntityDeathEvent& event);
    void onWaveComplete(const WaveCompleteEvent& event);

    // Draw the GameOver / Victory overlay via ImGui (composited by UIPass).
    void renderEndScreenOverlay(uint32_t screenWidth, uint32_t screenHeight);

    // ========================================================================
    // Core Systems
    // ========================================================================

    CatEngine::ECS ecs_;
    Engine::Input* input_;
    Engine::Window* window_ = nullptr;
    CatEngine::Renderer::Renderer* renderer_;
    CatEngine::AudioEngine* audioEngine_;
    Engine::ImGuiLayer* imguiLayer_ = nullptr;
    GameEventBus eventBus_;

    // Touch input system. Owned by the game so it lives as long as the
    // GLFW callbacks it installs do. On desktop builds it operates in
    // mouse-simulation mode; on mobile builds the same object picks up
    // real touch events from the platform's GLFW backend.
    std::unique_ptr<Engine::TouchInput> touchInput_;

    // CUDA context (shared between physics and particles)
    std::shared_ptr<CatEngine::CUDA::CudaContext> cudaContext_;

    // Physics and particles
    std::unique_ptr<CatEngine::Physics::PhysicsWorld> physicsWorld_;
    std::shared_ptr<CatEngine::CUDA::ParticleSystem> particleSystem_;

    // ========================================================================
    // Game Systems (in update order)
    // ========================================================================

    // Core gameplay systems
    PlayerControlSystem* playerControlSystem_ = nullptr;
    CombatSystem* combatSystem_ = nullptr;
    EnemyAISystem* enemyAISystem_ = nullptr;
    WaveSystem* waveSystem_ = nullptr;
    HealthSystem* healthSystem_ = nullptr;
    ProjectileSystem* projectileSystem_ = nullptr;

    // Advanced gameplay systems
    std::unique_ptr<StoryModeSystem> storyModeSystem_;
    std::unique_ptr<QuestSystem> questSystem_;
    std::unique_ptr<ElementalMagicSystem> magicSystem_;
    std::unique_ptr<DayNightCycleSystem> dayNightSystem_;
    std::unique_ptr<CatCustomizationSystem> customizationSystem_;
    std::unique_ptr<LevelingSystem> levelingSystem_;
    std::unique_ptr<NPCSystem> npcSystem_;
    std::unique_ptr<DialogSystem> dialogSystem_;
    std::unique_ptr<MerchantSystem> merchantSystem_;
    std::unique_ptr<Game::MobileControlsSystem> mobileControls_;

    // ========================================================================
    // World and Audio
    // ========================================================================

    std::unique_ptr<GameWorld> gameWorld_;
    std::unique_ptr<Game::GameAudio> gameAudio_;
    std::unique_ptr<Engine::SaveSystem> saveSystem_;

    // Non-owning pointer to the main-loop-owned GameConfig. Menus read
    // starting values and write through here when the Settings panel's
    // Close button is clicked. May be null in harnesses that construct
    // the game without a full config instance.
    Game::GameConfig* gameConfig_ = nullptr;

    // ========================================================================
    // UI Systems
    // ========================================================================

    // GameUI owns the actually-rendered HUD / MainMenu / PauseMenu / WavePopup
    // screens (see game/ui/GameUI.hpp). The hud_ / mainMenu_ / pauseMenu_
    // members below are non-owning convenience aliases into that instance so
    // the many notification / render / input call sites in this file can
    // continue to read as `hud_->showNotification(...)` without threading
    // `gameUI_->getHUD()` through every call. The aliases are assigned once
    // in initializeUI() after gameUI_->initialize() succeeds and are cleared
    // in shutdown() before gameUI_ itself is destroyed, so they never
    // outlive the screens they point at.
    //
    // This layout fixes the pre-refactor bug where hud_ / mainMenu_ /
    // pauseMenu_ were independently-owned unique_ptr<HUD/MainMenu/PauseMenu>
    // instances — each screen was then constructed and initialized twice
    // (once here, once inside GameUI), the double HUD was rendered every
    // frame during Playing, and the PauseMenu callbacks (Resume, Main Menu,
    // Quit) were wired onto the legacy free-standing instance while input
    // was routed to the GameUI-owned one, so the pause buttons did nothing.
    Game::HUD* hud_ = nullptr;
    Game::MainMenu* mainMenu_ = nullptr;
    Game::PauseMenu* pauseMenu_ = nullptr;
    std::unique_ptr<Game::GameUI> gameUI_;

    // ========================================================================
    // Game State
    // ========================================================================

    GameState currentState_ = GameState::MainMenu;
    GameState previousState_ = GameState::MainMenu;
    GameMode currentMode_ = GameMode::Menu;

    CatEngine::Entity playerEntity_;
    bool isStoryMode_ = false;

    // Game statistics
    float gameTime_ = 0.0F;
    float totalPlayTime_ = 0.0F;
    int enemiesKilled_ = 0;
    int waveNumber_ = 0;
    int currentSaveSlot_ = -1;

    // Particle effects
    uint32_t deathEmitterId_ = 0;

    // Companion to deathEmitterId_, fired from the CombatSystem onHitCallback
    // wired in connectSystemEvents(). One shared dormant emitter, re-enabled
    // per non-killing hit. Killing blows skip this and use the death burst
    // instead (see spawnHitParticles for the gating rationale).
    uint32_t hitEmitterId_ = 0;

    // ========================================================================
    // Camera shake (kill-feedback finishing touch)
    // ========================================================================
    //
    // Three packed scalars driving a single envelope-modulated 3D-noise jitter
    // applied to camPos in the per-frame camera setup. cameraShakeRemaining_
    // counts down each frame in updateSystems; while it's > 0,
    // sampleCameraShakeOffset() composes an offset whose amplitude scales by
    // (remaining/duration) so the shake decays smoothly to zero instead of
    // popping off when the timer trips zero.
    //
    // We store amplitude AND duration (rather than re-deriving duration from
    // a fixed constant) so triggerCameraShake() can scale the envelope per
    // event class — boss-kill shake gets longer + bigger, regular-dog kill
    // stays small + brief — without baking those choices into a switch
    // statement at the trigger site. The current callers all pass the same
    // duration (kept as data so future per-element tuning slots in cleanly).
    //
    // Why not a separate CameraShakeSystem: shake is purely camera-pose
    // mutation, has no ECS-component-shaped state (no per-entity field —
    // it's a single global property of the rendering camera), and the
    // existing camera setup at update()'s draw-list build is the single
    // canonical pose-write site. A dedicated System would be one more
    // global float trio + a dispatcher to copy into camPos each frame, vs
    // this layout where the shake math lives next to the camera math it
    // modifies.
    float cameraShakeRemaining_ = 0.0F;   // seconds left until envelope ends
    float cameraShakeDuration_ = 0.0F;    // total envelope duration (envelope normaliser)
    float cameraShakeAmplitude_ = 0.0F;   // peak offset in metres (decays via envelope)

    // Initialization flag
    bool initialized_ = false;

    // Has the terrain mesh been handed to the ScenePass yet?
    bool terrainUploadedToScenePass_ = false;

    // Has the particle system pointer been bound to the ScenePass yet?
    // Mirrors the lazy SetTerrain pattern: in `update()` once we observe a
    // live particle system AND a non-null ScenePass, we hand the pointer over
    // exactly once. This lets ScenePass's ribbon-trail path read live counts
    // and (in iteration 3d sub-task b) launch the device build kernel.
    bool particleSystemBoundToScenePass_ = false;
};

} // namespace CatGame
