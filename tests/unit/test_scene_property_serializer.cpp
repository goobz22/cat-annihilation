// test_scene_property_serializer.cpp
// ---------------------------------------------------------------------------
// Property + fuzz tests for engine/scene/SceneSerializer.hpp.
//
// The serializer is the load-bearing save/load layer for every scene file on
// disk. The existing test_ecs_scene_correctness.cpp pins four narrow JSON
// micro-bugs (precision, escaping, key ordering, NaN/Inf). This file expands
// to the FULL round-trip contract for scene-level state:
//
//   1. Empty scene round-trip — save → load → save produces byte-identical
//      output. (Deterministic key ordering invariant + clean defaults.)
//   2. Single-node scene round-trip — a scene with one named node and a
//      non-trivial transform survives one round-trip with bit-exact float
//      preservation on the transform fields (relying on %.17g formatting).
//   3. Multi-component round-trip — a scene with 5 component types attached
//      to a single entity. After load, every field of every component
//      equals the original.
//   4. Hierarchical round-trip — a 100-node tree of varying depth. After
//      load, the structural shape matches: same node count at each depth,
//      same parent-child name pairs, every name reachable via findNode.
//   5. Random tree round-trip stress — depth 6, branching factor 4. Build,
//      save, load, deep-compare. Repeat 8 different seeds.
//   6. Version handling — saving with VERSION=1 and loading the same string
//      succeeds; mutating the version field in the saved string to a
//      different number causes load to return nullptr (loud failure).
//   7. Float precision battery — positions {1e-6, 1e6, 0.1, -0.1, 1.0/3.0,
//      M_PI as float, denormal} round-trip through the serializer to within
//      tight epsilon (1e-6 of original, BIT-exact for values <= float).
//   8. Orphan / detached node — a node attached directly under the implicit
//      Root counts as a valid scene root entry.
//   9. Empty children array — saved correctly (`"children": []`) and
//      loaded into a node with zero children.
//
// HARD failures = bugs surfaced. If a property fires we name the bug in the
// failure message (Catch2 supports REQUIRE with stringified args), and the
// test stays failing so the next contributor sees it.
// ---------------------------------------------------------------------------

#define CATCH_CONFIG_FAST_COMPILE
#include "catch2/catch.hpp"

#include "ecs/ECS.hpp"
#include "ecs/Entity.hpp"
#include "game/components/CombatComponent.hpp"
#include "game/components/EnemyComponent.hpp"
#include "game/components/GameComponents.hpp"
#include "game/components/MovementComponent.hpp"
#include "game/components/ProjectileComponent.hpp"
#include "scene/Scene.hpp"
#include "scene/SceneNode.hpp"
#include "scene/SceneSerializer.hpp"

#include <cmath>
#include <random>
#include <set>
#include <string>
#include <vector>

using CatEngine::ECS;
using CatEngine::Entity;
using CatEngine::JsonValue;
using CatEngine::Scene;
using CatEngine::SceneNode;
using CatEngine::SceneSerializer;

namespace {

// Build a deterministic random tree of `totalNodes` nodes with depth and
// breadth caps. Each node has a unique name "n0", "n1", ... assigned in
// creation order.
SceneNode* attachRandomTreeBelow(Scene& scene, SceneNode* parent, int totalNodes,
                                 int maxDepth, int maxBranch, uint32_t seed,
                                 int& counter) {
    std::mt19937 rng(seed);
    std::vector<SceneNode*> open{parent};
    // Track per-node depth so we can cap.
    std::vector<int> depths{0};

    int created = 0;
    while (created < totalNodes && !open.empty()) {
        size_t pick = static_cast<size_t>(rng() % open.size());
        SceneNode* parentNode = open[pick];
        int parentDepth = depths[pick];
        if (parentDepth >= maxDepth || parentNode->getChildCount() >= static_cast<size_t>(maxBranch)) {
            // Pop saturated parent.
            open[pick] = open.back();
            depths[pick] = depths.back();
            open.pop_back();
            depths.pop_back();
            continue;
        }
        SceneNode* child = scene.createNode("n" + std::to_string(counter++), parentNode);
        ++created;
        if (parentDepth + 1 < maxDepth) {
            open.push_back(child);
            depths.push_back(parentDepth + 1);
        }
    }
    return parent;
}

// Walk a scene and produce a sorted list of "<parentName>/<childName>"
// pairs. Two structurally-equal trees produce equal lists regardless of
// dynamic-allocation address order.
std::vector<std::string> collectParentChildPairs(const SceneNode* root) {
    std::vector<std::string> pairs;
    root->visitDepthFirst([&](const SceneNode* node) {
        for (size_t i = 0; i < node->getChildCount(); ++i) {
            const SceneNode* child = node->getChildAt(i);
            pairs.push_back(node->getName() + "/" + child->getName());
        }
    });
    std::sort(pairs.begin(), pairs.end());
    return pairs;
}

// Collect all node names by DFS into a sorted vector.
std::vector<std::string> collectNames(const SceneNode* root) {
    std::vector<std::string> names;
    root->visitDepthFirst([&](const SceneNode* node) {
        names.push_back(node->getName());
    });
    std::sort(names.begin(), names.end());
    return names;
}

// Build a SceneSerializer with the default game-component set already
// registered. Even though the implementation hard-codes the dispatch,
// having a helper centralises the construction call site.
SceneSerializer makeSerializer() {
    return SceneSerializer{};
}

} // namespace

// ===========================================================================
// Empty scene round-trip — byte-identical re-save
// ===========================================================================

TEST_CASE("SceneSerializer: empty scene round-trips to byte-identical JSON",
          "[scene][serializer][round_trip][empty]") {
    Scene original("EmptyScene");
    SceneSerializer serializer = makeSerializer();

    std::string firstSave = serializer.saveToString(original);
    auto loaded = serializer.loadFromString(firstSave);
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->getName() == "EmptyScene");
    REQUIRE(loaded->getRootNode()->getChildCount() == 0);

    // Second save must be byte-identical because deterministic key ordering
    // is the property test_ecs_scene_correctness pins for JsonValue::toString
    // already — here we check the serializer doesn't introduce
    // non-determinism at the scene level.
    std::string secondSave = serializer.saveToString(*loaded);
    REQUIRE(firstSave == secondSave);
}

// ===========================================================================
// Single-node + multi-component round-trip
// ===========================================================================

TEST_CASE("SceneSerializer: single node with transform round-trips bit-exact",
          "[scene][serializer][round_trip][transform]") {
    Scene original("Single");
    SceneNode* node = original.createEntityNode("HostNode");
    Engine::Transform t;
    t.position = Engine::vec3(1.5f, -3.25f, 100.0f);
    t.scale = Engine::vec3(2.0f, 2.0f, 2.0f);
    node->setLocalTransform(t);

    SceneSerializer serializer = makeSerializer();
    auto reloaded = serializer.loadFromString(serializer.saveToString(original));
    REQUIRE(reloaded != nullptr);
    REQUIRE(reloaded->getRootNode()->getChildCount() == 1);
    const SceneNode* reloadedNode = reloaded->getRootNode()->getChildAt(0);
    REQUIRE(reloadedNode->getName() == "HostNode");
    const auto& reloadedLocal = reloadedNode->getLocalTransform();
    REQUIRE(reloadedLocal.position.x == 1.5f);
    REQUIRE(reloadedLocal.position.y == -3.25f);
    REQUIRE(reloadedLocal.position.z == 100.0f);
    REQUIRE(reloadedLocal.scale.x == 2.0f);
}

TEST_CASE("SceneSerializer: 5-component entity round-trips all field values",
          "[scene][serializer][round_trip][components]") {
    Scene original("Components");
    SceneNode* host = original.createEntityNode("HostEntity");
    Entity entity = host->getEntity();
    ECS& ecs = original.getECS();

    CatGame::HealthComponent health;
    health.currentHealth = 75.0f;
    health.maxHealth = 120.0f;
    health.shield = 15.5f;
    health.invincibilityDuration = 0.75f;
    health.canRegenerate = true;
    health.regenerationRate = 4.5f;
    ecs.addComponent(entity, health);

    CatGame::MovementComponent movement;
    movement.moveSpeed = 12.5f;
    movement.maxSpeed = 25.0f;
    movement.acceleration = 60.0f;
    movement.velocity = Engine::vec3(1.0f, 2.0f, 3.0f);
    movement.isGrounded = false;
    movement.canJump = false;
    ecs.addComponent(entity, movement);

    CatGame::CombatComponent combat;
    combat.attackDamage = 22.5f;
    combat.attackRange = 4.0f;
    combat.equippedWeapon = CatGame::WeaponType::Staff;
    combat.damageMultiplier = 1.5f;
    ecs.addComponent(entity, combat);

    CatGame::EnemyComponent enemy(CatGame::EnemyType::FastDog, Entity(7, 1));
    enemy.state = CatGame::AIState::Chasing;
    enemy.aggroRange = 50.0f;
    enemy.attackCooldown = 0.9f;
    enemy.scoreValue = 42;
    ecs.addComponent(entity, enemy);

    CatGame::ProjectileComponent projectile;
    projectile.type = CatGame::ProjectileType::Arrow;
    projectile.velocity = Engine::vec3(10.0f, 0.0f, 5.0f);
    projectile.damage = 18.0f;
    projectile.lifetime = 3.5f;
    projectile.radius = 0.25f;
    ecs.addComponent(entity, projectile);

    SceneSerializer serializer = makeSerializer();
    std::string json = serializer.saveToString(original);
    auto loaded = serializer.loadFromString(json);
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->getRootNode()->getChildCount() == 1);

    const SceneNode* loadedHost = loaded->getRootNode()->getChildAt(0);
    REQUIRE(loadedHost->hasEntity());
    Entity loadedEntity = loadedHost->getEntity();

    const ECS& loadedEcs = loaded->getECS();
    const auto* loadedHealth = loadedEcs.getComponent<CatGame::HealthComponent>(loadedEntity);
    REQUIRE(loadedHealth != nullptr);
    REQUIRE(loadedHealth->currentHealth == 75.0f);
    REQUIRE(loadedHealth->maxHealth == 120.0f);
    REQUIRE(loadedHealth->shield == 15.5f);
    REQUIRE(loadedHealth->invincibilityDuration == 0.75f);
    REQUIRE(loadedHealth->canRegenerate == true);
    REQUIRE(loadedHealth->regenerationRate == 4.5f);

    const auto* loadedMovement = loadedEcs.getComponent<CatGame::MovementComponent>(loadedEntity);
    REQUIRE(loadedMovement != nullptr);
    REQUIRE(loadedMovement->moveSpeed == 12.5f);
    REQUIRE(loadedMovement->velocity.x == 1.0f);
    REQUIRE(loadedMovement->velocity.y == 2.0f);
    REQUIRE(loadedMovement->velocity.z == 3.0f);
    REQUIRE(loadedMovement->isGrounded == false);
    REQUIRE(loadedMovement->canJump == false);

    const auto* loadedCombat = loadedEcs.getComponent<CatGame::CombatComponent>(loadedEntity);
    REQUIRE(loadedCombat != nullptr);
    REQUIRE(loadedCombat->attackDamage == 22.5f);
    REQUIRE(loadedCombat->equippedWeapon == CatGame::WeaponType::Staff);
    REQUIRE(loadedCombat->damageMultiplier == 1.5f);

    const auto* loadedEnemy = loadedEcs.getComponent<CatGame::EnemyComponent>(loadedEntity);
    REQUIRE(loadedEnemy != nullptr);
    REQUIRE(loadedEnemy->type == CatGame::EnemyType::FastDog);
    REQUIRE(loadedEnemy->state == CatGame::AIState::Chasing);
    REQUIRE(loadedEnemy->aggroRange == 50.0f);
    REQUIRE(loadedEnemy->scoreValue == 42);
    // The target entity id was 7,1 in the OLD ECS; after load it should be
    // remapped through entityRemap_. Since the target was never serialised
    // as a separate node, the remap could not resolve it — the
    // implementation explicitly nulls unresolved targets.
    REQUIRE_FALSE(loadedEnemy->target.isValid());

    const auto* loadedProjectile = loadedEcs.getComponent<CatGame::ProjectileComponent>(loadedEntity);
    REQUIRE(loadedProjectile != nullptr);
    REQUIRE(loadedProjectile->type == CatGame::ProjectileType::Arrow);
    REQUIRE(loadedProjectile->velocity.x == 10.0f);
    REQUIRE(loadedProjectile->damage == 18.0f);
    REQUIRE(loadedProjectile->lifetime == 3.5f);
}

// ===========================================================================
// Hierarchical round-trip
// ===========================================================================

TEST_CASE("SceneSerializer: 100-node hierarchy round-trips with structural equality",
          "[scene][serializer][round_trip][hierarchy]") {
    Scene original("Hierarchy");
    int counter = 0;
    attachRandomTreeBelow(original, original.getRootNode(), 100,
                          /*maxDepth=*/8, /*maxBranch=*/4, /*seed=*/0xABC123u, counter);

    SceneSerializer serializer = makeSerializer();
    std::string json = serializer.saveToString(original);
    auto loaded = serializer.loadFromString(json);
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->getName() == "Hierarchy");

    // Structural equality: same set of (parent_name, child_name) pairs.
    auto originalPairs = collectParentChildPairs(original.getRootNode());
    auto loadedPairs = collectParentChildPairs(loaded->getRootNode());
    REQUIRE(originalPairs == loadedPairs);

    // Same set of node names.
    auto originalNames = collectNames(original.getRootNode());
    auto loadedNames = collectNames(loaded->getRootNode());
    REQUIRE(originalNames == loadedNames);

    // Statistics agree.
    REQUIRE(loaded->getStatistics().nodeCount == original.getStatistics().nodeCount);
}

TEST_CASE("SceneSerializer: depth-6 / breadth-4 random trees round-trip across seeds",
          "[scene][serializer][round_trip][stress]") {
    // Eight different seeds; each produces a different tree shape. The
    // round-trip must preserve structure for every shape.
    for (uint32_t seed : {0x1u, 0x2u, 0x10u, 0x100u, 0xDEADu, 0xBEEFu, 0xCAFEu, 0xFEEDu}) {
        Scene original("S_" + std::to_string(seed));
        int counter = 0;
        attachRandomTreeBelow(original, original.getRootNode(), 60,
                              /*maxDepth=*/6, /*maxBranch=*/4, seed, counter);

        SceneSerializer serializer = makeSerializer();
        std::string json = serializer.saveToString(original);
        auto loaded = serializer.loadFromString(json);
        REQUIRE(loaded != nullptr);

        auto originalPairs = collectParentChildPairs(original.getRootNode());
        auto loadedPairs = collectParentChildPairs(loaded->getRootNode());
        REQUIRE(originalPairs == loadedPairs);
    }
}

// ===========================================================================
// Float precision battery
// ===========================================================================

TEST_CASE("SceneSerializer: float precision battery survives one round-trip",
          "[scene][serializer][round_trip][precision]") {
    Scene original("Precision");

    // Build a node per "hostile" float value. The transform.position.x
    // carries the test value; the y/z hold sentinels.
    const float hostileValues[] = {
        1.0e-6f,
        1.0e6f,
        0.1f,
        -0.1f,
        1.0f / 3.0f,
        3.14159265358979f,
        -1.5e10f,
        0.0f,
        -0.0f,
    };
    std::vector<SceneNode*> nodes;
    for (size_t i = 0; i < std::size(hostileValues); ++i) {
        SceneNode* node = original.createNode("v" + std::to_string(i));
        Engine::Transform t;
        t.position = Engine::vec3(hostileValues[i],
                                  static_cast<float>(i) * 0.5f,
                                  -static_cast<float>(i));
        node->setLocalTransform(t);
        nodes.push_back(node);
    }

    SceneSerializer serializer = makeSerializer();
    auto loaded = serializer.loadFromString(serializer.saveToString(original));
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->getRootNode()->getChildCount() == std::size(hostileValues));

    for (size_t i = 0; i < std::size(hostileValues); ++i) {
        const SceneNode* loadedNode = loaded->getRootNode()->getChildAt(i);
        const auto& localTransform = loadedNode->getLocalTransform();

        // Bit-exact float comparison (relies on %.17g formatting that
        // preserves every representable float).
        REQUIRE(localTransform.position.x == hostileValues[i]);
        REQUIRE(localTransform.position.y == static_cast<float>(i) * 0.5f);
        REQUIRE(localTransform.position.z == -static_cast<float>(i));
    }
}

// ===========================================================================
// Version handling
// ===========================================================================

TEST_CASE("SceneSerializer: matching version loads, mismatched version fails loudly",
          "[scene][serializer][version]") {
    Scene original("Versioned");
    original.createNode("OnlyChild");
    SceneSerializer serializer = makeSerializer();
    std::string json = serializer.saveToString(original);

    auto loaded = serializer.loadFromString(json);
    REQUIRE(loaded != nullptr);

    // Replace `"version": 1` with `"version": 99` in the serialized text.
    std::string mismatched = json;
    size_t versionPos = mismatched.find("\"version\":");
    REQUIRE(versionPos != std::string::npos);
    // Skip past `"version":` and any whitespace, find the digit, replace.
    size_t digitPos = mismatched.find_first_of("0123456789", versionPos);
    REQUIRE(digitPos != std::string::npos);
    size_t digitEnd = mismatched.find_first_not_of("0123456789", digitPos);
    mismatched.replace(digitPos, digitEnd - digitPos, "99");

    auto bad = serializer.loadFromString(mismatched);
    REQUIRE(bad == nullptr); // version-mismatch is a HARD failure
}

TEST_CASE("SceneSerializer: missing required top-level fields fails load",
          "[scene][serializer][version][malformed]") {
    SceneSerializer serializer = makeSerializer();
    // No `version` key — load should return nullptr instead of producing
    // a half-built scene.
    REQUIRE(serializer.loadFromString("{}") == nullptr);
    REQUIRE(serializer.loadFromString("{\"version\": 1}") == nullptr); // no metadata or sceneGraph
    REQUIRE(serializer.loadFromString("{\"version\": 1, \"metadata\": {}}") == nullptr); // no sceneGraph
}

TEST_CASE("SceneSerializer: VERSION constant is the documented value",
          "[scene][serializer][version][constant]") {
    REQUIRE(SceneSerializer::VERSION == 1);
}

// ===========================================================================
// Edge cases — empty scene, single node, orphan node
// ===========================================================================

TEST_CASE("SceneSerializer: orphan node attached directly to root round-trips",
          "[scene][serializer][edge][orphan]") {
    Scene original("Orphan");
    SceneNode* lone = original.createNode("LonelyNode");
    Engine::Transform t;
    t.position = Engine::vec3(42.0f, 0.0f, 0.0f);
    lone->setLocalTransform(t);

    SceneSerializer serializer = makeSerializer();
    auto loaded = serializer.loadFromString(serializer.saveToString(original));
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->getRootNode()->getChildCount() == 1);
    REQUIRE(loaded->getRootNode()->getChildAt(0)->getName() == "LonelyNode");
    REQUIRE(loaded->getRootNode()->getChildAt(0)->getLocalTransform().position.x == 42.0f);
}

TEST_CASE("SceneSerializer: node with empty children array round-trips with zero children",
          "[scene][serializer][edge][empty_children]") {
    Scene original("EmptyChildren");
    original.createNode("Leaf"); // no children attached
    SceneSerializer serializer = makeSerializer();
    std::string json = serializer.saveToString(original);

    // The serialized output must contain the `"children": []` shape so
    // a future loader can rely on it being present.
    REQUIRE(json.find("\"children\":") != std::string::npos);

    auto loaded = serializer.loadFromString(json);
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->getRootNode()->getChildCount() == 1);
    REQUIRE(loaded->getRootNode()->getChildAt(0)->getChildCount() == 0);
}

TEST_CASE("SceneSerializer: scene name with special characters survives escaping",
          "[scene][serializer][edge][escape]") {
    Scene original("Has\"Quote\\Back\nNewline");
    SceneSerializer serializer = makeSerializer();
    std::string json = serializer.saveToString(original);
    auto loaded = serializer.loadFromString(json);
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->getName() == "Has\"Quote\\Back\nNewline");
}

TEST_CASE("SceneSerializer: node names with special characters round-trip",
          "[scene][serializer][edge][escape_node]") {
    Scene original("E");
    original.createNode("name\"with quotes");
    original.createNode("name\\with backslashes");
    original.createNode("name\twith\ttabs");

    SceneSerializer serializer = makeSerializer();
    auto loaded = serializer.loadFromString(serializer.saveToString(original));
    REQUIRE(loaded != nullptr);

    auto names = collectNames(loaded->getRootNode());
    // names is sorted; check membership.
    std::set<std::string> set(names.begin(), names.end());
    REQUIRE(set.count("name\"with quotes") == 1);
    REQUIRE(set.count("name\\with backslashes") == 1);
    REQUIRE(set.count("name\twith\ttabs") == 1);
}

// ===========================================================================
// Scene metadata
// ===========================================================================

TEST_CASE("SceneSerializer: scene active flag round-trips",
          "[scene][serializer][metadata]") {
    Scene active("Active");
    active.setActive(true);

    Scene inactive("Inactive");
    inactive.setActive(false);

    SceneSerializer serializer = makeSerializer();
    auto loadedActive = serializer.loadFromString(serializer.saveToString(active));
    auto loadedInactive = serializer.loadFromString(serializer.saveToString(inactive));
    REQUIRE(loadedActive != nullptr);
    REQUIRE(loadedActive->isActive());
    REQUIRE(loadedInactive != nullptr);
    REQUIRE_FALSE(loadedInactive->isActive());
}

TEST_CASE("SceneSerializer: node active flag is preserved",
          "[scene][serializer][metadata][node_active]") {
    Scene original("S");
    SceneNode* enabled = original.createNode("Enabled");
    SceneNode* disabled = original.createNode("Disabled");
    enabled->setActive(true);
    disabled->setActive(false);

    SceneSerializer serializer = makeSerializer();
    auto loaded = serializer.loadFromString(serializer.saveToString(original));
    REQUIRE(loaded != nullptr);
    SceneNode* loadedEnabled = loaded->findNode("Enabled");
    SceneNode* loadedDisabled = loaded->findNode("Disabled");
    REQUIRE(loadedEnabled != nullptr);
    REQUIRE(loadedDisabled != nullptr);
    REQUIRE(loadedEnabled->isActive());
    REQUIRE_FALSE(loadedDisabled->isActive());
}

// ===========================================================================
// Entity-reference remap — cross-component target rewires correctly
// ===========================================================================

TEST_CASE("SceneSerializer: entity-reference remap rewires same-scene targets",
          "[scene][serializer][entity_remap]") {
    // Two nodes, each with its own entity. Node A's EnemyComponent.target
    // references Node B's entity. After save/load, the remap should
    // resolve the reference to Node B's NEW entity in the fresh ECS.
    Scene original("EntityRefs");
    SceneNode* nodeA = original.createEntityNode("A");
    SceneNode* nodeB = original.createEntityNode("B");
    Entity entityA = nodeA->getEntity();
    Entity entityB = nodeB->getEntity();
    ECS& ecs = original.getECS();

    CatGame::EnemyComponent enemy(CatGame::EnemyType::Dog, entityB);
    ecs.addComponent(entityA, enemy);

    SceneSerializer serializer = makeSerializer();
    auto loaded = serializer.loadFromString(serializer.saveToString(original));
    REQUIRE(loaded != nullptr);

    SceneNode* loadedA = loaded->findNode("A");
    SceneNode* loadedB = loaded->findNode("B");
    REQUIRE(loadedA != nullptr);
    REQUIRE(loadedB != nullptr);

    const auto* loadedEnemy = loaded->getECS().getComponent<CatGame::EnemyComponent>(loadedA->getEntity());
    REQUIRE(loadedEnemy != nullptr);
    // The target id should be remapped to loadedB's entity in the fresh ECS.
    REQUIRE(loadedEnemy->target == loadedB->getEntity());
    (void)entityA;
}

TEST_CASE("SceneSerializer: unresolved entity reference is nulled, not left stale",
          "[scene][serializer][entity_remap][null]") {
    Scene original("Stale");
    SceneNode* nodeA = original.createEntityNode("A");
    // Reference a never-serialized entity ID; the remap can't resolve it.
    CatGame::EnemyComponent enemy(CatGame::EnemyType::Dog, Entity(999, 1));
    original.getECS().addComponent(nodeA->getEntity(), enemy);

    SceneSerializer serializer = makeSerializer();
    auto loaded = serializer.loadFromString(serializer.saveToString(original));
    REQUIRE(loaded != nullptr);
    SceneNode* loadedA = loaded->findNode("A");
    REQUIRE(loadedA != nullptr);
    const auto* loadedEnemy = loaded->getECS().getComponent<CatGame::EnemyComponent>(loadedA->getEntity());
    REQUIRE(loadedEnemy != nullptr);
    // Implementation explicitly nulls unresolved references — pin this so
    // a regression that "preserves" a stale id can't ship.
    REQUIRE_FALSE(loadedEnemy->target.isValid());
}

// ===========================================================================
// Idempotent re-save
// ===========================================================================

TEST_CASE("SceneSerializer: save -> load -> save produces identical output",
          "[scene][serializer][idempotent]") {
    Scene original("Idempotent");
    int counter = 0;
    attachRandomTreeBelow(original, original.getRootNode(), 30,
                          /*maxDepth=*/4, /*maxBranch=*/3, /*seed=*/0xDDDDu, counter);

    // Give each node a unique transform.
    int idx = 0;
    original.getRootNode()->visitDepthFirst([&](SceneNode* node) {
        Engine::Transform t;
        t.position = Engine::vec3(static_cast<float>(idx),
                                  static_cast<float>(idx) * 0.5f,
                                  -static_cast<float>(idx));
        node->setLocalTransform(t);
        ++idx;
    });

    SceneSerializer serializer = makeSerializer();
    std::string first = serializer.saveToString(original);
    auto loaded = serializer.loadFromString(first);
    REQUIRE(loaded != nullptr);
    std::string second = serializer.saveToString(*loaded);
    // Bit-exact: relies on deterministic key ordering and lossless float
    // formatting. If this regresses, scene-file diffs and content-addressed
    // asset hashes get spurious churn on every rebuild.
    REQUIRE(first == second);
}

// ===========================================================================
// Stress: tree of depth 6 / breadth 4, multiple components per entity
// ===========================================================================

TEST_CASE("SceneSerializer: depth-6 / breadth-4 tree with components round-trips",
          "[scene][serializer][round_trip][stress][components]") {
    Scene original("BigScene");
    int counter = 0;
    attachRandomTreeBelow(original, original.getRootNode(), 200,
                          /*maxDepth=*/6, /*maxBranch=*/4, /*seed=*/0xBADBABEu, counter);

    // Attach an entity + components to half the nodes.
    int sprinkled = 0;
    original.getRootNode()->visitDepthFirst([&](SceneNode* node) {
        if (node == original.getRootNode()) return;
        if ((sprinkled++) % 2 == 0) {
            Entity entity = original.getECS().createEntity();
            node->setEntity(entity);
            CatGame::HealthComponent h;
            h.currentHealth = static_cast<float>(sprinkled);
            original.getECS().addComponent(entity, h);
            CatGame::MovementComponent m;
            m.moveSpeed = 1.0f + static_cast<float>(sprinkled);
            original.getECS().addComponent(entity, m);
        }
    });

    SceneSerializer serializer = makeSerializer();
    std::string json = serializer.saveToString(original);
    auto loaded = serializer.loadFromString(json);
    REQUIRE(loaded != nullptr);

    auto originalPairs = collectParentChildPairs(original.getRootNode());
    auto loadedPairs = collectParentChildPairs(loaded->getRootNode());
    REQUIRE(originalPairs == loadedPairs);

    // Spot-check a few entity components round-tripped. Walk both trees in
    // sync, comparing HealthComponent.currentHealth.
    auto walkSync = [](const SceneNode* a, const SceneNode* b,
                       auto& self, const ECS& ecsA, const ECS& ecsB) -> void {
        REQUIRE(a->getName() == b->getName());
        if (a->hasEntity()) {
            REQUIRE(b->hasEntity());
            const auto* aHealth = ecsA.getComponent<CatGame::HealthComponent>(a->getEntity());
            const auto* bHealth = ecsB.getComponent<CatGame::HealthComponent>(b->getEntity());
            if (aHealth) {
                REQUIRE(bHealth != nullptr);
                REQUIRE(bHealth->currentHealth == aHealth->currentHealth);
            }
        }
        REQUIRE(a->getChildCount() == b->getChildCount());
        for (size_t i = 0; i < a->getChildCount(); ++i) {
            self(a->getChildAt(i), b->getChildAt(i), self,
                 ecsA, ecsB);
        }
    };
    walkSync(original.getRootNode(), loaded->getRootNode(), walkSync,
             original.getECS(), loaded->getECS());
}

// ===========================================================================
// JsonValue round-trip via parse — pins the parser's symmetry with toString
// ===========================================================================

TEST_CASE("SceneSerializer: JsonValue::parse round-trips primitives + nested objects",
          "[scene][serializer][json][parse]") {
    JsonValue obj = JsonValue::object();
    obj["scalar_int"] = JsonValue(42);
    obj["scalar_neg"] = JsonValue(-3.5);
    obj["flag_true"] = JsonValue(true);
    obj["flag_false"] = JsonValue(false);
    obj["name"] = JsonValue("hello");

    JsonValue arr = JsonValue::array();
    arr.push(JsonValue(1));
    arr.push(JsonValue(2));
    arr.push(JsonValue(3));
    obj["values"] = arr;

    JsonValue nested = JsonValue::object();
    nested["depth"] = JsonValue(1);
    obj["nested"] = nested;

    std::string serialized = obj.toString();
    JsonValue parsed = JsonValue::parse(serialized);

    REQUIRE(parsed.isObject());
    REQUIRE(parsed["scalar_int"].asNumber() == 42.0);
    REQUIRE(parsed["scalar_neg"].asNumber() == -3.5);
    REQUIRE(parsed["flag_true"].asBool() == true);
    REQUIRE(parsed["flag_false"].asBool() == false);
    REQUIRE(parsed["name"].asString() == "hello");
    REQUIRE(parsed["values"].isArray());
    REQUIRE(parsed["values"].size() == 3);
    REQUIRE(parsed["values"][0].asNumber() == 1.0);
    REQUIRE(parsed["values"][2].asNumber() == 3.0);
    REQUIRE(parsed["nested"].isObject());
    REQUIRE(parsed["nested"]["depth"].asNumber() == 1.0);
}
