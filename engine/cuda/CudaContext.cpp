#include "CudaContext.hpp"
#include <sstream>
#include <iomanip>
#include <cstring>

namespace CatEngine {
namespace CUDA {

// DeviceProperties implementation
std::string DeviceProperties::getComputeCapability() const {
    return std::to_string(computeCapabilityMajor) + "." + std::to_string(computeCapabilityMinor);
}

std::string DeviceProperties::getUUIDString() const {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (int i = 0; i < 16; ++i) {
        oss << std::setw(2) << static_cast<int>(static_cast<unsigned char>(uuid.bytes[i]));
        if (i == 3 || i == 5 || i == 7 || i == 9) {
            oss << "-";
        }
    }
    return oss.str();
}

// CudaContext implementation
CudaContext::CudaContext()
    : m_deviceId(0)
    , m_initialized(false)
{
    initialize(0);
}

CudaContext::CudaContext(int deviceId)
    : m_deviceId(deviceId)
    , m_initialized(false)
{
    initialize(deviceId);
}

CudaContext::CudaContext(const cudaUUID_t& uuid, bool fallbackToDefault)
    : m_deviceId(0)
    , m_initialized(false)
{
    auto deviceId = findDeviceByUUID(uuid);
    if (deviceId.has_value()) {
        initialize(deviceId.value());
    } else if (fallbackToDefault) {
        initialize(0);
    } else {
        throw CudaException(cudaErrorInvalidDevice, __FILE__, __LINE__);
    }
}

CudaContext::~CudaContext() {
    if (m_initialized) {
        // Don't throw in destructor
        cudaDeviceReset();
    }
}

CudaContext::CudaContext(CudaContext&& other) noexcept
    : m_deviceId(other.m_deviceId)
    , m_properties(std::move(other.m_properties))
    , m_initialized(other.m_initialized)
{
    other.m_initialized = false;
}

CudaContext& CudaContext::operator=(CudaContext&& other) noexcept {
    if (this != &other) {
        if (m_initialized) {
            cudaDeviceReset();
        }
        m_deviceId = other.m_deviceId;
        m_properties = std::move(other.m_properties);
        m_initialized = other.m_initialized;
        other.m_initialized = false;
    }
    return *this;
}

void CudaContext::initialize(int deviceId) {
    int deviceCount = getDeviceCount();

    // No CUDA devices available
    if (deviceCount == 0) {
        throw CudaException(cudaErrorNoDevice, __FILE__, __LINE__);
    }

    // Invalid device ID
    if (deviceId < 0 || deviceId >= deviceCount) {
        throw CudaException(cudaErrorInvalidDevice, __FILE__, __LINE__);
    }

    m_deviceId = deviceId;
    CUDA_CHECK(cudaSetDevice(m_deviceId));

    loadProperties();
    m_initialized = true;
}

void CudaContext::loadProperties() {
    cudaDeviceProp prop;
    CUDA_CHECK(cudaGetDeviceProperties(&prop, m_deviceId));

    m_properties.name = prop.name;
    m_properties.deviceId = m_deviceId;
    m_properties.uuid = prop.uuid;
    m_properties.totalGlobalMem = prop.totalGlobalMem;
    m_properties.sharedMemPerBlock = prop.sharedMemPerBlock;
    m_properties.multiProcessorCount = prop.multiProcessorCount;
    m_properties.maxThreadsPerBlock = prop.maxThreadsPerBlock;
    m_properties.maxThreadsDim[0] = prop.maxThreadsDim[0];
    m_properties.maxThreadsDim[1] = prop.maxThreadsDim[1];
    m_properties.maxThreadsDim[2] = prop.maxThreadsDim[2];
    m_properties.maxGridSize[0] = prop.maxGridSize[0];
    m_properties.maxGridSize[1] = prop.maxGridSize[1];
    m_properties.maxGridSize[2] = prop.maxGridSize[2];
    m_properties.warpSize = prop.warpSize;
    m_properties.computeCapabilityMajor = prop.major;
    m_properties.computeCapabilityMinor = prop.minor;
    m_properties.clockRate = prop.clockRate;
    m_properties.memoryClockRate = prop.memoryClockRate;
    m_properties.memoryBusWidth = prop.memoryBusWidth;
    m_properties.canMapHostMemory = prop.canMapHostMemory;
    m_properties.unifiedAddressing = prop.unifiedAddressing;
    m_properties.concurrentKernels = prop.concurrentKernels;
    m_properties.asyncEngineCount = prop.asyncEngineCount;
    m_properties.pciDeviceId = prop.pciDeviceID;
    m_properties.pciDomainId = prop.pciDomainID;
    m_properties.pciBusId = prop.pciBusID;
}

void CudaContext::synchronize() const {
    CUDA_CHECK(cudaDeviceSynchronize());
}

void CudaContext::getMemoryInfo(size_t& free, size_t& total) const {
    CUDA_CHECK(cudaMemGetInfo(&free, &total));
}

void CudaContext::reset() const {
    CUDA_CHECK(cudaDeviceReset());
}

int CudaContext::getDeviceCount() {
    int count = 0;
    cudaError_t error = cudaGetDeviceCount(&count);

    // Handle various CUDA initialization errors gracefully
    if (error == cudaErrorNoDevice ||
        error == cudaErrorInsufficientDriver ||
        error == cudaErrorInvalidValue ||
        error == cudaErrorInitializationError ||
        error == cudaErrorUnknown) {
        // Reset CUDA error state and return 0 devices
        cudaGetLastError();
        return 0;
    }

    // For other errors, check and potentially throw
    if (error != cudaSuccess) {
        CUDA_CHECK(error);
    }

    return count;
}

DeviceProperties CudaContext::getDeviceProperties(int deviceId) {
    cudaDeviceProp prop;
    CUDA_CHECK(cudaGetDeviceProperties(&prop, deviceId));

    DeviceProperties properties;
    properties.name = prop.name;
    properties.deviceId = deviceId;
    properties.uuid = prop.uuid;
    properties.totalGlobalMem = prop.totalGlobalMem;
    properties.sharedMemPerBlock = prop.sharedMemPerBlock;
    properties.multiProcessorCount = prop.multiProcessorCount;
    properties.maxThreadsPerBlock = prop.maxThreadsPerBlock;
    properties.maxThreadsDim[0] = prop.maxThreadsDim[0];
    properties.maxThreadsDim[1] = prop.maxThreadsDim[1];
    properties.maxThreadsDim[2] = prop.maxThreadsDim[2];
    properties.maxGridSize[0] = prop.maxGridSize[0];
    properties.maxGridSize[1] = prop.maxGridSize[1];
    properties.maxGridSize[2] = prop.maxGridSize[2];
    properties.warpSize = prop.warpSize;
    properties.computeCapabilityMajor = prop.major;
    properties.computeCapabilityMinor = prop.minor;
    properties.clockRate = prop.clockRate;
    properties.memoryClockRate = prop.memoryClockRate;
    properties.memoryBusWidth = prop.memoryBusWidth;
    properties.canMapHostMemory = prop.canMapHostMemory;
    properties.unifiedAddressing = prop.unifiedAddressing;
    properties.concurrentKernels = prop.concurrentKernels;
    properties.asyncEngineCount = prop.asyncEngineCount;
    properties.pciDeviceId = prop.pciDeviceID;
    properties.pciDomainId = prop.pciDomainID;
    properties.pciBusId = prop.pciBusID;

    return properties;
}

std::vector<DeviceProperties> CudaContext::getAllDevices() {
    std::vector<DeviceProperties> devices;
    int count = getDeviceCount();
    devices.reserve(count);

    for (int i = 0; i < count; ++i) {
        devices.push_back(getDeviceProperties(i));
    }

    return devices;
}

std::optional<int> CudaContext::findDeviceByUUID(const cudaUUID_t& uuid) {
    int count = getDeviceCount();

    for (int i = 0; i < count; ++i) {
        cudaDeviceProp prop;
        cudaError_t error = cudaGetDeviceProperties(&prop, i);
        if (error != cudaSuccess) {
            continue;
        }

        if (std::memcmp(&prop.uuid, &uuid, sizeof(cudaUUID_t)) == 0) {
            return i;
        }
    }

    return std::nullopt;
}

bool CudaContext::isAvailable() {
    return getDeviceCount() > 0;
}

} // namespace CUDA
} // namespace CatEngine
