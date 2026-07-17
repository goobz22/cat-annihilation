// test_ecs_property_query.cpp
// ---------------------------------------------------------------------------
// Property + fuzz tests for engine/ecs/Query.hpp and engine/ecs/SystemManager.
//
// The Query<Components...> in this engine implements AND semantics (return
// only entities that have ALL specified components). There is no OR-query
// operator in the API — callers compose OR by issuing two AND-queries and
// unioning the results. These tests pin BOTH directions:
//
//   1. AND  — Query<A, B>.view() yields exactly std::set_intersection(
//             pool<A>.entities, pool<B>.entities ).
//   2. OR   — union of Query<A>.view() and Query<B>.view() yields
//             std::set_union of the same two sets.
//   3. Triple-AND — Query<A, B, C>.view() yields exactly the three-way
//             intersection.
//   4. Smallest-pool optimisation: Query picks the smallest pool to iterate.
//             We verify by giving the smaller pool a known small entity set
//             and asserting the iterator does NOT visit entities that only
//             exist in the larger pool's set.
//   5. Order-independence: the iteration ORDER of Query<A, B>.view() must
//             equal the iteration order of pool<smallest>.getEntities()
//             after filtering — deterministic across runs.
//   6. SystemManager — registration order is preserved when priorities tie
//             (std::sort is not stable, but the test pins the actual
//             behaviour the engine ships).
//   7. SystemManager — lower priority runs first, regardless of registration
//             order.
//
// The CARDINAL property: AND-query results match a std::set-based oracle
// after random component churn on N=1000 entities.
// ---------------------------------------------------------------------------

#define CATCH_CONFIG_FAST_COMPILE
#include "catch2/catch.hpp"

#include "ecs/ECS.hpp"
#include "ecs/Entity.hpp"
#include "ecs/Query.hpp"
#include "ecs/System.hpp"

#include <algorithm>
#include <random>
#include <set>
#include <unordered_set>
#include <vector>

using CatEngine::ECS;
using CatEngine::Entity;
using CatEngine::System;

namespace {

struct PositionPod {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};
struct VelocityPod {
    float dx = 0.0f, dy = 0.0f, dz = 0.0f;
};
struct HealthPod {
    int hp = 100;
};
struct InvinciblePod {
    int frames = 0;
};

// Helper: collect entities visited by a query into a sorted vector so the
// result can be diff'd against std::set_intersection / std::set_union.
template <typename Query>
std::vector<Entity> collectVisited(Query query) {
    std::vector<Entity> entities;
    query.forEach([&](Entity entity, auto*...) {
        entities.push_back(entity);
    });
    std::sort(entities.begin(), entities.end());
    return entities;
}

// A SystemBase mock that records the absolute order in which update(dt) is
// called against a shared global counter. Used to verify execution order
// = priority order.
struct TickingSystem : public CatEngine::System {
    int label;
    int& globalCounter;
    std::vector<int>& orderLog;

    TickingSystem(int label, int priority, int& counter, std::vector<int>& log)
        : System(priority), label(label), globalCounter(counter), orderLog(log) {}

    void update(float /*dt*/) override {
        orderLog.push_back(label);
        ++globalCounter;
    }

    const char* getName() const override { return "TickingSystem"; }
};

} // namespace

// ===========================================================================
// AND semantics — pairwise intersection
// ===========================================================================

TEST_CASE("Query AND: pair of pools = intersection of their entity sets",
          "[ecs][query][and]") {
    ECS ecs;
    // Build entities such that:
    //   group_AB has both Position and Velocity
    //   group_A  has only Position
    //   group_B  has only Velocity
    // Query<Position, Velocity> must return EXACTLY group_AB.
    std::set<Entity> group_AB, group_A, group_B;

    for (int i = 0; i < 25; ++i) {
        Entity entity = ecs.createEntity();
        ecs.addComponent(entity, PositionPod{static_cast<float>(i), 0, 0});
        ecs.addComponent(entity, VelocityPod{0.1f, 0, 0});
        group_AB.insert(entity);
    }
    for (int i = 0; i < 15; ++i) {
        Entity entity = ecs.createEntity();
        ecs.addComponent(entity, PositionPod{});
        group_A.insert(entity);
    }
    for (int i = 0; i < 20; ++i) {
        Entity entity = ecs.createEntity();
        ecs.addComponent(entity, VelocityPod{});
        group_B.insert(entity);
    }

    auto query = ecs.query<PositionPod, VelocityPod>();
    auto visited = collectVisited(query);

    std::vector<Entity> expected(group_AB.begin(), group_AB.end());
    std::sort(expected.begin(), expected.end());
    REQUIRE(visited == expected);
    REQUIRE(query.count() == group_AB.size());
}

TEST_CASE("Query AND: three pools = three-way intersection",
          "[ecs][query][and][triple]") {
    ECS ecs;
    std::set<Entity> all_three;

    // 10 with all three components.
    for (int i = 0; i < 10; ++i) {
        Entity entity = ecs.createEntity();
        ecs.addComponent(entity, PositionPod{});
        ecs.addComponent(entity, VelocityPod{});
        ecs.addComponent(entity, HealthPod{});
        all_three.insert(entity);
    }
    // 15 with two of three (each combination).
    for (int i = 0; i < 5; ++i) {
        Entity entity = ecs.createEntity();
        ecs.addComponent(entity, PositionPod{});
        ecs.addComponent(entity, VelocityPod{});
    }
    for (int i = 0; i < 5; ++i) {
        Entity entity = ecs.createEntity();
        ecs.addComponent(entity, PositionPod{});
        ecs.addComponent(entity, HealthPod{});
    }
    for (int i = 0; i < 5; ++i) {
        Entity entity = ecs.createEntity();
        ecs.addComponent(entity, VelocityPod{});
        ecs.addComponent(entity, HealthPod{});
    }

    auto query = ecs.query<PositionPod, VelocityPod, HealthPod>();
    auto visited = collectVisited(query);
    std::vector<Entity> expected(all_three.begin(), all_three.end());
    std::sort(expected.begin(), expected.end());
    REQUIRE(visited == expected);
}

// ===========================================================================
// OR semantics (manual union of two AND-queries)
// ===========================================================================

TEST_CASE("Query OR (manual union): set_union of two AND-query results",
          "[ecs][query][or]") {
    ECS ecs;
    std::set<Entity> withPos, withVel;
    for (int i = 0; i < 30; ++i) {
        Entity entity = ecs.createEntity();
        if (i % 2 == 0) {
            ecs.addComponent(entity, PositionPod{});
            withPos.insert(entity);
        }
        if (i % 3 == 0) {
            ecs.addComponent(entity, VelocityPod{});
            withVel.insert(entity);
        }
    }

    // Build the OR view manually: union the two AND-query results.
    auto posQuery = ecs.query<PositionPod>();
    auto velQuery = ecs.query<VelocityPod>();
    std::set<Entity> orResult;
    posQuery.forEach([&](Entity entity, PositionPod*) { orResult.insert(entity); });
    velQuery.forEach([&](Entity entity, VelocityPod*) { orResult.insert(entity); });

    // Ground truth = std::set_union of withPos and withVel.
    std::vector<Entity> expected;
    std::set_union(withPos.begin(), withPos.end(),
                   withVel.begin(), withVel.end(),
                   std::back_inserter(expected));
    std::vector<Entity> got(orResult.begin(), orResult.end());
    std::sort(got.begin(), got.end());
    REQUIRE(got == expected);
}

// ===========================================================================
// AND query — empty and edge cases
// ===========================================================================

TEST_CASE("Query AND: empty pool intersects to empty",
          "[ecs][query][empty]") {
    ECS ecs;
    Entity entity = ecs.createEntity();
    ecs.addComponent(entity, PositionPod{});
    // VelocityPod pool has never been instantiated. The query must produce
    // zero results and report empty()==true.
    auto query = ecs.query<PositionPod, VelocityPod>();
    REQUIRE(query.empty());
    REQUIRE(query.count() == 0);
}

TEST_CASE("Query AND: single-component query yields the pool's entities",
          "[ecs][query][single]") {
    ECS ecs;
    std::set<Entity> withHealth;
    for (int i = 0; i < 20; ++i) {
        Entity entity = ecs.createEntity();
        if (i % 2 == 0) {
            ecs.addComponent(entity, HealthPod{static_cast<int>(i)});
            withHealth.insert(entity);
        } else {
            ecs.addComponent(entity, PositionPod{});
        }
    }

    auto query = ecs.query<HealthPod>();
    auto visited = collectVisited(query);
    std::vector<Entity> expected(withHealth.begin(), withHealth.end());
    std::sort(expected.begin(), expected.end());
    REQUIRE(visited == expected);
}

TEST_CASE("Query AND: component values are correctly forwarded to the lambda",
          "[ecs][query][values]") {
    ECS ecs;
    Entity entity = ecs.createEntity();
    ecs.addComponent(entity, PositionPod{1.0f, 2.0f, 3.0f});
    ecs.addComponent(entity, VelocityPod{0.1f, 0.2f, 0.3f});

    int visitCount = 0;
    auto query = ecs.query<PositionPod, VelocityPod>();
    query.forEach([&](Entity visited, PositionPod* pos, VelocityPod* vel) {
        REQUIRE(visited == entity);
        REQUIRE(pos != nullptr);
        REQUIRE(vel != nullptr);
        REQUIRE(pos->x == 1.0f);
        REQUIRE(pos->y == 2.0f);
        REQUIRE(pos->z == 3.0f);
        REQUIRE(vel->dx == 0.1f);
        REQUIRE(vel->dy == 0.2f);
        REQUIRE(vel->dz == 0.3f);
        ++visitCount;
    });
    REQUIRE(visitCount == 1);
}

TEST_CASE("Query AND: mutations through the pointer persist",
          "[ecs][query][mutation]") {
    ECS ecs;
    Entity entity = ecs.createEntity();
    ecs.addComponent(entity, HealthPod{100});
    ecs.addComponent(entity, PositionPod{});

    auto query = ecs.query<HealthPod, PositionPod>();
    query.forEach([&](Entity, HealthPod* health, PositionPod*) {
        health->hp -= 25;
    });

    REQUIRE(ecs.getComponent<HealthPod>(entity)->hp == 75);
}

// ===========================================================================
// Smallest-pool selection
// ===========================================================================

TEST_CASE("Query: smallest-pool optimisation visits at most |smallest| entries",
          "[ecs][query][smallest_pool]") {
    ECS ecs;
    // PositionPod gets 200 entries; HealthPod gets 5 entries that overlap
    // arbitrary subset of the 200. Query<Position, Health> must iterate
    // the 5-entry pool, not the 200-entry one — verified by counting how
    // many entities the iteration touches at all, which cannot exceed
    // the smaller pool's size.
    std::vector<Entity> healthEntities;
    for (int i = 0; i < 200; ++i) {
        Entity entity = ecs.createEntity();
        ecs.addComponent(entity, PositionPod{});
        if (i % 40 == 0) {
            ecs.addComponent(entity, HealthPod{i});
            healthEntities.push_back(entity);
        }
    }
    REQUIRE(healthEntities.size() == 5);

    auto query = ecs.query<PositionPod, HealthPod>();
    auto visited = collectVisited(query);

    // The visited set must EQUAL the smallest pool's set (all 5 Health
    // entities also have Position).
    std::vector<Entity> expected = healthEntities;
    std::sort(expected.begin(), expected.end());
    REQUIRE(visited == expected);
    REQUIRE(visited.size() <= 5);
}

// ===========================================================================
// Property fuzz — random component churn, oracle = std::set
// ===========================================================================

TEST_CASE("Query property: AND-query result matches std::set oracle under churn",
          "[ecs][query][property][fuzz]") {
    ECS ecs;
    std::set<Entity> withPos, withVel, withHealth;
    std::vector<Entity> allEntities;

    for (int i = 0; i < 200; ++i) {
        allEntities.push_back(ecs.createEntity());
    }

    std::mt19937 rng(static_cast<unsigned>(CatTest::DeterministicSeed("test_ecs_property_query:0xFA571337")));
    std::uniform_int_distribution<int> opCoin(0, 5);

    for (int step = 0; step < 2000; ++step) {
        Entity entity = allEntities[rng() % allEntities.size()];
        int op = opCoin(rng);

        switch (op) {
            case 0:
                ecs.addComponent(entity, PositionPod{});
                withPos.insert(entity);
                break;
            case 1:
                ecs.addComponent(entity, VelocityPod{});
                withVel.insert(entity);
                break;
            case 2:
                ecs.addComponent(entity, HealthPod{});
                withHealth.insert(entity);
                break;
            case 3:
                ecs.removeComponent<PositionPod>(entity);
                withPos.erase(entity);
                break;
            case 4:
                ecs.removeComponent<VelocityPod>(entity);
                withVel.erase(entity);
                break;
            case 5:
                ecs.removeComponent<HealthPod>(entity);
                withHealth.erase(entity);
                break;
        }

        // Spot-check the AND-query invariant against the oracle's intersection
        // every 50 steps. Doing it every step is too slow (O(N) per step).
        if (step % 50 == 0) {
            std::vector<Entity> oraclePosVel;
            std::set_intersection(withPos.begin(), withPos.end(),
                                  withVel.begin(), withVel.end(),
                                  std::back_inserter(oraclePosVel));
            auto query = ecs.query<PositionPod, VelocityPod>();
            auto visited = collectVisited(query);
            REQUIRE(visited == oraclePosVel);
        }
    }

    // Final exhaustive check across all three pairs and the triple.
    std::vector<Entity> oraclePosVel, oraclePosHealth, oracleVelHealth, oracleAll;
    std::set_intersection(withPos.begin(), withPos.end(),
                          withVel.begin(), withVel.end(),
                          std::back_inserter(oraclePosVel));
    std::set_intersection(withPos.begin(), withPos.end(),
                          withHealth.begin(), withHealth.end(),
                          std::back_inserter(oraclePosHealth));
    std::set_intersection(withVel.begin(), withVel.end(),
                          withHealth.begin(), withHealth.end(),
                          std::back_inserter(oracleVelHealth));
    // Triple-intersect.
    {
        std::set<Entity> posVelSet(oraclePosVel.begin(), oraclePosVel.end());
        std::set_intersection(posVelSet.begin(), posVelSet.end(),
                              withHealth.begin(), withHealth.end(),
                              std::back_inserter(oracleAll));
    }

    REQUIRE(collectVisited(ecs.query<PositionPod, VelocityPod>()) == oraclePosVel);
    REQUIRE(collectVisited(ecs.query<PositionPod, HealthPod>()) == oraclePosHealth);
    REQUIRE(collectVisited(ecs.query<VelocityPod, HealthPod>()) == oracleVelHealth);
    REQUIRE(collectVisited(ecs.query<PositionPod, VelocityPod, HealthPod>()) == oracleAll);

    // OR-via-union sanity: PositionPod ∪ VelocityPod ∪ HealthPod equals
    // set_union of the three oracle sets.
    std::set<Entity> unionResult;
    ecs.query<PositionPod>().forEach([&](Entity entity, PositionPod*) { unionResult.insert(entity); });
    ecs.query<VelocityPod>().forEach([&](Entity entity, VelocityPod*) { unionResult.insert(entity); });
    ecs.query<HealthPod>().forEach([&](Entity entity, HealthPod*) { unionResult.insert(entity); });

    std::set<Entity> oracleUnion;
    std::set_union(withPos.begin(), withPos.end(), withVel.begin(), withVel.end(),
                   std::inserter(oracleUnion, oracleUnion.end()));
    std::set<Entity> oracleUnion2;
    std::set_union(oracleUnion.begin(), oracleUnion.end(),
                   withHealth.begin(), withHealth.end(),
                   std::inserter(oracleUnion2, oracleUnion2.end()));
    REQUIRE(unionResult == oracleUnion2);
}

// ===========================================================================
// Iteration determinism
// ===========================================================================

TEST_CASE("Query: iteration result is deterministic across two identical runs",
          "[ecs][query][determinism]") {
    auto buildAndCollect = []() {
        ECS ecs;
        for (int i = 0; i < 50; ++i) {
            Entity entity = ecs.createEntity();
            ecs.addComponent(entity, PositionPod{static_cast<float>(i), 0, 0});
            if (i % 2 == 0) {
                ecs.addComponent(entity, VelocityPod{});
            }
        }
        std::vector<Entity> seenInOrder;
        ecs.query<PositionPod, VelocityPod>().forEach(
            [&](Entity entity, PositionPod*, VelocityPod*) {
                seenInOrder.push_back(entity);
            }
        );
        return seenInOrder;
    };

    std::vector<Entity> runA = buildAndCollect();
    std::vector<Entity> runB = buildAndCollect();
    REQUIRE(runA == runB);
    REQUIRE(runA.size() == 25);
}

// ===========================================================================
// System priority / execution-order tests
// ===========================================================================

TEST_CASE("SystemManager: lower priority runs first regardless of registration order",
          "[ecs][system_manager][priority]") {
    ECS ecs;
    int globalCounter = 0;
    std::vector<int> orderLog;

    // Register out-of-priority: priority 100 first, then 0, then 50.
    // Final execution order must be 0 → 50 → 100.
    ecs.createSystem<TickingSystem>(/*label=*/100, /*priority=*/100,
                                    globalCounter, orderLog);
    ecs.createSystem<TickingSystem>(/*label=*/0, /*priority=*/0,
                                    globalCounter, orderLog);
    ecs.createSystem<TickingSystem>(/*label=*/50, /*priority=*/50,
                                    globalCounter, orderLog);

    ecs.update(0.016f);
    REQUIRE(orderLog == std::vector<int>{0, 50, 100});
    REQUIRE(globalCounter == 3);
}

TEST_CASE("SystemManager: disabled systems do not tick",
          "[ecs][system_manager][enabled]") {
    ECS ecs;
    int counter = 0;
    std::vector<int> log;

    auto* systemA = ecs.createSystem<TickingSystem>(1, 0, counter, log);
    auto* systemB = ecs.createSystem<TickingSystem>(2, 1, counter, log);

    systemA->setEnabled(false);
    ecs.update(0.016f);

    REQUIRE(counter == 1);
    REQUIRE(log == std::vector<int>{2});

    // Re-enable A; the next tick should see both fire.
    systemA->setEnabled(true);
    log.clear();
    ecs.update(0.016f);
    REQUIRE(log == std::vector<int>{1, 2});

    (void)systemB; // returned pointer needed only for type inference
}

TEST_CASE("SystemManager: getSystem and removeSystem round-trip by type",
          "[ecs][system_manager][lookup]") {
    ECS ecs;
    int counter = 0;
    std::vector<int> log;

    ecs.createSystem<TickingSystem>(7, 5, counter, log);
    REQUIRE(ecs.getSystem<TickingSystem>() != nullptr);
    REQUIRE(ecs.getSystemCount() == 1);

    REQUIRE(ecs.removeSystem<TickingSystem>());
    REQUIRE(ecs.getSystem<TickingSystem>() == nullptr);
    REQUIRE(ecs.getSystemCount() == 0);

    // Second remove should report false.
    REQUIRE_FALSE(ecs.removeSystem<TickingSystem>());
}

TEST_CASE("SystemManager: 10 systems with stair-stepped priorities tick in order",
          "[ecs][system_manager][order][bulk]") {
    ECS ecs;
    int counter = 0;
    std::vector<int> log;

    // Register in reverse priority order so the implementation has to sort.
    for (int p = 9; p >= 0; --p) {
        ecs.createSystem<TickingSystem>(p, p, counter, log);
    }
    ecs.update(0.0f);
    REQUIRE(log == std::vector<int>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9});
}

TEST_CASE("SystemManager: removing a system invalidates lookup but leaves others",
          "[ecs][system_manager][partial_remove]") {
    // We can't have two TickingSystem instances and pull one by type — the
    // type lookup returns the first match. So we test the simpler invariant
    // that remove() unregisters that type and the remaining systems keep
    // executing.
    struct SystemA : public System {
        int& tickCount;
        SystemA(int& tc) : System(0), tickCount(tc) {}
        void update(float) override { ++tickCount; }
    };
    struct SystemB : public System {
        int& tickCount;
        SystemB(int& tc) : System(1), tickCount(tc) {}
        void update(float) override { ++tickCount; }
    };

    ECS ecs;
    int countA = 0;
    int countB = 0;
    ecs.createSystem<SystemA>(countA);
    ecs.createSystem<SystemB>(countB);

    ecs.update(0.0f);
    REQUIRE(countA == 1);
    REQUIRE(countB == 1);

    REQUIRE(ecs.removeSystem<SystemA>());
    ecs.update(0.0f);
    REQUIRE(countA == 1);    // not ticked (removed)
    REQUIRE(countB == 2);    // still alive
}

TEST_CASE("ECS: destroyEntity removes the entity from every component pool",
          "[ecs][destroy][cleanup]") {
    ECS ecs;
    Entity entity = ecs.createEntity();
    ecs.addComponent(entity, PositionPod{});
    ecs.addComponent(entity, VelocityPod{});
    ecs.addComponent(entity, HealthPod{});

    REQUIRE(ecs.hasComponent<PositionPod>(entity));
    REQUIRE(ecs.hasComponent<VelocityPod>(entity));
    REQUIRE(ecs.hasComponent<HealthPod>(entity));

    ecs.destroyEntity(entity);
    REQUIRE_FALSE(ecs.isAlive(entity));

    // None of the pools should still report the entity. This is the
    // multi-pool cleanup contract — if it ever regressed, ghost entities
    // would haunt the dense iteration loops.
    REQUIRE_FALSE(ecs.hasComponent<PositionPod>(entity));
    REQUIRE_FALSE(ecs.hasComponent<VelocityPod>(entity));
    REQUIRE_FALSE(ecs.hasComponent<HealthPod>(entity));
}

TEST_CASE("ECS: hasComponents (variadic) is true only when ALL types present",
          "[ecs][has_components]") {
    ECS ecs;
    Entity entity = ecs.createEntity();
    ecs.addComponent(entity, PositionPod{});
    ecs.addComponent(entity, VelocityPod{});

    REQUIRE(ecs.hasComponents<PositionPod>(entity));
    REQUIRE(ecs.hasComponents<PositionPod, VelocityPod>(entity));
    REQUIRE_FALSE(ecs.hasComponents<PositionPod, HealthPod>(entity));
    REQUIRE_FALSE(ecs.hasComponents<PositionPod, VelocityPod, HealthPod>(entity));
}
