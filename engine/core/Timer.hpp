#pragma once

#include <chrono>
#include <cstdint>

namespace CatEngine {

/**
 * @brief High-resolution timer for game loop timing and performance measurement
 *
 * Provides delta time, elapsed time, FPS calculation, and fixed timestep accumulator
 * for physics updates. Uses std::chrono::high_resolution_clock for precise timing.
 */
class Timer {
public:
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = std::chrono::time_point<Clock>;
    using Duration = std::chrono::duration<double>;

    Timer();
    ~Timer() = default;

    /**
     * @brief Start/restart the timer
     */
    void Start();

    /**
     * @brief Update the timer (call once per frame)
     * @return Delta time in seconds since last update
     */
    double Update();

    /**
     * @brief Get delta time between last two updates
     * @return Delta time in seconds
     */
    double GetDeltaTime() const { return m_DeltaTime; }

    /**
     * @brief Get total elapsed time since timer started
     * @return Elapsed time in seconds
     */
    double GetElapsedTime() const;

    /**
     * @brief Get current frames per second (smoothed average)
     * @return FPS value
     */
    double GetFPS() const { return m_FPS; }

    /**
     * @brief Get total frame count since start
     * @return Frame count
     */
    uint64_t GetFrameCount() const { return m_FrameCount; }

    /**
     * @brief Accumulate time for fixed timestep physics updates
     * @param dt Delta time to accumulate
     */
    void AccumulateFixedTime(double dt) { m_FixedTimeAccumulator += dt; }

    /**
     * @brief Check if enough time has accumulated for a fixed timestep
     * @param fixedTimestep The fixed timestep duration
     * @return True if accumulated time >= fixedTimestep
     */
    bool CanStepFixed(double fixedTimestep) const {
        return m_FixedTimeAccumulator >= fixedTimestep;
    }

    /**
     * @brief Consume one fixed timestep from the accumulator
     * @param fixedTimestep The fixed timestep duration
     */
    void ConsumeFixedStep(double fixedTimestep) {
        m_FixedTimeAccumulator -= fixedTimestep;
    }

    /**
     * @brief Get current fixed time accumulator value
     * @return Accumulated time in seconds
     */
    double GetFixedTimeAccumulator() const { return m_FixedTimeAccumulator; }

    /**
     * @brief Reset the fixed time accumulator to zero
     */
    void ResetFixedTimeAccumulator() { m_FixedTimeAccumulator = 0.0; }

private:
    TimePoint m_StartTime;
    TimePoint m_LastFrameTime;

    double m_DeltaTime;
    double m_FixedTimeAccumulator;
    double m_FPS;
    double m_FPSUpdateAccumulator;

    uint64_t m_FrameCount;
    uint64_t m_FPSFrameCount;

    static constexpr double FPS_UPDATE_INTERVAL = 0.5; // Update FPS every 0.5 seconds
};

} // namespace CatEngine
