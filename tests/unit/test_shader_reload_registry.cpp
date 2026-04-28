// ============================================================================
// tests/unit/test_shader_reload_registry.cpp
//
// Unit coverage for engine/rhi/ShaderReloadRegistry.hpp — the renderer-side
// subscriber layer that the ShaderHotReloadDriver dispatches reload events
// through. The registry is pure STL (no Vulkan, no filesystem) so every case
// here exercises it end-to-end with captured-lambda subscribers; we never
// need a real VulkanShader or a running Vulkan device.
//
// Coverage intent:
//
//   - NormalizeSourcePath: the backslash-collapsing map-key canonicaliser.
//     Pure string -> string; testable on literal inputs.
//   - Register / Unregister lifecycle: handles are unique + opaque, Unregister
//     is idempotent, empty buckets are pruned so SubscriberCount reports 0
//     instead of a ghost key.
//   - Dispatch happy path: apply() runs, onReloaded() runs after success.
//   - Dispatch failure paths: compileOk=false, empty bytes, apply() returns
//     false -> onReloaded must NOT fire (the pass keeps its prior-good
//     pipeline; the whole point of the registry's apply/onReloaded split).
//   - Multi-subscriber fanout: two passes registering the same sourcePath
//     both receive the dispatch.
//   - Path normalisation through dispatch: register with backslashes, fire
//     with forward slashes (and vice-versa) — both hit.
//   - Unregister during dispatch: a callback that removes its own handle
//     must not crash the outer iteration (tested via the snapshot-before-
//     invoke guard inside Dispatch).
//   - DispatchResult bookkeeping: counts are accurate across the mixed
//     success/failure case.
// ============================================================================

#include "catch.hpp"
#include "engine/rhi/ShaderReloadRegistry.hpp"

#include <cstdint>
#include <string>
#include <vector>

using namespace CatEngine::RHI;
using namespace CatEngine::RHI::ShaderReloadRegistryDetail;

namespace {
// Convenience: build a small fake SPIR-V byte buffer. Real bytecode starts
// with the SPIR-V magic number 0x07230203 little-endian, but the registry
// doesn't validate bytes — it just hands them to the subscriber's apply(),
// which is user-owned. So any non-empty byte buffer is fine for registry
// tests.
std::vector<uint8_t> MakeFakeSpv(size_t n = 16) {
    std::vector<uint8_t> bytes(n);
    for (size_t i = 0; i < n; ++i) bytes[i] = static_cast<uint8_t>(i * 7 + 3);
    return bytes;
}
} // namespace

// ---------------------------------------------------------------------------
// NormalizeSourcePath — pure string canonicaliser.
// ---------------------------------------------------------------------------

TEST_CASE("NormalizeSourcePath leaves forward-slash paths unchanged",
          "[shader-reload-registry]") {
    REQUIRE(NormalizeSourcePath("shaders/shadow/shadow.vert") ==
            "shaders/shadow/shadow.vert");
    REQUIRE(NormalizeSourcePath("a/b/c.comp") == "a/b/c.comp");
    REQUIRE(NormalizeSourcePath("").empty());
    REQUIRE(NormalizeSourcePath("flat.frag") == "flat.frag");
}

TEST_CASE("NormalizeSourcePath converts backslashes to forward slashes",
          "[shader-reload-registry]") {
    REQUIRE(NormalizeSourcePath("shaders\\shadow\\shadow.vert") ==
            "shaders/shadow/shadow.vert");
    REQUIRE(NormalizeSourcePath("mixed/path\\with/both.frag") ==
            "mixed/path/with/both.frag");
    REQUIRE(NormalizeSourcePath("\\leading.vert") == "/leading.vert");
    REQUIRE(NormalizeSourcePath("trailing\\") == "trailing/");
}

// ---------------------------------------------------------------------------
// Register / Unregister lifecycle.
// ---------------------------------------------------------------------------

TEST_CASE("Register returns unique, valid handles",
          "[shader-reload-registry]") {
    auto& reg = ShaderReloadRegistry::Get();
    reg.ClearForTest();

    auto h1 = reg.Register("shaders/a.vert",
                           [](const std::vector<uint8_t>&) { return true; });
    auto h2 = reg.Register("shaders/a.vert",
                           [](const std::vector<uint8_t>&) { return true; });
    auto h3 = reg.Register("shaders/b.frag",
                           [](const std::vector<uint8_t>&) { return true; });

    REQUIRE(h1.IsValid());
    REQUIRE(h2.IsValid());
    REQUIRE(h3.IsValid());
    REQUIRE(h1.value != h2.value);
    REQUIRE(h2.value != h3.value);
    REQUIRE(h1.value != h3.value);
    REQUIRE(reg.TotalSubscribers() == 3);
    REQUIRE(reg.SubscriberCount("shaders/a.vert") == 2);
    REQUIRE(reg.SubscriberCount("shaders/b.frag") == 1);
    REQUIRE(reg.SubscriberCount("shaders/c.comp") == 0);
}

TEST_CASE("Default SubscriptionHandle is invalid",
          "[shader-reload-registry]") {
    ShaderReloadRegistry::SubscriptionHandle h;
    REQUIRE_FALSE(h.IsValid());
    REQUIRE(h.value == 0);
}

TEST_CASE("Unregister removes only the targeted subscriber",
          "[shader-reload-registry]") {
    auto& reg = ShaderReloadRegistry::Get();
    reg.ClearForTest();

    int fired_a1 = 0, fired_a2 = 0;
    auto h1 = reg.Register("shaders/x.vert",
                           [&fired_a1](const std::vector<uint8_t>&) { ++fired_a1; return true; });
    auto h2 = reg.Register("shaders/x.vert",
                           [&fired_a2](const std::vector<uint8_t>&) { ++fired_a2; return true; });

    REQUIRE(reg.SubscriberCount("shaders/x.vert") == 2);
    REQUIRE(reg.Unregister(h1) == true);
    REQUIRE(reg.SubscriberCount("shaders/x.vert") == 1);

    auto result = reg.Dispatch("shaders/x.vert", MakeFakeSpv(), /*compileOk=*/true);
    REQUIRE(result.subscribersNotified == 1);
    REQUIRE(fired_a1 == 0);     // first was unregistered
    REQUIRE(fired_a2 == 1);     // second still subscribed
    (void)h2;
}

TEST_CASE("Unregister is idempotent",
          "[shader-reload-registry]") {
    auto& reg = ShaderReloadRegistry::Get();
    reg.ClearForTest();

    auto h = reg.Register("shaders/y.frag",
                          [](const std::vector<uint8_t>&) { return true; });
    REQUIRE(reg.Unregister(h) == true);
    REQUIRE(reg.Unregister(h) == false);
    // Second removal is a no-op — doesn't throw, doesn't corrupt state.

    // An invalid handle (default-constructed) is also a no-op.
    REQUIRE(reg.Unregister({}) == false);

    // A "made up" handle value that was never issued is also a no-op.
    REQUIRE(reg.Unregister(
        ShaderReloadRegistry::SubscriptionHandle{999999}) == false);
}

TEST_CASE("Unregister prunes empty buckets",
          "[shader-reload-registry]") {
    auto& reg = ShaderReloadRegistry::Get();
    reg.ClearForTest();

    auto h = reg.Register("shaders/z.vert",
                          [](const std::vector<uint8_t>&) { return true; });
    REQUIRE(reg.SubscriberCount("shaders/z.vert") == 1);
    reg.Unregister(h);
    REQUIRE(reg.SubscriberCount("shaders/z.vert") == 0);
    REQUIRE(reg.TotalSubscribers() == 0);
    // The path-bucket should be pruned, not left empty — otherwise a
    // ghost key would linger in the map. We can only verify externally
    // via TotalSubscribers + SubscriberCount both being zero.
}

// ---------------------------------------------------------------------------
// Dispatch happy path.
// ---------------------------------------------------------------------------

TEST_CASE("Dispatch fires apply and onReloaded on success",
          "[shader-reload-registry]") {
    auto& reg = ShaderReloadRegistry::Get();
    reg.ClearForTest();

    std::vector<uint8_t> appliedBytes;
    int onReloadedFired = 0;

    reg.Register("shaders/p.vert",
                 [&appliedBytes](const std::vector<uint8_t>& bytes) {
                     appliedBytes = bytes;
                     return true;
                 },
                 [&onReloadedFired]() {
                     ++onReloadedFired;
                 });

    const auto bytes = MakeFakeSpv(32);
    auto result = reg.Dispatch("shaders/p.vert", bytes, /*compileOk=*/true);

    REQUIRE(result.subscribersNotified == 1);
    REQUIRE(result.applySucceeded == 1);
    REQUIRE(result.applyFailed == 0);
    REQUIRE(result.onReloadedFired == 1);
    REQUIRE(appliedBytes == bytes);
    REQUIRE(onReloadedFired == 1);
}

TEST_CASE("Dispatch fans out to every subscriber on the sourcePath",
          "[shader-reload-registry]") {
    auto& reg = ShaderReloadRegistry::Get();
    reg.ClearForTest();

    int pass1Applied = 0, pass1OnReloaded = 0;
    int pass2Applied = 0, pass2OnReloaded = 0;

    reg.Register("shaders/shared.vert",
                 [&pass1Applied](const std::vector<uint8_t>&) { ++pass1Applied; return true; },
                 [&pass1OnReloaded]() { ++pass1OnReloaded; });
    reg.Register("shaders/shared.vert",
                 [&pass2Applied](const std::vector<uint8_t>&) { ++pass2Applied; return true; },
                 [&pass2OnReloaded]() { ++pass2OnReloaded; });

    auto result = reg.Dispatch("shaders/shared.vert", MakeFakeSpv(),
                                /*compileOk=*/true);
    REQUIRE(result.subscribersNotified == 2);
    REQUIRE(result.applySucceeded == 2);
    REQUIRE(result.onReloadedFired == 2);
    REQUIRE(pass1Applied == 1);
    REQUIRE(pass2Applied == 1);
    REQUIRE(pass1OnReloaded == 1);
    REQUIRE(pass2OnReloaded == 1);
}

TEST_CASE("Dispatch with no subscribers is a clean zero result",
          "[shader-reload-registry]") {
    auto& reg = ShaderReloadRegistry::Get();
    reg.ClearForTest();

    auto result = reg.Dispatch("shaders/nobody.frag", MakeFakeSpv(), true);
    REQUIRE(result.subscribersNotified == 0);
    REQUIRE(result.applySucceeded == 0);
    REQUIRE(result.applyFailed == 0);
    REQUIRE(result.onReloadedFired == 0);
}

// ---------------------------------------------------------------------------
// Dispatch failure paths. These exist explicitly because the whole point of
// the apply/onReloaded split is to keep a broken reload from corrupting
// downstream pipelines.
// ---------------------------------------------------------------------------

TEST_CASE("Dispatch with compileOk=false notifies but does not apply",
          "[shader-reload-registry]") {
    auto& reg = ShaderReloadRegistry::Get();
    reg.ClearForTest();

    int applied = 0, onReloaded = 0;
    reg.Register("shaders/q.frag",
                 [&applied](const std::vector<uint8_t>&) { ++applied; return true; },
                 [&onReloaded]() { ++onReloaded; });

    auto result = reg.Dispatch("shaders/q.frag", {}, /*compileOk=*/false);
    REQUIRE(result.subscribersNotified == 1);
    REQUIRE(result.applySucceeded == 0);
    REQUIRE(result.applyFailed == 0);       // counted as a compile error, not an apply-fail
    REQUIRE(result.onReloadedFired == 0);
    REQUIRE(applied == 0);
    REQUIRE(onReloaded == 0);
}

TEST_CASE("Dispatch with compileOk=true but empty bytes counts as applyFailed",
          "[shader-reload-registry]") {
    auto& reg = ShaderReloadRegistry::Get();
    reg.ClearForTest();

    int applied = 0, onReloaded = 0;
    reg.Register("shaders/r.vert",
                 [&applied](const std::vector<uint8_t>&) { ++applied; return true; },
                 [&onReloaded]() { ++onReloaded; });

    auto result = reg.Dispatch("shaders/r.vert", {}, /*compileOk=*/true);
    REQUIRE(result.subscribersNotified == 1);
    REQUIRE(result.applyFailed == 1);       // slurp-lost-race surfaces here
    REQUIRE(result.applySucceeded == 0);
    REQUIRE(result.onReloadedFired == 0);
    REQUIRE(applied == 0);                  // apply itself was never invoked
    REQUIRE(onReloaded == 0);
}

TEST_CASE("Dispatch with apply returning false does not fire onReloaded",
          "[shader-reload-registry]") {
    auto& reg = ShaderReloadRegistry::Get();
    reg.ClearForTest();

    int applied = 0, onReloaded = 0;
    reg.Register("shaders/s.frag",
                 [&applied](const std::vector<uint8_t>&) {
                     ++applied;
                     return false;  // e.g. VulkanShader::ReloadFromSPIRV rejected the bytes
                 },
                 [&onReloaded]() { ++onReloaded; });

    auto result = reg.Dispatch("shaders/s.frag", MakeFakeSpv(),
                                /*compileOk=*/true);
    REQUIRE(result.subscribersNotified == 1);
    REQUIRE(result.applySucceeded == 0);
    REQUIRE(result.applyFailed == 1);
    REQUIRE(result.onReloadedFired == 0);
    REQUIRE(applied == 1);                  // apply was called
    REQUIRE(onReloaded == 0);               // but the downstream hook did NOT fire
}

TEST_CASE("Dispatch counts mixed apply success/failure correctly",
          "[shader-reload-registry]") {
    auto& reg = ShaderReloadRegistry::Get();
    reg.ClearForTest();

    reg.Register("shaders/t.vert",
                 [](const std::vector<uint8_t>&) { return true; },
                 []() {});
    reg.Register("shaders/t.vert",
                 [](const std::vector<uint8_t>&) { return false; },
                 []() {});  // onReloaded should NOT fire for the failed one
    reg.Register("shaders/t.vert",
                 [](const std::vector<uint8_t>&) { return true; },
                 []() {});

    auto result = reg.Dispatch("shaders/t.vert", MakeFakeSpv(),
                                /*compileOk=*/true);
    REQUIRE(result.subscribersNotified == 3);
    REQUIRE(result.applySucceeded == 2);
    REQUIRE(result.applyFailed == 1);
    REQUIRE(result.onReloadedFired == 2);   // only the 2 that applied successfully
}

// ---------------------------------------------------------------------------
// Path normalisation through the full Register -> Dispatch round trip.
// This is the one the pass-side code most likely trips on: Windows developer
// types a backslash path at Register, the driver fires with a forward-slash
// path, and the two had better meet in the middle.
// ---------------------------------------------------------------------------

TEST_CASE("Register with backslashes is reached by forward-slash Dispatch",
          "[shader-reload-registry]") {
    auto& reg = ShaderReloadRegistry::Get();
    reg.ClearForTest();

    int applied = 0;
    reg.Register("shaders\\shadow\\shadow.vert",
                 [&applied](const std::vector<uint8_t>&) { ++applied; return true; });

    auto result = reg.Dispatch("shaders/shadow/shadow.vert", MakeFakeSpv(),
                                /*compileOk=*/true);
    REQUIRE(result.subscribersNotified == 1);
    REQUIRE(applied == 1);
}

TEST_CASE("Register with forward slashes is reached by backslash Dispatch",
          "[shader-reload-registry]") {
    auto& reg = ShaderReloadRegistry::Get();
    reg.ClearForTest();

    int applied = 0;
    reg.Register("shaders/shadow/shadow.frag",
                 [&applied](const std::vector<uint8_t>&) { ++applied; return true; });

    auto result = reg.Dispatch("shaders\\shadow\\shadow.frag", MakeFakeSpv(),
                                /*compileOk=*/true);
    REQUIRE(result.subscribersNotified == 1);
    REQUIRE(applied == 1);
}

// ---------------------------------------------------------------------------
// Callback that mutates the registry during dispatch. The implementation
// snapshots the entry list before invoking callbacks so a self-unregister
// (or a sibling-register) doesn't crash the outer iteration. This is the
// test that would catch a regression if someone "optimised" the snapshot
// away and started iterating the live vector.
// ---------------------------------------------------------------------------

TEST_CASE("Callback can Unregister itself during Dispatch without crashing",
          "[shader-reload-registry]") {
    auto& reg = ShaderReloadRegistry::Get();
    reg.ClearForTest();

    ShaderReloadRegistry::SubscriptionHandle selfHandle;
    int appliedCount = 0;

    selfHandle = reg.Register(
        "shaders/suicide.vert",
        [&reg, &selfHandle, &appliedCount](const std::vector<uint8_t>&) {
            ++appliedCount;
            // Unregister myself mid-dispatch. Dispatch's snapshot-before-
            // invoke guard must keep the outer iteration stable.
            reg.Unregister(selfHandle);
            return true;
        });

    auto result = reg.Dispatch("shaders/suicide.vert", MakeFakeSpv(),
                                /*compileOk=*/true);
    REQUIRE(result.subscribersNotified == 1);
    REQUIRE(result.applySucceeded == 1);
    REQUIRE(appliedCount == 1);

    // After dispatch the subscriber is gone — the next dispatch is a no-op.
    auto result2 = reg.Dispatch("shaders/suicide.vert", MakeFakeSpv(),
                                 /*compileOk=*/true);
    REQUIRE(result2.subscribersNotified == 0);
}

TEST_CASE("Null apply / onReloaded callables are silently tolerated",
          "[shader-reload-registry]") {
    auto& reg = ShaderReloadRegistry::Get();
    reg.ClearForTest();

    // A subscriber registered with a default-constructed (empty) ApplyFn
    // shouldn't crash dispatch — it's not a useful subscription, but
    // tolerating it means a future caller that conditionally builds its
    // callback doesn't have to guard the Register call site.
    reg.Register("shaders/null.vert", {}, {});
    auto result = reg.Dispatch("shaders/null.vert", MakeFakeSpv(),
                                /*compileOk=*/true);
    REQUIRE(result.subscribersNotified == 1);
    REQUIRE(result.applySucceeded == 0);
    REQUIRE(result.applyFailed == 1);     // empty callable counts as an apply failure
    REQUIRE(result.onReloadedFired == 0);
}

TEST_CASE("ClearForTest preserves handle monotonicity",
          "[shader-reload-registry]") {
    auto& reg = ShaderReloadRegistry::Get();
    reg.ClearForTest();

    auto h1 = reg.Register("shaders/a.vert",
                           [](const std::vector<uint8_t>&) { return true; });
    reg.ClearForTest();
    auto h2 = reg.Register("shaders/a.vert",
                           [](const std::vector<uint8_t>&) { return true; });
    // After ClearForTest the handle counter must keep climbing so a stale
    // pre-clear handle can't alias a new post-clear entry. This is the
    // subtle invariant ClearForTest's comment calls out — if someone
    // "cleaned up" by resetting the counter, this test catches it.
    REQUIRE(h2.value > h1.value);
}
