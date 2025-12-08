#include "Config.hpp"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <cctype>
#include <cmath>

namespace Engine {
namespace Core {

// ============================================================================
// JsonValue Implementation
// ============================================================================

JsonValue::JsonValue() : type_(Type::Null), value_(std::monostate{}) {}

JsonValue::JsonValue(bool value) : type_(Type::Bool), value_(value) {}

JsonValue::JsonValue(int value) : type_(Type::Int), value_(static_cast<long long>(value)) {}

JsonValue::JsonValue(long long value) : type_(Type::Int), value_(value) {}

JsonValue::JsonValue(double value) : type_(Type::Double), value_(value) {}

JsonValue::JsonValue(const char* value) : type_(Type::String), value_(std::string(value)) {}

JsonValue::JsonValue(const std::string& value) : type_(Type::String), value_(value) {}

JsonValue::JsonValue(const Array& value)
    : type_(Type::Array), value_(std::make_shared<Array>(value)) {}

JsonValue::JsonValue(const Object& value)
    : type_(Type::Object), value_(std::make_shared<Object>(value)) {}

bool JsonValue::asBool() const {
    switch (type_) {
        case Type::Bool:
            return std::get<bool>(value_);
        case Type::Int:
            return std::get<long long>(value_) != 0;
        case Type::Double:
            return std::get<double>(value_) != 0.0;
        case Type::String:
            return !std::get<std::string>(value_).empty();
        default:
            return false;
    }
}

int JsonValue::asInt() const {
    switch (type_) {
        case Type::Int:
            return static_cast<int>(std::get<long long>(value_));
        case Type::Double:
            return static_cast<int>(std::get<double>(value_));
        case Type::Bool:
            return std::get<bool>(value_) ? 1 : 0;
        case Type::String:
            try {
                return std::stoi(std::get<std::string>(value_));
            } catch (...) {
                return 0;
            }
        default:
            return 0;
    }
}

long long JsonValue::asLongLong() const {
    switch (type_) {
        case Type::Int:
            return std::get<long long>(value_);
        case Type::Double:
            return static_cast<long long>(std::get<double>(value_));
        case Type::Bool:
            return std::get<bool>(value_) ? 1LL : 0LL;
        case Type::String:
            try {
                return std::stoll(std::get<std::string>(value_));
            } catch (...) {
                return 0LL;
            }
        default:
            return 0LL;
    }
}

double JsonValue::asDouble() const {
    switch (type_) {
        case Type::Double:
            return std::get<double>(value_);
        case Type::Int:
            return static_cast<double>(std::get<long long>(value_));
        case Type::Bool:
            return std::get<bool>(value_) ? 1.0 : 0.0;
        case Type::String:
            try {
                return std::stod(std::get<std::string>(value_));
            } catch (...) {
                return 0.0;
            }
        default:
            return 0.0;
    }
}

float JsonValue::asFloat() const {
    return static_cast<float>(asDouble());
}

std::string JsonValue::asString() const {
    switch (type_) {
        case Type::String:
            return std::get<std::string>(value_);
        case Type::Int:
            return std::to_string(std::get<long long>(value_));
        case Type::Double:
            return std::to_string(std::get<double>(value_));
        case Type::Bool:
            return std::get<bool>(value_) ? "true" : "false";
        case Type::Null:
            return "null";
        default:
            return "";
    }
}

const JsonValue::Array& JsonValue::asArray() const {
    if (type_ != Type::Array) {
        static const Array empty;
        return empty;
    }
    return *std::get<std::shared_ptr<Array>>(value_);
}

const JsonValue::Object& JsonValue::asObject() const {
    if (type_ != Type::Object) {
        static const Object empty;
        return empty;
    }
    return *std::get<std::shared_ptr<Object>>(value_);
}

JsonValue::Array& JsonValue::asArrayMutable() {
    if (type_ != Type::Array) {
        throw std::runtime_error("JsonValue is not an array");
    }
    return *std::get<std::shared_ptr<Array>>(value_);
}

JsonValue::Object& JsonValue::asObjectMutable() {
    if (type_ != Type::Object) {
        throw std::runtime_error("JsonValue is not an object");
    }
    return *std::get<std::shared_ptr<Object>>(value_);
}

const JsonValue& JsonValue::operator[](size_t index) const {
    static const JsonValue nullValue;
    if (type_ != Type::Array) {
        return nullValue;
    }
    const auto& arr = asArray();
    if (index >= arr.size()) {
        return nullValue;
    }
    return arr[index];
}

const JsonValue& JsonValue::operator[](const std::string& key) const {
    static const JsonValue nullValue;
    if (type_ != Type::Object) {
        return nullValue;
    }
    const auto& obj = asObject();
    auto it = obj.find(key);
    if (it == obj.end()) {
        return nullValue;
    }
    return it->second;
}

JsonValue& JsonValue::operator[](const std::string& key) {
    if (type_ == Type::Null) {
        type_ = Type::Object;
        value_ = std::make_shared<Object>();
    }
    if (type_ != Type::Object) {
        throw std::runtime_error("JsonValue is not an object");
    }
    return (*std::get<std::shared_ptr<Object>>(value_))[key];
}

bool JsonValue::hasKey(const std::string& key) const {
    if (type_ != Type::Object) {
        return false;
    }
    const auto& obj = asObject();
    return obj.find(key) != obj.end();
}

// ============================================================================
// Config Implementation
// ============================================================================

Config::Config()
    : root_()
    , filepath_()
    , hotReloadEnabled_(false)
    , reloadCallback_()
    , lastModificationTime_() {
}

Config::~Config() {
    disableHotReload();
}

bool Config::loadFromFile(const std::string& filepath) {
    try {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            return false;
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        file.close();

        std::string content = buffer.str();
        root_ = parseJson(content);
        filepath_ = filepath;

        // Update modification time for hot-reload
        if (std::filesystem::exists(filepath_)) {
            lastModificationTime_ = std::filesystem::last_write_time(filepath_);
        }

        return true;
    } catch (...) {
        return false;
    }
}

bool Config::saveToFile(const std::string& filepath) {
    std::string targetPath = filepath.empty() ? filepath_ : filepath;
    if (targetPath.empty()) {
        return false;
    }

    try {
        std::ofstream file(targetPath);
        if (!file.is_open()) {
            return false;
        }

        std::string json = serializeJson(root_);
        file << json;
        file.close();

        // Update modification time to avoid triggering hot-reload
        if (std::filesystem::exists(targetPath)) {
            lastModificationTime_ = std::filesystem::last_write_time(targetPath);
        }

        return true;
    } catch (...) {
        return false;
    }
}

bool Config::has(const std::string& key) const {
    return navigateToValue(key) != nullptr;
}

void Config::enableHotReload(ReloadCallback callback) {
    hotReloadEnabled_ = true;
    reloadCallback_ = callback;
}

void Config::disableHotReload() {
    hotReloadEnabled_ = false;
    reloadCallback_ = nullptr;
}

void Config::checkForReload() {
    if (!hotReloadEnabled_ || filepath_.empty()) {
        return;
    }

    try {
        if (!std::filesystem::exists(filepath_)) {
            return;
        }

        auto currentModTime = std::filesystem::last_write_time(filepath_);
        if (currentModTime > lastModificationTime_) {
            // File has been modified, reload
            if (loadFromFile(filepath_)) {
                if (reloadCallback_) {
                    reloadCallback_(*this);
                }
            }
        }
    } catch (...) {
        // Ignore errors during hot-reload check
    }
}

std::optional<std::vector<JsonValue>> Config::getArray(const std::string& key) const {
    const JsonValue* value = navigateToValue(key);
    if (!value || !value->isArray()) {
        return std::nullopt;
    }
    return value->asArray();
}

// ============================================================================
// JSON Parsing Implementation
// ============================================================================

void Config::skipWhitespace(const std::string& json, size_t& pos) {
    while (pos < json.length() && std::isspace(json[pos])) {
        ++pos;
    }
}

JsonValue Config::parseJson(const std::string& json) {
    size_t pos = 0;
    skipWhitespace(json, pos);
    return parseValue(json, pos);
}

JsonValue Config::parseValue(const std::string& json, size_t& pos) {
    skipWhitespace(json, pos);

    if (pos >= json.length()) {
        return JsonValue();
    }

    char c = json[pos];

    if (c == '{') {
        return parseObject(json, pos);
    } else if (c == '[') {
        return parseArray(json, pos);
    } else if (c == '"') {
        return parseString(json, pos);
    } else if (c == 't' || c == 'f') {
        return parseBool(json, pos);
    } else if (c == 'n') {
        return parseNull(json, pos);
    } else if (c == '-' || std::isdigit(c)) {
        return parseNumber(json, pos);
    }

    throw std::runtime_error("Invalid JSON at position " + std::to_string(pos));
}

JsonValue Config::parseObject(const std::string& json, size_t& pos) {
    JsonValue::Object obj;

    ++pos; // Skip '{'
    skipWhitespace(json, pos);

    if (pos < json.length() && json[pos] == '}') {
        ++pos;
        return JsonValue(obj);
    }

    while (pos < json.length()) {
        skipWhitespace(json, pos);

        // Parse key
        if (json[pos] != '"') {
            throw std::runtime_error("Expected string key in object");
        }
        std::string key = parseString(json, pos).asString();

        skipWhitespace(json, pos);

        // Expect ':'
        if (pos >= json.length() || json[pos] != ':') {
            throw std::runtime_error("Expected ':' after object key");
        }
        ++pos;

        skipWhitespace(json, pos);

        // Parse value
        JsonValue value = parseValue(json, pos);
        obj[key] = value;

        skipWhitespace(json, pos);

        if (pos >= json.length()) {
            throw std::runtime_error("Unexpected end of JSON in object");
        }

        if (json[pos] == '}') {
            ++pos;
            break;
        } else if (json[pos] == ',') {
            ++pos;
            continue;
        } else {
            throw std::runtime_error("Expected ',' or '}' in object");
        }
    }

    return JsonValue(obj);
}

JsonValue Config::parseArray(const std::string& json, size_t& pos) {
    JsonValue::Array arr;

    ++pos; // Skip '['
    skipWhitespace(json, pos);

    if (pos < json.length() && json[pos] == ']') {
        ++pos;
        return JsonValue(arr);
    }

    while (pos < json.length()) {
        skipWhitespace(json, pos);

        JsonValue value = parseValue(json, pos);
        arr.push_back(value);

        skipWhitespace(json, pos);

        if (pos >= json.length()) {
            throw std::runtime_error("Unexpected end of JSON in array");
        }

        if (json[pos] == ']') {
            ++pos;
            break;
        } else if (json[pos] == ',') {
            ++pos;
            continue;
        } else {
            throw std::runtime_error("Expected ',' or ']' in array");
        }
    }

    return JsonValue(arr);
}

JsonValue Config::parseString(const std::string& json, size_t& pos) {
    ++pos; // Skip opening '"'

    std::string result;
    while (pos < json.length()) {
        char c = json[pos];

        if (c == '"') {
            ++pos;
            return JsonValue(result);
        } else if (c == '\\') {
            ++pos;
            if (pos >= json.length()) {
                throw std::runtime_error("Unexpected end of string escape");
            }

            char escaped = json[pos];
            switch (escaped) {
                case '"':  result += '"'; break;
                case '\\': result += '\\'; break;
                case '/':  result += '/'; break;
                case 'b':  result += '\b'; break;
                case 'f':  result += '\f'; break;
                case 'n':  result += '\n'; break;
                case 'r':  result += '\r'; break;
                case 't':  result += '\t'; break;
                case 'u': {
                    // Unicode escape - simplified handling
                    ++pos;
                    if (pos + 3 >= json.length()) {
                        throw std::runtime_error("Invalid unicode escape");
                    }
                    // For simplicity, just copy the raw unicode sequence
                    result += "\\u";
                    result += json.substr(pos, 4);
                    pos += 3;
                    break;
                }
                default:
                    result += escaped;
                    break;
            }
            ++pos;
        } else {
            result += c;
            ++pos;
        }
    }

    throw std::runtime_error("Unterminated string");
}

JsonValue Config::parseNumber(const std::string& json, size_t& pos) {
    size_t start = pos;
    bool isDouble = false;

    // Optional minus
    if (json[pos] == '-') {
        ++pos;
    }

    // Integer part
    if (pos >= json.length() || !std::isdigit(json[pos])) {
        throw std::runtime_error("Invalid number");
    }

    while (pos < json.length() && std::isdigit(json[pos])) {
        ++pos;
    }

    // Fractional part
    if (pos < json.length() && json[pos] == '.') {
        isDouble = true;
        ++pos;
        while (pos < json.length() && std::isdigit(json[pos])) {
            ++pos;
        }
    }

    // Exponent part
    if (pos < json.length() && (json[pos] == 'e' || json[pos] == 'E')) {
        isDouble = true;
        ++pos;
        if (pos < json.length() && (json[pos] == '+' || json[pos] == '-')) {
            ++pos;
        }
        while (pos < json.length() && std::isdigit(json[pos])) {
            ++pos;
        }
    }

    std::string numberStr = json.substr(start, pos - start);

    if (isDouble) {
        return JsonValue(std::stod(numberStr));
    } else {
        return JsonValue(std::stoll(numberStr));
    }
}

JsonValue Config::parseBool(const std::string& json, size_t& pos) {
    if (json.substr(pos, 4) == "true") {
        pos += 4;
        return JsonValue(true);
    } else if (json.substr(pos, 5) == "false") {
        pos += 5;
        return JsonValue(false);
    }
    throw std::runtime_error("Invalid boolean value");
}

JsonValue Config::parseNull(const std::string& json, size_t& pos) {
    if (json.substr(pos, 4) == "null") {
        pos += 4;
        return JsonValue();
    }
    throw std::runtime_error("Invalid null value");
}

// ============================================================================
// JSON Serialization Implementation
// ============================================================================

std::string Config::serializeJson(const JsonValue& value, int indent) const {
    std::string indentStr(indent * 2, ' ');
    std::string nextIndentStr((indent + 1) * 2, ' ');

    switch (value.getType()) {
        case JsonValue::Type::Null:
            return "null";

        case JsonValue::Type::Bool:
            return value.asBool() ? "true" : "false";

        case JsonValue::Type::Int:
            return std::to_string(value.asLongLong());

        case JsonValue::Type::Double: {
            double d = value.asDouble();
            // Check if the double is effectively an integer
            if (std::floor(d) == d && std::abs(d) < 1e15) {
                return std::to_string(static_cast<long long>(d));
            }
            std::string str = std::to_string(d);
            // Remove trailing zeros after decimal point
            size_t decimal = str.find('.');
            if (decimal != std::string::npos) {
                size_t lastNonZero = str.find_last_not_of('0');
                if (lastNonZero > decimal) {
                    str = str.substr(0, lastNonZero + 1);
                }
                if (str.back() == '.') {
                    str += "0";
                }
            }
            return str;
        }

        case JsonValue::Type::String: {
            std::string str = value.asString();
            std::string escaped = "\"";
            for (char c : str) {
                switch (c) {
                    case '"':  escaped += "\\\""; break;
                    case '\\': escaped += "\\\\"; break;
                    case '\b': escaped += "\\b"; break;
                    case '\f': escaped += "\\f"; break;
                    case '\n': escaped += "\\n"; break;
                    case '\r': escaped += "\\r"; break;
                    case '\t': escaped += "\\t"; break;
                    default:
                        if (c < 32) {
                            char buf[7];
                            snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                            escaped += buf;
                        } else {
                            escaped += c;
                        }
                        break;
                }
            }
            escaped += "\"";
            return escaped;
        }

        case JsonValue::Type::Array: {
            const auto& arr = value.asArray();
            if (arr.empty()) {
                return "[]";
            }

            std::string result = "[\n";
            for (size_t i = 0; i < arr.size(); ++i) {
                result += nextIndentStr + serializeJson(arr[i], indent + 1);
                if (i < arr.size() - 1) {
                    result += ",";
                }
                result += "\n";
            }
            result += indentStr + "]";
            return result;
        }

        case JsonValue::Type::Object: {
            const auto& obj = value.asObject();
            if (obj.empty()) {
                return "{}";
            }

            std::string result = "{\n";
            size_t count = 0;
            for (const auto& [key, val] : obj) {
                result += nextIndentStr + "\"" + key + "\": ";
                result += serializeJson(val, indent + 1);
                if (count < obj.size() - 1) {
                    result += ",";
                }
                result += "\n";
                ++count;
            }
            result += indentStr + "}";
            return result;
        }
    }

    return "null";
}

// ============================================================================
// Navigation Implementation
// ============================================================================

const JsonValue* Config::navigateToValue(const std::string& key) const {
    if (key.empty()) {
        return &root_;
    }

    const JsonValue* current = &root_;
    std::string currentKey;

    for (size_t i = 0; i <= key.length(); ++i) {
        if (i == key.length() || key[i] == '.') {
            if (!currentKey.empty()) {
                if (!current->isObject()) {
                    return nullptr;
                }

                if (!current->hasKey(currentKey)) {
                    return nullptr;
                }

                current = &(*current)[currentKey];
                currentKey.clear();
            }
        } else {
            currentKey += key[i];
        }
    }

    return current;
}

JsonValue* Config::navigateToValueMutable(const std::string& key) {
    if (key.empty()) {
        return &root_;
    }

    JsonValue* current = &root_;
    std::string currentKey;

    for (size_t i = 0; i <= key.length(); ++i) {
        if (i == key.length() || key[i] == '.') {
            if (!currentKey.empty()) {
                if (current->getType() == JsonValue::Type::Null) {
                    *current = JsonValue(JsonValue::Object{});
                }

                if (!current->isObject()) {
                    return nullptr;
                }

                current = &(*current)[currentKey];
                currentKey.clear();
            }
        } else {
            currentKey += key[i];
        }
    }

    return current;
}

void Config::createNestedStructure(const std::string& key, const JsonValue& value) {
    if (key.empty()) {
        return;
    }

    JsonValue* current = &root_;
    if (current->getType() == JsonValue::Type::Null) {
        *current = JsonValue(JsonValue::Object{});
    }

    std::string currentKey;
    std::vector<std::string> keys;

    // Split the key by dots
    for (size_t i = 0; i <= key.length(); ++i) {
        if (i == key.length() || key[i] == '.') {
            if (!currentKey.empty()) {
                keys.push_back(currentKey);
                currentKey.clear();
            }
        } else {
            currentKey += key[i];
        }
    }

    // Navigate/create nested structure
    for (size_t i = 0; i < keys.size(); ++i) {
        const std::string& k = keys[i];

        if (i == keys.size() - 1) {
            // Last key, set the value
            if (!current->isObject()) {
                return;
            }
            (*current)[k] = value;
        } else {
            // Intermediate key, ensure it's an object
            if (!current->isObject()) {
                return;
            }

            JsonValue& next = (*current)[k];
            if (next.getType() == JsonValue::Type::Null) {
                next = JsonValue(JsonValue::Object{});
            }
            current = &next;
        }
    }
}

} // namespace Core
} // namespace Engine
