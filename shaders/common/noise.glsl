// Noise Functions for Cat Annihilation Engine
// Perlin, Simplex, and Worley noise implementations

#ifndef NOISE_GLSL
#define NOISE_GLSL

#include "constants.glsl"
#include "utils.glsl"

// 2D Perlin noise
float perlinNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);

    // Quintic interpolation curve
    vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);

    float a = hash(i + vec2(0.0, 0.0));
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));

    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

// 3D Perlin noise
float perlinNoise3D(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);

    vec3 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);

    float n000 = hash(vec2(hash(vec2(i.x, i.y)), i.z));
    float n100 = hash(vec2(hash(vec2(i.x + 1.0, i.y)), i.z));
    float n010 = hash(vec2(hash(vec2(i.x, i.y + 1.0)), i.z));
    float n110 = hash(vec2(hash(vec2(i.x + 1.0, i.y + 1.0)), i.z));
    float n001 = hash(vec2(hash(vec2(i.x, i.y)), i.z + 1.0));
    float n101 = hash(vec2(hash(vec2(i.x + 1.0, i.y)), i.z + 1.0));
    float n011 = hash(vec2(hash(vec2(i.x, i.y + 1.0)), i.z + 1.0));
    float n111 = hash(vec2(hash(vec2(i.x + 1.0, i.y + 1.0)), i.z + 1.0));

    float nx00 = mix(n000, n100, u.x);
    float nx10 = mix(n010, n110, u.x);
    float nx01 = mix(n001, n101, u.x);
    float nx11 = mix(n011, n111, u.x);

    float nxy0 = mix(nx00, nx10, u.y);
    float nxy1 = mix(nx01, nx11, u.y);

    return mix(nxy0, nxy1, u.z);
}

// Fractional Brownian Motion (fBm)
float fbm(vec2 p, int octaves) {
    float value = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;

    for (int i = 0; i < octaves; i++) {
        value += amplitude * perlinNoise(p * frequency);
        frequency *= 2.0;
        amplitude *= 0.5;
    }

    return value;
}

float fbm3D(vec3 p, int octaves) {
    float value = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;

    for (int i = 0; i < octaves; i++) {
        value += amplitude * perlinNoise3D(p * frequency);
        frequency *= 2.0;
        amplitude *= 0.5;
    }

    return value;
}

// Simplex noise 2D (more efficient than Perlin)
vec3 mod289(vec3 x) { return x - floor(x * (1.0 / 289.0)) * 289.0; }
vec2 mod289_2(vec2 x) { return x - floor(x * (1.0 / 289.0)) * 289.0; }
vec3 permute(vec3 x) { return mod289(((x * 34.0) + 1.0) * x); }

float simplexNoise(vec2 v) {
    const vec4 C = vec4(0.211324865405187,  // (3.0-sqrt(3.0))/6.0
                        0.366025403784439,  // 0.5*(sqrt(3.0)-1.0)
                        -0.577350269189626, // -1.0 + 2.0 * C.x
                        0.024390243902439); // 1.0 / 41.0

    vec2 i  = floor(v + dot(v, C.yy));
    vec2 x0 = v - i + dot(i, C.xx);

    vec2 i1 = (x0.x > x0.y) ? vec2(1.0, 0.0) : vec2(0.0, 1.0);
    vec4 x12 = x0.xyxy + C.xxzz;
    x12.xy -= i1;

    i = mod289_2(i);
    vec3 p = permute(permute(i.y + vec3(0.0, i1.y, 1.0)) + i.x + vec3(0.0, i1.x, 1.0));

    vec3 m = max(0.5 - vec3(dot(x0, x0), dot(x12.xy, x12.xy), dot(x12.zw, x12.zw)), 0.0);
    m = m * m;
    m = m * m;

    vec3 x = 2.0 * fract(p * C.www) - 1.0;
    vec3 h = abs(x) - 0.5;
    vec3 ox = floor(x + 0.5);
    vec3 a0 = x - ox;

    m *= 1.79284291400159 - 0.85373472095314 * (a0 * a0 + h * h);

    vec3 g;
    g.x = a0.x * x0.x + h.x * x0.y;
    g.yz = a0.yz * x12.xz + h.yz * x12.yw;
    return 130.0 * dot(m, g);
}

// Worley/Cellular noise (for more organic patterns)
float worleyNoise(vec2 p) {
    vec2 n = floor(p);
    vec2 f = fract(p);

    float minDist = 1.0;

    for (int j = -1; j <= 1; j++) {
        for (int i = -1; i <= 1; i++) {
            vec2 neighbor = vec2(float(i), float(j));
            vec2 point = hash2(n + neighbor);
            vec2 diff = neighbor + point - f;
            float dist = length(diff);
            minDist = min(minDist, dist);
        }
    }

    return minDist;
}

// 3D Worley noise
float worleyNoise3D(vec3 p) {
    vec3 n = floor(p);
    vec3 f = fract(p);

    float minDist = 1.0;

    for (int k = -1; k <= 1; k++) {
        for (int j = -1; j <= 1; j++) {
            for (int i = -1; i <= 1; i++) {
                vec3 neighbor = vec3(float(i), float(j), float(k));
                vec3 point = hash3(n + neighbor);
                vec3 diff = neighbor + point - f;
                float dist = length(diff);
                minDist = min(minDist, dist);
            }
        }
    }

    return minDist;
}

// Voronoi with more detail (returns closest and second-closest distances)
vec2 voronoiNoise(vec2 p) {
    vec2 n = floor(p);
    vec2 f = fract(p);

    float minDist1 = 1.0;
    float minDist2 = 1.0;

    for (int j = -1; j <= 1; j++) {
        for (int i = -1; i <= 1; i++) {
            vec2 neighbor = vec2(float(i), float(j));
            vec2 point = hash2(n + neighbor);
            vec2 diff = neighbor + point - f;
            float dist = length(diff);

            if (dist < minDist1) {
                minDist2 = minDist1;
                minDist1 = dist;
            } else if (dist < minDist2) {
                minDist2 = dist;
            }
        }
    }

    return vec2(minDist1, minDist2);
}

// Turbulence (absolute value fBm)
float turbulence(vec2 p, int octaves) {
    float value = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;

    for (int i = 0; i < octaves; i++) {
        value += amplitude * abs(perlinNoise(p * frequency));
        frequency *= 2.0;
        amplitude *= 0.5;
    }

    return value;
}

// Convenience aliases for common noise types
float noise1D(float p) {
    return perlinNoise(vec2(p, 0.0));
}

float noise2D(vec2 p) {
    return perlinNoise(p);
}

float noise3D(vec3 p) {
    return perlinNoise3D(p);
}

#endif // NOISE_GLSL
