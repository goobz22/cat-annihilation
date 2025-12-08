#version 450

// Fullscreen Quad Vertex Shader
// Used for all post-processing effects

layout(location = 0) out vec2 outTexCoord;

void main() {
    // Generate fullscreen triangle covering NDC [-1, 1]
    // Vertex 0: (-1, -1) -> UV (0, 0)
    // Vertex 1: (3, -1)  -> UV (2, 0)
    // Vertex 2: (-1, 3)  -> UV (0, 2)
    outTexCoord = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(outTexCoord * 2.0 - 1.0, 0.0, 1.0);
}
