#version 450

// Deferred Lighting Vertex Shader
// Fullscreen triangle for deferred lighting pass

layout(location = 0) out vec2 outTexCoord;

void main() {
    // Generate fullscreen triangle
    // Vertex 0: (-1, -1)
    // Vertex 1: (3, -1)
    // Vertex 2: (-1, 3)
    // This covers the entire screen with a single triangle
    outTexCoord = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(outTexCoord * 2.0 - 1.0, 0.0, 1.0);
}
