#pragma once

#include "RHITypes.hpp"
#include <cstdint>

namespace CatEngine::RHI {

/**
 * Abstract interface for GPU buffers
 * Represents vertex buffers, index buffers, uniform buffers, storage buffers, etc.
 */
class IRHIBuffer {
public:
    virtual ~IRHIBuffer() = default;

    /**
     * Get the size of the buffer in bytes
     */
    virtual uint64_t GetSize() const = 0;

    /**
     * Get the buffer usage flags
     */
    virtual BufferUsage GetUsage() const = 0;

    /**
     * Get the memory properties
     */
    virtual MemoryProperty GetMemoryProperties() const = 0;

    /**
     * Map buffer memory for CPU access
     * @param offset Offset in bytes from the start of the buffer
     * @param size Size in bytes to map (0 = entire buffer)
     * @return Pointer to mapped memory, nullptr on failure
     */
    virtual void* Map(uint64_t offset = 0, uint64_t size = 0) = 0;

    /**
     * Unmap previously mapped buffer memory
     */
    virtual void Unmap() = 0;

    /**
     * Flush mapped memory range to make CPU writes visible to GPU
     * @param offset Offset in bytes from the start of the buffer
     * @param size Size in bytes to flush (0 = entire buffer)
     */
    virtual void Flush(uint64_t offset = 0, uint64_t size = 0) = 0;

    /**
     * Invalidate mapped memory range to make GPU writes visible to CPU
     * @param offset Offset in bytes from the start of the buffer
     * @param size Size in bytes to invalidate (0 = entire buffer)
     */
    virtual void Invalidate(uint64_t offset = 0, uint64_t size = 0) = 0;

    /**
     * Update buffer data (convenience method for map/copy/unmap)
     * @param data Source data pointer
     * @param size Size in bytes to copy
     * @param offset Destination offset in buffer
     */
    virtual void UpdateData(const void* data, uint64_t size, uint64_t offset = 0) = 0;

    /**
     * Get GPU device address for this buffer (if supported)
     * @return Device address or 0 if not supported
     */
    virtual uint64_t GetDeviceAddress() const = 0;

    /**
     * Get debug name
     */
    virtual const char* GetDebugName() const = 0;
};

} // namespace CatEngine::RHI
