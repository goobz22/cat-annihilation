#include "SceneSerializer.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>

namespace CatEngine {

// ============================================================================
// JsonValue Implementation
// ============================================================================

JsonValue::JsonValue(bool value)
    : type_(Type::Bool), boolValue_(value), numberValue_(0.0) {}

JsonValue::JsonValue(int value)
    : type_(Type::Number), boolValue_(false), numberValue_(static_cast<double>(value)) {}

JsonValue::JsonValue(float value)
    : type_(Type::Number), boolValue_(false), numberValue_(static_cast<double>(value)) {}

JsonValue::JsonValue(double value)
    : type_(Type::Number), boolValue_(false), numberValue_(value) {}

JsonValue::JsonValue(const std::string& value)
    : type_(Type::String), boolValue_(false), numberValue_(0.0), stringValue_(value) {}

JsonValue::JsonValue(const char* value)
    : type_(Type::String), boolValue_(false), numberValue_(0.0), stringValue_(value) {}

JsonValue JsonValue::array() {
    JsonValue value;
    value.type_ = Type::Array;
    return value;
}

JsonValue JsonValue::object() {
    JsonValue value;
    value.type_ = Type::Object;
    return value;
}

bool JsonValue::asBool() const {
    return boolValue_;
}

double JsonValue::asNumber() const {
    return numberValue_;
}

const std::string& JsonValue::asString() const {
    return stringValue_;
}

void JsonValue::push(const JsonValue& value) {
    if (type_ != Type::Array) {
        type_ = Type::Array;
        arrayValue_.clear();
    }
    arrayValue_.push_back(value);
}

JsonValue& JsonValue::operator[](size_t index) {
    if (type_ != Type::Array) {
        type_ = Type::Array;
        arrayValue_.clear();
    }
    if (index >= arrayValue_.size()) {
        arrayValue_.resize(index + 1);
    }
    return arrayValue_[index];
}

const JsonValue& JsonValue::operator[](size_t index) const {
    static JsonValue null;
    if (type_ != Type::Array || index >= arrayValue_.size()) {
        return null;
    }
    return arrayValue_[index];
}

size_t JsonValue::size() const {
    if (type_ == Type::Array) {
        return arrayValue_.size();
    } else if (type_ == Type::Object) {
        return objectValue_.size();
    }
    return 0;
}

void JsonValue::set(const std::string& key, const JsonValue& value) {
    if (type_ != Type::Object) {
        type_ = Type::Object;
        objectValue_.clear();
    }
    objectValue_[key] = value;
}

JsonValue& JsonValue::operator[](const std::string& key) {
    if (type_ != Type::Object) {
        type_ = Type::Object;
        objectValue_.clear();
    }
    return objectValue_[key];
}

const JsonValue& JsonValue::operator[](const std::string& key) const {
    static JsonValue null;
    if (type_ != Type::Object) {
        return null;
    }
    auto it = objectValue_.find(key);
    return (it != objectValue_.end()) ? it->second : null;
}

bool JsonValue::has(const std::string& key) const {
    return type_ == Type::Object && objectValue_.find(key) != objectValue_.end();
}

std::string JsonValue::toString(int indent) const {
    std::ostringstream ss;
    std::string indentStr(indent * 2, ' ');
    std::string nextIndentStr((indent + 1) * 2, ' ');

    switch (type_) {
        case Type::Null:
            ss << "null";
            break;

        case Type::Bool:
            ss << (boolValue_ ? "true" : "false");
            break;

        case Type::Number:
            ss << std::fixed << std::setprecision(6) << numberValue_;
            break;

        case Type::String:
            ss << "\"" << stringValue_ << "\"";
            break;

        case Type::Array:
            if (arrayValue_.empty()) {
                ss << "[]";
            } else {
                ss << "[\n";
                for (size_t i = 0; i < arrayValue_.size(); ++i) {
                    ss << nextIndentStr << arrayValue_[i].toString(indent + 1);
                    if (i < arrayValue_.size() - 1) {
                        ss << ",";
                    }
                    ss << "\n";
                }
                ss << indentStr << "]";
            }
            break;

        case Type::Object:
            if (objectValue_.empty()) {
                ss << "{}";
            } else {
                ss << "{\n";
                size_t i = 0;
                for (const auto& [key, value] : objectValue_) {
                    ss << nextIndentStr << "\"" << key << "\": " << value.toString(indent + 1);
                    if (i < objectValue_.size() - 1) {
                        ss << ",";
                    }
                    ss << "\n";
                    i++;
                }
                ss << indentStr << "}";
            }
            break;
    }

    return ss.str();
}

JsonValue JsonValue::parse(const std::string& json) {
    // JSON parser implementation
    JsonValue result;
    size_t pos = 0;

    // Skip whitespace
    auto skipWhitespace = [&]() {
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r')) {
            pos++;
        }
    };

    // Forward declarations for recursive parsing
    std::function<JsonValue()> parseValue;
    std::function<std::string()> parseString;
    std::function<double()> parseNumber;

    parseString = [&]() -> std::string {
        std::string str;
        if (pos >= json.size() || json[pos] != '"') return str;
        pos++; // Skip opening quote

        while (pos < json.size() && json[pos] != '"') {
            if (json[pos] == '\\' && pos + 1 < json.size()) {
                pos++;
                switch (json[pos]) {
                    case 'n': str += '\n'; break;
                    case 't': str += '\t'; break;
                    case 'r': str += '\r'; break;
                    case '"': str += '"'; break;
                    case '\\': str += '\\'; break;
                    default: str += json[pos]; break;
                }
            } else {
                str += json[pos];
            }
            pos++;
        }
        if (pos < json.size()) pos++; // Skip closing quote
        return str;
    };

    parseNumber = [&]() -> double {
        size_t start = pos;
        if (pos < json.size() && json[pos] == '-') pos++;
        while (pos < json.size() && (std::isdigit(json[pos]) || json[pos] == '.' || json[pos] == 'e' || json[pos] == 'E' || json[pos] == '+' || json[pos] == '-')) {
            pos++;
        }
        return std::stod(json.substr(start, pos - start));
    };

    parseValue = [&]() -> JsonValue {
        skipWhitespace();
        if (pos >= json.size()) return JsonValue();

        char c = json[pos];

        // String
        if (c == '"') {
            return JsonValue(parseString());
        }
        // Number
        if (c == '-' || std::isdigit(c)) {
            return JsonValue(parseNumber());
        }
        // Object
        if (c == '{') {
            JsonValue obj = JsonValue::object();
            pos++; // Skip '{'
            skipWhitespace();
            if (pos < json.size() && json[pos] == '}') {
                pos++;
                return obj;
            }
            while (pos < json.size()) {
                skipWhitespace();
                std::string key = parseString();
                skipWhitespace();
                if (pos < json.size() && json[pos] == ':') pos++;
                skipWhitespace();
                obj.set(key, parseValue());
                skipWhitespace();
                if (pos < json.size() && json[pos] == ',') {
                    pos++;
                } else {
                    break;
                }
            }
            skipWhitespace();
            if (pos < json.size() && json[pos] == '}') pos++;
            return obj;
        }
        // Array
        if (c == '[') {
            JsonValue arr = JsonValue::array();
            pos++; // Skip '['
            skipWhitespace();
            if (pos < json.size() && json[pos] == ']') {
                pos++;
                return arr;
            }
            while (pos < json.size()) {
                skipWhitespace();
                arr.push(parseValue());
                skipWhitespace();
                if (pos < json.size() && json[pos] == ',') {
                    pos++;
                } else {
                    break;
                }
            }
            skipWhitespace();
            if (pos < json.size() && json[pos] == ']') pos++;
            return arr;
        }
        // Boolean true
        if (json.substr(pos, 4) == "true") {
            pos += 4;
            return JsonValue(true);
        }
        // Boolean false
        if (json.substr(pos, 5) == "false") {
            pos += 5;
            return JsonValue(false);
        }
        // Null
        if (json.substr(pos, 4) == "null") {
            pos += 4;
            return JsonValue();
        }

        return JsonValue();
    };

    result = parseValue();
    return result;
}

// ============================================================================
// SceneSerializer Implementation
// ============================================================================

SceneSerializer::SceneSerializer() {
}

std::string SceneSerializer::saveToString(const Scene& scene) {
    JsonValue root = JsonValue::object();

    // Version info
    root["version"] = JsonValue(VERSION);

    // Scene metadata
    JsonValue metadata = JsonValue::object();
    metadata["name"] = JsonValue(scene.getName());
    metadata["active"] = JsonValue(scene.isActive());
    root["metadata"] = metadata;

    // Serialize scene graph
    JsonValue sceneGraph = JsonValue::array();
    const SceneNode* rootNode = scene.getRootNode();

    for (size_t i = 0; i < rootNode->getChildCount(); ++i) {
        const SceneNode* child = rootNode->getChildAt(i);
        sceneGraph.push(serializeNode(child, scene.getECS()));
    }

    root["sceneGraph"] = sceneGraph;

    return root.toString();
}

bool SceneSerializer::saveToFile(const Scene& scene, const std::string& filepath) {
    std::string json = saveToString(scene);

    std::ofstream file(filepath);
    if (!file.is_open()) {
        return false;
    }

    file << json;
    file.close();
    return true;
}

std::unique_ptr<Scene> SceneSerializer::loadFromString(const std::string& json) {
    JsonValue root = JsonValue::parse(json);

    if (!root.has("version") || !root.has("metadata") || !root.has("sceneGraph")) {
        return nullptr;
    }

    // Check version
    int version = static_cast<int>(root["version"].asNumber());
    if (version != VERSION) {
        // Version mismatch - could handle migration here
        return nullptr;
    }

    // Create scene
    const JsonValue& metadata = root["metadata"];
    std::string name = metadata.has("name") ? metadata["name"].asString() : "Scene";
    auto scene = std::make_unique<Scene>(name);

    if (metadata.has("active")) {
        scene->setActive(metadata["active"].asBool());
    }

    // Clear entity remap
    entityRemap_.clear();

    // Deserialize scene graph
    const JsonValue& sceneGraph = root["sceneGraph"];
    for (size_t i = 0; i < sceneGraph.size(); ++i) {
        deserializeNode(sceneGraph[i], scene->getECS(), scene->getRootNode());
    }

    return scene;
}

std::unique_ptr<Scene> SceneSerializer::loadFromFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        return nullptr;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    file.close();

    return loadFromString(buffer.str());
}

// ============================================================================
// Node Serialization
// ============================================================================

JsonValue SceneSerializer::serializeNode(const SceneNode* node, const ECS& ecs) {
    JsonValue nodeJson = JsonValue::object();

    // Basic properties
    nodeJson["name"] = JsonValue(node->getName());
    nodeJson["active"] = JsonValue(node->isActive());

    // Transform
    nodeJson["transform"] = serializeTransform(node->getLocalTransform());

    // Entity (if present)
    if (node->hasEntity()) {
        nodeJson["entity"] = serializeEntity(node->getEntity(), ecs);
    }

    // Children
    JsonValue children = JsonValue::array();
    for (size_t i = 0; i < node->getChildCount(); ++i) {
        const SceneNode* child = node->getChildAt(i);
        children.push(serializeNode(child, ecs));
    }
    nodeJson["children"] = children;

    return nodeJson;
}

SceneNode* SceneSerializer::deserializeNode(const JsonValue& json, ECS& ecs, SceneNode* parent) {
    if (!json.has("name")) {
        return nullptr;
    }

    // Create node
    auto node = std::make_unique<SceneNode>(json["name"].asString());

    if (json.has("active")) {
        node->setActive(json["active"].asBool());
    }

    // Transform
    if (json.has("transform")) {
        node->setLocalTransform(deserializeTransform(json["transform"]));
    }

    // Entity
    if (json.has("entity")) {
        Entity entity = deserializeEntity(json["entity"], ecs);
        node->setEntity(entity);
    }

    SceneNode* nodePtr = node.get();

    // Add to parent
    if (parent) {
        parent->addChild(std::move(node));
    }

    // Children
    if (json.has("children")) {
        const JsonValue& children = json["children"];
        for (size_t i = 0; i < children.size(); ++i) {
            deserializeNode(children[i], ecs, nodePtr);
        }
    }

    return nodePtr;
}

// ============================================================================
// Transform Serialization
// ============================================================================

JsonValue SceneSerializer::serializeTransform(const Engine::Transform& transform) {
    JsonValue json = JsonValue::object();

    // Position
    JsonValue position = JsonValue::array();
    position.push(JsonValue(transform.position.x));
    position.push(JsonValue(transform.position.y));
    position.push(JsonValue(transform.position.z));
    json["position"] = position;

    // Rotation (as quaternion)
    JsonValue rotation = JsonValue::array();
    rotation.push(JsonValue(transform.rotation.x));
    rotation.push(JsonValue(transform.rotation.y));
    rotation.push(JsonValue(transform.rotation.z));
    rotation.push(JsonValue(transform.rotation.w));
    json["rotation"] = rotation;

    // Scale
    JsonValue scale = JsonValue::array();
    scale.push(JsonValue(transform.scale.x));
    scale.push(JsonValue(transform.scale.y));
    scale.push(JsonValue(transform.scale.z));
    json["scale"] = scale;

    return json;
}

Engine::Transform SceneSerializer::deserializeTransform(const JsonValue& json) {
    Engine::Transform transform;

    // Position
    if (json.has("position")) {
        const JsonValue& pos = json["position"];
        if (pos.size() >= 3) {
            transform.position = Engine::vec3(
                static_cast<float>(pos[0].asNumber()),
                static_cast<float>(pos[1].asNumber()),
                static_cast<float>(pos[2].asNumber())
            );
        }
    }

    // Rotation
    if (json.has("rotation")) {
        const JsonValue& rot = json["rotation"];
        if (rot.size() >= 4) {
            transform.rotation = Engine::Quaternion(
                static_cast<float>(rot[0].asNumber()),
                static_cast<float>(rot[1].asNumber()),
                static_cast<float>(rot[2].asNumber()),
                static_cast<float>(rot[3].asNumber())
            );
        }
    }

    // Scale
    if (json.has("scale")) {
        const JsonValue& scl = json["scale"];
        if (scl.size() >= 3) {
            transform.scale = Engine::vec3(
                static_cast<float>(scl[0].asNumber()),
                static_cast<float>(scl[1].asNumber()),
                static_cast<float>(scl[2].asNumber())
            );
        }
    }

    return transform;
}

// ============================================================================
// Entity Serialization
// ============================================================================

JsonValue SceneSerializer::serializeEntity(Entity entity, const ECS& ecs) {
    JsonValue json = JsonValue::object();

    // Store original entity ID for reference resolution
    json["id"] = JsonValue(static_cast<double>(entity.id));

    // Components array
    JsonValue components = JsonValue::array();

    // Serialize components using registered component serializers
    // Note: Full component serialization requires ECS to expose component iteration API.
    // The serializer->serialize() method expects a pointer to component data.
    // To implement this fully, ECS would need:
    //   - A way to iterate all component types an entity has
    //   - A way to get raw component data by type ID
    // For now, serialization of known component types can be done explicitly:
    (void)ecs;  // Suppress unused parameter warning
    (void)entity;
    // Component serialization would look like:
    // for (const auto& [typeId, serializer] : componentSerializers_) {
    //     if (void* data = ecs.getRawComponent(entity, typeId)) {
    //         JsonValue componentJson = JsonValue::object();
    //         componentJson["type"] = JsonValue(serializer->getTypeName());
    //         componentJson["data"] = serializer->serialize(data);
    //         components.push(componentJson);
    //     }
    // }

    json["components"] = components;

    return json;
}

Entity SceneSerializer::deserializeEntity(const JsonValue& json, ECS& ecs) {
    // Create new entity
    Entity entity = ecs.createEntity();

    // Store mapping for reference resolution
    if (json.has("id")) {
        uint64_t originalId = static_cast<uint64_t>(json["id"].asNumber());
        entityRemap_[originalId] = entity;
    }

    // Deserialize components
    if (json.has("components")) {
        const JsonValue& components = json["components"];
        for (size_t i = 0; i < components.size(); ++i) {
            const JsonValue& componentJson = components[i];

            if (!componentJson.has("type")) {
                continue;
            }

            std::string typeName = componentJson["type"].asString();

            // Find serializer by type name and deserialize component
            for (const auto& [typeId, serializer] : componentSerializers_) {
                if (serializer && std::string(serializer->getTypeName()) == typeName) {
                    // Create component storage and deserialize
                    std::vector<uint8_t> componentStorage(serializer->getComponentSize());
                    void* componentData = componentStorage.data();

                    // Deserialize the component data from JSON
                    if (componentJson.has("data")) {
                        serializer->deserialize(componentJson["data"], componentData);
                    }

                    // Note: The deserialized component would need to be added to the entity
                    // through a type-erased ECS API like: ecs.addRawComponent(entity, typeId, componentData)
                    // This requires ECS support for adding components by type ID
                    break;
                }
            }
        }
    }

    return entity;
}

} // namespace CatEngine
