#include "PhysicsWorld.hpp"
#include "../CudaError.hpp"

namespace CatEngine {
namespace Physics {

// ============================================================================
// Host Helper Functions
// ============================================================================

// Host version of make_float3
inline float3 host_make_float3(float x, float y, float z) {
    float3 v;
    v.x = x; v.y = y; v.z = z;
    return v;
}

// ============================================================================
// Device Helper Functions
// ============================================================================

__device__ __forceinline__ float3 make_float3(float x, float y, float z) {
    float3 v;
    v.x = x; v.y = y; v.z = z;
    return v;
}

// Custom atomicMin for floats (not natively supported)
__device__ __forceinline__ float atomicMinFloat(float* address, float val) {
    int* address_as_int = (int*)address;
    int old = *address_as_int, assumed;
    do {
        assumed = old;
        old = atomicCAS(address_as_int, assumed,
                        __float_as_int(fminf(val, __int_as_float(assumed))));
    } while (assumed != old);
    return __int_as_float(old);
}

__device__ __forceinline__ float3 operator+(float3 a, float3 b) {
    return make_float3(a.x + b.x, a.y + b.y, a.z + b.z);
}

__device__ __forceinline__ float3 operator-(float3 a, float3 b) {
    return make_float3(a.x - b.x, a.y - b.y, a.z - b.z);
}

__device__ __forceinline__ float3 operator*(float3 a, float s) {
    return make_float3(a.x * s, a.y * s, a.z * s);
}

__device__ __forceinline__ float dot(float3 a, float3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

__device__ __forceinline__ float length(float3 v) {
    return sqrtf(dot(v, v));
}

__device__ __forceinline__ float lengthSq(float3 v) {
    return dot(v, v);
}

__device__ __forceinline__ float3 normalize(float3 v) {
    float len = length(v);
    return len > 1e-6f ? v * (1.0f / len) : make_float3(0, 0, 0);
}

// ============================================================================
// GPU Raycast Kernels
// ============================================================================

/**
 * @brief Ray-Sphere intersection test
 */
__device__ bool intersectRaySphere(
    float3 rayOrigin,
    float3 rayDir,
    float3 sphereCenter,
    float sphereRadius,
    float& tHit
) {
    float3 oc = rayOrigin - sphereCenter;
    float a = dot(rayDir, rayDir);
    float b = 2.0f * dot(oc, rayDir);
    float c = dot(oc, oc) - sphereRadius * sphereRadius;
    float discriminant = b * b - 4 * a * c;

    if (discriminant < 0) {
        return false;
    }

    float t = (-b - sqrtf(discriminant)) / (2.0f * a);
    if (t >= 0) {
        tHit = t;
        return true;
    }

    return false;
}

/**
 * @brief Ray-AABB intersection test
 */
__device__ bool intersectRayAABB(
    float3 rayOrigin,
    float3 rayDir,
    float3 boxMin,
    float3 boxMax,
    float& tMin,
    float& tMax
) {
    float3 invDir = make_float3(1.0f / rayDir.x, 1.0f / rayDir.y, 1.0f / rayDir.z);

    float t1 = (boxMin.x - rayOrigin.x) * invDir.x;
    float t2 = (boxMax.x - rayOrigin.x) * invDir.x;
    float t3 = (boxMin.y - rayOrigin.y) * invDir.y;
    float t4 = (boxMax.y - rayOrigin.y) * invDir.y;
    float t5 = (boxMin.z - rayOrigin.z) * invDir.z;
    float t6 = (boxMax.z - rayOrigin.z) * invDir.z;

    tMin = fmaxf(fmaxf(fminf(t1, t2), fminf(t3, t4)), fminf(t5, t6));
    tMax = fminf(fminf(fmaxf(t1, t2), fmaxf(t3, t4)), fmaxf(t5, t6));

    // Ray intersects AABB if tMax >= tMin and tMax >= 0
    return tMax >= tMin && tMax >= 0;
}

/**
 * @brief GPU raycast kernel
 *
 * Performs parallel raycast against all bodies.
 * Each thread tests one body, then results are reduced.
 */
__global__ void raycastKernel(
    float3 rayOrigin,
    float3 rayDir,
    float maxDistance,
    const float3* positions,
    const float4* rotations,
    const int* colliderTypes,
    const float4* colliderParams,
    const float3* colliderOffsets,
    int bodyCount,
    // Output
    int* hitBodyIndex,
    float* hitDistance,
    float3* hitPoint,
    float3* hitNormal
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;

    __shared__ int sharedBestIdx[256];
    __shared__ float sharedBestDist[256];

    int threadIdx_x = threadIdx.x;

    // Initialize thread-local best hit
    float localBestDist = maxDistance;
    int localBestIdx = -1;
    float3 localHitPoint = make_float3(0, 0, 0);
    float3 localHitNormal = make_float3(0, 1, 0);

    if (idx < bodyCount) {
        int type = colliderTypes[idx];
        float4 params = colliderParams[idx];

        // Simple sphere raycast
        if (type == (int)ColliderType::Sphere) {
            float3 sphereCenter = positions[idx];
            float sphereRadius = params.x;

            float t;
            if (intersectRaySphere(rayOrigin, rayDir, sphereCenter, sphereRadius, t)) {
                if (t >= 0 && t < localBestDist) {
                    localBestDist = t;
                    localBestIdx = idx;
                    localHitPoint = rayOrigin + rayDir * t;
                    localHitNormal = normalize(localHitPoint - sphereCenter);
                }
            }
        }
        // For box/capsule, would implement full intersection tests here
    }

    // Store thread results in shared memory
    sharedBestIdx[threadIdx_x] = localBestIdx;
    sharedBestDist[threadIdx_x] = localBestDist;
    __syncthreads();

    // Reduce within block to find best hit
    for (int stride = blockDim.x / 2; stride > 0; stride >>= 1) {
        if (threadIdx_x < stride) {
            if (sharedBestDist[threadIdx_x + stride] < sharedBestDist[threadIdx_x]) {
                sharedBestIdx[threadIdx_x] = sharedBestIdx[threadIdx_x + stride];
                sharedBestDist[threadIdx_x] = sharedBestDist[threadIdx_x + stride];
            }
        }
        __syncthreads();
    }

    // Thread 0 of each block writes block's best result
    if (threadIdx_x == 0) {
        // Atomic min on distance to find global best
        float oldDist = atomicMinFloat(hitDistance, sharedBestDist[0]);
        if (sharedBestDist[0] < oldDist) {
            atomicExch(hitBodyIndex, sharedBestIdx[0]);
            // Would also store hit point and normal here
        }
    }
}

// ============================================================================
// Host Functions
// ============================================================================

/**
 * @brief Perform GPU-accelerated raycast (currently unused, for future optimization)
 */
void gpuRaycast(
    const GpuRigidBodies& bodies,
    const Engine::Ray& ray,
    float maxDistance,
    RaycastHit& hit,
    cudaStream_t stream
) {
    if (bodies.count == 0) {
        hit = RaycastHit();
        return;
    }

    // Allocate output buffers
    int* d_hitBodyIndex;
    float* d_hitDistance;
    float3* d_hitPoint;
    float3* d_hitNormal;

    CUDA_CHECK(cudaMalloc(&d_hitBodyIndex, sizeof(int)));
    CUDA_CHECK(cudaMalloc(&d_hitDistance, sizeof(float)));
    CUDA_CHECK(cudaMalloc(&d_hitPoint, sizeof(float3)));
    CUDA_CHECK(cudaMalloc(&d_hitNormal, sizeof(float3)));

    // Initialize with no hit
    int noHit = -1;
    CUDA_CHECK(cudaMemcpyAsync(d_hitBodyIndex, &noHit, sizeof(int),
                               cudaMemcpyHostToDevice, stream));
    CUDA_CHECK(cudaMemcpyAsync(d_hitDistance, &maxDistance, sizeof(float),
                               cudaMemcpyHostToDevice, stream));

    // Launch kernel
    const int blockSize = 256;
    const int numBlocks = (bodies.count + blockSize - 1) / blockSize;

    float3 rayOrigin = host_make_float3(ray.origin.x, ray.origin.y, ray.origin.z);
    float3 rayDir = host_make_float3(ray.direction.x, ray.direction.y, ray.direction.z);

    raycastKernel<<<numBlocks, blockSize, 0, stream>>>(
        rayOrigin,
        rayDir,
        maxDistance,
        bodies.positions,
        bodies.rotations,
        bodies.colliderTypes,
        bodies.colliderParams,
        bodies.colliderOffsets,
        bodies.count,
        d_hitBodyIndex,
        d_hitDistance,
        d_hitPoint,
        d_hitNormal
    );
    CUDA_CHECK_LAST();

    // Download results
    int hitBodyIndex;
    float hitDistance;
    float3 hitPoint;
    float3 hitNormal;

    CUDA_CHECK(cudaMemcpyAsync(&hitBodyIndex, d_hitBodyIndex, sizeof(int),
                               cudaMemcpyDeviceToHost, stream));
    CUDA_CHECK(cudaMemcpyAsync(&hitDistance, d_hitDistance, sizeof(float),
                               cudaMemcpyDeviceToHost, stream));
    CUDA_CHECK(cudaMemcpyAsync(&hitPoint, d_hitPoint, sizeof(float3),
                               cudaMemcpyDeviceToHost, stream));
    CUDA_CHECK(cudaMemcpyAsync(&hitNormal, d_hitNormal, sizeof(float3),
                               cudaMemcpyDeviceToHost, stream));

    // Wait for completion
    CUDA_CHECK(cudaStreamSynchronize(stream));

    // Fill output
    hit.bodyIndex = hitBodyIndex;
    hit.distance = hitDistance;
    hit.point = Engine::vec3(hitPoint.x, hitPoint.y, hitPoint.z);
    hit.normal = Engine::vec3(hitNormal.x, hitNormal.y, hitNormal.z);

    // Free buffers
    cudaFree(d_hitBodyIndex);
    cudaFree(d_hitDistance);
    cudaFree(d_hitPoint);
    cudaFree(d_hitNormal);
}

} // namespace Physics
} // namespace CatEngine
