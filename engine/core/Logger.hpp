#ifndef CAT_ENGINE_LOGGER_HPP
#define CAT_ENGINE_LOGGER_HPP

#include <string>
#include <string_view>
#include <format>
#include <memory>
#include <vector>
#include <mutex>
#include <chrono>
#include <fstream>
#include <array>
#include <source_location>

namespace CatEngine {

/**
 * @brief Log severity levels
 */
enum class LogLevel : uint8_t {
    TRACE = 0,
    DEBUG = 1,
    INFO = 2,
    WARN = 3,
    ERROR = 4,
    FATAL = 5
};

/**
 * @brief Compile-time minimum log level
 * Can be overridden by defining CAT_LOG_LEVEL before including this header
 */
#ifndef CAT_LOG_LEVEL
    #ifdef NDEBUG
        #define CAT_LOG_LEVEL 2  // INFO and above in release
    #else
        #define CAT_LOG_LEVEL 0  // TRACE and above in debug
    #endif
#endif

/**
 * @brief Structure representing a single log entry
 */
struct LogEntry {
    LogLevel level;
    std::chrono::system_clock::time_point timestamp;
    std::string message;
    std::string file;
    int line;
};

/**
 * @brief Abstract base class for log output sinks
 */
class LogSink {
public:
    virtual ~LogSink() = default;
    virtual void Write(const LogEntry& entry) = 0;
    virtual void Flush() = 0;
};

/**
 * @brief Console sink with ANSI color support
 */
class ConsoleSink : public LogSink {
public:
    ConsoleSink(bool use_colors = true);
    void Write(const LogEntry& entry) override;
    void Flush() override;

private:
    bool use_colors_;
    std::string GetColorCode(LogLevel level) const;
    std::string GetLevelString(LogLevel level) const;
};

/**
 * @brief File sink for persistent logging
 */
class FileSink : public LogSink {
public:
    explicit FileSink(const std::string& filepath);
    ~FileSink() override;
    void Write(const LogEntry& entry) override;
    void Flush() override;

private:
    std::ofstream file_;
    std::string GetLevelString(LogLevel level) const;
};

/**
 * @brief Ring buffer sink for in-memory recent logs
 */
class RingBufferSink : public LogSink {
public:
    explicit RingBufferSink(size_t max_entries = 1000);
    void Write(const LogEntry& entry) override;
    void Flush() override;
    std::vector<LogEntry> GetEntries() const;
    void Clear();

private:
    std::vector<LogEntry> buffer_;
    size_t max_entries_;
    size_t write_index_;
    bool wrapped_;
    mutable std::mutex buffer_mutex_;
};

/**
 * @brief Main logger class (singleton)
 */
class Logger {
public:
    static Logger& GetInstance();

    // Delete copy/move constructors
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    Logger(Logger&&) = delete;
    Logger& operator=(Logger&&) = delete;

    /**
     * @brief Add a sink to the logger
     */
    void AddSink(std::shared_ptr<LogSink> sink);

    /**
     * @brief Remove all sinks
     */
    void ClearSinks();

    /**
     * @brief Set minimum log level at runtime
     */
    void SetLevel(LogLevel level);

    /**
     * @brief Get current minimum log level
     */
    LogLevel GetLevel() const { return min_level_; }

    /**
     * @brief Log a message (internal implementation)
     */
    void Log(LogLevel level, std::string_view message,
             const char* file, int line);

    /**
     * @brief Template log method with formatting
     */
    template<typename... Args>
    void Log(LogLevel level, const char* file, int line,
             std::format_string<Args...> fmt, Args&&... args) {
        if (level < min_level_) {
            return;
        }

        std::string message = std::format(fmt, std::forward<Args>(args)...);
        Log(level, message, file, line);
    }

    /**
     * @brief Flush all sinks
     */
    void Flush();

private:
    Logger();
    ~Logger();

    std::vector<std::shared_ptr<LogSink>> sinks_;
    std::mutex mutex_;
    LogLevel min_level_;
};

} // namespace CatEngine

// Logging macros with compile-time filtering
#if CAT_LOG_LEVEL <= 0
    #define LOG_TRACE(fmt, ...) \
        ::CatEngine::Logger::GetInstance().Log( \
            ::CatEngine::LogLevel::TRACE, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#else
    #define LOG_TRACE(fmt, ...) ((void)0)
#endif

#if CAT_LOG_LEVEL <= 1
    #define LOG_DEBUG(fmt, ...) \
        ::CatEngine::Logger::GetInstance().Log( \
            ::CatEngine::LogLevel::DEBUG, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#else
    #define LOG_DEBUG(fmt, ...) ((void)0)
#endif

#if CAT_LOG_LEVEL <= 2
    #define LOG_INFO(fmt, ...) \
        ::CatEngine::Logger::GetInstance().Log( \
            ::CatEngine::LogLevel::INFO, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#else
    #define LOG_INFO(fmt, ...) ((void)0)
#endif

#if CAT_LOG_LEVEL <= 3
    #define LOG_WARN(fmt, ...) \
        ::CatEngine::Logger::GetInstance().Log( \
            ::CatEngine::LogLevel::WARN, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#else
    #define LOG_WARN(fmt, ...) ((void)0)
#endif

#if CAT_LOG_LEVEL <= 4
    #define LOG_ERROR(fmt, ...) \
        ::CatEngine::Logger::GetInstance().Log( \
            ::CatEngine::LogLevel::ERROR, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#else
    #define LOG_ERROR(fmt, ...) ((void)0)
#endif

#if CAT_LOG_LEVEL <= 5
    #define LOG_FATAL(fmt, ...) \
        ::CatEngine::Logger::GetInstance().Log( \
            ::CatEngine::LogLevel::FATAL, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#else
    #define LOG_FATAL(fmt, ...) ((void)0)
#endif

#endif // CAT_ENGINE_LOGGER_HPP
