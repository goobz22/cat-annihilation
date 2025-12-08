#pragma once

#include "Scene.hpp"
#include <string>
#include <sstream>
#include <unordered_map>
#include <functional>

namespace CatEngine {

/**
 * Simple JSON-like value type for serialization
 * Can be replaced with nlohmann/json or other JSON library
 */
class JsonValue {
public:
    enum class Type {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object
    };

    JsonValue() : type_(Type::Null) {}
    explicit JsonValue(bool value);
    explicit JsonValue(int value);
    explicit JsonValue(float value);
    explicit JsonValue(double value);
    explicit JsonValue(const std::string& value);
    explicit JsonValue(const char* value);

    // Factory methods
    static JsonValue array();
    static JsonValue object();

    // Type checking
    Type getType() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isBool() const { return type_ == Type::Bool; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    // Getters
    bool asBool() const;
    double asNumber() const;
    const std::string& asString() const;

    // Array operations
    void push(const JsonValue& value);
    JsonValue& operator[](size_t index);
    const JsonValue& operator[](size_t index) const;
    size_t size() const;

    // Object operations
    void set(const std::string& key, const JsonValue& value);
    JsonValue& operator[](const std::string& key);
    const JsonValue& operator[](const std::string& key) const;
    bool has(const std::string& key) const;

    // Serialization
    std::string toString(int indent = 0) const;
    static JsonValue parse(const std::string& json);

private:
    Type type_;
    bool boolValue_;
    double numberValue_;
    std::string stringValue_;
    std::vector<JsonValue> arrayValue_;
    std::unordered_map<std::string, JsonValue> objectValue_;
};

/**
 * Component Serializer Interface
 * Implement this to serialize custom component types
 */
class IComponentSerializer {
public:
    virtual ~IComponentSerializer() = default;

    /**
     * Serialize component data to JSON
     */
    virtual JsonValue serialize(const void* componentData) const = 0;

    /**
     * Deserialize component data from JSON
     */
    virtual void deserialize(const JsonValue& json, void* componentData) const = 0;

    /**
     * Get component size in bytes
     */
    virtual size_t getComponentSize() const = 0;

    /**
     * Get component type name
     */
    virtual const char* getTypeName() const = 0;
};

/**
 * Template component serializer
 */
template<typename T>
class ComponentSerializer : public IComponentSerializer {
public:
    using SerializeFunc = std::function<JsonValue(const T&)>;
    using DeserializeFunc = std::function<void(const JsonValue&, T&)>;

    ComponentSerializer(const char* typeName, SerializeFunc serializer, DeserializeFunc deserializer)
        : typeName_(typeName)
        , serializer_(std::move(serializer))
        , deserializer_(std::move(deserializer))
    {}

    JsonValue serialize(const void* componentData) const override {
        return serializer_(*static_cast<const T*>(componentData));
    }

    void deserialize(const JsonValue& json, void* componentData) const override {
        deserializer_(json, *static_cast<T*>(componentData));
    }

    size_t getComponentSize() const override {
        return sizeof(T);
    }

    const char* getTypeName() const override {
        return typeName_;
    }

private:
    const char* typeName_;
    SerializeFunc serializer_;
    DeserializeFunc deserializer_;
};

/**
 * Scene Serializer
 * Saves and loads scenes to/from JSON format
 */
class SceneSerializer {
public:
    static constexpr int VERSION = 1;

    SceneSerializer();
    ~SceneSerializer() = default;

    // ========================================================================
    // Component Serializer Registration
    // ========================================================================

    /**
     * Register a component serializer
     */
    template<typename T>
    void registerComponentSerializer(
        const char* typeName,
        typename ComponentSerializer<T>::SerializeFunc serializer,
        typename ComponentSerializer<T>::DeserializeFunc deserializer)
    {
        ComponentTypeId typeId = getComponentTypeId<T>();
        componentSerializers_[typeId] = std::make_unique<ComponentSerializer<T>>(
            typeName, std::move(serializer), std::move(deserializer)
        );
    }

    /**
     * Check if component type has a serializer
     */
    template<typename T>
    bool hasSerializer() const {
        ComponentTypeId typeId = getComponentTypeId<T>();
        return componentSerializers_.find(typeId) != componentSerializers_.end();
    }

    // ========================================================================
    // Scene Serialization
    // ========================================================================

    /**
     * Save scene to JSON string
     */
    std::string saveToString(const Scene& scene);

    /**
     * Save scene to file
     */
    bool saveToFile(const Scene& scene, const std::string& filepath);

    /**
     * Load scene from JSON string
     */
    std::unique_ptr<Scene> loadFromString(const std::string& json);

    /**
     * Load scene from file
     */
    std::unique_ptr<Scene> loadFromFile(const std::string& filepath);

    // ========================================================================
    // Node Serialization
    // ========================================================================

    /**
     * Serialize a scene node and its subtree
     */
    JsonValue serializeNode(const SceneNode* node, const ECS& ecs);

    /**
     * Deserialize a scene node and its subtree
     */
    SceneNode* deserializeNode(const JsonValue& json, ECS& ecs, SceneNode* parent);

private:
    // Serialization helpers
    JsonValue serializeTransform(const Engine::Transform& transform);
    Engine::Transform deserializeTransform(const JsonValue& json);

    JsonValue serializeEntity(Entity entity, const ECS& ecs);
    Entity deserializeEntity(const JsonValue& json, ECS& ecs);

    // Component serializers registry
    std::unordered_map<ComponentTypeId, std::unique_ptr<IComponentSerializer>> componentSerializers_;

    // Entity ID remapping (for handling entity references during deserialization)
    std::unordered_map<uint64_t, Entity> entityRemap_;
};

} // namespace CatEngine
