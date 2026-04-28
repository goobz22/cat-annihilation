#version 450

// oit_composite.vert
// =============================================================================
// Full-screen triangle vertex shader for the WBOIT composite pass.
//
// Rationale: a single triangle whose vertices sit at (-1,-1), (3,-1), (-1,3)
// perfectly covers the NDC cube and pre-clips past the [-1,1] viewport at
// rasterisation. Cheaper than a two-triangle quad (one less vertex shader
// invocation, no diagonal-edge overdraw) and is the canonical fullscreen
// technique used elsewhere in this engine (see shaders/postprocess/
// fullscreen.vert — this shader is a deliberate duplicate so the WBOIT
// composite pass doesn't reach into the post-process shader directory).
// =============================================================================

layout(location = 0) out vec2 outTexCoord;

void main() {
    // gl_VertexIndex 0..2 → (0,0), (2,0), (0,2); mapping to NDC via x*2-1.
    outTexCoord = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(outTexCoord * 2.0 - 1.0, 0.0, 1.0);
}
