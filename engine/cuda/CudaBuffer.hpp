#pragma once

#include "CudaError.hpp"
#include "CudaStream.hpp"
#include <cuda_runtime.h>
#include <memory>
#include <cstring>

namespace CatEngine {
namespace CUDA {

/**
 * @brief Memory type for CUDA buffers
 */
enum class MemoryType {
    Device,         // cudaMalloc - device memory only
    Pinned,         // cudaMallocHost - pinned host memory for fast transfers
    Managed         // cudaMallocManaged - unified memory accessible from CPU and GPU
};

/**
 * @brief Template class for typed GPU memory management
 *
 * @tparam T The type of elements stored in the buffer
 */
template<typename T>
class CudaBuffer {
public:
    /**
     * @brief Construct empty buffer
     */
    CudaBuffer()
        : m_ptr(nullptr)
        , m_size(0)
        , m_capacity(0)
        , m_memoryType(MemoryType::Device)
    {}

    /**
     * @brief Construct buffer with specified size and memory type
     */
    explicit CudaBuffer(size_t count, MemoryType memType = MemoryType::Device)
        : m_ptr(nullptr)
        , m_size(0)
        , m_capacity(0)
        , m_memoryType(memType)
    {
        allocate(count);
    }

    /**
     * @brief Construct buffer and copy from host data
     */
    CudaBuffer(const T* hostData, size_t count, MemoryType memType = MemoryType::Device)
        : m_ptr(nullptr)
        , m_size(0)
        , m_capacity(0)
        , m_memoryType(memType)
    {
        allocate(count);
        copyFromHost(hostData, count);
    }

    /**
     * @brief Destructor - frees GPU memory
     */
    ~CudaBuffer() {
        free();
    }

    // Non-copyable
    CudaBuffer(const CudaBuffer&) = delete;
    CudaBuffer& operator=(const CudaBuffer&) = delete;

    // Movable
    CudaBuffer(CudaBuffer&& other) noexcept
        : m_ptr(other.m_ptr)
        , m_size(other.m_size)
        , m_capacity(other.m_capacity)
        , m_memoryType(other.m_memoryType)
    {
        other.m_ptr = nullptr;
        other.m_size = 0;
        other.m_capacity = 0;
    }

    CudaBuffer& operator=(CudaBuffer&& other) noexcept {
        if (this != &other) {
            free();
            m_ptr = other.m_ptr;
            m_size = other.m_size;
            m_capacity = other.m_capacity;
            m_memoryType = other.m_memoryType;
            other.m_ptr = nullptr;
            other.m_size = 0;
            other.m_capacity = 0;
        }
        return *this;
    }

    /**
     * @brief Get device pointer
     */
    T* get() { return m_ptr; }
    const T* get() const { return m_ptr; }

    /**
     * @brief Get device pointer (implicit conversion)
     */
    operator T*() { return m_ptr; }
    operator const T*() const { return m_ptr; }

    /**
     * @brief Get number of elements
     */
    size_t size() const { return m_size; }

    /**
     * @brief Get capacity (allocated elements)
     */
    size_t capacity() const { return m_capacity; }

    /**
     * @brief Get size in bytes
     */
    size_t sizeBytes() const { return m_size * sizeof(T); }

    /**
     * @brief Get capacity in bytes
     */
    size_t capacityBytes() const { return m_capacity * sizeof(T); }

    /**
     * @brief Get memory type
     */
    MemoryType memoryType() const { return m_memoryType; }

    /**
     * @brief Check if buffer is empty
     */
    bool empty() const { return m_size == 0; }

    /**
     * @brief Resize buffer (reallocates if necessary)
     */
    void resize(size_t newSize) {
        if (newSize > m_capacity) {
            reallocate(newSize);
        }
        m_size = newSize;
    }

    /**
     * @brief Reserve capacity without changing size
     */
    void reserve(size_t newCapacity) {
        if (newCapacity > m_capacity) {
            reallocate(newCapacity);
        }
    }

    /**
     * @brief Copy data from host to device (synchronous)
     */
    void copyFromHost(const T* hostData, size_t count) {
        if (count > m_capacity) {
            resize(count);
        }
        m_size = count;
        CUDA_CHECK(cudaMemcpy(m_ptr, hostData, count * sizeof(T), cudaMemcpyHostToDevice));
    }

    /**
     * @brief Copy data from host to device (asynchronous)
     */
    void copyFromHostAsync(const T* hostData, size_t count, const CudaStream& stream) {
        if (count > m_capacity) {
            resize(count);
        }
        m_size = count;
        CUDA_CHECK(cudaMemcpyAsync(m_ptr, hostData, count * sizeof(T), cudaMemcpyHostToDevice, stream.get()));
    }

    /**
     * @brief Copy data from device to host (synchronous)
     */
    void copyToHost(T* hostData, size_t count) const {
        if (count > m_size) {
            count = m_size;
        }
        CUDA_CHECK(cudaMemcpy(hostData, m_ptr, count * sizeof(T), cudaMemcpyDeviceToHost));
    }

    /**
     * @brief Copy data from device to host (asynchronous)
     */
    void copyToHostAsync(T* hostData, size_t count, const CudaStream& stream) const {
        if (count > m_size) {
            count = m_size;
        }
        CUDA_CHECK(cudaMemcpyAsync(hostData, m_ptr, count * sizeof(T), cudaMemcpyDeviceToHost, stream.get()));
    }

    /**
     * @brief Copy data from another device buffer (synchronous)
     */
    void copyFromDevice(const T* deviceData, size_t count) {
        if (count > m_capacity) {
            resize(count);
        }
        m_size = count;
        CUDA_CHECK(cudaMemcpy(m_ptr, deviceData, count * sizeof(T), cudaMemcpyDeviceToDevice));
    }

    /**
     * @brief Copy data from another device buffer (asynchronous)
     */
    void copyFromDeviceAsync(const T* deviceData, size_t count, const CudaStream& stream) {
        if (count > m_capacity) {
            resize(count);
        }
        m_size = count;
        CUDA_CHECK(cudaMemcpyAsync(m_ptr, deviceData, count * sizeof(T), cudaMemcpyDeviceToDevice, stream.get()));
    }

    /**
     * @brief Copy from another CudaBuffer (synchronous)
     */
    void copyFrom(const CudaBuffer<T>& other) {
        copyFromDevice(other.get(), other.size());
    }

    /**
     * @brief Copy from another CudaBuffer (asynchronous)
     */
    void copyFromAsync(const CudaBuffer<T>& other, const CudaStream& stream) {
        copyFromDeviceAsync(other.get(), other.size(), stream);
    }

    /**
     * @brief Fill buffer with a value (synchronous)
     */
    void fill(const T& value) {
        if (m_memoryType == MemoryType::Managed || m_memoryType == MemoryType::Pinned) {
            // Can access directly for managed/pinned memory
            for (size_t i = 0; i < m_size; ++i) {
                m_ptr[i] = value;
            }
        } else {
            // For device memory, fill on host then copy
            std::vector<T> hostData(m_size, value);
            copyFromHost(hostData.data(), m_size);
        }
    }

    /**
     * @brief Set memory to zero (synchronous)
     */
    void zero() {
        CUDA_CHECK(cudaMemset(m_ptr, 0, sizeBytes()));
    }

    /**
     * @brief Set memory to zero (asynchronous)
     */
    void zeroAsync(const CudaStream& stream) {
        CUDA_CHECK(cudaMemsetAsync(m_ptr, 0, sizeBytes(), stream.get()));
    }

    /**
     * @brief Clear buffer (sets size to 0, keeps capacity)
     */
    void clear() {
        m_size = 0;
    }

    /**
     * @brief Free all memory
     */
    void free() {
        if (m_ptr != nullptr) {
            switch (m_memoryType) {
                case MemoryType::Device:
                    cudaFree(m_ptr);
                    break;
                case MemoryType::Pinned:
                    cudaFreeHost(m_ptr);
                    break;
                case MemoryType::Managed:
                    cudaFree(m_ptr);
                    break;
            }
            m_ptr = nullptr;
            m_size = 0;
            m_capacity = 0;
        }
    }

    /**
     * @brief Get direct access to data (only safe for Managed/Pinned memory)
     */
    T* data() {
        if (m_memoryType == MemoryType::Device) {
            throw std::runtime_error("Cannot access device memory directly from host");
        }
        return m_ptr;
    }

    const T* data() const {
        if (m_memoryType == MemoryType::Device) {
            throw std::runtime_error("Cannot access device memory directly from host");
        }
        return m_ptr;
    }

private:
    void allocate(size_t count) {
        if (count == 0) {
            return;
        }

        size_t bytes = count * sizeof(T);

        switch (m_memoryType) {
            case MemoryType::Device:
                CUDA_CHECK(cudaMalloc(&m_ptr, bytes));
                break;
            case MemoryType::Pinned:
                CUDA_CHECK(cudaMallocHost(&m_ptr, bytes));
                break;
            case MemoryType::Managed:
                CUDA_CHECK(cudaMallocManaged(&m_ptr, bytes));
                break;
        }

        m_size = count;
        m_capacity = count;
    }

    void reallocate(size_t newCapacity) {
        T* newPtr = nullptr;
        size_t bytes = newCapacity * sizeof(T);

        // Allocate new memory
        switch (m_memoryType) {
            case MemoryType::Device:
                CUDA_CHECK(cudaMalloc(&newPtr, bytes));
                break;
            case MemoryType::Pinned:
                CUDA_CHECK(cudaMallocHost(&newPtr, bytes));
                break;
            case MemoryType::Managed:
                CUDA_CHECK(cudaMallocManaged(&newPtr, bytes));
                break;
        }

        // Copy old data if exists
        if (m_ptr != nullptr && m_size > 0) {
            CUDA_CHECK(cudaMemcpy(newPtr, m_ptr, m_size * sizeof(T), cudaMemcpyDefault));

            // Free old memory
            switch (m_memoryType) {
                case MemoryType::Device:
                    cudaFree(m_ptr);
                    break;
                case MemoryType::Pinned:
                    cudaFreeHost(m_ptr);
                    break;
                case MemoryType::Managed:
                    cudaFree(m_ptr);
                    break;
            }
        }

        m_ptr = newPtr;
        m_capacity = newCapacity;
    }

    T* m_ptr;
    size_t m_size;
    size_t m_capacity;
    MemoryType m_memoryType;
};

} // namespace CUDA
} // namespace CatEngine
