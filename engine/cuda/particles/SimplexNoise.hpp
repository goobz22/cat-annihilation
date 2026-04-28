// SimplexNoise.hpp
// ---------------------------------------------------------------------------
// Stefan Gustavson's 2012 reference 3-D simplex noise ported to float math
// and made host/device-compatible so the same source of truth backs:
//   1. The CUDA particle turbulence kernel (curlNoise() in ParticleKernels.cu)
//   2. The Catch2 unit test suite (test_simplex_noise.cpp)
//
// WHY this exists (backlog: P1 "Curl-noise upgrade: simplex over Perlin"):
//   The previous ParticleKernels.cu path used grid-aligned value noise — an
//   8-corner hash lookup on a CUBIC lattice with a trilinear fade. That
//   produces visible "streaks" in the particle turbulence field whenever a
//   velocity vector aligns with the X, Y, or Z axis, because the cubic cell
//   walls ARE the axis planes. Simplex tessellation uses regular tetrahedra
//   whose faces are oblique to every coordinate axis, so the output is
//   isotropic at any orientation.
//
//   The old path stays compiled (and selectable through TurbulenceNoiseMode)
//   so portfolio A/B screenshots can still show the grid-banded "before".
//
// WHY header-only:
//   1. Unit tests can link the same function nvcc builds into the kernel,
//      guaranteeing the in-game behaviour matches the tested behaviour.
//   2. __host__ __device__ forces nvcc to emit a device-callable copy; plain
//      C++ includes see the attributes expanded to nothing.
//
// WHY the tables live inside accessor functions and not at namespace scope:
//   A namespace-scope `constexpr` array is a HOST-ONLY symbol from nvcc's
//   perspective — device code that reads it fails to compile with
//   "identifier … is undefined in device code". Wrapping the array in a
//   `__host__ __device__` function turns it into a function-local
//   constexpr, which nvcc correctly emits for both sides of the toolchain
//   (constant memory on device, .rodata on host). Ken Perlin's permutation
//   table is immutable enough that the minor duplication-per-call-site is
//   fine; the compiler will hoist the constant lookup into const-memory on
//   the device anyway.
// ---------------------------------------------------------------------------
#pragma once

#include <cstdint>

// Host/device attribute shim. When nvcc compiles a .cu translation unit that
// pulls in this header, __CUDACC__ is defined and we tag every function both
// __host__ and __device__ — nvcc then emits two copies of the code (one for
// the CPU side, one for the GPU). When a plain C++ compiler includes this
// header (e.g. from the Catch2 test runner), __CUDACC__ is undefined and the
// macro expands to just `inline`, which is all the host side needs.
#ifdef __CUDACC__
#define SIMPLEX_NOISE_HD __host__ __device__ inline
#else
#define SIMPLEX_NOISE_HD inline
#endif

namespace CatEngine {
namespace CUDA {
namespace noise {

struct Grad3 { float x, y, z; };

namespace detail {

// Ken Perlin's canonical permutation table (1983, reused by Gustavson 2012
// for simplex noise). 256 unique entries doubled to 512 so gradient-index
// lookups can do `PermutationAt(base + offset)` with `offset <= 1` and
// `base <= 255` without a wrap-around modulo on the hot path.
//
// WHY accessor function + local constexpr: see file header — namespace-scope
// constexpr arrays break the CUDA device-compile. Function-local constexpr
// arrays are accepted by nvcc for both host and device code.
SIMPLEX_NOISE_HD std::uint8_t PermutationAt(int idx) noexcept {
    constexpr std::uint8_t kPermutation[512] = {
        151, 160, 137,  91,  90,  15, 131,  13, 201,  95,  96,  53, 194, 233,   7, 225,
        140,  36, 103,  30,  69, 142,   8,  99,  37, 240,  21,  10,  23, 190,   6, 148,
        247, 120, 234,  75,   0,  26, 197,  62,  94, 252, 219, 203, 117,  35,  11,  32,
         57, 177,  33,  88, 237, 149,  56,  87, 174,  20, 125, 136, 171, 168,  68, 175,
         74, 165,  71, 134, 139,  48,  27, 166,  77, 146, 158, 231,  83, 111, 229, 122,
         60, 211, 133, 230, 220, 105,  92,  41,  55,  46, 245,  40, 244, 102, 143,  54,
         65,  25,  63, 161,   1, 216,  80,  73, 209,  76, 132, 187, 208,  89,  18, 169,
        200, 196, 135, 130, 116, 188, 159,  86, 164, 100, 109, 198, 173, 186,   3,  64,
         52, 217, 226, 250, 124, 123,   5, 202,  38, 147, 118, 126, 255,  82,  85, 212,
        207, 206,  59, 227,  47,  16,  58,  17, 182, 189,  28,  42, 223, 183, 170, 213,
        119, 248, 152,   2,  44, 154, 163,  70, 221, 153, 101, 155, 167,  43, 172,   9,
        129,  22,  39, 253,  19,  98, 108, 110,  79, 113, 224, 232, 178, 185, 112, 104,
        218, 246,  97, 228, 251,  34, 242, 193, 238, 210, 144,  12, 191, 179, 162, 241,
         81,  51, 145, 235, 249,  14, 239, 107,  49, 192, 214,  31, 181, 199, 106, 157,
        184,  84, 204, 176, 115, 121,  50,  45, 127,   4, 150, 254, 138, 236, 205,  93,
        222, 114,  67,  29,  24,  72, 243, 141, 128, 195,  78,  66, 215,  61, 156, 180,
        // Doubled (indices 256-511) so the table lookup is wrap-free for
        // base+offset where base is already masked to 0-255.
        151, 160, 137,  91,  90,  15, 131,  13, 201,  95,  96,  53, 194, 233,   7, 225,
        140,  36, 103,  30,  69, 142,   8,  99,  37, 240,  21,  10,  23, 190,   6, 148,
        247, 120, 234,  75,   0,  26, 197,  62,  94, 252, 219, 203, 117,  35,  11,  32,
         57, 177,  33,  88, 237, 149,  56,  87, 174,  20, 125, 136, 171, 168,  68, 175,
         74, 165,  71, 134, 139,  48,  27, 166,  77, 146, 158, 231,  83, 111, 229, 122,
         60, 211, 133, 230, 220, 105,  92,  41,  55,  46, 245,  40, 244, 102, 143,  54,
         65,  25,  63, 161,   1, 216,  80,  73, 209,  76, 132, 187, 208,  89,  18, 169,
        200, 196, 135, 130, 116, 188, 159,  86, 164, 100, 109, 198, 173, 186,   3,  64,
         52, 217, 226, 250, 124, 123,   5, 202,  38, 147, 118, 126, 255,  82,  85, 212,
        207, 206,  59, 227,  47,  16,  58,  17, 182, 189,  28,  42, 223, 183, 170, 213,
        119, 248, 152,   2,  44, 154, 163,  70, 221, 153, 101, 155, 167,  43, 172,   9,
        129,  22,  39, 253,  19,  98, 108, 110,  79, 113, 224, 232, 178, 185, 112, 104,
        218, 246,  97, 228, 251,  34, 242, 193, 238, 210, 144,  12, 191, 179, 162, 241,
         81,  51, 145, 235, 249,  14, 239, 107,  49, 192, 214,  31, 181, 199, 106, 157,
        184,  84, 204, 176, 115, 121,  50,  45, 127,   4, 150, 254, 138, 236, 205,  93,
        222, 114,  67,  29,  24,  72, 243, 141, 128, 195,  78,  66, 215,  61, 156, 180,
    };
    return kPermutation[idx & 511];
}

// 12 canonical edge gradient directions on the 3-cube — midpoints of the
// 12 edges of a unit cube centred at origin, which form an equidistributed
// 3D direction set (subset of icosahedral vertex directions up to scale).
// Every coordinate is 0 or ±1, so the dot with an offset collapses to a
// three-term signed sum — no multiplies, ideal for the inner loop.
SIMPLEX_NOISE_HD Grad3 GradAt(int idx) noexcept {
    constexpr Grad3 kGrad3[12] = {
        {  1.0f,  1.0f,  0.0f }, { -1.0f,  1.0f,  0.0f },
        {  1.0f, -1.0f,  0.0f }, { -1.0f, -1.0f,  0.0f },
        {  1.0f,  0.0f,  1.0f }, { -1.0f,  0.0f,  1.0f },
        {  1.0f,  0.0f, -1.0f }, { -1.0f,  0.0f, -1.0f },
        {  0.0f,  1.0f,  1.0f }, {  0.0f, -1.0f,  1.0f },
        {  0.0f,  1.0f, -1.0f }, {  0.0f, -1.0f, -1.0f },
    };
    // `idx` is always `% 12` by the caller (see HashGradientIndex), so a
    // plain index is safe here; the defensive mask would cost a modulo on
    // the hot path for no benefit.
    return kGrad3[idx];
}

// Branchless floor-to-int, avoiding a call to floorf() on CUDA (which hits a
// device intrinsic) and on MSVC (where floorf() of a negative value still
// needs the subtract-to-int fixup). Only used for lattice-cell
// identification, not for the corner offsets themselves, so the output type
// is int, not float.
SIMPLEX_NOISE_HD int FastFloor(float v) noexcept {
    int i = static_cast<int>(v);
    return v < static_cast<float>(i) ? i - 1 : i;
}

SIMPLEX_NOISE_HD float DotGrad(const Grad3& g, float x, float y, float z) noexcept {
    // Three-term signed sum (every grad component is 0 or ±1, so the
    // compiler folds this into add/sub — no multiplies on the hot path).
    return g.x * x + g.y * y + g.z * z;
}

// Lookup into the permutation table with automatic base masking. The inputs
// i, j, k can be negative (world-space noise coords) — mask them to 0-255
// before hashing so the 512-entry wrap-safe table covers every case.
SIMPLEX_NOISE_HD int HashGradientIndex(int i, int j, int k) noexcept {
    const int ii = i & 255;
    const int jj = j & 255;
    const int kk = k & 255;
    // Three nested lookups — the result is well-distributed across 0-255
    // because each lookup independently permutes, and `% 12` then maps to
    // one of the 12 gradient directions. Gustavson 2012 used the same
    // chained-hash pattern.
    return PermutationAt(ii + PermutationAt(jj + PermutationAt(kk))) % 12;
}

} // namespace detail

// --------------------------------------------------------------------------
// Simplex3D — 3-D simplex noise. Returns approximately [-1, 1].
//
// The theoretical peak is ~0.866 but the classical 32× scale is chosen to
// match the range of Perlin3D so callers swapping the two see the same
// amplitude. The output is C^2 continuous because each corner contributes
// with compact support (r^4 falloff with an r^2 cutoff).
//
// Reference: Gustavson 2012, "Simplex noise demystified".
// --------------------------------------------------------------------------
SIMPLEX_NOISE_HD float Simplex3D(float xin, float yin, float zin) noexcept {
    // Skewing / unskewing factors for 3D. F3 maps the cubic lattice so the
    // simplex corners land on integer coordinates after the skew; G3 is
    // the inverse. Their exact values are fixed by the choice of tetrahedral
    // tessellation and cannot be tuned without breaking the corner
    // contribution math below.
    constexpr float F3 = 1.0f / 3.0f;
    constexpr float G3 = 1.0f / 6.0f;

    // Step 1: skew input to find which simplex cell we're in.
    const float s  = (xin + yin + zin) * F3;
    const int   i  = detail::FastFloor(xin + s);
    const int   j  = detail::FastFloor(yin + s);
    const int   k  = detail::FastFloor(zin + s);

    const float t  = static_cast<float>(i + j + k) * G3;
    // Unskew cell origin back into (x, y, z) space.
    const float X0 = static_cast<float>(i) - t;
    const float Y0 = static_cast<float>(j) - t;
    const float Z0 = static_cast<float>(k) - t;
    // Offset from cell origin — the first of four corner offsets.
    const float x0 = xin - X0;
    const float y0 = yin - Y0;
    const float z0 = zin - Z0;

    // Step 2: determine which simplex (one of 6 orderings) we fell into,
    // and record the offsets to the second and third corners. The fourth
    // corner offset is always (1, 1, 1).
    int i1, j1, k1, i2, j2, k2;
    if (x0 >= y0) {
        if (y0 >= z0)      { i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 1; k2 = 0; }  // X Y Z
        else if (x0 >= z0) { i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 0; k2 = 1; }  // X Z Y
        else               { i1 = 0; j1 = 0; k1 = 1; i2 = 1; j2 = 0; k2 = 1; }  // Z X Y
    } else {
        if (y0 < z0)       { i1 = 0; j1 = 0; k1 = 1; i2 = 0; j2 = 1; k2 = 1; }  // Z Y X
        else if (x0 < z0)  { i1 = 0; j1 = 1; k1 = 0; i2 = 0; j2 = 1; k2 = 1; }  // Y Z X
        else               { i1 = 0; j1 = 1; k1 = 0; i2 = 1; j2 = 1; k2 = 0; }  // Y X Z
    }

    // Step 3: compute the four corner offsets in (x, y, z) space. Each
    // subsequent corner sits G3 further along the body diagonal because the
    // cubic-to-simplex skew shears everything along (1, 1, 1).
    const float x1 = x0 - static_cast<float>(i1) + G3;
    const float y1 = y0 - static_cast<float>(j1) + G3;
    const float z1 = z0 - static_cast<float>(k1) + G3;
    const float x2 = x0 - static_cast<float>(i2) + 2.0f * G3;
    const float y2 = y0 - static_cast<float>(j2) + 2.0f * G3;
    const float z2 = z0 - static_cast<float>(k2) + 2.0f * G3;
    const float x3 = x0 - 1.0f                   + 3.0f * G3;
    const float y3 = y0 - 1.0f                   + 3.0f * G3;
    const float z3 = z0 - 1.0f                   + 3.0f * G3;

    // Step 4: hash each corner to one of the 12 gradient directions.
    const int gi0 = detail::HashGradientIndex(i,       j,       k      );
    const int gi1 = detail::HashGradientIndex(i + i1,  j + j1,  k + k1 );
    const int gi2 = detail::HashGradientIndex(i + i2,  j + j2,  k + k2 );
    const int gi3 = detail::HashGradientIndex(i + 1,   j + 1,   k + 1  );

    // Step 5: compute each corner's contribution and sum. The (0.6 - r^2)^4
    // falloff — squared twice to give r^4 compact support — is what gives
    // simplex its C^2 continuity at the corner cutoff. If the offset is
    // beyond `sqrt(0.6) ≈ 0.775` the contribution is exactly zero, and the
    // geometry of the simplex tetrahedra guarantees at most four non-zero
    // corners overlap any point — so the inner loop is always four terms,
    // regardless of dimension parameters.
    float n0 = 0.0f, n1 = 0.0f, n2 = 0.0f, n3 = 0.0f;

    float t0 = 0.6f - x0 * x0 - y0 * y0 - z0 * z0;
    if (t0 >= 0.0f) {
        t0 *= t0;
        n0 = t0 * t0 * detail::DotGrad(detail::GradAt(gi0), x0, y0, z0);
    }

    float t1 = 0.6f - x1 * x1 - y1 * y1 - z1 * z1;
    if (t1 >= 0.0f) {
        t1 *= t1;
        n1 = t1 * t1 * detail::DotGrad(detail::GradAt(gi1), x1, y1, z1);
    }

    float t2 = 0.6f - x2 * x2 - y2 * y2 - z2 * z2;
    if (t2 >= 0.0f) {
        t2 *= t2;
        n2 = t2 * t2 * detail::DotGrad(detail::GradAt(gi2), x2, y2, z2);
    }

    float t3 = 0.6f - x3 * x3 - y3 * y3 - z3 * z3;
    if (t3 >= 0.0f) {
        t3 *= t3;
        n3 = t3 * t3 * detail::DotGrad(detail::GradAt(gi3), x3, y3, z3);
    }

    // The 32.0 scale is empirical — Gustavson 2012 measured the peak output
    // of the reference implementation with the chosen gradient set and 0.6
    // cutoff, and normalised to approximately [-1, 1]. Do not change without
    // re-measuring; the unit test pins the observed peak magnitude to catch
    // drift if the constants above are ever edited.
    return 32.0f * (n0 + n1 + n2 + n3);
}

} // namespace noise
} // namespace CUDA
} // namespace CatEngine
