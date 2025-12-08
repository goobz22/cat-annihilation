# Engine Core - Config System

A high-performance, type-safe configuration system for the Cat Annihilation game engine with built-in JSON parser.

## Features

- **JSON-based configuration** - Human-readable, industry-standard format
- **Type-safe getters** - Template-based API with compile-time type checking
- **Dot notation access** - Navigate nested structures easily: `config.get<int>("graphics.shadows.resolution")`
- **Default values** - Graceful fallback for missing keys
- **Hot-reload support** - Watch for file changes and reload automatically
- **Write-back capability** - Save modified settings from in-game menus
- **No external dependencies** - Built-in JSON parser, C++20 standard library only
- **Multiple type support** - int, long long, float, double, bool, string, arrays, objects

## Quick Start

### Basic Usage

```cpp
#include "Config.hpp"

using namespace Engine::Core;

int main() {
    Config config;

    // Load configuration
    config.loadFromFile("game_config.json");

    // Read values with type safety and defaults
    int width = config.get<int>("graphics.resolution.width", 1920);
    int height = config.get<int>("graphics.resolution.height", 1080);
    bool vsync = config.get<bool>("graphics.vsync", true);
    float fov = config.get<float>("graphics.fov", 90.0f);

    // Modify values
    config.set("graphics.resolution.width", 2560);
    config.set("graphics.vsync", false);

    // Save changes
    config.saveToFile();

    return 0;
}
```

### Hot-Reload Example

```cpp
// Enable hot-reload with callback
config.enableHotReload([](const Config& cfg) {
    std::cout << "Config reloaded!" << std::endl;
    // Update engine settings here
    updateGraphicsSettings(cfg);
});

// In your game loop
while (running) {
    config.checkForReload();  // Check for file modifications
    // ... rest of game loop
}
```

### Advanced Usage

```cpp
// Check if key exists
if (config.has("graphics.raytracing.enabled")) {
    bool rtEnabled = config.get<bool>("graphics.raytracing.enabled", false);
}

// Use optional for explicit handling
auto optValue = config.get<int>("graphics.antialiasing");
if (optValue.has_value()) {
    int aaLevel = optValue.value();
} else {
    // Key doesn't exist, set default
    config.set("graphics.antialiasing", 4);
}

// Access arrays
auto keybindings = config.getArray("gameplay.keybindings");
if (keybindings.has_value()) {
    for (const auto& binding : keybindings.value()) {
        std::string key = binding.asString();
        // Process keybinding
    }
}

// Direct JsonValue access for complex structures
const JsonValue& root = config.getRoot();
if (root.isObject() && root.hasKey("graphics")) {
    const JsonValue& graphics = root["graphics"];
    // Navigate manually
}
```

## JSON Configuration Format

```json
{
  "graphics": {
    "resolution": {
      "width": 1920,
      "height": 1080
    },
    "shadows": {
      "enabled": true,
      "resolution": 2048,
      "quality": "high"
    },
    "vsync": true,
    "fov": 90.0
  },
  "audio": {
    "masterVolume": 0.8,
    "musicVolume": 0.6,
    "sfxVolume": 1.0
  },
  "gameplay": {
    "difficulty": "normal",
    "mouseSensitivity": 0.5,
    "invertY": false
  }
}
```

## API Reference

### Config Class

#### Loading and Saving

- `bool loadFromFile(const std::string& filepath)` - Load configuration from JSON file
- `bool saveToFile(const std::string& filepath = "")` - Save configuration to file
- `const JsonValue& getRoot() const` - Get root JSON value for direct access

#### Reading Values

- `template<typename T> std::optional<T> get(const std::string& key) const`
  - Returns optional containing value if key exists and is convertible to T
  - Supported types: `int`, `long long`, `float`, `double`, `bool`, `std::string`

- `template<typename T> T get(const std::string& key, const T& defaultValue) const`
  - Returns value if key exists, otherwise returns defaultValue
  - Type-safe with automatic conversion

- `std::optional<std::vector<JsonValue>> getArray(const std::string& key) const`
  - Returns array if key exists and is an array type

#### Writing Values

- `template<typename T> void set(const std::string& key, const T& value)`
  - Set a value at the specified key path
  - Creates nested structure automatically
  - Supported types: `int`, `long long`, `float`, `double`, `bool`, `std::string`

#### Key Checking

- `bool has(const std::string& key) const` - Check if key exists

#### Hot-Reload

- `void enableHotReload(ReloadCallback callback)` - Enable file watching
- `void disableHotReload()` - Disable file watching
- `void checkForReload()` - Check for modifications and reload if changed

### JsonValue Class

#### Type Checking

- `Type getType() const` - Get the value type
- `bool isNull()`, `isBool()`, `isInt()`, `isDouble()`, `isString()`, `isArray()`, `isObject()`

#### Value Access

- `bool asBool() const` - Convert to boolean
- `int asInt() const` - Convert to integer
- `long long asLongLong() const` - Convert to long long
- `float asFloat() const` - Convert to float
- `double asDouble() const` - Convert to double
- `std::string asString() const` - Convert to string
- `const Array& asArray() const` - Get array (returns empty if not array)
- `const Object& asObject() const` - Get object (returns empty if not object)

#### Array/Object Access

- `const JsonValue& operator[](size_t index) const` - Array element access
- `const JsonValue& operator[](const std::string& key) const` - Object member access
- `bool hasKey(const std::string& key) const` - Check if object has key

## Building

### Using Makefile

```bash
# Build example program
make example

# Run example
make run

# Clean build artifacts
make clean
```

### Manual Compilation

```bash
# Compile the example
g++ -std=c++20 -o config_example config_example.cpp Config.cpp

# Run
./config_example
```

### Integration into Larger Project

```cmake
# CMakeLists.txt
add_library(EngineCore Config.cpp)
target_include_directories(EngineCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(EngineCore PUBLIC cxx_std_20)
```

## Performance Considerations

1. **File I/O** - Loading/saving is disk-bound. Cache the config object rather than repeatedly loading.

2. **Hot-Reload** - `checkForReload()` performs a filesystem stat call. Call it once per frame or less frequently.

3. **Nested Access** - Dot notation navigation has O(n) complexity where n is the depth. For frequently accessed values, cache the result:

```cpp
// Good - cache frequently used values
int shadowRes = config.get<int>("graphics.shadows.resolution", 2048);
// Use shadowRes throughout frame

// Avoid - repeated lookups
for (int i = 0; i < 1000; i++) {
    int res = config.get<int>("graphics.shadows.resolution", 2048);  // Inefficient
}
```

4. **Type Conversion** - JsonValue performs automatic type conversion. For critical paths, ensure types match the stored type to avoid conversion overhead.

## Thread Safety

**The Config class is NOT thread-safe.** If you need to access configuration from multiple threads:

1. Use external synchronization (mutex)
2. Load config on main thread, pass values to worker threads
3. Only reload on main thread

```cpp
// Example: Thread-safe reading
std::shared_mutex configMutex;
Config config;

// Reading thread
{
    std::shared_lock lock(configMutex);
    int value = config.get<int>("some.key", 0);
}

// Writing/reloading thread (exclusive)
{
    std::unique_lock lock(configMutex);
    config.set("some.key", 42);
    config.saveToFile();
}
```

## Error Handling

The Config system uses return values for error handling:

- `loadFromFile()` returns `false` on failure (file not found, parse error)
- `saveToFile()` returns `false` on failure (permission error, disk full)
- `get<T>()` returns `std::nullopt` if key doesn't exist or type conversion fails
- `get<T>(key, default)` always returns a value (either found or default)

```cpp
if (!config.loadFromFile("config.json")) {
    std::cerr << "Failed to load config, using defaults" << std::endl;
    // Set defaults
    config.set("graphics.resolution.width", 1920);
    // ...
}
```

## Best Practices

1. **Use defaults liberally** - Make your engine robust to missing config values
2. **Validate ranges** - Config system doesn't enforce value ranges
3. **Cache frequently accessed values** - Don't lookup the same key every frame
4. **Use meaningful key names** - `graphics.shadows.resolution` not `gfx.s.r`
5. **Group related settings** - Use nested objects for organization
6. **Document your config schema** - Provide example JSON files
7. **Save only when needed** - Don't save every frame, save when user changes settings

## Example: Settings Menu Integration

```cpp
class SettingsMenu {
    Config& config;

    void applyGraphicsSettings() {
        int width = widthSlider.getValue();
        int height = heightSlider.getValue();
        bool vsync = vsyncCheckbox.isChecked();

        config.set("graphics.resolution.width", width);
        config.set("graphics.resolution.height", height);
        config.set("graphics.vsync", vsync);

        // Apply settings to engine
        renderer.setResolution(width, height);
        renderer.setVSync(vsync);
    }

    void saveSettings() {
        if (config.saveToFile()) {
            showNotification("Settings saved!");
        } else {
            showError("Failed to save settings");
        }
    }
};
```

## Limitations

- **No schema validation** - No automatic type or range checking
- **No comments in JSON** - Standard JSON doesn't support comments (use separate documentation)
- **No cyclic references** - Not supported in JSON format
- **Memory usage** - Entire config loaded into memory (fine for typical game configs)

## Future Enhancements

Potential additions (not yet implemented):

- Binary serialization for faster loading
- Config file encryption
- Remote config loading (HTTP/HTTPS)
- Config diffing and merging
- Schema validation
- YAML/TOML format support

## License

Part of the Cat Annihilation Engine.

## Support

For issues or questions, refer to the main engine documentation.
