#include "DogEntity.hpp"
#include "../components/HealthComponent.hpp"
#include "../components/MovementComponent.hpp"
#include "../components/MeshComponent.hpp"
#include "../../engine/math/Transform.hpp"
#include "../../engine/math/Quaternion.hpp"
#include "../../engine/core/Logger.hpp"
#include "../../engine/assets/AssetManager.hpp"
#include "../../engine/assets/ModelLoader.hpp"
#include "../../engine/animation/Animator.hpp"
#include "../../engine/animation/Animation.hpp"
#include "../../engine/animation/Skeleton.hpp"
#include <exception>

namespace CatGame {

namespace {

// Resolve the on-disk model path for a given dog variant. Kept as a free
// function so the mapping lives next to the entity factory rather than being
// threaded through configuration.
//
// Meshy-AI produced four rigged variant meshes in `assets/models/meshy_raw_dogs/`:
//   dog_regular.glb — baseline silhouette, ~250k polys, standard proportions
//   dog_fast.glb    — leaner build, meant for the FastDog speedster
//   dog_big.glb     — stockier, larger chest — pairs with the +50% scale
//                      on BigDog for a readable "heavy" enemy
//   dog_boss.glb    — the boss variant, visually distinct colouring +
//                      accessories, used at 2.0x scale
// These are the production art assets that the game ships with; the
// earlier `assets/models/dog.gltf` hand-authored cube was a placeholder
// kept in-repo so builds wouldn't break while the rigger baked the real
// Meshy output. With Meshy assets landed, each variant gets its own mesh
// so silhouettes read correctly at a glance in combat. Scale adjustments
// (getStatsForType) remain layered on top — the big dog is BOTH a heftier
// rig AND scaled 1.5x, which keeps the size delta visible without letting
// enemies look like identical clones with size-only differentiation.
//
// If any variant file is missing, attachMeshAndAnimator catches the
// "Failed to open file" throw from ModelLoader::Load and leaves the dog
// without a mesh — AI and combat still work, the dog just renders as an
// invisible collider. That's strictly better than aborting the entire
// game on the first wave.
const char* modelPathForType(EnemyType type) {
    switch (type) {
        case EnemyType::BigDog:
            return "assets/models/meshy_raw_dogs/dog_big.glb";
        case EnemyType::FastDog:
            return "assets/models/meshy_raw_dogs/dog_fast.glb";
        case EnemyType::BossDog:
            return "assets/models/meshy_raw_dogs/dog_boss.glb";
        case EnemyType::Dog:
        default:
            return "assets/models/meshy_raw_dogs/dog_regular.glb";
    }
}

// Construct the mesh + animator for a newly-spawned dog. Matches the cat
// pipeline: load via AssetManager, derive a skeleton from the glTF node
// graph, and build an Animator that defaults to the idle clip when present.
void attachMeshAndAnimator(CatEngine::ECS* ecs, CatEngine::Entity entity, EnemyType type) {
    const char* modelPath = modelPathForType(type);
    auto& assets = CatEngine::AssetManager::GetInstance();

    // ModelLoader::Load throws std::runtime_error for missing files, unknown
    // extensions, or malformed glTF. Letting that propagate out of the dog
    // spawn path would abort the game the first time a wave ticks, which is
    // what the audit observed. Trap the exception, log it, and leave the
    // entity without a mesh — AI and combat still work against a proxy cube.
    std::shared_ptr<CatEngine::Model> model;
    try {
        model = assets.LoadModel(modelPath);
    } catch (const std::exception& ex) {
        Engine::Logger::warn(std::string("DogEntity: exception loading '") +
                             modelPath + "': " + ex.what());
        return;
    }
    if (!model) {
        Engine::Logger::warn(std::string("DogEntity: failed to load ") + modelPath);
        return;
    }

    MeshComponent mesh;
    mesh.sourcePath = modelPath;
    mesh.model = model;
    mesh.visible = true;

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

    auto animator = std::make_shared<Engine::Animator>(skeleton);
    for (const auto& clip : model->animations) {
        auto engineClip = std::make_shared<Engine::Animation>(clip.name, clip.duration);
        for (const auto& channel : clip.channels) {
            // Resolve the bone name from the glTF node graph rather than from
            // the numeric nodeIndex. The skeleton was populated above by
            // walking model->nodes in order and calling addBone(node.name,
            // ...), so bones are keyed by their glTF name ("spine", "head",
            // ...). Using the numeric index as a bone name — which the
            // earlier code did implicitly via std::to_string — produced
            // channels that resolved to no bone at all and animations that
            // silently did nothing. Keep this logic in sync with CatEntity.
            const bool nodeIndexValid =
                channel.nodeIndex >= 0 &&
                static_cast<size_t>(channel.nodeIndex) < model->nodes.size();
            const std::string& boneName = nodeIndexValid
                ? model->nodes[channel.nodeIndex].name
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
    if (animator->hasState("idle")) {
        animator->play("idle", 0.0F);
    }

    mesh.animator = animator;

    // Mirror the CatEntity info log so per-variant dog loads show up in
    // playtest logs. The nightly openclaw iteration uses this line to
    // confirm each variant GLB (dog_regular / dog_fast / dog_big /
    // dog_boss) is actually hitting the loader — without it, a silent
    // revert to placeholder geometry is indistinguishable from a clean
    // Meshy load in the log.
    Engine::Logger::info(std::string("DogEntity: loaded model '") + modelPath +
                         "' (meshes=" + std::to_string(model->meshes.size()) +
                         ", nodes=" + std::to_string(model->nodes.size()) +
                         ", clips=" + std::to_string(model->animations.size()) + ")");

    ecs->addComponent(entity, std::move(mesh));
}

} // namespace

CatEngine::Entity DogEntity::createDog(CatEngine::ECS* ecs,
                                       const Engine::vec3& position,
                                       CatEngine::Entity target) {
    return create(ecs, EnemyType::Dog, position, target);
}

CatEngine::Entity DogEntity::createBigDog(CatEngine::ECS* ecs,
                                          const Engine::vec3& position,
                                          CatEngine::Entity target) {
    return create(ecs, EnemyType::BigDog, position, target);
}

CatEngine::Entity DogEntity::createFastDog(CatEngine::ECS* ecs,
                                           const Engine::vec3& position,
                                           CatEngine::Entity target) {
    return create(ecs, EnemyType::FastDog, position, target);
}

CatEngine::Entity DogEntity::createBossDog(CatEngine::ECS* ecs,
                                           const Engine::vec3& position,
                                           CatEngine::Entity target) {
    return create(ecs, EnemyType::BossDog, position, target);
}

CatEngine::Entity DogEntity::create(CatEngine::ECS* ecs,
                                    EnemyType type,
                                    const Engine::vec3& position,
                                    CatEngine::Entity target,
                                    float healthMultiplier) {
    if (!ecs) {
        return CatEngine::NULL_ENTITY;
    }

    // Get stats for this enemy type
    EnemyStats stats = getStatsForType(type);

    // Create entity
    auto entity = ecs->createEntity();

    // Add Transform component
    Engine::Transform transform;
    transform.position = position;
    transform.rotation = Engine::Quaternion::identity();
    transform.scale = stats.scale;
    ecs->addComponent(entity, transform);

    // Add Enemy component
    EnemyComponent enemy(type, target);
    enemy.moveSpeed = stats.moveSpeed;
    enemy.attackDamage = stats.attackDamage;
    enemy.attackRange = stats.attackRange;
    enemy.aggroRange = stats.aggroRange;
    enemy.scoreValue = stats.scoreValue;
    ecs->addComponent(entity, enemy);

    // Add Health component
    float finalHealth = stats.health * healthMultiplier;
    HealthComponent health(finalHealth);
    ecs->addComponent(entity, health);

    // Add Movement component
    MovementComponent movement(stats.moveSpeed);
    ecs->addComponent(entity, movement);

    // Attach the variant-specific model and animator. A missing model file is
    // logged and skipped; the entity still functions for AI/combat purposes.
    attachMeshAndAnimator(ecs, entity, type);

    return entity;
}

DogEntity::EnemyStats DogEntity::getStatsForType(EnemyType type) {
    EnemyStats stats;

    switch (type) {
        case EnemyType::Dog:
            stats.health = 50.0F;
            stats.moveSpeed = 6.0F;
            stats.attackDamage = 10.0F;
            stats.attackRange = 2.0F;
            stats.aggroRange = 15.0F;
            stats.scoreValue = 10;
            stats.scale = Engine::vec3(1.0F, 1.0F, 1.0F);
            break;

        case EnemyType::BigDog:
            stats.health = 100.0F;         // 2x health
            stats.moveSpeed = 4.2F;        // 0.7x speed
            stats.attackDamage = 15.0F;    // 1.5x damage
            stats.attackRange = 2.5F;      // Slightly larger range
            stats.aggroRange = 15.0F;
            stats.scoreValue = 25;
            stats.scale = Engine::vec3(1.5F, 1.5F, 1.5F); // Bigger visually
            break;

        case EnemyType::FastDog:
            stats.health = 25.0F;          // 0.5x health
            stats.moveSpeed = 9.0F;        // 1.5x speed
            stats.attackDamage = 7.5F;     // 0.75x damage
            stats.attackRange = 1.8F;      // Slightly smaller range
            stats.aggroRange = 18.0F;      // Spots player from farther
            stats.scoreValue = 15;
            stats.scale = Engine::vec3(0.8F, 0.8F, 0.8F); // Smaller visually
            break;

        case EnemyType::BossDog:
            stats.health = 300.0F;         // Boss health
            stats.moveSpeed = 5.0F;        // Moderate speed
            stats.attackDamage = 25.0F;    // High damage
            stats.attackRange = 3.0F;      // Larger range
            stats.aggroRange = 25.0F;      // Large aggro range
            stats.scoreValue = 100;
            stats.scale = Engine::vec3(2.0F, 2.0F, 2.0F); // Much bigger
            break;
    }

    return stats;
}

} // namespace CatGame
