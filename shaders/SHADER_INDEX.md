# Complete Shader Index

## Common Utilities (4 files)

### `/home/user/cat-annihilation/shaders/common/constants.glsl`
- Mathematical constants (PI, EPSILON, etc.)
- Lighting constants (max lights, shadow bias, etc.)
- Clustered lighting grid dimensions (16x9x24)
- Post-processing constants
- Particle and culling limits

### `/home/user/cat-annihilation/shaders/common/utils.glsl`
- Octahedron normal encoding/decoding
- Depth linearization and reconstruction
- World/view position from depth
- Luminance calculation
- Safe normalization
- Value remapping
- Gamma correction (sRGB ↔ linear)
- Hash functions for random numbers
- Interleaved gradient noise
- Cluster index computation

### `/home/user/cat-annihilation/shaders/common/brdf.glsl`
- Fresnel-Schlick approximation
- Fresnel-Schlick with roughness (IBL)
- GGX/Trowbridge-Reitz normal distribution
- Schlick-GGX geometry function
- Smith's geometry function
- Full Cook-Torrance BRDF
- GGX importance sampling
- Environment BRDF for IBL

### `/home/user/cat-annihilation/shaders/common/noise.glsl`
- 2D/3D Perlin noise
- Fractional Brownian Motion (fBm)
- Simplex noise (optimized)
- Worley/Cellular noise
- Voronoi noise with distances
- Turbulence function

---

## Geometry Pass (5 files)

### `/home/user/cat-annihilation/shaders/geometry/gbuffer.vert`
**Purpose**: Standard geometry vertex shader for G-Buffer generation
- Transforms vertices to world/clip space
- Calculates TBN matrix for normal mapping
- Outputs: world position, normal, UV, TBN matrix

### `/home/user/cat-annihilation/shaders/geometry/gbuffer.frag`
**Purpose**: G-Buffer fragment shader (4 render targets)
- Samples material textures (albedo, normal, metallic/roughness, AO, emission)
- Applies normal mapping
- Outputs to 4 render targets:
  - RT0: position + depth
  - RT1: encoded normal + roughness
  - RT2: albedo + metallic
  - RT3: emission + AO

### `/home/user/cat-annihilation/shaders/geometry/skinned.vert`
**Purpose**: Skeletal animation vertex shader
- Supports up to 256 bone matrices
- Applies skinning to position, normal, tangent, bitangent
- 4 bone influences per vertex with weights

### `/home/user/cat-annihilation/shaders/geometry/terrain.vert`
**Purpose**: Heightmap-based terrain vertex shader
- Samples heightmap for vertex displacement
- Calculates normals from heightmap derivatives
- Supports LOD and texture coordinate scaling

### `/home/user/cat-annihilation/shaders/geometry/terrain.frag`
**Purpose**: Multi-texture terrain fragment shader
- 4-layer terrain texturing with splat map
- Triplanar mapping option
- Per-layer normal mapping
- Blends albedo, normals, and material properties

---

## Lighting (4 files)

### `/home/user/cat-annihilation/shaders/lighting/deferred.vert`
**Purpose**: Fullscreen triangle vertex shader
- Generates fullscreen triangle covering NDC
- Used for all deferred lighting passes

### `/home/user/cat-annihilation/shaders/lighting/deferred.frag`
**Purpose**: Main deferred lighting fragment shader
- Reconstructs G-Buffer data
- Directional light with cascaded shadows
- Clustered point and spot lights
- Full PBR evaluation with Cook-Torrance BRDF
- Supports 256 point lights + 128 spot lights

### `/home/user/cat-annihilation/shaders/lighting/clustered.comp`
**Purpose**: Clustered lighting compute shader (16x9x24 grid)
- Assigns lights to frustum clusters
- Sphere-AABB intersection for point lights
- Cone-AABB intersection for spot lights
- Logarithmic Z-distribution for depth
- Outputs light index lists per cluster

### `/home/user/cat-annihilation/shaders/lighting/ambient.frag`
**Purpose**: Image-based lighting (IBL)
- Diffuse: irradiance map sampling
- Specular: prefiltered environment map + BRDF LUT
- Fresnel-based energy conservation
- AO and intensity control

---

## Shadows (3 files)

### `/home/user/cat-annihilation/shaders/shadows/shadow_depth.vert`
**Purpose**: Shadow depth vertex shader
- Renders depth from light's perspective
- Supports skinned meshes
- Per-cascade rendering

### `/home/user/cat-annihilation/shaders/shadows/shadow_depth.frag`
**Purpose**: Shadow depth fragment shader
- Optional alpha testing for vegetation
- Depth written automatically to depth buffer

### `/home/user/cat-annihilation/shaders/shadows/pcf.glsl`
**Purpose**: Percentage-Closer Filtering functions
- 5x5 PCF with Poisson disk sampling (25 taps)
- Cascaded shadow map sampling
- Adaptive bias based on surface normal
- Cascade blending for smooth transitions
- Contact hardening shadow (PCSS-like)

---

## Forward Rendering (3 files)

### `/home/user/cat-annihilation/shaders/forward/forward.vert`
**Purpose**: Forward rendering vertex shader
- Standard vertex transformations
- Shadow map coordinate calculation
- TBN matrix for normal mapping

### `/home/user/cat-annihilation/shaders/forward/forward.frag`
**Purpose**: Forward rendering fragment shader
- Full lighting in single pass
- Directional light with shadows
- Point and spot lights (limited to 32 each for performance)
- PBR material evaluation
- Used for objects that don't fit deferred pipeline

### `/home/user/cat-annihilation/shaders/forward/transparent.frag`
**Purpose**: Transparent material fragment shader
- Refraction with IOR (index of refraction)
- Reflection from environment map
- Fresnel-based blend
- Multiple blend modes (alpha, additive, multiply)
- Drop shadows and outlines

---

## Post-Processing (5 files)

### `/home/user/cat-annihilation/shaders/postprocess/fullscreen.vert`
**Purpose**: Fullscreen triangle for post-processing
- Used by all post-processing effects
- Generates fullscreen triangle

### `/home/user/cat-annihilation/shaders/postprocess/tonemap.frag`
**Purpose**: Tonemapping fragment shader
- ACES filmic tonemapping
- Reinhard (simple and extended)
- Uncharted 2 filmic
- Exposure control
- Gamma correction

### `/home/user/cat-annihilation/shaders/postprocess/bloom_downsample.frag`
**Purpose**: Bloom downsample pass
- 13-tap dual filter downsample
- Quadratic threshold function (knee)
- First pass extracts bright areas
- Subsequent passes downsample mip chain

### `/home/user/cat-annihilation/shaders/postprocess/bloom_upsample.frag`
**Purpose**: Bloom upsample pass
- 9-tap dual filter upsample (tent filter)
- Progressive upsampling with additive blending
- Configurable filter radius
- Intensity control

### `/home/user/cat-annihilation/shaders/postprocess/fxaa.frag`
**Purpose**: FXAA 3.11 anti-aliasing
- Fast approximate anti-aliasing
- Edge detection via luminance
- Sub-pixel anti-aliasing
- Configurable quality settings
- Horizontal/vertical edge handling

---

## Compute Shaders (2 files)

### `/home/user/cat-annihilation/shaders/compute/culling.comp`
**Purpose**: GPU frustum and occlusion culling
- Sphere frustum test (quick rejection)
- AABB frustum test (accurate)
- Optional Hi-Z occlusion culling
- Outputs visible instance indices
- Atomic counter for draw calls
- Supports up to 65,536 instances

### `/home/user/cat-annihilation/shaders/compute/particle_update.comp`
**Purpose**: GPU particle system simulation
- Position/velocity integration
- Lifetime management
- Force fields (point, directional, vortex, noise)
- Collision detection (planes and spheres)
- Particle emission with randomization
- Supports up to 1M particles

---

## Sky Rendering (3 files)

### `/home/user/cat-annihilation/shaders/sky/skybox.vert`
**Purpose**: Skybox vertex shader
- Removes translation for infinite distance
- Forces depth to far plane
- Position as cubemap coordinate

### `/home/user/cat-annihilation/shaders/sky/skybox.frag`
**Purpose**: Cubemap skybox fragment shader
- Samples cubemap texture
- Y-axis rotation support
- Intensity control

### `/home/user/cat-annihilation/shaders/sky/atmosphere.frag`
**Purpose**: Physical atmospheric scattering
- Simplified Bruneton atmospheric model
- Rayleigh scattering (sky blue)
- Mie scattering (haze)
- Ray marching through atmosphere
- Sun disk rendering
- Physically-based parameters

---

## UI Rendering (3 files)

### `/home/user/cat-annihilation/shaders/ui/ui.vert`
**Purpose**: 2D UI vertex shader
- Orthographic projection
- 2D transformations (position, scale, rotation)
- Z-depth for layering

### `/home/user/cat-annihilation/shaders/ui/ui.frag`
**Purpose**: UI fragment shader
- Texture sampling
- Color tinting
- Multiple blend modes
- Opacity control

### `/home/user/cat-annihilation/shaders/ui/text_sdf.frag`
**Purpose**: SDF text rendering
- Signed distance field text
- Sharp rendering at any scale
- Outline support
- Drop shadow
- Configurable smoothing

---

## Summary Statistics

- **Total Files**: 32 shaders
- **Total Lines**: ~3,425 lines of code
- **Languages**: GLSL 4.50 (Vulkan)

### By Category:
- Common utilities: 4 files
- Geometry pass: 5 files
- Lighting: 4 files
- Shadows: 3 files
- Forward rendering: 3 files
- Post-processing: 5 files
- Compute shaders: 2 files
- Sky rendering: 3 files
- UI rendering: 3 files

### By Shader Type:
- Vertex shaders (.vert): 9 files
- Fragment shaders (.frag): 17 files
- Compute shaders (.comp): 2 files
- Library/include (.glsl): 4 files

---

## Integration Points

### Required Uniform Buffer Objects (UBOs):

**CameraData** (set 0, binding 0):
```glsl
struct CameraData {
    mat4 view;
    mat4 projection;
    mat4 viewProj;
    mat4 invViewProj;
    vec3 cameraPos;
    float nearPlane;
    float farPlane;
};
```

**LightData** (set 0, binding 1):
```glsl
struct LightData {
    DirectionalLight directionalLight;
    uint pointLightCount;
    uint spotLightCount;
    PointLight pointLights[256];
    SpotLight spotLights[128];
};
```

### Render Targets:

**G-Buffer** (4 attachments):
- Format: R16G16B16A16_SFLOAT or R32G32B32A32_SFLOAT
- All attachments same resolution

**Shadow Maps**:
- Format: D32_SFLOAT or D24_UNORM
- Array texture with 4 layers (cascades)

**Post-processing**:
- HDR format: R16G16B16A16_SFLOAT
- LDR output: R8G8B8A8_UNORM or B8G8R8A8_SRGB
