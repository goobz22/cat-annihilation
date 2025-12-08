#include "Timer.hpp"

namespace CatEngine {

Timer::Timer()
    : m_DeltaTime(0.0)
    , m_FixedTimeAccumulator(0.0)
    , m_FPS(0.0)
    , m_FPSUpdateAccumulator(0.0)
    , m_FrameCount(0)
    , m_FPSFrameCount(0)
{
    Start();
}

void Timer::Start() {
    m_StartTime = Clock::now();
    m_LastFrameTime = m_StartTime;
    m_DeltaTime = 0.0;
    m_FixedTimeAccumulator = 0.0;
    m_FPS = 0.0;
    m_FPSUpdateAccumulator = 0.0;
    m_FrameCount = 0;
    m_FPSFrameCount = 0;
}

double Timer::Update() {
    TimePoint currentTime = Clock::now();

    // Calculate delta time
    Duration delta = currentTime - m_LastFrameTime;
    m_DeltaTime = delta.count();

    // Update last frame time
    m_LastFrameTime = currentTime;

    // Increment frame count
    ++m_FrameCount;
    ++m_FPSFrameCount;

    // Update FPS calculation
    m_FPSUpdateAccumulator += m_DeltaTime;

    if (m_FPSUpdateAccumulator >= FPS_UPDATE_INTERVAL) {
        // Calculate smoothed FPS over the update interval
        m_FPS = static_cast<double>(m_FPSFrameCount) / m_FPSUpdateAccumulator;

        // Reset accumulator and frame counter
        m_FPSUpdateAccumulator = 0.0;
        m_FPSFrameCount = 0;
    }

    return m_DeltaTime;
}

double Timer::GetElapsedTime() const {
    TimePoint currentTime = Clock::now();
    Duration elapsed = currentTime - m_StartTime;
    return elapsed.count();
}

} // namespace CatEngine
