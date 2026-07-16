// test_ecs_scene_correctness.cpp
// ---------------------------------------------------------------------------
// Unit tests for engine/ecs/{Entity,EntityManager,ComponentPool,Query}.hpp
// and the JSON serialization primitives in engine/scene/SceneSerializer.hpp.
//
// WHY this suite exists:
//   The ECS and the scene serializer had ZERO test coverage despite being the
//   load-bearing backbone of every gameplay system (every Entity handle, every
//   component lookup, every save/load cycle goes through them). The audit
//   that produced this file uncovered four correctness bugs that would only
//   surface in subtle, hours-to-bisect ways:
//
//     1. JsonValue::toString output floats with std::fixed << setprecision(6),
//        which truncated transform components like vec3(0.1234567f, ...) to
//        0.123457 on save. After two reload cycles the cumulative drift was
//        already visible in shadow-bias gradients. The fix emits %.17g for
//        non-integer doubles (the canonical "round-trip a double" specifier),
//        and integer-fast-paths IDs so the diff stays compact. Tests pin a
//        bit-exact round-trip on a hostile float (M_PI as a float, denormals,
//        large magnitudes) and assert no precision is lost.
//
//     2. JsonValue::toString did not escape strings, so any SceneNode name
//        containing a quote, backslash, or control char produced
//        non-parseable JSON: `"My"Scene"` is two adjacent strings followed by
//        garbage. The fix is a standard RFC 8259 §7 escape pass. The test
//        round-trips a hostile string through parse/toString and asserts the
//        bytes survive (and that the intermediate JSON re-parses).
//
//     3. JsonValue object keys came out of std::unordered_map in
//        implementation-defined order, so two semantically-identical saves
//        produced byte-different files. That breaks any diff-driven build
//        cache (asset hashing, golden-file CI) and was the kind of bug that
//        looks like "the build is reproducible most of the time". The fix
//        emits keys via a std::map<string,...> view at format-time. The
//        test asserts strictly-ascending key order in the output.
//
//     4. EntityManager::create() correctly bumps generation on destroy, but
//        the contract had no test pinning that a recycled-index handle from
//        before a destroy reports isAlive() == false in the new ECS. Without
//        the test, a regression like "skip the ++generations_[index]" would
//        ship green. The test creates → destroys → recreates and asserts
//        the old handle is dead while the new handle (same index, new
//        generation) is alive.
//
// All four bugs are tiny but each one corrupts data silently. Test coverage
// is cheap; the cost-of-discovery would be measured in re-runs of overnight
// integration suites.
// ---------------------------------------------------------------------------

#define CATCH_CONFIG_FAST_COMPILE
#include "catch2/catch.hpp"

#include "ecs/Entity.hpp"
#include "ecs/EntityManager.hpp"
#include "ecs/ECS.hpp"
#include "ecs/Query.hpp"

#include "scene/SceneSerializer.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

namespace {

// Helper: simple POD components for ECS tests. They satisfy
// std::is_trivially_copyable, which makes them valid Components for the
// concept in Component.hpp.
struct PositionPod {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct VelocityPod {
    float dx = 0.0f;
    float dy = 0.0f;
    float dz = 0.0f;
};

struct TagPod {
    int kind = 0;
};

} // namespace

// ===========================================================================
// EntityManager - generation counter / handle reuse
// ===========================================================================

TEST_CASE("EntityManager: destroyed handle is not alive after recycle",
          "[ecs][entity_manager][generations]") {
    CatEngine::EntityManager manager;

    CatEngine::Entity firstHandle = manager.create();
    REQUIRE(manager.isAlive(firstHandle));
    REQUIRE(firstHandle.index() == 0);
    REQUIRE(firstHandle.generation() == 1);

    manager.destroy(firstHandle);
    REQUIRE_FALSE(manager.isAlive(firstHandle));

    // Recycle the slot. The new handle must reuse index 0 but bump
    // generation, otherwise the dangling firstHandle would falsely
    // report alive and resolve to the new entity's component data.
    CatEngine::Entity recycled = manager.create();
    REQUIRE(recycled.index() == 0);
    REQUIRE(recycled.generation() == 2);

    // This is the regression we care about: the OLD handle must remain
    // dead even though its slot is now in use by a different entity.
    REQUIRE_FALSE(manager.isAlive(firstHandle));
    REQUIRE(manager.isAlive(recycled));
    REQUIRE(firstHandle != recycled);
}

TEST_CASE("EntityManager: double-destroy is a no-op",
          "[ecs][entity_manager][robustness]") {
    CatEngine::EntityManager manager;
    CatEngine::Entity entity = manager.create();
    REQUIRE(manager.getAliveCount() == 1);

    manager.destroy(entity);
    REQUIRE(manager.getAliveCount() == 0);

    // Calling destroy on an already-dead handle must not double-decrement
    // aliveCount_ or double-push the slot into the freelist (which would
    // hand out the same index twice on the next two creates — that is the
    // textbook "danger of stale handle reuse" bug).
    manager.destroy(entity);
    REQUIRE(manager.getAliveCount() == 0);

    CatEngine::Entity firstReuse = manager.create();
    CatEngine::Entity secondReuse = manager.create();
    REQUIRE(firstReuse.index() != secondReuse.index());
}

// ===========================================================================
// ComponentPool - sparse/dense swap-on-remove (including tail removal)
// ===========================================================================

TEST_CASE("ComponentPool: tail removal leaves remaining entities intact",
          "[ecs][component_pool][remove]") {
    CatEngine::ComponentPool<PositionPod> pool;
    CatEngine::Entity entityA(0, 1);
    CatEngine::Entity entityB(1, 1);
    CatEngine::Entity entityC(2, 1);

    pool.add(entityA, PositionPod{1.0f, 0.0f, 0.0f});
    pool.add(entityB, PositionPod{2.0f, 0.0f, 0.0f});
    pool.add(entityC, PositionPod{3.0f, 0.0f, 0.0f});
    REQUIRE(pool.size() == 3);

    // Remove the TAIL (entityC). This is the swap-and-pop branch where the
    // swap is skipped because denseIndex == lastIndex. A regression here
    // (e.g. forgetting to set sparse_[entityC] = INVALID after pop_back, or
    // mistakenly swapping with self) would corrupt entityA/B's sparse
    // lookups.
    pool.remove(entityC);
    REQUIRE(pool.size() == 2);
    REQUIRE_FALSE(pool.has(entityC));
    REQUIRE(pool.has(entityA));
    REQUIRE(pool.has(entityB));
    REQUIRE(pool.get(entityA)->x == 1.0f);
    REQUIRE(pool.get(entityB)->x == 2.0f);
}

TEST_CASE("ComponentPool: middle removal swaps tail into the hole",
          "[ecs][component_pool][remove][swap]") {
    CatEngine::ComponentPool<PositionPod> pool;
    CatEngine::Entity entityA(0, 1);
    CatEngine::Entity entityB(1, 1);
    CatEngine::Entity entityC(2, 1);

    pool.add(entityA, PositionPod{1.0f, 0.0f, 0.0f});
    pool.add(entityB, PositionPod{2.0f, 0.0f, 0.0f});
    pool.add(entityC, PositionPod{3.0f, 0.0f, 0.0f});

    // Remove the MIDDLE element (entityB). The swap path moves entityC's
    // data into entityB's slot AND has to update sparse_[entityC] to
    // point at the new dense index. A regression that forgets the sparse
    // update would make has(entityC) return false-positive against the
    // wrong dense slot.
    pool.remove(entityB);
    REQUIRE(pool.size() == 2);
    REQUIRE_FALSE(pool.has(entityB));
    REQUIRE(pool.has(entityA));
    REQUIRE(pool.has(entityC));
    REQUIRE(pool.get(entityA)->x == 1.0f);
    REQUIRE(pool.get(entityC)->x == 3.0f);
}

// ===========================================================================
// Query - AND semantics across multiple component pools
// ===========================================================================

TEST_CASE("ECS query: only entities with ALL components are visited",
          "[ecs][query][and_semantics]") {
    CatEngine::ECS ecs;

    CatEngine::Entity movingEntity = ecs.createEntity();
    ecs.addComponent(movingEntity, PositionPod{1.0f, 2.0f, 3.0f});
    ecs.addComponent(movingEntity, VelocityPod{0.1f, 0.0f, 0.0f});

    CatEngine::Entity staticEntity = ecs.createEntity();
    ecs.addComponent(staticEntity, PositionPod{4.0f, 5.0f, 6.0f});
    // No velocity — must NOT appear in the Position+Velocity query.

    CatEngine::Entity disembodiedVelocity = ecs.createEntity();
    ecs.addComponent(disembodiedVelocity, VelocityPod{0.2f, 0.0f, 0.0f});
    // No position — must NOT appear either.

    auto query = ecs.query<PositionPod, VelocityPod>();
    size_t visited = 0;
    CatEngine::Entity visitedEntity;
    query.forEach([&](CatEngine::Entity entity, PositionPod*, VelocityPod*) {
        ++visited;
        visitedEntity = entity;
    });

    REQUIRE(visited == 1);
    REQUIRE(visitedEntity == movingEntity);
}

TEST_CASE("ECS query: empty pool yields zero results",
          "[ecs][query][empty]") {
    CatEngine::ECS ecs;
    CatEngine::Entity entityWithPos = ecs.createEntity();
    ecs.addComponent(entityWithPos, PositionPod{0.0f, 0.0f, 0.0f});

    // Query that intersects an existing pool with a never-instantiated one
    // must return zero — the AND of any set with the empty set is empty.
    auto query = ecs.query<PositionPod, TagPod>();
    size_t visited = 0;
    query.forEach([&](CatEngine::Entity, PositionPod*, TagPod*) { ++visited; });
    REQUIRE(visited == 0);
    REQUIRE(query.empty());
}

// ===========================================================================
// JsonValue - escaping, precision, deterministic ordering
// ===========================================================================

TEST_CASE("JsonValue: strings are RFC-8259 escaped on output",
          "[scene][serializer][json][escape]") {
    // A hostile string covering every metacharacter the parser already
    // unescapes on the way in. Without the matching escape on the way out
    // the round-trip produces invalid JSON and the parser silently chokes.
    const std::string hostile = "name=\"alpha\"\nwith\\back\tand\rcr";

    CatEngine::JsonValue value(hostile);
    std::string serialized = value.toString();

    // The bare quote in the input must NOT appear unescaped in the body;
    // it must be \" instead. Conversely, the outer wrapping quotes are
    // the only literal '"' characters allowed in the output.
    REQUIRE(serialized.front() == '"');
    REQUIRE(serialized.back() == '"');
    REQUIRE(serialized.find("\\\"") != std::string::npos);
    REQUIRE(serialized.find("\\\\") != std::string::npos);
    REQUIRE(serialized.find("\\n") != std::string::npos);
    REQUIRE(serialized.find("\\t") != std::string::npos);
    REQUIRE(serialized.find("\\r") != std::string::npos);

    // Round-trip via the parser proves the escaped form is valid JSON.
    // Wrap in an object so we exercise the same code path real scene
    // files use (parseValue dispatches on the leading '"').
    CatEngine::JsonValue envelope = CatEngine::JsonValue::object();
    envelope["k"] = value;
    std::string envelopeJson = envelope.toString();
    CatEngine::JsonValue parsed = CatEngine::JsonValue::parse(envelopeJson);
    REQUIRE(parsed.has("k"));
    REQUIRE(parsed["k"].asString() == hostile);
}

TEST_CASE("JsonValue: float round-trip preserves precision",
          "[scene][serializer][json][precision]") {
    // pi-as-a-float is the canonical "fits in float, doesn't fit in 6 digits
    // of fixed precision" value. With the old setprecision(6) + fixed it
    // round-tripped to 3.141593 → 3.141593f, losing ~4.6e-8.
    const float piFloat = 3.14159265358979f;
    CatEngine::JsonValue piValue(piFloat);
    std::string piString = piValue.toString();
    double parsed = std::stod(piString);
    // Compare in float space — the contract we care about is "save+load a
    // float and get the SAME float back", not full double precision.
    REQUIRE(static_cast<float>(parsed) == piFloat);

    // Very small denormal-ish positive value must not be flushed to 0
    // by the integer fast-path or by formatting truncation.
    const float tinyFloat = 1.0e-30f;
    CatEngine::JsonValue tinyValue(tinyFloat);
    std::string tinyString = tinyValue.toString();
    REQUIRE(static_cast<float>(std::stod(tinyString)) == tinyFloat);

    // Integer values must serialize without a trailing ".0" so entity IDs
    // and counts stay diff-friendly. A double value of 8589934593.0
    // (= (gen=2 << 32) | index=1) must come out as "8589934593", not
    // "8589934593.0" and certainly not "8.58993e+09".
    const double largeIntegerDouble = 8589934593.0;
    CatEngine::JsonValue idValue(largeIntegerDouble);
    REQUIRE(idValue.toString() == "8589934593");
}

TEST_CASE("JsonValue: object keys are emitted in deterministic (sorted) order",
          "[scene][serializer][json][determinism]") {
    // Insert keys in an order that std::unordered_map would NOT preserve.
    // The output must come out lexicographically sorted so byte-diff tests
    // and content-addressable storage stay stable across builds.
    CatEngine::JsonValue obj = CatEngine::JsonValue::object();
    obj["zebra"] = CatEngine::JsonValue(1);
    obj["alpha"] = CatEngine::JsonValue(2);
    obj["mango"] = CatEngine::JsonValue(3);

    std::string serialized = obj.toString();

    size_t alphaPos = serialized.find("\"alpha\"");
    size_t mangoPos = serialized.find("\"mango\"");
    size_t zebraPos = serialized.find("\"zebra\"");

    REQUIRE(alphaPos != std::string::npos);
    REQUIRE(mangoPos != std::string::npos);
    REQUIRE(zebraPos != std::string::npos);
    REQUIRE(alphaPos < mangoPos);
    REQUIRE(mangoPos < zebraPos);
}

TEST_CASE("JsonValue: NaN and infinity serialize to 0 (RFC 8259 conformance)",
          "[scene][serializer][json][edge_cases]") {
    // RFC 8259 forbids NaN/Inf as number literals. The old formatter would
    // emit "nan" / "inf" via the iostream default, which std::stod then
    // throws on, killing the entire scene load. Emitting "0" keeps the
    // scene loadable and lets gameplay code re-derive the value.
    CatEngine::JsonValue nanValue(std::numeric_limits<double>::quiet_NaN());
    CatEngine::JsonValue infValue(std::numeric_limits<double>::infinity());
    REQUIRE(nanValue.toString() == "0");
    REQUIRE(infValue.toString() == "0");
}
