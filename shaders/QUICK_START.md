# Quick Start Guide - Cat Annihilation Shaders

## TL;DR

All 32 GLSL shaders for your Vulkan game engine are ready to use!

## Compile All Shaders

```bash
cd /home/user/cat-annihilation/shaders
./compile_shaders.sh
```

Output goes to: `/home/user/cat-annihilation/shaders/compiled/`

## Basic Rendering Pipeline

### 1. Shadow Pass
```c
// Render depth from light's perspective
Use: shadows/shadow_depth.vert + shadows/shadow_depth.frag
Output: Shadow map array (4 cascades)
```

### 2. Geometry Pass (G-Buffer)
```c
// Standard objects
Use: geometry/gbuffer.vert + geometry/gbuffer.frag

// Animated characters
Use: geometry/skinned.vert + geometry/gbuffer.frag

// Terrain
Use: geometry/terrain.vert + geometry/terrain.frag

Output: 4 render targets (position, normal, albedo, emission)
```

### 3. Clustered Lighting
```c
// Assign lights to clusters
Use: lighting/clustered.comp
Dispatch: (1, 1, 24) workgroups
```

### 4. Lighting Pass
```c
// Deferred lighting
Use: lighting/deferred.vert + lighting/deferred.frag
Input: G-Buffer + shadow maps + cluster data

// Add ambient/IBL
Use: lighting/deferred.vert + lighting/ambient.frag (additive blend)
```

### 5. Forward Pass (Transparent)
```c
// Transparent objects
Use: forward/forward.vert + forward/transparent.frag
```

### 6. Post-Processing
```c
// Bloom downsample (6 passes, each half resolution)
Use: postprocess/fullscreen.vert + postprocess/bloom_downsample.frag

// Bloom upsample (6 passes, upscaling)
Use: postprocess/fullscreen.vert + postprocess/bloom_upsample.frag

// Tonemap
Use: postprocess/fullscreen.vert + postprocess/tonemap.frag

// FXAA (final)
Use: postprocess/fullscreen.vert + postprocess/fxaa.frag
```

### 7. UI Overlay
```c
// UI elements
Use: ui/ui.vert + ui/ui.frag

// Text
Use: ui/ui.vert + ui/text_sdf.frag
```

## Key Uniforms

### Camera Data (Set 0, Binding 0)
```glsl
layout(set = 0, binding = 0) uniform CameraData {
    mat4 view;
    mat4 projection;
    mat4 viewProj;
    mat4 invViewProj;
    vec3 cameraPos;
    float nearPlane;
    float farPlane;
};
```

### Light Data (Set 0, Binding 1)
```glsl
layout(set = 0, binding = 1) uniform LightData {
    DirectionalLight directionalLight;
    uint pointLightCount;  // Max 256
    uint spotLightCount;   // Max 128
    PointLight pointLights[256];
    SpotLight spotLights[128];
};
```

## Common Issues & Solutions

### Shaders won't compile
```bash
# Check if glslc is installed
which glslc

# Install Vulkan SDK if missing
# Linux: sudo apt install vulkan-sdk
# Windows: Download from lunarg.com
```

### #include not working
```bash
# glslc doesn't support relative includes by default
# Use absolute paths or add include directories:
glslc -I/home/user/cat-annihilation/shaders -fshader-stage=...
```

### Normal mapping looks wrong
```glsl
// Make sure TBN matrix is orthonormal
T = normalize(T - dot(T, N) * N);  // Re-orthogonalize
B = cross(N, T);
```

### Shadows are too dark/light
```glsl
// Adjust shadow bias in common/constants.glsl
const float SHADOW_BIAS = 0.005;  // Increase if shadow acne
                                  // Decrease if peter-panning
```

### Clustered lighting not working
```glsl
// Make sure atomic counter is reset to 0 each frame
// Check cluster grid dimensions match in shader and CPU code
```

### Bloom too bright
```glsl
// Adjust threshold and intensity
push_constants.threshold = 1.0;   // Increase to reduce bloom
push_constants.intensity = 0.04;  // Decrease for subtler bloom
```

## Configuration

All tunable parameters are in `/home/user/cat-annihilation/shaders/common/constants.glsl`:

```glsl
// Lighting
const int MAX_POINT_LIGHTS = 256;
const int MAX_SPOT_LIGHTS = 128;

// Clustered lighting grid
const uint CLUSTER_GRID_X = 16;
const uint CLUSTER_GRID_Y = 9;
const uint CLUSTER_GRID_Z = 24;

// Shadows
const float SHADOW_BIAS = 0.005;
const int PCF_KERNEL_SIZE = 5;

// Post-processing
const int BLOOM_MIP_LEVELS = 6;
const float BLOOM_THRESHOLD = 1.0;
const float BLOOM_INTENSITY = 0.04;

// Particles
const uint MAX_PARTICLES = 1048576;  // 1M
```

## Performance Tuning

### High-end (Desktop)
- Cluster grid: 16x9x24
- PCF kernel: 5x5
- Bloom mips: 6
- Max particles: 1M
- FXAA: Enabled

### Mid-range
- Cluster grid: 16x9x16
- PCF kernel: 3x3
- Bloom mips: 4
- Max particles: 256K
- FXAA: Enabled

### Low-end (Mobile)
- Cluster grid: 8x5x12
- PCF kernel: 2x2
- Bloom mips: 3
- Max particles: 64K
- FXAA: Disabled

## Documentation Files

- **README.md** - Complete documentation with examples
- **SHADER_INDEX.md** - Detailed description of every shader
- **IMPLEMENTATION_SUMMARY.md** - Technical specifications and checklist
- **QUICK_START.md** - This file

## Need Help?

1. Check the README.md for detailed explanations
2. Check SHADER_INDEX.md for shader-specific details
3. Look at shader comments - every function is documented
4. Common includes (common/*.glsl) have usage examples

## Example: Minimal Rendering Loop

```c
// 1. Shadow pass
render_shadows(shadow_depth.vert, shadow_depth.frag);

// 2. G-Buffer pass
render_gbuffer(gbuffer.vert, gbuffer.frag);

// 3. Cluster lights
dispatch_compute(clustered.comp);

// 4. Lighting
render_fullscreen(deferred.vert, deferred.frag);

// 5. Forward (transparent)
render_forward(forward.vert, transparent.frag);

// 6. Post-processing
for (int i = 0; i < 6; i++) {
    downsample(fullscreen.vert, bloom_downsample.frag);
}
for (int i = 5; i >= 0; i--) {
    upsample(fullscreen.vert, bloom_upsample.frag);
}
tonemap(fullscreen.vert, tonemap.frag);
fxaa(fullscreen.vert, fxaa.frag);

// 7. UI
render_ui(ui.vert, ui.frag);
```

## Ready to Go!

Your complete shader pipeline is ready. Just compile and integrate with your Vulkan renderer!

```bash
cd /home/user/cat-annihilation/shaders
./compile_shaders.sh
```
