#version 450

// Touch Controls Vertex Shader
// Specialized for rendering virtual joysticks and buttons

layout(location = 0) in vec2 inPosition;  // Screen-space position
layout(location = 1) in vec2 inTexCoord;  // Texture coordinates
layout(location = 2) in vec4 inColor;     // Vertex color
layout(location = 3) in vec2 inCenter;    // Control center (for joysticks/buttons)

layout(location = 0) out vec2 outTexCoord;
layout(location = 1) out vec4 outColor;
layout(location = 2) out vec2 outLocalPos;  // Position relative to control center
layout(location = 3) out float outRadius;   // Control radius (for effects)

// Push constants for transform
layout(push_constant) uniform TouchControlTransform {
    mat4 projection;        // Orthographic projection matrix
    vec2 screenSize;        // Screen dimensions
    float globalOpacity;    // Global opacity modifier
    float globalScale;      // Global scale modifier
    float time;             // Time for animations
    float pulseIntensity;   // Pulse animation intensity
    int controlType;        // 0=joystick base, 1=joystick thumb, 2=button
    int isPressed;          // Whether control is pressed
} transform;

void main() {
    // Calculate local position relative to center
    vec2 localPos = inPosition - inCenter;
    outLocalPos = localPos;

    // Apply global scale
    vec2 scaledPos = inCenter + localPos * transform.globalScale;

    // Animation: pulse effect when pressed
    if (transform.isPressed != 0) {
        float pulse = 1.0 + sin(transform.time * 8.0) * 0.1 * transform.pulseIntensity;
        scaledPos = inCenter + localPos * transform.globalScale * pulse;
    }

    // Animation: breathing effect when idle (subtle)
    float breathe = 1.0 + sin(transform.time * 2.0) * 0.02;
    scaledPos = inCenter + (scaledPos - inCenter) * breathe;

    // Apply orthographic projection
    gl_Position = transform.projection * vec4(scaledPos, 0.0, 1.0);

    // Pass through attributes
    outTexCoord = inTexCoord;

    // Apply global opacity to color
    outColor = inColor;
    outColor.a *= transform.globalOpacity;

    // Calculate radius for fragment shader effects
    outRadius = length(localPos);
}
