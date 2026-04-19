#version 450

// Forward Rendering Vertex Shader
// Used for objects that don't fit deferred pipeline (e.g., transparent objects).
//
// Shares engine/renderer/Mesh.hpp::Vertex layout with the G-Buffer pass:
//   loc 0 position, loc 1 normal, loc 2 tangent (vec4 packed handedness),
//   loc 3 uv0, loc 4 uv1. Bitangent is derived from cross(N, T.xyz) * T.w.
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec2 inTexCoord;
layout(location = 4) in vec2 inTexCoord1;

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outTexCoord;
layout(location = 3) out mat3 outTBN;
layout(location = 6) out vec4 outFragPosLightSpace;

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

// Shadow data
layout(set = 0, binding = 1) uniform ShadowData {
    mat4 lightViewProj;
} shadowData;

// Push constants for per-object data
layout(push_constant) uniform ObjectData {
    mat4 model;
    mat4 normalMatrix;
} object;

void main() {
    // Transform position to world space
    vec4 worldPos = object.model * vec4(inPosition, 1.0);
    outWorldPos = worldPos.xyz;

    // Transform normal to world space
    outNormal = normalize(mat3(object.normalMatrix) * inNormal);

    // Pass through texture coordinates
    outTexCoord = inTexCoord;

    // Calculate TBN matrix for normal mapping. Bitangent is reconstructed
    // from the normal-mapped tangent using the handedness packed in
    // tangent.w (glTF 2.0 convention). Keeping the basis derivation in-shader
    // also guarantees orthogonality when the normal matrix is non-uniform.
    vec3 T = normalize(mat3(object.normalMatrix) * inTangent.xyz);
    vec3 N = outNormal;
    vec3 B = normalize(cross(N, T) * inTangent.w);
    outTBN = mat3(T, B, N);

    // Calculate position in light space for shadow mapping
    outFragPosLightSpace = shadowData.lightViewProj * worldPos;

    // Transform to clip space
    gl_Position = camera.viewProj * worldPos;
}
