# Cat Engine Job System

A high-performance, lock-free job system for the Cat Annihilation game engine, designed for efficient multi-threaded task execution.

## Features

- **Lock-free job queue** using atomic operations and work-stealing algorithm
- **Thread pool** with `hardware_concurrency - 1` workers (reserves one core for main thread)
- **Job dependencies** via atomic counters
- **Job stealing** for automatic load balancing
- **Priority levels** (LOW, NORMAL, HIGH)
- **Parallel-for helper** for easy data parallelism
- **Smart waiting** with spinning then yielding to reduce latency
- **C++20 standards** using `std::jthread` for clean thread management

## Architecture

### Components

1. **Job.hpp** - Job structure with function, counter, and priority
2. **JobQueue.hpp** - Lock-free ring buffer with Chase-Lev deque algorithm
3. **WorkerThread.hpp/cpp** - Worker thread with job stealing capability
4. **JobSystem.hpp/cpp** - Main system managing thread pool and job distribution

### Job Queue Algorithm

The job queue uses a modified Chase-Lev work-stealing deque:
- **Owner thread** pushes/pops from the bottom (LIFO for cache locality)
- **Thief threads** steal from the top (FIFO to minimize contention)
- **Lock-free** using atomic operations and compare-exchange
- **Ring buffer** with 4096 job capacity per worker

## Usage

### Basic Job Submission

```cpp
#include "JobSystem.hpp"

CatEngine::JobSystem jobSystem;

// Submit a simple job
jobSystem.SubmitJob([]() {
    // Do work here
});

// Wait for completion
jobSystem.WaitForAll();
```

### Job Dependencies

```cpp
std::atomic<uint32_t> counter(3);

// Submit multiple jobs with shared counter
for (int i = 0; i < 3; ++i) {
    jobSystem.SubmitJob([i]() {
        ProcessData(i);
    }, &counter);
}

// Wait for all jobs to complete
jobSystem.WaitForCounter(&counter);
```

### Parallel For

```cpp
std::vector<float> data(10000);

// Process array in parallel
jobSystem.ParallelFor(0, 10000, [&data](uint32_t i) {
    data[i] = ComputeValue(i);
});
```

### Job Priorities

```cpp
// High priority job executes first
jobSystem.SubmitJob(
    []() { CriticalTask(); },
    nullptr,
    JobPriority::HIGH
);

// Normal priority (default)
jobSystem.SubmitJob([]() { RegularTask(); });

// Low priority
jobSystem.SubmitJob(
    []() { BackgroundTask(); },
    nullptr,
    JobPriority::LOW
);
```

### Complex Dependencies

```cpp
// Phase 1: Parallel data loading
std::atomic<uint32_t> loadCounter(4);
for (int i = 0; i < 4; ++i) {
    jobSystem.SubmitJob([i]() { LoadChunk(i); }, &loadCounter);
}
jobSystem.WaitForCounter(&loadCounter);

// Phase 2: Process loaded data
std::atomic<uint32_t> processCounter(2);
for (int i = 0; i < 2; ++i) {
    jobSystem.SubmitJob([i]() { ProcessChunk(i); }, &processCounter);
}
jobSystem.WaitForCounter(&processCounter);
```

## Building

### CMake

```bash
cd engine/jobs
mkdir build && cd build
cmake ..
make
```

### Manual Compilation

```bash
g++ -std=c++20 -pthread -O3 \
    JobSystem.cpp WorkerThread.cpp example_usage.cpp \
    -o job_system_example
```

## Performance Characteristics

- **Job submission**: O(1) lock-free
- **Job stealing**: O(1) with minimal contention
- **Memory overhead**: ~128KB per worker (4096 jobs × ~32 bytes)
- **Latency**: Spin-wait for ~100 iterations, then yield
- **Scalability**: Linear up to hardware thread count

## Thread Safety

- All public APIs are thread-safe
- Jobs can submit other jobs safely
- Counters use `std::atomic` with acquire/release semantics
- No deadlocks or race conditions in core system

## Best Practices

1. **Job granularity**: Aim for 100μs - 1ms per job
2. **Batch work**: Use `ParallelFor` for data parallelism
3. **Avoid blocking**: Don't use mutexes in jobs if possible
4. **Counter reuse**: Create counters on stack, don't heap allocate
5. **Priority sparingly**: Most jobs should be NORMAL priority

## Example Use Cases

### Physics Simulation
```cpp
jobSystem.ParallelFor(0, entityCount, [](uint32_t i) {
    UpdatePhysics(entities[i]);
});
```

### Particle Systems
```cpp
std::atomic<uint32_t> particleCounter(particleSystems.size());
for (auto& system : particleSystems) {
    jobSystem.SubmitJob([&system]() {
        system.Update();
    }, &particleCounter, JobPriority::LOW);
}
```

### Rendering Preparation
```cpp
// High priority for rendering
jobSystem.SubmitJob([]() {
    CullObjects();
    BuildRenderList();
}, nullptr, JobPriority::HIGH);
```

## Integration with Game Engine

```cpp
class GameEngine {
    CatEngine::JobSystem m_jobSystem;

public:
    void Update(float deltaTime) {
        std::atomic<uint32_t> updateCounter(3);

        // Update physics, AI, and audio in parallel
        m_jobSystem.SubmitJob([this, deltaTime]() {
            UpdatePhysics(deltaTime);
        }, &updateCounter);

        m_jobSystem.SubmitJob([this, deltaTime]() {
            UpdateAI(deltaTime);
        }, &updateCounter);

        m_jobSystem.SubmitJob([this, deltaTime]() {
            UpdateAudio(deltaTime);
        }, &updateCounter);

        // Wait for all systems
        m_jobSystem.WaitForCounter(&updateCounter);

        // Render on main thread
        Render();
    }
};
```

## Technical Details

### Memory Model

- **Atomic counters**: Use `memory_order_acquire/release` for synchronization
- **Job queue**: Sequential consistency for critical sections
- **Thread fence**: Used for ordering guarantees

### Work Stealing Strategy

1. Worker tries its own queue first (LIFO - cache friendly)
2. If empty, randomly selects another worker
3. Steals from top of victim's queue (FIFO - reduces contention)
4. Tries up to 4 random workers before yielding

### Parallel-For Implementation

- Auto-calculates batch size: `totalWork / (workers × 4)`
- Creates one job per batch
- Uses atomic counter for synchronization
- Waits for all batches to complete

## Limitations

- Maximum 4096 jobs per worker queue (overflow will block)
- Jobs must be copyable (uses `std::function`)
- No priority preemption (priority only affects queue order)
- No job cancellation support

## Future Enhancements

- [ ] Job profiling and statistics
- [ ] Dynamic thread count adjustment
- [ ] NUMA-aware thread pinning
- [ ] Job graph visualization
- [ ] Fiber-based cooperative scheduling

## License

Part of the Cat Annihilation game engine.
