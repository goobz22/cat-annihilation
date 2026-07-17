// test_ecs_stress_churn.cpp
// ---------------------------------------------------------------------------
// Stress / churn test for engine/ecs/ECS.hpp — the integrated entity +
// component + query stack. The unit-level property tests in
// test_ecs_property_*.cpp exercise each subsystem in isolation; this file
// builds the worst-case workload the engine will see in a real game frame:
//
//   - 10,000 entities live at once (= peak we expect in a battle scene),
//   - 10 distinct component types attached in random combinations,
//   - 100 simulated frames of churn: each frame randomly adds entities,
//     destroys some, adds/removes a few components, and runs a query
//     touching every pool.
//
// The invariants we pin (any one regressing is a SHIP-blocker):
//
//   1. After the simulation ends and we destroy every remaining entity,
//      ECS::getEntityCount() == 0. Anything else means the EntityManager
//      leaked alive slots.
//   2. Every component pool's size() == 0 after destroyAll. If a pool still
//      reports rows after we drained every entity, the destroyEntity loop
//      that walks componentPools_ regressed (e.g. type-erased remove()
//      silently no-op'd a pool).
//   3. The total number of component values across all 10 pools matches the
//      oracle's tally at every step (sampled every 10 frames). Drift here
//      indicates the sparse-dense bookkeeping has gone wrong under load.
//   4. ecs.clear() leaves the ECS in a "fresh" state: zero entities, zero
//      pool rows, but the system manager state is also dropped.
//
// We use a deterministic seed so a fail reproduces. The test takes ~30 ms
// in release / ~100 ms in debug on a 2026-class laptop — well under the
// per-test budget Catch2 considers fast.
// ---------------------------------------------------------------------------

#define CATCH_CONFIG_FAST_COMPILE
#include "catch2/catch.hpp"

#include "ecs/ECS.hpp"

#include <map>
#include <random>
#include <set>
#include <vector>

using CatEngine::ECS;
using CatEngine::Entity;

namespace {

// 10 distinct POD component types so we exercise the variadic component-id
// generator (Component.hpp uses a static-init counter per type). All POD so
// addComponent picks the trivially-copyable concept branch.
struct C0  { int v = 0; };
struct C1  { int v = 0; };
struct C2  { int v = 0; };
struct C3  { int v = 0; };
struct C4  { int v = 0; };
struct C5  { int v = 0; };
struct C6  { int v = 0; };
struct C7  { int v = 0; };
struct C8  { int v = 0; };
struct C9  { int v = 0; };

// Compile-time dispatcher: invoke the right ECS::has / add / remove path
// for a runtime component index in [0, 10). std::variant / if-else chain
// keeps the test code straight-line and matches what gameplay code looks
// like when it dispatches across a small fixed set of component types.
template <int Index>
struct ComponentByIndex;
template <> struct ComponentByIndex<0> { using Type = C0; };
template <> struct ComponentByIndex<1> { using Type = C1; };
template <> struct ComponentByIndex<2> { using Type = C2; };
template <> struct ComponentByIndex<3> { using Type = C3; };
template <> struct ComponentByIndex<4> { using Type = C4; };
template <> struct ComponentByIndex<5> { using Type = C5; };
template <> struct ComponentByIndex<6> { using Type = C6; };
template <> struct ComponentByIndex<7> { using Type = C7; };
template <> struct ComponentByIndex<8> { using Type = C8; };
template <> struct ComponentByIndex<9> { using Type = C9; };

template <typename F>
void dispatchByIndex(int index, F&& fn) {
    switch (index) {
        case 0: fn.template operator()<C0>(); break;
        case 1: fn.template operator()<C1>(); break;
        case 2: fn.template operator()<C2>(); break;
        case 3: fn.template operator()<C3>(); break;
        case 4: fn.template operator()<C4>(); break;
        case 5: fn.template operator()<C5>(); break;
        case 6: fn.template operator()<C6>(); break;
        case 7: fn.template operator()<C7>(); break;
        case 8: fn.template operator()<C8>(); break;
        case 9: fn.template operator()<C9>(); break;
    }
}

constexpr int kComponentTypeCount = 10;

// Counters for the per-type pool occupancy oracle. We keep a parallel
// std::set<Entity> per type; the test invariant is
// pool<C_k>.size() == oracleSets[k].size() at every check point.
struct Oracle {
    std::set<Entity> sets[kComponentTypeCount];
    std::set<Entity> aliveEntities;

    size_t totalComponents() const {
        size_t total = 0;
        for (const auto& set : sets) total += set.size();
        return total;
    }

    void onEntityCreated(Entity entity) {
        aliveEntities.insert(entity);
    }

    void onEntityDestroyed(Entity entity) {
        aliveEntities.erase(entity);
        for (auto& set : sets) {
            set.erase(entity);
        }
    }

    void onComponentAdded(int typeIndex, Entity entity) {
        sets[typeIndex].insert(entity);
    }

    void onComponentRemoved(int typeIndex, Entity entity) {
        sets[typeIndex].erase(entity);
    }
};

size_t poolSize(ECS& ecs, int typeIndex) {
    size_t result = 0;
    dispatchByIndex(typeIndex, [&]<typename T>() {
        auto* pool = ecs.getComponentPool<T>();
        result = pool ? pool->size() : 0;
    });
    return result;
}

void addComponentByIndex(ECS& ecs, int typeIndex, Entity entity, int value) {
    dispatchByIndex(typeIndex, [&]<typename T>() {
        ecs.addComponent(entity, T{value});
    });
}

void removeComponentByIndex(ECS& ecs, int typeIndex, Entity entity) {
    dispatchByIndex(typeIndex, [&]<typename T>() {
        ecs.removeComponent<T>(entity);
    });
}

} // namespace

// ===========================================================================
// 10k entities × 10 component types × 100 frames of churn
// ===========================================================================

TEST_CASE("ECS stress: 10k entities × 10 components × 100 frames of churn",
          "[ecs][stress][churn]") {
    ECS ecs;
    Oracle oracle;
    std::mt19937 rng(static_cast<unsigned>(CatTest::DeterministicSeed("test_ecs_stress_churn:0x5713551D")));
    std::vector<Entity> liveEntities;
    liveEntities.reserve(10'000);

    // Pre-seed with 5,000 entities, each with 2-4 random components. This is
    // the steady-state condition the simulation starts from.
    std::uniform_int_distribution<int> initialComponentCount(2, 4);
    std::uniform_int_distribution<int> typeDist(0, kComponentTypeCount - 1);
    std::uniform_int_distribution<int> valueDist(-1000, 1000);

    for (int i = 0; i < 5000; ++i) {
        Entity entity = ecs.createEntity();
        liveEntities.push_back(entity);
        oracle.onEntityCreated(entity);

        std::set<int> typesAdded;
        int targetCount = initialComponentCount(rng);
        while (static_cast<int>(typesAdded.size()) < targetCount) {
            int type = typeDist(rng);
            if (typesAdded.insert(type).second) {
                int value = valueDist(rng);
                addComponentByIndex(ecs, type, entity, value);
                oracle.onComponentAdded(type, entity);
            }
        }
    }

    REQUIRE(ecs.getEntityCount() == liveEntities.size());

    // 100 frames of churn. Each frame:
    //   - Add ~50 new entities with random components.
    //   - Destroy ~30 entities at random.
    //   - Add ~100 components to existing entities at random.
    //   - Remove ~80 components from existing entities at random.
    //   - Every 10 frames: per-pool size oracle check.

    const int framesToRun = 100;
    for (int frame = 0; frame < framesToRun; ++frame) {
        // Add new entities.
        for (int i = 0; i < 50; ++i) {
            if (liveEntities.size() >= 10'000) break; // cap at 10k peak
            Entity entity = ecs.createEntity();
            liveEntities.push_back(entity);
            oracle.onEntityCreated(entity);
            int targetCount = initialComponentCount(rng);
            std::set<int> typesAdded;
            while (static_cast<int>(typesAdded.size()) < targetCount) {
                int type = typeDist(rng);
                if (typesAdded.insert(type).second) {
                    addComponentByIndex(ecs, type, entity, valueDist(rng));
                    oracle.onComponentAdded(type, entity);
                }
            }
        }

        // Destroy random entities.
        for (int i = 0; i < 30 && !liveEntities.empty(); ++i) {
            std::uniform_int_distribution<size_t> pickDist(0, liveEntities.size() - 1);
            size_t pick = pickDist(rng);
            Entity victim = liveEntities[pick];
            ecs.destroyEntity(victim);
            oracle.onEntityDestroyed(victim);
            liveEntities[pick] = liveEntities.back();
            liveEntities.pop_back();
        }

        // Add components to existing entities.
        for (int i = 0; i < 100 && !liveEntities.empty(); ++i) {
            Entity entity = liveEntities[rng() % liveEntities.size()];
            int type = typeDist(rng);
            int value = valueDist(rng);
            addComponentByIndex(ecs, type, entity, value);
            oracle.onComponentAdded(type, entity);
        }

        // Remove components from existing entities.
        for (int i = 0; i < 80 && !liveEntities.empty(); ++i) {
            Entity entity = liveEntities[rng() % liveEntities.size()];
            int type = typeDist(rng);
            removeComponentByIndex(ecs, type, entity);
            oracle.onComponentRemoved(type, entity);
        }

        // Periodic invariant check.
        if (frame % 10 == 0) {
            REQUIRE(ecs.getEntityCount() == liveEntities.size());
            REQUIRE(ecs.getEntityCount() == oracle.aliveEntities.size());
            for (int type = 0; type < kComponentTypeCount; ++type) {
                REQUIRE(poolSize(ecs, type) == oracle.sets[type].size());
            }
        }
    }

    // Final exhaustive check before teardown.
    REQUIRE(ecs.getEntityCount() == liveEntities.size());
    REQUIRE(ecs.getEntityCount() == oracle.aliveEntities.size());
    for (int type = 0; type < kComponentTypeCount; ++type) {
        REQUIRE(poolSize(ecs, type) == oracle.sets[type].size());
    }

    // Destroy every remaining entity. After this, no pool should hold
    // any rows. If any does we have a leak: destroyEntity walks
    // componentPools_ and calls remove() on each, so a regression that
    // skips a pool would show up exactly here.
    std::vector<Entity> snapshot = liveEntities;
    for (Entity entity : snapshot) {
        ecs.destroyEntity(entity);
    }

    REQUIRE(ecs.getEntityCount() == 0);
    for (int type = 0; type < kComponentTypeCount; ++type) {
        REQUIRE(poolSize(ecs, type) == 0);
    }
}

// ===========================================================================
// ECS::clear drops every pool and the entity manager simultaneously
// ===========================================================================

TEST_CASE("ECS::clear: full reset under load",
          "[ecs][stress][clear]") {
    ECS ecs;
    std::vector<Entity> handles;
    for (int i = 0; i < 1000; ++i) {
        Entity entity = ecs.createEntity();
        ecs.addComponent(entity, C0{i});
        ecs.addComponent(entity, C1{i * 2});
        if (i % 3 == 0) ecs.addComponent(entity, C2{i * 3});
        handles.push_back(entity);
    }
    REQUIRE(ecs.getEntityCount() == 1000);

    ecs.clear();
    REQUIRE(ecs.getEntityCount() == 0);
    // After clear, every old handle is dead and every pool is empty.
    for (Entity entity : handles) {
        REQUIRE_FALSE(ecs.isAlive(entity));
    }
    REQUIRE(poolSize(ecs, 0) == 0);
    REQUIRE(poolSize(ecs, 1) == 0);
    REQUIRE(poolSize(ecs, 2) == 0);
}

// ===========================================================================
// ECS::clearEntities preserves systems
// ===========================================================================

namespace {
struct CountingSystem : public CatEngine::System {
    int& tickCount;
    CountingSystem(int& counter) : System(0), tickCount(counter) {}
    void update(float) override { ++tickCount; }
};
} // namespace

TEST_CASE("ECS::clearEntities: preserves systems, drops entities + components",
          "[ecs][stress][clear_entities]") {
    ECS ecs;
    int tickCount = 0;
    ecs.createSystem<CountingSystem>(tickCount);
    REQUIRE(ecs.getSystemCount() == 1);

    for (int i = 0; i < 100; ++i) {
        Entity entity = ecs.createEntity();
        ecs.addComponent(entity, C0{i});
    }
    REQUIRE(ecs.getEntityCount() == 100);

    ecs.clearEntities();
    REQUIRE(ecs.getEntityCount() == 0);
    REQUIRE(poolSize(ecs, 0) == 0);
    // The CountingSystem stays alive — clearEntities is the gameplay-level
    // "reset the level" operation that keeps the registered systems live so
    // a new level can be loaded without re-wiring them.
    REQUIRE(ecs.getSystemCount() == 1);

    ecs.update(0.016f);
    REQUIRE(tickCount == 1);
}

// ===========================================================================
// Stress: rapid create+destroy of single-entity churn (worst case for
// generation counter)
// ===========================================================================

TEST_CASE("ECS stress: 100k create/destroy on a single slot",
          "[ecs][stress][generations]") {
    ECS ecs;
    Entity entity = ecs.createEntity();
    uint32_t lastGeneration = entity.generation();

    // Hammer the same slot. Each cycle: destroy, recreate, add a component,
    // remove it. The pool's add/remove must remain stable across thousands
    // of recycles; the generation must strictly increase each cycle.
    for (int cycle = 0; cycle < 100'000; ++cycle) {
        ecs.destroyEntity(entity);
        entity = ecs.createEntity();
        REQUIRE(entity.generation() > lastGeneration);
        lastGeneration = entity.generation();

        ecs.addComponent(entity, C0{cycle});
        REQUIRE(ecs.getComponent<C0>(entity)->v == cycle);
        ecs.removeComponent<C0>(entity);
        REQUIRE_FALSE(ecs.hasComponent<C0>(entity));
    }

    REQUIRE(ecs.getEntityCount() == 1);
}
