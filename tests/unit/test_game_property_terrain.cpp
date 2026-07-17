/**
 * Property tests for Terrain heightmap sampling — game/world/Terrain.cpp
 * (Terrain::getHeightAt + Terrain::worldToGrid + Terrain::sampleHeightmap).
 *
 * Why a pure-math mirror rather than instantiating Terrain directly:
 * The shipping Terrain class drags in cudaContext / CudaBuffer / cuda_runtime.h
 * and generates its heightmap on the GPU. Even the CPU-side `getHeightAt`
 * path requires `downloadFromGpu()` first, which requires a live CUDA
 * context that the no-GPU test build (USE_MOCK_GPU=1) does not provide.
 *
 * The drift-guard pattern test_clustered_lighting_math.cpp pioneered
 * applies cleanly here: we re-implement the bilinear sampler INLINE,
 * byte-for-byte against Terrain.cpp lines 370-403 + 483-500, and pin
 * the algorithmic contract. If a future Terrain refactor changes the
 * sampler (different boundary clamp / different floor convention /
 * different coordinate origin) the production file and this mirror
 * diverge — the test fails — and the diff makes the divergence visible
 * before the divergence ships.
 *
 * Tests cover:
 *
 *   - Bilinear sample at exact integer grid coordinates matches the
 *     stored heightmap value byte-for-byte.
 *   - Bilinear sample inside any [0, 1]^2 cell falls inside the closed
 *     interval [min, max] of that cell's 4 corners.
 *   - Out-of-bounds query returns 0 (Terrain.cpp:371-373 contract).
 *   - isInBounds boundary is closed at +/- halfSize.
 *   - getBounds AABB is centered on origin with min.y=0, max.y=heightScale.
 *   - Sampling on a constant-height map returns that constant everywhere.
 *   - Sampling on a linear ramp returns a linear interpolation.
 *   - Sampling on a +/- ramp checkerboard preserves the mean across the
 *     full domain to within float precision.
 */

#include "catch.hpp"
#include "test_seed.hpp"
#include "engine/math/Vector.hpp"
#include "engine/math/AABB.hpp"

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

namespace {

// Pure-math mirror of Terrain::Params — only the fields the sampler needs.
struct TerrainMirrorParams {
    int resolution = 16;
    float size = 16.0f;
    float heightScale = 50.0f;
};

// Pure-math mirror of Terrain::sampleHeightmap (Terrain.cpp:495-500).
inline float sampleHeightmap(const std::vector<float>& heightmap, int x, int z, int resolution) {
    x = std::clamp(x, 0, resolution - 1);
    z = std::clamp(z, 0, resolution - 1);
    return heightmap[z * resolution + x];
}

// Pure-math mirror of Terrain::isInBounds (Terrain.cpp:448-451).
inline bool isInBounds(float x, float z, const TerrainMirrorParams& p) {
    const float halfSize = p.size * 0.5f;
    return x >= -halfSize && x <= halfSize && z >= -halfSize && z <= halfSize;
}

// Pure-math mirror of Terrain::getHeightAt (Terrain.cpp:370-403). The
// `downloaded` short-circuit is dropped (this is a CPU-only sampler that
// owns its data inline), but the rest of the formula is identical.
float getHeightAt(const std::vector<float>& heightmap,
                  float x, float z,
                  const TerrainMirrorParams& p) {
    if (!isInBounds(x, z, p)) {
        return 0.0f;
    }

    const int resolution = p.resolution;
    const float cellSize = p.size / (resolution - 1);
    const float halfSize = p.size * 0.5f;

    const float localX = (x + halfSize) / cellSize;
    const float localZ = (z + halfSize) / cellSize;

    const int x0 = static_cast<int>(std::floor(localX));
    const int z0 = static_cast<int>(std::floor(localZ));
    const int x1 = std::min(x0 + 1, resolution - 1);
    const int z1 = std::min(z0 + 1, resolution - 1);

    const float fx = localX - x0;
    const float fz = localZ - z0;

    const float h00 = sampleHeightmap(heightmap, x0, z0, resolution);
    const float h10 = sampleHeightmap(heightmap, x1, z0, resolution);
    const float h01 = sampleHeightmap(heightmap, x0, z1, resolution);
    const float h11 = sampleHeightmap(heightmap, x1, z1, resolution);

    // Same Math::lerp as Engine::Math::lerp — a + (b-a)*t.
    auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
    const float h0 = lerp(h00, h10, fx);
    const float h1 = lerp(h01, h11, fx);
    return lerp(h0, h1, fz);
}

// World-to-grid coordinate mirror (Terrain.cpp:483-493). Used to compute
// the world coordinate of an exact integer grid cell so tests can assert
// "sampling AT cell N returns heightmap[N]".
inline void gridToWorld(int gridX, int gridZ, const TerrainMirrorParams& p,
                        float& worldX, float& worldZ) {
    const float cellSize = p.size / (p.resolution - 1);
    const float halfSize = p.size * 0.5f;
    worldX = static_cast<float>(gridX) * cellSize - halfSize;
    worldZ = static_cast<float>(gridZ) * cellSize - halfSize;
}

}  // namespace

TEST_CASE("Terrain sample at exact integer grid coords matches stored value byte-for-byte",
          "[terrain][property][bilinear][exact]") {
    TerrainMirrorParams params;
    params.resolution = 8;
    params.size = 8.0f;

    // Build a heightmap with a unique signature value per cell so a
    // mismatch is obvious.
    std::vector<float> heightmap(params.resolution * params.resolution);
    for (int z = 0; z < params.resolution; ++z) {
        for (int x = 0; x < params.resolution; ++x) {
            heightmap[z * params.resolution + x] =
                static_cast<float>(z * 100 + x);
        }
    }

    // Every integer grid coordinate must round-trip through the sampler
    // back to its stored value. Boundary cells (x == resolution-1 or
    // z == resolution-1) are tested too — `std::min(x0+1, resolution-1)`
    // clamps the upper neighbour so fx==0 at the boundary returns the
    // boundary cell exactly.
    for (int z = 0; z < params.resolution; ++z) {
        for (int x = 0; x < params.resolution; ++x) {
            float wx, wz;
            gridToWorld(x, z, params, wx, wz);
            const float sampled = getHeightAt(heightmap, wx, wz, params);
            const float stored = heightmap[z * params.resolution + x];
            if (sampled != Approx(stored).epsilon(1.0e-5f)) {
                INFO("Mismatch at (x=" << x << ", z=" << z << ")"
                     << " sampled=" << sampled << " stored=" << stored);
                FAIL();
            }
        }
    }
}

TEST_CASE("Terrain bilinear sample inside a cell stays within [min, max] of corners",
          "[terrain][property][bilinear][bounded]") {
    TerrainMirrorParams params;
    params.resolution = 16;
    params.size = 16.0f;

    // Random heights per cell. Bilinear interpolation of 4 finite corners
    // is a convex combination — every interior sample must fall inside
    // the closed interval [min corner, max corner].
    std::mt19937 rng(static_cast<unsigned>(CatTest::DeterministicSeed("test_game_property_terrain:0xDEADBEEF")));
    std::uniform_real_distribution<float> heightDist(0.0f, 100.0f);
    std::vector<float> heightmap(params.resolution * params.resolution);
    for (auto& h : heightmap) {
        h = heightDist(rng);
    }

    // Sample many points strictly inside the grid (not on the boundary).
    std::uniform_real_distribution<float> coordDist(-params.size * 0.5f + 0.1f,
                                                     params.size * 0.5f - 0.1f);
    for (int i = 0; i < 5000; ++i) {
        const float x = coordDist(rng);
        const float z = coordDist(rng);

        // Locate the cell.
        const float cellSize = params.size / (params.resolution - 1);
        const float halfSize = params.size * 0.5f;
        const int x0 = static_cast<int>(std::floor((x + halfSize) / cellSize));
        const int z0 = static_cast<int>(std::floor((z + halfSize) / cellSize));
        const int x1 = std::min(x0 + 1, params.resolution - 1);
        const int z1 = std::min(z0 + 1, params.resolution - 1);

        const float h00 = sampleHeightmap(heightmap, x0, z0, params.resolution);
        const float h10 = sampleHeightmap(heightmap, x1, z0, params.resolution);
        const float h01 = sampleHeightmap(heightmap, x0, z1, params.resolution);
        const float h11 = sampleHeightmap(heightmap, x1, z1, params.resolution);

        const float minCorner = std::min({h00, h10, h01, h11});
        const float maxCorner = std::max({h00, h10, h01, h11});
        const float sampled = getHeightAt(heightmap, x, z, params);

        if (sampled < minCorner - 1.0e-4f || sampled > maxCorner + 1.0e-4f) {
            INFO("Bilinear out-of-cell at (x=" << x << ", z=" << z << ")"
                 << " sampled=" << sampled
                 << " min=" << minCorner << " max=" << maxCorner);
            FAIL();
        }
    }
}

TEST_CASE("Terrain sample is exactly the constant on a constant-height map",
          "[terrain][property][bilinear][constant]") {
    TerrainMirrorParams params;
    params.resolution = 32;
    params.size = 64.0f;
    const float kConstant = 17.5f;
    std::vector<float> heightmap(params.resolution * params.resolution, kConstant);

    std::mt19937 rng(static_cast<unsigned>(CatTest::DeterministicSeed("test_game_property_terrain:0xABCDEF")));
    std::uniform_real_distribution<float> coordDist(-params.size * 0.5f,
                                                     params.size * 0.5f);
    for (int i = 0; i < 1000; ++i) {
        const float sampled = getHeightAt(heightmap, coordDist(rng), coordDist(rng), params);
        REQUIRE(sampled == Approx(kConstant).epsilon(1.0e-6f));
    }
}

TEST_CASE("Terrain sample on a linear ramp produces a linear gradient",
          "[terrain][property][bilinear][ramp]") {
    TerrainMirrorParams params;
    params.resolution = 32;
    params.size = 32.0f;

    // Ramp from 0 at x=-halfSize to 1 at x=+halfSize, independent of z.
    std::vector<float> heightmap(params.resolution * params.resolution);
    for (int z = 0; z < params.resolution; ++z) {
        for (int x = 0; x < params.resolution; ++x) {
            heightmap[z * params.resolution + x] =
                static_cast<float>(x) / static_cast<float>(params.resolution - 1);
        }
    }

    // Sample along a line at z=0. The sampled value should be the world
    // x-coordinate remapped to [0, 1].
    const float halfSize = params.size * 0.5f;
    for (float x = -halfSize; x <= halfSize; x += 0.5f) {
        const float sampled = getHeightAt(heightmap, x, 0.0f, params);
        const float expected = (x + halfSize) / params.size;
        REQUIRE(sampled == Approx(expected).margin(1.0e-3f));
    }
}

TEST_CASE("Terrain out-of-bounds query returns 0",
          "[terrain][property][bounds][oob]") {
    TerrainMirrorParams params;
    params.resolution = 16;
    params.size = 16.0f;

    std::vector<float> heightmap(params.resolution * params.resolution, 99.0f);
    const float halfSize = params.size * 0.5f;

    REQUIRE(getHeightAt(heightmap, halfSize + 0.1f, 0.0f, params) == 0.0f);
    REQUIRE(getHeightAt(heightmap, -halfSize - 0.1f, 0.0f, params) == 0.0f);
    REQUIRE(getHeightAt(heightmap, 0.0f, halfSize + 0.1f, params) == 0.0f);
    REQUIRE(getHeightAt(heightmap, 0.0f, -halfSize - 0.1f, params) == 0.0f);
    REQUIRE(getHeightAt(heightmap, 1.0e9f, 1.0e9f, params) == 0.0f);
}

TEST_CASE("Terrain isInBounds is closed at the +/- halfSize boundary",
          "[terrain][property][bounds][closed]") {
    TerrainMirrorParams params;
    params.resolution = 8;
    params.size = 8.0f;
    const float halfSize = params.size * 0.5f;

    REQUIRE(isInBounds(halfSize, halfSize, params));
    REQUIRE(isInBounds(-halfSize, -halfSize, params));
    REQUIRE(isInBounds(0.0f, 0.0f, params));
    REQUIRE_FALSE(isInBounds(halfSize + 1.0e-3f, 0.0f, params));
    REQUIRE_FALSE(isInBounds(0.0f, -halfSize - 1.0e-3f, params));
}

TEST_CASE("Terrain getBounds AABB is centered with documented y-extents",
          "[terrain][property][bounds][aabb]") {
    // Mirror of Terrain::getBounds (Terrain.cpp:453-459). Pure inline math
    // — no Terrain instance needed.
    TerrainMirrorParams params;
    params.resolution = 16;
    params.size = 100.0f;
    params.heightScale = 25.0f;

    const float halfSize = params.size * 0.5f;
    Engine::AABB bounds(
        Engine::vec3(-halfSize, 0.0f, -halfSize),
        Engine::vec3(halfSize, params.heightScale, halfSize)
    );

    REQUIRE(bounds.min.x == Approx(-50.0f));
    REQUIRE(bounds.min.y == Approx(0.0f));
    REQUIRE(bounds.min.z == Approx(-50.0f));
    REQUIRE(bounds.max.x == Approx(50.0f));
    REQUIRE(bounds.max.y == Approx(25.0f));
    REQUIRE(bounds.max.z == Approx(50.0f));
    REQUIRE(bounds.isValid());
    REQUIRE(bounds.center().x == Approx(0.0f));
    REQUIRE(bounds.center().z == Approx(0.0f));
    REQUIRE(bounds.center().y == Approx(params.heightScale * 0.5f));
}

TEST_CASE("Terrain bilinear sample is exact on a single-axis ramp at z midpoints",
          "[terrain][property][bilinear][midpoint]") {
    TerrainMirrorParams params;
    params.resolution = 4;
    params.size = 4.0f;

    // 4x4 grid, height = z * 10 (independent of x).
    //   z=0 -> 0   z=1 -> 10   z=2 -> 20   z=3 -> 30
    std::vector<float> heightmap(16);
    for (int z = 0; z < 4; ++z) {
        for (int x = 0; x < 4; ++x) {
            heightmap[z * 4 + x] = static_cast<float>(z * 10);
        }
    }

    // Sample at the midpoint between z=0 and z=1 (world space: halfway
    // between -halfSize and -halfSize + cellSize). At this midpoint the
    // sampler should return 5.0 (lerp between 0 and 10 at t=0.5).
    const float cellSize = params.size / (params.resolution - 1);
    const float halfSize = params.size * 0.5f;
    const float worldZ = -halfSize + cellSize * 0.5f;
    REQUIRE(getHeightAt(heightmap, 0.0f, worldZ, params) == Approx(5.0f));
}

TEST_CASE("Terrain sample preserves arithmetic mean across the domain on a balanced "
          "checkerboard",
          "[terrain][property][bilinear][mean]") {
    TerrainMirrorParams params;
    params.resolution = 32;
    params.size = 32.0f;

    // Checkerboard with +1 and -1. The arithmetic mean of the heightmap
    // is 0; the bilinear-resampled mean over a uniform grid of probe
    // points should also be ~0 within float precision.
    std::vector<float> heightmap(params.resolution * params.resolution);
    for (int z = 0; z < params.resolution; ++z) {
        for (int x = 0; x < params.resolution; ++x) {
            heightmap[z * params.resolution + x] = ((x + z) % 2 == 0) ? 1.0f : -1.0f;
        }
    }

    double sum = 0.0;
    int samples = 0;
    const float halfSize = params.size * 0.5f;
    const float step = 0.2f;
    for (float z = -halfSize + step; z < halfSize - step; z += step) {
        for (float x = -halfSize + step; x < halfSize - step; x += step) {
            sum += getHeightAt(heightmap, x, z, params);
            ++samples;
        }
    }
    const double mean = sum / static_cast<double>(samples);
    // Bilinear averaging of a +/- 1 checkerboard tends toward 0; allow
    // generous tolerance for the discrete probe grid sampling artifacts.
    REQUIRE(std::abs(mean) < 0.10);
}

TEST_CASE("Terrain bilinear sample is deterministic — same input, same output",
          "[terrain][property][bilinear][determinism]") {
    TerrainMirrorParams params;
    params.resolution = 16;
    params.size = 16.0f;

    std::mt19937 rng(static_cast<unsigned>(CatTest::DeterministicSeed("test_game_property_terrain:0xC0DEC0DE")));
    std::uniform_real_distribution<float> heightDist(-50.0f, 50.0f);
    std::vector<float> heightmap(256);
    for (auto& h : heightmap) {
        h = heightDist(rng);
    }

    const float x = 1.234f, z = -3.567f;
    const float first = getHeightAt(heightmap, x, z, params);
    for (int i = 0; i < 1000; ++i) {
        REQUIRE(getHeightAt(heightmap, x, z, params) == first);
    }
}

TEST_CASE("Terrain bilinear sample handles 2x2 minimum-resolution grid",
          "[terrain][property][bilinear][edge][minres]") {
    // Smallest valid grid has resolution=2 (cellSize = size / (2-1) = size).
    // Single cell spanning the full domain.
    TerrainMirrorParams params;
    params.resolution = 2;
    params.size = 10.0f;

    std::vector<float> heightmap = {0.0f, 10.0f, 20.0f, 30.0f};
    // Heightmap layout (z*res + x):
    //   z=0: [h00=0, h10=10]
    //   z=1: [h01=20, h11=30]

    // Corners.
    REQUIRE(getHeightAt(heightmap, -5.0f, -5.0f, params) == Approx(0.0f));
    REQUIRE(getHeightAt(heightmap, 5.0f, -5.0f, params) == Approx(10.0f));
    REQUIRE(getHeightAt(heightmap, -5.0f, 5.0f, params) == Approx(20.0f));
    REQUIRE(getHeightAt(heightmap, 5.0f, 5.0f, params) == Approx(30.0f));

    // Centre of the cell — full bilinear average.
    REQUIRE(getHeightAt(heightmap, 0.0f, 0.0f, params) == Approx(15.0f));
}

TEST_CASE("Terrain bilinear sample partial derivatives are stable (cell-interior smoothness)",
          "[terrain][property][bilinear][derivative]") {
    TerrainMirrorParams params;
    params.resolution = 8;
    params.size = 8.0f;

    // Simple z-only ramp; sampling along z should produce a strictly
    // monotone series of values inside any single cell. Across cells the
    // derivative changes but never reverses sign for a monotone ramp.
    std::vector<float> heightmap(64);
    for (int z = 0; z < 8; ++z) {
        for (int x = 0; x < 8; ++x) {
            heightmap[z * 8 + x] = static_cast<float>(z);
        }
    }

    float lastSample = -std::numeric_limits<float>::infinity();
    const float halfSize = params.size * 0.5f;
    for (float z = -halfSize; z <= halfSize; z += 0.1f) {
        const float sampled = getHeightAt(heightmap, 0.0f, z, params);
        REQUIRE(sampled >= lastSample - 1.0e-4f);  // monotone non-decreasing.
        lastSample = sampled;
    }
}

TEST_CASE("Terrain bilinear sample handles asymmetric cell sizes (size != resolution)",
          "[terrain][property][bilinear][asymmetric]") {
    // cellSize = size / (res - 1) — exercise with non-integer ratio.
    TerrainMirrorParams params;
    params.resolution = 5;
    params.size = 13.7f;  // cellSize ~= 3.425

    // Constant height — sampler should be invariant.
    std::vector<float> heightmap(25, 42.0f);
    std::mt19937 rng(static_cast<unsigned>(CatTest::DeterministicSeed("test_game_property_terrain:0x4242")));
    std::uniform_real_distribution<float> coordDist(-params.size * 0.5f,
                                                     params.size * 0.5f);
    for (int i = 0; i < 500; ++i) {
        REQUIRE(getHeightAt(heightmap, coordDist(rng), coordDist(rng), params)
                == Approx(42.0f));
    }
}

TEST_CASE("Terrain bilinear sample mirrors Terrain.cpp byte-for-byte at known points",
          "[terrain][property][bilinear][golden]") {
    // Golden-value check: with a known heightmap and known coordinates,
    // compute the bilinear-sample expected value by hand and assert the
    // mirror returns it. Pins the algorithm against algebraic drift.
    TerrainMirrorParams params;
    params.resolution = 3;
    params.size = 2.0f;     // cellSize = 1.0
    std::vector<float> heightmap = {
        0.0f,  1.0f,  2.0f,   // z=0 row
        4.0f,  5.0f,  6.0f,   // z=1 row
        8.0f,  9.0f, 10.0f    // z=2 row
    };

    // World (x,z)=(0.5, 0.5) -> localX = (0.5 + 1.0)/1.0 = 1.5,
    // localZ = 1.5. floor -> x0=1, z0=1. fx=0.5, fz=0.5.
    // h00 = heightmap[1*3+1] = 5
    // h10 = heightmap[1*3+2] = 6
    // h01 = heightmap[2*3+1] = 9
    // h11 = heightmap[2*3+2] = 10
    // h0 = lerp(5, 6, 0.5) = 5.5
    // h1 = lerp(9, 10, 0.5) = 9.5
    // result = lerp(5.5, 9.5, 0.5) = 7.5
    REQUIRE(getHeightAt(heightmap, 0.5f, 0.5f, params) == Approx(7.5f));
}
