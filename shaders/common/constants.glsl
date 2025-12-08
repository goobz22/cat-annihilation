// Common Constants for Cat Annihilation Engine
// Shared across all shaders

#ifndef CONSTANTS_GLSL
#define CONSTANTS_GLSL

// Mathematical constants
const float PI = 3.14159265359;
const float TWO_PI = 6.28318530718;
const float HALF_PI = 1.57079632679;
const float INV_PI = 0.31830988618;
const float INV_TWO_PI = 0.15915494309;
const float EPSILON = 1e-6;

// Lighting constants
const float MIN_ROUGHNESS = 0.04;
const float MAX_ROUGHNESS = 1.0;
const int MAX_POINT_LIGHTS = 256;
const int MAX_SPOT_LIGHTS = 128;
const int MAX_CASCADE_COUNT = 4;

// Clustered lighting grid dimensions
const uint CLUSTER_GRID_X = 16;
const uint CLUSTER_GRID_Y = 9;
const uint CLUSTER_GRID_Z = 24;
const uint MAX_LIGHTS_PER_CLUSTER = 128;

// Shadow constants
const float SHADOW_BIAS = 0.005;
const int PCF_KERNEL_SIZE = 5;
const float PCF_RADIUS = 2.0;

// Post-processing constants
const int BLOOM_MIP_LEVELS = 6;
const float BLOOM_THRESHOLD = 1.0;
const float BLOOM_KNEE = 0.5;
const float BLOOM_INTENSITY = 0.04;

// Particle system constants
const float PARTICLE_GRAVITY = -9.81;
const uint MAX_PARTICLES = 1048576; // 1M particles

// Culling constants
const uint MAX_INSTANCES = 65536;

// G-buffer normal encoding precision
const float NORMAL_ENCODE_SCALE = 2.0;
const float NORMAL_ENCODE_BIAS = -1.0;

#endif // CONSTANTS_GLSL
