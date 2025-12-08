#include "Logger.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <ctime>

namespace CatEngine {

// ANSI color codes
namespace Colors {
    constexpr const char* RESET = "\033[0m";
    constexpr const char* GRAY = "\033[90m";
    constexpr const char* GREEN = "\033[32m";
    constexpr const char* CYAN = "\033[36m";
    constexpr const char* YELLOW = "\033[33m";
    constexpr const char* RED = "\033[31m";
    constexpr const char* MAGENTA_BOLD = "\033[1;35m";
}

/**
 * @brief Convert timestamp to formatted string
 */
static std::string FormatTimestamp(const std::chrono::system_clock::time_point& tp) {
    auto time_t = std::chrono::system_clock::to_time_t(tp);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        tp.time_since_epoch()) % 1000;

    std::tm tm_buf;
    #ifdef _WIN32
        localtime_s(&tm_buf, &time_t);
    #else
        localtime_r(&time_t, &tm_buf);
    #endif

    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

/**
 * @brief Extract filename from full path
 */
static std::string ExtractFilename(const std::string& path) {
    size_t pos = path.find_last_of("/\\");
    return (pos == std::string::npos) ? path : path.substr(pos + 1);
}

// ============================================================================
// ConsoleSink Implementation
// ============================================================================

ConsoleSink::ConsoleSink(bool use_colors) : use_colors_(use_colors) {}

std::string ConsoleSink::GetColorCode(LogLevel level) const {
    if (!use_colors_) return "";

    switch (level) {
        case LogLevel::TRACE: return Colors::GRAY;
        case LogLevel::DEBUG: return Colors::GREEN;
        case LogLevel::INFO:  return Colors::CYAN;
        case LogLevel::WARN:  return Colors::YELLOW;
        case LogLevel::ERROR: return Colors::RED;
        case LogLevel::FATAL: return Colors::MAGENTA_BOLD;
        default: return Colors::RESET;
    }
}

std::string ConsoleSink::GetLevelString(LogLevel level) const {
    switch (level) {
        case LogLevel::TRACE: return "TRACE";
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO ";
        case LogLevel::WARN:  return "WARN ";
        case LogLevel::ERROR: return "ERROR";
        case LogLevel::FATAL: return "FATAL";
        default: return "UNKNOWN";
    }
}

void ConsoleSink::Write(const LogEntry& entry) {
    std::string timestamp = FormatTimestamp(entry.timestamp);
    std::string filename = ExtractFilename(entry.file);
    std::string color = GetColorCode(entry.level);
    std::string level_str = GetLevelString(entry.level);

    std::ostringstream oss;
    oss << color
        << "[" << level_str << "] "
        << "[" << timestamp << "] "
        << "[" << filename << ":" << entry.line << "] "
        << entry.message;

    if (use_colors_) {
        oss << Colors::RESET;
    }

    // Output to stderr for WARN and above, stdout otherwise
    if (entry.level >= LogLevel::WARN) {
        std::cerr << oss.str() << std::endl;
    } else {
        std::cout << oss.str() << std::endl;
    }
}

void ConsoleSink::Flush() {
    std::cout.flush();
    std::cerr.flush();
}

// ============================================================================
// FileSink Implementation
// ============================================================================

FileSink::FileSink(const std::string& filepath) {
    file_.open(filepath, std::ios::out | std::ios::app);
    if (!file_.is_open()) {
        std::cerr << "Failed to open log file: " << filepath << std::endl;
    }
}

FileSink::~FileSink() {
    if (file_.is_open()) {
        file_.close();
    }
}

std::string FileSink::GetLevelString(LogLevel level) const {
    switch (level) {
        case LogLevel::TRACE: return "TRACE";
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO ";
        case LogLevel::WARN:  return "WARN ";
        case LogLevel::ERROR: return "ERROR";
        case LogLevel::FATAL: return "FATAL";
        default: return "UNKNOWN";
    }
}

void FileSink::Write(const LogEntry& entry) {
    if (!file_.is_open()) return;

    std::string timestamp = FormatTimestamp(entry.timestamp);
    std::string filename = ExtractFilename(entry.file);
    std::string level_str = GetLevelString(entry.level);

    file_ << "[" << level_str << "] "
          << "[" << timestamp << "] "
          << "[" << filename << ":" << entry.line << "] "
          << entry.message << std::endl;
}

void FileSink::Flush() {
    if (file_.is_open()) {
        file_.flush();
    }
}

// ============================================================================
// RingBufferSink Implementation
// ============================================================================

RingBufferSink::RingBufferSink(size_t max_entries)
    : max_entries_(max_entries)
    , write_index_(0)
    , wrapped_(false) {
    buffer_.reserve(max_entries_);
}

void RingBufferSink::Write(const LogEntry& entry) {
    std::lock_guard<std::mutex> lock(buffer_mutex_);

    if (buffer_.size() < max_entries_) {
        buffer_.push_back(entry);
    } else {
        buffer_[write_index_] = entry;
        wrapped_ = true;
    }

    write_index_ = (write_index_ + 1) % max_entries_;
}

void RingBufferSink::Flush() {
    // Ring buffer doesn't need flushing
}

std::vector<LogEntry> RingBufferSink::GetEntries() const {
    std::lock_guard<std::mutex> lock(buffer_mutex_);

    if (!wrapped_) {
        return buffer_;
    }

    // Return entries in chronological order
    std::vector<LogEntry> result;
    result.reserve(buffer_.size());

    for (size_t i = 0; i < buffer_.size(); ++i) {
        size_t idx = (write_index_ + i) % buffer_.size();
        result.push_back(buffer_[idx]);
    }

    return result;
}

void RingBufferSink::Clear() {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    buffer_.clear();
    write_index_ = 0;
    wrapped_ = false;
}

// ============================================================================
// Logger Implementation
// ============================================================================

Logger::Logger() : min_level_(static_cast<LogLevel>(CAT_LOG_LEVEL)) {
    // Initialize with console sink by default
    AddSink(std::make_shared<ConsoleSink>(true));
}

Logger::~Logger() {
    Flush();
}

Logger& Logger::GetInstance() {
    static Logger instance;
    return instance;
}

void Logger::AddSink(std::shared_ptr<LogSink> sink) {
    std::lock_guard<std::mutex> lock(mutex_);
    sinks_.push_back(sink);
}

void Logger::ClearSinks() {
    std::lock_guard<std::mutex> lock(mutex_);
    sinks_.clear();
}

void Logger::SetLevel(LogLevel level) {
    min_level_ = level;
}

void Logger::Log(LogLevel level, std::string_view message,
                 const char* file, int line) {
    if (level < min_level_) {
        return;
    }

    LogEntry entry{
        level,
        std::chrono::system_clock::now(),
        std::string(message),
        file,
        line
    };

    // Lock only during the write operation
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& sink : sinks_) {
        sink->Write(entry);
    }
}

void Logger::Flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& sink : sinks_) {
        sink->Flush();
    }
}

} // namespace CatEngine
