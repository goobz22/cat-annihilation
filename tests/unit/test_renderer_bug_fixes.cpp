/**
 * @file test_renderer_bug_fixes.cpp
 * @brief Host-testable Catch2 coverage for the 2026-05 renderer bug-hunt.
 *
 * Each TEST_CASE locks down one fix landed in the renderer territory pass:
 *
 *   1. Camera::SetPerspective + SetClipPlanes input validation.
 *      Pre-fix: a zero/negative near plane or far <= near divided by zero
 *      or produced an inverted depth range in mat4::perspective. Post-fix:
 *      the setter clamps to a positive minimum near and far > near so the
 *      projection matrix is always well-formed regardless of caller input.
 *
 *   2. Mesh::OptimizeIndices submesh-bounds-check.
 *      Pre-fix: a submesh with indexOffset + indexCount > indices.size()
 *      handed the optimizer an out-of-bounds pointer/length, causing a
 *      buffer overflow write back into mesh memory. Post-fix: the
 *      optimizer skips any submesh whose range exceeds the index buffer,
 *      leaving the rest of the mesh correctly reordered.
 *
 *   3. Mesh::GenerateTangents degenerate-UV guard.
 *      Pre-fix: a triangle whose UV-edge determinant was zero divided by
 *      zero and produced ±infinity / NaN tangents that propagated through
 *      normalize() into a 0-length tangent. Post-fix: degenerate triangles
 *      are skipped and the per-vertex accumulator stays finite.
 *
 *   4. ImageCompare::SSIM on tiny images.
 *      Window-size clamping must produce a finite score for any image
 *      with at least 2x2 pixels — pre-2026-05 the clamp was already
 *      present but had no test guarding the contract.
 *
 * Pure STL + engine math + Mesh.hpp / Camera.hpp / ImageCompare.hpp — no
 * Vulkan, no CUDA, no GPU. Links into the no-GPU host test build the same
 * way test_mesh_optimizer / test_image_compare / test_oit_weight do.
 */

#include "catch.hpp"

#include "engine/renderer/Camera.hpp"
#include "engine/renderer/ImageCompare.hpp"
#include "engine/renderer/Mesh.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

using namespace CatEngine::Renderer;

// =============================================================================
// 1. Camera::SetPerspective + SetClipPlanes input validation
// =============================================================================

TEST_CASE("Camera SetPerspective clamps zero or negative near plane",
          "[renderer][camera]") {
    Camera camera;
    // Zero near — pre-fix would divide by zero in mat4::perspective.
    camera.SetPerspective(/*fov*/ 1.0f, /*aspect*/ 16.0f / 9.0f,
                          /*near*/ 0.0f, /*far*/ 100.0f);
    REQUIRE(camera.GetNearPlane() > 0.0f);
    REQUIRE(camera.GetFarPlane() > camera.GetNearPlane());

    // Negative near — same pathological-projection trigger.
    camera.SetPerspective(1.0f, 16.0f / 9.0f, -1.0f, 100.0f);
    REQUIRE(camera.GetNearPlane() > 0.0f);
    REQUIRE(camera.GetFarPlane() > camera.GetNearPlane());
}

TEST_CASE("Camera SetPerspective fixes near>=far swap and equal-plane",
          "[renderer][camera]") {
    Camera camera;
    // far == near collapses the entire depth range to one plane.
    camera.SetPerspective(1.0f, 1.0f, 1.0f, 1.0f);
    REQUIRE(camera.GetFarPlane() > camera.GetNearPlane());

    // Swapped (far < near) — flips depth handedness.
    camera.SetPerspective(1.0f, 1.0f, 100.0f, 1.0f);
    REQUIRE(camera.GetFarPlane() > camera.GetNearPlane());
}

TEST_CASE("Camera SetPerspective clamps invalid aspect ratio",
          "[renderer][camera]") {
    Camera camera;
    // Zero aspect (minimized window with width=0) would zero out the x scale.
    camera.SetPerspective(1.0f, 0.0f, 0.1f, 100.0f);
    REQUIRE(camera.GetAspectRatio() > 0.0f);

    // Negative aspect would mirror the world horizontally — recoverable
    // when window is restored only if the setter clamps it.
    camera.SetPerspective(1.0f, -16.0f / 9.0f, 0.1f, 100.0f);
    REQUIRE(camera.GetAspectRatio() > 0.0f);
}

TEST_CASE("Camera SetClipPlanes guards zero near and swap", "[renderer][camera]") {
    Camera camera;
    // Default state is perspective; the setter should refuse to land near<=0.
    camera.SetClipPlanes(0.0f, 50.0f);
    REQUIRE(camera.GetNearPlane() > 0.0f);
    REQUIRE(camera.GetFarPlane() > camera.GetNearPlane());

    // far <= near — depth range must remain strictly positive.
    camera.SetClipPlanes(5.0f, 5.0f);
    REQUIRE(camera.GetFarPlane() > camera.GetNearPlane());

    camera.SetClipPlanes(50.0f, 1.0f);
    REQUIRE(camera.GetFarPlane() > camera.GetNearPlane());
}

TEST_CASE("Camera SetPerspective accepts legitimate inputs unchanged",
          "[renderer][camera]") {
    Camera camera;
    camera.SetPerspective(1.0f, 16.0f / 9.0f, 0.1f, 1000.0f);
    REQUIRE(camera.GetNearPlane() == Approx(0.1f));
    REQUIRE(camera.GetFarPlane() == Approx(1000.0f));
    REQUIRE(camera.GetAspectRatio() == Approx(16.0f / 9.0f));
    REQUIRE(camera.GetFieldOfView() == Approx(1.0f));
}

// =============================================================================
// 2. Mesh::OptimizeIndices submesh-bounds-check
// =============================================================================

namespace {

// Build a tiny synthetic mesh: 4 vertices forming a quad, 6 indices = 2 tris.
// No UVs or normals needed — OptimizeIndices only reads the index buffer and
// the vertex count.
Mesh MakeQuadMesh() {
    Mesh mesh;
    mesh.vertices.resize(4);
    mesh.vertices[0].position = Engine::vec3(0.0f, 0.0f, 0.0f);
    mesh.vertices[1].position = Engine::vec3(1.0f, 0.0f, 0.0f);
    mesh.vertices[2].position = Engine::vec3(1.0f, 1.0f, 0.0f);
    mesh.vertices[3].position = Engine::vec3(0.0f, 1.0f, 0.0f);
    mesh.indices = {0, 1, 2, 2, 3, 0};
    return mesh;
}

} // namespace

TEST_CASE("Mesh OptimizeIndices skips out-of-bounds submesh range",
          "[renderer][mesh]") {
    Mesh mesh = MakeQuadMesh();

    // Submesh whose range overflows the index buffer.
    // Pre-fix: the optimizer wrote into indices.data() + 0 for 100 entries.
    Submesh bad(/*offset*/ 0, /*count*/ 100, /*matIndex*/ 0);
    mesh.submeshes.push_back(bad);

    const std::vector<uint32_t> indicesBefore = mesh.indices;
    REQUIRE_NOTHROW(mesh.OptimizeIndices());
    // The bad submesh must be skipped — index buffer is left untouched.
    REQUIRE(mesh.indices == indicesBefore);
}

TEST_CASE("Mesh OptimizeIndices skips submesh whose offset exceeds index buffer",
          "[renderer][mesh]") {
    Mesh mesh = MakeQuadMesh();
    Submesh bad(/*offset*/ 1000, /*count*/ 3, /*matIndex*/ 0);
    mesh.submeshes.push_back(bad);

    REQUIRE_NOTHROW(mesh.OptimizeIndices());
    REQUIRE(mesh.indices.size() == 6);
}

TEST_CASE("Mesh OptimizeIndices preserves triangle set for valid submesh",
          "[renderer][mesh]") {
    Mesh mesh = MakeQuadMesh();
    // Valid submesh covering both triangles.
    Submesh ok(/*offset*/ 0, /*count*/ 6, /*matIndex*/ 0);
    mesh.submeshes.push_back(ok);

    REQUIRE_NOTHROW(mesh.OptimizeIndices());
    REQUIRE(mesh.indices.size() == 6);
    // Both triangles must still reference vertices 0..3.
    for (uint32_t idx : mesh.indices) {
        REQUIRE(idx < mesh.vertices.size());
    }
}

// =============================================================================
// 3. Mesh::GenerateTangents degenerate-UV guard
// =============================================================================

TEST_CASE("Mesh GenerateTangents handles degenerate UV triangle",
          "[renderer][mesh]") {
    Mesh mesh;
    mesh.vertices.resize(3);

    // Three position-distinct vertices that all share the same UV — the
    // UV-edge determinant is exactly zero. Pre-fix: divide-by-zero produced
    // NaN/inf tangents that survived normalize() as 0-length.
    mesh.vertices[0].position = Engine::vec3(0.0f, 0.0f, 0.0f);
    mesh.vertices[1].position = Engine::vec3(1.0f, 0.0f, 0.0f);
    mesh.vertices[2].position = Engine::vec3(0.0f, 1.0f, 0.0f);
    mesh.vertices[0].normal = Engine::vec3(0.0f, 0.0f, 1.0f);
    mesh.vertices[1].normal = Engine::vec3(0.0f, 0.0f, 1.0f);
    mesh.vertices[2].normal = Engine::vec3(0.0f, 0.0f, 1.0f);
    mesh.vertices[0].uv0 = Engine::vec2(0.5f, 0.5f);
    mesh.vertices[1].uv0 = Engine::vec2(0.5f, 0.5f);
    mesh.vertices[2].uv0 = Engine::vec2(0.5f, 0.5f);
    mesh.indices = {0, 1, 2};

    REQUIRE_NOTHROW(mesh.GenerateTangents());
    // Every tangent component must be finite (no NaN, no inf) even though
    // the degenerate triangle contributes nothing.
    for (const auto& vertex : mesh.vertices) {
        REQUIRE(std::isfinite(vertex.tangent.x));
        REQUIRE(std::isfinite(vertex.tangent.y));
        REQUIRE(std::isfinite(vertex.tangent.z));
        REQUIRE(std::isfinite(vertex.tangent.w));
    }
}

TEST_CASE("Mesh GenerateTangents produces finite results for a well-formed quad",
          "[renderer][mesh]") {
    Mesh mesh = Mesh::CreateQuad();
    // Quad already has valid UVs that span [0,1] — the Lengyel solve must
    // produce finite, normalised tangents at every vertex.
    REQUIRE_NOTHROW(mesh.GenerateTangents());
    for (const auto& vertex : mesh.vertices) {
        REQUIRE(std::isfinite(vertex.tangent.x));
        REQUIRE(std::isfinite(vertex.tangent.y));
        REQUIRE(std::isfinite(vertex.tangent.z));
        // Handedness is encoded in w and must be ±1.
        REQUIRE((vertex.tangent.w == Approx(1.0f) ||
                 vertex.tangent.w == Approx(-1.0f)));
    }
}

// =============================================================================
// 4. ImageCompare::SSIM on tiny images
// =============================================================================

TEST_CASE("ImageCompare SSIM clamps window size to small images",
          "[renderer][image_compare]") {
    // 4x4 solid grey — window=8 would step past the image without the
    // clamp. We compare an image against itself; the score should be 1.0.
    auto identical = ImageCompare::SolidColor(4, 4, 128, 128, 128);
    const double score = ImageCompare::SSIM(identical, identical,
                                            /*windowSize*/ 8);
    REQUIRE(!std::isnan(score));
    REQUIRE(score == Approx(1.0).margin(1e-9));
}

TEST_CASE("ImageCompare SSIM returns NaN for degenerate 1x1 images",
          "[renderer][image_compare]") {
    // 1x1 images cannot produce a 2x2 variance window, so the guard in
    // SSIM (windowSize >= 2 required) must surface NaN rather than
    // dividing by zero in the variance step.
    auto onePixel = ImageCompare::SolidColor(1, 1, 200, 100, 50);
    const double score = ImageCompare::SSIM(onePixel, onePixel, 8);
    REQUIRE(std::isnan(score));
}
