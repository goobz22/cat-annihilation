#include "CatEntity.hpp"
#include "../components/GameComponents.hpp"
#include "../components/MeshComponent.hpp"
#include "../components/LocomotionStateMachine.hpp"
#include "../../engine/math/Transform.hpp"
#include "../../engine/core/Logger.hpp"
#include "../../engine/assets/AssetManager.hpp"
#include "../../engine/assets/ModelLoader.hpp"
#include "../../engine/animation/Animator.hpp"
#include "../../engine/animation/Animation.hpp"
#include "../../engine/animation/Skeleton.hpp"
#include <exception>

namespace CatGame {

namespace {

// Default player cat model. The `cats/*.glb` directory holds two parallel
// sets of Meshy-AI assets:
//   * `cats/<name>.glb`        — the raw Meshy export. Single-mesh, single-
//                                 node "rig" (the model's root node IS the
//                                 mesh), no animation clips. Loading these
//                                 produces nodes=1 / clips=0 in the playtest
//                                 log — a confirmed regression observed
//                                 2026-04-24 (player rendered as a static
//                                 T-posed mesh that slid across terrain
//                                 with no skeletal motion).
//   * `cats/rigged/<name>.glb` — Meshy auto-rigger output, processed through
//                                 the rig-batch tooling (see
//                                 `assets/models/cats/rig_batch_v6.log`).
//                                 These carry a real skeleton hierarchy and
//                                 — when the rigger added them — animation
//                                 clips. The rig_batch tooling drops the
//                                 rigged GLBs in this subdirectory so the
//                                 raw exports stay byte-identical with
//                                 Meshy's web download for repro purposes.
//
// We bind to the rigged variant by default so the player cat actually
// has a skeleton the Animator + skinning shader can drive. Downstream code
// (`loadModel`, `configureAnimations`) is path-agnostic; the only difference
// they observe is that `model->nodes.size() > 1` and the inverse-bind
// matrices are non-identity, which is exactly what the rigged path needs.
// If a future iteration wants to test the raw mesh (e.g. to compare flat-
// shaded vs skinned output), swap to `assets/models/cats/ember_leader.glb`.
constexpr const char* kDefaultCatModelPath = "assets/models/cats/rigged/ember_leader.glb";

} // namespace

CatEngine::Entity CatEntity::create(CatEngine::ECS& ecs, const Engine::vec3& position) {
    return createCustom(
        ecs,
        position,
        DEFAULT_HEALTH,
        DEFAULT_MOVE_SPEED,
        DEFAULT_ATTACK_DAMAGE
    );
}

CatEngine::Entity CatEntity::createCustom(
    CatEngine::ECS& ecs,
    const Engine::vec3& position,
    float maxHealth,
    float moveSpeed,
    float attackDamage
) {
    // Create entity
    CatEngine::Entity entity = ecs.createEntity();

    // Add Transform component
    Engine::Transform transform;
    transform.position = position;
    transform.rotation = Engine::Quaternion::identity();
    transform.scale = Engine::vec3(1.0F, 1.0F, 1.0F);
    ecs.addComponent(entity, transform);

    // Add Health component
    HealthComponent health;
    health.maxHealth = maxHealth;
    health.currentHealth = maxHealth;
    health.invincibilityDuration = 0.5F;

    // Set up death callback - the actual death-event publish is handled in
    // the game-layer setOnEntityDeath callback (CatAnnihilation.cpp:286),
    // which fires from HealthSystem::handleDeath when currentHealth<=0 in
    // updateHealth observes the !isDead transition. The game-layer
    // callback publishes EntityDeathEvent (which spawns the death-burst
    // particle effect via spawnDeathParticles), publishes EnemyKilledEvent
    // for enemies (which awards XP, plays the kill sting, and updates
    // quest progress), and routes the player to GameOver. We deliberately
    // leave HealthComponent::onDeath empty here so the canonical
    // game-layer callback owns the death-handling chain — HealthComponent
    // calling onDeath inline before HealthSystem sees the transition would
    // bypass the !isDead guard in HealthSystem::updateHealth and prevent
    // handleDeath / handleEnemyDeath / the death-pose freeze (and now the
    // particle burst) from firing.
    health.onDeath = [entity]() {
        (void)entity;  // Entity ID available if needed for cleanup
    };

    // Set up damage callback - triggers visual/audio feedback
    health.onDamage = [](float damage) {
        // Damage feedback is handled by the DamageEvent published by CombatSystem
        // The CatAnnihilation class listens for this and triggers HUD effects + audio
        // No direct action needed here - event-driven architecture handles it
        (void)damage;  // Damage amount available if needed for scaling effects
    };

    ecs.addComponent(entity, health);

    // Add Movement component
    MovementComponent movement;
    movement.moveSpeed = moveSpeed;
    movement.maxSpeed = moveSpeed * 2.0F;
    movement.acceleration = 50.0F;
    movement.deceleration = 30.0F;
    movement.jumpForce = 15.0F;
    movement.gravityMultiplier = 2.0F;
    movement.isGrounded = true;
    movement.canMove = true;
    movement.canJump = true;

    ecs.addComponent(entity, movement);

    // Add Combat component
    CombatComponent combat;
    combat.attackDamage = attackDamage;
    combat.attackSpeed = DEFAULT_ATTACK_SPEED;
    combat.attackRange = DEFAULT_ATTACK_RANGE;
    combat.equippedWeapon = WeaponType::Sword;
    combat.damageMultiplier = 1.0F;
    combat.attackSpeedMultiplier = 1.0F;
    combat.canAttack = true;

    ecs.addComponent(entity, combat);

    // Attach the default cat model and animator. Failure is non-fatal: the
    // entity still has Transform/Health/Movement/Combat and will render as
    // invisible until a model is supplied via loadModel() directly.
    //
    // Path resolution: the default hero mesh is a Meshy-generated,
    // auto-rigged .glb (see kDefaultCatModelPath at the top of this file).
    // The older `assets/models/cat.gltf` placeholder was a hand-authored
    // stand-in with a 7-node skeleton; the Meshy mesh is the production art
    // the game ships with. If the Meshy file is missing, loadModel()
    // catches the "Failed to open file" throw and falls back to a model-
    // less entity, so the factory never aborts the whole game.
    if (loadModel(ecs, entity, kDefaultCatModelPath)) {
        configureAnimations(ecs, entity);

        // Stamp the player's clan-leader identity onto the mesh tint. The
        // player is canonically the EmberClan leader (their default model
        // is `ember_leader.glb`), so the tint mirrors the
        // NPCSystem::ComputeNpcTint(EmberClan, ClanLeader) result: warm
        // ember orange with the +12% leader brightness, clamped into [0,1].
        // We hardcode the result here rather than calling into NPCSystem
        // because (a) CatEntity has no NPCSystem dependency and dragging
        // one in for a single tint lookup would couple the player factory
        // to NPC code, and (b) the player tint is a fixed identity — if a
        // future patch makes the player switchable across clans, that's a
        // bigger refactor that would expose tint as a parameter to
        // createCustom().
        //
        // Without this the player rendered identical-white to every
        // unlit / no-baseColorFactor cat in the world (16 NPCs + the
        // player → 17 visually-identical white silhouettes); now the
        // player reads as the saturated ember-orange leader from any
        // distance.
        MeshComponent* meshAttached = ecs.getComponent<MeshComponent>(entity);
        if (meshAttached != nullptr) {
            // Pre-clamped result of (0.95, 0.45, 0.20) * 1.12, with the
            // x-channel saturating at 1.00. Matches what
            // NPCSystem::ComputeNpcTint emits for an EmberClan ClanLeader.
            meshAttached->hasTintOverride = true;
            meshAttached->tintOverride = Engine::vec3(1.00F, 0.504F, 0.224F);
        }
    }

    return entity;
}

bool CatEntity::loadModel(CatEngine::ECS& ecs, CatEngine::Entity entity, const char* modelPath) {
    if (!ecs.isAlive(entity) || modelPath == nullptr) {
        Engine::Logger::warn("CatEntity::loadModel: invalid entity or null path");
        return false;
    }

    // Route through AssetManager so repeated requests reuse the same Model
    // shared_ptr and upload GPU buffers only once.
    //
    // ModelLoader::Load throws std::runtime_error on missing file, unknown
    // extension, or malformed glTF. That exception must not escape into the
    // ECS createCustom() path — an uncaught throw from a constructor-like
    // factory aborts the whole game on first spawn. Catch here, log, and
    // return false so the caller can fall back to a model-less entity.
    auto& assets = CatEngine::AssetManager::GetInstance();
    std::shared_ptr<CatEngine::Model> model;
    try {
        model = assets.LoadModel(modelPath);
    } catch (const std::exception& ex) {
        Engine::Logger::warn(std::string("CatEntity::loadModel: exception loading '") +
                             modelPath + "': " + ex.what());
        return false;
    }
    if (!model) {
        Engine::Logger::warn(std::string("CatEntity::loadModel: failed to load ") + modelPath);
        return false;
    }

    MeshComponent mesh;
    mesh.sourcePath = modelPath;
    mesh.model = model;
    mesh.visible = true;

    // Build the skeleton from the glTF node graph so that downstream
    // animation sampling has a stable bone order matching inverseBindMatrix.
    auto skeleton = std::make_shared<Engine::Skeleton>();
    for (const auto& node : model->nodes) {
        skeleton->addBone(node.name, node.parentIndex);
    }
    if (!model->nodes.empty()) {
        std::vector<Engine::mat4> inverseBinds;
        inverseBinds.reserve(model->nodes.size());
        std::vector<Engine::Transform> bindPose;
        bindPose.reserve(model->nodes.size());
        for (const auto& node : model->nodes) {
            Engine::mat4 ibm;
            // glm::mat4 is column-major, same as Engine::mat4 — copy component-wise.
            for (int column = 0; column < 4; ++column) {
                for (int row = 0; row < 4; ++row) {
                    ibm[column][row] = node.inverseBindMatrix[column][row];
                }
            }
            inverseBinds.push_back(ibm);

            Engine::mat4 local;
            for (int column = 0; column < 4; ++column) {
                for (int row = 0; row < 4; ++row) {
                    local[column][row] = node.localTransform[column][row];
                }
            }
            bindPose.push_back(Engine::Transform::fromMatrix(local));
        }
        skeleton->setInverseBindMatrices(inverseBinds);
        skeleton->setBindPose(bindPose);
    }
    mesh.skeleton = skeleton;

    ecs.addComponent(entity, std::move(mesh));

    Engine::Logger::info(std::string("CatEntity: loaded model '") + modelPath +
                         "' (meshes=" + std::to_string(model->meshes.size()) +
                         ", nodes=" + std::to_string(model->nodes.size()) +
                         ", clips=" + std::to_string(model->animations.size()) + ")");
    return true;
}

bool CatEntity::configureAnimations(CatEngine::ECS& ecs, CatEngine::Entity entity) {
    MeshComponent* mesh = ecs.getComponent<MeshComponent>(entity);
    if (mesh == nullptr || !mesh->skeleton) {
        Engine::Logger::warn("CatEntity::configureAnimations: mesh or skeleton missing");
        return false;
    }

    auto animator = std::make_shared<Engine::Animator>(mesh->skeleton);

    // Translate each glTF animation clip into an Engine::Animation and register
    // it as a named state. An entity with zero clips still gets a valid animator
    // so gameplay code can call play() safely and get the bind pose back.
    for (const auto& clip : mesh->model->animations) {
        auto engineClip = std::make_shared<Engine::Animation>(clip.name, clip.duration);

        for (const auto& channel : clip.channels) {
            // Resolve the bone name from the glTF node graph. The skeleton was
            // populated in loadModel() by iterating model->nodes in order and
            // calling addBone(node.name, ...). Using `std::to_string(nodeIndex)`
            // here (as the pre-fix code did) compared the numeric index "7"
            // against the actual bone name "spine" and always missed, so no
            // animation channel was ever bound to a bone — the cat stood in
            // bind pose regardless of what clip was playing. Look the name up
            // directly via the node index.
            const bool nodeIndexValid =
                channel.nodeIndex >= 0 &&
                static_cast<size_t>(channel.nodeIndex) < mesh->model->nodes.size();
            const std::string& boneName = nodeIndexValid
                ? mesh->model->nodes[channel.nodeIndex].name
                : std::string();
            Engine::AnimationChannel outChannel(channel.nodeIndex, boneName);

            if (channel.path == "translation") {
                outChannel.positionKeyframes.reserve(channel.times.size());
                for (size_t i = 0; i < channel.times.size() && i < channel.translations.size(); ++i) {
                    const auto& value = channel.translations[i];
                    outChannel.positionKeyframes.emplace_back(
                        channel.times[i], Engine::vec3(value.x, value.y, value.z));
                }
            } else if (channel.path == "rotation") {
                outChannel.rotationKeyframes.reserve(channel.times.size());
                for (size_t i = 0; i < channel.times.size() && i < channel.rotations.size(); ++i) {
                    const auto& value = channel.rotations[i];
                    outChannel.rotationKeyframes.emplace_back(
                        channel.times[i], Engine::Quaternion(value.x, value.y, value.z, value.w));
                }
            } else if (channel.path == "scale") {
                outChannel.scaleKeyframes.reserve(channel.times.size());
                for (size_t i = 0; i < channel.times.size() && i < channel.scales.size(); ++i) {
                    const auto& value = channel.scales[i];
                    outChannel.scaleKeyframes.emplace_back(
                        channel.times[i], Engine::vec3(value.x, value.y, value.z));
                }
            }

            if (!outChannel.isEmpty()) {
                engineClip->addChannel(outChannel);
            }
        }

        const bool loops = (clip.name == "idle" || clip.name == "walk" || clip.name == "run");
        animator->addState(Engine::AnimationState(clip.name, engineClip, 1.0F, loops));
    }

    // Default to idle when present; otherwise leave animator paused at bind pose.
    if (animator->hasState("idle")) {
        animator->play("idle", 0.0F);
    }

    // Wire the locomotion state machine so the per-frame "speed" parameter
    // (fed from MovementComponent::getCurrentSpeed() in CatAnnihilation::update)
    // drives idle <-> walk <-> run transitions automatically. Without this,
    // every cat sat in idle even while sliding across the terrain at full
    // speed — the rig would T-bob in place and the visible velocity was a
    // disconnected translation of the bind pose. The helper conditionally
    // adds only the transitions whose endpoint clips actually exist on the
    // rig, so a stripped-down NPC with only "idle" stays single-clip safe.
    wireLocomotionTransitions(*animator);

    mesh->animator = animator;
    return true;
}

} // namespace CatGame
