#version 450

// Terrain Vertex Shader
// Supports heightmap-based terrain with LOD

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outTexCoord;
layout(location = 3) out vec3 outTangent;
layout(location = 4) out vec3 outBitangent;
layout(location = 5) out float outHeight;

// Camera uniform buffer
layout(set = 0, binding = 0) uniform CameraData {
    mat4 view;
    mat4 projection;
    mat4 viewProj;
    mat4 invViewProj;
    vec3 cameraPos;
    float nearPlane;
    float farPlane;
} camera;

// Terrain heightmap
layout(set = 2, binding = 0) uniform sampler2D heightMap;

// Terrain settings
layout(push_constant) uniform TerrainData {
    mat4 model;
    vec2 terrainScale;
    float heightScale;
    float texCoordScale;
} terrain;

void main() {
    // Sample height from heightmap
    float height = texture(heightMap, inTexCoord).r * terrain.heightScale;

    // Calculate world position with height
    vec3 position = inPosition;
    position.y += height;
    vec4 worldPos = terrain.model * vec4(position, 1.0);
    outWorldPos = worldPos.xyz;
    outHeight = height;

    // Calculate normal from heightmap
    vec2 texelSize = 1.0 / textureSize(heightMap, 0);

    float hL = texture(heightMap, inTexCoord - vec2(texelSize.x, 0.0)).r * terrain.heightScale;
    float hR = texture(heightMap, inTexCoord + vec2(texelSize.x, 0.0)).r * terrain.heightScale;
    float hD = texture(heightMap, inTexCoord - vec2(0.0, texelSize.y)).r * terrain.heightScale;
    float hU = texture(heightMap, inTexCoord + vec2(0.0, texelSize.y)).r * terrain.heightScale;

    vec3 tangent = normalize(vec3(2.0 * terrain.terrainScale.x, hR - hL, 0.0));
    vec3 bitangent = normalize(vec3(0.0, hU - hD, 2.0 * terrain.terrainScale.y));
    vec3 normal = normalize(cross(tangent, bitangent));

    outNormal = normalize(mat3(terrain.model) * normal);
    outTangent = normalize(mat3(terrain.model) * tangent);
    outBitangent = normalize(mat3(terrain.model) * bitangent);

    // Scale texture coordinates for tiling
    outTexCoord = inTexCoord * terrain.texCoordScale;

    // Transform to clip space
    gl_Position = camera.viewProj * worldPos;
}
