# Cat Engine CUDA Module

High-performance CUDA context and memory management system for the Cat Annihilation game engine.

## Overview

This module provides C++20 RAII wrappers around the CUDA Runtime API, offering:

- **CudaContext**: Device initialization, selection, and management
- **CudaStream**: Asynchronous execution stream management
- **CudaBuffer<T>**: Type-safe GPU memory allocation and transfer
- **CudaError**: Comprehensive error handling with exceptions

## Features

### CudaContext
- Automatic device selection (default device 0)
- Device selection by UUID (for Vulkan/CUDA interop)
- Multi-GPU support
- Device capability queries
- Memory information tracking

### CudaStream
- Default and custom stream creation
- Stream priorities (High/Normal/Low)
- Non-blocking streams
- Async operation support
- Stream synchronization and queries

### CudaBuffer<T>
- Template-based type safety
- Three memory types:
  - **Device**: GPU-only memory (fastest for kernels)
  - **Pinned**: Page-locked host memory (fast transfers)
  - **Managed**: Unified memory (CPU+GPU accessible)
- Synchronous and asynchronous transfers
- Automatic memory management (RAII)
- Resize and reserve capabilities
- Zero-copy operations for managed memory

### Error Handling
- Exception-based error handling
- CUDA_CHECK macro for all CUDA calls
- Detailed error messages with file/line information
- Error code to string conversion

## Requirements

- CUDA Toolkit 11.0 or later
- CMake 3.18 or later
- C++20 compatible compiler
- NVIDIA GPU with compute capability 5.0+

## Building

```bash
cd engine/cuda
mkdir build && cd build
cmake ..
make
```

Or integrate into parent CMake project:

```cmake
add_subdirectory(engine/cuda)
target_link_libraries(your_target PRIVATE CatEngineCUDA)
```

## Usage Examples

### Basic Context Initialization

```cpp
#include "cuda/CudaContext.hpp"

using namespace CatEngine::CUDA;

// Initialize with default device
CudaContext context;

// Or select specific device
CudaContext context(1);

// Or match Vulkan device by UUID
cudaUUID uuid = getVulkanDeviceUUID();
CudaContext context(uuid, true); // Falls back to device 0 if not found
```

### Device Queries

```cpp
// Check if CUDA is available
if (!CudaContext::isAvailable()) {
    return;
}

// Get device count
int count = CudaContext::getDeviceCount();

// Get all devices
auto devices = CudaContext::getAllDevices();
for (const auto& dev : devices) {
    std::cout << dev.name << " - "
              << dev.getComputeCapability() << std::endl;
}

// Get current device properties
const auto& props = context.getProperties();
std::cout << "Memory: " << props.totalGlobalMem / (1024*1024) << " MB\n";
std::cout << "SMs: " << props.multiProcessorCount << std::endl;
```

### Memory Management

```cpp
#include "cuda/CudaBuffer.hpp"

// Create device buffer
CudaBuffer<float> deviceBuffer(1000);

// Create and initialize from host data
std::vector<float> hostData(1000, 3.14f);
CudaBuffer<float> buffer(hostData.data(), hostData.size());

// Copy operations
buffer.copyFromHost(hostData.data(), hostData.size());
buffer.copyToHost(hostData.data(), hostData.size());

// Resize
buffer.resize(2000);  // Reallocates if needed
buffer.reserve(5000); // Pre-allocate capacity

// Fill and zero
buffer.zero();        // Set all to zero
buffer.fill(42.0f);   // Fill with value (for managed/pinned)
```

### Managed Memory (Zero-Copy)

```cpp
// Create managed buffer (accessible from CPU and GPU)
CudaBuffer<int> managed(1000, MemoryType::Managed);

// Direct access from host
int* data = managed.data();
for (int i = 0; i < 1000; ++i) {
    data[i] = i * 2;
}

// Use in CUDA kernel (pointer automatically accessible)
myKernel<<<blocks, threads>>>(managed.get());
context.synchronize();

// Read results directly
std::cout << "Result: " << data[0] << std::endl;
```

### Async Streams

```cpp
#include "cuda/CudaStream.hpp"

// Create custom streams
CudaStream stream1(CudaStream::Flags::NonBlocking);
CudaStream stream2(CudaStream::Priority::High);

// Async transfers
CudaBuffer<float> pinned(1000, MemoryType::Pinned);
CudaBuffer<float> device(1000, MemoryType::Device);

device.copyFromHostAsync(pinned.data(), 1000, stream1);

// Launch kernel on stream
myKernel<<<blocks, threads, 0, stream1>>>(device.get());

// Check completion
if (stream1.query()) {
    std::cout << "Stream completed!" << std::endl;
}

// Wait for completion
stream1.synchronize();
```

### Error Handling

```cpp
try {
    CudaContext context;
    CudaBuffer<float> buffer(1000000000); // May fail

    // CUDA_CHECK automatically throws on error
    CUDA_CHECK(cudaMalloc(&ptr, size));

} catch (const CudaException& e) {
    std::cerr << "CUDA Error: " << e.what() << std::endl;
    std::cerr << "Error code: " << e.getError() << std::endl;
    std::cerr << "Location: " << e.getFile() << ":" << e.getLine() << std::endl;
}
```

### Memory Types Comparison

| Type | Location | Access | Best For |
|------|----------|--------|----------|
| Device | GPU only | GPU kernels only | High-performance compute |
| Pinned | CPU RAM | CPU direct access | Fast CPU↔GPU transfers |
| Managed | Unified | CPU + GPU | Prototyping, simple access |

### Complete Example

See `example.cpp` for a comprehensive demonstration including:
- Device enumeration
- Basic memory operations
- Managed memory usage
- Async stream operations
- Memory info queries
- Buffer resize operations

Build and run:
```bash
# Add to CMakeLists.txt:
add_executable(cuda_example example.cpp)
target_link_libraries(cuda_example PRIVATE CatEngineCUDA)

# Run
./cuda_example
```

## Integration with Vulkan

To match CUDA device with Vulkan physical device:

```cpp
// Get UUID from Vulkan device
VkPhysicalDeviceIDProperties idProps = {};
// ... query Vulkan device properties ...

// Convert to CUDA UUID
cudaUUID cudaUuid;
std::memcpy(&cudaUuid, idProps.deviceUUID, sizeof(cudaUUID));

// Create CUDA context for same device
CudaContext context(cudaUuid);

// Now CUDA and Vulkan use the same GPU
```

## Performance Tips

1. **Use Pinned Memory for Transfers**: Much faster than pageable memory
2. **Async Operations**: Overlap transfers with computation using streams
3. **Managed Memory**: Great for prototyping, but may be slower than explicit transfers
4. **Stream Priorities**: Use high priority for latency-sensitive operations
5. **Batch Operations**: Group small allocations to reduce overhead

## Thread Safety

- **CudaContext**: Thread-safe for queries, not for modification
- **CudaStream**: Each thread should have its own stream for async operations
- **CudaBuffer**: Not thread-safe; use external synchronization

## API Reference

### CudaContext Methods
- `CudaContext()` - Initialize with device 0
- `CudaContext(int deviceId)` - Initialize with specific device
- `CudaContext(cudaUUID, bool fallback)` - Initialize by UUID
- `getDeviceId()` - Get current device ID
- `getProperties()` - Get device properties
- `synchronize()` - Wait for all operations
- `getMemoryInfo(free, total)` - Query memory usage
- `reset()` - Reset device
- Static: `getDeviceCount()`, `getAllDevices()`, `findDeviceByUUID()`

### CudaStream Methods
- `CudaStream()` - Default stream
- `CudaStream(Flags)` - Custom stream with flags
- `CudaStream(Priority, Flags)` - Stream with priority
- `get()` - Get cudaStream_t handle
- `synchronize()` - Wait for stream
- `query()` - Check if complete
- `waitEvent(event)` - Wait for CUDA event

### CudaBuffer<T> Methods
- `CudaBuffer(size, type)` - Allocate buffer
- `CudaBuffer(hostData, size, type)` - Allocate and initialize
- `get()` - Get device pointer
- `size()`, `capacity()` - Get element counts
- `resize(newSize)` - Change size
- `reserve(capacity)` - Pre-allocate
- `copyFromHost(data, count)` - Sync copy to device
- `copyToHost(data, count)` - Sync copy from device
- `copyFromHostAsync(data, count, stream)` - Async copy to device
- `copyToHostAsync(data, count, stream)` - Async copy from device
- `copyFromDevice(data, count)` - Device-to-device copy
- `zero()`, `zeroAsync(stream)` - Clear memory
- `clear()` - Set size to 0
- `free()` - Deallocate

## License

Part of the Cat Annihilation game engine.
