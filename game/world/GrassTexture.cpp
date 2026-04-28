// Procedural grass diffuse texture generator. See GrassTexture.hpp for
// the spec — this implementation mirrors the canvas algorithm at
// src/components/game/ForestEnvironment.tsx:234-281 step-for-step.
//
// Why we don't use a "real" grass shader / displacement / blade
// instancing: the web port doesn't either. AP-OPTIMIZE-BEFORE-PARITY
// applies — get the same look first, then improve.
//
// Why a deterministic seed: the web port uses Math.random() with no
// seed, so each page-load gets a different grass tile. That's fine in
// the browser (one-shot canvas) but bad for screenshot diffs in the
// native engine — we want yesterday's frame and today's frame to share
// the same grass when nothing else changed, so a regression in fog or
// lighting reads cleanly against an unchanged ground.

#include "GrassTexture.hpp"

#include <algorithm>
#include <cmath>
#include <random>

namespace CatGame {

namespace {

// Pack four bytes into a row-major (y * Width + x) * 4 cell.
inline void writeRGBA(std::vector<uint8_t>& rgba, uint32_t x, uint32_t y,
                      uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    const size_t idx = (static_cast<size_t>(y) * GrassTextureBuffer::Width
                        + static_cast<size_t>(x)) * 4;
    rgba[idx + 0] = r;
    rgba[idx + 1] = g;
    rgba[idx + 2] = b;
    rgba[idx + 3] = a;
}

// Standard "src RGBA over dst RGBA" alpha composite, both 8-bit. We
// only use alpha < 255 for the dark-spot stamps (alpha 128 = 0.5 in
// the canvas), so this path matters specifically there. Grass blades
// are opaque (alpha 255) and could call writeRGBA directly, but routing
// them through here keeps the call site uniform.
inline void blendOver(std::vector<uint8_t>& rgba, uint32_t x, uint32_t y,
                      uint8_t srcR, uint8_t srcG, uint8_t srcB, uint8_t srcA) {
    const size_t idx = (static_cast<size_t>(y) * GrassTextureBuffer::Width
                        + static_cast<size_t>(x)) * 4;
    const float a = static_cast<float>(srcA) / 255.0F;
    const float ia = 1.0F - a;
    rgba[idx + 0] = static_cast<uint8_t>(static_cast<float>(srcR) * a
                                         + static_cast<float>(rgba[idx + 0]) * ia);
    rgba[idx + 1] = static_cast<uint8_t>(static_cast<float>(srcG) * a
                                         + static_cast<float>(rgba[idx + 1]) * ia);
    rgba[idx + 2] = static_cast<uint8_t>(static_cast<float>(srcB) * a
                                         + static_cast<float>(rgba[idx + 2]) * ia);
    // Output alpha stays 255 — the destination is fully opaque grass.
}

} // anonymous namespace

GrassTextureBuffer GenerateGrassTexture(uint32_t seed) {
    GrassTextureBuffer out;
    out.rgba.resize(static_cast<size_t>(GrassTextureBuffer::Width)
                    * GrassTextureBuffer::Height * 4);

    // Step 1 — base fill #7fb069.
    // 0x7F = 127, 0xB0 = 176, 0x69 = 105. Web port: ctx.fillStyle = '#7fb069'.
    constexpr uint8_t baseR = 0x7F;
    constexpr uint8_t baseG = 0xB0;
    constexpr uint8_t baseB = 0x69;
    for (uint32_t y = 0; y < GrassTextureBuffer::Height; ++y) {
        for (uint32_t x = 0; x < GrassTextureBuffer::Width; ++x) {
            writeRGBA(out.rgba, x, y, baseR, baseG, baseB, 255);
        }
    }

    // Deterministic RNG. std::mt19937 matches the web port's "uniform
    // distribution over 0..1" — Math.random() is implementation-defined
    // in JS but in practice is also a Mersenne-style PRNG, so the
    // _shape_ of the texture (density, dispersion) ports cleanly.
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> uni01(0.0F, 1.0F);

    // Step 2 — 300 grass-blade rectangles.
    // Web port loop body (line 253-262):
    //   const x = Math.random() * 256;
    //   const y = Math.random() * 256;
    //   const length = Math.random() * 4 + 2;     // 2..6 inclusive-ish
    //   const width  = 1;
    //   const variation = Math.floor(Math.random() * 20) - 10;  // -10..9
    //   ctx.fillStyle = `rgb(${127 + variation}, ${176 + variation}, 105)`;
    //   ctx.fillRect(x, y, width, length);
    //
    // ctx.fillRect(x, y, 1, length) writes the rectangle [x, x+1) × [y, y+length).
    // Pixels with non-integer corners are clipped by the canvas to the
    // floor pixel; we floor the position here to match.
    constexpr int bladeCount = 300;
    for (int i = 0; i < bladeCount; ++i) {
        const auto px      = static_cast<int>(uni01(rng) * GrassTextureBuffer::Width);
        const auto py      = static_cast<int>(uni01(rng) * GrassTextureBuffer::Height);
        const auto length  = static_cast<int>(uni01(rng) * 4.0F + 2.0F); // 2..5
        const int variation = static_cast<int>(std::floor(uni01(rng) * 20.0F)) - 10;

        const int rr = std::clamp(127 + variation, 0, 255);
        const int gg = std::clamp(176 + variation, 0, 255);
        constexpr int bb = 105;

        for (int dy = 0; dy < length; ++dy) {
            const int y = py + dy;
            if (y < 0 || y >= static_cast<int>(GrassTextureBuffer::Height)) continue;
            if (px < 0 || px >= static_cast<int>(GrassTextureBuffer::Width)) continue;
            writeRGBA(out.rgba, static_cast<uint32_t>(px), static_cast<uint32_t>(y),
                      static_cast<uint8_t>(rr), static_cast<uint8_t>(gg),
                      static_cast<uint8_t>(bb), 255);
        }
    }

    // Step 3 — 15 dark spots (rgba(45, 80, 50, 0.5) filled circles, r=1..4).
    // Web port loop body (line 266-274):
    //   const x = Math.random() * 256;
    //   const y = Math.random() * 256;
    //   const size = Math.random() * 3 + 1;   // 1..4
    //   ctx.fillStyle = 'rgba(45, 80, 50, 0.5)';
    //   ctx.beginPath();
    //   ctx.arc(x, y, size, 0, Math.PI * 2);
    //   ctx.fill();
    //
    // We rasterise the disc by sampling integer pixels inside radius
    // and using blendOver with srcA=128 (0.5 of 255, rounded down).
    constexpr int spotCount = 15;
    constexpr uint8_t spotR  = 45;
    constexpr uint8_t spotG  = 80;
    constexpr uint8_t spotB  = 50;
    constexpr uint8_t spotA  = 128;
    for (int i = 0; i < spotCount; ++i) {
        const float cx     = uni01(rng) * GrassTextureBuffer::Width;
        const float cy     = uni01(rng) * GrassTextureBuffer::Height;
        const float radius = uni01(rng) * 3.0F + 1.0F;
        const auto rCeil   = static_cast<int>(std::ceil(radius)) + 1;
        const float r2     = radius * radius;
        for (int dy = -rCeil; dy <= rCeil; ++dy) {
            for (int dx = -rCeil; dx <= rCeil; ++dx) {
                const float px = cx + static_cast<float>(dx);
                const float py = cy + static_cast<float>(dy);
                const int ix = static_cast<int>(px);
                const int iy = static_cast<int>(py);
                if (ix < 0 || ix >= static_cast<int>(GrassTextureBuffer::Width)) continue;
                if (iy < 0 || iy >= static_cast<int>(GrassTextureBuffer::Height)) continue;
                const float ddx = px - cx;
                const float ddy = py - cy;
                if (ddx * ddx + ddy * ddy > r2) continue;
                blendOver(out.rgba, static_cast<uint32_t>(ix),
                          static_cast<uint32_t>(iy),
                          spotR, spotG, spotB, spotA);
            }
        }
    }

    return out;
}

} // namespace CatGame
