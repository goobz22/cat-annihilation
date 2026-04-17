#version 450

// Flat-shaded entity fragment shader. Same sun direction as the terrain
// shader so lighting stays coherent across the scene.

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec3 vColor;

layout(location = 0) out vec4 outColor;

const vec3 SUN_DIR   = normalize(vec3(0.35, 0.85, 0.4));
const vec3 SUN_COLOR = vec3(1.0, 0.96, 0.88);

void main() {
    vec3 n = normalize(vNormal);
    float lambert = max(dot(n, SUN_DIR), 0.0);
    vec3 ambient = vColor * 0.35;
    vec3 diffuse = vColor * SUN_COLOR * lambert;
    outColor = vec4(ambient + diffuse, 1.0);
}
