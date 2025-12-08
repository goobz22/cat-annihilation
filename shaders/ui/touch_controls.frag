#version 450

// Touch Controls Fragment Shader
// Renders virtual joysticks and buttons with visual feedback

layout(location = 0) in vec2 inTexCoord;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inLocalPos;   // Position relative to control center
layout(location = 3) in float inRadius;    // Distance from center

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D controlTexture;  // Optional icon texture

layout(push_constant) uniform TouchControlTransform {
    layout(offset = 0) mat4 projection;
    layout(offset = 64) vec2 screenSize;
    layout(offset = 72) float globalOpacity;
    layout(offset = 76) float globalScale;
    layout(offset = 80) float time;
    layout(offset = 84) float pulseIntensity;
    layout(offset = 88) int controlType;
    layout(offset = 92) int isPressed;
} transform;

// Additional settings
layout(push_constant) uniform TouchControlSettings {
    layout(offset = 96) vec4 tintColor;
    layout(offset = 112) float innerRadius;
    layout(offset = 116) float outerRadius;
    layout(offset = 120) float borderWidth;
    layout(offset = 124) float featherAmount;
    layout(offset = 128) int useTexture;
    layout(offset = 132) int showBorder;
    layout(offset = 136) float cooldownPercent;  // 0-1 for cooldown overlay
    layout(offset = 140) float glowIntensity;
} settings;

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

    float feather = settings.featherAmount * transform.globalScale;
    float distance = length(inLocalPos);

    // === JOYSTICK BASE (Type 0) ===
    if (transform.controlType == 0) {
        // Outer ring
        float ring = smoothRing(distance, settings.innerRadius * 0.8, settings.outerRadius, feather);

        // Inner fill with radial gradient
        float innerCircle = smoothCircle(distance, settings.innerRadius * 0.8, feather);
        float gradient = radialGradient(distance, settings.outerRadius);

        finalColor.rgb = mix(finalColor.rgb, finalColor.rgb * 0.5, gradient * innerCircle);
        alpha *= max(ring, innerCircle * 0.3);

        // Border
        if (settings.showBorder != 0) {
            float border = smoothRing(distance, settings.outerRadius - settings.borderWidth, settings.outerRadius, feather * 0.5);
            finalColor.rgb = mix(finalColor.rgb, vec3(1.0), border * 0.5);
            alpha = max(alpha, border * 0.8);
        }
    }
    // === JOYSTICK THUMB (Type 1) ===
    else if (transform.controlType == 1) {
        // Solid circle with gradient
        float circle = smoothCircle(distance, settings.innerRadius, feather);
        float gradient = radialGradient(distance, settings.innerRadius);

        finalColor.rgb = mix(finalColor.rgb * 0.7, finalColor.rgb, gradient);
        alpha *= circle;

        // Add glow when pressed
        if (transform.isPressed != 0) {
            finalColor.rgb = addGlow(finalColor.rgb, distance, settings.innerRadius, settings.glowIntensity);
        }

        // Border
        if (settings.showBorder != 0) {
            float border = smoothRing(distance, settings.innerRadius - settings.borderWidth, settings.innerRadius, feather * 0.5);
            finalColor.rgb = mix(finalColor.rgb, vec3(1.0), border);
            alpha = max(alpha, border);
        }
    }
    // === BUTTON (Type 2) ===
    else if (transform.controlType == 2) {
        // Main button circle
        float circle = smoothCircle(distance, settings.outerRadius, feather);
        float gradient = radialGradient(distance, settings.outerRadius);

        // Apply gradient for depth
        finalColor.rgb = mix(finalColor.rgb * 0.6, finalColor.rgb, pow(gradient, 1.5));
        alpha *= circle;

        // Add glow when pressed
        if (transform.isPressed != 0) {
            finalColor.rgb = addGlow(finalColor.rgb, distance, settings.outerRadius, settings.glowIntensity * 0.5);
            finalColor.rgb *= 1.2;  // Brighten when pressed
        }

        // Border
        if (settings.showBorder != 0) {
            float border = smoothRing(distance, settings.outerRadius - settings.borderWidth, settings.outerRadius, feather * 0.5);
            finalColor.rgb = mix(finalColor.rgb, vec3(1.0), border * 0.7);
            alpha = max(alpha, border);
        }

        // Cooldown overlay
        if (settings.cooldownPercent > 0.0) {
            float cooldownMask = cooldownPie(inLocalPos, settings.cooldownPercent);
            if (cooldownMask > 0.5) {
                finalColor.rgb *= 0.4;  // Darken cooldown area
                finalColor.a *= 0.8;
            }
        }

        // Optional texture/icon
        if (settings.useTexture != 0) {
            vec4 texColor = texture(controlTexture, inTexCoord);
            finalColor.rgb = mix(finalColor.rgb, texColor.rgb, texColor.a * 0.8);
        }
    }

    // Apply tint color
    finalColor *= settings.tintColor;
    finalColor.a = alpha;

    // Discard fully transparent pixels
    if (finalColor.a < 0.01) {
        discard;
    }

    outColor = finalColor;
}
