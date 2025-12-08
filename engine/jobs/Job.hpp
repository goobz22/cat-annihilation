#pragma once

#include <atomic>
#include <functional>
#include <memory>

namespace CatEngine {

/**
 * Job priority levels for task scheduling
 */
enum class JobPriority : uint8_t {
    LOW = 0,
    NORMAL = 1,
    HIGH = 2
};

/**
 * Job structure representing a unit of work
 * Uses atomic counter for dependency tracking
 */
struct Job {
    using JobFunction = std::function<void()>;

    JobFunction function;                    // Work to be executed
    std::atomic<uint32_t>* counter;         // Optional completion counter for dependencies
    JobPriority priority;                    // Execution priority

    Job()
        : function(nullptr)
        , counter(nullptr)
        , priority(JobPriority::NORMAL)
    {}

    Job(JobFunction func, std::atomic<uint32_t>* cnt = nullptr, JobPriority prio = JobPriority::NORMAL)
        : function(std::move(func))
        , counter(cnt)
        , priority(prio)
    {}

    /**
     * Execute the job and decrement counter if present
     */
    void Execute() {
        if (function) {
            function();
        }

        if (counter) {
            counter->fetch_sub(1, std::memory_order_release);
        }
    }

    bool IsValid() const {
        return function != nullptr;
    }
};

} // namespace CatEngine
