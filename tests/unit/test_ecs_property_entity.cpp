// test_ecs_property_entity.cpp
// ---------------------------------------------------------------------------
// Property + fuzz tests for engine/ecs/EntityManager + engine/ecs/Entity. The
// existing test_ecs_scene_correctness.cpp pins one generation-recycle case
// and one double-destroy case. This file extends coverage to the full
// generation-counter lifecycle under randomized churn, the bit-packing of
// Entity(index, generation) into a single uint64_t, the freelist FIFO
// ordering, multi-cycle reuse, isAlive() boundary conditions, and the
// out-of-range / NULL_ENTITY paths.
//
// The properties enforced here:
//   1. Entity(index, generation) packs lossless: round-trip through .index()
//      and .generation() preserves both fields exactly across the full 32-bit
//      range of either side.
//   2. NULL_ENTITY.id == 0 and isValid() returns false; every freshly created
//      handle has generation >= 1 and isValid() returns true.
//   3. After a create→destroy→create cycle on the same slot, the OLD handle
//      stays dead (generation moved forward) and the new handle is alive.
//      This must hold for ANY interleaving of N create/destroys, not just the
//      trivial 1-entity case.
//   4. aliveCount_ equals (created - destroyed) at every step, even under
//      random create/destroy patterns with N=1000 entities.
//   5. After clear() the manager looks like a fresh one: aliveCount_==0,
//      getTotalCreated()==0, and the first new handle has index==0,
//      generation==1.
//   6. Handle equality/inequality/ordering operators behave as bitwise
//      operations on the packed id.
//
// If a property fires it indicates either a generation-counter bug (a handle
// surviving destroy) or a packing bug (index bleeding into generation or
// vice versa). Both are silent corruptions that would only surface as "an
// AI is targeting an entity that was already destroyed several frames ago"
// in a real game session — exactly the kind of bug a property test exists
// to catch before it ships.
// ---------------------------------------------------------------------------

#define CATCH_CONFIG_FAST_COMPILE
#include "catch2/catch.hpp"

#include "ecs/Entity.hpp"
#include "ecs/EntityManager.hpp"

#include <cstdint>
#include <random>
#include <set>
#include <unordered_set>
#include <vector>

using CatEngine::Entity;
using CatEngine::EntityManager;
using CatEngine::NULL_ENTITY;

// ===========================================================================
// Entity packing — bit-layout round-trip across the full 32-bit range
// ===========================================================================

TEST_CASE("Entity: NULL_ENTITY has id 0 and is not valid",
          "[ecs][entity][null]") {
    REQUIRE(NULL_ENTITY.id == 0);
    REQUIRE_FALSE(NULL_ENTITY.isValid());
    REQUIRE(NULL_ENTITY.index() == 0u);
    REQUIRE(NULL_ENTITY.generation() == 0u);
}

TEST_CASE("Entity: pack/unpack round-trip for spot values",
          "[ecs][entity][packing]") {
    // Spot values chosen to exercise low/high bits and known edge cases:
    // index=0/gen=1 is the first created handle; index=UINT32_MAX is the
    // upper bound; gen=UINT32_MAX is the upper bound after many recycles.
    struct Case { uint32_t index; uint32_t generation; };
    Case cases[] = {
        {0, 1}, {1, 1}, {7, 3}, {0xFFFF, 0xFFFF},
        {0, UINT32_MAX}, {UINT32_MAX, 1}, {UINT32_MAX, UINT32_MAX},
        {0x12345678u, 0xABCDEF01u}, {1, 0}, {0, 0},
    };
    for (const Case& kase : cases) {
        Entity entity(kase.index, kase.generation);
        REQUIRE(entity.index() == kase.index);
        REQUIRE(entity.generation() == kase.generation);
    }
}

TEST_CASE("Entity: fuzz pack/unpack with random uint32 pairs",
          "[ecs][entity][packing][fuzz]") {
    // 5,000 random (index, generation) pairs. If any pair fails to round-trip
    // through the bit-pack we have an Entity-layout bug — the kind that
    // would corrupt every saved handle in scene files.
    std::mt19937 rng(static_cast<unsigned>(CatTest::DeterministicSeed("test_ecs_property_entity:0xCA7F00D")));
    std::uniform_int_distribution<uint32_t> dist(0, UINT32_MAX);

    for (int iteration = 0; iteration < 5000; ++iteration) {
        uint32_t index = dist(rng);
        uint32_t generation = dist(rng);
        Entity entity(index, generation);
        REQUIRE(entity.index() == index);
        REQUIRE(entity.generation() == generation);

        // Cross-check explicit-id construction stays consistent with the
        // (index, generation) constructor.
        Entity rebuilt(entity.id);
        REQUIRE(rebuilt.index() == index);
        REQUIRE(rebuilt.generation() == generation);
        REQUIRE(rebuilt == entity);
    }
}

TEST_CASE("Entity: equality / inequality / ordering follow packed id bits",
          "[ecs][entity][operators]") {
    Entity entityA(5, 2);
    Entity entityB(5, 2);
    Entity entityC(5, 3);
    Entity entityD(6, 2);

    REQUIRE(entityA == entityB);
    REQUIRE(entityA != entityC);
    REQUIRE(entityA != entityD);

    // Ordering is on the packed uint64. Generation lives in the upper 32 bits
    // so a higher generation sorts AFTER the same index/lower generation,
    // and a higher index sorts after a lower index when generation matches.
    REQUIRE(entityA < entityC);
    REQUIRE(entityA < entityD);
    REQUIRE_FALSE(entityB < entityA);
}

TEST_CASE("Entity: hash specialisation lets handles live in unordered_set",
          "[ecs][entity][hash]") {
    // Without the std::hash<Entity> specialisation, std::unordered_set<Entity>
    // is not even instantiable. Several gameplay systems keep an
    // unordered_set of "entities I am targeting / interested in", so this
    // is a real link-time / template-instantiation guarantee.
    std::unordered_set<Entity> targets;
    targets.insert(Entity(1, 1));
    targets.insert(Entity(2, 1));
    targets.insert(Entity(1, 1)); // duplicate, must not double-insert
    REQUIRE(targets.size() == 2);
    REQUIRE(targets.count(Entity(1, 1)) == 1);
    REQUIRE(targets.count(Entity(2, 1)) == 1);
    REQUIRE(targets.count(Entity(3, 1)) == 0);
}

// ===========================================================================
// EntityManager — single-entity lifecycle invariants
// ===========================================================================

TEST_CASE("EntityManager: fresh manager has zero alive and zero total created",
          "[ecs][entity_manager][lifecycle]") {
    EntityManager manager;
    REQUIRE(manager.getAliveCount() == 0);
    REQUIRE(manager.getTotalCreated() == 0);
    REQUIRE_FALSE(manager.isAlive(NULL_ENTITY));
    REQUIRE_FALSE(manager.isAlive(Entity(0, 1))); // never created
    REQUIRE_FALSE(manager.isAlive(Entity(42, 7)));
}

TEST_CASE("EntityManager: first three handles use slots 0, 1, 2 with gen=1",
          "[ecs][entity_manager][allocation]") {
    EntityManager manager;
    Entity entityA = manager.create();
    Entity entityB = manager.create();
    Entity entityC = manager.create();

    REQUIRE(entityA.index() == 0u);
    REQUIRE(entityB.index() == 1u);
    REQUIRE(entityC.index() == 2u);
    REQUIRE(entityA.generation() == 1u);
    REQUIRE(entityB.generation() == 1u);
    REQUIRE(entityC.generation() == 1u);

    REQUIRE(manager.getAliveCount() == 3);
    REQUIRE(manager.getTotalCreated() == 3);
}

TEST_CASE("EntityManager: out-of-range handles are reported dead, never alive",
          "[ecs][entity_manager][bounds]") {
    EntityManager manager;
    Entity onlyEntity = manager.create();
    REQUIRE(manager.isAlive(onlyEntity));

    // Indices beyond the high-water mark must not access generations_ past
    // its end. A buggy isAlive() that skipped the bounds check would
    // read uninitialised memory and report a spurious alive/dead answer.
    REQUIRE_FALSE(manager.isAlive(Entity(1, 1)));
    REQUIRE_FALSE(manager.isAlive(Entity(100, 1)));
    REQUIRE_FALSE(manager.isAlive(Entity(UINT32_MAX, 1)));

    // An impossible generation on the live slot is also dead.
    REQUIRE_FALSE(manager.isAlive(Entity(0, 2)));
    REQUIRE_FALSE(manager.isAlive(Entity(0, UINT32_MAX)));
    REQUIRE_FALSE(manager.isAlive(Entity(0, 0))); // gen 0 is the NULL marker
}

TEST_CASE("EntityManager: destroy on never-allocated handle is a no-op",
          "[ecs][entity_manager][robustness]") {
    EntityManager manager;
    REQUIRE(manager.getAliveCount() == 0);

    manager.destroy(Entity(100, 1));
    manager.destroy(NULL_ENTITY);
    manager.destroy(Entity(0, 1));

    REQUIRE(manager.getAliveCount() == 0);
    REQUIRE(manager.getTotalCreated() == 0);
}

// ===========================================================================
// EntityManager — recycle / generation forward-march
// ===========================================================================

TEST_CASE("EntityManager: generation strictly increases across N recycle cycles",
          "[ecs][entity_manager][generations][recycle]") {
    EntityManager manager;
    Entity slotZero = manager.create();
    REQUIRE(slotZero.index() == 0u);
    uint32_t lastGen = slotZero.generation();

    // Cycle the SAME slot 100 times. Generation must increase on every
    // destroy → recreate. A regression that resets generation back to 1
    // would let an old handle look alive on the freshly-reused slot.
    for (int cycle = 0; cycle < 100; ++cycle) {
        Entity old = slotZero;
        manager.destroy(slotZero);
        REQUIRE_FALSE(manager.isAlive(old));
        slotZero = manager.create();
        REQUIRE(slotZero.index() == 0u);
        REQUIRE(slotZero.generation() > lastGen);
        lastGen = slotZero.generation();
        REQUIRE(manager.isAlive(slotZero));
        // Every prior handle on this slot stays dead.
        REQUIRE_FALSE(manager.isAlive(old));
    }
}

TEST_CASE("EntityManager: freelist is FIFO across multiple slot recycles",
          "[ecs][entity_manager][freelist][order]") {
    EntityManager manager;
    Entity entityA = manager.create();
    Entity entityB = manager.create();
    Entity entityC = manager.create();
    REQUIRE(entityA.index() == 0u);
    REQUIRE(entityB.index() == 1u);
    REQUIRE(entityC.index() == 2u);

    // Destroy in order A, B, C. Freelist is std::queue (FIFO), so the
    // next three creates must hand back slots 0, 1, 2 in that order.
    manager.destroy(entityA);
    manager.destroy(entityB);
    manager.destroy(entityC);

    Entity reuse0 = manager.create();
    Entity reuse1 = manager.create();
    Entity reuse2 = manager.create();
    REQUIRE(reuse0.index() == 0u);
    REQUIRE(reuse1.index() == 1u);
    REQUIRE(reuse2.index() == 2u);

    // Each reused handle has a generation strictly above the original's.
    REQUIRE(reuse0.generation() > entityA.generation());
    REQUIRE(reuse1.generation() > entityB.generation());
    REQUIRE(reuse2.generation() > entityC.generation());
}

TEST_CASE("EntityManager: clear() resets to like-new state",
          "[ecs][entity_manager][clear]") {
    EntityManager manager;
    Entity a = manager.create();
    Entity b = manager.create();
    manager.destroy(a);
    REQUIRE(manager.getAliveCount() == 1);
    REQUIRE(manager.getTotalCreated() == 2);

    manager.clear();
    REQUIRE(manager.getAliveCount() == 0);
    REQUIRE(manager.getTotalCreated() == 0);

    // After clear, neither pre-existing handle should be alive.
    REQUIRE_FALSE(manager.isAlive(a));
    REQUIRE_FALSE(manager.isAlive(b));

    // First post-clear handle starts at index=0 generation=1 again.
    Entity afterClear = manager.create();
    REQUIRE(afterClear.index() == 0u);
    REQUIRE(afterClear.generation() == 1u);
}

// ===========================================================================
// EntityManager — property test under random create/destroy churn
// ===========================================================================

TEST_CASE("EntityManager property: aliveCount tracks ground truth under churn",
          "[ecs][entity_manager][property][churn]") {
    // Run a randomized create/destroy stream and compare the manager's
    // accounting against a std::set "ground truth" of alive handles after
    // every step. If aliveCount drifts from the set size, or isAlive() ever
    // disagrees with set membership, we have a real correctness bug.
    EntityManager manager;
    std::set<uint64_t> aliveIds;
    std::vector<Entity> aliveHandles;
    std::vector<Entity> historicalHandles; // includes destroyed

    std::mt19937 rng(static_cast<unsigned>(CatTest::DeterministicSeed("test_ecs_property_entity:0xBABE1234")));
    std::bernoulli_distribution createCoin(0.65); // bias toward create

    for (int step = 0; step < 1000; ++step) {
        bool create = aliveHandles.empty() || createCoin(rng);
        if (create) {
            Entity entity = manager.create();
            REQUIRE(aliveIds.insert(entity.id).second); // never duplicate id
            aliveHandles.push_back(entity);
            historicalHandles.push_back(entity);
        } else {
            std::uniform_int_distribution<size_t> pickDist(0, aliveHandles.size() - 1);
            size_t pick = pickDist(rng);
            Entity victim = aliveHandles[pick];
            manager.destroy(victim);
            aliveIds.erase(victim.id);
            aliveHandles[pick] = aliveHandles.back();
            aliveHandles.pop_back();
        }

        REQUIRE(manager.getAliveCount() == aliveIds.size());

        // Every handle still in the ground-truth set must report alive.
        for (Entity alive : aliveHandles) {
            REQUIRE(manager.isAlive(alive));
        }
    }

    // Every historical handle that is NOT in the alive set must be dead. This
    // is the critical property: a destroyed handle MUST NOT come back alive
    // even after the same slot was reused by a different generation.
    for (Entity historical : historicalHandles) {
        bool stillAlive = aliveIds.count(historical.id) > 0;
        if (stillAlive) {
            REQUIRE(manager.isAlive(historical));
        } else {
            REQUIRE_FALSE(manager.isAlive(historical));
        }
    }
}

TEST_CASE("EntityManager property: recycled handle never collides with creator",
          "[ecs][entity_manager][property][collision]") {
    // The cardinal property of generation-counter ECS: when slot N is reused,
    // the new handle's PACKED id must not equal any handle ever issued for
    // slot N before, otherwise the new entity is indistinguishable from one
    // of the destroyed ones and gameplay code that kept a stale pointer
    // would silently retarget itself onto the new entity.
    EntityManager manager;
    std::mt19937 rng(static_cast<unsigned>(CatTest::DeterministicSeed("test_ecs_property_entity:0xDEADC0DE")));

    std::vector<Entity> aliveHandles;
    std::set<uint64_t> everSeen;

    for (int step = 0; step < 500; ++step) {
        if (aliveHandles.empty() || (rng() & 1)) {
            Entity entity = manager.create();
            // No id ever issued by this manager has been seen before.
            REQUIRE(everSeen.insert(entity.id).second);
            aliveHandles.push_back(entity);
        } else {
            size_t pick = rng() % aliveHandles.size();
            Entity victim = aliveHandles[pick];
            manager.destroy(victim);
            aliveHandles[pick] = aliveHandles.back();
            aliveHandles.pop_back();
        }
    }
}

TEST_CASE("EntityManager: aliveCount under interleaved create/destroy bursts",
          "[ecs][entity_manager][bursts]") {
    EntityManager manager;
    // Create 100, then destroy 50, then create 50 — final alive should be
    // 100. The 50 creates after the destroys should each reuse a slot, so
    // total created stays at 100 (no new slots beyond the high-water).
    std::vector<Entity> handles;
    handles.reserve(100);
    for (int i = 0; i < 100; ++i) {
        handles.push_back(manager.create());
    }
    REQUIRE(manager.getAliveCount() == 100);
    REQUIRE(manager.getTotalCreated() == 100);

    for (int i = 0; i < 50; ++i) {
        manager.destroy(handles[i]);
    }
    REQUIRE(manager.getAliveCount() == 50);
    REQUIRE(manager.getTotalCreated() == 100); // slot count unchanged

    std::vector<Entity> recycled;
    for (int i = 0; i < 50; ++i) {
        recycled.push_back(manager.create());
    }
    REQUIRE(manager.getAliveCount() == 100);
    // All 50 recycled handles must have been pulled from the freelist —
    // total created must NOT grow past the high water mark.
    REQUIRE(manager.getTotalCreated() == 100);

    // Every recycled handle is alive and has a different id from every
    // destroyed handle.
    std::set<uint64_t> destroyedIds;
    for (int i = 0; i < 50; ++i) destroyedIds.insert(handles[i].id);
    for (Entity rec : recycled) {
        REQUIRE(manager.isAlive(rec));
        REQUIRE(destroyedIds.count(rec.id) == 0);
    }
    for (int i = 0; i < 50; ++i) {
        REQUIRE_FALSE(manager.isAlive(handles[i])); // destroyed handle stays dead
    }
    for (int i = 50; i < 100; ++i) {
        REQUIRE(manager.isAlive(handles[i])); // untouched handles stay alive
    }
}

TEST_CASE("EntityManager: getTotalCreated counts slots, not allocations",
          "[ecs][entity_manager][accounting]") {
    EntityManager manager;
    // Allocate, then recycle the same slot many times. getTotalCreated()
    // documents itself as "Get total number of entities ever created"
    // but the implementation returns generations_.size() — the slot
    // high-water mark. Pin that behaviour so a future "rename for clarity"
    // refactor doesn't silently change it.
    Entity entity = manager.create();
    REQUIRE(manager.getTotalCreated() == 1);

    for (int i = 0; i < 10; ++i) {
        manager.destroy(entity);
        entity = manager.create();
    }
    REQUIRE(manager.getTotalCreated() == 1); // still one slot ever allocated
}

TEST_CASE("EntityManager: 10k create/destroy stream leaves count consistent",
          "[ecs][entity_manager][stress]") {
    // Cardinal-rule stress: 10k operations, no leaks, no double-counts, no
    // crash. The exact ratio of creates to destroys is randomized so we
    // hit the empty-freelist new-slot path AND the freelist-reuse path
    // in roughly even measure.
    EntityManager manager;
    std::vector<Entity> aliveHandles;
    std::mt19937 rng(static_cast<unsigned>(CatTest::DeterministicSeed("test_ecs_property_entity:0xFEEDF00D")));

    for (int step = 0; step < 10000; ++step) {
        bool create = aliveHandles.empty() || (rng() & 3) != 0; // ~75% create
        if (create) {
            aliveHandles.push_back(manager.create());
        } else {
            size_t pick = rng() % aliveHandles.size();
            manager.destroy(aliveHandles[pick]);
            aliveHandles[pick] = aliveHandles.back();
            aliveHandles.pop_back();
        }
    }

    REQUIRE(manager.getAliveCount() == aliveHandles.size());
    for (Entity entity : aliveHandles) {
        REQUIRE(manager.isAlive(entity));
    }
}

TEST_CASE("EntityManager: handles from independent managers can collide on id",
          "[ecs][entity_manager][isolation]") {
    // Two managers do NOT share state. Each starts at slot 0/gen 1. Their
    // first handles have identical bit patterns BUT each manager only
    // recognises its own. This is the property gameplay code relies on
    // when each Scene owns its own EntityManager (Scene.hpp does exactly
    // this) — entities don't leak across scenes.
    EntityManager managerA;
    EntityManager managerB;
    Entity fromA = managerA.create();
    Entity fromB = managerB.create();
    REQUIRE(fromA.id == fromB.id);
    REQUIRE(managerA.isAlive(fromA));
    REQUIRE(managerB.isAlive(fromB));

    // Destroying in A must not affect B's view of the bit-identical handle.
    managerA.destroy(fromA);
    REQUIRE_FALSE(managerA.isAlive(fromA));
    REQUIRE(managerB.isAlive(fromB));
}
