#pragma once

#include <cuda_runtime.h>
#include <stdexcept>
#include <string>
#include <sstream>

namespace CatEngine {
namespace CUDA {

/**
 * @brief Exception class for CUDA errors
 */
class CudaException : public std::runtime_error {
public:
    CudaException(cudaError_t error, const char* file, int line)
        : std::runtime_error(formatError(error, file, line))
        , m_error(error)
        , m_file(file)
        , m_line(line)
    {}

    cudaError_t getError() const { return m_error; }
    const char* getFile() const { return m_file; }
    int getLine() const { return m_line; }

private:
    static std::string formatError(cudaError_t error, const char* file, int line) {
        std::ostringstream oss;
        oss << "CUDA Error at " << file << ":" << line << " - "
            << cudaGetErrorName(error) << ": " << cudaGetErrorString(error);
        return oss.str();
    }

    cudaError_t m_error;
    const char* m_file;
    int m_line;
};

/**
 * @brief Macro to check CUDA errors and throw exception on failure
 *
 * Usage: CUDA_CHECK(cudaMalloc(&ptr, size));
 */
#define CUDA_CHECK(call) \
    do { \
        cudaError_t error = (call); \
        if (error != cudaSuccess) { \
            throw ::CatEngine::CUDA::CudaException(error, __FILE__, __LINE__); \
        } \
    } while(0)

/**
 * @brief Get last CUDA error and check it
 */
#define CUDA_CHECK_LAST_ERROR() \
    CUDA_CHECK(cudaGetLastError())

/**
 * @brief Alias for CUDA_CHECK_LAST_ERROR (backwards compatibility)
 */
#define CUDA_CHECK_LAST() CUDA_CHECK_LAST_ERROR()

/**
 * @brief Convert CUDA error to string
 */
inline const char* cudaErrorToString(cudaError_t error) {
    return cudaGetErrorString(error);
}

/**
 * @brief Get CUDA error name
 */
inline const char* cudaErrorName(cudaError_t error) {
    return cudaGetErrorName(error);
}

} // namespace CUDA
} // namespace CatEngine
