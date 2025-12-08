#version 450

// Cat Fur Vertex Shader
// Handles vertex transformation and tangent space calculation

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inTangent;
layout(location = 4) in vec3 inBitangent;

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outTexCoord;
layout(location = 3) out mat3 outTBN;
layout(location = 6) out vec4 outFragPosLightSpace;
layout(location = 7) out vec3 outTangent;
layout(location = 8) out vec3 outBitangent;

// Uniform buffers
layout(set = 0, binding = 0) uniform CameraData {
    mat4 view;
    mat4 projection;
    mat4 viewProj;
    mat4 invViewProj;
    vec3 cameraPos;
    float nearPlane;
    float farPlane;
} camera;

layout(set = 0, binding = 1) uniform ObjectData {
    mat4 model;
    mat4 normalMatrix;
} object;

layout(set = 0, binding = 3) uniform ShadowData {
    mat4 lightSpaceMatrix;
} shadow;

void main() {
    // Transform position to world space
    vec4 worldPos = object.model * vec4(inPosition, 1.0);
    outWorldPos = worldPos.xyz;

    // Transform to clip space
    gl_Position = camera.viewProj * worldPos;

    // Transform normal to world space
    outNormal = normalize(mat3(object.normalMatrix) * inNormal);

    // Pass through texture coordinates
    outTexCoord = inTexCoord;

    // Calculate tangent space matrix (TBN)
    vec3 T = normalize(mat3(object.normalMatrix) * inTangent);
    vec3 B = normalize(mat3(object.normalMatrix) * inBitangent);
    vec3 N = outNormal;

    // Re-orthogonalize tangent with respect to normal (Gram-Schmidt)
    T = normalize(T - dot(T, N) * N);

    // Calculate bitangent (handle mirrored UVs)
    B = cross(N, T);

    // Build TBN matrix for tangent space to world space transformation
    outTBN = mat3(T, B, N);

    // Output tangent and bitangent for anisotropic lighting
    outTangent = T;
    outBitangent = B;

    // Calculate position in light space for shadow mapping
    outFragPosLightSpace = shadow.lightSpaceMatrix * worldPos;
}
