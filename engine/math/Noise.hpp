#ifndef ENGINE_NOISE_HPP
#define ENGINE_NOISE_HPP

#include "Vector.hpp"
#include "Math.hpp"
#include <array>
#include <cstdint>

namespace Engine {
namespace Noise {

// ============================================================================
// Perlin Noise Implementation
// ============================================================================
class Perlin {
private:
    static constexpr int PERM_SIZE = 256;
    std::array<int, PERM_SIZE * 2> perm;

    // Permutation table (Ken Perlin's original)
    static constexpr std::array<int, 256> permutation = {
        151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,140,36,103,30,69,142,
        8,99,37,240,21,10,23,190,6,148,247,120,234,75,0,26,197,62,94,252,219,203,117,
        35,11,32,57,177,33,88,237,149,56,87,174,20,125,136,171,168,68,175,74,165,71,
        134,139,48,27,166,77,146,158,231,83,111,229,122,60,211,133,230,220,105,92,41,
        55,46,245,40,244,102,143,54,65,25,63,161,1,216,80,73,209,76,132,187,208,89,
        18,169,200,196,135,130,116,188,159,86,164,100,109,198,173,186,3,64,52,217,226,
        250,124,123,5,202,38,147,118,126,255,82,85,212,207,206,59,227,47,16,58,17,182,
        189,28,42,223,183,170,213,119,248,152,2,44,154,163,70,221,153,101,155,167,43,
        172,9,129,22,39,253,19,98,108,110,79,113,224,232,178,185,112,104,218,246,97,
        228,251,34,242,193,238,210,144,12,191,179,162,241,81,51,145,235,249,14,239,
        107,49,192,214,31,181,199,106,157,184,84,204,176,115,121,50,45,127,4,150,254,
        138,236,205,93,222,114,67,29,24,72,243,141,128,195,78,66,215,61,156,180
    };

    float fade(float t) const {
        return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
    }

    float grad(int hash, float x, float y, float z) const {
        int h = hash & 15;
        float u = h < 8 ? x : y;
        float v = h < 4 ? y : h == 12 || h == 14 ? x : z;
        return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
    }

public:
    Perlin(uint32_t seed = 0) {
        // Initialize permutation table
        for (int i = 0; i < PERM_SIZE; i++) {
            perm[i] = permutation[i];
            perm[PERM_SIZE + i] = permutation[i];
        }

        // Optionally shuffle with seed
        if (seed != 0) {
            uint32_t state = seed;
            for (int i = PERM_SIZE - 1; i > 0; i--) {
                // Simple LCG random
                state = state * 1664525u + 1013904223u;
                int j = state % (i + 1);
                std::swap(perm[i], perm[j]);
                perm[PERM_SIZE + i] = perm[i];
            }
        }
    }

    // 1D Perlin noise
    float noise(float x) const {
        return noise(x, 0.0f, 0.0f);
    }

    // 2D Perlin noise
    float noise(float x, float y) const {
        return noise(x, y, 0.0f);
    }

    // 3D Perlin noise
    float noise(float x, float y, float z) const {
        // Find unit cube that contains point
        int X = static_cast<int>(std::floor(x)) & 255;
        int Y = static_cast<int>(std::floor(y)) & 255;
        int Z = static_cast<int>(std::floor(z)) & 255;

        // Find relative x, y, z of point in cube
        x -= std::floor(x);
        y -= std::floor(y);
        z -= std::floor(z);

        // Compute fade curves
        float u = fade(x);
        float v = fade(y);
        float w = fade(z);

        // Hash coordinates of the 8 cube corners
        int A = perm[X] + Y;
        int AA = perm[A] + Z;
        int AB = perm[A + 1] + Z;
        int B = perm[X + 1] + Y;
        int BA = perm[B] + Z;
        int BB = perm[B + 1] + Z;

        // Blend results from 8 corners
        float res = Math::lerp(
            Math::lerp(
                Math::lerp(grad(perm[AA], x, y, z),
                          grad(perm[BA], x - 1, y, z), u),
                Math::lerp(grad(perm[AB], x, y - 1, z),
                          grad(perm[BB], x - 1, y - 1, z), u),
                v),
            Math::lerp(
                Math::lerp(grad(perm[AA + 1], x, y, z - 1),
                          grad(perm[BA + 1], x - 1, y, z - 1), u),
                Math::lerp(grad(perm[AB + 1], x, y - 1, z - 1),
                          grad(perm[BB + 1], x - 1, y - 1, z - 1), u),
                v),
            w);

        return res;
    }

    // Octave noise (fractal Brownian motion)
    float octave(float x, int octaves = 4, float persistence = 0.5f) const {
        float total = 0.0f;
        float frequency = 1.0f;
        float amplitude = 1.0f;
        float maxValue = 0.0f;

        for (int i = 0; i < octaves; i++) {
            total += noise(x * frequency) * amplitude;
            maxValue += amplitude;
            amplitude *= persistence;
            frequency *= 2.0f;
        }

        return total / maxValue;
    }

    float octave(float x, float y, int octaves = 4, float persistence = 0.5f) const {
        float total = 0.0f;
        float frequency = 1.0f;
        float amplitude = 1.0f;
        float maxValue = 0.0f;

        for (int i = 0; i < octaves; i++) {
            total += noise(x * frequency, y * frequency) * amplitude;
            maxValue += amplitude;
            amplitude *= persistence;
            frequency *= 2.0f;
        }

        return total / maxValue;
    }

    float octave(float x, float y, float z, int octaves = 4, float persistence = 0.5f) const {
        float total = 0.0f;
        float frequency = 1.0f;
        float amplitude = 1.0f;
        float maxValue = 0.0f;

        for (int i = 0; i < octaves; i++) {
            total += noise(x * frequency, y * frequency, z * frequency) * amplitude;
            maxValue += amplitude;
            amplitude *= persistence;
            frequency *= 2.0f;
        }

        return total / maxValue;
    }
};

// ============================================================================
// Simplex Noise Implementation (faster than Perlin)
// ============================================================================
class Simplex {
private:
    static constexpr int PERM_SIZE = 256;
    std::array<int, PERM_SIZE * 2> perm;
    std::array<int, PERM_SIZE * 2> permMod12;

    // Simplex skew constants
    static constexpr float F2 = 0.5f * (1.7320508f - 1.0f); // (sqrt(3) - 1) / 2
    static constexpr float G2 = (3.0f - 1.7320508f) / 6.0f; // (3 - sqrt(3)) / 6
    static constexpr float F3 = 1.0f / 3.0f;
    static constexpr float G3 = 1.0f / 6.0f;

    // Gradient vectors for 3D
    static constexpr std::array<std::array<int, 3>, 12> grad3 = {{
        {1,1,0}, {-1,1,0}, {1,-1,0}, {-1,-1,0},
        {1,0,1}, {-1,0,1}, {1,0,-1}, {-1,0,-1},
        {0,1,1}, {0,-1,1}, {0,1,-1}, {0,-1,-1}
    }};

    // Permutation table
    static constexpr std::array<int, 256> permutation = {
        151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,140,36,103,30,69,142,
        8,99,37,240,21,10,23,190,6,148,247,120,234,75,0,26,197,62,94,252,219,203,117,
        35,11,32,57,177,33,88,237,149,56,87,174,20,125,136,171,168,68,175,74,165,71,
        134,139,48,27,166,77,146,158,231,83,111,229,122,60,211,133,230,220,105,92,41,
        55,46,245,40,244,102,143,54,65,25,63,161,1,216,80,73,209,76,132,187,208,89,
        18,169,200,196,135,130,116,188,159,86,164,100,109,198,173,186,3,64,52,217,226,
        250,124,123,5,202,38,147,118,126,255,82,85,212,207,206,59,227,47,16,58,17,182,
        189,28,42,223,183,170,213,119,248,152,2,44,154,163,70,221,153,101,155,167,43,
        172,9,129,22,39,253,19,98,108,110,79,113,224,232,178,185,112,104,218,246,97,
        228,251,34,242,193,238,210,144,12,191,179,162,241,81,51,145,235,249,14,239,
        107,49,192,214,31,181,199,106,157,184,84,204,176,115,121,50,45,127,4,150,254,
        138,236,205,93,222,114,67,29,24,72,243,141,128,195,78,66,215,61,156,180
    };

    float dot(const std::array<int, 3>& g, float x, float y, float z) const {
        return g[0] * x + g[1] * y + g[2] * z;
    }

public:
    Simplex(uint32_t seed = 0) {
        // Initialize permutation table
        for (int i = 0; i < PERM_SIZE; i++) {
            perm[i] = permutation[i];
            perm[PERM_SIZE + i] = permutation[i];
        }

        // Optionally shuffle with seed
        if (seed != 0) {
            uint32_t state = seed;
            for (int i = PERM_SIZE - 1; i > 0; i--) {
                state = state * 1664525u + 1013904223u;
                int j = state % (i + 1);
                std::swap(perm[i], perm[j]);
                perm[PERM_SIZE + i] = perm[i];
            }
        }

        // Initialize permMod12
        for (int i = 0; i < PERM_SIZE * 2; i++) {
            permMod12[i] = perm[i] % 12;
        }
    }

    // 2D Simplex noise
    float noise(float x, float y) const {
        // Skew input space to determine which simplex cell we're in
        float s = (x + y) * F2;
        int i = static_cast<int>(std::floor(x + s));
        int j = static_cast<int>(std::floor(y + s));

        float t = (i + j) * G2;
        float X0 = i - t;
        float Y0 = j - t;
        float x0 = x - X0;
        float y0 = y - Y0;

        // Determine which simplex we're in
        int i1, j1;
        if (x0 > y0) { i1 = 1; j1 = 0; }
        else { i1 = 0; j1 = 1; }

        // Offsets for second corner
        float x1 = x0 - i1 + G2;
        float y1 = y0 - j1 + G2;
        float x2 = x0 - 1.0f + 2.0f * G2;
        float y2 = y0 - 1.0f + 2.0f * G2;

        // Work out hashed gradient indices
        int ii = i & 255;
        int jj = j & 255;
        int gi0 = permMod12[ii + perm[jj]];
        int gi1 = permMod12[ii + i1 + perm[jj + j1]];
        int gi2 = permMod12[ii + 1 + perm[jj + 1]];

        // Calculate contribution from three corners
        float n0, n1, n2;
        float t0 = 0.5f - x0 * x0 - y0 * y0;
        if (t0 < 0) n0 = 0.0f;
        else {
            t0 *= t0;
            n0 = t0 * t0 * (grad3[gi0][0] * x0 + grad3[gi0][1] * y0);
        }

        float t1 = 0.5f - x1 * x1 - y1 * y1;
        if (t1 < 0) n1 = 0.0f;
        else {
            t1 *= t1;
            n1 = t1 * t1 * (grad3[gi1][0] * x1 + grad3[gi1][1] * y1);
        }

        float t2 = 0.5f - x2 * x2 - y2 * y2;
        if (t2 < 0) n2 = 0.0f;
        else {
            t2 *= t2;
            n2 = t2 * t2 * (grad3[gi2][0] * x2 + grad3[gi2][1] * y2);
        }

        // Add contributions and scale to [-1, 1]
        return 70.0f * (n0 + n1 + n2);
    }

    // 3D Simplex noise
    float noise(float x, float y, float z) const {
        // Skew input space
        float s = (x + y + z) * F3;
        int i = static_cast<int>(std::floor(x + s));
        int j = static_cast<int>(std::floor(y + s));
        int k = static_cast<int>(std::floor(z + s));

        float t = (i + j + k) * G3;
        float X0 = i - t;
        float Y0 = j - t;
        float Z0 = k - t;
        float x0 = x - X0;
        float y0 = y - Y0;
        float z0 = z - Z0;

        // Determine which simplex we're in
        int i1, j1, k1, i2, j2, k2;
        if (x0 >= y0) {
            if (y0 >= z0) { i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 1; k2 = 0; }
            else if (x0 >= z0) { i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 0; k2 = 1; }
            else { i1 = 0; j1 = 0; k1 = 1; i2 = 1; j2 = 0; k2 = 1; }
        } else {
            if (y0 < z0) { i1 = 0; j1 = 0; k1 = 1; i2 = 0; j2 = 1; k2 = 1; }
            else if (x0 < z0) { i1 = 0; j1 = 1; k1 = 0; i2 = 0; j2 = 1; k2 = 1; }
            else { i1 = 0; j1 = 1; k1 = 0; i2 = 1; j2 = 1; k2 = 0; }
        }

        float x1 = x0 - i1 + G3;
        float y1 = y0 - j1 + G3;
        float z1 = z0 - k1 + G3;
        float x2 = x0 - i2 + 2.0f * G3;
        float y2 = y0 - j2 + 2.0f * G3;
        float z2 = z0 - k2 + 2.0f * G3;
        float x3 = x0 - 1.0f + 3.0f * G3;
        float y3 = y0 - 1.0f + 3.0f * G3;
        float z3 = z0 - 1.0f + 3.0f * G3;

        // Hash coordinates
        int ii = i & 255;
        int jj = j & 255;
        int kk = k & 255;
        int gi0 = permMod12[ii + perm[jj + perm[kk]]];
        int gi1 = permMod12[ii + i1 + perm[jj + j1 + perm[kk + k1]]];
        int gi2 = permMod12[ii + i2 + perm[jj + j2 + perm[kk + k2]]];
        int gi3 = permMod12[ii + 1 + perm[jj + 1 + perm[kk + 1]]];

        // Calculate contributions
        float n0, n1, n2, n3;
        float t0 = 0.6f - x0 * x0 - y0 * y0 - z0 * z0;
        if (t0 < 0) n0 = 0.0f;
        else {
            t0 *= t0;
            n0 = t0 * t0 * dot(grad3[gi0], x0, y0, z0);
        }

        float t1 = 0.6f - x1 * x1 - y1 * y1 - z1 * z1;
        if (t1 < 0) n1 = 0.0f;
        else {
            t1 *= t1;
            n1 = t1 * t1 * dot(grad3[gi1], x1, y1, z1);
        }

        float t2 = 0.6f - x2 * x2 - y2 * y2 - z2 * z2;
        if (t2 < 0) n2 = 0.0f;
        else {
            t2 *= t2;
            n2 = t2 * t2 * dot(grad3[gi2], x2, y2, z2);
        }

        float t3 = 0.6f - x3 * x3 - y3 * y3 - z3 * z3;
        if (t3 < 0) n3 = 0.0f;
        else {
            t3 *= t3;
            n3 = t3 * t3 * dot(grad3[gi3], x3, y3, z3);
        }

        // Scale to [-1, 1]
        return 32.0f * (n0 + n1 + n2 + n3);
    }

    // Octave noise (fractal Brownian motion)
    float octave(float x, float y, int octaves = 4, float persistence = 0.5f) const {
        float total = 0.0f;
        float frequency = 1.0f;
        float amplitude = 1.0f;
        float maxValue = 0.0f;

        for (int i = 0; i < octaves; i++) {
            total += noise(x * frequency, y * frequency) * amplitude;
            maxValue += amplitude;
            amplitude *= persistence;
            frequency *= 2.0f;
        }

        return total / maxValue;
    }

    float octave(float x, float y, float z, int octaves = 4, float persistence = 0.5f) const {
        float total = 0.0f;
        float frequency = 1.0f;
        float amplitude = 1.0f;
        float maxValue = 0.0f;

        for (int i = 0; i < octaves; i++) {
            total += noise(x * frequency, y * frequency, z * frequency) * amplitude;
            maxValue += amplitude;
            amplitude *= persistence;
            frequency *= 2.0f;
        }

        return total / maxValue;
    }
};

// ============================================================================
// Convenience functions
// ============================================================================

// Value noise (simpler, blockier than Perlin)
inline float valueNoise(float x, float y, uint32_t seed = 0) {
    // Hash function
    auto hash = [seed](int x, int y) -> float {
        uint32_t n = seed + x * 374761393u + y * 668265263u;
        n = (n ^ (n >> 13)) * 1274126177u;
        n = n ^ (n >> 16);
        return (n & 0x7fffffff) / 2147483647.0f;
    };

    int xi = static_cast<int>(std::floor(x));
    int yi = static_cast<int>(std::floor(y));
    float xf = x - xi;
    float yf = y - yi;

    // Smooth interpolation
    float u = xf * xf * (3.0f - 2.0f * xf);
    float v = yf * yf * (3.0f - 2.0f * yf);

    float a = hash(xi, yi);
    float b = hash(xi + 1, yi);
    float c = hash(xi, yi + 1);
    float d = hash(xi + 1, yi + 1);

    return Math::lerp(Math::lerp(a, b, u), Math::lerp(c, d, u), v);
}

// Cellular/Worley noise
inline float cellularNoise(float x, float y, uint32_t seed = 0) {
    int xi = static_cast<int>(std::floor(x));
    int yi = static_cast<int>(std::floor(y));

    float minDist = Math::INFINITY_F;

    // Check 3x3 grid
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            int cx = xi + dx;
            int cy = yi + dy;

            // Hash to get point position in cell
            uint32_t n = seed + cx * 374761393u + cy * 668265263u;
            n = (n ^ (n >> 13)) * 1274126177u;
            n = n ^ (n >> 16);

            float px = cx + (n & 0xffff) / 65535.0f;
            float py = cy + ((n >> 16) & 0xffff) / 65535.0f;

            float dx_f = x - px;
            float dy_f = y - py;
            float dist = std::sqrt(dx_f * dx_f + dy_f * dy_f);

            minDist = std::min(minDist, dist);
        }
    }

    return minDist;
}

} // namespace Noise
} // namespace Engine

#endif // ENGINE_NOISE_HPP
