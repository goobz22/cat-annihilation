#pragma once

/**
 * Mock CUDA API - Stub implementations for testing without GPU
 *
 * This mock provides stub implementations of CUDA functions
 * to allow testing of game logic without GPU hardware.
 */

#ifdef USE_MOCK_GPU

#include <cstddef>

// Mock CUDA types
typedef void* CUdeviceptr;
typedef int CUdevice;
typedef void* CUcontext;
typedef void* CUstream;
typedef void* CUmodule;
typedef void* CUfunction;

// Mock CUDA error codes
enum cudaError_t {
    cudaSuccess = 0,
    cudaErrorMemoryAllocation = 2,
    cudaErrorInvalidValue = 11
};

// Mock CUDA functions (no-ops)
namespace MockCUDA {
    inline cudaError_t cudaMalloc(void** ptr, size_t size) {
        *ptr = malloc(size);
        return cudaSuccess;
    }

    inline cudaError_t cudaFree(void* ptr) {
        free(ptr);
        return cudaSuccess;
    }

    inline cudaError_t cudaMemcpy(void* dst, const void* src, size_t size, int kind) {
        memcpy(dst, src, size);
        return cudaSuccess;
    }

    inline cudaError_t cudaMemset(void* ptr, int value, size_t size) {
        memset(ptr, value, size);
        return cudaSuccess;
    }

    inline cudaError_t cudaDeviceSynchronize() {
        return cudaSuccess;
    }

    inline cudaError_t cudaStreamCreate(CUstream* stream) {
        *stream = nullptr;
        return cudaSuccess;
    }

    inline cudaError_t cudaStreamDestroy(CUstream stream) {
        return cudaSuccess;
    }

    inline cudaError_t cudaStreamSynchronize(CUstream stream) {
        return cudaSuccess;
    }
}

// Map mock functions to CUDA names
#define cudaMalloc MockCUDA::cudaMalloc
#define cudaFree MockCUDA::cudaFree
#define cudaMemcpy MockCUDA::cudaMemcpy
#define cudaMemset MockCUDA::cudaMemset
#define cudaDeviceSynchronize MockCUDA::cudaDeviceSynchronize
#define cudaStreamCreate MockCUDA::cudaStreamCreate
#define cudaStreamDestroy MockCUDA::cudaStreamDestroy
#define cudaStreamSynchronize MockCUDA::cudaStreamSynchronize

#endif // USE_MOCK_GPU
