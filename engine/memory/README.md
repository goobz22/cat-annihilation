# CatEngine Memory Allocators

High-performance custom memory allocators for the Cat Annihilation game engine.

## Overview

This module provides three specialized allocators optimized for different use cases:

### 1. PoolAllocator
**Best for:** Frequently allocated/deallocated objects of the same size

- **Performance:** O(1) allocation and deallocation
- **Fragmentation:** None
- **Use cases:** Particles, bullets, enemies, audio sources
- **Features:** Freelist implementation, thread-safe option

```cpp
// Allocate pool for 1000 particles, each 64 bytes
PoolAllocator particlePool(64, 1000, true); // thread-safe

// Allocate a particle
Particle* p = static_cast<Particle*>(particlePool.allocate(sizeof(Particle)));

// Return to pool when done
particlePool.deallocate(p);

// Reset entire pool
particlePool.reset();
```

### 2. StackAllocator
**Best for:** Per-frame or per-level allocations with scoped lifetimes

- **Performance:** O(1) allocation, no individual deallocation
- **Fragmentation:** None
- **Use cases:** Level loading, scene hierarchies, temporary buffers
- **Features:** Marker-based rollback, proper alignment

```cpp
// Allocate 1MB stack
StackAllocator levelStack(1024 * 1024);

// Save marker before loading level
auto marker = levelStack.getMarker();

// Load level data
void* levelData = levelStack.allocate(500000);
void* meshData = levelStack.allocate(300000);

// Unload level by rolling back
levelStack.rollbackToMarker(marker);
```

### 3. LinearAllocator
**Best for:** Frame-temporary data that resets each frame

- **Performance:** Extremely fast O(1) allocation (just pointer bump)
- **Fragmentation:** None
- **Use cases:** Per-frame buffers, string formatting, render commands
- **Features:** Peak usage tracking, thread-safe option

```cpp
// Allocate 512KB frame allocator
LinearAllocator frameAlloc(512 * 1024);

void renderFrame() {
    // Allocate temporary buffers
    void* cmdBuffer = frameAlloc.allocate(16384);
    void* tempString = frameAlloc.allocate(256);

    // ... use buffers ...

    // Reset at end of frame
    frameAlloc.reset();

    // Check peak usage for optimization
    size_t peak = frameAlloc.getPeakUsage();
}
```

## Base Allocator Interface

All allocators implement the `Allocator` interface:

```cpp
class Allocator {
    virtual void* allocate(size_t size, size_t alignment) = 0;
    virtual void deallocate(void* ptr) = 0;
    virtual void reset() = 0;

    size_t getTotalSize() const;
    size_t getUsedSize() const;
    size_t getAllocationCount() const;
    bool canAllocate(size_t size) const;
};
```

## Thread Safety

All allocators support optional thread-safety:

```cpp
// Thread-safe pool allocator
PoolAllocator pool(64, 1000, true);

// Thread-safe stack allocator
StackAllocator stack(1024 * 1024, true);

// Thread-safe linear allocator
LinearAllocator linear(512 * 1024, true);
```

## Memory Statistics

Track memory usage across all allocators:

```cpp
void printAllocatorStats(const Allocator& alloc) {
    std::cout << "Total: " << alloc.getTotalSize() << " bytes\n";
    std::cout << "Used: " << alloc.getUsedSize() << " bytes\n";
    std::cout << "Allocations: " << alloc.getAllocationCount() << "\n";
}
```

## Best Practices

1. **Choose the right allocator:**
   - Pool → Same-size objects with unpredictable lifetime
   - Stack → Hierarchical/scoped data with predictable order
   - Linear → Temporary per-frame data

2. **Alignment matters:**
   - Always specify alignment for SIMD types (16-byte, 32-byte)
   - Default alignment is `alignof(std::max_align_t)`

3. **Thread safety:**
   - Only enable if truly needed (adds mutex overhead)
   - Consider per-thread allocators for better performance

4. **Memory sizing:**
   - Profile your game to determine optimal sizes
   - Use peak usage tracking to avoid over-allocation

## C++20 Features Used

- `[[nodiscard]]` attributes
- `std::aligned_alloc` for proper alignment
- Move semantics for allocator ownership
- `std::unique_ptr` for RAII mutex management
- `noexcept` specifications
- Concepts-ready interface design

## Compilation

```bash
g++ -std=c++20 -c PoolAllocator.cpp StackAllocator.cpp LinearAllocator.cpp
```

## Integration Example

```cpp
#include "memory/PoolAllocator.hpp"
#include "memory/StackAllocator.hpp"
#include "memory/LinearAllocator.hpp"

class GameEngine {
    // Pool for frequently spawned entities
    PoolAllocator enemyPool{sizeof(Enemy), 500, true};
    PoolAllocator bulletPool{sizeof(Bullet), 2000, true};

    // Stack for level data
    StackAllocator levelStack{16 * 1024 * 1024}; // 16MB

    // Linear for per-frame data
    LinearAllocator frameAlloc{1 * 1024 * 1024}; // 1MB

public:
    void update() {
        // Spawn enemy from pool
        Enemy* enemy = new(enemyPool.allocate(sizeof(Enemy))) Enemy();

        // Use frame allocator for temporary data
        void* tempData = frameAlloc.allocate(4096);

        // ... game logic ...

        // Reset frame allocator each frame
        frameAlloc.reset();
    }
};
```
