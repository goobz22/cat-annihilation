#version 450

// Simple lit terrain fragment shader.
// Albedo is a weighted blend of grass/dirt/rock driven by the terrain's
// splat weights. A single directional "sun" light provides diffuse shading,
// plus a constant ambient term so unlit slopes don't go black.

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec4 vSplatWeights;
layout(location = 2) in vec3 vWorldPos;

layout(location = 0) out vec4 outColor;

const vec3 SUN_DIR   = normalize(vec3(0.35, 0.85, 0.4));
const vec3 SUN_COLOR = vec3(1.0, 0.96, 0.88);

const vec3 GRASS_COLOR = vec3(0.24, 0.55, 0.20);
const vec3 DIRT_COLOR  = vec3(0.45, 0.30, 0.15);
const vec3 ROCK_COLOR  = vec3(0.50, 0.50, 0.55);

void main() {
    float wSum = max(vSplatWeights.r + vSplatWeights.g + vSplatWeights.b, 1e-4);
    vec3 albedo =
        (GRASS_COLOR * vSplatWeights.r +
         DIRT_COLOR  * vSplatWeights.g +
         ROCK_COLOR  * vSplatWeights.b) / wSum;

    vec3 n = normalize(vNormal);
    float lambert = max(dot(n, SUN_DIR), 0.0);

    vec3 ambient = albedo * 0.28;
    vec3 diffuse = albedo * SUN_COLOR * lambert;

    outColor = vec4(ambient + diffuse, 1.0);
}
