#version 450

// Touch Controls Fragment Shader
// Renders virtual joysticks and buttons with visual feedback

layout(location = 0) in vec2 inTexCoord;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inLocalPos;   // Position relative to control center
layout(location = 3) in float inRadius;    // Distance from center

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D controlTexture;  // Optional icon texture

// Combined push_constant block (only one allowed per stage)
layout(push_constant) uniform TouchControlData {
    // Transform data
    layout(offset = 0) mat4 projection;
    layout(offset = 64) vec2 screenSize;
    layout(offset = 72) float globalOpacity;
    layout(offset = 76) float globalScale;
    layout(offset = 80) float time;
    layout(offset = 84) float pulseIntensity;
    layout(offset = 88) int controlType;
    layout(offset = 92) int isPressed;
    // Settings data
    layout(offset = 96) vec4 tintColor;
    layout(offset = 112) float innerRadius;
    layout(offset = 116) float outerRadius;
    layout(offset = 120) float borderWidth;
    layout(offset = 124) float featherAmount;
    layout(offset = 128) int useTexture;
    layout(offset = 132) int showBorder;
    layout(offset = 136) float cooldownPercent;  // 0-1 for cooldown overlay
    layout(offset = 140) float glowIntensity;
} data;

const float PI = 3.14159265359;

// Smooth circle with anti-aliasing
float smoothCircle(float distance, float radius, float feather) {
    return 1.0 - smoothstep(radius - feather, radius + feather, distance);
}

// Ring shape (hollow circle)
float smoothRing(float distance, float innerR, float outerR, float feather) {
    float outer = smoothCircle(distance, outerR, feather);
    float inner = smoothCircle(distance, innerR, feather);
    return outer - inner;
}

// Radial gradient
float radialGradient(float distance, float radius) {
    return 1.0 - clamp(distance / radius, 0.0, 1.0);
}

// Cooldown pie chart effect
float cooldownPie(vec2 localPos, float percent) {
    if (percent <= 0.0 || percent >= 1.0) {
        return 0.0;
    }

    float angle = atan(localPos.y, localPos.x);
    float normalizedAngle = (angle + PI) / (2.0 * PI);  // 0 to 1

    // Start from top (12 o'clock) and go clockwise
    float startAngle = 0.75;  // Start at top
    float currentAngle = mod(startAngle - percent, 1.0);

    if (normalizedAngle >= currentAngle && normalizedAngle <= startAngle) {
        return 1.0;
    } else if (currentAngle > startAngle) {
        if (normalizedAngle >= currentAngle || normalizedAngle <= startAngle) {
            return 1.0;
        }
    }

    return 0.0;
}

// Glow effect
vec3 addGlow(vec3 color, float distance, float radius, float intensity) {
    float glowFactor = exp(-distance * distance / (radius * radius * 0.5)) * intensity;
    return color + vec3(glowFactor);
}

void main() {
    vec4 finalColor = inColor;
    float alpha = inColor.a;

    float feather = data.featherAmount * data.globalScale;
    float distance = length(inLocalPos);

    // === JOYSTICK BASE (Type 0) ===
    if (data.controlType == 0) {
        // Outer ring
        float ring = smoothRing(distance, data.innerRadius * 0.8, data.outerRadius, feather);

        // Inner fill with radial gradient
        float innerCircle = smoothCircle(distance, data.innerRadius * 0.8, feather);
        float gradient = radialGradient(distance, data.outerRadius);

        finalColor.rgb = mix(finalColor.rgb, finalColor.rgb * 0.5, gradient * innerCircle);
        alpha *= max(ring, innerCircle * 0.3);

        // Border
        if (data.showBorder != 0) {
            float border = smoothRing(distance, data.outerRadius - data.borderWidth, data.outerRadius, feather * 0.5);
            finalColor.rgb = mix(finalColor.rgb, vec3(1.0), border * 0.5);
            alpha = max(alpha, border * 0.8);
        }
    }
    // === JOYSTICK THUMB (Type 1) ===
    else if (data.controlType == 1) {
        // Solid circle with gradient
        float circle = smoothCircle(distance, data.innerRadius, feather);
        float gradient = radialGradient(distance, data.innerRadius);

        finalColor.rgb = mix(finalColor.rgb * 0.7, finalColor.rgb, gradient);
        alpha *= circle;

        // Add glow when pressed
        if (data.isPressed != 0) {
            finalColor.rgb = addGlow(finalColor.rgb, distance, data.innerRadius, data.glowIntensity);
        }

        // Border
        if (data.showBorder != 0) {
            float border = smoothRing(distance, data.innerRadius - data.borderWidth, data.innerRadius, feather * 0.5);
            finalColor.rgb = mix(finalColor.rgb, vec3(1.0), border);
            alpha = max(alpha, border);
        }
    }
    // === BUTTON (Type 2) ===
    else if (data.controlType == 2) {
        // Main button circle
        float circle = smoothCircle(distance, data.outerRadius, feather);
        float gradient = radialGradient(distance, data.outerRadius);

        // Apply gradient for depth
        finalColor.rgb = mix(finalColor.rgb * 0.6, finalColor.rgb, pow(gradient, 1.5));
        alpha *= circle;

        // Add glow when pressed
        if (data.isPressed != 0) {
            finalColor.rgb = addGlow(finalColor.rgb, distance, data.outerRadius, data.glowIntensity * 0.5);
            finalColor.rgb *= 1.2;  // Brighten when pressed
        }

        // Border
        if (data.showBorder != 0) {
            float border = smoothRing(distance, data.outerRadius - data.borderWidth, data.outerRadius, feather * 0.5);
            finalColor.rgb = mix(finalColor.rgb, vec3(1.0), border * 0.7);
            alpha = max(alpha, border);
        }

        // Cooldown overlay
        if (data.cooldownPercent > 0.0) {
            float cooldownMask = cooldownPie(inLocalPos, data.cooldownPercent);
            if (cooldownMask > 0.5) {
                finalColor.rgb *= 0.4;  // Darken cooldown area
                finalColor.a *= 0.8;
            }
        }

        // Optional texture/icon
        if (data.useTexture != 0) {
            vec4 texColor = texture(controlTexture, inTexCoord);
            finalColor.rgb = mix(finalColor.rgb, texColor.rgb, texColor.a * 0.8);
        }
    }

    // Apply tint color
    finalColor *= data.tintColor;
    finalColor.a = alpha;

    // Discard fully transparent pixels
    if (finalColor.a < 0.01) {
        discard;
    }

    outColor = finalColor;
}
