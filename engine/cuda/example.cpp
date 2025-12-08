/**
 * @file example.cpp
 * @brief Example usage of the Cat Engine CUDA module
 */

#include "cuda/CudaContext.hpp"
#include "cuda/CudaStream.hpp"
#include "cuda/CudaBuffer.hpp"
#include <iostream>
#include <vector>

using namespace CatEngine::CUDA;

void printDeviceInfo() {
    std::cout << "=== CUDA Device Information ===" << std::endl;

    if (!CudaContext::isAvailable()) {
        std::cout << "No CUDA devices available!" << std::endl;
        return;
    }

    int deviceCount = CudaContext::getDeviceCount();
    std::cout << "Found " << deviceCount << " CUDA device(s)" << std::endl << std::endl;

    auto devices = CudaContext::getAllDevices();
    for (const auto& dev : devices) {
        std::cout << "Device " << dev.deviceId << ": " << dev.name << std::endl;
        std::cout << "  UUID: " << dev.getUUIDString() << std::endl;
        std::cout << "  Compute Capability: " << dev.getComputeCapability() << std::endl;
        std::cout << "  Total Memory: " << (dev.totalGlobalMem / (1024*1024)) << " MB" << std::endl;
        std::cout << "  Multiprocessors: " << dev.multiProcessorCount << std::endl;
        std::cout << "  Max Threads per Block: " << dev.maxThreadsPerBlock << std::endl;
        std::cout << "  Warp Size: " << dev.warpSize << std::endl;
        std::cout << std::endl;
    }
}

void basicMemoryOperations() {
    std::cout << "=== Basic Memory Operations ===" << std::endl;

    // Initialize context
    CudaContext context;

    // Create a buffer of 1000 floats in device memory
    CudaBuffer<float> deviceBuffer(1000, MemoryType::Device);
    std::cout << "Created device buffer with " << deviceBuffer.size() << " elements" << std::endl;

    // Create host data
    std::vector<float> hostData(1000);
    for (size_t i = 0; i < hostData.size(); ++i) {
        hostData[i] = static_cast<float>(i);
    }

    // Copy to device
    deviceBuffer.copyFromHost(hostData.data(), hostData.size());
    std::cout << "Copied " << hostData.size() << " elements to device" << std::endl;

    // Copy back to host
    std::vector<float> result(1000);
    deviceBuffer.copyToHost(result.data(), result.size());
    std::cout << "Copied back to host" << std::endl;

    // Verify
    bool correct = true;
    for (size_t i = 0; i < 10; ++i) {
        if (result[i] != hostData[i]) {
            correct = false;
            break;
        }
    }
    std::cout << "Verification: " << (correct ? "PASSED" : "FAILED") << std::endl;
    std::cout << "First 5 elements: ";
    for (size_t i = 0; i < 5; ++i) {
        std::cout << result[i] << " ";
    }
    std::cout << std::endl << std::endl;
}

void managedMemoryExample() {
    std::cout << "=== Managed Memory Example ===" << std::endl;

    CudaContext context;

    // Create managed memory buffer (accessible from both CPU and GPU)
    CudaBuffer<int> managedBuffer(100, MemoryType::Managed);
    std::cout << "Created managed buffer with " << managedBuffer.size() << " elements" << std::endl;

    // Access directly from host (no explicit copy needed)
    int* data = managedBuffer.data();
    for (size_t i = 0; i < 10; ++i) {
        data[i] = i * 2;
    }
    std::cout << "Wrote directly to managed memory" << std::endl;

    // Synchronize to ensure GPU can see the data
    context.synchronize();

    std::cout << "First 5 elements: ";
    for (size_t i = 0; i < 5; ++i) {
        std::cout << data[i] << " ";
    }
    std::cout << std::endl << std::endl;
}

void asyncStreamExample() {
    std::cout << "=== Async Stream Example ===" << std::endl;

    CudaContext context;

    // Create custom streams
    CudaStream stream1(CudaStream::Flags::NonBlocking);
    CudaStream stream2(CudaStream::Flags::NonBlocking);
    std::cout << "Created 2 non-blocking streams" << std::endl;

    // Create pinned host memory for fast transfers
    CudaBuffer<double> pinnedBuffer(1000, MemoryType::Pinned);
    CudaBuffer<double> deviceBuffer1(1000, MemoryType::Device);
    CudaBuffer<double> deviceBuffer2(1000, MemoryType::Device);

    // Fill pinned buffer
    double* pinnedData = pinnedBuffer.data();
    for (size_t i = 0; i < 1000; ++i) {
        pinnedData[i] = i * 3.14159;
    }
    std::cout << "Filled pinned host buffer" << std::endl;

    // Async copy to device on different streams
    deviceBuffer1.copyFromHostAsync(pinnedData, 1000, stream1);
    deviceBuffer2.copyFromHostAsync(pinnedData, 1000, stream2);
    std::cout << "Initiated async copies on both streams" << std::endl;

    // Wait for streams to complete
    stream1.synchronize();
    stream2.synchronize();
    std::cout << "Streams synchronized" << std::endl;

    // Check if operations completed
    std::cout << "Stream 1 completed: " << (stream1.query() ? "Yes" : "No") << std::endl;
    std::cout << "Stream 2 completed: " << (stream2.query() ? "Yes" : "No") << std::endl;
    std::cout << std::endl;
}

void memoryInfoExample() {
    std::cout << "=== Memory Information ===" << std::endl;

    CudaContext context;

    size_t free, total;
    context.getMemoryInfo(free, total);

    std::cout << "Total GPU Memory: " << (total / (1024*1024)) << " MB" << std::endl;
    std::cout << "Free GPU Memory: " << (free / (1024*1024)) << " MB" << std::endl;
    std::cout << "Used GPU Memory: " << ((total - free) / (1024*1024)) << " MB" << std::endl;
    std::cout << std::endl;
}

void resizeExample() {
    std::cout << "=== Buffer Resize Example ===" << std::endl;

    CudaContext context;

    CudaBuffer<int> buffer(100);
    std::cout << "Initial size: " << buffer.size()
              << ", capacity: " << buffer.capacity() << std::endl;

    buffer.resize(200);
    std::cout << "After resize(200) - size: " << buffer.size()
              << ", capacity: " << buffer.capacity() << std::endl;

    buffer.reserve(500);
    std::cout << "After reserve(500) - size: " << buffer.size()
              << ", capacity: " << buffer.capacity() << std::endl;

    buffer.clear();
    std::cout << "After clear() - size: " << buffer.size()
              << ", capacity: " << buffer.capacity() << std::endl;
    std::cout << std::endl;
}

int main() {
    try {
        printDeviceInfo();
        basicMemoryOperations();
        managedMemoryExample();
        asyncStreamExample();
        memoryInfoExample();
        resizeExample();

        std::cout << "All examples completed successfully!" << std::endl;
        return 0;

    } catch (const CudaException& e) {
        std::cerr << "CUDA Error: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
