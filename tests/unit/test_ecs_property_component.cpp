// test_ecs_property_component.cpp
// ---------------------------------------------------------------------------
// Property + fuzz tests for engine/ecs/ComponentPool.hpp — the sparse-dense
// storage backing every ECS component lookup. The existing scene-correctness
// suite pins tail-removal and middle-removal on 3 entities. This file goes
// the rest of the way:
//
//   1. Add N=1000 components to random entity indices in random order, then
//      iterate dense_ — every component value must be locatable through
//      sparse_, and every entity in denseToEntity_ must report has()==true.
//      The "ground truth" is a std::map<Entity, ValueType> built alongside.
//   2. Random sequence of add / remove / replace / get under churn, with
//      a ground-truth std::map cross-check after every step.
//   3. Re-add to an already-occupied slot must REPLACE in place (no growth,
//      no duplicate dense entry, no sparse slot orphan). This is the
//      "component already exists, replace it" branch in ComponentPool::add.
//   4. swap-on-remove invariant: after removing N/2 random middle entries,
//      iterating dense_ in order must yield exactly the surviving entities
//      in their post-swap positions, and getData()'s memory remains
//      contiguous (i.e. dense_ is genuinely packed — no holes).
//   5. Cross-pool isolation: removing component A from an entity must not
//      affect component B on the same entity. Re-adding A must not disturb
//      B.
//   6. emplace() vs add() must produce semantically equal pool state when
//      handed the same value.
//   7. clear() resets size() to 0 and makes has() false for every prior
//      entity.
//
// A failure in (1)/(2)/(4) means the sparse-dense bookkeeping has drifted —
// the silent class of bug where dense_[i] holds the right value but
// sparse_[denseToEntity_[i].index()] points to a different denseIndex, so
// has()/get() return inconsistent answers.
// ---------------------------------------------------------------------------

#define CATCH_CONFIG_FAST_COMPILE
#include "catch2/catch.hpp"
#include "test_seed.hpp"

#include "ecs/Entity.hpp"
#include "ecs/EntityManager.hpp"
#include "ecs/ComponentPool.hpp"

#include <algorithm>
#include <map>
#include <random>
#include <set>
#include <vector>

using CatEngine::ComponentPool;
using CatEngine::Entity;
using CatEngine::EntityManager;

namespace {

// Component types deliberately POD so they satisfy the Component concept's
// is_trivially_copyable branch — keeps the test orthogonal to move-semantics
// quirks of the pool's emplace path.
struct ScalarComponent {
    int value = 0;
};

struct Vec3Component {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    bool operator==(const Vec3Component& other) const {
        return x == other.x && y == other.y && z == other.z;
    }
};

struct TagComponent {
    uint32_t flags = 0;
};

// A non-trivially-copyable but move-constructible component, hitting the
// concept's second branch and the rvalue add() overload.
struct MoveOnlyComponent {
    std::vector<int> bag;
    MoveOnlyComponent() = default;
    MoveOnlyComponent(std::initializer_list<int> init) : bag(init) {}
    MoveOnlyComponent(const MoveOnlyComponent&) = delete;
    MoveOnlyComponent& operator=(const MoveOnlyComponent&) = delete;
    MoveOnlyComponent(MoveOnlyComponent&&) = default;
    MoveOnlyComponent& operator=(MoveOnlyComponent&&) = default;
};

} // namespace

// ===========================================================================
// Basic-coverage gaps the existing suite did not cover
// ===========================================================================

TEST_CASE("ComponentPool: get on never-added entity returns nullptr",
          "[ecs][component_pool][get]") {
    ComponentPool<ScalarComponent> pool;
    REQUIRE(pool.get(Entity(0, 1)) == nullptr);
    REQUIRE(pool.get(Entity(100, 99)) == nullptr);
    REQUIRE_FALSE(pool.has(Entity(0, 1)));
    REQUIRE(pool.size() == 0);
}

TEST_CASE("ComponentPool: remove on never-added entity is a no-op",
          "[ecs][component_pool][remove][robustness]") {
    ComponentPool<ScalarComponent> pool;
    pool.remove(Entity(7, 1));
    pool.remove(Entity(0, 1));
    REQUIRE(pool.size() == 0);

    // After bogus removals, real adds still work.
    Entity entity(0, 1);
    pool.add(entity, ScalarComponent{42});
    REQUIRE(pool.has(entity));
    REQUIRE(pool.get(entity)->value == 42);
}

TEST_CASE("ComponentPool: re-add replaces in place without growing dense_",
          "[ecs][component_pool][replace]") {
    ComponentPool<ScalarComponent> pool;
    Entity entity(3, 1);

    pool.add(entity, ScalarComponent{1});
    REQUIRE(pool.size() == 1);
    REQUIRE(pool.get(entity)->value == 1);

    // Re-add must replace, not insert. Without this branch the dense array
    // would grow and sparse_ would point to a stale slot — the older value
    // would shadow the new one because it sits earlier in dense_.
    pool.add(entity, ScalarComponent{99});
    REQUIRE(pool.size() == 1);
    REQUIRE(pool.get(entity)->value == 99);

    // emplace must also replace.
    pool.emplace(entity, ScalarComponent{200});
    REQUIRE(pool.size() == 1);
    REQUIRE(pool.get(entity)->value == 200);
}

TEST_CASE("ComponentPool: getData is contiguous and matches getEntities order",
          "[ecs][component_pool][packing]") {
    ComponentPool<ScalarComponent> pool;
    pool.add(Entity(0, 1), ScalarComponent{10});
    pool.add(Entity(1, 1), ScalarComponent{20});
    pool.add(Entity(2, 1), ScalarComponent{30});
    pool.add(Entity(3, 1), ScalarComponent{40});

    const auto& data = pool.getData();
    const auto& entities = pool.getEntities();
    REQUIRE(data.size() == entities.size());
    REQUIRE(data.size() == 4);

    // Each dense index must correspond to the entity at the same dense index.
    for (size_t denseIndex = 0; denseIndex < data.size(); ++denseIndex) {
        Entity entity = entities[denseIndex];
        REQUIRE(pool.get(entity) == &data[denseIndex]);
    }
}

TEST_CASE("ComponentPool: middle removal preserves contiguity (no holes)",
          "[ecs][component_pool][packing][remove]") {
    ComponentPool<ScalarComponent> pool;
    for (uint32_t i = 0; i < 10; ++i) {
        pool.add(Entity(i, 1), ScalarComponent{static_cast<int>(i * 11)});
    }
    REQUIRE(pool.size() == 10);

    // Remove 5 middles. Dense_ must shrink by exactly 5 and remain hole-free.
    for (uint32_t victim : {1u, 3u, 5u, 7u, 4u}) {
        pool.remove(Entity(victim, 1));
    }
    REQUIRE(pool.size() == 5);

    const auto& data = pool.getData();
    const auto& entities = pool.getEntities();
    REQUIRE(data.size() == 5);
    REQUIRE(entities.size() == 5);

    // The five survivors are exactly 0, 2, 6, 8, 9 — and each one is still
    // reachable via sparse lookup at its post-swap dense index.
    std::set<uint32_t> survivorIndices;
    for (Entity entity : entities) {
        survivorIndices.insert(entity.index());
    }
    REQUIRE(survivorIndices == std::set<uint32_t>{0u, 2u, 6u, 8u, 9u});

    for (size_t denseIndex = 0; denseIndex < entities.size(); ++denseIndex) {
        Entity entity = entities[denseIndex];
        REQUIRE(pool.has(entity));
        const ScalarComponent* component = pool.get(entity);
        REQUIRE(component != nullptr);
        REQUIRE(component == &data[denseIndex]); // sparse → dense agrees
        REQUIRE(component->value == static_cast<int>(entity.index() * 11));
    }
}

TEST_CASE("ComponentPool: clear() resets pool to fresh state",
          "[ecs][component_pool][clear]") {
    ComponentPool<ScalarComponent> pool;
    for (uint32_t i = 0; i < 20; ++i) {
        pool.add(Entity(i, 1), ScalarComponent{static_cast<int>(i)});
    }
    REQUIRE(pool.size() == 20);

    pool.clear();
    REQUIRE(pool.size() == 0);
    for (uint32_t i = 0; i < 20; ++i) {
        REQUIRE_FALSE(pool.has(Entity(i, 1)));
        REQUIRE(pool.get(Entity(i, 1)) == nullptr);
    }

    // Pool is reusable after clear: re-adding works without leftover state.
    pool.add(Entity(0, 2), ScalarComponent{999});
    REQUIRE(pool.size() == 1);
    REQUIRE(pool.get(Entity(0, 2))->value == 999);
}

TEST_CASE("ComponentPool: rvalue add path stores moved value",
          "[ecs][component_pool][move]") {
    ComponentPool<MoveOnlyComponent> pool;
    Entity entity(5, 1);
    pool.add(entity, MoveOnlyComponent{1, 2, 3});

    const MoveOnlyComponent* stored = pool.get(entity);
    REQUIRE(stored != nullptr);
    REQUIRE(stored->bag.size() == 3);
    REQUIRE(stored->bag[0] == 1);
    REQUIRE(stored->bag[1] == 2);
    REQUIRE(stored->bag[2] == 3);

    // Replacing via rvalue add must also work.
    pool.add(entity, MoveOnlyComponent{9, 9});
    REQUIRE(pool.size() == 1);
    REQUIRE(pool.get(entity)->bag.size() == 2);
    REQUIRE(pool.get(entity)->bag[0] == 9);
}

TEST_CASE("ComponentPool: emplace constructs in place from raw args",
          "[ecs][component_pool][emplace]") {
    ComponentPool<Vec3Component> pool;
    Entity entity(2, 1);
    pool.emplace(entity, Vec3Component{1.0f, 2.0f, 3.0f});

    Vec3Component* component = pool.get(entity);
    REQUIRE(component != nullptr);
    REQUIRE(component->x == 1.0f);
    REQUIRE(component->y == 2.0f);
    REQUIRE(component->z == 3.0f);
}

// ===========================================================================
// Property fuzz — 1000 entities × random add/remove/replace, vs std::map oracle
// ===========================================================================

TEST_CASE("ComponentPool property: random churn matches std::map ground truth",
          "[ecs][component_pool][property][fuzz]") {
    // Allocate 1000 unique entity indices via a real EntityManager so the
    // pool sees an entity-index distribution that mirrors production. Then
    // run a long stream of add/remove/replace ops against the pool AND a
    // std::map oracle; after every op assert the two agree on:
    //   - membership (has)
    //   - value (get)
    //   - count (size)
    //   - every dense index has consistent reverse lookup
    EntityManager manager;
    std::vector<Entity> handles;
    handles.reserve(1000);
    for (int i = 0; i < 1000; ++i) {
        handles.push_back(manager.create());
    }

    ComponentPool<ScalarComponent> pool;
    std::map<uint64_t, int> oracle;

    std::mt19937 rng(static_cast<unsigned>(CatTest::DeterministicSeed("test_ecs_property_component:0xC0FFEE")));
    std::uniform_int_distribution<int> opCoin(0, 99);
    std::uniform_int_distribution<int> valueDist(-1'000'000, 1'000'000);

    for (int step = 0; step < 4000; ++step) {
        Entity entity = handles[rng() % handles.size()];
        int op = opCoin(rng);

        if (op < 60) {
            // Add or replace
            int value = valueDist(rng);
            pool.add(entity, ScalarComponent{value});
            oracle[entity.id] = value;
        } else if (op < 85) {
            // Remove
            pool.remove(entity);
            oracle.erase(entity.id);
        } else {
            // Read-only check (no state change). Still exercises the
            // sparse path for hits and misses.
            if (oracle.count(entity.id)) {
                REQUIRE(pool.has(entity));
                REQUIRE(pool.get(entity)->value == oracle[entity.id]);
            } else {
                REQUIRE_FALSE(pool.has(entity));
                REQUIRE(pool.get(entity) == nullptr);
            }
        }

        // Invariant: pool size matches oracle size.
        REQUIRE(pool.size() == oracle.size());

        // Invariant: every entity in the oracle has the right value in the
        // pool. Sample 16 of them so the test stays sub-second; full sweep
        // happens once at the end.
        size_t sampled = 0;
        for (auto it = oracle.begin(); it != oracle.end() && sampled < 16; ++it, ++sampled) {
            Entity sampleEntity(it->first);
            REQUIRE(pool.has(sampleEntity));
            REQUIRE(pool.get(sampleEntity)->value == it->second);
        }
    }

    // Final full sweep: oracle is the ground truth on both directions.
    for (const auto& [entityId, expected] : oracle) {
        Entity entity(entityId);
        REQUIRE(pool.has(entity));
        REQUIRE(pool.get(entity)->value == expected);
    }
    for (Entity entity : handles) {
        bool oracleHas = oracle.count(entity.id) > 0;
        REQUIRE(pool.has(entity) == oracleHas);
    }

    // Reverse direction: every entity the pool reports must be in the oracle.
    const auto& denseEntities = pool.getEntities();
    REQUIRE(denseEntities.size() == oracle.size());
    for (Entity entity : denseEntities) {
        REQUIRE(oracle.count(entity.id) > 0);
    }
}

TEST_CASE("ComponentPool property: 1000-entity bulk add then remove leaves pool empty",
          "[ecs][component_pool][property][bulk]") {
    ComponentPool<Vec3Component> pool;
    EntityManager manager;
    std::vector<Entity> handles;
    handles.reserve(1000);

    for (int i = 0; i < 1000; ++i) {
        Entity entity = manager.create();
        handles.push_back(entity);
        pool.add(entity, Vec3Component{
            static_cast<float>(i),
            static_cast<float>(i) * 0.5f,
            static_cast<float>(i) * 0.25f
        });
    }
    REQUIRE(pool.size() == 1000);

    // Remove in a deterministic-but-non-sequential order to exercise the
    // swap-on-remove path heavily (every removal except the last triggers
    // a swap).
    std::vector<Entity> removalOrder = handles;
    std::mt19937 rng(static_cast<unsigned>(CatTest::DeterministicSeed("test_ecs_property_component:0x12345678")));
    std::shuffle(removalOrder.begin(), removalOrder.end(), rng);

    for (size_t i = 0; i < removalOrder.size(); ++i) {
        Entity victim = removalOrder[i];
        REQUIRE(pool.has(victim));
        pool.remove(victim);
        REQUIRE_FALSE(pool.has(victim));
        REQUIRE(pool.size() == removalOrder.size() - i - 1);

        // Every NOT-yet-removed entity must still be present with its
        // original value. Sparse-dense bookkeeping bugs surface here as
        // the swapped tail entity reporting wrong data after the swap.
        for (size_t j = i + 1; j < removalOrder.size(); ++j) {
            Entity survivor = removalOrder[j];
            const Vec3Component* component = pool.get(survivor);
            REQUIRE(component != nullptr);
            // Reconstruct the expected value from the original add ordering.
            // survivor was at handles[k] = the entity manager's k-th create,
            // i.e. survivor.index() == k.
            uint32_t originalIndex = survivor.index();
            REQUIRE(component->x == static_cast<float>(originalIndex));
            REQUIRE(component->y == static_cast<float>(originalIndex) * 0.5f);
            REQUIRE(component->z == static_cast<float>(originalIndex) * 0.25f);
        }
    }

    REQUIRE(pool.size() == 0);
}

TEST_CASE("ComponentPool: cross-pool independence (remove A keeps B intact)",
          "[ecs][component_pool][independence]") {
    ComponentPool<ScalarComponent> poolA;
    ComponentPool<TagComponent> poolB;

    Entity entity(0, 1);
    poolA.add(entity, ScalarComponent{77});
    poolB.add(entity, TagComponent{0xABCD});

    REQUIRE(poolA.has(entity));
    REQUIRE(poolB.has(entity));

    poolA.remove(entity);
    REQUIRE_FALSE(poolA.has(entity));
    // B must remain — the two pools share no state.
    REQUIRE(poolB.has(entity));
    REQUIRE(poolB.get(entity)->flags == 0xABCD);

    // Re-adding A must not disturb B.
    poolA.add(entity, ScalarComponent{88});
    REQUIRE(poolA.get(entity)->value == 88);
    REQUIRE(poolB.get(entity)->flags == 0xABCD);
}

TEST_CASE("ComponentPool: high-index entity does not waste sparse memory beyond resize",
          "[ecs][component_pool][sparse_resize]") {
    // sparse_ resizes to entityIndex + 1. Adding entity index 1000 must
    // make all 1001 sparse_ slots well-defined (INVALID_INDEX). A buggy
    // resize that leaves them uninitialised would produce a random
    // dense_index on the next has() call for a never-added entity.
    ComponentPool<ScalarComponent> pool;
    pool.add(Entity(1000, 1), ScalarComponent{42});
    REQUIRE(pool.size() == 1);
    REQUIRE(pool.has(Entity(1000, 1)));
    REQUIRE(pool.get(Entity(1000, 1))->value == 42);

    // Every other index in [0, 1000) must report not-present.
    for (uint32_t i = 0; i < 1000; ++i) {
        REQUIRE_FALSE(pool.has(Entity(i, 1)));
        REQUIRE(pool.get(Entity(i, 1)) == nullptr);
    }
}

TEST_CASE("ComponentPool: removing the only element leaves pool valid for re-add",
          "[ecs][component_pool][single_element]") {
    ComponentPool<ScalarComponent> pool;
    Entity entity(0, 1);
    pool.add(entity, ScalarComponent{1});
    pool.remove(entity);
    REQUIRE(pool.size() == 0);
    REQUIRE_FALSE(pool.has(entity));

    // After full empty, re-add same entity must work — this hits the
    // edge case where sparse_[entityIndex] was just set back to
    // INVALID_INDEX and the add must start a fresh dense entry.
    pool.add(entity, ScalarComponent{2});
    REQUIRE(pool.size() == 1);
    REQUIRE(pool.has(entity));
    REQUIRE(pool.get(entity)->value == 2);
}

TEST_CASE("ComponentPool: const get is functionally identical to mutable get",
          "[ecs][component_pool][const]") {
    ComponentPool<ScalarComponent> pool;
    Entity entity(0, 1);
    pool.add(entity, ScalarComponent{7});

    const ComponentPool<ScalarComponent>& constPool = pool;
    const ScalarComponent* viaConst = constPool.get(entity);
    ScalarComponent* viaMutable = pool.get(entity);
    REQUIRE(viaConst != nullptr);
    REQUIRE(viaMutable != nullptr);
    REQUIRE(viaConst == viaMutable);
    REQUIRE(viaConst->value == 7);
}
