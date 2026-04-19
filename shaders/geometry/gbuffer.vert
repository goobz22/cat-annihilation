#version 450

// G-Buffer Vertex Shader
// Outputs vertex attributes for deferred rendering.
//
// Vertex attribute layout matches engine/renderer/Mesh.hpp::Vertex:
//   loc 0: position  (vec3)
//   loc 1: normal    (vec3)
//   loc 2: tangent   (vec4) — .xyz is the tangent direction, .w is the
//                             bitangent handedness (+/-1). This is the
//                             glTF 2.0 / industry-standard packed-tangent
//                             convention; bitangent is reconstructed below
//                             as cross(normal, tangent.xyz) * tangent.w,
//                             saving 12 bytes per vertex versus shipping
//                             a full vec3 bitangent.
//   loc 3: uv0       (vec2)
//   loc 4: uv1       (vec2) — secondary UVs (lightmaps etc.); unused here
//                             but part of the binding so the stride and
//                             offsets line up with the CPU-side Vertex
//                             struct exactly.
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec2 inTexCoord;
layout(location = 4) in vec2 inTexCoord1;

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outTexCoord;
layout(location = 3) out mat3 outTBN;

// Uniform Buffer Objects
layout(set = 0, binding = 0) uniform CameraData {
    mat4 view;
    mat4 projection;
    mat4 viewProj;
    mat4 invViewProj;
    vec3 cameraPos;
    float nearPlane;
    float farPlane;
} camera;

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

    // Calculate TBN matrix for normal mapping. Tangent is transformed by the
    // normal matrix (same space as the normal); the bitangent is reconstructed
    // from N and T with the handedness stored in tangent.w, matching the
    // glTF 2.0 convention that MikkTSpace-generated tangents follow.
    vec3 T = normalize(mat3(object.normalMatrix) * inTangent.xyz);
    vec3 N = outNormal;
    vec3 B = normalize(cross(N, T) * inTangent.w);
    outTBN = mat3(T, B, N);

    // Transform to clip space
    gl_Position = camera.viewProj * worldPos;
}
