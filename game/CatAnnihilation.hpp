#pragma once

#include "../engine/ecs/ECS.hpp"
#include "../engine/ecs/Entity.hpp"
#include "../engine/core/Input.hpp"
#include "../engine/renderer/Renderer.hpp"
#include "../engine/audio/AudioEngine.hpp"
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
#include "world/GameWorld.hpp"
#include "game_events.hpp"
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
    explicit CatAnnihilation(Engine::Input* input,
                            CatEngine::Renderer::Renderer* renderer,
                            CatEngine::AudioEngine* audioEngine);

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

    // ========================================================================
    // Core Systems
    // ========================================================================

    CatEngine::ECS ecs_;
    Engine::Input* input_;
    CatEngine::Renderer::Renderer* renderer_;
    CatEngine::AudioEngine* audioEngine_;
    GameEventBus eventBus_;

    // Physics and particles
    std::unique_ptr<CatEngine::PhysicsWorld> physicsWorld_;
    std::unique_ptr<CatEngine::ParticleSystem> particleSystem_;

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
    std::unique_ptr<MobileControlsSystem> mobileControls_;

    // ========================================================================
    // World and Audio
    // ========================================================================

    std::unique_ptr<GameWorld> gameWorld_;
    std::unique_ptr<GameAudio> gameAudio_;

    // ========================================================================
    // UI Systems
    // ========================================================================

    std::unique_ptr<HUD> hud_;
    std::unique_ptr<MainMenu> mainMenu_;
    std::unique_ptr<PauseMenu> pauseMenu_;
    std::unique_ptr<GameUI> gameUI_;

    // ========================================================================
    // Game State
    // ========================================================================

    GameState currentState_ = GameState::MainMenu;
    GameState previousState_ = GameState::MainMenu;
    GameMode currentMode_ = GameMode::Menu;

    CatEngine::Entity playerEntity_;
    bool isStoryMode_ = false;

    // Game statistics
    float gameTime_ = 0.0f;
    float totalPlayTime_ = 0.0f;
    int enemiesKilled_ = 0;
    int waveNumber_ = 0;
    int currentSaveSlot_ = -1;

    // Initialization flag
    bool initialized_ = false;
};

} // namespace CatGame
