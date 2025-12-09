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

// GPU Profiling (Placeholder Implementation)

void Profiler::BeginGPUQuery(const std::string& name) {
    if (!m_Enabled) return;

    // Placeholder: In a real implementation, this would submit a Vulkan timestamp query
    // For now, we'll use CPU timing as a placeholder
    auto now = Clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
        now.time_since_epoch()
    ).count();

    m_ActiveGPUQueries[name] = timestamp;
}

void Profiler::EndGPUQuery(const std::string& name) {
    if (!m_Enabled) return;

    auto it = m_ActiveGPUQueries.find(name);
    if (it == m_ActiveGPUQueries.end()) {
        return;
    }

    // Placeholder: In a real implementation, this would read back Vulkan timestamp query results
    auto now = Clock::now();
    auto endTimestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
        now.time_since_epoch()
    ).count();

    GPUTimestamp timestamp;
    timestamp.name = name;
    timestamp.startTimestamp = it->second;
    timestamp.endTimestamp = endTimestamp;
    timestamp.duration = static_cast<double>(endTimestamp - it->second) / 1000000.0; // Convert to ms

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
