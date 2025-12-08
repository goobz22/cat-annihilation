// Minimal CUDA type stubs for compilation checking without CUDA Toolkit
// This file provides just enough types to make headers parse without linking
#ifndef CUDA_STUBS_H
#define CUDA_STUBS_H

#include <stddef.h>
#include <stdint.h>

// CUDA qualifiers
#define __global__
#define __device__
#define __host__
#define __shared__
#define __constant__
#define __managed__

// CUDA built-in types
typedef struct {
    unsigned int x, y, z;
} uint3;

typedef struct {
    int x, y, z;
} int3;

typedef struct {
    float x, y, z;
} float3;

typedef struct {
    float x, y, z, w;
} float4;

typedef struct {
    unsigned int x, y, z;
} dim3;

// CUDA built-in variables (just declarations)
#ifdef __CUDA_ARCH__
extern __device__ uint3 threadIdx;
extern __device__ uint3 blockIdx;
extern __device__ dim3 blockDim;
extern __device__ dim3 gridDim;
extern __device__ int warpSize;
#else
// Host-side stubs
static uint3 threadIdx;
static uint3 blockIdx;
static dim3 blockDim;
static dim3 gridDim;
static int warpSize;
#endif

// CUDA error codes
typedef enum cudaError {
    cudaSuccess = 0,
    cudaErrorMemoryAllocation = 2,
    cudaErrorInvalidValue = 11,
    cudaErrorInvalidDevicePointer = 17,
    cudaErrorInvalidMemcpyDirection = 21
} cudaError_t;

// CUDA memory copy kinds
typedef enum cudaMemcpyKind {
    cudaMemcpyHostToHost = 0,
    cudaMemcpyHostToDevice = 1,
    cudaMemcpyDeviceToHost = 2,
    cudaMemcpyDeviceToDevice = 3,
    cudaMemcpyDefault = 4
} cudaMemcpyKind;

// CUDA stream
typedef void* cudaStream_t;

// CUDA event
typedef void* cudaEvent_t;

// Stub function declarations
#ifdef __cplusplus
extern "C" {
#endif

cudaError_t cudaMalloc(void** devPtr, size_t size);
cudaError_t cudaFree(void* devPtr);
cudaError_t cudaMemcpy(void* dst, const void* src, size_t count, cudaMemcpyKind kind);
cudaError_t cudaMemcpyAsync(void* dst, const void* src, size_t count, cudaMemcpyKind kind, cudaStream_t stream);
cudaError_t cudaMemset(void* devPtr, int value, size_t count);
cudaError_t cudaDeviceSynchronize(void);
cudaError_t cudaGetLastError(void);
const char* cudaGetErrorString(cudaError_t error);
cudaError_t cudaStreamCreate(cudaStream_t* pStream);
cudaError_t cudaStreamDestroy(cudaStream_t stream);
cudaError_t cudaStreamSynchronize(cudaStream_t stream);

#ifdef __cplusplus
}
#endif

// CUDA math functions (host stubs)
#ifdef __cplusplus
inline __host__ __device__ float fminf(float a, float b) { return a < b ? a : b; }
inline __host__ __device__ float fmaxf(float a, float b) { return a > b ? a : b; }
inline __host__ __device__ float sqrtf(float x) { return 0.0f; }
inline __host__ __device__ float rsqrtf(float x) { return 0.0f; }
#endif

#endif // CUDA_STUBS_H
