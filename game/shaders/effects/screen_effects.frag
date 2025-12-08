#version 450

// Screen effects fragment shader
// Handles: screen shake, vignette, flash effects, slow-motion

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

// Uniforms
layout(binding = 0) uniform sampler2D screenTexture;

layout(binding = 1) uniform ScreenEffectsUBO {
    // Vignette (low HP effect)
    float vignetteIntensity;      // 0.0 to 1.0
    vec3 vignetteColor;            // Usually red (1.0, 0.0, 0.0)

    // Screen shake
    vec2 shakeOffset;              // Offset for screen shake
    float shakeIntensity;          // 0.0 to 1.0

    // Flash effects
    float flashIntensity;          // 0.0 to 1.0
    vec3 flashColor;               // Flash color (white for normal, gold for perfect block)

    // Time dilation (slow-motion)
    float timeDilation;            // 0.0 to 1.0 (0.0 = normal, 1.0 = very slow)

    // Chromatic aberration (for big hits)
    float chromaticAberration;     // 0.0 to 1.0

    // Overall saturation
    float saturation;              // 0.0 to 1.0 (lower for slow-mo effect)

    // Damage flash
    float damageFlash;             // 0.0 to 1.0, red flash when taking damage

    // Hit freeze frame
    float hitFreeze;               // 0.0 to 1.0, pauses for impact
} effects;

// Helper functions

/**
 * Calculate vignette effect
 * Returns intensity (0.0 = no effect, 1.0 = full darkening)
 */
float calculateVignette(vec2 uv) {
    // Distance from center
    vec2 center = vec2(0.5, 0.5);
    float dist = distance(uv, center);

    // Vignette falloff
    float vignette = smoothstep(0.4, 1.2, dist);

    return vignette * effects.vignetteIntensity;
}

/**
 * Apply chromatic aberration
 * Separates RGB channels for impact effect
 */
vec3 chromaticAberration(sampler2D tex, vec2 uv, float intensity) {
    vec2 offset = vec2(intensity * 0.01, 0.0);

    float r = texture(tex, uv + offset).r;
    float g = texture(tex, uv).g;
    float b = texture(tex, uv - offset).b;

    return vec3(r, g, b);
}

/**
 * Convert RGB to grayscale
 */
float toGrayscale(vec3 color) {
    return dot(color, vec3(0.299, 0.587, 0.114));
}

/**
 * Adjust saturation
 * saturation = 0.0 -> grayscale
 * saturation = 1.0 -> original color
 */
vec3 adjustSaturation(vec3 color, float saturation) {
    float gray = toGrayscale(color);
    return mix(vec3(gray), color, saturation);
}

void main() {
    // Apply screen shake offset
    vec2 uv = fragTexCoord + effects.shakeOffset;

    // Clamp UV to avoid artifacts at edges
    uv = clamp(uv, 0.0, 1.0);

    // Sample screen texture with chromatic aberration
    vec3 color;
    if (effects.chromaticAberration > 0.01) {
        color = chromaticAberration(screenTexture, uv, effects.chromaticAberration);
    } else {
        color = texture(screenTexture, uv).rgb;
    }

    // Apply saturation adjustment (for slow-motion effect)
    color = adjustSaturation(color, effects.saturation);

    // Apply vignette effect
    float vignetteAmount = calculateVignette(uv);
    color = mix(color, effects.vignetteColor, vignetteAmount);

    // Apply damage flash (red flash when taking damage)
    if (effects.damageFlash > 0.01) {
        vec3 damageColor = vec3(1.0, 0.2, 0.2);  // Red
        color = mix(color, damageColor, effects.damageFlash * 0.5);
    }

    // Apply generic flash effect (perfect block, critical hit, etc.)
    if (effects.flashIntensity > 0.01) {
        color = mix(color, effects.flashColor, effects.flashIntensity);
    }

    // Apply time dilation visual effect
    if (effects.timeDilation > 0.01) {
        // Slight blue tint for slow-motion
        vec3 slowMoTint = vec3(0.8, 0.9, 1.0);
        color = mix(color, color * slowMoTint, effects.timeDilation * 0.3);

        // Add subtle scanlines
        float scanline = sin(uv.y * 800.0) * 0.02 * effects.timeDilation;
        color += scanline;
    }

    // Apply hit freeze effect (slight desaturation and darkening)
    if (effects.hitFreeze > 0.01) {
        color *= (1.0 - effects.hitFreeze * 0.3);  // Darken
        float gray = toGrayscale(color);
        color = mix(color, vec3(gray), effects.hitFreeze * 0.5);  // Desaturate
    }

    // Output final color
    outColor = vec4(color, 1.0);
}
