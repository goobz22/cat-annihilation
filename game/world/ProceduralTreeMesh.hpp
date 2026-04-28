#pragma once

#include "../../engine/assets/ModelLoader.hpp"
#include <memory>

namespace CatGame {

// 2026-04-26 SURVIVAL-PORT — procedural tree primitives that mirror the
// web-port ForestEnvironment.tsx geometry one-to-one.
//
// WHY procedural and not Meshy GLBs:
//   The Meshy props/tree_*.glb exports range from 23 MB (tree_dead_burnt)
//   to 469 MB (landmark_ancient_oak), with the standard pine + oak models
//   at 162 MB and 319 MB respectively. Meshy targets static rendering /
//   3D-print quality, not real-time games, so each export ships with
//   millions of vertices. The native engine has no decimation pipeline
//   yet, and a first integration attempt (cat-verify rows #104-#105)
//   measured fps=8.5 with the 80 m cull and fps=0.6 with a 35 m cull +
//   64-tree budget. Both unplayable.
//
//   The web port's ForestEnvironment.tsx renders trees as the simplest
//   possible geometry: an 8-segment cylinder trunk plus a 12×12 sphere
//   foliage. This file generates the SAME geometry server-side in C++,
//   producing CatEngine::Model instances the existing ScenePass model
//   path can render at full fps. The visual fidelity matches the web
//   port exactly; the engine's MeshSubmissionSystem retention ring keeps
//   the tree models alive across frames-in-flight.
//
// WHY one Model per primitive (trunk / pineFoliage / oakFoliage / bush)
// rather than one Model per tree-type:
//   The entity fragment shader takes a per-entity vec3 color (via the
//   ScenePass::EntityDraw color field) and the bind-pose mesh path
//   does NOT honour per-mesh materials — every mesh inside a Model is
//   drawn with the same EntityDraw color. So a "Pine model with trunk +
//   foliage merged" would render as one solid colour, losing the
//   trunk-brown / foliage-green split that makes a tree read as a tree.
//   Splitting into separate primitives lets each tree emit two
//   EntityDraws (one trunk + one foliage), each with its own colour,
//   sharing the same modelMatrix (translate-rotate-scale of the tree
//   instance). Doubles the draw count vs single-model, but a 64-tree
//   budget × 2 draws = 128 model draws per frame is well within the
//   budget the existing cat/dog mesh path already runs at.
//
// WHY hand-rolled instead of a library:
//   The engine has no shape-generation library and pulling one in for
//   a cylinder + sphere is gross over-engineering. The math is ~30
//   lines per primitive and the explicit loops document the geometry
//   for any future reader (this is a portfolio engine — see CLAUDE.md
//   "Robust explanatory comments on non-trivial logic. Explain the
//   WHY, not the WHAT").

struct ProceduralTreeMeshes {
    // Single trunk shape used by both pine and oak (web port also shares
    // the trunk JSX between types). Top radius 0.3, bottom radius 0.4,
    // height 4, 8 radial segments. Vertical extent y∈[0, 4] in local
    // space — matches the web-port `position={[0, 2, 0]}` group offset
    // applied to a height-4 cylinder centred on its midpoint.
    std::shared_ptr<CatEngine::Model> trunk;

    // Pine foliage: sphere radius 2, 12×12 segments, centred at (0, 5, 0)
    // in local space. Sits above the 4-tall trunk with a metre of
    // overlap so the trunk doesn't show through the canopy from the
    // sides.
    std::shared_ptr<CatEngine::Model> pineFoliage;

    // Oak foliage: sphere radius 2.5, 12×12 segments, centred at (0, 5, 0).
    // Same vertical position as pine; the wider radius gives oaks a
    // visibly broader canopy without changing the vertical proportions.
    std::shared_ptr<CatEngine::Model> oakFoliage;

    // Bush: sphere radius 0.7, 8×8 segments, centred at (0, 0, 0). The
    // web port renders bushes as a single low-poly sphere without a
    // trunk. Lower segment count (8 vs 12) is intentional — a 0.7 m
    // sphere at typical viewing distance reads fine as 8-segment, and
    // the lower poly count multiplies through Forest's bush count
    // savings.
    std::shared_ptr<CatEngine::Model> bush;
};

// Build all four primitives. Idempotent in the sense that calling it
// twice returns two independent Model instances; callers should cache
// the first result. The Models are populated with one Mesh each, the
// vertex layout matching ScenePass::EnsureModelGpuMesh's expected
// interleaved (vec3 position, vec3 normal, vec2 texcoord0) packing.
//
// No materials are populated — the entity fragment path doesn't sample
// the Material list for bind-pose meshes, so leaving Model::materials
// empty is safe and saves the Material vector's allocation. Per-entity
// colour comes from EntityDraw::color at submission time.
ProceduralTreeMeshes BuildProceduralTreeMeshes();

} // namespace CatGame
