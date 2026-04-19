#include "CatAnnihilation.hpp"
#include "entities/CatEntity.hpp"
#include "components/GameComponents.hpp"
#include "components/EnemyComponent.hpp"
#include "config/GameplayConfig.hpp"
#include "../engine/core/Logger.hpp"
#include "../engine/math/Vector.hpp"
#include "../engine/math/Matrix.hpp"
#include "../engine/math/Math.hpp"
#include "../engine/cuda/particles/ParticleEmitter.hpp"
#include "../engine/renderer/passes/ScenePass.hpp"
#include "../engine/renderer/MeshSubmissionSystem.hpp"
#include "components/MeshComponent.hpp"
#include "../engine/rhi/vulkan/VulkanCommandBuffer.hpp"
#include "world/GameWorld.hpp"
#include "world/Terrain.hpp"
#include "ui/WavePopup.hpp"

#include "imgui.h"
#include <cmath>
#include <iostream>

namespace CatGame {

namespace {

// Vulkan-convention perspective: RH, depth [0,1], Y flipped via negative
// element at (1,1) so we can consume Engine::mat4::lookAt (OpenGL RH view)
// without a viewport Y-flip.
Engine::mat4 makeVulkanPerspective(float fovyRad, float aspect,
                                   float nearZ, float farZ) {
    const float f = 1.0f / std::tan(fovyRad * 0.5f);
    Engine::mat4 m(0.0f);
    m[0][0] = f / aspect;
    m[1][1] = -f;
    m[2][2] = farZ / (nearZ - farZ);
    m[2][3] = -1.0f;
    m[3][2] = (nearZ * farZ) / (nearZ - farZ);
    return m;
}

} // namespace

// ============================================================================
// Constructor / Destructor
// ============================================================================

CatAnnihilation::CatAnnihilation(Engine::Input* input,
                                 Engine::Window* window,
                                 CatEngine::Renderer::Renderer* renderer,
                                 CatEngine::AudioEngine* audioEngine,
                                 Engine::ImGuiLayer* imguiLayer)
    : input_(input)
    , window_(window)
    , renderer_(renderer)
    , audioEngine_(audioEngine)
    , imguiLayer_(imguiLayer)
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

    // Tune wave pacing so the first round is actually survivable: fewer
    // enemies, spawned over a longer window, farther from the player.
    if (waveSystem_ != nullptr) {
        WaveConfig waveConfig;
        waveConfig.baseEnemyCount = 3;
        waveConfig.enemyCountMultiplier = 2.0f;  // 3 / 5 / 7 / 9 / 11
        waveConfig.spawnDelay = 1.5f;            // one dog per 1.5 s
        waveConfig.transitionDelay = 5.0f;       // breathing room between waves
        waveConfig.minSpawnDistance = 25.0f;
        waveConfig.spawnRadius = 40.0f;
        waveSystem_->setConfig(waveConfig);
    }

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

    // Wire TouchInput + MobileControlsSystem. The touch system needs the raw
    // GLFWwindow to install cursor / mouse-button callbacks — on desktop it
    // simulates a single touch point via the mouse, on a mobile/touchscreen
    // build the same object receives real touch events.
    //
    // MobileControlsSystem::initialize() accepts a null uiSystem today (the
    // on-screen joystick / button layer is drawn directly via UIPass once
    // the UI system is in place). Passing nullptr here is deliberate — it
    // means "no overlay UI yet" rather than "mobile controls disabled",
    // and keeps the touch / gesture pipeline live so the rest of the
    // codebase can consume it without the whole chain silently no-oping.
    if (window_ != nullptr && window_->getHandle() != nullptr) {
        touchInput_ = std::make_unique<Engine::TouchInput>(window_->getHandle());
        touchInput_->initialize();

        const uint32_t screenWidth  = window_->getWidth();
        const uint32_t screenHeight = window_->getHeight();
        mobileControls_->initialize(touchInput_.get(),
                                    /*uiSystem*/ nullptr,
                                    screenWidth, screenHeight);
    } else {
        Engine::Logger::warn("TouchInput skipped: no window handle available");
    }

    Engine::Logger::info("Advanced gameplay systems initialized");

    // Initialize game world
    gameWorld_ = std::make_unique<GameWorld>(*cudaContext_, *physicsWorld_);
    gameWorld_->initialize();

    // Wire player control system to terrain + combat so the cat stays on the
    // heightfield and left-click actually drives CombatSystem::performAttack.
    if (playerControlSystem_ != nullptr) {
        playerControlSystem_->setCombatSystem(combatSystem_);
        if (gameWorld_ != nullptr) {
            playerControlSystem_->setTerrain(gameWorld_->getTerrain());
        }
    }

    // HealthSystem publishes enemy/player deaths through a plain callback —
    // wire it here so the score ticks up on dog kills and the state machine
    // transitions to GameOver when the player's HP reaches zero.
    if (healthSystem_ != nullptr) {
        healthSystem_->setOnEntityDeath([this](CatEngine::Entity entity, bool isEnemy) {
            if (isEnemy) {
                ++enemiesKilled_;
                Engine::Logger::info("[kill] Enemy died. Total kills: " +
                                     std::to_string(enemiesKilled_));
            } else if (entity == playerEntity_) {
                Engine::Logger::info("[death] Player died, → GameOver");
                setState(GameState::GameOver);
            }
        });
    }

    // Run a short campaign — finishing wave 5 flips the state machine to
    // Victory so the end-screen overlay appears.
    if (waveSystem_ != nullptr) {
        waveSystem_->setOnWaveStart([](int wave) {
            Engine::Logger::info("[wave] Starting wave " + std::to_string(wave));
        });
        waveSystem_->setOnWaveComplete([this](int waveNumber) {
            waveNumber_ = waveNumber;
            Engine::Logger::info("[wave] Completed wave " + std::to_string(waveNumber));
            if (waveNumber >= 5 && currentState_ == GameState::Playing) {
                setState(GameState::Victory);
            }
        });
    }

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

    // Setup UI callbacks. GameUI owns the MainMenu that actually receives input and
    // is drawn on screen, so wire the callbacks + ImGui layer onto that instance.
    // mainMenu_ is legacy/duplicate and stays dormant (no render, no callbacks).
    if (gameUI_ != nullptr) {
        auto& activeMenu = gameUI_->getMainMenu();
        activeMenu.setStartGameCallback([this]() {
            startNewGame(false); // Start arcade mode
        });
        activeMenu.setContinueCallback([this]() {
            continueGame();
        });
        activeMenu.setQuitCallback([]() {
            // Handled by main loop (ESC/quit button closes window)
        });
        if (imguiLayer_ != nullptr) {
            activeMenu.setImGuiLayer(imguiLayer_);
            gameUI_->getHUD().setImGuiLayer(imguiLayer_);
            gameUI_->getWavePopup().setImGuiLayer(imguiLayer_);
        }
        // Thread settings bindings through to the live main menu so the
        // Settings panel's sliders / checkboxes drive real engine systems.
        activeMenu.setSettingsBindings(window_, renderer_, gameConfig_);
    }

    // Also wire the ImGui layer onto the legacy mainMenu_/hud_/pauseMenu_ instances
    // even though they aren't rendered — keeps the setter symmetric if anything else
    // decides to render them later.
    if (imguiLayer_ != nullptr) {
        if (mainMenu_ != nullptr)  { mainMenu_->setImGuiLayer(imguiLayer_); }
        if (hud_ != nullptr)       { hud_->setImGuiLayer(imguiLayer_); }
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

        if (imguiLayer_ != nullptr) {
            pauseMenu_->setImGuiLayer(imguiLayer_);
        }
        pauseMenu_->setSettingsBindings(window_, renderer_, gameConfig_);
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

    // Tear down touch input before the window is gone — shutdown detaches
    // the GLFW cursor/mouse callbacks it installed so they can't fire
    // against a dangling `this` during later GLFW polling.
    if (mobileControls_ != nullptr) { mobileControls_->shutdown(); }
    if (touchInput_ != nullptr) {
        touchInput_->shutdown();
        touchInput_.reset();
    }

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
    handleInput();

    // Touch input + mobile controls run before the rest of the gameplay
    // systems so any joystick/button state they produce is visible to
    // PlayerControlSystem on the same frame.
    if (touchInput_ != nullptr) { touchInput_->update(dt); }
    if (mobileControls_ != nullptr) { mobileControls_->update(dt); }

    if (dayNightSystem_ != nullptr) { dayNightSystem_->update(dt); }

    if (dialogSystem_ != nullptr && dialogSystem_->isInDialog()) {
        dialogSystem_->update(dt);
        return;
    }

    if (npcSystem_ != nullptr) { npcSystem_->update(dt); }
    ecs_.update(dt);
    if (questSystem_ != nullptr) { questSystem_->update(dt); }
    if (storyModeSystem_ != nullptr && isStoryMode_) { storyModeSystem_->update(dt); }
    if (magicSystem_ != nullptr) { magicSystem_->update(dt); }
    if (levelingSystem_ != nullptr) { levelingSystem_->update(dt); }

    // CUDA physics — re-enabled after the SpatialHash hashTableSize overflow fix
    // (gridSize^3 was overflowing uint32 to 0, producing 0-byte cellStarts/cellEnds
    // allocations and cudaErrorIllegalAddress on the first broadphase write).
    if (physicsWorld_ != nullptr) { physicsWorld_->step(dt); }

    if (particleSystem_ != nullptr) { particleSystem_->update(dt); }
    if (gameWorld_ != nullptr) { gameWorld_->update(dt); }

    gameTime_ += dt;
}

void CatAnnihilation::updateUI(float dt) {
    if (gameUI_ != nullptr) {
        gameUI_->update(dt);
    }

    // Update HUD during Playing. The visible HUD lives inside GameUI — the
    // free-standing `hud_` member is a legacy duplicate that isn't rendered.
    if (currentState_ == GameState::Playing && gameUI_ != nullptr) {
        auto& activeHud = gameUI_->getHUD();

        if (ecs_.isAlive(playerEntity_)) {
            auto* healthComp = ecs_.getComponent<HealthComponent>(playerEntity_);
            if (healthComp != nullptr) {
                activeHud.setHealth(healthComp->currentHealth, healthComp->maxHealth);
            }

            // Drive wave + remaining-dog display straight from the authoritative
            // WaveSystem state — the old waveNumber_ member only got bumped on
            // wave-complete events, so the HUD stayed on "WAVE 0  Dogs 0/0"
            // for the entire first wave.
            if (waveSystem_ != nullptr) {
                const int liveWave = waveSystem_->getCurrentWave();
                activeHud.setWave(static_cast<uint32_t>(std::max(liveWave, 1)));
                const int remaining = waveSystem_->getEnemiesRemaining();
                const int total = waveSystem_->getConfig().baseEnemyCount +
                    (std::max(liveWave, 1) - 1) *
                    static_cast<int>(waveSystem_->getConfig().enemyCountMultiplier);
                activeHud.setEnemyCount(static_cast<uint32_t>(std::max(remaining, 0)),
                                        static_cast<uint32_t>(std::max(total, 0)));
            }
            activeHud.setScore(static_cast<uint32_t>(enemiesKilled_));
        }
    }
}

void CatAnnihilation::handleInput() {
    // Route input to the active UI screen (main menu, pause menu, wave popup, etc.)
    // before anything else so menu nav (Up/Down/Enter, mouse clicks) can register.
    if (gameUI_ != nullptr) {
        gameUI_->handleInput();
    }

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

    // ---- 3D scene pass (terrain etc.) -------------------------------------
    //
    // Runs first so ImGui/UI composites on top. We drive a slow orbital camera
    // for the initial revision — there's no player-follow camera component yet.
    // Terrain is uploaded once, lazily, the first time it's available.
    if (currentState_ == GameState::Playing) {
        auto* scenePass = renderer_->GetScenePass();
        if (scenePass != nullptr && gameWorld_ != nullptr) {
            if (!terrainUploadedToScenePass_) {
                const auto* terrain = gameWorld_->getTerrain();
                if (terrain != nullptr && !terrain->getVertices().empty()) {
                    scenePass->SetTerrain(*terrain);
                    terrainUploadedToScenePass_ = true;
                }
            }

            // Third-person camera follows the player. PlayerControlSystem's
            // camera state is driven by mouse-look + cameraFollowSpeed and
            // already handles rotation + smoothing; we just consume it here.
            Engine::vec3 camPos;
            Engine::vec3 camFwd;
            Engine::vec3 camTarget;
            bool haveCamera = false;
            if (playerControlSystem_ != nullptr && ecs_.isAlive(playerEntity_)) {
                camPos = playerControlSystem_->getCameraPosition();
                camFwd = playerControlSystem_->getCameraForward();
                camTarget = camPos + camFwd;
                haveCamera = true;
            }
            // Fallback: static overview if the player isn't alive yet
            if (!haveCamera) {
                camPos = Engine::vec3(0.0F, 120.0F, 260.0F);
                camTarget = Engine::vec3(0.0F, 20.0F, 0.0F);
            }

            const float aspect = (screenHeight > 0)
                ? static_cast<float>(screenWidth) / static_cast<float>(screenHeight)
                : 1.0f;
            Engine::mat4 view = Engine::mat4::lookAt(camPos, camTarget,
                                                     Engine::vec3(0.0F, 1.0F, 0.0F));
            Engine::mat4 proj = makeVulkanPerspective(
                60.0f * Engine::Math::DEG_TO_RAD, aspect, 0.1f, 2000.0f);
            Engine::mat4 viewProj = proj * view;

            // Build entity draw list — cat (green tall box) + dogs (red cubes
            // sized by EnemyType). Walks the ECS each frame; trivial at wave
            // populations (< 100 enemies typical).
            //
            // Entities that already carry a MeshComponent are routed through
            // MeshSubmissionSystem below and skipped here — that prevents
            // double-rendering the same entity as both a proxy cube and a
            // mesh-sized cube. Entities without a MeshComponent (e.g., a
            // spawn whose model file failed to load and loadModel() returned
            // false) still fall through to the proxy-cube path so they
            // remain visible and debuggable in the scene.
            std::vector<CatEngine::Renderer::ScenePass::EntityDraw> entityDraws;
            entityDraws.reserve(64);
            if (ecs_.isAlive(playerEntity_) &&
                !ecs_.hasComponent<MeshComponent>(playerEntity_)) {
                auto* t = ecs_.getComponent<Engine::Transform>(playerEntity_);
                if (t != nullptr) {
                    CatEngine::Renderer::ScenePass::EntityDraw d;
                    d.position = t->position + Engine::vec3(0.0F, 0.75F, 0.0F);
                    d.halfExtents = Engine::vec3(0.5F, 0.75F, 0.9F);
                    d.color = Engine::vec3(0.45F, 0.85F, 0.35F); // green cat
                    entityDraws.push_back(d);
                }
            }
            ecs_.forEach<EnemyComponent, Engine::Transform>(
                [&](CatEngine::Entity enemyEntity, EnemyComponent* enemy, Engine::Transform* t) {
                    if (t == nullptr || enemy == nullptr) return;
                    if (ecs_.hasComponent<MeshComponent>(enemyEntity)) return;
                    CatEngine::Renderer::ScenePass::EntityDraw d;
                    float sz = 0.6F;
                    Engine::vec3 tint(0.85F, 0.22F, 0.22F); // default dog red
                    switch (enemy->type) {
                        case EnemyType::BigDog:   sz = 0.95F; tint = Engine::vec3(0.8F, 0.15F, 0.15F); break;
                        case EnemyType::FastDog:  sz = 0.45F; tint = Engine::vec3(1.0F, 0.55F, 0.15F); break;
                        case EnemyType::BossDog:  sz = 1.4F;  tint = Engine::vec3(0.65F, 0.05F, 0.65F); break;
                        case EnemyType::Dog:
                        default: break;
                    }
                    d.position = t->position + Engine::vec3(0.0F, sz, 0.0F);
                    d.halfExtents = Engine::vec3(sz, sz, sz * 1.2F);
                    d.color = tint;
                    entityDraws.push_back(d);
                });

            // Submit mesh-backed entities (those carrying Transform +
            // MeshComponent). The static instance persists across frames so
            // its internal ring of strong shared_ptr<Model> refs — one slot
            // per frame in flight — can keep each Model alive until the GPU
            // fence for its recording frame has signalled. The renderer's
            // frame index drives the ring slot. Without this, a destroyed
            // entity's Model could be freed by AssetManager's unused-asset
            // sweep while the GPU was still reading its vertex buffers.
            static CatEngine::Renderer::MeshSubmissionSystem meshSubmission;
            meshSubmission.Submit(ecs_,
                                  static_cast<std::size_t>(renderer_->GetFrameIndex()),
                                  entityDraws);

            auto* sceneCmdBuffer = renderer_->GetCommandBuffer();
            if (sceneCmdBuffer != nullptr) {
                auto* vkCmd = static_cast<CatEngine::RHI::VulkanCommandBuffer*>(
                    sceneCmdBuffer)->GetHandle();
                uint32_t imageIndex = renderer_->GetCurrentSwapchainImageIndex();
                scenePass->Execute(vkCmd, imageIndex, viewProj, entityDraws);
            }
        }
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

    // MainMenu is rendered by gameUI_ above; the mainMenu_ member is legacy/unused.

    // Game Over / Victory overlay — emitted here as ImGui widgets so the
    // UIPass composite step picks them up in the same render pass.
    if (imguiLayer_ != nullptr &&
        (currentState_ == GameState::GameOver || currentState_ == GameState::Victory)) {
        renderEndScreenOverlay(screenWidth, screenHeight);
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

    // Keep GameUI's state machine in sync — without this, the menu keeps rendering
    // after we transition to Playing (and the HUD never appears).
    if (gameUI_ != nullptr) {
        switch (newState) {
            case GameState::MainMenu:  gameUI_->setGameState(Game::GameState::MainMenu);  break;
            case GameState::Playing:   gameUI_->setGameState(Game::GameState::Playing);   break;
            case GameState::Paused:    gameUI_->setGameState(Game::GameState::Paused);    break;
            case GameState::GameOver:  gameUI_->setGameState(Game::GameState::GameOver);  break;
            case GameState::Victory:   gameUI_->setGameState(Game::GameState::Victory);   break;
        }
    }

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

            // Kick off the wave loop once, on first entry into Playing.
            // WaveSystem::startWaves guards itself with a `wavesStarted_` flag,
            // so subsequent pause/resume cycles don't restart from wave 1.
            if (waveSystem_ != nullptr) {
                waveSystem_->startWaves();
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
    // Spawn on top of the terrain at the world origin. Without the terrain
    // height lookup the player lands at y=0 — which can be beneath the
    // generated heightfield — and ends up clipped inside the world.
    Engine::vec3 spawnPosition(0.0F, 0.0F, 0.0F);
    if (gameWorld_ != nullptr) {
        const auto* terrain = gameWorld_->getTerrain();
        if (terrain != nullptr) {
            spawnPosition.y = terrain->getHeightAt(0.0F, 0.0F);
        }
    }
    // Beefy survival-game stats: enough HP + damage to realistically clear
    // five waves on the first attempt, still tense because dog counts scale
    // (wave 5 = 11 dogs). Fast-swing sword (attackSpeed 3 -> cooldown 0.33s).
    playerEntity_ = CatEntity::createCustom(ecs_, spawnPosition,
                                            /*maxHealth*/ 400.0f,
                                            /*moveSpeed*/ 12.0f,
                                            /*attackDamage*/ 60.0f);
    if (auto* combat = ecs_.getComponent<CombatComponent>(playerEntity_)) {
        combat->attackSpeed = 3.0f;       // 0.33 s cooldown
        combat->attackRange = 4.0f;       // a touch wider than default
    }

    // Set player entity in control system
    if (playerControlSystem_ != nullptr) {
        playerControlSystem_->setPlayerEntity(playerEntity_);
    }

    // Wave system spawns enemies around the player; it early-returns on an
    // invalid playerEntity_, so it has to be told about the player before any
    // wave starts.
    if (waveSystem_ != nullptr) {
        waveSystem_->setPlayer(playerEntity_);
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

void CatAnnihilation::renderEndScreenOverlay(uint32_t screenWidth, uint32_t screenHeight) {
    if (imguiLayer_ == nullptr) return;

    const float width = static_cast<float>(screenWidth);
    const float height = static_cast<float>(screenHeight);
    const bool victory = (currentState_ == GameState::Victory);

    // Full-screen dim overlay + centered title/subtitle
    ImGui::SetNextWindowPos(ImVec2(0.0F, 0.0F));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0F, 0.0F, 0.0F, 0.70F));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));

    constexpr ImGuiWindowFlags kFlags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings;

    ImGui::Begin("##EndScreenOverlay", nullptr, kFlags);

    if (auto* titleFont = imguiLayer_->GetTitleFont()) {
        ImGui::PushFont(titleFont);
    }
    const char* title = victory ? "VICTORY" : "YOU DIED";
    const ImVec2 titleSize = ImGui::CalcTextSize(title);
    ImGui::SetCursorPos(ImVec2((width - titleSize.x) * 0.5F, height * 0.30F));
    const ImVec4 titleColor = victory
        ? ImVec4(0.95F, 0.88F, 0.30F, 1.0F)
        : ImVec4(0.90F, 0.15F, 0.15F, 1.0F);
    ImGui::TextColored(titleColor, "%s", title);
    if (imguiLayer_->GetTitleFont() != nullptr) {
        ImGui::PopFont();
    }

    if (auto* boldFont = imguiLayer_->GetBoldFont()) {
        ImGui::PushFont(boldFont);
    }
    char summary[128];
    std::snprintf(summary, sizeof(summary),
                  "Wave %d   Enemies killed %d",
                  waveSystem_ != nullptr ? waveSystem_->getCurrentWave() : 0,
                  enemiesKilled_);
    const ImVec2 summarySize = ImGui::CalcTextSize(summary);
    ImGui::SetCursorPos(ImVec2((width - summarySize.x) * 0.5F, height * 0.30F + titleSize.y + 28.0F));
    ImGui::TextColored(ImVec4(1.0F, 1.0F, 1.0F, 0.92F), "%s", summary);
    if (imguiLayer_->GetBoldFont() != nullptr) {
        ImGui::PopFont();
    }

    if (auto* regularFont = imguiLayer_->GetRegularFont()) {
        ImGui::PushFont(regularFont);
    }
    const char* prompt = "Press R or Enter to restart   \u2022   Esc for main menu";
    const ImVec2 promptSize = ImGui::CalcTextSize(prompt);
    ImGui::SetCursorPos(ImVec2((width - promptSize.x) * 0.5F, height * 0.55F));
    ImGui::TextColored(ImVec4(0.85F, 0.85F, 0.90F, 0.85F), "%s", prompt);
    if (imguiLayer_->GetRegularFont() != nullptr) {
        ImGui::PopFont();
    }

    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
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
