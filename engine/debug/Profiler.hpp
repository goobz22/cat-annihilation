#pragma once

#include <chrono>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <thread>
#include <cstdint>
#include <vulkan/vulkan.h>

// The Profiler is the engine-wide singleton for both CPU and GPU timing. GPU
// timing is driven by a single Vulkan VkQueryPool allocated at engine
// startup; the pool holds N timestamp slots and is recycled every frame via
// vkCmdResetQueryPool. CPU and GPU timing are independent — the CPU path
// works without any Vulkan setup, the GPU path becomes available once
// InitializeGPU() has been called with a valid VkDevice and the
// timestampPeriod queried from VkPhysicalDeviceLimits.

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
 * @brief GPU timestamp query result
 *
 * One entry per named query resolved during the most recent
 * ResolveGPUQueries() call. Timestamps are in Vulkan tick units when raw;
 * `duration` is converted to milliseconds using the device-reported
 * timestampPeriod (nanoseconds per tick) so callers don't need to know the
 * hardware-specific scale factor.
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
 * - GPU timestamp queries via Vulkan VkQueryPool (see InitializeGPU)
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

    // ========================================================================
    // GPU Profiling Interface (Vulkan VkQueryPool-backed)
    // ========================================================================
    //
    // Lifecycle:
    //   1. InitializeGPU(device, timestampPeriod) — call once after device
    //      creation. Allocates a VkQueryPool sized to 2 * maxQueries slots
    //      (start + end per named query).
    //   2. BeginFrameGPU(cmd) — call once per frame at the top of the
    //      command buffer. Resets the pool so the new frame's queries start
    //      from a clean slate.
    //   3. BeginGPUQuery(name, cmd) / EndGPUQuery(name, cmd) — emit timestamp
    //      queries into the recorded command buffer. The string-only
    //      overloads (no cmd argument) fall back to CPU-side timing, which
    //      is useful when no command buffer is available (engine startup,
    //      tool code) and for callers that just want approximate durations
    //      without plumbing a Vulkan handle everywhere.
    //   4. ResolveGPUQueries() — call once per frame after the command
    //      buffer has been submitted and the fence signaled. Reads query
    //      results back from the pool, converts tick deltas to milliseconds
    //      using the stored timestampPeriod, and populates m_GPUTimestamps.
    //   5. ShutdownGPU() — destroys the pool. Must happen before vkDestroyDevice.

    /**
     * @brief Initialize the GPU profiling subsystem against a Vulkan device.
     * @param device The Vulkan device that owns all command buffers passed to
     *               Begin/End GPUQuery. Must outlive the Profiler's GPU state.
     * @param timestampPeriodNanoseconds Pulled from
     *        VkPhysicalDeviceLimits::timestampPeriod; each timestamp tick
     *        corresponds to this many nanoseconds on the device.
     * @param maxQueries Maximum number of named GPU queries per frame
     *                   (allocates 2 * maxQueries timestamp slots). Default
     *                   covers the ~100 scopes a typical frame touches.
     * @return true on success, false if the pool could not be created.
     */
    bool InitializeGPU(VkDevice device,
                       float timestampPeriodNanoseconds,
                       uint32_t maxQueries = 256);

    /**
     * @brief Destroy the GPU query pool. Safe to call even if InitializeGPU
     *        was never called or already failed — it no-ops in both cases.
     */
    void ShutdownGPU();

    /**
     * @brief Reset the query pool for a new frame. Must be recorded into the
     *        same command buffer that will issue the frame's queries, before
     *        any BeginGPUQuery call. Internally clears the per-frame
     *        slot-allocation map so names emitted in the previous frame can
     *        be reused.
     */
    void BeginFrameGPU(VkCommandBuffer cmd);

    /**
     * @brief Emit a start-timestamp query at the top of the pipeline.
     *        Pairs with EndGPUQuery(name, cmd); the pair's duration becomes
     *        available after ResolveGPUQueries runs.
     */
    void BeginGPUQuery(const std::string& name, VkCommandBuffer cmd);

    /**
     * @brief Emit an end-timestamp query at the bottom of the pipeline.
     */
    void EndGPUQuery(const std::string& name, VkCommandBuffer cmd);

    /**
     * @brief Resolve all queries issued since the last BeginFrameGPU call.
     *        Reads results synchronously — caller must guarantee the
     *        submitting command buffer has completed (fence signaled) before
     *        calling, otherwise vkGetQueryPoolResults either blocks or
     *        returns VK_NOT_READY depending on the flags passed below.
     *        Uses WAIT flag so this always produces valid data at the cost
     *        of a potential stall; acceptable because the Profiler is a
     *        debug-only subsystem.
     */
    void ResolveGPUQueries();

    // CPU-side fallback timing: identical interface, usable when no Vulkan
    // command buffer is in scope. Durations are CPU clock deltas, not GPU
    // ticks, so the two paths should not be mixed within the same named
    // query or callers will get inconsistent results.
    void BeginGPUQuery(const std::string& name);
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

    // ========================================================================
    // GPU profiling state
    // ========================================================================

    // Resolved query results from the most recent ResolveGPUQueries call.
    // Each ResolveGPUQueries reset this vector so it never grows unbounded.
    std::vector<GPUTimestamp> m_GPUTimestamps;

    // Active CPU-fallback queries keyed by name. For GPU-backed queries the
    // slot-pair map below is the source of truth; this map only stores the
    // CPU-timing path's start timestamps so BeginGPUQuery/EndGPUQuery (no
    // cmd buffer) can compute their durations without a Vulkan pool.
    std::unordered_map<std::string, uint64_t> m_ActiveGPUQueries;

    // Vulkan query pool state. m_GPUQueryDevice is non-null iff
    // InitializeGPU succeeded; all GPU-backed BeginGPUQuery/EndGPUQuery calls
    // early-return when it's null, downgrading silently to the CPU path so
    // non-profiling engine builds don't crash from a forgotten init.
    VkDevice    m_GPUQueryDevice      = VK_NULL_HANDLE;
    VkQueryPool m_GPUQueryPool        = VK_NULL_HANDLE;
    uint32_t    m_GPUQueryPoolSize    = 0;     // Total timestamp slots in the pool.
    uint32_t    m_GPUQueryNextSlot    = 0;     // Next free slot for this frame.
    float       m_GPUTimestampPeriod  = 1.0f;  // nanoseconds per tick.

    // Maps each named query to the (start, end) slot pair issued in the
    // current frame. Cleared by BeginFrameGPU so a name can be reused each
    // frame without colliding with the previous frame's slot allocation.
    struct GPUQuerySlotPair {
        uint32_t startSlot;
        uint32_t endSlot;
    };
    std::unordered_map<std::string, GPUQuerySlotPair> m_GPUQuerySlots;
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
