#version 450

// Skinned Mesh Vertex Shader
// Supports skeletal animation for characters

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inTangent;
layout(location = 4) in vec3 inBitangent;
layout(location = 5) in vec4 inBoneIndices;
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
    // Calculate skinning matrix
    mat4 skinMatrix =
        boneData.bones[int(inBoneIndices.x)] * inBoneWeights.x +
        boneData.bones[int(inBoneIndices.y)] * inBoneWeights.y +
        boneData.bones[int(inBoneIndices.z)] * inBoneWeights.z +
        boneData.bones[int(inBoneIndices.w)] * inBoneWeights.w;

    // Apply skinning to position
    vec4 skinnedPosition = skinMatrix * vec4(inPosition, 1.0);
    vec4 worldPos = object.model * skinnedPosition;
    outWorldPos = worldPos.xyz;

    // Apply skinning to normal
    mat3 skinNormalMatrix = mat3(skinMatrix);
    vec3 skinnedNormal = skinNormalMatrix * inNormal;
    outNormal = normalize(mat3(object.normalMatrix) * skinnedNormal);

    // Apply skinning to tangent and bitangent
    vec3 skinnedTangent = skinNormalMatrix * inTangent;
    vec3 skinnedBitangent = skinNormalMatrix * inBitangent;

    // Calculate TBN matrix
    vec3 T = normalize(mat3(object.normalMatrix) * skinnedTangent);
    vec3 B = normalize(mat3(object.normalMatrix) * skinnedBitangent);
    vec3 N = outNormal;
    outTBN = mat3(T, B, N);

    // Pass through texture coordinates
    outTexCoord = inTexCoord;

    // Transform to clip space
    gl_Position = camera.viewProj * worldPos;
}
