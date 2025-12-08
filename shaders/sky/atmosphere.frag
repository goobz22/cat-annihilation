#version 450

// Atmospheric Scattering Fragment Shader
// Physically-based atmospheric scattering (simplified Bruneton model)

layout(location = 0) in vec3 inTexCoord;
layout(location = 0) out vec4 outColor;

// Camera data
layout(set = 0, binding = 0) uniform CameraData {
    mat4 view;
    mat4 projection;
    mat4 viewProj;
    mat4 invViewProj;
    vec3 cameraPos;
    float nearPlane;
    float farPlane;
} camera;

// Day/Night cycle integration
layout(push_constant) uniform DayNightAtmosphere {
    vec3 sunDirection;
    float sunIntensity;

    vec3 sunColor;
    float timeOfDay;  // 0.0-1.0

    vec3 rayleighScattering;  // Typically (5.8e-6, 13.5e-6, 33.1e-6)
    float rayleighScaleHeight;

    vec3 mieScattering;       // Typically (21e-6, 21e-6, 21e-6)
    float mieScaleHeight;

    vec3 planetCenter;
    float planetRadius;       // Earth: 6371 km

    float atmosphereRadius;   // Earth: 6471 km (100km atmosphere)
    float mieG;              // Mie phase asymmetry factor (0.76)
    float sunAngularRadius;   // Sun angular size
    float exposure;
} atmosphere;

#include "../common/constants.glsl"
#include "../common/utils.glsl"

// Ray-sphere intersection
vec2 raySphereIntersect(vec3 rayOrigin, vec3 rayDir, vec3 sphereCenter, float sphereRadius) {
    vec3 oc = rayOrigin - sphereCenter;
    float b = dot(oc, rayDir);
    float c = dot(oc, oc) - sphereRadius * sphereRadius;
    float discriminant = b * b - c;

    if (discriminant < 0.0) {
        return vec2(-1.0); // No intersection
    }

    float sqrtDisc = sqrt(discriminant);
    return vec2(-b - sqrtDisc, -b + sqrtDisc);
}

// Rayleigh phase function
float rayleighPhase(float cosTheta) {
    return (3.0 / (16.0 * PI)) * (1.0 + cosTheta * cosTheta);
}

// Mie phase function (Henyey-Greenstein)
float miePhase(float cosTheta, float g) {
    float g2 = g * g;
    float denom = 1.0 + g2 - 2.0 * g * cosTheta;
    return (1.0 / (4.0 * PI)) * ((1.0 - g2) / pow(denom, 1.5));
}

// Sample atmospheric scattering along a ray
vec3 atmosphericScattering(vec3 rayOrigin, vec3 rayDir, float maxDist, vec3 sunDir) {
    // Check intersection with atmosphere
    vec2 intersection = raySphereIntersect(rayOrigin, rayDir, atmosphere.planetCenter, atmosphere.atmosphereRadius);

    if (intersection.x < 0.0 && intersection.y < 0.0) {
        return vec3(0.0); // No intersection with atmosphere
    }

    // Determine ray segment through atmosphere
    float rayStart = max(intersection.x, 0.0);
    float rayEnd = min(intersection.y, maxDist);

    if (rayStart > rayEnd) {
        return vec3(0.0);
    }

    // Number of samples along view ray
    const int numSamples = 16;
    const int numSamplesLight = 8;

    float segmentLength = (rayEnd - rayStart) / float(numSamples);
    vec3 rayleighAccum = vec3(0.0);
    vec3 mieAccum = vec3(0.0);
    vec3 opticalDepth = vec3(0.0);

    for (int i = 0; i < numSamples; i++) {
        float t = rayStart + (float(i) + 0.5) * segmentLength;
        vec3 samplePos = rayOrigin + rayDir * t;

        // Calculate height
        float height = length(samplePos - atmosphere.planetCenter) - atmosphere.planetRadius;

        // Density at sample point
        float rayleighDensity = exp(-height / atmosphere.rayleighScaleHeight);
        float mieDensity = exp(-height / atmosphere.mieScaleHeight);

        // Optical depth for this segment
        vec3 segmentOpticalDepth = (atmosphere.rayleighScattering * rayleighDensity +
                                    atmosphere.mieScattering * mieDensity) * segmentLength;

        opticalDepth += segmentOpticalDepth;

        // Light ray optical depth (from sample point to sun)
        vec2 lightIntersection = raySphereIntersect(samplePos, sunDir, atmosphere.planetCenter, atmosphere.atmosphereRadius);
        float lightRayLength = lightIntersection.y;

        if (lightRayLength > 0.0) {
            float lightSegmentLength = lightRayLength / float(numSamplesLight);
            vec3 lightOpticalDepth = vec3(0.0);

            for (int j = 0; j < numSamplesLight; j++) {
                float lt = (float(j) + 0.5) * lightSegmentLength;
                vec3 lightSamplePos = samplePos + sunDir * lt;

                float lightHeight = length(lightSamplePos - atmosphere.planetCenter) - atmosphere.planetRadius;

                float lightRayleighDensity = exp(-lightHeight / atmosphere.rayleighScaleHeight);
                float lightMieDensity = exp(-lightHeight / atmosphere.mieScaleHeight);

                lightOpticalDepth += (atmosphere.rayleighScattering * lightRayleighDensity +
                                     atmosphere.mieScattering * lightMieDensity) * lightSegmentLength;
            }

            // Total attenuation
            vec3 attenuation = exp(-(opticalDepth + lightOpticalDepth));

            // Accumulate scattering
            rayleighAccum += rayleighDensity * attenuation * segmentLength;
            mieAccum += mieDensity * attenuation * segmentLength;
        }
    }

    // Apply phase functions
    float cosTheta = dot(rayDir, sunDir);
    vec3 rayleighScatter = rayleighAccum * atmosphere.rayleighScattering * rayleighPhase(cosTheta);
    vec3 mieScatter = mieAccum * atmosphere.mieScattering * miePhase(cosTheta, atmosphere.mieG);

    // Combine and apply sun intensity
    vec3 color = (rayleighScatter + mieScatter) * atmosphere.sunIntensity;

    return color;
}

// Add sun disk with day/night cycle color
vec3 addSun(vec3 rayDir, vec3 sunDir, vec3 scatterColor) {
    float cosTheta = dot(rayDir, sunDir);
    float sunAngularSize = cos(atmosphere.sunAngularRadius);

    if (cosTheta > sunAngularSize && atmosphere.sunIntensity > 0.01) {
        // Sun disk
        float sunGlow = smoothstep(sunAngularSize, 1.0, cosTheta);
        vec3 sunColor = atmosphere.sunColor * atmosphere.sunIntensity * sunGlow * 10.0;
        return scatterColor + sunColor;
    }

    return scatterColor;
}

// Add moon disk at night
vec3 addMoon(vec3 rayDir, vec3 scatterColor) {
    // Moon is opposite to sun
    vec3 moonDir = -normalize(atmosphere.sunDirection);
    float cosTheta = dot(rayDir, moonDir);
    float moonAngularSize = cos(atmosphere.sunAngularRadius * 0.9);

    // Only render moon at night
    if (cosTheta > moonAngularSize && atmosphere.timeOfDay < 0.2 || atmosphere.timeOfDay > 0.8) {
        float moonGlow = smoothstep(moonAngularSize, 1.0, cosTheta);
        vec3 moonColor = vec3(0.6, 0.6, 0.55) * moonGlow * 0.3;
        return scatterColor + moonColor;
    }

    return scatterColor;
}

void main() {
    vec3 rayDir = normalize(inTexCoord);

    // Calculate atmospheric scattering
    vec3 rayOrigin = camera.cameraPos + atmosphere.planetCenter;
    vec3 sunDir = normalize(atmosphere.sunDirection);

    vec3 color = atmosphericScattering(rayOrigin, rayDir, 1e10, sunDir);

    // Add celestial bodies
    color = addSun(rayDir, sunDir, color);
    color = addMoon(rayDir, color);

    // Apply exposure (adjust based on time of day)
    float dynamicExposure = atmosphere.exposure;
    if (atmosphere.timeOfDay < 0.2 || atmosphere.timeOfDay > 0.8) {
        // Night - increase exposure
        dynamicExposure *= 2.0;
    }
    color *= dynamicExposure;

    // Tonemap (simple Reinhard for now, will be tonemapped again in post)
    color = color / (color + vec3(1.0));

    outColor = vec4(color, 1.0);
}
