// Unit tests for the particle alpha fade (engine/cuda/particles/ParticleFade.hpp)
// — the exact function the CUDA update kernel executes per particle per frame,
// compiled for the host via the plain-C++ branch of PARTICLE_FADE_HD (same
// pattern as the simplex-noise tests).
//
// Round-7 audit (2026-07-18), LOW: the kernel used to do `color.w *= ratio`
// against the PERSISTED color each frame, so the stored alpha became the
// running PRODUCT of every past lifetimeRatio instead of baseAlpha * ratio —
// the fade compounded geometrically and every death/hit burst vanished at ~25%
// of its configured lifetime (~1e-5 by frame 45 of a 1.5 s / 60 fps burst,
// where the intended linear fade is still ~0.5 of base). fadedAlpha applies
// the telescoping ratio-of-ratios update, whose invariant these tests pin:
// after any number of frames the stored alpha equals baseAlpha * currentRatio.

#include "catch.hpp"
#include "engine/cuda/particles/ParticleFade.hpp"

#include <cmath>

using CatEngine::CUDA::fade::fadedAlpha;

TEST_CASE("particle alpha fade is linear in remaining lifetime, not compounding",
          "[particles][fade]") {
    // A 1.5 s particle at 60 fps with base alpha 0.8 — the audit's own example.
    const float maxLifetime = 1.5f;
    const float dt = 1.0f / 60.0f;
    const float baseAlpha = 0.8f;

    float lifetime = maxLifetime;
    float alpha = baseAlpha;  // colors[idx].w as written by the emission kernel

    // Simulate the kernel's per-frame order: decrement lifetime, then fade.
    for (int frame = 0; frame < 45; ++frame) {
        lifetime -= dt;
        alpha = fadedAlpha(alpha, lifetime, maxLifetime, dt);
    }

    // Invariant: stored alpha == baseAlpha * currentRatio. At frame 45 of 90
    // the ratio is 0.5, so alpha must be ~0.4. The pre-fix compounding product
    // prod_{k=1..45}(1 - k/90) is ~1e-5 here — three orders of magnitude off —
    // so this assertion fails first against the old `w *= ratio` semantics.
    const float expectedRatio = lifetime / maxLifetime;
    REQUIRE(alpha == Approx(baseAlpha * expectedRatio).epsilon(0.001));
    REQUIRE(alpha > 0.35f);  // blunt floor: visibly alive at half lifetime

    // Run to 90% of lifetime consumed — still linear, still visible.
    for (int frame = 45; frame < 81; ++frame) {
        lifetime -= dt;
        alpha = fadedAlpha(alpha, lifetime, maxLifetime, dt);
    }
    REQUIRE(alpha == Approx(baseAlpha * (lifetime / maxLifetime)).epsilon(0.005));
}

TEST_CASE("particle alpha fade edge cases", "[particles][fade]") {
    SECTION("terminal frame (previous ratio ~0) returns alpha unchanged") {
        // lifetime + dt ≈ 0: the kill pass culls the particle next; the guard
        // avoids the divide and holds the alpha for that single frame.
        const float alpha = fadedAlpha(0.3f, -0.016f, 1.5f, 0.016f);
        REQUIRE(alpha == Approx(0.3f));
    }
    SECTION("non-positive maxLifetime is inert") {
        REQUIRE(fadedAlpha(0.7f, 0.5f, 0.0f, 0.016f) == Approx(0.7f));
        REQUIRE(fadedAlpha(0.7f, 0.5f, -1.0f, 0.016f) == Approx(0.7f));
    }
    SECTION("first update from full lifetime applies exactly one ratio step") {
        // Spawn: lifetime = maxLifetime. After the first decrement the previous
        // ratio reconstructs to exactly 1.0, so alpha = base * r_1.
        const float maxLifetime = 2.0f;
        const float dt = 0.1f;
        const float alpha = fadedAlpha(1.0f, maxLifetime - dt, maxLifetime, dt);
        REQUIRE(alpha == Approx((maxLifetime - dt) / maxLifetime));
    }
}
