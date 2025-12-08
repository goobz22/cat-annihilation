#pragma once

#include <chrono>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <thread>
#include <cstdint>

namespace CatEngine {

/**
 * @brief Profiling scope statistics
 */
struct ProfileStats {
    std::string name;
    uint32_t depth;
    uint64_t callCount;
    double totalTime;
    double minTime;
    double maxTime;
    double averageTime;

    ProfileStats()
        : depth(0)
        , callCount(0)
        , totalTime(0.0)
        , minTime(std::numeric_limits<double>::max())
        , maxTime(0.0)
        , averageTime(0.0)
    {}
};

/**
 * @brief Frame timing data for history tracking
 */
struct FrameTiming {
    uint64_t frameNumber;
    double frameTime;
    std::unordered_map<std::string, double> scopeTimes;

    FrameTiming() : frameNumber(0), frameTime(0.0) {}
};

/**
 * @brief GPU timestamp query placeholder for Vulkan integration
 */
struct GPUTimestamp {
    std::string name;
    uint64_t startTimestamp;
    uint64_t endTimestamp;
    double duration; // In milliseconds

    GPUTimestamp() : startTimestamp(0), endTimestamp(0), duration(0.0) {}
};

/**
 * @brief Thread-local profiling context
 */
class ProfilerContext {
public:
    struct ScopeEntry {
        std::string name;
        std::chrono::time_point<std::chrono::high_resolution_clock> startTime;
        uint32_t depth;
    };

    std::vector<ScopeEntry> scopeStack;
    std::unordered_map<std::string, ProfileStats> stats;

    void PushScope(const std::string& name);
    void PopScope();
    void Reset();
};

/**
 * @brief High-performance hierarchical profiler with thread-safety
 *
 * Features:
 * - Scoped timing with RAII
 * - Hierarchical profiling (nested scopes tracked as tree)
 * - Frame timing history (last 120 frames)
 * - Per-scope statistics: min, max, average, total time
 * - Thread-safe (each thread has own stack)
 * - Formatted report output
 * - GPU timestamp query interface (placeholder)
 */
class Profiler {
public:
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = std::chrono::time_point<Clock>;

    static Profiler& Get();

    /**
     * @brief Begin a new profiling frame
     */
    void BeginFrame();

    /**
     * @brief End the current profiling frame
     */
    void EndFrame();

    /**
     * @brief Push a new profiling scope onto the stack
     * @param name Name of the scope
     */
    void PushScope(const std::string& name);

    /**
     * @brief Pop the current profiling scope from the stack
     */
    void PopScope();

    /**
     * @brief Get statistics for a specific scope
     * @param name Scope name
     * @return Pointer to stats, or nullptr if not found
     */
    const ProfileStats* GetScopeStats(const std::string& name) const;

    /**
     * @brief Get all collected statistics
     * @return Map of scope names to statistics
     */
    std::unordered_map<std::string, ProfileStats> GetAllStats() const;

    /**
     * @brief Get frame timing history
     * @return Vector of recent frame timings
     */
    const std::vector<FrameTiming>& GetFrameHistory() const { return m_FrameHistory; }

    /**
     * @brief Get current frame number
     * @return Frame count
     */
    uint64_t GetFrameNumber() const { return m_FrameNumber; }

    /**
     * @brief Print formatted profiling report to stdout
     * @param sortByTime If true, sort by total time; otherwise by name
     */
    void PrintReport(bool sortByTime = true) const;

    /**
     * @brief Reset all profiling data
     */
    void Reset();

    /**
     * @brief Enable or disable profiling
     * @param enabled True to enable, false to disable
     */
    void SetEnabled(bool enabled) { m_Enabled = enabled; }

    /**
     * @brief Check if profiling is enabled
     * @return True if enabled
     */
    bool IsEnabled() const { return m_Enabled; }

    // GPU Profiling Interface (placeholder for Vulkan integration)

    /**
     * @brief Begin GPU timestamp query
     * @param name Query name
     */
    void BeginGPUQuery(const std::string& name);

    /**
     * @brief End GPU timestamp query
     * @param name Query name
     */
    void EndGPUQuery(const std::string& name);

    /**
     * @brief Get GPU query results
     * @return Vector of GPU timestamps
     */
    const std::vector<GPUTimestamp>& GetGPUTimestamps() const { return m_GPUTimestamps; }

    /**
     * @brief Print GPU profiling report
     */
    void PrintGPUReport() const;

private:
    Profiler();
    ~Profiler() = default;

    Profiler(const Profiler&) = delete;
    Profiler& operator=(const Profiler&) = delete;

    ProfilerContext& GetThreadContext();
    void MergeThreadStats();

    bool m_Enabled;
    uint64_t m_FrameNumber;
    TimePoint m_FrameStartTime;

    // Thread-local contexts
    mutable std::mutex m_ContextMutex;
    std::unordered_map<std::thread::id, std::unique_ptr<ProfilerContext>> m_ThreadContexts;

    // Frame history (last 120 frames)
    static constexpr size_t MAX_FRAME_HISTORY = 120;
    std::vector<FrameTiming> m_FrameHistory;

    // Merged statistics across all threads
    mutable std::mutex m_StatsMutex;
    std::unordered_map<std::string, ProfileStats> m_MergedStats;

    // GPU profiling (placeholder)
    std::vector<GPUTimestamp> m_GPUTimestamps;
    std::unordered_map<std::string, uint64_t> m_ActiveGPUQueries;
};

/**
 * @brief RAII scope guard for profiling
 */
class ProfileScope {
public:
    explicit ProfileScope(const std::string& name) : m_Name(name) {
        if (Profiler::Get().IsEnabled()) {
            Profiler::Get().PushScope(m_Name);
        }
    }

    ~ProfileScope() {
        if (Profiler::Get().IsEnabled()) {
            Profiler::Get().PopScope();
        }
    }

    ProfileScope(const ProfileScope&) = delete;
    ProfileScope& operator=(const ProfileScope&) = delete;

private:
    std::string m_Name;
};

} // namespace CatEngine

// Convenience macros for profiling
#define PROFILE_SCOPE(name) CatEngine::ProfileScope _profileScope##__LINE__(name)
#define PROFILE_FUNCTION() PROFILE_SCOPE(__FUNCTION__)
