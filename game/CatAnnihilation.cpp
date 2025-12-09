#include "CatAnnihilation.hpp"
#include "entities/CatEntity.hpp"
#include "components/GameComponents.hpp"
#include "config/GameplayConfig.hpp"
#include "../engine/core/Logger.hpp"
#include "../engine/math/Vector.hpp"
#include "../engine/cuda/particles/ParticleEmitter.hpp"
#include <iostream>

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

    // Step 1: Initialize CUDA context
    cudaContext_ = std::make_shared<CatEngine::CUDA::CudaContext>();
    Engine::Logger::info("CUDA context initialized");

    // Step 2: Initialize physics world
    // Increase maxBodies to handle terrain + forest trees (default was 10000)
    constexpr int MAX_PHYSICS_BODIES = 20000;
    constexpr int MAX_CONTACTS = 100000;
    physicsWorld_ = std::make_unique<CatEngine::Physics::PhysicsWorld>(*cudaContext_, MAX_PHYSICS_BODIES, MAX_CONTACTS);
    physicsWorld_->setGravity(Engine::vec3(0.0F, -9.81F, 0.0F));
    Engine::Logger::info("Physics world initialized (maxBodies=" + std::to_string(MAX_PHYSICS_BODIES) + ", maxContacts=" + std::to_string(MAX_CONTACTS) + ")");

    // Step 3: Initialize particle system
    CatEngine::CUDA::ParticleSystem::Config particleConfig;
    particleConfig.maxParticles = 100000;
    particleConfig.enableSorting = true;
    particleConfig.enableCompaction = true;
    particleSystem_ = std::make_shared<CatEngine::CUDA::ParticleSystem>(*cudaContext_, particleConfig);
    Engine::Logger::info("Particle system initialized");

    // Step 4: Initialize game systems (in dependency order)
    initializeSystems();

    // Step 5: Initialize UI
    initializeUI();

    // Step 6: Load game data and assets
    loadGameData();
    loadAssets();

    // Step 7: Setup event handlers
    setupEventHandlers();
    connectSystemEvents();

    // Step 8: Set initial state
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

    magicSystem_ = std::make_unique<ElementalMagicSystem>(particleSystem_, 35);
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
    dialogSystem_->init(&ecs_);

    merchantSystem_ = std::make_unique<MerchantSystem>();
    merchantSystem_->init(&ecs_);

    customizationSystem_ = std::make_unique<CatCustomizationSystem>();
    customizationSystem_->initialize();

    mobileControls_ = std::make_unique<Game::MobileControlsSystem>();

    Engine::Logger::info("Advanced gameplay systems initialized");

    // Initialize game world
    gameWorld_ = std::make_unique<GameWorld>(*cudaContext_, *physicsWorld_);
    gameWorld_->initialize();

    // Initialize game audio
    gameAudio_ = std::make_unique<Game::GameAudio>(*audioEngine_);
    if (!gameAudio_->initialize("assets/audio/")) {
        Engine::Logger::warn("Failed to load some game audio assets");
    }

    // Initialize save system
    saveSystem_ = std::make_unique<Engine::SaveSystem>();
    saveSystem_->initialize();

    // Create death particle emitter for reuse
    createDeathParticleEmitter();

    Engine::Logger::info("All game systems initialized");
}

void CatAnnihilation::createDeathParticleEmitter() {
    // Create a reusable death effect emitter
    CatEngine::CUDA::ParticleEmitter deathEmitter;
    deathEmitter.enabled = false;  // Disabled by default, triggered on death
    deathEmitter.shape = CatEngine::CUDA::EmissionShape::Sphere;
    deathEmitter.mode = CatEngine::CUDA::EmissionMode::OneShot;
    deathEmitter.shapeParams.sphereRadius = 0.5F;
    deathEmitter.shapeParams.sphereEmitFromShell = true;
    deathEmitter.burstEnabled = true;
    deathEmitter.burstCount = 50;

    // Particle properties for death effect
    deathEmitter.initialProperties.velocityMin = Engine::vec3(-3.0F, 1.0F, -3.0F);
    deathEmitter.initialProperties.velocityMax = Engine::vec3(3.0F, 5.0F, 3.0F);
    deathEmitter.initialProperties.lifetimeMin = 0.5F;
    deathEmitter.initialProperties.lifetimeMax = 1.5F;
    deathEmitter.initialProperties.sizeMin = 0.05F;
    deathEmitter.initialProperties.sizeMax = 0.15F;
    deathEmitter.initialProperties.colorBase = Engine::vec4(1.0F, 0.3F, 0.1F, 1.0F);      // Orange-red base
    deathEmitter.initialProperties.colorVariation = Engine::vec4(0.2F, 0.1F, 0.05F, 0.0F); // Slight variation
    deathEmitter.fadeOutAlpha = true;  // Fade out over lifetime
    deathEmitter.scaleOverLifetime = true;
    deathEmitter.endScale = 0.0F;      // Shrink to nothing

    if (particleSystem_ != nullptr) {
        deathEmitterId_ = particleSystem_->addEmitter(deathEmitter);
    }
}

void CatAnnihilation::initializeUI() {
    Engine::Logger::info("Initializing UI systems...");

    // Create main UI container
    gameUI_ = std::make_unique<Game::GameUI>(*input_, *gameAudio_);
    if (!gameUI_->initialize()) {
        Engine::Logger::error("Failed to initialize game UI");
        return;
    }

    // Create individual UI components
    hud_ = std::make_unique<Game::HUD>(*input_, *gameAudio_);
    hud_->initialize();

    mainMenu_ = std::make_unique<Game::MainMenu>(*input_, *gameAudio_);
    mainMenu_->initialize();

    pauseMenu_ = std::make_unique<Game::PauseMenu>(*input_, *gameAudio_);
    pauseMenu_->initialize();

    // Setup UI callbacks
    if (mainMenu_ != nullptr) {
        mainMenu_->setStartGameCallback([this]() {
            startNewGame(false); // Start arcade mode
        });

        mainMenu_->setContinueCallback([this]() {
            continueGame();
        });

        mainMenu_->setQuitCallback([]() {
            // Handled by main loop
        });
    }

    if (pauseMenu_ != nullptr) {
        pauseMenu_->setResumeCallback([this]() {
            unpause();
        });

        pauseMenu_->setMainMenuCallback([this]() {
            quitToMenu();
        });

        pauseMenu_->setQuitCallback([]() {
            // Handled by main loop
        });
    }

    Engine::Logger::info("UI systems initialized");
}

void CatAnnihilation::loadGameData() {
    Engine::Logger::info("Loading game data...");

    // Load quest data
    if (questSystem_ != nullptr) {
        questSystem_->loadQuestsFromFile("assets/data/quests.json");
    }

    // Load NPC data
    if (npcSystem_ != nullptr) {
        npcSystem_->loadNPCsFromFile("assets/npcs/npcs.json");
    }

    // Load spell definitions
    // Note: ElementalMagicSystem::loadSpellDefinitions() is private and called internally during init()
    // No explicit call needed here - spell definitions are loaded as part of magicSystem_->init()

    Engine::Logger::info("Game data loaded");
}

void CatAnnihilation::loadAssets() {
    Engine::Logger::info("Loading game assets...");

    // Load audio
    // Note: Audio files are loaded in GameAudio::initialize()
    // No need to load music tracks here

    // Load world assets
    // Note: GameWorld loads assets internally during initialize() - no separate loadAssets() call needed

    Engine::Logger::info("Assets loaded");
}

void CatAnnihilation::connectSystemEvents() {
    Engine::Logger::info("Connecting system events...");

    // Connect combat system events to quest system
    if (combatSystem_ != nullptr && questSystem_ != nullptr) {
        combatSystem_->setOnKillCallback([](CatEngine::Entity /*killer*/, CatEngine::Entity /*killed*/) {
            // This will be handled by event bus
        });
    }

    // Connect wave system to quest system
    // Wave completion events will be published to event bus

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
    if (gameUI_ != nullptr) { gameUI_->shutdown(); }
    if (hud_ != nullptr) { hud_->shutdown(); }
    if (mainMenu_ != nullptr) { mainMenu_->shutdown(); }
    if (pauseMenu_ != nullptr) { pauseMenu_->shutdown(); }

    // Shutdown game systems
    if (gameAudio_ != nullptr) { gameAudio_->shutdown(); }
    // Note: GameWorld, MerchantSystem, DialogSystem, NPCSystem, StoryModeSystem, QuestSystem,
    // LevelingSystem, MagicSystem, and DayNightCycleSystem are cleaned up by their destructors
    // when their unique_ptrs are released. The System base class does not define a shutdown() method.
    if (customizationSystem_ != nullptr) { customizationSystem_->shutdown(); }

    // Note: ParticleSystem and PhysicsWorld cleanup is handled by their destructors (RAII pattern)

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
    if (gameAudio_ != nullptr) {
        gameAudio_->update(dt);
    }
}

void CatAnnihilation::updateSystems(float dt) {
    // 1. Handle input
    handleInput();

    // 2. Day/night cycle
    if (dayNightSystem_ != nullptr) {
        dayNightSystem_->update(dt);
    }

    // 3. Dialog system (can pause other systems)
    if (dialogSystem_ != nullptr && dialogSystem_->isInDialog()) {
        dialogSystem_->update(dt);
        return; // Don't update other systems during dialog
    }

    // 4. NPC updates
    if (npcSystem_ != nullptr) {
        npcSystem_->update(dt);
    }

    // 5. Update all ECS systems (player, combat, enemies, projectiles, health, waves)
    ecs_.update(dt);

    // 6. Quest progress checks
    if (questSystem_ != nullptr) {
        questSystem_->update(dt);
    }

    // 7. Story mode progression
    if (storyModeSystem_ != nullptr && isStoryMode_) {
        storyModeSystem_->update(dt);
    }

    // 8. Magic system
    if (magicSystem_ != nullptr) {
        magicSystem_->update(dt);
    }

    // 9. Leveling/XP
    if (levelingSystem_ != nullptr) {
        levelingSystem_->update(dt);
    }

    // 10. Physics step
    if (physicsWorld_ != nullptr) {
        physicsWorld_->step(dt);
    }

    // 11. Particle updates
    if (particleSystem_ != nullptr) {
        particleSystem_->update(dt);
    }

    // 12. World updates
    if (gameWorld_ != nullptr) {
        gameWorld_->update(dt);
    }

    // Update game time
    gameTime_ += dt;
}

void CatAnnihilation::updateUI(float dt) {
    if (gameUI_ != nullptr) {
        gameUI_->update(dt);
    }

    // Update HUD if playing
    if (currentState_ == GameState::Playing && hud_ != nullptr) {
        hud_->update(dt);

        // Update HUD with player stats
        if (ecs_.isAlive(playerEntity_)) {
            // Get player health component
            auto* healthComp = ecs_.getComponent<HealthComponent>(playerEntity_);
            if (healthComp != nullptr) {
                hud_->setHealth(healthComp->currentHealth, healthComp->maxHealth);
            }

            // Update other HUD elements
            hud_->setWave(waveNumber_);
            hud_->setScore(enemiesKilled_); // Use score to display enemies killed
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
    static int renderCallCount = 0;
    renderCallCount++;
    if (renderCallCount <= 5) {
        std::cout << "[CatAnnihilation::render] called, count=" << renderCallCount << "\n";
        std::cout.flush();
    }

    if (!initialized_ || renderer_ == nullptr) {
        if (renderCallCount <= 5) {
            std::cout << "[CatAnnihilation::render] not initialized or no renderer\n";
            std::cout.flush();
        }
        return;
    }

    // Render game world
    // Note: World geometry and entities are rendered by the Renderer using scene graph data.
    // GameWorld provides terrain/collision data, not direct rendering calls.

    // Render particles
    // Note: ParticleSystem provides GPU buffer pointers via getRenderData() for the Renderer
    // to draw particles using compute shader output. No direct Render() call needed.

    // Get UIPass for 2D rendering
    if (renderCallCount <= 5) {
        std::cout << "[CatAnnihilation::render] getting UIPass...\n";
        std::cout.flush();
    }
    auto* uiPass = renderer_->GetUIPass();
    if (uiPass == nullptr) {
        if (renderCallCount <= 5) {
            std::cout << "[CatAnnihilation::render] UIPass is null, returning\n";
            std::cout.flush();
        }
        return;
    }

    // Get screen dimensions
    uint32_t screenWidth = renderer_->GetWidth();
    uint32_t screenHeight = renderer_->GetHeight();
    if (renderCallCount <= 5) {
        std::cout << "[CatAnnihilation::render] screen " << screenWidth << "x" << screenHeight << "\n";
        std::cout.flush();
    }

    // Begin UI frame - MUST be called before any DrawQuad/DrawText calls
    uiPass->BeginFrame();

    // Render UI
    if (gameUI_ != nullptr) {
        if (renderCallCount <= 5) {
            std::cout << "[CatAnnihilation::render] rendering gameUI_...\n";
            std::cout.flush();
        }
        gameUI_->render(*uiPass, screenWidth, screenHeight);
    }

    // Render HUD
    if (hud_ != nullptr && currentState_ == GameState::Playing) {
        if (renderCallCount <= 5) {
            std::cout << "[CatAnnihilation::render] rendering HUD...\n";
            std::cout.flush();
        }
        hud_->render(*uiPass, screenWidth, screenHeight);
    }

    // Render menu
    if (mainMenu_ != nullptr && currentState_ == GameState::MainMenu) {
        if (renderCallCount <= 5) {
            std::cout << "[CatAnnihilation::render] rendering mainMenu_... state=" << static_cast<int>(currentState_) << "\n";
            std::cout.flush();
        }
        mainMenu_->render(*uiPass, screenWidth, screenHeight);
        if (renderCallCount <= 5) {
            std::cout << "[CatAnnihilation::render] mainMenu_->render() DONE\n";
            std::cout.flush();
        }
    }

    // Render pause menu
    if (pauseMenu_ != nullptr && currentState_ == GameState::Paused) {
        if (renderCallCount <= 5) {
            std::cout << "[CatAnnihilation::render] rendering pauseMenu_...\n";
            std::cout.flush();
        }
        pauseMenu_->render(*uiPass, screenWidth, screenHeight);
        if (renderCallCount <= 5) {
            std::cout << "[CatAnnihilation::render] pauseMenu_->render() DONE\n";
            std::cout.flush();
        }
    }

    // End UI frame - batches and sorts draw commands
    if (renderCallCount <= 5) {
        std::cout << "[CatAnnihilation::render] calling uiPass->EndFrame()...\n";
        std::cout.flush();
    }
    uiPass->EndFrame();
    if (renderCallCount <= 5) {
        std::cout << "[CatAnnihilation::render] uiPass->EndFrame() DONE\n";
        std::cout.flush();
    }

    // Execute UI draw commands on the current command buffer
    auto* cmdBuffer = renderer_->GetCommandBuffer();
    if (renderCallCount <= 5) {
        std::cout << "[CatAnnihilation::render] cmdBuffer=" << cmdBuffer << "\n";
        std::cout.flush();
    }
    if (cmdBuffer != nullptr) {
        if (renderCallCount <= 5) {
            std::cout << "[CatAnnihilation::render] calling uiPass->Execute()...\n";
            std::cout.flush();
        }
        uiPass->Execute(cmdBuffer, renderer_->GetFrameIndex());
        if (renderCallCount <= 5) {
            std::cout << "[CatAnnihilation::render] uiPass->Execute() DONE\n";
            std::cout.flush();
        }
    } else {
        if (renderCallCount <= 5) {
            std::cout << "[CatAnnihilation::render] WARNING: cmdBuffer is NULL!\n";
            std::cout.flush();
        }
    }

    if (renderCallCount <= 5) {
        std::cout << "[CatAnnihilation::render] complete\n";
        std::cout.flush();
    }
}

// ============================================================================
// State Updates
// ============================================================================

void CatAnnihilation::updateMainMenu(float /*dt*/) {
    // Check for start game input
    if (input_->isKeyPressed(Engine::Input::Key::Enter) ||
        input_->isKeyPressed(Engine::Input::Key::Space)) {
        startNewGame();
    }
}

void CatAnnihilation::updatePlaying(float dt) {
    // Update all game systems
    updateSystems(dt);

    // Note: This is an endless wave game - victory is triggered by story mode completion
    // or by reaching a specific objective, not by completing all waves
    if (isStoryMode_ && storyModeSystem_ != nullptr && storyModeSystem_->isStoryComplete()) {
        setState(GameState::Victory);
    }
}

void CatAnnihilation::updatePaused(float /*dt*/) {
    // Check for restart input
    if (input_->isKeyPressed(Engine::Input::Key::R)) {
        restart();
    }

    // Check for quit to menu
    if (input_->isKeyPressed(Engine::Input::Key::Q)) {
        quitToMenu();
    }
}

void CatAnnihilation::updateGameOver(float /*dt*/) {
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
}

void CatAnnihilation::updateVictory(float /*dt*/) {
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
            if (gameAudio_ != nullptr) {
                gameAudio_->playMenuMusic();
            }
            break;

        case GameState::Playing:
            // Ensure player entity exists
            if (!ecs_.isAlive(playerEntity_)) {
                createPlayer();
            }

            // Enable player controls
            if (playerControlSystem_ != nullptr) {
                playerControlSystem_->setControlEnabled(true);
            }

            // Start gameplay music
            if (gameAudio_ != nullptr) {
                gameAudio_->playGameplayMusic();
            }

            // Resume time
            if (dayNightSystem_ != nullptr) {
                dayNightSystem_->resumeTime();
            }

            setGameMode(GameMode::Playing);
            break;

        case GameState::Paused:
            // Disable player controls
            if (playerControlSystem_ != nullptr) {
                playerControlSystem_->setControlEnabled(false);
            }

            // Pause time
            if (dayNightSystem_ != nullptr) {
                dayNightSystem_->pauseTime();
            }

            setGameMode(GameMode::Paused);
            break;

        case GameState::GameOver:
            // Disable player controls
            if (playerControlSystem_ != nullptr) {
                playerControlSystem_->setControlEnabled(false);
            }

            // Play game over music
            if (gameAudio_ != nullptr) {
                gameAudio_->playDefeatMusic();
            }

            setGameMode(GameMode::GameOver);
            break;

        case GameState::Victory:
            // Disable player controls
            if (playerControlSystem_ != nullptr) {
                playerControlSystem_->setControlEnabled(false);
            }

            // Play victory music
            if (gameAudio_ != nullptr) {
                gameAudio_->playVictoryMusic();
            }

            setGameMode(GameMode::Victory);
            break;
    }
}

void CatAnnihilation::onStateExit(GameState /*state*/) {
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
    gameTime_ = 0.0F;
    enemiesKilled_ = 0;
    waveNumber_ = 0;

    // Reset day/night cycle to noon
    if (dayNightSystem_ != nullptr) {
        dayNightSystem_->setTimeOfDay(0.5F);
    }

    // Recreate player
    createPlayer();

    // Start new game
    setState(GameState::Playing);
}

void CatAnnihilation::startNewGame(bool storyMode) {
    Engine::Logger::info(storyMode ? "Starting story mode..." : "Starting arcade mode...");

    isStoryMode_ = storyMode;

    if (storyMode && storyModeSystem_ != nullptr) {
        // Start story mode with default clan
        storyModeSystem_->startStoryMode(Clan::MistClan);
    }

    restart();
}

void CatAnnihilation::continueGame() {
    Engine::Logger::info("Continuing from last save...");

    // Find the most recent save by checking all slots
    int mostRecentSlot = -1;
    uint64_t mostRecentTime = 0;

    for (int i = 0; i < 10; ++i) {
        if (saveSystem_ != nullptr && saveSystem_->doesSaveExist(i)) {
            auto header = saveSystem_->getSaveHeader(i);
            if (header.timestamp > mostRecentTime) {
                mostRecentTime = header.timestamp;
                mostRecentSlot = i;
            }
        }
    }

    if (mostRecentSlot >= 0) {
        loadGame(mostRecentSlot);
    } else {
        // No saves found, start new game
        Engine::Logger::warn("No save files found, starting new game");
        startNewGame(false);
    }
}

void CatAnnihilation::quitToMenu() {
    Engine::Logger::info("Returning to main menu...");

    // Clear all entities
    ecs_.clearEntities();

    // Reset statistics
    gameTime_ = 0.0F;
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

    if (saveSystem_ == nullptr) {
        Engine::Logger::error("Save system not initialized");
        return false;
    }

    // Build save game data from current game state
    Engine::SaveGameData saveData;

    // Player stats
    if (levelingSystem_ != nullptr) {
        saveData.stats.level = levelingSystem_->getLevel();
        saveData.stats.experience = levelingSystem_->getXP();
        saveData.stats.experienceToNextLevel = levelingSystem_->getXPToNextLevel();
    }

    // Player health from entity
    if (ecs_.isAlive(playerEntity_)) {
        auto* healthComp = ecs_.getComponent<HealthComponent>(playerEntity_);
        if (healthComp != nullptr) {
            saveData.stats.currentHealth = healthComp->currentHealth;
            saveData.stats.maxHealth = healthComp->maxHealth;
        }

        // Player position
        auto* transform = ecs_.getComponent<Engine::Transform>(playerEntity_);
        if (transform != nullptr) {
            saveData.position = transform->position;
            saveData.rotation = transform->rotation;
        }
    }

    // Quest progress
    if (questSystem_ != nullptr) {
        saveData.activeQuests = questSystem_->getActiveQuestIds();
        saveData.completedQuests = questSystem_->getCompletedQuestIds();
    }

    // Story mode state
    if (storyModeSystem_ != nullptr && isStoryMode_) {
        saveData.storyState.currentChapter = storyModeSystem_->getCurrentChapterNumber();
        saveData.storyState.currentMission = storyModeSystem_->getCurrentMission();
    }

    // World state
    if (dayNightSystem_ != nullptr) {
        // getCurrentTime() returns 0.0-1.0, convert to 0-24 hours for save format
        saveData.timeOfDay = dayNightSystem_->getCurrentTime() * 24.0F;
        saveData.currentDay = dayNightSystem_->getCurrentDay();
    }

    // Currency from merchant system
    if (merchantSystem_ != nullptr) {
        saveData.currency = merchantSystem_->getPlayerCurrency();
    }

    // Set the data and save
    saveSystem_->setCurrentSaveData(saveData);
    bool success = saveSystem_->saveGame(slotIndex);

    if (success) {
        currentSaveSlot_ = slotIndex;

        // Publish save event
        GameSavedEvent event("slot_" + std::to_string(slotIndex));
        event.playtime = totalPlayTime_;
        event.playerLevel = levelingSystem_ != nullptr ? levelingSystem_->getLevel() : 1;
        event.wasAutoSave = false;
        eventBus_.publish(event);

        Engine::Logger::info("Game saved successfully");
    } else {
        Engine::Logger::error("Failed to save game");
    }

    return success;
}

bool CatAnnihilation::loadGame(int slotIndex) {
    Engine::Logger::info("Loading game from slot " + std::to_string(slotIndex) + "...");

    if (saveSystem_ == nullptr) {
        Engine::Logger::error("Save system not initialized");
        return false;
    }

    // Load the save data
    if (!saveSystem_->loadGame(slotIndex)) {
        Engine::Logger::error("Failed to load save file from slot " + std::to_string(slotIndex));
        return false;
    }

    const Engine::SaveGameData& saveData = saveSystem_->getLoadedSaveData();

    // Clear existing entities and recreate player
    ecs_.clearEntities();
    createPlayer();

    // Restore player stats
    if (levelingSystem_ != nullptr) {
        levelingSystem_->setLevel(saveData.stats.level);
        levelingSystem_->setXP(saveData.stats.experience);
    }

    // Restore player health
    if (ecs_.isAlive(playerEntity_)) {
        auto* healthComp = ecs_.getComponent<HealthComponent>(playerEntity_);
        if (healthComp != nullptr) {
            healthComp->maxHealth = saveData.stats.maxHealth;
            healthComp->currentHealth = saveData.stats.currentHealth;
        }

        // Restore player position
        auto* transform = ecs_.getComponent<Engine::Transform>(playerEntity_);
        if (transform != nullptr) {
            transform->position = saveData.position;
            transform->rotation = saveData.rotation;
        }
    }

    // Restore quest progress
    if (questSystem_ != nullptr) {
        questSystem_->loadQuestState(saveData.activeQuests, saveData.completedQuests);
    }

    // Restore story mode state
    if (storyModeSystem_ != nullptr) {
        isStoryMode_ = saveData.storyState.currentChapter > 0;
        if (isStoryMode_) {
            storyModeSystem_->setChapter(saveData.storyState.currentChapter);
            storyModeSystem_->setMission(saveData.storyState.currentMission);
        }
    }

    // Restore world state
    if (dayNightSystem_ != nullptr) {
        // Convert from 0-24 hours to 0.0-1.0 format
        dayNightSystem_->setTimeOfDay(saveData.timeOfDay / 24.0F);
        dayNightSystem_->setCurrentDay(saveData.currentDay);
    }

    // Restore currency
    if (merchantSystem_ != nullptr) {
        merchantSystem_->setPlayerCurrency(saveData.currency);
    }

    currentSaveSlot_ = slotIndex;

    // Publish load event
    GameLoadedEvent event("slot_" + std::to_string(slotIndex));
    event.playtime = totalPlayTime_;
    event.playerLevel = levelingSystem_ != nullptr ? levelingSystem_->getLevel() : 1;
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
    Engine::vec3 spawnPosition(0.0F, 0.0F, 0.0F);
    playerEntity_ = CatEntity::create(ecs_, spawnPosition);

    // Set player entity in control system
    if (playerControlSystem_ != nullptr) {
        playerControlSystem_->setPlayerEntity(playerEntity_);
    }

    Engine::Logger::info("Player entity created");
}

void CatAnnihilation::spawnDeathParticles(const Engine::vec3& position) {
    if (particleSystem_ == nullptr) {
        return;
    }

    // Get the death emitter and update its position
    CatEngine::CUDA::ParticleEmitter* emitter = particleSystem_->getEmitter(deathEmitterId_);
    if (emitter != nullptr) {
        emitter->position = position;
        particleSystem_->updateEmitter(deathEmitterId_, *emitter);
        particleSystem_->triggerBurst(deathEmitterId_);
    }
}

// ============================================================================
// Event Handlers
// ============================================================================

void CatAnnihilation::onEnemyKilled(const EnemyKilledEvent& event) {
    // Update statistics
    enemiesKilled_++;

    // Award XP to killer
    if (levelingSystem_ != nullptr && event.killer == playerEntity_) {
        levelingSystem_->addXP(event.xpReward);
    }

    // Update quest progress
    if (questSystem_ != nullptr) {
        questSystem_->onEnemyKilled(event.enemyType);
    }

    // Update story mode progress
    if (storyModeSystem_ != nullptr && isStoryMode_) {
        storyModeSystem_->addExperience(event.xpReward);
    }

    // Play kill sound
    if (gameAudio_ != nullptr) {
        gameAudio_->playEnemyDeath({event.position.x, event.position.y, event.position.z});
    }

    Engine::Logger::debug("Enemy killed: " + event.enemyType + " (+" + std::to_string(event.xpReward) + " XP)");
}

void CatAnnihilation::onQuestCompleted(const QuestCompletedEvent& event) {
    // Award XP
    if (levelingSystem_ != nullptr) {
        levelingSystem_->addXP(event.xpReward);
    }

    // Award currency via merchant system
    if (merchantSystem_ != nullptr) {
        merchantSystem_->addCurrency(event.currencyReward);
        Engine::Logger::debug("Awarded " + std::to_string(event.currencyReward) + " currency");
    }

    // Update story progress if in story mode
    if (storyModeSystem_ != nullptr && isStoryMode_ && event.wasMainQuest) {
        storyModeSystem_->advanceStory();
    }

    // Play quest complete sound
    if (gameAudio_ != nullptr) {
        gameAudio_->playSound2D("quest_complete", 1.0F);
    }

    // Show UI notification
    if (hud_ != nullptr) {
        hud_->showNotification("Quest Complete: " + event.questName, "success", 4.0F);
    }

    Engine::Logger::info("Quest completed: " + event.questName + " (+" + std::to_string(event.xpReward) + " XP)");
}

void CatAnnihilation::onLevelUp(const LevelUpEvent& event) {
    // Update player stats
    if (ecs_.isAlive(playerEntity_)) {
        auto* healthComp = ecs_.getComponent<HealthComponent>(playerEntity_);
        if (healthComp != nullptr) {
            healthComp->maxHealth += GameplayConfig::Player::HEALTH_PER_LEVEL;
            healthComp->currentHealth = healthComp->maxHealth; // Full heal on level up
        }
    }

    // Play level up sound
    if (gameAudio_ != nullptr) {
        gameAudio_->playLevelUp();
    }

    // Show UI notification
    if (hud_ != nullptr) {
        hud_->showNotification("Level Up! Level " + std::to_string(event.newLevel), "success", 3.0F);
    }

    Engine::Logger::info("Level up! New level: " + std::to_string(event.newLevel));
}

void CatAnnihilation::onDamage(const DamageEvent& event) {
    // Update HUD damage indicator
    if (hud_ != nullptr && event.target == playerEntity_) {
        // Direction from damage source to player (2D normalized for HUD)
        std::array<float, 2> damageDir = {0.0F, 1.0F}; // Default direction
        hud_->showDamageIndicator(damageDir, event.amount / 100.0F);
    }

    // Play damage sound
    if (gameAudio_ != nullptr) {
        if (event.target == playerEntity_) {
            gameAudio_->playPlayerHurt();
        } else {
            gameAudio_->playEnemyHit({event.hitPosition.x, event.hitPosition.y, event.hitPosition.z});
        }
    }

    // Show damage number
    if (hud_ != nullptr) {
        std::array<float, 2> screenPos = {event.hitPosition.x, event.hitPosition.y};
        hud_->showDamageNumber(event.amount, screenPos, event.wasCritical);
    }
}

void CatAnnihilation::onEntityDeath(const EntityDeathEvent& event) {
    // Get entity position for particle effects
    Engine::vec3 deathPosition(0.0F, 0.0F, 0.0F);
    if (ecs_.isAlive(event.entity)) {
        auto* transform = ecs_.getComponent<Engine::Transform>(event.entity);
        if (transform != nullptr) {
            deathPosition = transform->position;
        }
    }

    // Check if player died
    if (event.entity == playerEntity_) {
        Engine::Logger::info("Player died!");
        setState(GameState::GameOver);
    }

    // Spawn death particles at entity position
    spawnDeathParticles(deathPosition);
}

void CatAnnihilation::onWaveComplete(const WaveCompleteEvent& event) {
    waveNumber_ = event.waveNumber;

    // Award XP
    if (levelingSystem_ != nullptr) {
        levelingSystem_->addXP(event.xpReward);
    }

    // Play wave complete sound
    if (gameAudio_ != nullptr) {
        gameAudio_->playWaveComplete();
    }

    // Update HUD with new wave info
    if (hud_ != nullptr) {
        hud_->setWave(event.waveNumber + 1); // Show next wave number
        hud_->showNotification("Wave " + std::to_string(event.waveNumber) + " Complete!", "info", 3.0F);
    }

    Engine::Logger::info("Wave " + std::to_string(event.waveNumber) + " complete! (+" + std::to_string(event.xpReward) + " XP)");
}

} // namespace CatGame
