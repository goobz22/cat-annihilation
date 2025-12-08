// Common Utility Functions for Cat Annihilation Engine

#ifndef UTILS_GLSL
#define UTILS_GLSL

#include "constants.glsl"

// Octahedron normal encoding (compact normal storage)
vec2 encodeNormal(vec3 n) {
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    vec2 encoded = n.z >= 0.0 ? n.xy : (1.0 - abs(n.yx)) * (step(0.0, n.xy) * 2.0 - 1.0);
    return encoded * 0.5 + 0.5;
}

vec3 decodeNormal(vec2 encoded) {
    encoded = encoded * 2.0 - 1.0;
    vec3 n = vec3(encoded.x, encoded.y, 1.0 - abs(encoded.x) - abs(encoded.y));
    float t = max(-n.z, 0.0);
    n.x += n.x >= 0.0 ? -t : t;
    n.y += n.y >= 0.0 ? -t : t;
    return normalize(n);
}

// Linearize depth from depth buffer
float linearizeDepth(float depth, float near, float far) {
    float z = depth * 2.0 - 1.0; // Back to NDC
    return (2.0 * near * far) / (far + near - z * (far - near));
}

// Reconstruct world position from depth
vec3 worldPositionFromDepth(vec2 uv, float depth, mat4 invViewProj) {
    vec4 clipSpacePos = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 worldPos = invViewProj * clipSpacePos;
    return worldPos.xyz / worldPos.w;
}

// Calculate view space position from depth
vec3 viewPositionFromDepth(vec2 uv, float depth, mat4 invProj) {
    vec4 clipSpacePos = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 viewPos = invProj * clipSpacePos;
    return viewPos.xyz / viewPos.w;
}

// Luminance calculation
float luminance(vec3 color) {
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

// Safe normalization
vec3 safeNormalize(vec3 v) {
    float len = length(v);
    return len > EPSILON ? v / len : vec3(0.0, 0.0, 1.0);
}

// Remap value from one range to another
float remap(float value, float inMin, float inMax, float outMin, float outMax) {
    return outMin + (value - inMin) * (outMax - outMin) / (inMax - inMin);
}

// Smooth minimum (for soft blending)
float smin(float a, float b, float k) {
    float h = max(k - abs(a - b), 0.0) / k;
    return min(a, b) - h * h * h * k * (1.0 / 6.0);
}

// Smooth maximum
float smax(float a, float b, float k) {
    return -smin(-a, -b, k);
}

// Gamma correction
vec3 toLinear(vec3 sRGB) {
    return pow(sRGB, vec3(2.2));
}

vec3 toSRGB(vec3 linear) {
    return pow(linear, vec3(1.0 / 2.2));
}

// Hash function for random number generation
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

vec2 hash2(vec2 p) {
    p = vec2(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)));
    return fract(sin(p) * 43758.5453123);
}

vec3 hash3(vec3 p) {
    p = vec3(dot(p, vec3(127.1, 311.7, 74.7)),
             dot(p, vec3(269.5, 183.3, 246.1)),
             dot(p, vec3(113.5, 271.9, 124.6)));
    return fract(sin(p) * 43758.5453123);
}

// Interleaved gradient noise (for dithering)
float interleavedGradientNoise(vec2 screenPos) {
    vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
    return fract(magic.z * fract(dot(screenPos, magic.xy)));
}

// Compute cluster index from screen position and depth
uvec3 computeClusterIndex(vec2 screenUV, float viewDepth, float near, float far) {
    uvec2 tileIndex = uvec2(screenUV * vec2(CLUSTER_GRID_X, CLUSTER_GRID_Y));

    // Logarithmic Z distribution for better depth precision
    float zSlice = log2(viewDepth / near) / log2(far / near);
    uint zIndex = uint(zSlice * float(CLUSTER_GRID_Z));
    zIndex = clamp(zIndex, 0u, CLUSTER_GRID_Z - 1u);

    return uvec3(tileIndex, zIndex);
}

// Convert cluster index to linear index
uint clusterIndexToLinear(uvec3 clusterIndex) {
    return clusterIndex.x +
           clusterIndex.y * CLUSTER_GRID_X +
           clusterIndex.z * CLUSTER_GRID_X * CLUSTER_GRID_Y;
}

#endif // UTILS_GLSL
