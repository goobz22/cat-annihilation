#pragma once

#include "CudaError.hpp"
#include <cuda_runtime.h>
#include <memory>

namespace CatEngine {
namespace CUDA {

/**
 * @brief RAII wrapper for CUDA streams
 */
class CudaStream {
public:
    /**
     * @brief Flags for stream creation
     */
    enum class Flags : unsigned int {
        Default = cudaStreamDefault,
        NonBlocking = cudaStreamNonBlocking
    };

    /**
     * @brief Priority levels for streams
     */
    enum class Priority {
        High,
        Normal,
        Low
    };

    /**
     * @brief Create a default stream (uses NULL stream)
     */
    CudaStream();

    /**
     * @brief Create a custom stream with flags
     */
    explicit CudaStream(Flags flags);

    /**
     * @brief Create a custom stream with priority
     */
    explicit CudaStream(Priority priority, Flags flags = Flags::Default);

    /**
     * @brief Destructor - destroys stream if not default
     */
    ~CudaStream();

    // Non-copyable, movable
    CudaStream(const CudaStream&) = delete;
    CudaStream& operator=(const CudaStream&) = delete;
    CudaStream(CudaStream&& other) noexcept;
    CudaStream& operator=(CudaStream&& other) noexcept;

    /**
     * @brief Get the underlying CUDA stream
     */
    cudaStream_t get() const { return m_stream; }

    /**
     * @brief Get the underlying CUDA stream (implicit conversion)
     */
    operator cudaStream_t() const { return m_stream; }

    /**
     * @brief Synchronize (wait for all operations in stream to complete)
     */
    void synchronize() const;

    /**
     * @brief Query if stream has completed all operations
     * @return true if all operations completed, false otherwise
     */
    bool query() const;

    /**
     * @brief Check if this is the default stream
     */
    bool isDefault() const { return m_isDefault; }

    /**
     * @brief Wait for an event on this stream
     */
    void waitEvent(cudaEvent_t event) const;

    /**
     * @brief Get stream priority
     */
    int getPriority() const;

    /**
     * @brief Get stream flags
     */
    unsigned int getFlags() const;

    /**
     * @brief Static: Get the default stream
     */
    static CudaStream getDefaultStream();

    /**
     * @brief Static: Get valid priority range
     */
    static void getPriorityRange(int& leastPriority, int& greatestPriority);

private:
    void destroy();

    cudaStream_t m_stream;
    bool m_isDefault;
    bool m_ownsStream;
};

} // namespace CUDA
} // namespace CatEngine
