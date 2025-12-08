#version 450

// Shadow Depth Vertex Shader
// Renders depth from light's perspective for shadow mapping

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inBoneIndices;
layout(location = 2) in vec4 inBoneWeights;

// Light view-projection matrix (for cascaded shadows, this is per cascade)
layout(push_constant) uniform ShadowData {
    mat4 lightViewProj;
    mat4 model;
    int isSkinned;
} shadow;

// Bone matrices for skinned meshes
layout(set = 0, binding = 0) uniform BoneData {
    mat4 bones[256];
} boneData;

void main() {
    vec3 position = inPosition;

    // Apply skinning if needed
    if (shadow.isSkinned != 0) {
        mat4 skinMatrix =
            boneData.bones[int(inBoneIndices.x)] * inBoneWeights.x +
            boneData.bones[int(inBoneIndices.y)] * inBoneWeights.y +
            boneData.bones[int(inBoneIndices.z)] * inBoneWeights.z +
            boneData.bones[int(inBoneIndices.w)] * inBoneWeights.w;

        vec4 skinnedPosition = skinMatrix * vec4(inPosition, 1.0);
        position = skinnedPosition.xyz;
    }

    // Transform to light space
    gl_Position = shadow.lightViewProj * shadow.model * vec4(position, 1.0);
}
