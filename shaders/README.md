# Cat Annihilation - GLSL Shader Library

Complete Vulkan shader pipeline for the Cat Annihilation game engine.

## Overview

This directory contains all GLSL shaders (GLSL 4.50 with Vulkan layout qualifiers) for a modern deferred rendering pipeline with PBR materials, clustered lighting, cascaded shadow maps, and post-processing effects.

**Total: 32 shader files | ~3,425 lines of code**

## Directory Structure

```
shaders/
├── common/              # Shared utilities and constants
│   ├── constants.glsl   # Mathematical and rendering constants
│   ├── utils.glsl       # Utility functions (normal encoding, depth, etc.)
│   ├── brdf.glsl        # Cook-Torrance PBR BRDF functions
│   └── noise.glsl       # Perlin, Simplex, Worley noise functions
│
├── geometry/            # G-Buffer generation (deferred rendering)
│   ├── gbuffer.vert     # Standard geometry vertex shader
│   ├── gbuffer.frag     # G-Buffer fragment shader (4 render targets)
│   ├── skinned.vert     # Skeletal animation vertex shader
│   ├── terrain.vert     # Heightmap terrain vertex shader
│   └── terrain.frag     # Multi-texture terrain with triplanar mapping
│
├── lighting/            # Deferred lighting passes
│   ├── deferred.vert    # Fullscreen quad vertex shader
│   ├── deferred.frag    # Main deferred lighting fragment shader
│   ├── clustered.comp   # Clustered light assignment (16x9x24 grid)
│   └── ambient.frag     # Image-based lighting (IBL) with environment maps
│
├── shadows/             # Shadow mapping
│   ├── shadow_depth.vert # Shadow depth rendering
│   ├── shadow_depth.frag # Optional alpha testing for shadows
│   └── pcf.glsl         # 5x5 PCF filtering with cascaded shadows
│
├── forward/             # Forward rendering path
│   ├── forward.vert     # Forward rendering vertex shader
│   ├── forward.frag     # Forward rendering with lighting
│   └── transparent.frag # Transparent materials with refraction
│
├── postprocess/         # Post-processing effects
│   ├── fullscreen.vert  # Fullscreen triangle vertex shader
│   ├── tonemap.frag     # ACES/Reinhard/Uncharted2 tonemapping
│   ├── bloom_downsample.frag # 13-tap dual filter downsample
│   ├── bloom_upsample.frag   # 9-tap dual filter upsample
│   └── fxaa.frag        # FXAA 3.11 anti-aliasing
│
├── compute/             # Compute shaders
│   ├── culling.comp     # GPU frustum & occlusion culling
│   └── particle_update.comp # GPU particle system simulation
│
├── sky/                 # Sky rendering
│   ├── skybox.vert      # Skybox vertex shader
│   ├── skybox.frag      # Cubemap skybox
│   └── atmosphere.frag  # Physical atmospheric scattering
│
└── ui/                  # User interface rendering
    ├── ui.vert          # 2D UI vertex shader
    ├── ui.frag          # Textured UI fragment shader
    └── text_sdf.frag    # SDF text rendering with outlines
```

## Key Features

### Rendering Pipeline
- **Deferred Rendering**: 4-target G-Buffer (position, normal, albedo, emission)
- **PBR Materials**: Cook-Torrance BRDF with GGX distribution
- **Clustered Lighting**: 16×9×24 frustum grid, supports 256 point lights + 128 spot lights
- **Cascaded Shadows**: 4-cascade shadow maps with PCF filtering
- **IBL**: Image-based lighting with irradiance and prefiltered environment maps

### G-Buffer Layout
- **RT0**: `vec4(worldPos.xyz, depth)`
- **RT1**: `vec4(encodedNormal.xy, roughness, 0.0)`
- **RT2**: `vec4(albedo.rgb, metallic)`
- **RT3**: `vec4(emission.rgb, ao)`

### Descriptor Set Layout
```glsl
// Set 0: Global data (all shaders)
binding 0: CameraData (UBO)
binding 1: LightData (UBO)

// Set 1: Material textures
binding 0-5: Texture samplers

// Set 2: Scene/effect specific
binding 0-N: Additional resources
```

## Compilation

All shaders are written for Vulkan and must be compiled with `glslc`:

```bash
# Single shader
glslc -fshader-stage=vert geometry/gbuffer.vert -o gbuffer.vert.spv
glslc -fshader-stage=frag geometry/gbuffer.frag -o gbuffer.frag.spv
glslc -fshader-stage=comp lighting/clustered.comp -o clustered.comp.spv

# Batch compilation script
./compile_shaders.sh
```

## Shader Compilation Script

Create `/home/user/cat-annihilation/shaders/compile_shaders.sh`:

```bash
#!/bin/bash
# Compile all shaders to SPIR-V

OUTPUT_DIR="compiled"
mkdir -p $OUTPUT_DIR

# Vertex shaders
for file in geometry/*.vert forward/*.vert sky/*.vert ui/*.vert lighting/*.vert postprocess/*.vert shadows/*.vert; do
    if [ -f "$file" ]; then
        output="$OUTPUT_DIR/$(basename $file).spv"
        echo "Compiling $file..."
        glslc -fshader-stage=vert "$file" -o "$output"
    fi
done

# Fragment shaders
for file in geometry/*.frag forward/*.frag sky/*.frag ui/*.frag lighting/*.frag postprocess/*.frag shadows/*.frag; do
    if [ -f "$file" ]; then
        output="$OUTPUT_DIR/$(basename $file).spv"
        echo "Compiling $file..."
        glslc -fshader-stage=frag "$file" -o "$output"
    fi
done

# Compute shaders
for file in compute/*.comp lighting/*.comp; do
    if [ -f "$file" ]; then
        output="$OUTPUT_DIR/$(basename $file).spv"
        echo "Compiling $file..."
        glslc -fshader-stage=comp "$file" -o "$output"
    fi
done

echo "Shader compilation complete!"
```

## Usage Examples

### Deferred Rendering Pipeline

1. **Geometry Pass**: Render to G-Buffer
   - Use `gbuffer.vert` + `gbuffer.frag` for standard geometry
   - Use `skinned.vert` + `gbuffer.frag` for animated characters
   - Use `terrain.vert` + `terrain.frag` for terrain

2. **Light Culling**: Assign lights to clusters
   - Dispatch `clustered.comp` to build light lists

3. **Lighting Pass**: Combine G-Buffer + lighting
   - Render fullscreen quad with `deferred.vert` + `deferred.frag`
   - Optionally add IBL with `ambient.frag`

4. **Forward Pass**: Transparent objects
   - Use `forward.vert` + `transparent.frag`

5. **Post-Processing**: Apply effects
   - Tonemap: `fullscreen.vert` + `tonemap.frag`
   - Bloom: Downsample/upsample chain
   - FXAA: `fullscreen.vert` + `fxaa.frag`

## Performance Notes

- **Clustered Lighting**: Configure grid size in `common/constants.glsl` (default: 16×9×24)
- **Shadow Quality**: Adjust `PCF_KERNEL_SIZE` in `common/constants.glsl` (default: 5×5)
- **Bloom Iterations**: Use 6 mip levels for good quality/performance balance
- **Particle System**: Supports up to 1M particles with GPU simulation

## Technical Details

### Normal Encoding
Octahedron encoding for 16-bit normal storage (saves bandwidth):
```glsl
vec2 encoded = encodeNormal(worldNormal);  // Store in G-Buffer
vec3 normal = decodeNormal(encoded);       // Reconstruct in lighting pass
```

### Clustered Lighting
```glsl
// Logarithmic Z-distribution for better depth precision
uvec3 clusterIndex = computeClusterIndex(screenUV, viewDepth, near, far);
uint linearIndex = clusterIndexToLinear(clusterIndex);
// Fetch light list for this cluster
```

### PBR Material Model
```glsl
// Cook-Torrance specular BRDF
D = distributionGGX(N, H, roughness);      // GGX normal distribution
G = geometrySmith(N, V, L, roughness);     // Smith geometry function
F = fresnelSchlick(cosTheta, F0);          // Fresnel-Schlick approximation

specular = (D * G * F) / (4 * NdotV * NdotL);
```

## Dependencies

- **Vulkan SDK**: Required for `glslc` compiler
- **SPIR-V Tools**: Optional, for shader validation and optimization

## License

Part of the Cat Annihilation game engine.
