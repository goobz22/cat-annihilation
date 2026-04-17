#version 450

// UI Fragment Shader.
// Text rendering is now handled by Dear ImGui (see engine/ui/ImGuiLayer),
// so this shader is a plain solid-color quad — no more 5x5 bitmap patterns.

layout(location = 0) in vec2 inTexCoord;
layout(location = 1) in vec4 inColor;
layout(location = 2) in float inCharCode;
layout(location = 0) out vec4 outColor;

void main() {
    // charCode/UV are retained on the vertex for future use (textured quads);
    // the main menu draws its text through Dear ImGui now, so here we just paint
    // the quad in its vertex color.
    outColor = inColor;
    if (outColor.a < 0.01) {
        discard;
    }
}
