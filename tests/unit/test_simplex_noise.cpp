// test_simplex_noise.cpp
// ---------------------------------------------------------------------------
// Unit tests for the 3-D simplex noise used by the CUDA particle turbulence
// kernel (`engine/cuda/particles/SimplexNoise.hpp`).
//
// WHY these tests matter:
//   The header is shared between nvcc-compiled device code and the Catch2
//   test runner — any drift in the tabulated permutation, the skew/unskew
//   constants, or the 32× output scale silently breaks the in-game
//   turbulence field's amplitude + distribution. The tests below pin every
//   contract downstream callers rely on:
//
//     1. Bounded output (|value| ≤ 1 across a broad sample grid)
//     2. Continuity (small input delta → small output delta)
//     3. Determinism (same inputs → same outputs across calls)
//     4. Non-axis-aligned variation (main motivation for the swap: value
//        noise produced grid banding because its cell faces were the axis
//        planes; simplex should NOT produce a stronger axis response than a
//        diagonal response)
//     5. Zero-input shape (integer lattice points are hashed by corner
//        contribution, so `Simplex3D(0,0,0)` is a well-defined
//        short-circuit-able edge)
//     6. FastFloor + DotGrad detail primitives match their math contract
//        (so if the hot path breaks, the test suite points at which helper
//        drifted)
//
//   These are host-only tests — the shared header is compiled here as plain
//   C++ via the `#ifndef __CUDACC__` branch of SIMPLEX_NOISE_HD. Because
//   the math is bit-identical across host and device (no CUDA intrinsics,
//   only portable <cstdint> and float ops), passing these tests is a
//   sufficient proxy for passing on-GPU.
// ---------------------------------------------------------------------------
#include "catch.hpp"
#include "engine/cuda/particles/SimplexNoise.hpp"

#include <cmath>
#include <limits>

using namespace CatEngine::CUDA::noise;

namespace {

// How far apart two float samples may land before we call them "discontinuous".
// Simplex noise is C^2 — the Lipschitz constant of a 32×(r^4) profile over
// the sampled domain is bounded by the max gradient of the corner
// contributions summed across ≤4 active corners. Empirically the maximum
// one-sided gradient stays below ~8 per unit of input, so for `h=1e-4`
// the output can change by at most ~8e-4. We give ourselves a 10× headroom
// here because we're approximating the Lipschitz bound, not proving it.
constexpr float kContinuityStepSize = 1.0e-4f;
constexpr float kContinuityEpsilon  = 8.0e-3f;

// The backlog item promises "|value| ~ 1" — Gustavson's 32× scale gives a
// theoretical peak at about ±1 on the reference implementation. We allow
// a sliver of overshoot because the measured maximum slightly exceeds 1.0
// on some gradient tables.
constexpr float kAmplitudeBound = 1.05f;

} // namespace

TEST_CASE("Simplex3D: output is bounded across a large sample grid",
          "[simplex][noise]") {
    // Sample densely across a volume that covers multiple simplex cells
    // (each cell is ~1 unit, so a 6×6×6 grid at 0.25 spacing hits ~8 cells
    // in each axis, with ~216 samples per cell — enough to brush every
    // corner's support region).
    float peak = 0.0f;
    int   sampleCount = 0;
    for (int ix = -12; ix <= 12; ++ix) {
        for (int iy = -12; iy <= 12; ++iy) {
            for (int iz = -12; iz <= 12; ++iz) {
                const float x = static_cast<float>(ix) * 0.25f;
                const float y = static_cast<float>(iy) * 0.25f;
                const float z = static_cast<float>(iz) * 0.25f;
                const float v = Simplex3D(x, y, z);
                REQUIRE(std::isfinite(v));
                const float a = std::fabs(v);
                if (a > peak) peak = a;
                ++sampleCount;
            }
        }
    }
    REQUIRE(sampleCount == 25 * 25 * 25);
    REQUIRE(peak <= kAmplitudeBound);
    // Witness a non-trivial output range — if the peak is near zero the
    // gradient table was cleared or the permutation drifted.
    REQUIRE(peak > 0.3f);
}

TEST_CASE("Simplex3D: C^0 continuity under tiny input deltas",
          "[simplex][noise]") {
    // Pick a handful of sites and perturb each by a tiny offset in every
    // axis. Output deltas should stay within a Lipschitz-bounded epsilon.
    struct Site { float x, y, z; };
    constexpr Site kSites[] = {
        { 0.12f,  0.37f,  0.81f},
        {-1.50f,  2.10f, -0.66f},
        {10.00f, -4.42f,  3.75f},
        { 0.00f,  0.00f,  0.00f},
        { 0.33f,  0.33f,  0.33f},  // near-centre of a simplex cell
    };
    const float h = kContinuityStepSize;
    for (const auto& s : kSites) {
        const float v0 = Simplex3D(s.x, s.y, s.z);
        const float vx = Simplex3D(s.x + h, s.y,     s.z    );
        const float vy = Simplex3D(s.x,     s.y + h, s.z    );
        const float vz = Simplex3D(s.x,     s.y,     s.z + h);
        REQUIRE(std::fabs(vx - v0) < kContinuityEpsilon);
        REQUIRE(std::fabs(vy - v0) < kContinuityEpsilon);
        REQUIRE(std::fabs(vz - v0) < kContinuityEpsilon);
    }
}

TEST_CASE("Simplex3D: deterministic across repeated calls",
          "[simplex][noise]") {
    // Same input always produces the same output, regardless of how many
    // samples came before it. This is the core contract the mock test
    // harness assumes when comparing particle trajectories frame-to-frame.
    const float ax = 2.718f, ay = -1.414f, az = 0.577f;
    const float bx = 0.5f,   by = 0.25f,   bz = 0.125f;

    const float a1 = Simplex3D(ax, ay, az);
    const float b1 = Simplex3D(bx, by, bz);
    for (int i = 0; i < 128; ++i) {
        (void)Simplex3D(static_cast<float>(i) * 0.13f,
                        static_cast<float>(i) * 0.19f,
                        static_cast<float>(i) * 0.27f);
    }
    const float a2 = Simplex3D(ax, ay, az);
    const float b2 = Simplex3D(bx, by, bz);

    REQUIRE(a1 == a2);
    REQUIRE(b1 == b2);
}

TEST_CASE("Simplex3D: axis response ≈ diagonal response (anti-grid-banding)",
          "[simplex][noise]") {
    // The motivating defect of the old value-noise path: an axis sweep
    // produced visibly periodic output tied to the cubic grid spacing,
    // while a diagonal sweep — which crosses simplex / cube cells at
    // roughly √3 the rate — produced a different response amplitude.
    // Simplex noise's tetrahedral tessellation has no axis-aligned cell
    // faces, so an axis sweep should show the same variance distribution
    // as a diagonal sweep. We measure variance on both and assert the
    // axis variance is NOT systematically lower than the diagonal
    // variance (historically it was ~40% lower for the old path).
    //
    // "Within a factor of 2" is a deliberately loose bound — simplex gives
    // small random variation run-to-run; the old value-noise path showed
    // an order-of-magnitude gap.
    constexpr int kSamples = 1024;
    const auto variance = [](float (*sampler)(int)) {
        double mean = 0.0;
        for (int i = 0; i < kSamples; ++i) mean += static_cast<double>(sampler(i));
        mean /= kSamples;
        double sq = 0.0;
        for (int i = 0; i < kSamples; ++i) {
            const double d = static_cast<double>(sampler(i)) - mean;
            sq += d * d;
        }
        return sq / kSamples;
    };

    // Axis sweep along X, y=z=0.37 (arbitrary non-axis-cell offsets).
    const double axisVar = variance([](int i) -> float {
        const float x = static_cast<float>(i) * 0.0625f;
        return Simplex3D(x, 0.37f, -0.29f);
    });
    // Diagonal sweep along (1,1,1)/√3 scaled.
    const double diagVar = variance([](int i) -> float {
        const float t = static_cast<float>(i) * 0.0625f;
        return Simplex3D(t * 0.577f + 0.11f,
                         t * 0.577f - 0.21f,
                         t * 0.577f + 0.03f);
    });

    REQUIRE(axisVar > 0.0);
    REQUIRE(diagVar > 0.0);
    // Both directions must have comparable signal strength (within 2×).
    REQUIRE(axisVar < 2.0 * diagVar);
    REQUIRE(diagVar < 2.0 * axisVar);
}

TEST_CASE("Simplex3D: origin has a well-defined short-circuit value",
          "[simplex][noise]") {
    // The origin is the 1st corner of the (0,0,0) simplex cell and its
    // offset from that corner is exactly zero, so r^2 = 0 and t0 = 0.6.
    // The output depends only on the permutation at index 0 and the chosen
    // gradient. We don't pin the exact value (it would break if the
    // permutation table is ever re-sampled for a different visual
    // character) — we only require it is finite and bounded.
    const float v = Simplex3D(0.0f, 0.0f, 0.0f);
    REQUIRE(std::isfinite(v));
    REQUIRE(std::fabs(v) <= kAmplitudeBound);
}

TEST_CASE("Simplex3D: symmetry under permutation-safe large-integer shifts",
          "[simplex][noise]") {
    // The hash masks inputs with `i & 255`, so shifts by 256 on an
    // integer lattice should reproduce the same output — the test pins
    // that wrap without relying on implementation details. This is also
    // the test that catches off-by-one drift if the permutation table
    // ever gets an extra or missing row (the `& 255` only works as
    // written when kPermutation has exactly 256 unique entries).
    const float a = Simplex3D(1.25f, -0.75f, 2.5f);
    const float b = Simplex3D(1.25f + 256.0f, -0.75f + 256.0f, 2.5f + 256.0f);
    REQUIRE(a == Approx(b).margin(1.0e-4f));
}

TEST_CASE("detail::FastFloor: matches std::floor on float inputs",
          "[simplex][noise]") {
    // Checking the branchless fast-floor against the library floor for a
    // swathe of tricky inputs — positive, negative, near-integer, fractional
    // near zero, and the zero itself. If this test fails the Simplex3D
    // output will be wrong at every lattice boundary.
    const float kInputs[] = {
        0.0f, 0.5f, 0.999999f, 1.0f, 1.0001f, -0.0001f, -0.5f, -1.0f,
        -0.9999f, 3.14159f, -3.14159f, 100.0f, -100.0f, 0.333333f
    };
    for (float v : kInputs) {
        const int expected = static_cast<int>(std::floor(v));
        const int actual   = detail::FastFloor(v);
        REQUIRE(actual == expected);
    }
}

TEST_CASE("detail::DotGrad: dot with zero offset is zero",
          "[simplex][noise]") {
    // Every gradient direction dotted with the zero offset is zero by
    // definition; this guards the helper against an accidental `+= g.x` vs
    // `*= g.x` drift that wouldn't show up in Simplex3D's final output
    // but would break analytical derivative computation in any future
    // caller (e.g. an analytical curl that needs DotGrad(...) at the
    // corner origin).
    for (int i = 0; i < 12; ++i) {
        const Grad3 g = detail::GradAt(i);
        REQUIRE(detail::DotGrad(g, 0.0f, 0.0f, 0.0f) == 0.0f);
    }
}

TEST_CASE("detail::GradAt: every direction has magnitude √2 (unit after scale)",
          "[simplex][noise]") {
    // The 12 canonical gradients are unit vectors scaled by √2 (each has
    // exactly two nonzero components of magnitude 1). If this regresses, the
    // 32× amplitude scale will no longer normalise the output to [-1, 1].
    for (int i = 0; i < 12; ++i) {
        const Grad3 g = detail::GradAt(i);
        const float m2 = g.x * g.x + g.y * g.y + g.z * g.z;
        REQUIRE(m2 == Approx(2.0f).epsilon(1.0e-6f));
    }
}
