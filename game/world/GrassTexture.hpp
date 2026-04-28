#pragma once

// Procedural grass diffuse texture for the survival/forest ground plane.
//
// Mirrors the canvas algorithm at
// src/components/game/ForestEnvironment.tsx:234-281 in the web port:
//
//   1. Fill base color #7fb069 (light grass green).
//   2. Stroke 300 grass-blade rectangles in rgb(127±10, 176±10, 105),
//      width=1px, height=2..6px, at random positions.
//   3. Stamp 15 dark spots rgba(45, 80, 50, 128) (alpha 0.5 mapped to
//      128/255), filled circles of radius 1..4px.
//
// The native version uses a deterministic seed so the same grass tile
// appears across runs (the web port re-randomises on every page load,
// which is fine for a one-shot canvas but bad for screenshot diffs).
//
// 256×256 RGBA8 → 256 KiB. Tiled 20× across the 10000×10000 ground
// plane in scene.vert/frag — see web port `texture.repeat.set(20, 20)`.
//
// 2026-04-26 SURVIVAL-ONLY MODE port — anti-pattern AP-IGNORE-WEB-SOURCE
// applies: every constant in this file (#7fb069, ±10, 300 blades, etc.)
// traces directly to the corresponding line in ForestEnvironment.tsx.
// Do not invent new variations without confirming against the web port.

#include <cstdint>
#include <vector>

namespace CatGame {

struct GrassTextureBuffer {
    static constexpr uint32_t Width  = 256;
    static constexpr uint32_t Height = 256;
    // Tiled across world space: web port sets THREE.RepeatWrapping with
    // repeat=(20,20) on a 10000-unit ground plane, so each tile spans
    // 500 world units. ScenePass samples with UV = position.xz / TileSize.
    static constexpr float    TileSize = 500.0F;

    // Row-major RGBA8. Size = Width * Height * 4 bytes.
    std::vector<uint8_t> rgba;
};

// Generate the grass texture deterministically from a seed.
//
// Default seed 0xCA75A55U is a stable arbitrary number (loosely "CATs"
// + "GRASs" mashed up — the rationale is just "pick one and keep it"
// so the screenshot diffs in cat-annihilation/docs/parity/ stay
// reproducible). Override only if you need a different visual.
GrassTextureBuffer GenerateGrassTexture(uint32_t seed = 0xCA75A55U);

} // namespace CatGame
