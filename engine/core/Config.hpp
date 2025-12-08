#ifndef ENGINE_CORE_CONFIG_HPP
#define ENGINE_CORE_CONFIG_HPP

#include <string>
#include <unordered_map>
#include <vector>
#include <variant>
#include <memory>
#include <functional>
#include <optional>
#include <filesystem>
#include <chrono>

namespace Engine {
namespace Core {

/**
 * @brief Represents a JSON value that can be of various types
 *
 * Supports null, boolean, integer, floating-point, string, array, and object types.
 */
class JsonValue {
public:
    enum class Type {
        Null,
        Bool,
        Int,
        Double,
        String,
        Array,
        Object
    };

    using Object = std::unordered_map<std::string, JsonValue>;
    using Array = std::vector<JsonValue>;

    // Constructors
    JsonValue();
    JsonValue(bool value);
    JsonValue(int value);
    JsonValue(long long value);
    JsonValue(double value);
    JsonValue(const char* value);
    JsonValue(const std::string& value);
    JsonValue(const Array& value);
    JsonValue(const Object& value);

    // Type checking
    Type getType() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isBool() const { return type_ == Type::Bool; }
    bool isInt() const { return type_ == Type::Int; }
    bool isDouble() const { return type_ == Type::Double; }
    bool isNumber() const { return isInt() || isDouble(); }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    // Getters with type conversion
    bool asBool() const;
    int asInt() const;
    long long asLongLong() const;
    double asDouble() const;
    float asFloat() const;
    std::string asString() const;
    const Array& asArray() const;
    const Object& asObject() const;

    // Mutable access for arrays and objects
    Array& asArrayMutable();
    Object& asObjectMutable();

    // Array/Object access
    const JsonValue& operator[](size_t index) const;
    const JsonValue& operator[](const std::string& key) const;
    JsonValue& operator[](const std::string& key);

    // Check if object has key
    bool hasKey(const std::string& key) const;

private:
    Type type_;
    std::variant<
        std::monostate,  // Null
        bool,
        long long,
        double,
        std::string,
        std::shared_ptr<Array>,
        std::shared_ptr<Object>
    > value_;
};

/**
 * @brief Configuration management system with JSON support
 *
 * Features:
 * - JSON-based configuration loading
 * - Type-safe getters with dot notation support
 * - Default values for missing keys
 * - Hot-reload file watching
 * - Write-back capability for settings menus
 */
class Config {
public:
    using ReloadCallback = std::function<void(const Config&)>;

    Config();
    ~Config();

    /**
     * @brief Load configuration from a JSON file
     * @param filepath Path to the JSON configuration file
     * @return true if loading succeeded, false otherwise
     */
    bool loadFromFile(const std::string& filepath);

    /**
     * @brief Save current configuration to a JSON file
     * @param filepath Path to save the configuration (defaults to loaded file)
     * @return true if saving succeeded, false otherwise
     */
    bool saveToFile(const std::string& filepath = "");

    /**
     * @brief Get a configuration value with dot notation
     * @tparam T The type to retrieve (int, float, double, bool, string)
     * @param key The configuration key (supports dot notation like "graphics.shadows.resolution")
     * @return The value if found and convertible to T
     */
    template<typename T>
    std::optional<T> get(const std::string& key) const;

    /**
     * @brief Get a configuration value with default fallback
     * @tparam T The type to retrieve
     * @param key The configuration key (supports dot notation)
     * @param defaultValue The default value if key is not found
     * @return The value if found, otherwise defaultValue
     */
    template<typename T>
    T get(const std::string& key, const T& defaultValue) const;

    /**
     * @brief Set a configuration value with dot notation
     * @tparam T The type to set
     * @param key The configuration key (supports dot notation)
     * @param value The value to set
     */
    template<typename T>
    void set(const std::string& key, const T& value);

    /**
     * @brief Check if a key exists in the configuration
     * @param key The configuration key (supports dot notation)
     * @return true if the key exists
     */
    bool has(const std::string& key) const;

    /**
     * @brief Enable hot-reload file watching
     * @param callback Function to call when file changes are detected
     */
    void enableHotReload(ReloadCallback callback);

    /**
     * @brief Disable hot-reload file watching
     */
    void disableHotReload();

    /**
     * @brief Check for file modifications and reload if changed
     * Call this regularly (e.g., each frame) when hot-reload is enabled
     */
    void checkForReload();

    /**
     * @brief Get the root JSON value for direct access
     * @return The root JsonValue object
     */
    const JsonValue& getRoot() const { return root_; }

    /**
     * @brief Get array values from configuration
     * @param key The configuration key
     * @return Array of JsonValues if found
     */
    std::optional<std::vector<JsonValue>> getArray(const std::string& key) const;

private:
    // Parse JSON from string
    JsonValue parseJson(const std::string& json);

    // JSON parsing helpers
    JsonValue parseValue(const std::string& json, size_t& pos);
    JsonValue parseObject(const std::string& json, size_t& pos);
    JsonValue parseArray(const std::string& json, size_t& pos);
    JsonValue parseString(const std::string& json, size_t& pos);
    JsonValue parseNumber(const std::string& json, size_t& pos);
    JsonValue parseBool(const std::string& json, size_t& pos);
    JsonValue parseNull(const std::string& json, size_t& pos);

    // Skip whitespace in JSON
    void skipWhitespace(const std::string& json, size_t& pos);

    // Serialize JSON to string
    std::string serializeJson(const JsonValue& value, int indent = 0) const;

    // Navigate to a value using dot notation
    const JsonValue* navigateToValue(const std::string& key) const;
    JsonValue* navigateToValueMutable(const std::string& key);

    // Create nested structure for a key path
    void createNestedStructure(const std::string& key, const JsonValue& value);

    JsonValue root_;
    std::string filepath_;

    // Hot-reload support
    bool hotReloadEnabled_;
    ReloadCallback reloadCallback_;
    std::filesystem::file_time_type lastModificationTime_;
};

// Template implementations

template<typename T>
std::optional<T> Config::get(const std::string& key) const {
    const JsonValue* value = navigateToValue(key);
    if (!value) {
        return std::nullopt;
    }

    if constexpr (std::is_same_v<T, bool>) {
        return value->asBool();
    } else if constexpr (std::is_same_v<T, int>) {
        return value->asInt();
    } else if constexpr (std::is_same_v<T, long long>) {
        return value->asLongLong();
    } else if constexpr (std::is_same_v<T, float>) {
        return value->asFloat();
    } else if constexpr (std::is_same_v<T, double>) {
        return value->asDouble();
    } else if constexpr (std::is_same_v<T, std::string>) {
        return value->asString();
    } else {
        static_assert(sizeof(T) == 0, "Unsupported type for Config::get");
        return std::nullopt;
    }
}

template<typename T>
T Config::get(const std::string& key, const T& defaultValue) const {
    auto value = get<T>(key);
    return value.value_or(defaultValue);
}

template<typename T>
void Config::set(const std::string& key, const T& value) {
    JsonValue jsonValue;

    if constexpr (std::is_same_v<T, bool>) {
        jsonValue = JsonValue(value);
    } else if constexpr (std::is_same_v<T, int>) {
        jsonValue = JsonValue(value);
    } else if constexpr (std::is_same_v<T, long long>) {
        jsonValue = JsonValue(value);
    } else if constexpr (std::is_same_v<T, float>) {
        jsonValue = JsonValue(static_cast<double>(value));
    } else if constexpr (std::is_same_v<T, double>) {
        jsonValue = JsonValue(value);
    } else if constexpr (std::is_same_v<T, std::string>) {
        jsonValue = JsonValue(value);
    } else if constexpr (std::is_convertible_v<T, std::string>) {
        // Handle const char* and string literals (const char[N])
        jsonValue = JsonValue(std::string(value));
    } else {
        static_assert(sizeof(T) == 0, "Unsupported type for Config::set");
    }

    createNestedStructure(key, jsonValue);
}

} // namespace Core
} // namespace Engine

#endif // ENGINE_CORE_CONFIG_HPP
