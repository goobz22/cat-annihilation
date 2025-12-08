# Engine Math Library

A comprehensive, SIMD-optimized math library for the Cat Annihilation game engine, built with C++20 and SSE4.1 intrinsics.

## Features

- **SIMD-Optimized Vectors**: vec2, vec3, vec4 with SSE acceleration
- **Matrix Operations**: mat3, mat4 with camera and projection matrices
- **Quaternion Math**: Rotation operations with slerp interpolation
- **Transforms**: Complete 3D transformation system (position, rotation, scale)
- **Collision Detection**: AABB with ray and sphere intersection tests
- **View Frustum Culling**: Frustum extraction and containment tests
- **Ray Casting**: Ray-sphere, ray-plane, ray-triangle, ray-AABB tests
- **Procedural Noise**: Perlin and Simplex noise with octave support

## Files

| File | Description |
|------|-------------|
| `Math.hpp` | Core math utilities, constants, and helper functions |
| `Vector.hpp` | SIMD-optimized 2D, 3D, and 4D vectors |
| `Matrix.hpp` | 3x3 and 4x4 matrices with camera functions |
| `Quaternion.hpp` | Rotation quaternions with slerp and euler conversion |
| `Transform.hpp` | 3D transformation class combining position, rotation, scale |
| `AABB.hpp` | Axis-aligned bounding boxes with intersection tests |
| `Frustum.hpp` | View frustum for culling with plane extraction |
| `Ray.hpp` | Ray casting with multiple intersection tests |
| `Noise.hpp` | Perlin and Simplex noise implementations |

## Compilation

```bash
g++ -std=c++20 -msse4.1 -O2 -o your_program your_program.cpp
```

### Required Flags
- `-std=c++20`: C++20 standard support
- `-msse4.1`: SSE4.1 intrinsics for SIMD optimization
- `-O2` or `-O3`: Optimization level for best performance

## Usage Examples

### Vectors

```cpp
#include "Vector.hpp"
using namespace Engine;

// Create vectors
vec3 a(1.0f, 2.0f, 3.0f);
vec3 b(4.0f, 5.0f, 6.0f);

// Operations (SIMD-accelerated)
vec3 sum = a + b;
vec3 scaled = a * 2.0f;
float dotProduct = a.dot(b);
vec3 crossProduct = a.cross(b);

// Normalize
vec3 normalized = a.normalized();

// Interpolation
vec3 interpolated = vec3::lerp(a, b, 0.5f);
```

### Matrices

```cpp
#include "Matrix.hpp"
using namespace Engine;

// Projection matrix
mat4 projection = mat4::perspective(
    Math::radians(45.0f),  // FOV
    16.0f / 9.0f,          // Aspect ratio
    0.1f,                   // Near plane
    100.0f                  // Far plane
);

// View matrix
mat4 view = mat4::lookAt(
    vec3(0, 0, 5),  // Eye position
    vec3(0, 0, 0),  // Target
    vec3(0, 1, 0)   // Up vector
);

// Model transformations
mat4 model = mat4::translate(vec3(1, 2, 3));
model = model * mat4::rotateY(Math::radians(45.0f));
model = model * mat4::scale(vec3(2, 2, 2));
```

### Quaternions

```cpp
#include "Quaternion.hpp"
using namespace Engine;

// From euler angles
Quaternion q1 = Quaternion::fromEuler(0.0f, Math::radians(45.0f), 0.0f);

// From axis-angle
Quaternion q2 = Quaternion::fromAxisAngle(vec3(0, 1, 0), Math::radians(90.0f));

// Spherical interpolation
Quaternion interpolated = Quaternion::slerp(q1, q2, 0.5f);

// Rotate a vector
vec3 rotated = q1.rotate(vec3(1, 0, 0));

// Convert to matrix
mat4 rotationMatrix = q1.toMatrix();

// Convert back to euler
vec3 euler = q1.toEuler();
```

### Transforms

```cpp
#include "Transform.hpp"
using namespace Engine;

Transform transform;
transform.position = vec3(1, 2, 3);
transform.rotation = Quaternion::fromEuler(0, Math::radians(45.0f), 0);
transform.scale = vec3(2, 2, 2);

// Transform points and vectors
vec3 transformedPoint = transform.transformPoint(vec3(1, 0, 0));
vec3 transformedDir = transform.transformDirection(vec3(0, 1, 0));

// Get transformation matrix
mat4 matrix = transform.toMatrix();

// Combine transforms
Transform combined = parentTransform * childTransform;

// Interpolate transforms
Transform interpolated = Transform::lerp(t1, t2, 0.5f);
```

### AABB Collision

```cpp
#include "AABB.hpp"
using namespace Engine;

AABB box1(vec3(-1, -1, -1), vec3(1, 1, 1));
AABB box2(vec3(0, 0, 0), vec3(2, 2, 2));

// AABB-AABB intersection
if (box1.intersects(box2)) {
    // Collision detected
}

// Point containment
if (box1.contains(vec3(0, 0, 0))) {
    // Point is inside
}

// Sphere intersection
if (box1.intersectsSphere(vec3(2, 0, 0), 1.5f)) {
    // Sphere intersects AABB
}

// Expand AABB
box1.expand(vec3(5, 5, 5));

// Get properties
vec3 center = box1.center();
vec3 extents = box1.extents();
float volume = box1.volume();
```

### Ray Casting

```cpp
#include "Ray.hpp"
using namespace Engine;

Ray ray(vec3(0, 0, 0), vec3(0, 0, -1));

// Ray-sphere intersection
float t;
if (ray.intersectsSphere(vec3(0, 0, -5), 1.0f, t)) {
    vec3 hitPoint = ray.at(t);
}

// Ray-AABB intersection
float tMin, tMax;
if (ray.intersectsAABB(aabbMin, aabbMax, tMin, tMax)) {
    vec3 entryPoint = ray.at(tMin);
    vec3 exitPoint = ray.at(tMax);
}

// Ray-triangle intersection
vec3 barycentric;
if (ray.intersectsTriangle(v0, v1, v2, t, barycentric)) {
    vec3 hitPoint = ray.at(t);
}

// Ray-plane intersection
if (ray.intersectsPlane(planeNormal, planeDistance, t)) {
    vec3 hitPoint = ray.at(t);
}
```

### Frustum Culling

```cpp
#include "Frustum.hpp"
using namespace Engine;

// Extract frustum from view-projection matrix
mat4 viewProjection = projection * view;
Frustum frustum = Frustum::fromMatrix(viewProjection);

// Test AABB visibility
if (frustum.intersectsAABB(objectAABB)) {
    // Object is visible, render it
}

// Test sphere visibility
if (frustum.intersectsSphere(sphereCenter, sphereRadius)) {
    // Sphere is visible
}

// Test point containment
if (frustum.contains(point)) {
    // Point is inside frustum
}
```

### Noise Generation

```cpp
#include "Noise.hpp"
using namespace Engine;

// Perlin noise
Noise::Perlin perlin(12345); // Optional seed
float value1 = perlin.noise(x, y, z);

// Octave Perlin (fractal Brownian motion)
float terrain = perlin.octave(x, y, z,
    4,      // octaves
    0.5f    // persistence
);

// Simplex noise (faster)
Noise::Simplex simplex;
float value2 = simplex.noise(x, y, z);

// Octave simplex
float clouds = simplex.octave(x, y,
    6,      // octaves
    0.5f    // persistence
);

// Value noise
float simple = Noise::valueNoise(x, y, seed);

// Cellular/Worley noise
float cells = Noise::cellularNoise(x, y, seed);
```

## Performance Considerations

### SIMD Optimization
- vec3 and vec4 use `__m128` SSE intrinsics for vectorized operations
- Vectors are aligned to 16-byte boundaries for optimal SIMD performance
- Component-wise operations are SIMD-accelerated where beneficial

### Best Practices
1. **Normalization**: Always normalize quaternions after multiple operations
2. **Matrix Inverse**: Cache inverse matrices when used repeatedly
3. **Frustum Culling**: Perform AABB tests before more expensive sphere tests
4. **Noise**: Use Simplex over Perlin for better performance
5. **Transforms**: Convert to matrices once, not per-vertex

## Constants

Available in `Math.hpp`:
- `Math::PI` - π (3.14159...)
- `Math::TWO_PI` - 2π
- `Math::HALF_PI` - π/2
- `Math::DEG_TO_RAD` - Degrees to radians multiplier
- `Math::RAD_TO_DEG` - Radians to degrees multiplier
- `Math::EPSILON` - Small value for floating-point comparisons (1e-6)

## Utility Functions

### Math Utilities
```cpp
float clamped = Math::clamp(value, min, max);
float interpolated = Math::lerp(a, b, t);
float smooth = Math::smoothstep(edge0, edge1, x);
float rads = Math::radians(degrees);
float degs = Math::degrees(radians);
bool equal = Math::approximately(a, b); // Epsilon comparison
```

## Testing

Run the included test suite:

```bash
cd engine/math
g++ -std=c++20 -msse4.1 -O2 -o test_math test_math.cpp
./test_math
```

## License

Part of the Cat Annihilation game engine.

## Author

Created for the Cat Annihilation CUDA/Vulkan game engine project.
