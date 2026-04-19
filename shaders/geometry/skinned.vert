#version 450

// Skinned Mesh Vertex Shader
// Supports skeletal animation for characters.
//
// Vertex attribute layout matches engine/renderer/Mesh.hpp::SkinnedVertex:
//   loc 0: position     (vec3)
//   loc 1: normal       (vec3)
//   loc 2: tangent      (vec4) — .xyz tangent, .w handedness (glTF 2.0).
//                                 Bitangent is reconstructed in-shader.
//   loc 3: uv0          (vec2)
//   loc 4: uv1          (vec2)
//   loc 5: joints       (ivec4) — int32 bone indices (CPU-side int32_t[4])
//   loc 6: weights      (vec4)  — float bone weights, should sum to 1.0
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec2 inTexCoord;
layout(location = 4) in vec2 inTexCoord1;
layout(location = 5) in ivec4 inBoneIndices;
layout(location = 6) in vec4 inBoneWeights;

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outTexCoord;
layout(location = 3) out mat3 outTBN;

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

// Bone matrices (up to 256 bones)
layout(set = 2, binding = 0) uniform BoneData {
    mat4 bones[256];
} boneData;

// Push constants
layout(push_constant) uniform ObjectData {
    mat4 model;
    mat4 normalMatrix;
} object;

void main() {
    // Calculate skinning matrix. inBoneIndices is ivec4 so no float->int cast
    // is needed; this avoids a subtle precision issue where float bone indices
    // >= 2^24 would stop being exactly representable.
    mat4 skinMatrix =
        boneData.bones[inBoneIndices.x] * inBoneWeights.x +
        boneData.bones[inBoneIndices.y] * inBoneWeights.y +
        boneData.bones[inBoneIndices.z] * inBoneWeights.z +
        boneData.bones[inBoneIndices.w] * inBoneWeights.w;

    // Apply skinning to position
    vec4 skinnedPosition = skinMatrix * vec4(inPosition, 1.0);
    vec4 worldPos = object.model * skinnedPosition;
    outWorldPos = worldPos.xyz;

    // Apply skinning to normal
    mat3 skinNormalMatrix = mat3(skinMatrix);
    vec3 skinnedNormal = skinNormalMatrix * inNormal;
    outNormal = normalize(mat3(object.normalMatrix) * skinnedNormal);

    // Apply skinning to tangent. Bitangent is reconstructed from the skinned
    // normal and tangent using the packed handedness (tangent.w). That keeps
    // the basis orthogonal across the skinning blend and avoids shipping a
    // separate bitangent attribute (glTF 2.0 packed-tangent convention).
    vec3 skinnedTangent = skinNormalMatrix * inTangent.xyz;

    // Calculate TBN matrix
    vec3 T = normalize(mat3(object.normalMatrix) * skinnedTangent);
    vec3 N = outNormal;
    vec3 B = normalize(cross(N, T) * inTangent.w);
    outTBN = mat3(T, B, N);

    // Pass through texture coordinates
    outTexCoord = inTexCoord;

    // Transform to clip space
    gl_Position = camera.viewProj * worldPos;
}
