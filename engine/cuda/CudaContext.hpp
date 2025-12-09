#pragma once

#include "CudaError.hpp"
#include <cuda_runtime.h>
#include <vector>
#include <string>
#include <optional>
#include <memory>

namespace CatEngine {
namespace CUDA {

/**
 * @brief Device properties wrapper
 */
struct DeviceProperties {
    std::string name;
    int deviceId;
    cudaUUID_t uuid;
    size_t totalGlobalMem;
    size_t sharedMemPerBlock;
    int multiProcessorCount;
    int maxThreadsPerBlock;
    int maxThreadsDim[3];
    int maxGridSize[3];
    int warpSize;
    int computeCapabilityMajor;
    int computeCapabilityMinor;
    int clockRate;
    size_t memoryClockRate;
    int memoryBusWidth;
    bool canMapHostMemory;
    bool unifiedAddressing;
    bool concurrentKernels;
    bool asyncEngineCount;
    int pciDeviceId;
    int pciDomainId;
    int pciBusId;

    std::string getComputeCapability() const;
    std::string getUUIDString() const;
};

/**
 * @brief CUDA Context - manages CUDA device initialization and selection
 */
class CudaContext {
public:
    /**
     * @brief Construct context with automatic device selection (device 0)
     */
    CudaContext();

    /**
     * @brief Construct context with specific device ID
     */
    explicit CudaContext(int deviceId);

    /**
     * @brief Construct context by matching Vulkan UUID
     * @param uuid The UUID from Vulkan device
     * @param fallbackToDefault If true, falls back to device 0 if UUID not found
     */
    CudaContext(const cudaUUID_t& uuid, bool fallbackToDefault = true);

    /**
     * @brief Destructor - resets CUDA device
     */
    ~CudaContext();

    // Non-copyable, movable
    CudaContext(const CudaContext&) = delete;
    CudaContext& operator=(const CudaContext&) = delete;
    CudaContext(CudaContext&& other) noexcept;
    CudaContext& operator=(CudaContext&& other) noexcept;

    /**
     * @brief Get current device ID
     */
    int getDeviceId() const { return m_deviceId; }

    /**
     * @brief Get device properties
     */
    const DeviceProperties& getProperties() const { return m_properties; }

    /**
     * @brief Synchronize device (wait for all pending operations)
     */
    void synchronize() const;

    /**
     * @brief Get available memory on device
     */
    void getMemoryInfo(size_t& free, size_t& total) const;

    /**
     * @brief Reset the device (clear all allocations)
     */
    void reset() const;

    /**
     * @brief Static: Get number of available CUDA devices
     */
    static int getDeviceCount();

    /**
     * @brief Static: Get properties for specific device
     */
    static DeviceProperties getDeviceProperties(int deviceId);

    /**
     * @brief Static: Get all available devices
     */
    static std::vector<DeviceProperties> getAllDevices();

    /**
     * @brief Static: Find device by UUID
     * @return Device ID if found, std::nullopt otherwise
     */
    static std::optional<int> findDeviceByUUID(const cudaUUID_t& uuid);

    /**
     * @brief Static: Check if CUDA is available
     */
    static bool isAvailable();

private:
    void initialize(int deviceId);
    void loadProperties();

    int m_deviceId;
    DeviceProperties m_properties;
    bool m_initialized;
};

} // namespace CUDA
} // namespace CatEngine
