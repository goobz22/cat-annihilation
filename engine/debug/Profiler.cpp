#include "Profiler.hpp"
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <sstream>

namespace CatEngine {

using Clock = std::chrono::high_resolution_clock;

// ProfilerContext Implementation

void ProfilerContext::PushScope(const std::string& name) {
    ScopeEntry entry;
    entry.name = name;
    entry.startTime = Clock::now();
    entry.depth = static_cast<uint32_t>(scopeStack.size());

    scopeStack.push_back(entry);
}

void ProfilerContext::PopScope() {
    if (scopeStack.empty()) {
        return;
    }

    ScopeEntry entry = scopeStack.back();
    scopeStack.pop_back();

    // Calculate duration
    auto endTime = Clock::now();
    std::chrono::duration<double, std::milli> duration = endTime - entry.startTime;
    double durationMs = duration.count();

    // Update statistics
    auto& stat = stats[entry.name];
    stat.name = entry.name;
    stat.depth = entry.depth;
    stat.callCount++;
    stat.totalTime += durationMs;
    stat.minTime = std::min(stat.minTime, durationMs);
    stat.maxTime = std::max(stat.maxTime, durationMs);
    stat.averageTime = stat.totalTime / stat.callCount;
}

void ProfilerContext::Reset() {
    scopeStack.clear();
    stats.clear();
}

// Profiler Implementation

Profiler::Profiler()
    : m_Enabled(true)
    , m_FrameNumber(0)
{
}

Profiler& Profiler::Get() {
    static Profiler instance;
    return instance;
}

ProfilerContext& Profiler::GetThreadContext() {
    std::thread::id threadId = std::this_thread::get_id();

    std::lock_guard<std::mutex> lock(m_ContextMutex);

    auto it = m_ThreadContexts.find(threadId);
    if (it == m_ThreadContexts.end()) {
        auto context = std::make_unique<ProfilerContext>();
        auto* contextPtr = context.get();
        m_ThreadContexts[threadId] = std::move(context);
        return *contextPtr;
    }

    return *it->second;
}

void Profiler::BeginFrame() {
    if (!m_Enabled) return;

    m_FrameStartTime = Clock::now();
}

void Profiler::EndFrame() {
    if (!m_Enabled) return;

    auto frameEndTime = Clock::now();
    std::chrono::duration<double, std::milli> frameDuration = frameEndTime - m_FrameStartTime;

    // Merge statistics from all threads
    MergeThreadStats();

    // Record frame timing
    FrameTiming frameTiming;
    frameTiming.frameNumber = m_FrameNumber;
    frameTiming.frameTime = frameDuration.count();

    {
        std::lock_guard<std::mutex> lock(m_StatsMutex);
        for (const auto& [name, stats] : m_MergedStats) {
            frameTiming.scopeTimes[name] = stats.totalTime;
        }
    }

    // Add to history (maintain max size)
    m_FrameHistory.push_back(frameTiming);
    if (m_FrameHistory.size() > MAX_FRAME_HISTORY) {
        m_FrameHistory.erase(m_FrameHistory.begin());
    }

    ++m_FrameNumber;
}

void Profiler::PushScope(const std::string& name) {
    if (!m_Enabled) return;

    ProfilerContext& context = GetThreadContext();
    context.PushScope(name);
}

void Profiler::PopScope() {
    if (!m_Enabled) return;

    ProfilerContext& context = GetThreadContext();
    context.PopScope();
}

void Profiler::MergeThreadStats() {
    std::lock_guard<std::mutex> contextLock(m_ContextMutex);
    std::lock_guard<std::mutex> statsLock(m_StatsMutex);

    // Clear previous merged stats
    m_MergedStats.clear();

    // Merge stats from all thread contexts
    for (const auto& [threadId, context] : m_ThreadContexts) {
        for (const auto& [name, stats] : context->stats) {
            auto& mergedStat = m_MergedStats[name];

            if (mergedStat.callCount == 0) {
                // First entry for this scope
                mergedStat = stats;
            } else {
                // Merge with existing stats
                mergedStat.callCount += stats.callCount;
                mergedStat.totalTime += stats.totalTime;
                mergedStat.minTime = std::min(mergedStat.minTime, stats.minTime);
                mergedStat.maxTime = std::max(mergedStat.maxTime, stats.maxTime);
                mergedStat.averageTime = mergedStat.totalTime / mergedStat.callCount;
                mergedStat.depth = std::min(mergedStat.depth, stats.depth);
            }
        }
    }
}

const ProfileStats* Profiler::GetScopeStats(const std::string& name) const {
    std::lock_guard<std::mutex> lock(m_StatsMutex);

    auto it = m_MergedStats.find(name);
    if (it != m_MergedStats.end()) {
        return &it->second;
    }

    return nullptr;
}

std::unordered_map<std::string, ProfileStats> Profiler::GetAllStats() const {
    std::lock_guard<std::mutex> lock(m_StatsMutex);
    return m_MergedStats;
}

void Profiler::PrintReport(bool sortByTime) const {
    std::lock_guard<std::mutex> lock(m_StatsMutex);

    if (m_MergedStats.empty()) {
        std::cout << "No profiling data available.\n";
        return;
    }

    // Convert to vector for sorting
    std::vector<ProfileStats> sortedStats;
    sortedStats.reserve(m_MergedStats.size());

    for (const auto& [name, stats] : m_MergedStats) {
        sortedStats.push_back(stats);
    }

    // Sort by total time or name
    if (sortByTime) {
        std::sort(sortedStats.begin(), sortedStats.end(),
            [](const ProfileStats& a, const ProfileStats& b) {
                return a.totalTime > b.totalTime;
            });
    } else {
        std::sort(sortedStats.begin(), sortedStats.end(),
            [](const ProfileStats& a, const ProfileStats& b) {
                return a.name < b.name;
            });
    }

    // Print header
    std::cout << "\n";
    std::cout << "==================== PROFILER REPORT ====================\n";
    std::cout << "Frame: " << m_FrameNumber << "\n";
    std::cout << "=========================================================\n";
    std::cout << std::left << std::setw(30) << "Scope"
              << std::right << std::setw(8) << "Calls"
              << std::setw(12) << "Total (ms)"
              << std::setw(12) << "Avg (ms)"
              << std::setw(12) << "Min (ms)"
              << std::setw(12) << "Max (ms)"
              << "\n";
    std::cout << "---------------------------------------------------------\n";

    // Print stats
    for (const auto& stats : sortedStats) {
        // Add indentation based on depth
        std::string indentedName = std::string(stats.depth * 2, ' ') + stats.name;

        std::cout << std::left << std::setw(30) << indentedName
                  << std::right << std::setw(8) << stats.callCount
                  << std::setw(12) << std::fixed << std::setprecision(3) << stats.totalTime
                  << std::setw(12) << std::fixed << std::setprecision(3) << stats.averageTime
                  << std::setw(12) << std::fixed << std::setprecision(3) << stats.minTime
                  << std::setw(12) << std::fixed << std::setprecision(3) << stats.maxTime
                  << "\n";
    }

    std::cout << "=========================================================\n\n";
}

void Profiler::Reset() {
    std::lock_guard<std::mutex> contextLock(m_ContextMutex);
    std::lock_guard<std::mutex> statsLock(m_StatsMutex);

    // Reset all thread contexts
    for (auto& [threadId, context] : m_ThreadContexts) {
        context->Reset();
    }

    m_MergedStats.clear();
    m_FrameHistory.clear();
    m_FrameNumber = 0;
    m_GPUTimestamps.clear();
    m_ActiveGPUQueries.clear();
}

// ============================================================================
// GPU Profiling — Vulkan VkQueryPool-backed timestamps + CPU-side fallback
// ============================================================================
//
// The GPU path works by recording vkCmdWriteTimestamp commands into the
// application's normal command buffer. Each named query reserves two slots
// in a shared VkQueryPool — one TOP_OF_PIPE slot at Begin and one
// BOTTOM_OF_PIPE slot at End — and the pair's delta becomes the query's GPU
// duration. ResolveGPUQueries then reads all slots back synchronously using
// VK_QUERY_RESULT_WAIT, multiplies by the device's timestampPeriod, and
// stores the results as milliseconds for uniform reporting alongside CPU
// timings.

bool Profiler::InitializeGPU(VkDevice device,
                             float timestampPeriodNanoseconds,
                             uint32_t maxQueries) {
    if (device == VK_NULL_HANDLE || maxQueries == 0) {
        return false;
    }

    // Each named query consumes two timestamp slots (start + end), so the
    // pool must be sized to 2x the logical query count.
    const uint32_t poolSize = maxQueries * 2u;

    VkQueryPoolCreateInfo poolInfo{};
    poolInfo.sType      = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    poolInfo.queryType  = VK_QUERY_TYPE_TIMESTAMP;
    poolInfo.queryCount = poolSize;

    VkQueryPool pool = VK_NULL_HANDLE;
    if (vkCreateQueryPool(device, &poolInfo, nullptr, &pool) != VK_SUCCESS) {
        std::cerr << "[Profiler] vkCreateQueryPool failed; GPU timing disabled" << std::endl;
        return false;
    }

    m_GPUQueryDevice     = device;
    m_GPUQueryPool       = pool;
    m_GPUQueryPoolSize   = poolSize;
    m_GPUQueryNextSlot   = 0;
    m_GPUTimestampPeriod = timestampPeriodNanoseconds;

    m_GPUQuerySlots.clear();
    return true;
}

void Profiler::ShutdownGPU() {
    if (m_GPUQueryPool != VK_NULL_HANDLE && m_GPUQueryDevice != VK_NULL_HANDLE) {
        vkDestroyQueryPool(m_GPUQueryDevice, m_GPUQueryPool, nullptr);
    }
    m_GPUQueryPool       = VK_NULL_HANDLE;
    m_GPUQueryDevice     = VK_NULL_HANDLE;
    m_GPUQueryPoolSize   = 0;
    m_GPUQueryNextSlot   = 0;
    m_GPUTimestampPeriod = 1.0f;
    m_GPUQuerySlots.clear();
    m_ActiveGPUQueries.clear();
}

void Profiler::BeginFrameGPU(VkCommandBuffer cmd) {
    if (!m_Enabled || m_GPUQueryPool == VK_NULL_HANDLE || cmd == VK_NULL_HANDLE) {
        return;
    }

    // Resetting the entire pool in one call is cheap and guarantees every
    // slot returns a defined result after ResolveGPUQueries regardless of
    // how many queries the frame ends up emitting.
    vkCmdResetQueryPool(cmd, m_GPUQueryPool, 0, m_GPUQueryPoolSize);

    m_GPUQueryNextSlot = 0;
    m_GPUQuerySlots.clear();
}

void Profiler::BeginGPUQuery(const std::string& name, VkCommandBuffer cmd) {
    if (!m_Enabled || m_GPUQueryPool == VK_NULL_HANDLE || cmd == VK_NULL_HANDLE) {
        // No initialized pool or no command buffer — downgrade to the CPU
        // path so callers don't silently lose the query.
        BeginGPUQuery(name);
        return;
    }

    // A query needs two free slots (start + end). If the pool is exhausted,
    // drop the query rather than scribbling past the pool end, which would
    // corrupt previously-recorded queries.
    if (m_GPUQueryNextSlot + 2u > m_GPUQueryPoolSize) {
        std::cerr << "[Profiler] GPU query pool exhausted, dropping '"
                  << name << "'" << std::endl;
        return;
    }

    GPUQuerySlotPair slots;
    slots.startSlot = m_GPUQueryNextSlot++;
    slots.endSlot   = m_GPUQueryNextSlot++;
    m_GPUQuerySlots[name] = slots;

    // TOP_OF_PIPE lands the timestamp at the earliest point the device can
    // measure the command boundary, giving a tight lower bound on the
    // pass's actual GPU start time.
    vkCmdWriteTimestamp(cmd,
                        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                        m_GPUQueryPool,
                        slots.startSlot);
}

void Profiler::EndGPUQuery(const std::string& name, VkCommandBuffer cmd) {
    if (!m_Enabled || m_GPUQueryPool == VK_NULL_HANDLE || cmd == VK_NULL_HANDLE) {
        EndGPUQuery(name);
        return;
    }

    auto it = m_GPUQuerySlots.find(name);
    if (it == m_GPUQuerySlots.end()) {
        // No matching BeginGPUQuery — most likely a mismatched or
        // double-ended query. Ignoring is safer than asserting; the
        // profiler is a debug tool and should never take the engine down.
        return;
    }

    // BOTTOM_OF_PIPE pairs with the TOP_OF_PIPE at Begin: the delta between
    // the two gives the total time the pass spent occupying the device.
    vkCmdWriteTimestamp(cmd,
                        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                        m_GPUQueryPool,
                        it->second.endSlot);
}

void Profiler::ResolveGPUQueries() {
    if (!m_Enabled || m_GPUQueryPool == VK_NULL_HANDLE || m_GPUQuerySlots.empty()) {
        return;
    }

    // Read every slot that has been written this frame in one round-trip.
    // VK_QUERY_RESULT_WAIT_BIT makes the call block until the results are
    // ready, trading a potential stall for guaranteed-valid data. The
    // profiler should only be enabled when the user explicitly asked for
    // profiling, so the stall is acceptable.
    const uint32_t usedSlots = m_GPUQueryNextSlot;
    if (usedSlots == 0) return;

    std::vector<uint64_t> timestamps(usedSlots);
    VkResult result = vkGetQueryPoolResults(
        m_GPUQueryDevice,
        m_GPUQueryPool,
        0,                                               // firstQuery
        usedSlots,                                       // queryCount
        sizeof(uint64_t) * timestamps.size(),            // dataSize
        timestamps.data(),
        sizeof(uint64_t),                                // stride
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT
    );

    if (result != VK_SUCCESS) {
        std::cerr << "[Profiler] vkGetQueryPoolResults failed (" << result
                  << "); dropping this frame's GPU timings" << std::endl;
        return;
    }

    // Replace rather than append: the current frame's results fully
    // supersede anything resolved previously, and keeping old entries
    // around would grow the report output unboundedly over time.
    m_GPUTimestamps.clear();
    m_GPUTimestamps.reserve(m_GPUQuerySlots.size());

    for (const auto& [name, slots] : m_GPUQuerySlots) {
        const uint64_t startTicks = timestamps[slots.startSlot];
        const uint64_t endTicks   = timestamps[slots.endSlot];
        const uint64_t deltaTicks = (endTicks > startTicks)
                                        ? (endTicks - startTicks)
                                        : 0ull;

        // timestampPeriod is nanoseconds-per-tick, so deltaTicks *
        // timestampPeriod / 1e6 converts to milliseconds — the unit every
        // other report path in the Profiler uses.
        const double durationMs =
            (static_cast<double>(deltaTicks) *
             static_cast<double>(m_GPUTimestampPeriod)) / 1'000'000.0;

        GPUTimestamp ts;
        ts.name            = name;
        ts.startTimestamp  = startTicks;
        ts.endTimestamp    = endTicks;
        ts.duration        = durationMs;
        m_GPUTimestamps.push_back(std::move(ts));
    }
}

// ----------------------------------------------------------------------------
// CPU-side fallback path
//
// These overloads are used when no Vulkan command buffer is in scope — most
// notably during engine startup before the Renderer is wired up, and from
// tool or editor code that doesn't live inside the main render loop. They
// measure CPU wall-clock only, which is not what a caller asking for "GPU
// timings" strictly wants; they exist so the Begin/End pair can never silently
// drop measurements just because Vulkan state isn't ready yet.
// ----------------------------------------------------------------------------

void Profiler::BeginGPUQuery(const std::string& name) {
    if (!m_Enabled) return;

    auto now = Clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
        now.time_since_epoch()
    ).count();

    m_ActiveGPUQueries[name] = static_cast<uint64_t>(timestamp);
}

void Profiler::EndGPUQuery(const std::string& name) {
    if (!m_Enabled) return;

    auto it = m_ActiveGPUQueries.find(name);
    if (it == m_ActiveGPUQueries.end()) {
        return;
    }

    auto now = Clock::now();
    auto endTimestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
        now.time_since_epoch()
    ).count();

    GPUTimestamp timestamp;
    timestamp.name = name;
    timestamp.startTimestamp = it->second;
    timestamp.endTimestamp = static_cast<uint64_t>(endTimestamp);
    // CPU path reports durations already in nanoseconds, so the /1e6 here
    // matches the GPU path's output scale (milliseconds).
    timestamp.duration =
        static_cast<double>(endTimestamp - static_cast<int64_t>(it->second)) / 1'000'000.0;

    m_GPUTimestamps.push_back(timestamp);
    m_ActiveGPUQueries.erase(it);
}

void Profiler::PrintGPUReport() const {
    if (m_GPUTimestamps.empty()) {
        std::cout << "No GPU profiling data available.\n";
        return;
    }

    std::cout << "\n";
    std::cout << "==================== GPU PROFILER REPORT ====================\n";
    std::cout << std::left << std::setw(40) << "Query Name"
              << std::right << std::setw(15) << "Duration (ms)"
              << "\n";
    std::cout << "-------------------------------------------------------------\n";

    for (const auto& timestamp : m_GPUTimestamps) {
        std::cout << std::left << std::setw(40) << timestamp.name
                  << std::right << std::setw(15) << std::fixed << std::setprecision(3)
                  << timestamp.duration
                  << "\n";
    }

    std::cout << "=============================================================\n\n";
}

} // namespace CatEngine
