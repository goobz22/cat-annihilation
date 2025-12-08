#include "CatAnnihilation.hpp"
#include "entities/CatEntity.hpp"
#include "components/GameComponents.hpp"
#include "config/GameplayConfig.hpp"
#include "../engine/core/Logger.hpp"
#include "../engine/math/Vector.hpp"

namespace CatGame {

// ============================================================================
// Constructor / Destructor
// ============================================================================

CatAnnihilation::CatAnnihilation(Engine::Input* input,
                                 CatEngine::Renderer::Renderer* renderer,
                                 CatEngine::AudioEngine* audioEngine)
    : input_(input)
    , renderer_(renderer)
    , audioEngine_(audioEngine)
    , playerEntity_(CatEngine::NULL_ENTITY)
{
    Engine::Logger::info("CatAnnihilation game class created");
}

CatAnnihilation::~CatAnnihilation() {
    if (initialized_) {
        shutdown();
    }
}

// ============================================================================
// Initialization
// ============================================================================

bool CatAnnihilation::initialize() {
    if (initialized_) {
        Engine::Logger::warn("Game already initialized");
        return true;
    }

    Engine::Logger::info("Initializing Cat Annihilation game...");

    // Step 1: Initialize physics world
    physicsWorld_ = std::make_unique<CatEngine::PhysicsWorld>();
    physicsWorld_->Initialize();
    Engine::Logger::info("Physics world initialized");

    // Step 2: Initialize particle system
    particleSystem_ = std::make_unique<CatEngine::ParticleSystem>();
    particleSystem_->Initialize(10000); // Max 10,000 particles
    Engine::Logger::info("Particle system initialized");

    // Step 3: Initialize game systems (in dependency order)
    initializeSystems();

    // Step 4: Initialize UI
    initializeUI();

    // Step 5: Load game data and assets
    loadGameData();
    loadAssets();

    // Step 6: Setup event handlers
    setupEventHandlers();
    connectSystemEvents();

    // Step 7: Set initial state
    setState(GameState::MainMenu);
    setGameMode(GameMode::Menu);

    initialized_ = true;
    Engine::Logger::info("Cat Annihilation game initialized successfully");
    return true;
}

void CatAnnihilation::initializeSystems() {
    Engine::Logger::info("Initializing game systems...");

    // Core gameplay systems (managed by ECS)
    playerControlSystem_ = ecs_.createSystem<PlayerControlSystem>(input_, 0);
    combatSystem_ = ecs_.createSystem<CombatSystem>(10);
    healthSystem_ = ecs_.createSystem<HealthSystem>(15);
    projectileSystem_ = ecs_.createSystem<ProjectileSystem>(20);
    enemyAISystem_ = ecs_.createSystem<EnemyAISystem>(25);
    waveSystem_ = ecs_.createSystem<WaveSystem>(30);

    Engine::Logger::info("Core gameplay systems created");

    // Advanced gameplay systems (standalone)
    dayNightSystem_ = std::make_unique<DayNightCycleSystem>();
    dayNightSystem_->initialize();
    dayNightSystem_->setDayLength(GameplayConfig::DayNight::DAY_LENGTH_SECONDS);

    magicSystem_ = std::make_unique<ElementalMagicSystem>(35);
    magicSystem_->init(&ecs_);

    levelingSystem_ = std::make_unique<LevelingSystem>();
    levelingSystem_->initialize();

    questSystem_ = std::make_unique<QuestSystem>(40);
    questSystem_->init(&ecs_);

    storyModeSystem_ = std::make_unique<StoryModeSystem>(45);
    storyModeSystem_->init(&ecs_);

    npcSystem_ = std::make_unique<NPCSystem>(50);
    npcSystem_->init(&ecs_);

    dialogSystem_ = std::make_unique<DialogSystem>();
    dialogSystem_->initialize();

    merchantSystem_ = std::make_unique<MerchantSystem>();
    merchantSystem_->initialize();

    customizationSystem_ = std::make_unique<CatCustomizationSystem>();
    customizationSystem_->initialize();

    mobileControls_ = std::make_unique<MobileControlsSystem>();
    mobileControls_->initialize();

    Engine::Logger::info("Advanced gameplay systems initialized");

    // Initialize game world
    gameWorld_ = std::make_unique<GameWorld>();
    gameWorld_->initialize();

    // Initialize game audio
    gameAudio_ = std::make_unique<GameAudio>(*audioEngine_);
    if (!gameAudio_->initialize("assets/audio/")) {
        Engine::Logger::warn("Failed to load some game audio assets");
    }

    Engine::Logger::info("All game systems initialized");
}

void CatAnnihilation::initializeUI() {
    Engine::Logger::info("Initializing UI systems...");

    // Create main UI container
    gameUI_ = std::make_unique<GameUI>(*input_, *gameAudio_);
    if (!gameUI_->initialize()) {
        Engine::Logger::error("Failed to initialize game UI");
        return;
    }

    // Create individual UI components
    hud_ = std::make_unique<HUD>();
    hud_->initialize();

    mainMenu_ = std::make_unique<MainMenu>();
    mainMenu_->initialize();

    pauseMenu_ = std::make_unique<PauseMenu>();
    pauseMenu_->initialize();

    // Setup UI callbacks
    mainMenu_->setStartGameCallback([this]() {
        startNewGame(false); // Start arcade mode
    });

    mainMenu_->setStartStoryCallback([this]() {
        startNewGame(true); // Start story mode
    });

    mainMenu_->setContinueCallback([this]() {
        continueGame();
    });

    mainMenu_->setQuitCallback([this]() {
        // Handled by main loop
    });

    pauseMenu_->setResumeCallback([this]() {
        unpause();
    });

    pauseMenu_->setMainMenuCallback([this]() {
        quitToMenu();
    });

    pauseMenu_->setQuitCallback([this]() {
        // Handled by main loop
    });

    Engine::Logger::info("UI systems initialized");
}

void CatAnnihilation::loadGameData() {
    Engine::Logger::info("Loading game data...");

    // Load quest data
    if (questSystem_) {
        questSystem_->loadQuestData("assets/data/quests.json");
    }

    // Load NPC data
    if (npcSystem_) {
        npcSystem_->loadNPCData("assets/data/npcs.json");
    }

    // Load spell definitions
    if (magicSystem_) {
        magicSystem_->loadSpellDefinitions("assets/data/spells.json");
    }

    Engine::Logger::info("Game data loaded");
}

void CatAnnihilation::loadAssets() {
    Engine::Logger::info("Loading game assets...");

    // Load audio
    if (gameAudio_) {
        gameAudio_->loadMusicTrack("menu", "assets/audio/music/menu.ogg");
        gameAudio_->loadMusicTrack("gameplay", "assets/audio/music/gameplay.ogg");
        gameAudio_->loadMusicTrack("combat", "assets/audio/music/combat.ogg");
        gameAudio_->loadMusicTrack("victory", "assets/audio/music/victory.ogg");
        gameAudio_->loadMusicTrack("gameover", "assets/audio/music/gameover.ogg");
    }

    // Load world assets
    if (gameWorld_) {
        gameWorld_->loadAssets();
    }

    Engine::Logger::info("Assets loaded");
}

void CatAnnihilation::connectSystemEvents() {
    Engine::Logger::info("Connecting system events...");

    // Connect combat system events to quest system
    if (combatSystem_ && questSystem_) {
        combatSystem_->setOnKillCallback([this](CatEngine::Entity killer, CatEngine::Entity killed) {
            // This will be handled by event bus
        });
    }

    // Connect wave system to quest system
    if (waveSystem_) {
        // Wave completion events will be published to event bus
    }

    Engine::Logger::info("System events connected");
}

void CatAnnihilation::setupEventHandlers() {
    Engine::Logger::info("Setting up event handlers...");

    // Enemy killed -> Quest progress, XP gain, Loot
    eventBus_.subscribe<EnemyKilledEvent>([this](const EnemyKilledEvent& e) {
        onEnemyKilled(e);
    });

    // Quest completed -> Rewards, Story progress
    eventBus_.subscribe<QuestCompletedEvent>([this](const QuestCompletedEvent& e) {
        onQuestCompleted(e);
    });

    // Level up -> Ability unlock, UI notification
    eventBus_.subscribe<LevelUpEvent>([this](const LevelUpEvent& e) {
        onLevelUp(e);
    });

    // Damage taken -> UI update, Status effects, Death check
    eventBus_.subscribe<DamageEvent>([this](const DamageEvent& e) {
        onDamage(e);
    });

    // Entity death -> Cleanup, Effects
    eventBus_.subscribe<EntityDeathEvent>([this](const EntityDeathEvent& e) {
        onEntityDeath(e);
    });

    // Wave complete -> XP reward, UI notification
    eventBus_.subscribe<WaveCompleteEvent>([this](const WaveCompleteEvent& e) {
        onWaveComplete(e);
    });

    Engine::Logger::info("Event handlers set up");
}

// ============================================================================
// Shutdown
// ============================================================================

void CatAnnihilation::shutdown() {
    if (!initialized_) {
        return;
    }

    Engine::Logger::info("Shutting down Cat Annihilation game...");

    // Shutdown UI
    if (gameUI_) gameUI_->shutdown();
    if (hud_) hud_->shutdown();
    if (mainMenu_) mainMenu_->shutdown();
    if (pauseMenu_) pauseMenu_->shutdown();

    // Shutdown game systems
    if (gameAudio_) gameAudio_->shutdown();
    if (gameWorld_) gameWorld_->shutdown();
    if (customizationSystem_) customizationSystem_->shutdown();
    if (merchantSystem_) merchantSystem_->shutdown();
    if (dialogSystem_) dialogSystem_->shutdown();
    if (npcSystem_) npcSystem_->shutdown();
    if (storyModeSystem_) storyModeSystem_->shutdown();
    if (questSystem_) questSystem_->shutdown();
    if (levelingSystem_) levelingSystem_->shutdown();
    if (magicSystem_) magicSystem_->shutdown();
    if (dayNightSystem_) dayNightSystem_->shutdown();

    // Clear particle system
    if (particleSystem_) particleSystem_->Shutdown();

    // Clear physics world
    if (physicsWorld_) physicsWorld_->Shutdown();

    // Clean up ECS (this will clean up all ECS systems and entities)
    ecs_.clear();

    // Reset pointers
    playerControlSystem_ = nullptr;
    combatSystem_ = nullptr;
    enemyAISystem_ = nullptr;
    waveSystem_ = nullptr;
    healthSystem_ = nullptr;
    projectileSystem_ = nullptr;

    // Clear event bus
    eventBus_.clear();

    initialized_ = false;
    Engine::Logger::info("Cat Annihilation shutdown complete");
}

// ============================================================================
// Main Update Loop
// ============================================================================

void CatAnnihilation::update(float dt) {
    if (!initialized_) {
        return;
    }

    // Update total playtime
    totalPlayTime_ += dt;

    // Update based on current state
    switch (currentState_) {
        case GameState::MainMenu:
            updateMainMenu(dt);
            break;

        case GameState::Playing:
            updatePlaying(dt);
            break;

        case GameState::Paused:
            updatePaused(dt);
            break;

        case GameState::GameOver:
            updateGameOver(dt);
            break;

        case GameState::Victory:
            updateVictory(dt);
            break;
    }

    // Always update UI
    updateUI(dt);

    // Always update audio
    if (gameAudio_) {
        gameAudio_->update(dt);
    }
}

void CatAnnihilation::updateSystems(float dt) {
    // 1. Handle input
    handleInput();

    // 2. Day/night cycle
    if (dayNightSystem_) {
        dayNightSystem_->update(dt);
    }

    // 3. Dialog system (can pause other systems)
    if (dialogSystem_ && dialogSystem_->isActive()) {
        dialogSystem_->update(dt);
        return; // Don't update other systems during dialog
    }

    // 4. NPC updates
    if (npcSystem_) {
        npcSystem_->update(dt);
    }

    // 5. Update all ECS systems (player, combat, enemies, projectiles, health, waves)
    ecs_.update(dt);

    // 6. Quest progress checks
    if (questSystem_) {
        questSystem_->update(dt);
    }

    // 7. Story mode progression
    if (storyModeSystem_ && isStoryMode_) {
        storyModeSystem_->update(dt);
    }

    // 8. Magic system
    if (magicSystem_) {
        magicSystem_->update(dt);
    }

    // 9. Leveling/XP
    if (levelingSystem_) {
        levelingSystem_->update(dt);
    }

    // 10. Physics step
    if (physicsWorld_) {
        physicsWorld_->Update(dt);
    }

    // 11. Particle updates
    if (particleSystem_) {
        particleSystem_->Update(dt);
    }

    // 12. World updates
    if (gameWorld_) {
        gameWorld_->update(dt);
    }

    // Update game time
    gameTime_ += dt;
}

void CatAnnihilation::updateUI(float dt) {
    if (gameUI_) {
        gameUI_->update(dt);
    }

    // Update HUD if playing
    if (currentState_ == GameState::Playing && hud_) {
        hud_->update(dt);

        // Update HUD with player stats
        if (ecs_.isAlive(playerEntity_)) {
            // Get player health component
            auto healthComp = ecs_.getComponent<HealthComponent>(playerEntity_);
            if (healthComp) {
                hud_->setHealth(healthComp->currentHealth, healthComp->maxHealth);
            }

            // Update other HUD elements
            hud_->setWave(waveNumber_);
            hud_->setEnemiesKilled(enemiesKilled_);
        }
    }
}

void CatAnnihilation::handleInput() {
    // Check for pause
    if (input_->isKeyPressed(Engine::Input::Key::Escape)) {
        if (currentState_ == GameState::Playing) {
            pause();
        } else if (currentState_ == GameState::Paused) {
            unpause();
        }
    }

    // Debug keys (only in debug builds)
    #ifdef _DEBUG
    if (input_->isKeyPressed(Engine::Input::Key::F1)) {
        // Toggle god mode
        Engine::Logger::info("God mode toggled");
    }
    if (input_->isKeyPressed(Engine::Input::Key::F2)) {
        // Spawn test enemy
        Engine::Logger::info("Test enemy spawned");
    }
    if (input_->isKeyPressed(Engine::Input::Key::F3)) {
        // Complete current quest
        Engine::Logger::info("Quest completed (debug)");
    }
    #endif
}

// ============================================================================
// Render
// ============================================================================

void CatAnnihilation::render() {
    if (!initialized_ || !renderer_) {
        return;
    }

    // Render game world
    if (gameWorld_ && (currentState_ == GameState::Playing || currentState_ == GameState::Paused)) {
        gameWorld_->render(*renderer_);
    }

    // Render particles
    if (particleSystem_) {
        particleSystem_->Render();
    }

    // Render UI
    if (gameUI_) {
        gameUI_->render(*renderer_);
    }

    // Render HUD
    if (hud_ && currentState_ == GameState::Playing) {
        hud_->render(*renderer_);
    }

    // Render menu
    if (mainMenu_ && currentState_ == GameState::MainMenu) {
        mainMenu_->render(*renderer_);
    }

    // Render pause menu
    if (pauseMenu_ && currentState_ == GameState::Paused) {
        pauseMenu_->render(*renderer_);
    }
}

// ============================================================================
// State Updates
// ============================================================================

void CatAnnihilation::updateMainMenu(float dt) {
    // Check for start game input
    if (input_->isKeyPressed(Engine::Input::Key::Enter) ||
        input_->isKeyPressed(Engine::Input::Key::Space)) {
        startNewGame();
    }

    (void)dt;
}

void CatAnnihilation::updatePlaying(float dt) {
    // Update all game systems
    updateSystems(dt);

    // Check victory conditions
    if (waveSystem_) {
        // TODO: Check if player reached final wave
    }
}

void CatAnnihilation::updatePaused(float dt) {
    // Check for restart input
    if (input_->isKeyPressed(Engine::Input::Key::R)) {
        restart();
    }

    // Check for quit to menu
    if (input_->isKeyPressed(Engine::Input::Key::Q)) {
        quitToMenu();
    }

    (void)dt;
}

void CatAnnihilation::updateGameOver(float dt) {
    // Check for restart input
    if (input_->isKeyPressed(Engine::Input::Key::R) ||
        input_->isKeyPressed(Engine::Input::Key::Enter) ||
        input_->isKeyPressed(Engine::Input::Key::Space)) {
        restart();
    }

    // Check for quit to menu
    if (input_->isKeyPressed(Engine::Input::Key::Escape) ||
        input_->isKeyPressed(Engine::Input::Key::Q)) {
        quitToMenu();
    }

    (void)dt;
}

void CatAnnihilation::updateVictory(float dt) {
    // Check for continue/restart input
    if (input_->isKeyPressed(Engine::Input::Key::Enter) ||
        input_->isKeyPressed(Engine::Input::Key::Space)) {
        restart();
    }

    // Check for quit to menu
    if (input_->isKeyPressed(Engine::Input::Key::Escape) ||
        input_->isKeyPressed(Engine::Input::Key::Q)) {
        quitToMenu();
    }

    (void)dt;
}

// ============================================================================
// State Management
// ============================================================================

void CatAnnihilation::setState(GameState newState) {
    if (currentState_ == newState) {
        return;
    }

    // Exit current state
    onStateExit(currentState_);

    // Store previous state
    previousState_ = currentState_;
    currentState_ = newState;

    // Enter new state
    onStateEnter(newState);

    Engine::Logger::info("Game state changed to: " + std::to_string(static_cast<int>(newState)));
}

void CatAnnihilation::setGameMode(GameMode newMode) {
    if (currentMode_ == newMode) {
        return;
    }

    currentMode_ = newMode;
    Engine::Logger::info("Game mode changed to: " + std::to_string(static_cast<int>(newMode)));
}

void CatAnnihilation::onStateEnter(GameState state) {
    switch (state) {
        case GameState::MainMenu:
            if (gameAudio_) {
                gameAudio_->playMusic("menu", true);
            }
            break;

        case GameState::Playing:
            // Ensure player entity exists
            if (!ecs_.isAlive(playerEntity_)) {
                createPlayer();
            }

            // Enable player controls
            if (playerControlSystem_) {
                playerControlSystem_->setControlEnabled(true);
            }

            // Start gameplay music
            if (gameAudio_) {
                gameAudio_->playMusic("gameplay", true);
            }

            // Resume time
            if (dayNightSystem_) {
                dayNightSystem_->resumeTime();
            }

            setGameMode(GameMode::Playing);
            break;

        case GameState::Paused:
            // Disable player controls
            if (playerControlSystem_) {
                playerControlSystem_->setControlEnabled(false);
            }

            // Pause time
            if (dayNightSystem_) {
                dayNightSystem_->pauseTime();
            }

            setGameMode(GameMode::Paused);
            break;

        case GameState::GameOver:
            // Disable player controls
            if (playerControlSystem_) {
                playerControlSystem_->setControlEnabled(false);
            }

            // Play game over music
            if (gameAudio_) {
                gameAudio_->playMusic("gameover", false);
            }

            setGameMode(GameMode::GameOver);
            break;

        case GameState::Victory:
            // Disable player controls
            if (playerControlSystem_) {
                playerControlSystem_->setControlEnabled(false);
            }

            // Play victory music
            if (gameAudio_) {
                gameAudio_->playMusic("victory", false);
            }

            setGameMode(GameMode::Victory);
            break;
    }
}

void CatAnnihilation::onStateExit(GameState state) {
    (void)state;
    // Cleanup for previous state if needed
}

void CatAnnihilation::pause() {
    if (currentState_ == GameState::Playing) {
        setState(GameState::Paused);
    }
}

void CatAnnihilation::unpause() {
    if (currentState_ == GameState::Paused) {
        setState(previousState_);
    }
}

void CatAnnihilation::togglePause() {
    if (currentState_ == GameState::Paused) {
        unpause();
    } else if (currentState_ == GameState::Playing) {
        pause();
    }
}

void CatAnnihilation::restart() {
    Engine::Logger::info("Restarting game...");

    // Clear all entities
    ecs_.clearEntities();

    // Reset game statistics
    gameTime_ = 0.0f;
    enemiesKilled_ = 0;
    waveNumber_ = 0;

    // Reset systems
    if (waveSystem_) {
        waveSystem_->reset();
    }

    if (questSystem_) {
        questSystem_->reset();
    }

    if (dayNightSystem_) {
        dayNightSystem_->setTimeOfDay(0.5f); // Noon
    }

    // Recreate player
    createPlayer();

    // Start new game
    setState(GameState::Playing);
}

void CatAnnihilation::startNewGame(bool storyMode) {
    Engine::Logger::info(storyMode ? "Starting story mode..." : "Starting arcade mode...");

    isStoryMode_ = storyMode;

    if (storyMode && storyModeSystem_) {
        // Start story mode with default clan
        storyModeSystem_->startStoryMode(Clan::MistClan);
    }

    restart();
}

void CatAnnihilation::continueGame() {
    Engine::Logger::info("Continuing from last save...");
    // TODO: Load most recent save
    loadGame(0);
}

void CatAnnihilation::quitToMenu() {
    Engine::Logger::info("Returning to main menu...");

    // Clear all entities
    ecs_.clearEntities();

    // Reset statistics
    gameTime_ = 0.0f;
    enemiesKilled_ = 0;
    waveNumber_ = 0;
    isStoryMode_ = false;

    // Go to menu
    setState(GameState::MainMenu);
}

// ============================================================================
// Save/Load
// ============================================================================

bool CatAnnihilation::saveGame(int slotIndex) {
    Engine::Logger::info("Saving game to slot " + std::to_string(slotIndex) + "...");

    // TODO: Implement save system
    // Save player state, quest progress, inventory, etc.

    currentSaveSlot_ = slotIndex;

    // Publish save event
    GameSavedEvent event("slot_" + std::to_string(slotIndex));
    event.playtime = totalPlayTime_;
    event.playerLevel = levelingSystem_ ? levelingSystem_->getPlayerLevel() : 1;
    event.wasAutoSave = false;
    eventBus_.publish(event);

    Engine::Logger::info("Game saved successfully");
    return true;
}

bool CatAnnihilation::loadGame(int slotIndex) {
    Engine::Logger::info("Loading game from slot " + std::to_string(slotIndex) + "...");

    // TODO: Implement load system
    // Load player state, quest progress, inventory, etc.

    currentSaveSlot_ = slotIndex;

    // Publish load event
    GameLoadedEvent event("slot_" + std::to_string(slotIndex));
    event.playtime = totalPlayTime_;
    event.playerLevel = levelingSystem_ ? levelingSystem_->getPlayerLevel() : 1;
    eventBus_.publish(event);

    setState(GameState::Playing);

    Engine::Logger::info("Game loaded successfully");
    return true;
}

// ============================================================================
// Helper Functions
// ============================================================================

void CatAnnihilation::createPlayer() {
    // Create player cat entity at spawn position
    Engine::vec3 spawnPosition(0.0f, 0.0f, 0.0f);
    playerEntity_ = CatEntity::create(ecs_, spawnPosition);

    // Set player entity in control system
    if (playerControlSystem_) {
        playerControlSystem_->setPlayerEntity(playerEntity_);
    }

    Engine::Logger::info("Player entity created");
}

// ============================================================================
// Event Handlers
// ============================================================================

void CatAnnihilation::onEnemyKilled(const EnemyKilledEvent& event) {
    // Update statistics
    enemiesKilled_++;

    // Award XP to killer
    if (levelingSystem_ && event.killer == playerEntity_) {
        levelingSystem_->addExperience(event.xpReward);
    }

    // Update quest progress
    if (questSystem_) {
        questSystem_->onEnemyKilled(event.enemyType, 1);
    }

    // Update story mode progress
    if (storyModeSystem_ && isStoryMode_) {
        storyModeSystem_->addExperience(event.xpReward);
    }

    // Play kill sound
    if (gameAudio_) {
        gameAudio_->playSoundEffect("enemy_death", event.position);
    }

    Engine::Logger::debug("Enemy killed: " + event.enemyType + " (+" + std::to_string(event.xpReward) + " XP)");
}

void CatAnnihilation::onQuestCompleted(const QuestCompletedEvent& event) {
    // Award XP
    if (levelingSystem_) {
        levelingSystem_->addExperience(event.xpReward);
    }

    // Award currency
    // TODO: Add currency to player inventory

    // Update story progress if in story mode
    if (storyModeSystem_ && isStoryMode_ && event.wasMainQuest) {
        // Advance story
    }

    // Play quest complete sound
    if (gameAudio_) {
        gameAudio_->playSoundEffect("quest_complete");
    }

    // Show UI notification
    if (hud_) {
        hud_->showNotification("Quest Complete: " + event.questName);
    }

    Engine::Logger::info("Quest completed: " + event.questName + " (+" + std::to_string(event.xpReward) + " XP)");
}

void CatAnnihilation::onLevelUp(const LevelUpEvent& event) {
    // Update player stats
    if (ecs_.isAlive(playerEntity_)) {
        auto healthComp = ecs_.getComponent<HealthComponent>(playerEntity_);
        if (healthComp) {
            healthComp->maxHealth += GameplayConfig::Player::HEALTH_PER_LEVEL;
            healthComp->currentHealth = healthComp->maxHealth; // Full heal on level up
        }
    }

    // Play level up sound
    if (gameAudio_) {
        gameAudio_->playSoundEffect("level_up");
    }

    // Show UI notification
    if (hud_) {
        hud_->showNotification("Level Up! Level " + std::to_string(event.newLevel));
    }

    Engine::Logger::info("Level up! New level: " + std::to_string(event.newLevel));
}

void CatAnnihilation::onDamage(const DamageEvent& event) {
    // Update HUD damage indicator
    if (hud_ && event.target == playerEntity_) {
        hud_->showDamageIndicator(event.amount);
    }

    // Play damage sound
    if (gameAudio_) {
        if (event.target == playerEntity_) {
            gameAudio_->playSoundEffect("player_hurt");
        } else {
            gameAudio_->playSoundEffect("enemy_hurt", event.hitPosition);
        }
    }

    // Show damage number
    if (hud_) {
        hud_->spawnDamageNumber(event.hitPosition, event.amount, event.wasCritical);
    }
}

void CatAnnihilation::onEntityDeath(const EntityDeathEvent& event) {
    // Check if player died
    if (event.entity == playerEntity_) {
        Engine::Logger::info("Player died!");
        setState(GameState::GameOver);
    } else {
        // Enemy death - already handled by EnemyKilledEvent
    }

    // Spawn death particles
    if (particleSystem_) {
        // TODO: Spawn death effect particles
    }
}

void CatAnnihilation::onWaveComplete(const WaveCompleteEvent& event) {
    waveNumber_ = event.waveNumber;

    // Award XP
    if (levelingSystem_) {
        levelingSystem_->addExperience(event.xpReward);
    }

    // Play wave complete sound
    if (gameAudio_) {
        gameAudio_->playSoundEffect("wave_complete");
    }

    // Show UI notification
    if (hud_) {
        hud_->showWaveComplete(event.waveNumber, event.xpReward);
    }

    Engine::Logger::info("Wave " + std::to_string(event.waveNumber) + " complete! (+" + std::to_string(event.xpReward) + " XP)");
}

} // namespace CatGame
