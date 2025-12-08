# CUDA Module API Overview

## Quick Reference

### Header Files Structure

```
engine/cuda/
├── CudaError.hpp       - Error handling (macros, exceptions)
├── CudaContext.hpp     - Device management
├── CudaContext.cpp
├── CudaStream.hpp      - Stream management
├── CudaStream.cpp
└── CudaBuffer.hpp      - Memory management (header-only template)
```

### Include Order

```cpp
#include "cuda/CudaError.hpp"    // Include first (used by others)
#include "cuda/CudaContext.hpp"  // Device initialization
#include "cuda/CudaStream.hpp"   // Async operations
#include "cuda/CudaBuffer.hpp"   // Memory buffers
```

## Core Classes

### 1. CudaContext - Device Management

**Purpose**: Initialize and manage CUDA devices, query capabilities

**Construction**:
```cpp
CudaContext ctx;                    // Use device 0
CudaContext ctx(deviceId);          // Use specific device
CudaContext ctx(uuid, fallback);    // Match Vulkan UUID
```

**Key Methods**:
```cpp
int getDeviceId()                                    // Current device ID
const DeviceProperties& getProperties()              // Device info
void synchronize()                                   // Wait for device
void getMemoryInfo(size_t& free, size_t& total)     // Memory usage

// Static methods
static int getDeviceCount()
static DeviceProperties getDeviceProperties(int id)
static std::vector<DeviceProperties> getAllDevices()
static std::optional<int> findDeviceByUUID(cudaUUID)
static bool isAvailable()
```

**DeviceProperties Members**:
```cpp
std::string name
int deviceId
cudaUUID uuid
size_t totalGlobalMem
int multiProcessorCount
int computeCapabilityMajor/Minor
int maxThreadsPerBlock
int warpSize
// ... and more

std::string getComputeCapability()
std::string getUUIDString()
```

### 2. CudaStream - Async Execution

**Purpose**: Manage asynchronous CUDA operations

**Construction**:
```cpp
CudaStream stream;                                    // Default stream
CudaStream stream(CudaStream::Flags::NonBlocking);   // Custom stream
CudaStream stream(CudaStream::Priority::High);       // High priority
```

**Flags**:
```cpp
enum class Flags {
    Default,        // cudaStreamDefault
    NonBlocking     // cudaStreamNonBlocking
};
```

**Priorities**:
```cpp
enum class Priority {
    High,    // Highest priority
    Normal,  // Default priority
    Low      // Lowest priority
};
```

**Key Methods**:
```cpp
cudaStream_t get()              // Get underlying stream
void synchronize()              // Wait for stream
bool query()                    // Check if complete
void waitEvent(cudaEvent_t)     // Wait for event
int getPriority()               // Get stream priority
unsigned int getFlags()         // Get stream flags

// Static
static CudaStream getDefaultStream()
static void getPriorityRange(int& least, int& greatest)
```

### 3. CudaBuffer<T> - Memory Management

**Purpose**: Type-safe GPU memory allocation and transfers

**Memory Types**:
```cpp
enum class MemoryType {
    Device,   // GPU-only (cudaMalloc)
    Pinned,   // Page-locked host (cudaMallocHost)
    Managed   // Unified memory (cudaMallocManaged)
};
```

**Construction**:
```cpp
CudaBuffer<T> buf;                           // Empty
CudaBuffer<T> buf(count, memType);          // Allocate
CudaBuffer<T> buf(hostData, count, type);   // Allocate + copy
```

**Accessors**:
```cpp
T* get()                     // Device pointer
size_t size()                // Element count
size_t capacity()            // Allocated elements
size_t sizeBytes()           // Size in bytes
size_t capacityBytes()       // Capacity in bytes
MemoryType memoryType()      // Memory type
bool empty()                 // Is empty?
```

**Size Management**:
```cpp
void resize(size_t)          // Change size (may reallocate)
void reserve(size_t)         // Pre-allocate capacity
void clear()                 // Set size to 0
void free()                  // Deallocate all
```

**Synchronous Transfers**:
```cpp
void copyFromHost(const T* hostData, size_t count)
void copyToHost(T* hostData, size_t count)
void copyFromDevice(const T* deviceData, size_t count)
void copyFrom(const CudaBuffer<T>& other)
```

**Asynchronous Transfers**:
```cpp
void copyFromHostAsync(const T* hostData, size_t count, CudaStream& stream)
void copyToHostAsync(T* hostData, size_t count, CudaStream& stream)
void copyFromDeviceAsync(const T* deviceData, size_t count, CudaStream& stream)
void copyFromAsync(const CudaBuffer<T>& other, CudaStream& stream)
```

**Utilities**:
```cpp
void fill(const T& value)              // Fill with value (managed/pinned only)
void zero()                            // Memset to 0 (sync)
void zeroAsync(CudaStream& stream)     // Memset to 0 (async)
T* data()                              // Direct access (managed/pinned only)
```

### 4. Error Handling

**Exception Class**:
```cpp
class CudaException : public std::runtime_error {
    cudaError_t getError() const;
    const char* getFile() const;
    int getLine() const;
};
```

**Macros**:
```cpp
CUDA_CHECK(cudaCall);           // Check and throw on error
CUDA_CHECK_LAST_ERROR();        // Check last error
```

**Functions**:
```cpp
const char* cudaErrorToString(cudaError_t)
const char* cudaErrorName(cudaError_t)
```

## Usage Patterns

### Pattern 1: Simple Computation

```cpp
CudaContext ctx;
CudaBuffer<float> input(1000);
CudaBuffer<float> output(1000);

// Initialize
std::vector<float> data(1000, 1.0f);
input.copyFromHost(data.data(), 1000);

// Compute
myKernel<<<blocks, threads>>>(input.get(), output.get());
ctx.synchronize();

// Retrieve results
output.copyToHost(data.data(), 1000);
```

### Pattern 2: Async Pipeline

```cpp
CudaContext ctx;
CudaStream stream(CudaStream::Flags::NonBlocking);

CudaBuffer<float> pinned(1000, MemoryType::Pinned);
CudaBuffer<float> device(1000, MemoryType::Device);

// Async transfer
device.copyFromHostAsync(pinned.data(), 1000, stream);

// Async kernel
myKernel<<<blocks, threads, 0, stream>>>(device.get());

// Continue work on CPU while GPU runs...

// Wait when needed
stream.synchronize();
```

### Pattern 3: Zero-Copy with Managed Memory

```cpp
CudaContext ctx;
CudaBuffer<int> managed(1000, MemoryType::Managed);

// Direct CPU access
int* data = managed.data();
for (int i = 0; i < 1000; ++i) {
    data[i] = i;
}

// Use in kernel (same pointer works)
myKernel<<<blocks, threads>>>(managed.get());
ctx.synchronize();

// Read results directly
std::cout << data[0] << std::endl;
```

### Pattern 4: Multi-Stream Concurrency

```cpp
CudaContext ctx;
CudaStream streams[4];
CudaBuffer<float> buffers[4];

for (int i = 0; i < 4; ++i) {
    streams[i] = CudaStream(CudaStream::Flags::NonBlocking);
    buffers[i] = CudaBuffer<float>(1000);
}

// Launch work on all streams
for (int i = 0; i < 4; ++i) {
    myKernel<<<blocks, threads, 0, streams[i]>>>(buffers[i].get());
}

// Synchronize all
for (auto& stream : streams) {
    stream.synchronize();
}
```

### Pattern 5: Vulkan Interop

```cpp
// Get Vulkan device UUID
VkPhysicalDeviceIDProperties vkProps = {};
// ... query from Vulkan ...

// Match CUDA device
cudaUUID uuid;
memcpy(&uuid, vkProps.deviceUUID, sizeof(cudaUUID));

CudaContext ctx(uuid);  // Same GPU for both APIs

// Now can share memory between CUDA and Vulkan
// (requires external memory extensions)
```

## Build Integration

### Standalone Build

```bash
cd engine/cuda
mkdir build && cd build
cmake ..
make
./CudaExample  # Run example program
```

### Parent Project Integration

```cmake
# In parent CMakeLists.txt
add_subdirectory(engine/cuda)

# Link to your target
add_executable(MyGame main.cpp)
target_link_libraries(MyGame PRIVATE CatEngineCUDA)
```

### Usage in Code

```cpp
#include "cuda/CudaContext.hpp"
#include "cuda/CudaStream.hpp"
#include "cuda/CudaBuffer.hpp"

using namespace CatEngine::CUDA;
```

## Performance Guidelines

1. **Use appropriate memory type**:
   - Device: Best for pure GPU compute
   - Pinned: Best for frequent CPU↔GPU transfers
   - Managed: Best for prototyping, simplicity

2. **Async operations**: Overlap transfers with compute
3. **Stream priorities**: Critical paths get High priority
4. **Batch allocations**: Reduce allocation overhead
5. **Reuse buffers**: Don't allocate in hot loops

## Thread Safety

- **CudaContext**: Thread-safe for queries
- **CudaStream**: Create per-thread for async work
- **CudaBuffer**: Not thread-safe, use locks

## Requirements

- CUDA Toolkit 11.0+
- CMake 3.20+
- C++20 compiler
- Compute Capability 5.0+ GPU
