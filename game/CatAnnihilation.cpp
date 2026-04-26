#include "CatAnnihilation.hpp"
#include "entities/CatEntity.hpp"
#include "entities/DogEntity.hpp"
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
#include <algorithm>
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

    // Step 6: Build the game world before loading content.
    //
    // Why before loadGameData(): NPCs spawn during loadGameData() and must
    // snap their Y to the terrain heightfield (the JSON catalogue authors
    // every NPC at y = 0.5 but the heightfield reaches 50 m at peaks; an
    // unsnapped NPC is buried under the terrain at almost every authored
    // x,z). The terrain only exists once gameWorld_ is built, so we move
    // world construction earlier and wire the height sampler into
    // NPCSystem + WaveSystem here. The previous sequence
    //   loadGameData -> initializeUI -> ... -> gameWorld
    // shipped a world map populated entirely with sub-terrain NPCs that
    // the renderer dutifully skinned into invisibility — exactly the
    // disconnect the user-directive ("ship the cat") flagged.
    gameWorld_ = std::make_unique<GameWorld>(*cudaContext_, *physicsWorld_);
    gameWorld_->initialize();

    // Wire systems that consume terrain. PlayerControlSystem reads the
    // heightfield every frame for the cat's foot snap; NPCSystem +
    // WaveSystem sample once per spawn to ground-snap their entities.
    // The sampler captures `terrain` by raw pointer because the GameWorld
    // outlives every system in this scope (initialize -> shutdown
    // ordering). The bool guard on terrain != nullptr is defensive — a
    // CUDA-init failure inside GameWorld::initialize() leaves the
    // gameWorld_ valid but the terrain absent, in which case we fall
    // through to the previous (unsnapped) behaviour rather than dereference
    // a null sampler at first spawn.
    if (gameWorld_ != nullptr) {
        const auto* terrain = gameWorld_->getTerrain();
        if (playerControlSystem_ != nullptr) {
            playerControlSystem_->setCombatSystem(combatSystem_);
            playerControlSystem_->setElementalMagicSystem(magicSystem_.get());
            if (terrain != nullptr) {
                playerControlSystem_->setTerrain(terrain);
            }
        }
        if (terrain != nullptr) {
            auto sampler = [terrain](float x, float z) {
                return terrain->getHeightAt(x, z);
            };
            if (npcSystem_ != nullptr) {
                npcSystem_->setTerrainHeightSampler(sampler);
            }
            if (waveSystem_ != nullptr) {
                waveSystem_->setTerrainHeightSampler(sampler);
            }
        }
    }

    // Step 7: Load game data (NPCs spawn here; with terrain wired above
    // they ground-snap on creation) and game assets.
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

    // Wire CombatSystem into ElementalMagicSystem so spell hits + DOT ticks
    // route damage through CombatSystem::applyDamageWithType. This is what
    // makes the per-element hit-burst dispatcher (kHitProfiles[]) and per-
    // element death-burst dispatcher (kDeathProfiles[]) fire on spell impacts.
    //
    // BEFORE this wire-up, applySpellDamage and the DOT tick called
    // health->damage() directly — bypassing onHitCallback_ (no per-element
    // hit-burst on spell impact) and HealthComponent.lastDamageType (no per-
    // element death-burst on spell-killing-blow). Routing through CombatSystem
    // closes both gaps in a single call site so a Fire spell now produces
    // an orange-yellow burst and an Ice spell produces a pale-cyan burst,
    // automatically picked up by the existing dispatcher infrastructure
    // landed two iterations ago.
    //
    // Wired here (right after magicSystem_->init()) because both systems
    // exist by this point — combatSystem_ was created on line 179, and the
    // magic system is the dependent. The wire is non-owning (raw pointer);
    // CombatSystem outlives ElementalMagicSystem since the ECS owns the
    // former and CatAnnihilation owns the latter, and both are torn down
    // together at shutdown via ECS::shutdown() + the unique_ptr's destructor.
    if (combatSystem_ != nullptr && magicSystem_ != nullptr) {
        magicSystem_->setCombatSystem(combatSystem_);
    }

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

    // (Game world + terrain-dependent system wiring moved earlier in
    // initialize(), right after initializeUI(). See the block above
    // labelled "Step 6: Build the game world before loading content."
    // for the rationale — NPCs spawn during loadGameData() and need the
    // terrain heightfield available at spawn time so they don't end up
    // buried under the heightmap. Doing the wiring twice would double-
    // construct the GameWorld which trips a CUDA-context assertion.)

    // HealthSystem publishes enemy/player deaths through a plain callback —
    // wire it here so the score ticks up on dog kills and the state machine
    // transitions to GameOver when the player's HP reaches zero.
    //
    // When an enemy dies we also republish the death as an
    // EnemyKilledEvent on the game's event bus. This is the bridge between
    // the purely-ECS HealthSystem (which knows "entity X died, was enemy")
    // and the gameplay subsystems that listen for kills: LevelingSystem
    // awards XP, QuestSystem ticks objectives, GameAudio plays a death
    // sting, StoryModeSystem accrues chapter experience. Before this wire
    // those subscribers existed but nothing ever published to them, so XP
    // sat at 0 forever and kill-based quests could never complete.
    //
    // The killer is reported as the player entity because every death in
    // the current wave-survival harness is a consequence of player damage
    // (EnemyAISystem only hits the cat; enemies don't friendly-fire each
    // other). Once projectile/AoE/environmental damage gains independent
    // owners (e.g., turret NPCs), CombatSystem should populate the killer
    // field directly at the point of the killing blow and feed it through
    // the death callback — at that point the healthSystem_ callback
    // signature grows an `Entity killer` field and this site forwards it.
    if (healthSystem_ != nullptr) {
        healthSystem_->setOnEntityDeath([this](CatEngine::Entity entity, bool isEnemy) {
            if (isEnemy) {
                // NOTE: do NOT bump enemiesKilled_ here. The kill counter is
                // owned by onEnemyKilled() (the EnemyKilledEvent subscriber),
                // which is the single canonical point that also awards XP,
                // advances quests, and plays the death sting. Previously this
                // callback ALSO did `++enemiesKilled_`, and because we then
                // publish an EnemyKilledEvent below, each kill was counted
                // twice — the "[kill] Enemy died. Total kills: N" log would
                // show N (odd) but the HUD/heartbeat score would show N+1.
                // Keeping the counter write in exactly one place (the event
                // handler) makes the score monotonic and trivially testable.

                // Resolve the enemy's type label + XP reward. If the
                // EnemyComponent was never attached (shouldn't happen in
                // normal spawn flow, but we defend against it) fall back to
                // a generic Dog at its base 10 XP so the XP path is never
                // silently zero — a bug-finder would rather see "Total
                // kills rises but XP climbs" than a fully green dashboard
                // hiding a missed component.
                std::string enemyTypeName = "Dog";
                int xpReward = 10;
                if (auto* enemy = ecs_.getComponent<EnemyComponent>(entity)) {
                    xpReward = enemy->scoreValue;
                    switch (enemy->type) {
                        case EnemyType::Dog:     enemyTypeName = "Dog";     break;
                        case EnemyType::BigDog:  enemyTypeName = "BigDog";  break;
                        case EnemyType::FastDog: enemyTypeName = "FastDog"; break;
                        case EnemyType::BossDog: enemyTypeName = "BossDog"; break;
                    }
                }

                // Position for audio spatialization and death particles. A
                // missing Transform is rare but survivable — publishing at
                // the origin is visually wrong but won't crash the audio
                // engine (which spatializes against the listener position).
                Engine::vec3 enemyPosition(0.0F, 0.0F, 0.0F);
                if (auto* transform = ecs_.getComponent<Engine::Transform>(entity)) {
                    enemyPosition = transform->position;
                }

                eventBus_.publish(EnemyKilledEvent{
                    entity,
                    playerEntity_,
                    enemyTypeName,
                    xpReward,
                    enemyPosition});
            } else if (entity == playerEntity_) {
                Engine::Logger::info("[death] Player died, → GameOver");
                setState(GameState::GameOver);
            }

            // Publish the generic EntityDeathEvent regardless of enemy/player.
            // Until this iteration the event was declared (game_events.hpp:109)
            // and a subscriber was registered (CatAnnihilation::onEntityDeath
            // — spawns death-burst particles, sets GameOver on player death)
            // but no code path ever published it, so the death-particle
            // emitter wired up in createDeathParticleEmitter() was unreachable
            // dead code: every kill in the playtest log fired the lunge,
            // flinch, and death-pose beats but never the orange-red burst at
            // the kill site. Publishing here — the single canonical "this
            // entity died" callback fired from HealthSystem::handleDeath
            // for every death path (combat, DOT, scripted kill) — wakes
            // the subscriber and lights up the burst. Position is sourced
            // from the dying entity's Transform (same lookup we just did
            // for enemies; redo for the player branch which fell through
            // without one). A missing Transform is rare but survivable —
            // the burst spawns at world origin which is visually wrong
            // but won't crash the particle system.
            Engine::vec3 deathPosition(0.0F, 0.0F, 0.0F);
            if (auto* transform = ecs_.getComponent<Engine::Transform>(entity)) {
                deathPosition = transform->position;
            }
            EntityDeathEvent deathEvent(entity, playerEntity_,
                                        isEnemy ? "combat" : "player");
            deathEvent.deathPosition = deathPosition;
            deathEvent.wasPlayer = (entity == playerEntity_);

            // Recover the killing-blow damage type from the dying entity's
            // HealthComponent. CombatSystem::applyDamage and
            // applyDamageWithType both stamp lastDamageType BEFORE calling
            // health->damage(), so by the time we get here (after
            // HealthSystem::handleDeath has fired the onEntityDeath_
            // callback that landed us in this lambda) the field reflects
            // the type of the damage that drove hp to zero. Defaults to
            // Physical via the HealthComponent default — so a death from
            // a non-combat path (scripted HealthSystem::kill, future falling
            // damage, etc.) still produces a valid orange-red death burst.
            if (auto* health = ecs_.getComponent<HealthComponent>(entity)) {
                deathEvent.damageType = health->lastDamageType;
            }
            eventBus_.publish(deathEvent);
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

    // Companion non-killing-hit emitter — same dormant/re-enable pattern,
    // but tuned smaller and neutral-coloured so the on-screen mass of
    // particles during a 2-3 hit/sec combo cadence stays legible. Wired
    // to fire from CombatSystem::applyDamage via setOnHitCallback in
    // connectSystemEvents().
    createHitParticleEmitter();

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

void CatAnnihilation::createHitParticleEmitter() {
    // Companion to createDeathParticleEmitter() — see the comment in
    // CatAnnihilation.hpp for the high-level "why a separate, smaller,
    // neutral-coloured emitter for non-killing hits" rationale. The tuning
    // below is calibrated so a 2-3 hit/sec combo cadence stays visually
    // legible: 8 particles per burst (death is 50) keeps the screen from
    // saturating during multi-hit combos, the 0.25 m sphere shell is
    // tighter than death's 0.5 m so the burst reads as a localised
    // "thwack" rather than an explosion, and the 0.30-0.55 s lifetime
    // window is short enough that the burst clears before the next
    // swing's burst arrives but long enough (~18-33 frames at 60 fps)
    // to be perceptually distinct from a single-frame flash.
    //
    // The white-yellow albedo is deliberately neutral relative to the
    // orange-red death burst: a generic kinetic-impact hue that reads
    // the same regardless of whether the hit landed via melee, fire
    // spell, or frost projectile. Per-element variants (fire = orange,
    // frost = pale-blue, storm = white-cyan, light = yellow-white) are
    // a natural follow-on once we want the elemental magic system to be
    // visually distinguishable mid-combat — for now, "every hit looks
    // the same and every kill looks different" is the right legibility
    // contract because death is the rarer + more meaningful event.
    CatEngine::CUDA::ParticleEmitter hitEmitter;
    hitEmitter.enabled = false;  // Dormant; re-enabled per spawnHitParticles call.
    hitEmitter.shape = CatEngine::CUDA::EmissionShape::Sphere;
    hitEmitter.mode = CatEngine::CUDA::EmissionMode::OneShot;
    hitEmitter.shapeParams.sphereRadius = 0.25F;       // Half of death's 0.5 m.
    hitEmitter.shapeParams.sphereEmitFromShell = true;
    hitEmitter.burstEnabled = true;
    hitEmitter.burstCount = 8;                          // ~6× lighter than death's 50.

    // Velocity is symmetric on x/z and slightly biased upward on y so the
    // burst arcs outward-and-up like a small impact spray. Magnitudes are
    // ~half of the death-burst velocities (death is [-3,+3] / [+1,+5];
    // hit is [-1.5,+1.5] / [+0.5,+2.5]) so the particles cover proportionally
    // less ground in their shorter lifetime — the shape of the burst is
    // similar to death, just scaled to "tap" instead of "explosion".
    hitEmitter.initialProperties.velocityMin = Engine::vec3(-1.5F, 0.5F, -1.5F);
    hitEmitter.initialProperties.velocityMax = Engine::vec3(1.5F, 2.5F, 1.5F);
    hitEmitter.initialProperties.lifetimeMin = 0.30F;
    hitEmitter.initialProperties.lifetimeMax = 0.55F;
    hitEmitter.initialProperties.sizeMin = 0.04F;       // Slightly smaller than death (0.05).
    hitEmitter.initialProperties.sizeMax = 0.10F;       // Tighter range too (death is 0.05-0.15).

    // Neutral white-yellow base. Death uses (1.0, 0.3, 0.1, 1.0) — orange-red.
    // Hit uses (1.0, 0.95, 0.7, 1.0) — warm white biased slightly toward
    // yellow so it reads against both grass-green terrain and dog-fur
    // brown without being mistaken for the death burst's red.
    hitEmitter.initialProperties.colorBase = Engine::vec4(1.0F, 0.95F, 0.70F, 1.0F);
    hitEmitter.initialProperties.colorVariation = Engine::vec4(0.05F, 0.05F, 0.10F, 0.0F);
    hitEmitter.fadeOutAlpha = true;       // Same as death — fade rather than pop out.
    hitEmitter.scaleOverLifetime = true;
    hitEmitter.endScale = 0.0F;

    if (particleSystem_ != nullptr) {
        hitEmitterId_ = particleSystem_->addEmitter(hitEmitter);
    }
}

void CatAnnihilation::initializeUI() {
    Engine::Logger::info("Initializing UI systems...");

    // Create main UI container. GameUI::initialize() constructs and
    // initializes the actual HUD / MainMenu / PauseMenu / WavePopup screen
    // objects internally — we do NOT construct our own. See the comment on
    // the hud_ / mainMenu_ / pauseMenu_ members in CatAnnihilation.hpp for
    // why: independently-owning them was the source of a double-init and
    // a silently-broken pause menu.
    gameUI_ = std::make_unique<Game::GameUI>(*input_, *gameAudio_);
    if (!gameUI_->initialize()) {
        Engine::Logger::error("Failed to initialize game UI");
        return;
    }

    // Point the legacy aliases at GameUI's owned screens so the rest of
    // this translation unit can keep using `hud_->showNotification(...)`
    // etc. unchanged.
    hud_ = &gameUI_->getHUD();
    mainMenu_ = &gameUI_->getMainMenu();
    pauseMenu_ = &gameUI_->getPauseMenu();

    // ----- MainMenu callbacks + bindings -----------------------------------
    // Wired directly onto the GameUI-owned instance (which is what
    // GameUI::handleInput routes to); mainMenu_ is an alias to that same
    // instance so either reference is equivalent here.
    mainMenu_->setStartGameCallback([this]() {
        startNewGame(false); // arcade mode
    });
    mainMenu_->setContinueCallback([this]() {
        continueGame();
    });
    mainMenu_->setQuitCallback([]() {
        // Handled by main loop (ESC/quit button closes window)
    });
    if (imguiLayer_ != nullptr) {
        mainMenu_->setImGuiLayer(imguiLayer_);
        hud_->setImGuiLayer(imguiLayer_);
        gameUI_->getWavePopup().setImGuiLayer(imguiLayer_);
    }
    // Thread settings bindings through to the live main menu so the
    // Settings panel's sliders / checkboxes drive real engine systems.
    mainMenu_->setSettingsBindings(window_, renderer_, gameConfig_);

    // ----- PauseMenu callbacks + bindings ----------------------------------
    // Pre-refactor these fired on a separate pauseMenu_ instance that
    // GameUI never routed input to, so the in-game pause screen's Resume /
    // Main Menu / Quit buttons did nothing. Wiring them on the GameUI-owned
    // instance (via the alias) makes the pause menu actually work.
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

    Engine::Logger::info("UI systems initialized");
}

void CatAnnihilation::loadGameData() {
    Engine::Logger::info("Loading game data...");

    // Load quest data.
    // WHY: the file lives at assets/quests/quests.json (see assets/quests/ on
    // disk and QUEST_SYSTEM_INTEGRATION.md). The old path assets/data/quests.json
    // does not exist, so the fopen silently failed and QuestSystem fell back to
    // its hard-coded quests_data table. The fallback still works but the WARN
    // that documented it was also being swallowed by a now-fixed logger bug in
    // quest_system.cpp, so the miswired path was invisible at runtime.
    if (questSystem_ != nullptr) {
        questSystem_->loadQuestsFromFile("assets/quests/quests.json");
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

    // Pre-load dog GLB variants so wave-1's first-of-each-variant spawn
    // doesn't synchronously block the game thread on disk-I/O + glTF parse.
    //
    // Why this is here and not in DogEntity itself:
    //   The cat NPCs are pre-loaded as a side effect of NPCSystem reading
    //   npcs.json — every NPC entry triggers a CatEntity attach which goes
    //   through AssetManager::LoadModel. There is no equivalent file for
    //   dog variants (the wave system just calls DogEntity::create<type>
    //   on demand), so the explicit pre-load lives at the same scope as
    //   other "warm caches before the main loop starts" work.
    //
    // Empirical motivation (2026-04-26 cat-verify evidence row #6):
    //   Without this call, BigDog spawns at ~frame 150 and the next
    //   heartbeat reads fps=14 (vs the wave-2 BigDog respawn at frame
    //   ~960 which reads fps=56 because the model is cached by then).
    //   The 4× delta is the disk-read + glTF parse for dog_big.glb's
    //   ~250k-vertex Meshy export. Pre-loading folds that cost into
    //   engine init, where 200 ms is invisible alongside the 2.4 s the
    //   16-cat NPC pre-load already spends.
    //
    // Cost: four LoadModel calls during init. Idempotent — see the
    //   docblock on DogEntity::PreloadAllVariants for why calling this
    //   twice (e.g., from a future restart-to-main-menu flow) is safe.
    DogEntity::PreloadAllVariants();

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

    // Hit-burst particle wire — see createHitParticleEmitter / spawnHitParticles
    // for the emitter design and the long-form rationale on cadence + tuning.
    //
    // Why we gate on the target's HealthComponent::isDead:
    //
    // CombatSystem::applyDamage fires onHitCallback_ AFTER health->damage()
    // has applied the damage (so isDead reflects the post-hit state) but
    // BEFORE the kill-callback path. That means on a killing blow we see
    // BOTH this callback fire AND the death-burst path fire (via
    // setOnEntityDeath -> EntityDeathEvent -> onEntityDeath ->
    // spawnDeathParticles). Without the gate, every killing blow would
    // double-burst — a small white-yellow puff layered under the larger
    // orange-red death cloud — which adds visual noise without delta and
    // muddies the legibility contract ("hits look generic, kills look
    // distinct"). Skipping the hit burst when the same hit also produced
    // the kill keeps the contract clean.
    //
    // Why we look up the HealthComponent from the ECS instead of trusting
    // a flag on HitInfo:
    //
    // HitInfo today does not carry an isKill bit — adding one would
    // require touching CombatSystem::applyDamage's struct fill plus every
    // other applyDamage variant. Reading the post-state via
    // ecs_.getComponent<HealthComponent>(target) is one map probe per
    // damage tick (~6-12 Hz peak combat), which is cheap relative to the
    // ParticleSystem::triggerBurst path it replaces or pairs with, and
    // keeps the change purely additive in the game layer.
    //
    // Why not gate on a position-distance test against the most recent
    // death position (i.e. "if a death burst already fired this frame at
    // ~the same xyz, skip the hit burst"):
    //
    // Multi-attacker scenarios make distance gating unreliable — two
    // simultaneous melee hits on adjacent enemies in a wave-3 cluster
    // sit at <1 m apart, well within any death-burst-radius gate. The
    // per-target isDead flag is the canonical signal because it
    // describes exactly what we want to suppress: a hit burst on the
    // entity that just died.
    if (combatSystem_ != nullptr) {
        combatSystem_->setOnHitCallback([this](const HitInfo& hitInfo) {
            if (auto* targetHealth = ecs_.getComponent<HealthComponent>(hitInfo.target)) {
                if (targetHealth->isDead) {
                    return;  // Killing blow — death-burst path will fire.
                }
            }
            // Per-element hit-burst: forward the HitInfo's damageType so the
            // dispatcher inside spawnHitParticles picks the matching profile
            // (warm-white for Physical, orange-yellow for Fire, pale-cyan for
            // Ice, yellow-green for Poison, white-purple for Magic).
            // CombatSystem::applyDamage and applyDamageWithType both populate
            // hitInfo.damageType so callers don't need to know the source —
            // melee + projectile hits arrive as Physical, DOT ticks arrive
            // as the DOT's element (Burning→Fire, Frozen→Ice, Poisoned→
            // Poison), spell-direct hits arrive as the spell's element.
            spawnHitParticles(hitInfo.hitPosition, hitInfo.damageType);
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

    // Shutdown UI. The hud_ / mainMenu_ / pauseMenu_ raw pointers are
    // non-owning aliases into gameUI_'s internal screens (see the member
    // comment in CatAnnihilation.hpp) — gameUI_->shutdown() destroys them
    // exactly once. Clear the aliases first so any late-arriving event
    // (e.g. a destructor-fired callback on a still-live ECS system) can't
    // touch a dangling pointer.
    hud_ = nullptr;
    mainMenu_ = nullptr;
    pauseMenu_ = nullptr;
    if (gameUI_ != nullptr) { gameUI_->shutdown(); }

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

    // Tick every entity's Animator. CatEntity::create / DogEntity::create
    // build per-entity Animators (37-bone cat rig + 7 clips, 34-bone dog rig
    // + 7 clips after the rig_quadruped pass) and call play("idle"), but
    // until this loop landed nobody was actually advancing them per-frame —
    // the cat and dogs would render frozen at t=0 of the idle clip even
    // once the renderer wires up real skinned-mesh draws. We walk
    // MeshComponent here (not a dedicated AnimationSystem subclass) because
    // the tick is a single line and an entire system class plus
    // CMakeLists/registration boilerplate would dwarf the actual work for
    // no reuse benefit yet. If we later need ordering relative to other
    // systems (e.g. IK after physics, root-motion before locomotion), we
    // promote this to a real System with a priority and move it into
    // engine/animation/. The current pipeline order is: AI / physics happen
    // above, animator advances bone state here, then the renderer pulls the
    // resulting skinning matrices via animator->getCurrentSkinningMatrices()
    // in MeshSubmissionSystem / ScenePass once skinned draws land. Entities
    // with no animator (terrain, props, projectiles) skip the tick — the
    // null-check is free-form and there are no allocations on the hot path.
    //
    // Per-frame "speed" injection: when an entity has both an animator and
    // a MovementComponent we feed the current horizontal speed into the
    // animator's float parameter bag BEFORE update(dt). The locomotion
    // state machine wired by `wireLocomotionTransitions` (see
    // game/components/LocomotionStateMachine.hpp) reads "speed" inside
    // Animator::checkTransitions to decide idle <-> walk <-> run. NPCs
    // without a MovementComponent (stationary mentors / merchants) keep
    // the default 0.0 and stay in idle, which is exactly the desired
    // behaviour — they're scenery, not walking dogs.
    ecs_.forEach<MeshComponent>(
        [this, dt](CatEngine::Entity entity, MeshComponent* meshComponent) {
            if (meshComponent == nullptr || !meshComponent->animator) {
                return;
            }
            auto* movementComponent =
                ecs_.getComponent<MovementComponent>(entity);
            // Skip the locomotion speed-feed for an entity that's already
            // been put into its death pose by HealthSystem::handleDeath.
            // The locomotion-state-machine transitions only have edges
            // between idle/walk/run, so they cannot pull a layDown back
            // to walk — but writing a stale "speed" value into the
            // parameter bag of a dead entity is sloppy and would hide a
            // future regression where someone wires a layDown→idle edge
            // via wireLocomotionTransitions and the corpse spontaneously
            // stands up before despawn. The deathPosed latch is set
            // exactly once in HealthSystem::handleDeath; see
            // MeshComponent.hpp's deathPosed docblock for the full
            // rationale on why this gate lives here vs in the
            // animator runtime itself.
            if (movementComponent != nullptr && !meshComponent->deathPosed) {
                meshComponent->animator->setFloat(
                    "speed", movementComponent->getCurrentSpeed());
            }
            meshComponent->animator->update(dt);

            // Skip idle-variant cycling on dying entities. Without this
            // gate, a stationary dying NPC (a clan-mentor cat hit by an
            // out-of-range BossDog AOE) would have its layDown clip
            // overwritten the moment its Resting phase elapses (3-13 s
            // depending on seed jitter) — replacing the death pose with
            // a standUpFromLay clip mid-corpse-decay, which reads as
            // "the dead cat just got up and walked off". The gate
            // intentionally returns BEFORE the cycler's gate-off
            // constexpr check below so dead entities short-circuit the
            // entire cycler regardless of whether the cycler itself is
            // enabled.
            if (meshComponent->deathPosed) {
                return;
            }

            // ---- Idle-variant cycling: gated off (timing-dependent crash) -
            //
            // Status (2026-04-25): the cycler is fully written below but
            // gated off because flipping the constexpr to true reliably
            // crashes the game inside the first ~150 ms of the main loop —
            // BEFORE any cycler play() call has fired (first cooldown is
            // 4-12 s, so no transition is meant to happen until well into
            // gameplay). The crash dies silently after the first-frame
            // MeshSubmission survey print and never reaches a heartbeat.
            //
            // The 2026-04-25 iteration confirmed the bug is timing-dependent.
            // With a 7-line `Logger::info` in Animator::startTransition
            // printing state names + animation pointers + pose sizes, the
            // game survived 50 s of clean autoplay with the cycler firing
            // dozens of sit / lay / standUp transitions across cats and
            // wave-spawned dogs (rigs of 36/37/38 bones — bone counts and
            // pose sizes matched on every transition logged). With the
            // diagnostic call removed, the game crashes on first frame
            // again. The Logger::info added enough per-startTransition
            // latency (string concat + lock + IO) to mask whatever race
            // is happening — but startTransition isn't even called yet
            // when the crash hits, so the masking is happening somewhere
            // upstream (likely a per-frame cycler bookkeeping path that
            // hits a hot-cache contention or a buffer-not-ready window).
            //
            // Concrete next-iteration repro:
            //   1. flip constexpr to `true` here, build, run
            //      `CatAnnihilation.exe --autoplay --exit-after-seconds 12`
            //      → crashes ~150 ms after "Entering main loop", no transition
            //      log lines, no heartbeat.
            //   2. add a `Logger::info("[anim] startTransition ...")` line
            //      with similar string-concat cost at the top of
            //      Animator::startTransition (or anywhere else that runs
            //      every animator->update()) → game survives 50 s clean.
            // The latency-masking points the diagnosis at something like
            // a per-frame structure that's being read before it's been
            // written by another system — most likely either (a) the
            // skinning-palette upload path racing against the first
            // animator->update() because the palette buffer isn't fully
            // descriptor-bound yet on frame 1, (b) a swapchain-readback
            // fence not yet signalled when the cycler-bearing entity is
            // first scheduled, or (c) an ECS component-vector being
            // resized between the player-cat spawn and the wave-dog spawn,
            // invalidating an iterator the cycler holds. Any of those
            // would be sensitive to per-frame timing.
            //
            // Cost of the gate: one branch per animator-bearing entity per
            // frame, identical to the previous bisect marker it replaced.
            //
            // 2026-04-25 update: the gate flipped to `true` after the multi-
            // iteration arc of Vulkan UB fixes (VulkanBuffer::Map double-map,
            // VulkanSwapchain TRANSFER_DST/SRC, RecreateSwapchain WaitIdle,
            // ScenePass::OnResize always-rebuild) resolved the post-first-
            // frame silent crash window. The "timing-dependent crash" the
            // earlier comments tracked was a SYMPTOM of those underlying
            // Vulkan validation errors — the validation-layer mutex latency
            // and the Logger::info-in-startTransition both papered over the
            // same race by adding per-frame work that happened to keep one
            // of the freed Vulkan handles alive long enough not to fault.
            // With the source bugs fixed, the cycler runs without the
            // diagnostic prop. If a regression resurfaces, the recipe is:
            // (a) flip back to `false`, (b) re-run with `--validation` to
            // see which fence/semaphore/imageview/framebuffer is being
            // used after free, (c) fix the lifetime there — not here.
            constexpr bool kIdleVariantCyclingEnabled = true;
            if (!kIdleVariantCyclingEnabled) {
                return;
            }

            // Idle-variant cycling: stationary entities periodically play
            // sitDown / layDown, hold the pose, then play the matching
            // standUp clip and return to idle. We tick this AFTER
            // animator->update(dt) so we observe the post-update value of
            // `isPlaying` — the animator clears it the instant a
            // non-looping clip's m_currentTime reaches its duration, which
            // is our signal that GoingDown / ComingUp finished. See
            // MeshComponent.hpp for the rationale on the 4-phase machine
            // and the per-entity seeding.
            //
            // Speed gate: if the entity is moving (>5 cm/s, well below any
            // walk threshold) we treat it as "leaving idle" — break out of
            // the variant and return to looping idle. The locomotion state
            // machine in LocomotionStateMachine.hpp will then transition
            // idle->walk on its own once we've handed it back. NPCs with
            // no MovementComponent fall through with `speed=0` and cycle
            // forever (which is exactly the intent — they're stationary
            // scenery and the cycling is the *only* visible motion they
            // contribute to the world).
            //
            // Cost: one branch + at most one std::string compare per
            // entity per frame on the steady-state Idle path
            // (idleVariantNextDelay -= dt + zero-vs-positive compare).
            // The play() and seed-init paths fire at most once per
            // multi-second cycle. ECS-side this adds nothing — we
            // already had the lambda + getComponent<MovementComponent>.
            constexpr float kIdleVariantStillSpeed = 0.05F;
            const float currentSpeed = (movementComponent != nullptr)
                ? movementComponent->getCurrentSpeed() : 0.0F;
            const bool stationary = currentSpeed <= kIdleVariantStillSpeed;

            if (!stationary) {
                if (meshComponent->idleVariantPhase
                        != MeshComponent::IdleVariantPhase::Idle) {
                    // Movement break-out: collapse whatever variant is
                    // running back to idle so the locomotion SM can take
                    // over. Use a 100 ms blend so the transition isn't a
                    // pop — short enough that the cat doesn't appear to
                    // "decide" to stand up over half a second when the
                    // player jerks the joystick.
                    meshComponent->animator->play("idle", 0.10F);
                    meshComponent->idleVariantPhase =
                        MeshComponent::IdleVariantPhase::Idle;
                    meshComponent->idleVariantPhaseTimer = 0.0F;
                    // Reseed the next-delay so the cat doesn't immediately
                    // try to sit again the moment it stops; -1 triggers
                    // the lazy-init path on the next stationary tick.
                    meshComponent->idleVariantNextDelay = -1.0F;
                }
                return;
            }

            // Lazy-seed on first stationary observation. Knuth's
            // multiplicative hash on the entity id gives us 8 bits of
            // jitter without needing a global RNG — and crucially each
            // call site (16 NPCs spawned in the same world-load frame)
            // gets a distinct seed because each entity has a distinct id.
            // Without this all 16 cats would sit at the same instant.
            if (meshComponent->idleVariantNextDelay < 0.0F) {
                const uint64_t hashed = entity.id * 2654435761ULL;
                meshComponent->idleVariantSeed =
                    static_cast<uint8_t>((hashed >> 16) & 0xFF);
                const float jitterFraction =
                    static_cast<float>(meshComponent->idleVariantSeed) / 255.0F;
                // First cooldown 4-12 s — short enough that the user sees
                // the first sit within ~10 s of standing in the village,
                // long enough that cats don't immediately start sitting
                // the instant the world loads (which would feel scripted).
                meshComponent->idleVariantNextDelay = 4.0F + 8.0F * jitterFraction;
            }

            meshComponent->idleVariantPhaseTimer += dt;

            switch (meshComponent->idleVariantPhase) {
                case MeshComponent::IdleVariantPhase::Idle: {
                    meshComponent->idleVariantNextDelay -= dt;
                    if (meshComponent->idleVariantNextDelay > 0.0F) {
                        break;
                    }
                    // Pick sit (~70 %) vs lay (~30 %) using the seed's LSB
                    // mod 10. Sit is the more common, shorter cycle —
                    // bench cats sit far more often than they lay, and
                    // sitDown reads more clearly at gameplay distance
                    // than the longer layDown silhouette. Rotate the seed
                    // each cycle (Knuth-hash the previous seed) so the
                    // same cat alternates choices over time instead of
                    // always picking the same variant.
                    const uint8_t pickByte =
                        static_cast<uint8_t>(meshComponent->idleVariantSeed % 10U);
                    const bool pickLay = pickByte >= 7U;
                    const char* downClipName = pickLay ? "layDown" : "sitDown";
                    if (meshComponent->animator->hasState(downClipName)) {
                        meshComponent->animator->play(downClipName, 0.20F);
                        meshComponent->idleVariantPhase =
                            MeshComponent::IdleVariantPhase::GoingDown;
                        meshComponent->idleVariantPhaseTimer = 0.0F;
                        meshComponent->idleVariantUsedLay = pickLay;
                    } else {
                        // Asset doesn't ship the chosen down-clip — wait
                        // another short cooldown and try again rather than
                        // looping play() on a missing state every frame.
                        meshComponent->idleVariantNextDelay = 6.0F;
                    }
                    break;
                }

                case MeshComponent::IdleVariantPhase::GoingDown: {
                    // Animator clears m_playing the instant the
                    // non-looping clip's currentTime reaches its duration.
                    // We hold a 0.20 s safety floor in phase timer so a
                    // very short clip + transition doesn't false-positive
                    // before the play() request has actually started
                    // advancing the animation.
                    const bool clipDone =
                        !meshComponent->animator->isPlaying() &&
                        meshComponent->idleVariantPhaseTimer > 0.20F;
                    if (clipDone) {
                        meshComponent->idleVariantPhase =
                            MeshComponent::IdleVariantPhase::Resting;
                        meshComponent->idleVariantPhaseTimer = 0.0F;
                    }
                    break;
                }

                case MeshComponent::IdleVariantPhase::Resting: {
                    // Hold duration scales with whether we sat or laid:
                    // sit-rests are 3-7 s (a brief beat), lay-rests are
                    // 6-12 s (a deliberate snooze). Both are jittered by
                    // the entity seed so cats don't all stand up at the
                    // same instant.
                    const float jitterFraction =
                        static_cast<float>(meshComponent->idleVariantSeed) / 255.0F;
                    const float restDuration = meshComponent->idleVariantUsedLay
                        ? (6.0F + 6.0F * jitterFraction)
                        : (3.0F + 4.0F * jitterFraction);
                    if (meshComponent->idleVariantPhaseTimer >= restDuration) {
                        const char* upClipName = meshComponent->idleVariantUsedLay
                            ? "standUpFromLay"
                            : "standUpFromSit";
                        if (meshComponent->animator->hasState(upClipName)) {
                            meshComponent->animator->play(upClipName, 0.20F);
                            meshComponent->idleVariantPhase =
                                MeshComponent::IdleVariantPhase::ComingUp;
                            meshComponent->idleVariantPhaseTimer = 0.0F;
                        } else {
                            // Stand-up clip missing — give up gracefully
                            // by snapping back to idle. Better than
                            // freezing in the seated pose forever, and
                            // the locomotion SM will pick up cleanly.
                            meshComponent->animator->play("idle", 0.20F);
                            meshComponent->idleVariantPhase =
                                MeshComponent::IdleVariantPhase::Idle;
                            meshComponent->idleVariantPhaseTimer = 0.0F;
                            // Re-roll seed for next cycle (see Idle case).
                            meshComponent->idleVariantSeed = static_cast<uint8_t>(
                                (meshComponent->idleVariantSeed * 73U + 41U) & 0xFFU);
                            meshComponent->idleVariantNextDelay = 8.0F + 12.0F * jitterFraction;
                        }
                    }
                    break;
                }

                case MeshComponent::IdleVariantPhase::ComingUp: {
                    const bool clipDone =
                        !meshComponent->animator->isPlaying() &&
                        meshComponent->idleVariantPhaseTimer > 0.20F;
                    if (clipDone) {
                        meshComponent->animator->play("idle", 0.15F);
                        meshComponent->idleVariantPhase =
                            MeshComponent::IdleVariantPhase::Idle;
                        meshComponent->idleVariantPhaseTimer = 0.0F;
                        // Re-roll seed so the next cycle's sit-vs-lay
                        // pick + first-cooldown jitter aren't a function
                        // of just the entity id (otherwise the same cat
                        // would always pick the same variant on every
                        // cycle for the rest of the session).
                        const float jitterFraction =
                            static_cast<float>(meshComponent->idleVariantSeed) / 255.0F;
                        meshComponent->idleVariantSeed = static_cast<uint8_t>(
                            (meshComponent->idleVariantSeed * 73U + 41U) & 0xFFU);
                        // Next cooldown 8-20 s (longer than the first one
                        // so consecutive cycles don't feel rushed).
                        meshComponent->idleVariantNextDelay = 8.0F + 12.0F * jitterFraction;
                    }
                    break;
                }
            }
        });

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

    // Decay the camera-shake envelope. We do this AFTER all systems have
    // run (so a kill processed inside HealthSystem during this same tick
    // gets a full first-frame envelope of 1.0 rather than being decayed
    // by dt before the camera setup reads it on the next frame), but
    // BEFORE the camera setup in the parent update() reads it (because
    // the camera setup runs LATER inside update() after updateSystems
    // returns, so the decrement-here / read-there ordering is consistent
    // with the in-flight envelope progressing one tick per frame).
    if (cameraShakeRemaining_ > 0.0F) {
        cameraShakeRemaining_ = std::max(0.0F, cameraShakeRemaining_ - dt);
        // When the envelope tips zero, also clear amplitude so a stale
        // value doesn't leak into the next shake's max-merge calculation
        // (a kill 5 minutes after the last shake should not "compound"
        // with whatever the previous shake's amplitude was).
        if (cameraShakeRemaining_ <= 0.0F) {
            cameraShakeAmplitude_ = 0.0F;
            cameraShakeDuration_ = 0.0F;
        }
    }
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
    // Per-frame top-level render entry. Early-out cheaply on uninitialized
    // state so window callbacks fired during shutdown can't crash. World /
    // particle geometry is submitted through the Renderer's scene graph —
    // this function is only responsible for driving ScenePass camera state
    // and UIPass (HUD / menus / ImGui overlays).
    if (!initialized_ || renderer_ == nullptr) {
        return;
    }

    auto* uiPass = renderer_->GetUIPass();
    if (uiPass == nullptr) {
        return;
    }

    uint32_t screenWidth = renderer_->GetWidth();
    uint32_t screenHeight = renderer_->GetHeight();

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

            // Iteration 3d sub-task (d): bind the particle system to the
            // scene pass once it's live. ParticleSystem is constructed in
            // initialize() so by the first Playing-state frame it's
            // guaranteed non-null; we still guard for null defensively
            // because the construction path can early-return on CUDA init
            // failure (e.g. missing driver, no compatible GPU) and the game
            // has to keep running with ribbons disabled.
            if (!particleSystemBoundToScenePass_ && particleSystem_ != nullptr) {
                scenePass->SetParticleSystem(particleSystem_.get());
                particleSystemBoundToScenePass_ = true;
            }

            // Third-person camera follows the player. PlayerControlSystem
            // handles the camera *position* (orbit around the player driven
            // by mouse yaw/pitch + smoothed lerp), but we override the
            // *look-at target* here to point directly at the player's
            // torso instead of accepting the system's forward vector.
            //
            // Why: the system computes `camTarget = camPos + rotate(0,0,-1)`,
            // which means the camera looks along a fixed pitched ray
            // regardless of where the player actually is. With the default
            // offset (0, 5, 10) and pitch -0.3 rad, the player ends up
            // ~24° below the camera's view axis — which is exactly the
            // bottom-of-frame clipping the user-directive ("ship the cat",
            // 2026-04-24 18:58) flagged as the camera-framing bug. The
            // playtest screenshot from this iteration's baseline frame-
            // dump confirmed it: the cat sat at the very bottom of the
            // frame, half-cropped, while the rest of the scene was sky
            // and distant terrain.
            //
            // The fix is a classic look-at: derive the view direction
            // from camPos -> playerPos so the cat is ALWAYS centred in
            // frame, no matter what pitch/yaw the player commands. We
            // aim ~0.75 m above the player's transform origin (cat
            // torso/head height — the rigged Meshy GLBs put the origin
            // at the feet and stand ~1.0–1.5 m tall) so the cat fills
            // the frame around the cross-hair instead of appearing
            // above or below it. PlayerControlSystem's yaw/pitch are
            // still authoritative for camera *position* — they orbit
            // the camera around the player just as before. Only the
            // look direction is now anchored to the cat.
            Engine::vec3 camPos;
            Engine::vec3 camTarget;
            bool haveCamera = false;
            if (playerControlSystem_ != nullptr && ecs_.isAlive(playerEntity_)) {
                camPos = playerControlSystem_->getCameraPosition();
                if (auto* playerXform =
                        ecs_.getComponent<Engine::Transform>(playerEntity_)) {
                    camTarget = playerXform->position +
                                Engine::vec3(0.0F, 0.75F, 0.0F);
                } else {
                    // Player entity exists but has no Transform yet (the
                    // first frame of a game-restart can momentarily land
                    // in this branch between createPlayer() and the next
                    // tick). Fall back to the system's forward-vector
                    // target so the camera doesn't snap to (0,0,0).
                    camTarget = camPos +
                                playerControlSystem_->getCameraForward();
                }
                haveCamera = true;
            }
            // Fallback: static overview if the player isn't alive yet
            if (!haveCamera) {
                camPos = Engine::vec3(0.0F, 120.0F, 260.0F);
                camTarget = Engine::vec3(0.0F, 20.0F, 0.0F);
            }

            // Apply kill-feedback camera shake. Adds a small (≤ 0.25 m peak,
            // quadratically-decaying) 3D-noise offset to camPos ONLY — not
            // camTarget — so the look direction stays anchored to the cat
            // even as the camera position jitters. If we shook camTarget
            // too, the look-direction would slosh away from the player and
            // the cat would slide around the frame during the shake, which
            // breaks the previous iteration's framing-anchor work. Cost
            // when no shake is active: one early-return inside the sampler
            // (cameraShakeRemaining_ <= 0 check) — single compare, no
            // allocation, no trig, no branch into the noise composer.
            camPos += sampleCameraShakeOffset();

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
            //
            // Frustum: extracted from the same viewProj we use for the scene
            // pass so cull and draw agree on what's on-screen. Each Meshy
            // rigged cat is 100k-200k verts and the world has 16 NPCs
            // scattered across multiple clans plus the wave-spawned dogs;
            // skipping the off-camera ones is the difference between a
            // CPU-skinning bottleneck (every-NPC-every-frame at ~3M verts)
            // and a budget the player camera can afford. MeshSubmissionSystem
            // gates the cull on `frustum != nullptr`, so passing it always
            // is the correct call — there's no performance reason to ever
            // skip it for a normal in-game frame.
            Engine::Frustum cullFrustum = Engine::Frustum::fromMatrix(viewProj);
            static CatEngine::Renderer::MeshSubmissionSystem meshSubmission;

            // Distance cull radius (metres). Sized to comfortably cover the
            // gameplay arena around the player — wave-spawn radius is ~50 m
            // (game/systems/wave_system.cpp picks spawn positions on a ring),
            // and a typical engagement happens within 30 m. 80 m gives a
            // generous buffer for: (1) the player chasing or being chased
            // toward the edge of the arena; (2) any same-clan NPC sitting
            // on the rim of the spawn ring; (3) a few metres of slop so
            // entities crossing the threshold don't pop in/out frame-by-frame.
            //
            // Empirical motivation (cat-verify evidence row #8, frame=600):
            // during wave-1 cleared the camera widens enough that 15 of 17
            // visited entities pass the frustum cull, all 15 emit, and the
            // GPU pipeline collapses to 10 fps for ~5 s. A distance cull
            // at 80 m drops the same scene to ~3-5 emitted (player + nearby
            // dogs + adjacent same-clan mentor) — well within budget.
            //
            // Why 80 m and not 50 m: 50 m would cut some of the spawn-ring
            // dogs at the very moment they spawn (the wave system places
            // them on a 50 m ring centred on the player), which would make
            // dogs literally pop into existence as they cross the radius
            // toward the player. Picking 80 m means the spawn ring is
            // always inside the cull distance and dogs are visible from
            // the moment they spawn. The 30 m buffer also covers the case
            // where the autoplay AI moves the player toward the edge of
            // the arena — the player is always at the centre of the cull
            // sphere by construction (it's anchored to camPos which is
            // playerPos + camera offset).
            constexpr float kMeshDistanceCullMetres = 80.0F;
            meshSubmission.Submit(ecs_,
                                  static_cast<std::size_t>(renderer_->GetFrameIndex()),
                                  entityDraws,
                                  &cullFrustum,
                                  &camPos,
                                  kMeshDistanceCullMetres);

            auto* sceneCmdBuffer = renderer_->GetCommandBuffer();
            // DIAG: log per-frame whether the scene Execute path is reached.
            // Suspicion: ScenePass::Execute fires only on frame 1 then never
            // again, leaving the swapchain showing only the BeginFrame clear
            // color. This print should fire ~60×/sec if scene rendering is
            // alive, or only once if it bails out.
            static int sceneRenderCallCount = 0;
            ++sceneRenderCallCount;
            if (sceneRenderCallCount == 1
                || sceneRenderCallCount == 30
                || sceneRenderCallCount == 60
                || sceneRenderCallCount == 300
                || sceneRenderCallCount == 600) {
                std::cerr << "[CatRender-DIAG] frame=" << sceneRenderCallCount
                          << " sceneCmdBuffer=" << (sceneCmdBuffer ? 1 : 0)
                          << " entityDraws=" << entityDraws.size() << "\n";
            }
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

    // GameUI drives all 2D screen rendering (main menu, HUD when Playing,
    // HUD + pause overlay when Paused, wave popup, end-game overlay). It
    // reads currentState from its own member that setState() keeps in
    // sync, so one call here covers every game state. Pre-refactor this
    // function also rendered hud_ and pauseMenu_ directly on top of
    // GameUI::render() — but those were aliases into the same instances
    // GameUI already drew, so every frame in Playing / Paused drew the
    // HUD / PauseMenu twice, wasting UIPass batching and producing a
    // slight double-alpha visual artifact on translucent HUD elements.
    if (gameUI_ != nullptr) {
        gameUI_->render(*uiPass, screenWidth, screenHeight);
    }

    // Game Over / Victory overlay — emitted here as ImGui widgets so the
    // UIPass composite step picks them up in the same render pass.
    if (imguiLayer_ != nullptr &&
        (currentState_ == GameState::GameOver || currentState_ == GameState::Victory)) {
        renderEndScreenOverlay(screenWidth, screenHeight);
    }

    // EndFrame sorts/batches the collected draw commands; Execute replays
    // them onto the renderer's current command buffer inside the UI subpass.
    uiPass->EndFrame();

    auto* cmdBuffer = renderer_->GetCommandBuffer();
    if (cmdBuffer != nullptr) {
        uiPass->Execute(cmdBuffer, renderer_->GetFrameIndex());
    } else {
        std::cerr << "[CatAnnihilation::render] WARNING: command buffer is NULL\n";
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

    // Recreate player and re-populate world entities (NPCs etc.) since
    // clearEntities() above just wiped them. Without the second call, the
    // 16 world-map NPCs that loadGameData() spawns at engine init never
    // come back, so every restart leaves the player solo on an empty map.
    createPlayer();
    repopulateWorldEntities();

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

    // Clear existing entities and recreate player. NPCs and other world-
    // persistent entities also have to come back because clearEntities()
    // wipes them — see repopulateWorldEntities() for why this re-spawn
    // pass is mandatory on every game-start path.
    ecs_.clearEntities();
    createPlayer();
    repopulateWorldEntities();

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

void CatAnnihilation::repopulateWorldEntities() {
    // No NPCSystem in this build — fine, just nothing to re-spawn. Headless
    // tests / unit harnesses that exercise CatAnnihilation without the JSON
    // catalogue hit this branch and return cleanly.
    if (npcSystem_ == nullptr) {
        return;
    }

    // Reset NPCSystem's internal "id -> NPCData" map so the upcoming
    // loadNPCsFromFile pass doesn't collide with the dead-entity bookkeeping
    // left over from the prior session. clearAll also resets dialog/shop/
    // training state in case a `restart()` was triggered mid-conversation.
    npcSystem_->clearAll();

    // The catalogue path is the same one loadGameData uses at startup. We
    // don't read it from a config slot because the NPC roster is part of
    // the world definition, not per-save state — even a fresh game in a
    // new save slot expects the same 16 mentors/leaders/merchants.
    const std::string npcCataloguePath = "assets/npcs/npcs.json";
    if (!npcSystem_->loadNPCsFromFile(npcCataloguePath)) {
        Engine::Logger::warn(
            "repopulateWorldEntities: failed to reload NPCs from " +
            npcCataloguePath + " — world map will be empty this session");
    }
}

// ----------------------------------------------------------------------------
// Per-element particle tuning tables
// ----------------------------------------------------------------------------
//
// Single-emitter parametrization: the death and hit emitters created in
// createDeathParticleEmitter / createHitParticleEmitter are shared dormant
// OneShot emitters whose properties get rewritten in place each spawn call.
// The per-element tables below specify what to overwrite for each
// DamageType — colour, velocity range, lifetime range, sphere radius,
// burst count.
//
// Why a tunings table instead of one emitter per element:
//   A pool of 5 death + 5 hit = 10 dedicated emitters would push 10 entries
//   into ParticleSystem's emitter map and require 10 createXxxEmitter()
//   functions each ~50 lines — most of them duplicating the existing
//   shape/mode/fade/scale envelope. The dispatcher table keeps the
//   "one shared dormant emitter, mutated per call" pattern from the
//   prior iteration (which got battle-tested with the death-burst,
//   hit-burst, and ribbon-trail emitters) and adds ~8 lines per element
//   instead of 50. Cost: one struct copy + one updateEmitter() call per
//   spawn — same overhead the existing position-only mutation already
//   pays. Single map probe per spawn either way.
//
// Velocity profiles (per-element colour reasoning):
//   - Physical: existing orange-red death / warm-white hit. Default.
//     Reads as "kinetic impact" — generic claw/sword/arrow. Hue separates
//     it from every elemental kill.
//   - Fire: orange-yellow base, biased upward (sparks rise). Lifetime
//     bumped 30% so the afterglow lingers. Reads as "burning ember
//     scatter".
//   - Ice: pale-cyan base, slow downward drift (frost-shatter falls).
//     Burst count slightly higher (denser shatter, slower individual
//     particles compensate for legibility). Reads as "frost crystals".
//   - Poison: yellow-green base, near-zero velocity (cloud lingers).
//     Lifetime bumped 80% to emphasize the persistence of poison.
//     Reads as "toxic miasma".
//   - Magic: white-purple base, fast outward radial. Tighter sphere
//     radius for a sharper "spell-impact" pop. Reads as "arcane burst".
//   - True: pure white-yellow, mostly the same as Physical hit (because
//     True damage is the rare "bypasses everything" category — the
//     visual identity is "absolute impact" not a distinct element).
//
// Each element gets BOTH a death and a hit tuning so the visual delta is
// consistent across the kill cadence: the per-element burst that fires on
// every non-killing tick (~6-12 Hz) reinforces the same colour identity
// the eventual death burst (~<2 Hz) will pay off.
namespace {

struct ParticleProfile {
    Engine::vec4 colorBase;         // RGBA colour (a=1 unless we want pre-multiplied alpha)
    Engine::vec4 colorVariation;    // Random ±range applied per-particle
    Engine::vec3 velocityMin;       // Lower bound of per-particle initial velocity
    Engine::vec3 velocityMax;       // Upper bound
    float lifetimeMin;
    float lifetimeMax;
    float sphereRadius;             // Sphere-shell emission radius
    uint32_t burstCount;            // Particles per triggerBurst
};

// Index = static_cast<int>(DamageType). Ordered to match status_effects.hpp:
// Physical=0, Fire=1, Ice=2, Poison=3, Magic=4, True=5.
//
// Tuning rationale for each entry is in the file-level comment block above.
//
// DEATH profiles. Burst count 50 = the existing default; per-element values
// scale lifetime / colour / velocity but keep the burst count similar so the
// "this enemy died" beat reads with similar visual weight regardless of
// element. (Boss-kill scaling is a separate Next that will multiply burst
// count by enemy tier, not damage type.)
//
// const (not constexpr) because Engine::vec3 / Engine::vec4 ship with
// non-constexpr constructors (Vector.hpp lines 238-242 — they wrap an SSE
// __m128 and the SIMD intrinsic constructor isn't a constant expression
// in MSVC). The arrays are still file-scope statically-initialised at
// program startup, which is identical perf to constexpr — the only thing
// we lose is compile-time evaluation that we don't need anyway.
const ParticleProfile kDeathProfiles[6] = {
    // Physical (0) — orange-red, default
    {
        Engine::vec4(1.00F, 0.30F, 0.10F, 1.0F),
        Engine::vec4(0.20F, 0.10F, 0.05F, 0.0F),
        Engine::vec3(-3.00F, 1.00F, -3.00F),
        Engine::vec3( 3.00F, 5.00F,  3.00F),
        0.50F, 1.50F,
        0.50F,
        50U
    },
    // Fire (1) — orange-yellow with strong upward bias, lingering afterglow
    {
        Engine::vec4(1.00F, 0.65F, 0.10F, 1.0F),
        Engine::vec4(0.10F, 0.20F, 0.05F, 0.0F),
        Engine::vec3(-2.50F, 2.50F, -2.50F),    // upward floor (sparks rise)
        Engine::vec3( 2.50F, 6.50F,  2.50F),    // taller upward ceiling
        0.80F, 2.00F,                            // lifetime 30-33% longer
        0.50F,
        50U
    },
    // Ice (2) — pale-cyan, slow downward drift, dense shatter
    {
        Engine::vec4(0.55F, 0.85F, 1.00F, 1.0F),
        Engine::vec4(0.15F, 0.10F, 0.05F, 0.0F),
        Engine::vec3(-2.00F, -1.50F, -2.00F),   // downward floor (frost falls)
        Engine::vec3( 2.00F,  1.50F,  2.00F),   // limited upward
        0.60F, 1.20F,
        0.55F,                                   // slightly wider scatter
        60U                                      // denser to compensate slower velocity
    },
    // Poison (3) — yellow-green, lingering toxic cloud
    {
        Engine::vec4(0.65F, 0.95F, 0.20F, 1.0F),
        Engine::vec4(0.10F, 0.05F, 0.05F, 0.0F),
        Engine::vec3(-1.20F, 0.30F, -1.20F),    // very slow drift
        Engine::vec3( 1.20F, 1.50F,  1.20F),
        1.20F, 2.50F,                            // 60-80% longer lifetime (cloud lingers)
        0.50F,
        45U
    },
    // Magic (4) — white-purple radial burst
    {
        Engine::vec4(0.85F, 0.65F, 1.00F, 1.0F),
        Engine::vec4(0.10F, 0.05F, 0.10F, 0.0F),
        Engine::vec3(-3.50F, 1.50F, -3.50F),    // wider horizontal spread
        Engine::vec3( 3.50F, 5.50F,  3.50F),
        0.50F, 1.30F,
        0.45F,                                   // tighter sphere — spell-impact pop
        55U
    },
    // True (5) — pure white-yellow, distinctive "armour-bypassing" identity
    {
        Engine::vec4(1.00F, 0.95F, 0.50F, 1.0F),
        Engine::vec4(0.05F, 0.05F, 0.10F, 0.0F),
        Engine::vec3(-3.00F, 1.00F, -3.00F),
        Engine::vec3( 3.00F, 5.00F,  3.00F),
        0.50F, 1.50F,
        0.50F,
        50U
    }
};

// HIT profiles. Burst count 8 = the existing default; the per-element values
// keep the count low (so peak combat doesn't drown in particles) and adjust
// only the visual identity. Same dispatcher pattern as deaths. const (not
// constexpr) for the same vec3/vec4-constructor reason as kDeathProfiles.
const ParticleProfile kHitProfiles[6] = {
    // Physical (0) — warm white-yellow, default kinetic impact
    {
        Engine::vec4(1.00F, 0.95F, 0.70F, 1.0F),
        Engine::vec4(0.05F, 0.05F, 0.10F, 0.0F),
        Engine::vec3(-1.50F, 0.50F, -1.50F),
        Engine::vec3( 1.50F, 2.50F,  1.50F),
        0.30F, 0.55F,
        0.25F,
        8U
    },
    // Fire (1) — orange-yellow with sparks-rising velocity profile
    {
        Engine::vec4(1.00F, 0.55F, 0.10F, 1.0F),
        Engine::vec4(0.10F, 0.15F, 0.05F, 0.0F),
        Engine::vec3(-1.20F, 1.50F, -1.20F),    // upward floor
        Engine::vec3( 1.20F, 3.50F,  1.20F),
        0.40F, 0.70F,                            // longer afterglow than physical hit
        0.25F,
        10U
    },
    // Ice (2) — pale-cyan with downward drift
    {
        Engine::vec4(0.60F, 0.85F, 1.00F, 1.0F),
        Engine::vec4(0.10F, 0.05F, 0.05F, 0.0F),
        Engine::vec3(-1.20F, -0.80F, -1.20F),   // downward floor
        Engine::vec3( 1.20F,  0.80F,  1.20F),
        0.35F, 0.65F,
        0.28F,
        10U
    },
    // Poison (3) — yellow-green lingering cloud (small, but persistent)
    {
        Engine::vec4(0.65F, 0.95F, 0.20F, 1.0F),
        Engine::vec4(0.10F, 0.05F, 0.05F, 0.0F),
        Engine::vec3(-0.80F, 0.20F, -0.80F),    // very slow drift
        Engine::vec3( 0.80F, 1.00F,  0.80F),
        0.55F, 1.10F,                            // ~2x default lifetime
        0.25F,
        9U
    },
    // Magic (4) — white-purple sharp radial pop
    {
        Engine::vec4(0.85F, 0.65F, 1.00F, 1.0F),
        Engine::vec4(0.10F, 0.05F, 0.10F, 0.0F),
        Engine::vec3(-1.80F, 0.80F, -1.80F),    // wider radial
        Engine::vec3( 1.80F, 2.80F,  1.80F),
        0.30F, 0.50F,
        0.22F,
        9U
    },
    // True (5) — pure white-yellow, slightly brighter than Physical
    {
        Engine::vec4(1.00F, 0.95F, 0.50F, 1.0F),
        Engine::vec4(0.05F, 0.05F, 0.10F, 0.0F),
        Engine::vec3(-1.50F, 0.50F, -1.50F),
        Engine::vec3( 1.50F, 2.50F,  1.50F),
        0.30F, 0.55F,
        0.25F,
        8U
    }
};

// ─── Per-element kill-shake profiles ──────────────────────────────────────
// EntityDeathEvent.damageType ALREADY carries the killing-blow's element
// (populated in CatAnnihilation::setOnEntityDeath from
// HealthComponent.lastDamageType, which CombatSystem stamps before every
// damage call). The death-burst dispatcher reads it to pick a particle
// profile; the camera-shake trigger should too, so a Fire kill feels like
// an explosion (harder shake) and a Frost kill feels like a shatter
// (softer-but-longer envelope) instead of every kill collapsing to the
// same 0.12 m / 0.18 s "physical thump" regardless of how the enemy died.
//
// Tuning band stays inside the perceptual envelope established by the
// camera-shake landing iteration (CatAnnihilation::triggerCameraShake
// hard-clamps amplitude to 0.25 m and duration to 0.08–0.60 s):
//   - amplitude — never above 0.20 m (above which the framing-anchor work
//     becomes visible as a wobble) and never below 0.06 m (sub-pixel
//     aliasing at 1080p makes the shake imperceptible).
//   - duration  — never below 0.10 s (single-frame pop reads as glitch,
//     not feedback) and never above 0.40 s (outlives the death-burst's
//     1.5 s ceiling visually but starts to bleed into subsequent kills
//     during dense combat).
//
// Per-element character mapping (rows indexed by static_cast<int>(
// DamageType) — same indexing convention as kHitProfiles / kDeathProfiles
// so a future maintainer reading three sibling tables sees one consistent
// dispatch contract):
//   - Physical (0): 0.12 m / 0.18 s — baseline, byte-exact identical to
//     the prior fixed-tuning landing so existing playtest behaviour is
//     preserved when no elemental damage flows through the kill path.
//   - Fire     (1): 0.16 m / 0.20 s — "explosion" feel. Highest amplitude
//     in the table to match the orange-yellow upward-spark death-burst's
//     pressure-wave-peak read; marginally longer envelope so the shake
//     trails the burst's rising sparks rather than cutting under them.
//   - Ice      (2): 0.08 m / 0.30 s — "shatter" feel. Low amplitude but
//     longer envelope so the shake reads as crystals settling rather
//     than a kinetic thump; pairs with the pale-cyan downward-drift of
//     kDeathProfiles[Ice] to suggest "freeze, crack, fall".
//   - Poison   (3): 0.06 m / 0.35 s — "wilting" feel. Smallest amplitude
//     (poison is a non-impact death — the entity wilts/dissolves rather
//     than explodes) but longest envelope so the camera tracks the slow-
//     fade of the yellow-green miasma cloud instead of snapping back to
//     stillness mid-fade.
//   - Magic    (4): 0.14 m / 0.10 s — "arcane snap" feel. Shortest
//     envelope in the table — the shake is a sharp punctuation mark
//     matching the fast outward radial of kDeathProfiles[Magic];
//     amplitude slightly above Physical so the kill-moment punctuation
//     reads even at the brief envelope.
//   - True     (5): 0.15 m / 0.15 s — "armour-bypass thud" feel. Slightly
//     larger amplitude than Physical (true damage ignored armour, the
//     kill should feel weightier) at identical duration; a small upgrade
//     over Physical that signals "this enemy was finished by something
//     that bypassed defences" without changing the rhythm of the kill.
//
// Why we pick a struct rather than two parallel float arrays: the trigger
// site reads BOTH amplitude and duration together — keeping them packed
// in one row prevents a future caller from drifting the indices apart
// (e.g. amplitude[Fire] but duration[Ice]). One row, one DamageType,
// one tuning unit.
struct KillShakeProfile {
    float amplitudeMeters;   // peak offset before envelope decay
    float durationSeconds;   // total envelope length (also the decay denominator)
};

const KillShakeProfile kKillShakeProfiles[6] = {
    {0.12F, 0.18F},  // Physical — baseline thump (preserves prior tuning)
    {0.16F, 0.20F},  // Fire     — explosion peak
    {0.08F, 0.30F},  // Ice      — shatter settle
    {0.06F, 0.35F},  // Poison   — wilting drift
    {0.14F, 0.10F},  // Magic    — arcane snap
    {0.15F, 0.15F},  // True     — armour-bypass thud
};

// Same fallback contract as selectDeathProfile / selectHitProfile: a
// future DamageType addition that wasn't reflected here silently maps
// to Physical instead of reading past the end of the array. A clamp on
// the index rather than on the result is intentional — the result of
// reading past the end is undefined behaviour, the clamp is the safe
// pre-condition.
inline const KillShakeProfile& selectKillShakeProfile(CatGame::DamageType type) {
    const int idx = static_cast<int>(type);
    if (idx < 0 || idx >= 6) {
        return kKillShakeProfiles[0];
    }
    return kKillShakeProfiles[idx];
}

// Per-enemy-type death-burst AND kill-shake scaling.
//
// The per-element table (kHitProfiles / kDeathProfiles / kKillShakeProfiles)
// answers "what KIND of kill was this?". This function answers a complementary
// question: "how BIG was the enemy that died?". A regular Dog kill should feel
// like a single kinetic beat; a BossDog kill should feel like a finishing
// move. The multiplier scales BOTH the death-burst particle count AND the
// kill-shake amplitude so the size delta hits the eye AND the camera at the
// same moment, giving each tier of enemy its own kinetic signature.
//
// Tuning rationale:
//   - Dog (1.0x)     — baseline. 50-particle burst + per-element shake.
//                      The bread-and-butter wave-1 enemy; the kill cadence is
//                      "frequent and quick" so it must NOT feel oppressive.
//   - FastDog (1.0x) — same kinetic weight as Dog. FastDog is differentiated
//                      by mobility (1.5x speed, 0.75x damage), not by death
//                      weight; scaling its burst would mis-cue the player
//                      that it was tougher than it was.
//   - BigDog (1.5x)  — 75-particle burst + 1.5x shake. Visibly heavier than
//                      a Dog kill but well inside the per-frame particle
//                      budget. Reads as "that took more to put down".
//   - BossDog (2.5x) — 125-particle burst + 2.5x shake. The shake multiplier
//                      blows past triggerCameraShake's 0.25 m hard ceiling
//                      for any element — that's intentional. BossDog kills
//                      saturate the framing-anchor ceiling regardless of
//                      element so they all read as "weighty finisher" while
//                      still preserving per-element envelope-duration character
//                      (Fire BossDog still has the longest envelope etc.).
//                      Burst count 125 is well under the 1M particle pool.
//
// Pairs naturally with the per-element tables: a BossDog Fire kill produces
// 125 orange-yellow upward sparks AND a 0.25 m / 0.20 s explosion-clamp shake;
// a regular Dog Physical kill produces the unchanged 50 orange-red sphere
// burst AND the unchanged 0.12 m / 0.18 s baseline thump. Four-quadrant
// tactile delta (element × enemy-tier) from one playtest.
//
// Why not switch on AIState too: AIState::Dead transitions on the same frame
// the kill fires, so it adds zero diagnostic value over EnemyType. EnemyType
// is the stable identity for tier-scaling.
inline float enemyTypeMultiplier(CatGame::EnemyType type) {
    switch (type) {
        case CatGame::EnemyType::Dog:     return 1.0F;
        case CatGame::EnemyType::FastDog: return 1.0F;
        case CatGame::EnemyType::BigDog:  return 1.5F;
        case CatGame::EnemyType::BossDog: return 2.5F;
    }
    // Defensive fallback for a future EnemyType addition that wasn't reflected
    // here. Returning 1.0 falls back to baseline Dog tuning, which is always a
    // valid burst — same fallback contract as selectDeathProfile / selectHitProfile.
    return 1.0F;
}

// Human-readable name for log lines. Mirrors damageTypeName() so the per-tier
// canary log lines have the same naming convention as the per-element ones.
inline const char* enemyTypeName(CatGame::EnemyType type) {
    switch (type) {
        case CatGame::EnemyType::Dog:     return "Dog";
        case CatGame::EnemyType::FastDog: return "FastDog";
        case CatGame::EnemyType::BigDog:  return "BigDog";
        case CatGame::EnemyType::BossDog: return "BossDog";
    }
    return "UnknownEnemy";
}

// Helper: clamp a DamageType to a valid index for the profile tables.
// Defends against a future enum addition that wasn't reflected in the
// tables — a missing entry would silently read past the end and produce
// undefined behaviour; the clamp falls back to Physical (index 0) which
// still produces a valid burst.
inline const ParticleProfile& selectDeathProfile(CatGame::DamageType type) {
    const int idx = static_cast<int>(type);
    if (idx < 0 || idx >= 6) {
        return kDeathProfiles[0];
    }
    return kDeathProfiles[idx];
}

inline const ParticleProfile& selectHitProfile(CatGame::DamageType type) {
    const int idx = static_cast<int>(type);
    if (idx < 0 || idx >= 6) {
        return kHitProfiles[0];
    }
    return kHitProfiles[idx];
}

// Human-readable name for log lines. Single source of truth so the canary
// log lines and any future debug overlay agree on naming.
inline const char* damageTypeName(CatGame::DamageType type) {
    switch (type) {
        case CatGame::DamageType::Physical: return "Physical";
        case CatGame::DamageType::Fire:     return "Fire";
        case CatGame::DamageType::Ice:      return "Ice";
        case CatGame::DamageType::Poison:   return "Poison";
        case CatGame::DamageType::Magic:    return "Magic";
        case CatGame::DamageType::True:     return "True";
    }
    return "Unknown";
}

} // namespace

void CatAnnihilation::spawnDeathParticles(const Engine::vec3& position,
                                          DamageType damageType,
                                          float burstMultiplier) {
    if (particleSystem_ == nullptr) {
        return;
    }

    // Per-element profile selection. The tables above map each DamageType to
    // a colour / velocity / lifetime / radius / count tuning. The fallback
    // in selectDeathProfile guarantees we always get a valid struct even if
    // a future DamageType is added without being added to the table.
    const ParticleProfile& profile = selectDeathProfile(damageType);

    // Per-enemy-type burst scaling. The multiplier comes from
    // enemyTypeMultiplier(EnemyComponent.type) at the caller (onEntityDeath
    // probes the dying entity's EnemyComponent before calling this). Defaults
    // to 1.0 so non-enemy callers (player death, scripted kills, future test
    // hooks that don't have an EnemyComponent) get the unchanged baseline
    // burst count.
    //
    // Floor at 1 so a future tuning misstep (multiplier of 0 / negative)
    // can't silently produce a no-op burst that swallows the kill cue.
    // Round-half-up via +0.5F so a 1.5x on 50 lands at 75 not 74. Cast to
    // uint32_t after the floor so the assignment to emitter->burstCount
    // (also uint32_t) is type-safe.
    //
    // Burst count headroom: even the largest combination (BossDog 2.5x on the
    // 60-particle Ice profile = 150) is six orders of magnitude under the
    // 1M particle pool ceiling (ParticleSystem.hpp:38), so no clamp is
    // needed against the GPU buffer size.
    const float scaledFloat =
        static_cast<float>(profile.burstCount) * burstMultiplier;
    const uint32_t scaledBurstCount =
        static_cast<uint32_t>(scaledFloat < 1.0F ? 1.0F : scaledFloat + 0.5F);

    // Get the death emitter, move it to the kill site, OVERWRITE its tunings
    // with the per-element profile, re-enable it for a single burst, and
    // trigger.
    //
    // Why we have to re-enable each call:
    //   createDeathParticleEmitter() configures the emitter with
    //   `enabled = false` and `mode = OneShot` so it sits dormant in the
    //   ParticleSystem's emitter map across every frame *except* the
    //   ones immediately following a death. ParticleSystem::processEmitters
    //   gates each emitter on `if (!emitter.enabled) continue;`
    //   (ParticleSystem.cu:481), so a `triggerBurst` against a disabled
    //   emitter sets `burstTriggered=true` and then is silently skipped
    //   on the next tick. After a OneShot burst processes, the same loop
    //   sets `enabled=false` again (ParticleSystem.cu:490-492) — that's
    //   the OneShot semantic — so we're cleared to re-enable on the
    //   next death without the burst chaining into a continuous stream.
    //   In short: enable → burst → engine auto-disables → next call
    //   re-enables. Single shared emitter, fire-and-forget per death.
    //
    // Why we mutate the local copy AND call updateEmitter (instead of
    // mutating the live emitter pointer in place):
    //   updateEmitter() copies the local back into the map under the
    //   matching id, preserving the existing pattern this function
    //   already used for position. Keeping all writes in the same
    //   "edit-then-publish" flow means a future reader sees one
    //   consistent contract instead of "position via copy, enable via
    //   raw pointer mutation." Cost is one trivial struct copy on a
    //   path that fires at most a handful of times per second during
    //   wave clears.
    //
    // Why per-element values are written EVERY call (not only when the type
    // changed):
    //   The check would be one extra branch per call to save ~6 scalar
    //   writes; not worth the complexity at <2 Hz call frequency. Always
    //   writing also defends against a future code path that mutates the
    //   emitter directly between spawn calls — the dispatcher always brings
    //   the emitter back to a known-good per-element state.
    CatEngine::CUDA::ParticleEmitter* emitter = particleSystem_->getEmitter(deathEmitterId_);
    if (emitter != nullptr) {
        emitter->position = position;
        emitter->enabled = true;
        emitter->burstCount = scaledBurstCount;
        emitter->shapeParams.sphereRadius = profile.sphereRadius;
        emitter->initialProperties.colorBase = profile.colorBase;
        emitter->initialProperties.colorVariation = profile.colorVariation;
        emitter->initialProperties.velocityMin = profile.velocityMin;
        emitter->initialProperties.velocityMax = profile.velocityMax;
        emitter->initialProperties.lifetimeMin = profile.lifetimeMin;
        emitter->initialProperties.lifetimeMax = profile.lifetimeMax;
        particleSystem_->updateEmitter(deathEmitterId_, *emitter);
        particleSystem_->triggerBurst(deathEmitterId_);

        // One-time confirmation log per session — same regression-canary
        // pattern the attack-lunge / hit-flinch / death-pose iterations
        // established. Future portfolio reviewers can grep the playtest
        // log for the line; if it never fires after a kill is logged,
        // the death-burst wire is broken (likely either the
        // EntityDeathEvent publish in setOnEntityDeath was reverted or
        // particleSystem_ is null in this configuration).
        //
        // We log the FIRST burst overall and the FIRST burst per element so
        // a portfolio reviewer can verify the dispatcher actually picked
        // distinct profiles — without the per-element log, an Ice kill
        // followed by a Fire kill would both produce only the global
        // "first death-burst triggered" line and the per-element delta
        // would be invisible in the log. The multiplier is also surfaced
        // in the log so a per-tier scaling regression (multiplier always
        // 1.0 → all bosses look like regular dogs) is visible without
        // re-instrumenting.
        static bool firstDeathBurstLogged = false;
        static bool firstPerElementLogged[6] = {false, false, false, false, false, false};
        if (!firstDeathBurstLogged) {
            firstDeathBurstLogged = true;
            Engine::Logger::info(
                "[ParticleSystem] first death-burst triggered (count="
                + std::to_string(emitter->burstCount)
                + ", element=" + damageTypeName(damageType)
                + ", multiplier=" + std::to_string(burstMultiplier)
                + ", pos=" + std::to_string(position.x)
                + "," + std::to_string(position.y)
                + "," + std::to_string(position.z) + ")");
        }
        const int idx = static_cast<int>(damageType);
        if (idx >= 0 && idx < 6 && !firstPerElementLogged[idx]) {
            firstPerElementLogged[idx] = true;
            Engine::Logger::info(
                std::string("[ParticleSystem] first ") + damageTypeName(damageType)
                + " death-burst triggered (count=" + std::to_string(emitter->burstCount)
                + ", radius=" + std::to_string(profile.sphereRadius)
                + ", lifetime=" + std::to_string(profile.lifetimeMin)
                + "-" + std::to_string(profile.lifetimeMax) + " s"
                + ", multiplier=" + std::to_string(burstMultiplier) + ")");
        }
    }
}

void CatAnnihilation::spawnHitParticles(const Engine::vec3& position,
                                        DamageType damageType) {
    if (particleSystem_ == nullptr) {
        return;
    }

    // Per-element profile — see kHitProfiles in the anonymous namespace above
    // for the per-DamageType colour / velocity / lifetime / radius / count
    // table and the rationale for each tuning. The profile is small enough
    // to copy by value cheaply, but we use a const-ref to avoid an unnecessary
    // struct copy on a path that fires at 6-12 Hz peak combat.
    const ParticleProfile& profile = selectHitProfile(damageType);

    // The mechanics here are deliberately a near-mirror of spawnDeathParticles
    // (move emitter to the hit site, OVERWRITE per-element tunings, re-enable,
    // trigger, log-once) because the engine-side semantics are identical: a
    // single shared dormant OneShot emitter is the cheapest and clearest way
    // to represent a fire-and-forget burst at a moving world position. See
    // spawnDeathParticles' comment block for the long-form rationale on why
    // we re-enable each call (OneShot self-disables after firing,
    // ParticleSystem.cu:481/490) and why we mutate a local copy +
    // updateEmitter() rather than poking the emitter pointer in place
    // (consistency with the rest of the file's emitter-edit flow).
    //
    // Cadence note: where deathEmitterId_ fires at <2 Hz during wave clears,
    // hitEmitterId_ can fire at 6-12 Hz during sustained combat (player
    // multi-hit combo + 2-3 enemies in melee range, each with their own
    // attack cadence applying damage to the player). The per-call cost is
    // a handful of scalar writes + one updateEmitter + one triggerBurst —
    // negligible relative to the GPU-side cost (8-10 particles per burst,
    // ~0.4 s lifetime; steady-state ~30-50 alive particles during peak
    // combat = ~0.05% of the 100k pool). Negligible.
    CatEngine::CUDA::ParticleEmitter* emitter = particleSystem_->getEmitter(hitEmitterId_);
    if (emitter != nullptr) {
        emitter->position = position;
        emitter->enabled = true;
        emitter->burstCount = profile.burstCount;
        emitter->shapeParams.sphereRadius = profile.sphereRadius;
        emitter->initialProperties.colorBase = profile.colorBase;
        emitter->initialProperties.colorVariation = profile.colorVariation;
        emitter->initialProperties.velocityMin = profile.velocityMin;
        emitter->initialProperties.velocityMax = profile.velocityMax;
        emitter->initialProperties.lifetimeMin = profile.lifetimeMin;
        emitter->initialProperties.lifetimeMax = profile.lifetimeMax;
        particleSystem_->updateEmitter(hitEmitterId_, *emitter);
        particleSystem_->triggerBurst(hitEmitterId_);

        // Regression canary — same single-fire pattern the lunge / flinch /
        // death-pose / death-burst iterations established. If a future
        // iteration regresses the wire (callback unset, emitter ID stale,
        // applyDamage path bypassed by a new attack handler), the absence
        // of this line in the playtest log after damage events fire is
        // the dispositive signal.
        //
        // Per-element first-burst lines — same reasoning as the death path:
        // without per-element logging, a Burning DOT + a Frost spell would
        // both fire bursts but the dispatcher's element selection would be
        // unobservable in the log.
        static bool firstHitBurstLogged = false;
        static bool firstPerElementLogged[6] = {false, false, false, false, false, false};
        if (!firstHitBurstLogged) {
            firstHitBurstLogged = true;
            Engine::Logger::info(
                "[ParticleSystem] first hit-burst triggered (count="
                + std::to_string(emitter->burstCount)
                + ", element=" + damageTypeName(damageType)
                + ", pos=" + std::to_string(position.x)
                + "," + std::to_string(position.y)
                + "," + std::to_string(position.z) + ")");
        }
        const int idx = static_cast<int>(damageType);
        if (idx >= 0 && idx < 6 && !firstPerElementLogged[idx]) {
            firstPerElementLogged[idx] = true;
            Engine::Logger::info(
                std::string("[ParticleSystem] first ") + damageTypeName(damageType)
                + " hit-burst triggered (count=" + std::to_string(emitter->burstCount)
                + ", radius=" + std::to_string(profile.sphereRadius)
                + ", lifetime=" + std::to_string(profile.lifetimeMin)
                + "-" + std::to_string(profile.lifetimeMax) + " s)");
        }
    }
}

void CatAnnihilation::triggerCameraShake(float amplitudeMeters, float durationSeconds) {
    // Hard ceiling on amplitude. The third-person follow-cam sits ~10–15 m
    // behind the cat at default offset; an offset of 0.5 m+ starts to feel
    // like the camera is unhinged from the player and ruins the framing
    // gain we got from the previous camera-anchor work. 0.25 m is the
    // empirical sweet spot for "the kill landed with weight" without
    // disengaging the camera. Hard-clamping rather than soft-asserting
    // means a future overly-eager caller (e.g. a boss explosion with
    // amplitude=2.0) can't ruin the framing — just gets clamped to a
    // visible-but-bounded shake.
    constexpr float kAmplitudeCeilingMeters = 0.25F;

    // Hard floor on duration. Below ~80 ms the envelope decays so fast
    // that humans perceive it as a single-frame pop rather than a shake.
    // Above ~600 ms the shake outlives the death-burst particle window
    // (~500 ms of visible particles) and reads as "the camera is broken"
    // rather than "the kill landed". 80–600 ms is the perceptual band.
    constexpr float kMinDurationSeconds = 0.08F;
    constexpr float kMaxDurationSeconds = 0.60F;

    const float requestedAmplitude =
        std::clamp(amplitudeMeters, 0.0F, kAmplitudeCeilingMeters);
    const float requestedDuration =
        std::clamp(durationSeconds, kMinDurationSeconds, kMaxDurationSeconds);

    // Re-trigger merge: we don't replace the in-flight shake, we extend it
    // along whichever dimension is bigger. The alternative — replace, so
    // the LAST trigger wins — produces an audibly-wrong "the second kill
    // dampened the first" feeling when two enemies die ~50 ms apart at
    // the end of a wave. Max-merge is the standard AAA approach: a louder
    // event escalates the in-progress shake; a quieter event extends but
    // doesn't dampen.
    cameraShakeAmplitude_ = std::max(cameraShakeAmplitude_, requestedAmplitude);
    cameraShakeRemaining_ = std::max(cameraShakeRemaining_, requestedDuration);
    // Always reset duration to the new request so the envelope normaliser
    // (remaining/duration) starts from 1.0 → 0.0 across the new envelope,
    // not at some fractional value left over from a partially-decayed
    // earlier shake. Without this, a re-trigger mid-decay would jump the
    // envelope back to (remaining/old_duration) — a sudden re-amplify that
    // reads as a glitch.
    cameraShakeDuration_ = requestedDuration;

    // Regression canary — same single-fire pattern the lunge / flinch /
    // death-pose / death-burst / hit-burst iterations established. If a
    // future iteration regresses the wire (callback unset, onEntityDeath
    // path bypassed by a new death handler), the absence of this line in
    // the playtest log after a kill is the dispositive signal.
    static bool firstShakeLogged = false;
    if (!firstShakeLogged && requestedAmplitude > 0.0F) {
        firstShakeLogged = true;
        Engine::Logger::info(
            "[Camera] first kill-shake triggered (amplitude="
            + std::to_string(requestedAmplitude)
            + " m, duration=" + std::to_string(requestedDuration) + " s)");
    }
}

Engine::vec3 CatAnnihilation::sampleCameraShakeOffset() const {
    // Inactive: zero offset. The caller already adds this to camPos blindly,
    // so an inactive sampler must produce exact zeros (not a tiny float
    // drift) to keep the framing stable when no shake is active.
    if (cameraShakeRemaining_ <= 0.0F || cameraShakeDuration_ <= 0.0F ||
        cameraShakeAmplitude_ <= 0.0F) {
        return Engine::vec3(0.0F, 0.0F, 0.0F);
    }

    // Envelope: linear decay from 1.0 at trigger time to 0.0 at envelope end.
    // We use a SQUARE of the linear factor so the shake feels punchy at the
    // start and fades quickly toward the tail — matches how kinetic impact
    // physically settles in the real world (initial high-energy ringing,
    // exponentially-damped tail). Pure linear envelope reads as a "smooth
    // wobble" rather than an "impact"; quadratic punches harder.
    const float linearFactor =
        std::clamp(cameraShakeRemaining_ / cameraShakeDuration_, 0.0F, 1.0F);
    const float envelope = linearFactor * linearFactor;
    const float amplitude = cameraShakeAmplitude_ * envelope;

    // 3D pseudo-noise from decoupled sin waves at non-commensurate frequencies.
    // The three frequencies (37, 53, 71 Hz) are chosen as primes far enough
    // apart that the (x,y,z) tuple at any given gameTime looks visually
    // uncorrelated — no axis snaps in lock-step with another. The phase
    // offsets (1.7, 4.1, 6.3 rad) further break any residual diagonal
    // correlation that pure-sin coincidence at the same time argument
    // could introduce. Result: x, y, z each trace independent ~30-Hz
    // oscillations, sampled at 60 fps gives ~2 visible cycles per envelope
    // (with 0.18 s typical duration) — enough to read as shake, not as
    // a single linear nudge.
    const float t = gameTime_;
    const float kFreqX = 37.0F;
    const float kFreqY = 53.0F;
    const float kFreqZ = 71.0F;
    const float kPhaseX = 1.7F;
    const float kPhaseY = 4.1F;
    const float kPhaseZ = 6.3F;

    // Vertical jitter is intentionally damped (0.6×) because vertical
    // camera-bob couples with the cat's pelvis-bob in the locomotion clip
    // and at full amplitude reads as a player-character physics bug rather
    // than a kill-feedback shake. Horizontal jitter at 1.0× is the dominant
    // axis; cinematographers calibrate to horizontal because the eye is
    // most sensitive to horizon tilt.
    Engine::vec3 offset(
        std::sin(t * kFreqX + kPhaseX) * amplitude,
        std::sin(t * kFreqY + kPhaseY) * amplitude * 0.6F,
        std::sin(t * kFreqZ + kPhaseZ) * amplitude
    );
    return offset;
}

// ============================================================================
// Event Handlers
// ============================================================================

void CatAnnihilation::onEnemyKilled(const EnemyKilledEvent& event) {
    // Update statistics. This is the single canonical increment site for the
    // kill counter — the HealthSystem death callback (which publishes this
    // event) intentionally does NOT bump the counter, to avoid the double-
    // increment bug that plagued the heartbeat/HUD score pre-2026-04-24.
    enemiesKilled_++;

    // One log line per kill, emitted *after* the canonical increment so the
    // printed tally matches what the HUD, heartbeat, and getEnemiesKilled()
    // observers read. This was previously emitted from the death callback
    // (before the event-handler increment), which is why the logged tally
    // was always exactly one behind the HUD score — a symptom of the same
    // double-count bug.
    Engine::Logger::info("[kill] Enemy died. Total kills: " +
                         std::to_string(enemiesKilled_));

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

    // Per-enemy-type kinetic scaling. EnemyComponent is still alive at this
    // point (HealthSystem::handleDeath fires onEntityDeath_ INSIDE the
    // updateHealth pass, and the matching ecs_->destroyEntity only fires
    // once health->deathTimer crosses health->deathAnimationDuration on a
    // LATER tick — see HealthSystem.cpp:74-82). The probe is therefore safe
    // and deterministic. Player death falls through with the default 1.0x
    // (player has no EnemyComponent), so the player path is byte-exact
    // identical to before this iteration.
    //
    // The multiplier feeds BOTH spawnDeathParticles' burst count AND
    // triggerCameraShake's amplitude so the per-tier delta hits the eye AND
    // the camera at the same moment. The shake amplitude clamp inside
    // triggerCameraShake (≤0.25 m hard ceiling) saturates BossDog-tier
    // kills regardless of element — that's intentional: BossDog shakes pin
    // at the framing-anchor ceiling so they all feel "weighty finisher"
    // while the per-element envelope DURATION (Fire 0.20 s vs Ice 0.30 s
    // etc.) preserves elemental character even at the saturation point.
    //
    // Why we read EnemyType once and reuse it for both calls: a future
    // refactor that splits onEntityDeath into per-tier sub-handlers would
    // re-probe the component twice. The single read AT THIS LEVEL keeps
    // both consumers (burst + shake) in lockstep — a per-tier multiplier
    // change in enemyTypeMultiplier() touches both calls atomically.
    float perTierMultiplier = 1.0F;
    CatGame::EnemyType perTierType = CatGame::EnemyType::Dog;
    bool isEnemy = false;
    if (auto* enemyComp = ecs_.getComponent<EnemyComponent>(event.entity)) {
        isEnemy = true;
        perTierType = enemyComp->type;
        perTierMultiplier = enemyTypeMultiplier(perTierType);
    }

    // Spawn death particles at entity position. Forward the killing-blow's
    // damage type from EntityDeathEvent so the dispatcher inside
    // spawnDeathParticles picks the right per-element profile (orange-red
    // for Physical, orange-yellow for Fire, pale-cyan for Ice, yellow-green
    // for Poison, white-purple for Magic). The setOnEntityDeath publish site
    // populates event.damageType from the dying entity's
    // HealthComponent.lastDamageType (see the construct site above) so the
    // chain CombatSystem::applyDamage → HealthComponent.lastDamageType →
    // EntityDeathEvent.damageType → spawnDeathParticles is end-to-end
    // type-typed. burstMultiplier scales the burst count by enemy tier;
    // see enemyTypeMultiplier() for the per-tier rationale.
    spawnDeathParticles(deathPosition, event.damageType, perTierMultiplier);

    // Camera shake: a small punchy jitter on the kill moment to give the
    // attack-lunge → hit-flinch → death-burst sequence a tactile beat.
    //
    // We deliberately gate on `event.entity != playerEntity_` here. Player
    // death is a state transition (Playing → GameOver) that ALREADY
    // produces a strong visual cue (the GameOver overlay fades in over
    // the existing scene); shaking the camera for ~0.18 s right before
    // the overlay paints would read as a glitch ("why did the camera
    // jolt right as the screen went dark?") instead of as kill feedback.
    // Enemy kills get the shake; player death does not.
    //
    // Per-element tuning: kKillShakeProfiles in the anonymous namespace
    // above maps DamageType → (amplitudeMeters, durationSeconds) so a
    // Fire kill explodes harder than a Frost kill which shatters longer
    // than a Poison kill which wilts the longest. Physical (the default
    // for melee + unspecified callers) preserves the prior fixed-tuning
    // 0.12 m / 0.18 s exactly, so existing playtest behaviour is byte-
    // exact identical when no elemental damage flows through the kill
    // path. The tuning rationale for each row lives next to the table
    // itself; see the long block comment above kKillShakeProfiles.
    //
    // The trigger site does NOT pre-clamp — triggerCameraShake() already
    // hard-clamps amplitude (≤0.25 m) and duration (0.08–0.60 s) at the
    // single ingress point, so every row of kKillShakeProfiles passes
    // through unchanged (all rows are well inside both clamps) AND the
    // contract holds for a future caller that picks a different table or
    // hard-codes a value.
    if (event.entity != playerEntity_) {
        const KillShakeProfile& shakeProfile =
            selectKillShakeProfile(event.damageType);

        // Per-enemy-type amplitude scaling. Same multiplier the burst-count
        // path uses (Dog/FastDog 1.0x, BigDog 1.5x, BossDog 2.5x); the size
        // delta hits the eye AND the camera at the same moment. The
        // amplitude clamp inside triggerCameraShake (≤0.25 m hard ceiling)
        // saturates the BossDog tier across every element — that's
        // intentional: a BossDog kill should pin at the framing-anchor
        // ceiling regardless of element. The DURATION is NOT scaled — only
        // amplitude — so the per-element envelope length (Fire 0.20 s vs
        // Ice 0.30 s etc.) preserves elemental character even at the
        // saturation point. A bigger enemy makes the ground shake harder,
        // not necessarily longer, which matches how impact physics works
        // in the real world.
        const float scaledAmplitude =
            shakeProfile.amplitudeMeters * perTierMultiplier;
        triggerCameraShake(scaledAmplitude, shakeProfile.durationSeconds);

        // Per-element regression canary — mirrors the per-element burst
        // log lines in spawnDeathParticles / spawnHitParticles. Logs the
        // FIRST kill-shake of each element so the playtest log shows the
        // dispatcher selected distinct tunings; subsequent kills of the
        // same element are silent (we don't want to flood the log) but
        // still fire the trigger correctly.
        //
        // Why per-element AND a global "first kill-shake" line in
        // triggerCameraShake itself: the global line proves the wire is
        // intact (callback registered, onEntityDeath fired, trigger
        // reached); the per-element lines prove the dispatcher picked
        // distinct values per kill type. Without the per-element lines,
        // an Ice kill followed by a Fire kill would both produce only
        // the global "first kill-shake triggered (amplitude=...)" line
        // (with the Physical/Ice value, since global logs once on first
        // call), and the per-element delta would be invisible in the log
        // — exactly the gap the spawnDeathParticles canary closed for
        // bursts, applied here for camera-shake.
        //
        // requestedAmplitude is logged BEFORE triggerCameraShake's clamp;
        // a future regression where BossDog kills no longer pin at the
        // 0.25 m ceiling shows up as a divergence between the requested
        // value here and the rendered shake (which the per-tier line
        // logs separately).
        static bool firstPerElementShakeLogged[6] = {
            false, false, false, false, false, false};
        const int idx = static_cast<int>(event.damageType);
        if (idx >= 0 && idx < 6 && !firstPerElementShakeLogged[idx]) {
            firstPerElementShakeLogged[idx] = true;
            Engine::Logger::info(
                std::string("[Camera] first ") +
                damageTypeName(event.damageType) +
                " kill-shake triggered (requestedAmplitude=" +
                std::to_string(scaledAmplitude) +
                " m, duration=" +
                std::to_string(shakeProfile.durationSeconds) +
                " s, multiplier=" + std::to_string(perTierMultiplier) + ")");
        }

        // Per-enemy-type regression canary — sister to the per-element
        // line above. Logs the FIRST kill of each EnemyType so a portfolio
        // reviewer can verify the per-tier dispatcher actually picks
        // distinct multipliers. Without this line a "BigDog kill but
        // multiplier still 1.0" regression would be silent in the log
        // (the per-element line could fire on the same kill but only
        // shows multiplier as part of a single per-element value, not
        // as a confirmation that EnemyType was probed correctly).
        //
        // Index by the EnemyType enum (4 values: Dog, BigDog, FastDog,
        // BossDog at game/components/EnemyComponent.hpp:10-15). Same
        // out-of-range clamp pattern as the per-element canary.
        static bool firstPerTierShakeLogged[4] = {false, false, false, false};
        if (isEnemy) {
            const int tierIdx = static_cast<int>(perTierType);
            if (tierIdx >= 0 && tierIdx < 4 && !firstPerTierShakeLogged[tierIdx]) {
                firstPerTierShakeLogged[tierIdx] = true;
                Engine::Logger::info(
                    std::string("[Camera] first ") +
                    enemyTypeName(perTierType) +
                    " kill observed (multiplier=" +
                    std::to_string(perTierMultiplier) +
                    ", element=" + damageTypeName(event.damageType) +
                    ", requestedAmplitude=" +
                    std::to_string(scaledAmplitude) + " m)");
            }
        }
    }
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
