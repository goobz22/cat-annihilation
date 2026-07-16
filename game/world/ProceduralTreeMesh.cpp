#include "ProceduralTreeMesh.hpp"

#include <cmath>
#include <utility>

namespace CatGame {

namespace {

constexpr float kPi = 3.14159265358979323846F;

// ---------------------------------------------------------------------
// Cylinder builder
// ---------------------------------------------------------------------
//
// Generates a tapered cylinder centred on the +Y axis. Parameters mirror
// THREE.CylinderGeometry (top, bottom, height, radial segments, height
// segments=1, openEnded=false) so the visual matches ForestEnvironment.tsx
// when the local mesh is offset by `position={[0, height/2, 0]}` (which
// yields y∈[0, height] in world space — same as the web port).
//
// The mesh has THREE vertex rings:
//   - top ring   (y = +height/2, radius = topRadius)
//   - bottom ring (y = -height/2, radius = bottomRadius)
//   - top + bottom cap centres (single vertex each)
//
// Side triangulation: each radial segment forms a quad between the top
// and bottom ring vertex, split into two triangles. Cap triangulation:
// a fan from the cap centre to each pair of adjacent ring vertices.
//
// Normals on the side: a true tapered cylinder has a slight downward
// component (the surface tilts inward as it rises), so we compute the
// per-vertex side normal as a slope-corrected outward direction. For
// the cap triangles the normal is pure ±Y. The web port uses
// meshStandardMaterial with default smooth shading, so we emit smooth
// (per-vertex) normals on the side and flat normals on the caps.
//
// Texcoords: the cylinder maps u along the angular direction (0..1
// around) and v along the height (0 at bottom, 1 at top). Caps get a
// projected uv (centre at 0.5,0.5, radius going outward). This matches
// THREE's default cylinder UV layout.
static void AppendCylinder(CatEngine::Mesh& mesh,
                           float topRadius,
                           float bottomRadius,
                           float height,
                           int radialSegments,
                           float yCentre)
{
    const float halfHeight = height * 0.5F;
    // Side-normal slope correction. The cylinder's slant line in the YR
    // plane has dy/dr = height / (bottomRadius - topRadius) when
    // bottomRadius > topRadius. The outward-pointing normal in 2D is
    // perpendicular to that slant: nR = dy / hypot(dy, dr), nY = dr /
    // hypot(dy, dr). For a non-tapered cylinder (bottomRadius == topRadius)
    // the Y component collapses to zero and the normal is purely radial,
    // which is the right limit. We compute it once outside the loop.
    const float radiusDelta = bottomRadius - topRadius;
    const float slantHypot  = std::sqrt(radiusDelta * radiusDelta + height * height);
    // Defensive: a degenerate (zero-height or zero-radius-everywhere)
    // cylinder would produce a hypot of 0; the caller's defaults preclude
    // this, but the guard keeps a future bad parameter out of NaN-land.
    const float invHypot   = (slantHypot > 1e-6F) ? (1.0F / slantHypot) : 1.0F;
    const float sideNormalY = radiusDelta * invHypot; // small +ve when tapered

    const uint32_t baseIndex = static_cast<uint32_t>(mesh.vertices.size());

    // ---- Side ring vertices (top then bottom, one pair per segment+1) ----
    // Using (radialSegments + 1) vertices per ring lets the seam vertex
    // exist twice with u=0 and u=1, so the texture wraps cleanly.
    for (int seg = 0; seg <= radialSegments; ++seg) {
        const float u = static_cast<float>(seg) / static_cast<float>(radialSegments);
        const float angle = u * 2.0F * kPi;
        const float cosA = std::cos(angle);
        const float sinA = std::sin(angle);

        // Top ring vertex
        CatEngine::Vertex vTop{};
        vTop.position = glm::vec3(cosA * topRadius,
                                   yCentre + halfHeight,
                                   sinA * topRadius);
        vTop.normal   = glm::vec3(cosA * invHypot * height,  // radial × cos(slant)
                                   sideNormalY,
                                   sinA * invHypot * height);
        vTop.texcoord0 = glm::vec2(u, 1.0F);
        mesh.vertices.push_back(vTop);

        // Bottom ring vertex (same uv x, v=0)
        CatEngine::Vertex vBottom{};
        vBottom.position = glm::vec3(cosA * bottomRadius,
                                      yCentre - halfHeight,
                                      sinA * bottomRadius);
        vBottom.normal   = glm::vec3(cosA * invHypot * height,
                                      sideNormalY,
                                      sinA * invHypot * height);
        vBottom.texcoord0 = glm::vec2(u, 0.0F);
        mesh.vertices.push_back(vBottom);
    }

    // ---- Side triangle indices ----
    // Each pair (top_i, bot_i, top_{i+1}, bot_{i+1}) yields two triangles
    // wound CCW when viewed from outside the cylinder.
    for (int seg = 0; seg < radialSegments; ++seg) {
        const uint32_t topI    = baseIndex + static_cast<uint32_t>(seg * 2 + 0);
        const uint32_t botI    = baseIndex + static_cast<uint32_t>(seg * 2 + 1);
        const uint32_t topI1   = baseIndex + static_cast<uint32_t>((seg + 1) * 2 + 0);
        const uint32_t botI1   = baseIndex + static_cast<uint32_t>((seg + 1) * 2 + 1);

        // Tri 1: topI, botI, botI1
        mesh.indices.push_back(topI);
        mesh.indices.push_back(botI);
        mesh.indices.push_back(botI1);
        // Tri 2: topI, botI1, topI1
        mesh.indices.push_back(topI);
        mesh.indices.push_back(botI1);
        mesh.indices.push_back(topI1);
    }

    // ---- Top cap fan ----
    // Centre vertex first, then a ring of cap vertices (separate copy
    // from the side ring so cap normal is +Y while side normal slopes).
    const uint32_t topCentreIndex = static_cast<uint32_t>(mesh.vertices.size());
    {
        CatEngine::Vertex centre{};
        centre.position = glm::vec3(0.0F, yCentre + halfHeight, 0.0F);
        centre.normal   = glm::vec3(0.0F, 1.0F, 0.0F);
        centre.texcoord0 = glm::vec2(0.5F, 0.5F);
        mesh.vertices.push_back(centre);
    }
    const uint32_t topRingFirst = static_cast<uint32_t>(mesh.vertices.size());
    for (int seg = 0; seg <= radialSegments; ++seg) {
        const float u = static_cast<float>(seg) / static_cast<float>(radialSegments);
        const float angle = u * 2.0F * kPi;
        const float cosA = std::cos(angle);
        const float sinA = std::sin(angle);

        CatEngine::Vertex v{};
        v.position = glm::vec3(cosA * topRadius,
                                yCentre + halfHeight,
                                sinA * topRadius);
        v.normal   = glm::vec3(0.0F, 1.0F, 0.0F);
        // Cap UV: planar projection — centre at (0.5, 0.5), perimeter
        // mapped onto a unit disk inside the unit square.
        v.texcoord0 = glm::vec2(0.5F + cosA * 0.5F, 0.5F + sinA * 0.5F);
        mesh.vertices.push_back(v);
    }
    for (int seg = 0; seg < radialSegments; ++seg) {
        // Wind so the +Y face is visible from above — CCW when viewed
        // from +Y looking down toward origin means the order is
        // (centre, ring[i+1], ring[i]).
        mesh.indices.push_back(topCentreIndex);
        mesh.indices.push_back(topRingFirst + static_cast<uint32_t>(seg + 1));
        mesh.indices.push_back(topRingFirst + static_cast<uint32_t>(seg));
    }

    // ---- Bottom cap fan ----
    const uint32_t bottomCentreIndex = static_cast<uint32_t>(mesh.vertices.size());
    {
        CatEngine::Vertex centre{};
        centre.position = glm::vec3(0.0F, yCentre - halfHeight, 0.0F);
        centre.normal   = glm::vec3(0.0F, -1.0F, 0.0F);
        centre.texcoord0 = glm::vec2(0.5F, 0.5F);
        mesh.vertices.push_back(centre);
    }
    const uint32_t bottomRingFirst = static_cast<uint32_t>(mesh.vertices.size());
    for (int seg = 0; seg <= radialSegments; ++seg) {
        const float u = static_cast<float>(seg) / static_cast<float>(radialSegments);
        const float angle = u * 2.0F * kPi;
        const float cosA = std::cos(angle);
        const float sinA = std::sin(angle);

        CatEngine::Vertex v{};
        v.position = glm::vec3(cosA * bottomRadius,
                                yCentre - halfHeight,
                                sinA * bottomRadius);
        v.normal   = glm::vec3(0.0F, -1.0F, 0.0F);
        v.texcoord0 = glm::vec2(0.5F + cosA * 0.5F, 0.5F + sinA * 0.5F);
        mesh.vertices.push_back(v);
    }
    for (int seg = 0; seg < radialSegments; ++seg) {
        // Wind so -Y face is visible from below.
        mesh.indices.push_back(bottomCentreIndex);
        mesh.indices.push_back(bottomRingFirst + static_cast<uint32_t>(seg));
        mesh.indices.push_back(bottomRingFirst + static_cast<uint32_t>(seg + 1));
    }
}

// ---------------------------------------------------------------------
// UV-sphere builder
// ---------------------------------------------------------------------
//
// Generates a sphere via the classic latitude (y) × longitude (xz)
// parameterisation, matching THREE.SphereGeometry's default
// (widthSegments × heightSegments). All vertices have the same outward
// normal as their position (sphere normal == normalised position) so
// smooth shading is automatic. UVs map u along longitude and v along
// latitude (both in [0, 1]).
//
// The web port uses 12×12 for pine/oak and 8×8 for bushes; we honour
// that by passing the segment counts through.
static void AppendSphere(CatEngine::Mesh& mesh,
                         float radius,
                         int widthSegments,
                         int heightSegments,
                         const glm::vec3& centre)
{
    const uint32_t baseIndex = static_cast<uint32_t>(mesh.vertices.size());

    for (int latIndex = 0; latIndex <= heightSegments; ++latIndex) {
        // Latitude angle ranges from 0 (north pole, +Y) to PI (south pole, -Y).
        const float v = static_cast<float>(latIndex) / static_cast<float>(heightSegments);
        const float phi = v * kPi;
        const float sinPhi = std::sin(phi);
        const float cosPhi = std::cos(phi);

        for (int lonIndex = 0; lonIndex <= widthSegments; ++lonIndex) {
            const float u = static_cast<float>(lonIndex) / static_cast<float>(widthSegments);
            const float theta = u * 2.0F * kPi;
            const float sinTheta = std::sin(theta);
            const float cosTheta = std::cos(theta);

            const glm::vec3 localPos(sinPhi * cosTheta,
                                      cosPhi,
                                      sinPhi * sinTheta);

            CatEngine::Vertex vert{};
            vert.position = centre + localPos * radius;
            // Sphere normals are the un-scaled, un-translated direction
            // from centre to surface — already unit length.
            vert.normal = localPos;
            // Mirror THREE's default sphere UV layout: u inverted along
            // longitude isn't strictly required for the unlit-ish
            // procedural path, but matching the web port's UV convention
            // means a future texture swap reads identically on both sides.
            vert.texcoord0 = glm::vec2(u, 1.0F - v);
            mesh.vertices.push_back(vert);
        }
    }

    const uint32_t stride = static_cast<uint32_t>(widthSegments + 1);
    for (int latIndex = 0; latIndex < heightSegments; ++latIndex) {
        for (int lonIndex = 0; lonIndex < widthSegments; ++lonIndex) {
            const uint32_t a = baseIndex +
                static_cast<uint32_t>(latIndex) * stride +
                static_cast<uint32_t>(lonIndex);
            const uint32_t b = a + stride;
            const uint32_t c = a + 1U;
            const uint32_t d = b + 1U;

            // Skip degenerate triangles at the poles where one edge of
            // the quad collapses. Latitude 0 (north pole) has all top
            // vertices coincident; latitude heightSegments-1 (south
            // pole) has all bottom vertices coincident. THREE normally
            // emits these zero-area triangles too — they cost a few
            // extra GPU verts but the index buffer logic is simpler.
            mesh.indices.push_back(a);
            mesh.indices.push_back(b);
            mesh.indices.push_back(c);

            mesh.indices.push_back(c);
            mesh.indices.push_back(b);
            mesh.indices.push_back(d);
        }
    }
}

// Wrap a single Mesh into a Model. The bind-pose render path consumes
// `model->meshes[0]` directly via EnsureModelGpuMesh — no nodes, no
// animations, no skinning skeleton needed. We DO populate
// boundsMin/boundsMax so any future code that iterates Mesh bounds
// (e.g. AABB-based culling, debug overlays) doesn't see zero-bounds
// Faceted dodecahedron matching three.js `DodecahedronGeometry(radius, 0)`
// (the web port's rock, ForestEnvironment.tsx:211-226). three's
// PolyhedronGeometry normalises the 20 canonical vertices onto the
// radius sphere and emits NON-indexed triangles, which gives each
// triangle a constant (face) normal — the faceted read that makes a
// half-metre prop look like a rock instead of a pebble-smooth ball. We
// reproduce that by duplicating the three corner vertices per triangle
// and assigning them the face normal.
static void AppendDodecahedron(CatEngine::Mesh& mesh,
                               float radius,
                               const glm::vec3& centre)
{
    // Canonical dodecahedron vertex table (three.js DodecahedronGeometry
    // source): the 8 cube corners (±1,±1,±1) plus the 12 rectangle
    // vertices built from the golden ratio t and its reciprocal r.
    const float t = (1.0F + std::sqrt(5.0F)) / 2.0F;
    const float r = 1.0F / t;
    const glm::vec3 canonical[20] = {
        {-1, -1, -1}, {-1, -1,  1}, {-1,  1, -1}, {-1,  1,  1},
        { 1, -1, -1}, { 1, -1,  1}, { 1,  1, -1}, { 1,  1,  1},
        { 0, -r, -t}, { 0, -r,  t}, { 0,  r, -t}, { 0,  r,  t},
        {-r, -t,  0}, {-r,  t,  0}, { r, -t,  0}, { r,  t,  0},
        {-t,  0, -r}, { t,  0, -r}, {-t,  0,  r}, { t,  0,  r},
    };
    // 36 triangles = 12 pentagonal faces × 3 fan triangles, in three.js's
    // published index order so the silhouette matches the web rock
    // exactly frame-for-frame in side-by-side captures.
    static constexpr uint32_t kIndices[36 * 3] = {
        3, 11, 7,   3, 7, 15,   3, 15, 13,
        7, 19, 17,  7, 17, 6,   7, 6, 15,
        17, 4, 8,   17, 8, 10,  17, 10, 6,
        8, 0, 16,   8, 16, 2,   8, 2, 10,
        0, 12, 1,   0, 1, 18,   0, 18, 16,
        6, 10, 2,   6, 2, 13,   6, 13, 15,
        2, 16, 18,  2, 18, 3,   2, 3, 13,
        18, 1, 9,   18, 9, 11,  18, 11, 3,
        4, 14, 12,  4, 12, 0,   4, 0, 8,
        11, 9, 5,   11, 5, 19,  11, 19, 7,
        19, 5, 14,  19, 14, 4,  19, 4, 17,
        1, 12, 14,  1, 14, 5,   1, 5, 9,
    };

    for (int tri = 0; tri < 36; ++tri) {
        glm::vec3 corners[3];
        for (int lane = 0; lane < 3; ++lane) {
            // Normalise onto the radius sphere — the canonical table has
            // mixed vertex magnitudes (√3 for cube corners, √(r²+t²) for
            // the rectangles); three.js projects them all to `radius`.
            corners[lane] =
                glm::normalize(canonical[kIndices[tri * 3 + lane]]) * radius;
        }

        glm::vec3 faceNormal = glm::cross(corners[1] - corners[0],
                                          corners[2] - corners[0]);
        // Orient outward: for a convex solid centred at the origin the
        // face normal must point away from the centre. This guards the
        // hand-copied index table against a winding slip — a flipped
        // face would be backface-culled into a hole in the rock.
        const glm::vec3 faceCentroid =
            (corners[0] + corners[1] + corners[2]) / 3.0F;
        if (glm::dot(faceNormal, faceCentroid) < 0.0F) {
            std::swap(corners[1], corners[2]);
            faceNormal = -faceNormal;
        }
        faceNormal = glm::normalize(faceNormal);

        const uint32_t firstIndex = static_cast<uint32_t>(mesh.vertices.size());
        for (int lane = 0; lane < 3; ++lane) {
            CatEngine::Vertex vert{};
            vert.position = centre + corners[lane];
            vert.normal = faceNormal;
            // Planar UV projection — the rock is flat-colored (#777777
            // via EntityDraw::color) so the coordinates only need to be
            // finite, not artistic.
            vert.texcoord0 = glm::vec2(corners[lane].x / radius * 0.5F + 0.5F,
                                       corners[lane].z / radius * 0.5F + 0.5F);
            mesh.vertices.push_back(vert);
            mesh.indices.push_back(firstIndex + static_cast<uint32_t>(lane));
        }
    }
}

// AABBs collapsed at the origin.
static std::shared_ptr<CatEngine::Model> WrapMeshAsModel(CatEngine::Mesh&& mesh) {
    if (!mesh.vertices.empty()) {
        glm::vec3 minPos = mesh.vertices.front().position;
        glm::vec3 maxPos = mesh.vertices.front().position;
        for (const auto& v : mesh.vertices) {
            minPos = glm::min(minPos, v.position);
            maxPos = glm::max(maxPos, v.position);
        }
        mesh.boundsMin = minPos;
        mesh.boundsMax = maxPos;
    }

    auto model = std::make_shared<CatEngine::Model>();
    model->meshes.push_back(std::move(mesh));
    return model;
}

} // namespace

ProceduralTreeMeshes BuildProceduralTreeMeshes() {
    ProceduralTreeMeshes result;

    // Trunk: top radius 0.3, bottom radius 0.4, height 4, 8 segments.
    // yCentre 2 puts the cylinder at y∈[0, 4] in local space, matching
    // the web port `<mesh position={[0, 2, 0]}>` offset around a
    // `<cylinderGeometry args={[0.3, 0.4, 4, 8]}/>` (THREE places the
    // cylinder centred on its origin, so position=2 lifts the bottom
    // to y=0 and the top to y=4).
    {
        CatEngine::Mesh trunkMesh;
        trunkMesh.name = "tree_trunk_procedural";
        AppendCylinder(trunkMesh,
                       /*topRadius   */ 0.3F,
                       /*bottomRadius*/ 0.4F,
                       /*height      */ 4.0F,
                       /*radialSegs  */ 8,
                       /*yCentre     */ 2.0F);
        result.trunk = WrapMeshAsModel(std::move(trunkMesh));
    }

    // Pine foliage: sphere radius 2 at y=5, 12×12 segments.
    {
        CatEngine::Mesh foliageMesh;
        foliageMesh.name = "tree_pine_foliage_procedural";
        AppendSphere(foliageMesh,
                     /*radius   */ 2.0F,
                     /*widthSegs*/ 12,
                     /*heightSegs*/ 12,
                     /*centre   */ glm::vec3(0.0F, 5.0F, 0.0F));
        result.pineFoliage = WrapMeshAsModel(std::move(foliageMesh));
    }

    // Oak foliage: sphere radius 2.5 at y=5, 12×12 segments.
    {
        CatEngine::Mesh foliageMesh;
        foliageMesh.name = "tree_oak_foliage_procedural";
        AppendSphere(foliageMesh,
                     /*radius   */ 2.5F,
                     /*widthSegs*/ 12,
                     /*heightSegs*/ 12,
                     /*centre   */ glm::vec3(0.0F, 5.0F, 0.0F));
        result.oakFoliage = WrapMeshAsModel(std::move(foliageMesh));
    }

    // Bush: sphere radius 0.7 at y=0, 8×8 segments.
    {
        CatEngine::Mesh bushMesh;
        bushMesh.name = "tree_bush_procedural";
        AppendSphere(bushMesh,
                     /*radius   */ 0.7F,
                     /*widthSegs*/ 8,
                     /*heightSegs*/ 8,
                     /*centre   */ glm::vec3(0.0F, 0.0F, 0.0F));
        result.bush = WrapMeshAsModel(std::move(bushMesh));
    }

    // Rock: faceted dodecahedron, circumradius 0.5, at the origin.
    {
        CatEngine::Mesh rockMesh;
        rockMesh.name = "rock_procedural";
        AppendDodecahedron(rockMesh,
                           /*radius*/ 0.5F,
                           /*centre*/ glm::vec3(0.0F, 0.0F, 0.0F));
        result.rock = WrapMeshAsModel(std::move(rockMesh));
    }

    return result;
}

} // namespace CatGame
