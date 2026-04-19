#include "SceneSerializer.hpp"

// V1 serializer hard-codes the known gameplay component set. Scanning the ECS
// type-erased would require a component-reflection API we don't have yet;
// explicit registration per shipped component type keeps serialization
// lossless and the schema reviewable.
#include "../../game/components/GameComponents.hpp"
#include "../../game/components/EnemyComponent.hpp"
#include "../../game/components/ProjectileComponent.hpp"

#include <fstream>
#include <sstream>
#include <iomanip>

namespace CatEngine {

// ============================================================================
// Component Serialization Primitives
// ============================================================================

namespace {

JsonValue writeVec3(const Engine::vec3& vector) {
    JsonValue array = JsonValue::array();
    array.push(JsonValue(vector.x));
    array.push(JsonValue(vector.y));
    array.push(JsonValue(vector.z));
    return array;
}

Engine::vec3 readVec3(const JsonValue& json) {
    Engine::vec3 result(0.0f, 0.0f, 0.0f);
    if (json.size() >= 3) {
        result.x = static_cast<float>(json[0].asNumber());
        result.y = static_cast<float>(json[1].asNumber());
        result.z = static_cast<float>(json[2].asNumber());
    }
    return result;
}

JsonValue writeHealth(const CatGame::HealthComponent& health) {
    JsonValue json = JsonValue::object();
    json["currentHealth"] = JsonValue(health.currentHealth);
    json["maxHealth"] = JsonValue(health.maxHealth);
    json["shield"] = JsonValue(health.shield);
    json["maxShield"] = JsonValue(health.maxShield);
    json["invincibilityTimer"] = JsonValue(health.invincibilityTimer);
    json["invincibilityDuration"] = JsonValue(health.invincibilityDuration);
    json["canRegenerate"] = JsonValue(health.canRegenerate);
    json["regenerationRate"] = JsonValue(health.regenerationRate);
    json["regenerationDelay"] = JsonValue(health.regenerationDelay);
    json["timeSinceLastDamage"] = JsonValue(health.timeSinceLastDamage);
    json["isDead"] = JsonValue(health.isDead);
    json["deathTimer"] = JsonValue(health.deathTimer);
    json["deathAnimationDuration"] = JsonValue(health.deathAnimationDuration);
    return json;
}

CatGame::HealthComponent readHealth(const JsonValue& json) {
    CatGame::HealthComponent health;
    if (json.has("currentHealth")) health.currentHealth = static_cast<float>(json["currentHealth"].asNumber());
    if (json.has("maxHealth")) health.maxHealth = static_cast<float>(json["maxHealth"].asNumber());
    if (json.has("shield")) health.shield = static_cast<float>(json["shield"].asNumber());
    if (json.has("maxShield")) health.maxShield = static_cast<float>(json["maxShield"].asNumber());
    if (json.has("invincibilityTimer")) health.invincibilityTimer = static_cast<float>(json["invincibilityTimer"].asNumber());
    if (json.has("invincibilityDuration")) health.invincibilityDuration = static_cast<float>(json["invincibilityDuration"].asNumber());
    if (json.has("canRegenerate")) health.canRegenerate = json["canRegenerate"].asBool();
    if (json.has("regenerationRate")) health.regenerationRate = static_cast<float>(json["regenerationRate"].asNumber());
    if (json.has("regenerationDelay")) health.regenerationDelay = static_cast<float>(json["regenerationDelay"].asNumber());
    if (json.has("timeSinceLastDamage")) health.timeSinceLastDamage = static_cast<float>(json["timeSinceLastDamage"].asNumber());
    if (json.has("isDead")) health.isDead = json["isDead"].asBool();
    if (json.has("deathTimer")) health.deathTimer = static_cast<float>(json["deathTimer"].asNumber());
    if (json.has("deathAnimationDuration")) health.deathAnimationDuration = static_cast<float>(json["deathAnimationDuration"].asNumber());
    // onDamage / onDeath callbacks are intentionally not restored; gameplay
    // systems rewire them when entities are hydrated into a running scene.
    return health;
}

JsonValue writeMovement(const CatGame::MovementComponent& movement) {
    JsonValue json = JsonValue::object();
    json["moveSpeed"] = JsonValue(movement.moveSpeed);
    json["maxSpeed"] = JsonValue(movement.maxSpeed);
    json["acceleration"] = JsonValue(movement.acceleration);
    json["deceleration"] = JsonValue(movement.deceleration);
    json["velocity"] = writeVec3(movement.velocity);
    json["jumpForce"] = JsonValue(movement.jumpForce);
    json["gravityMultiplier"] = JsonValue(movement.gravityMultiplier);
    json["isGrounded"] = JsonValue(movement.isGrounded);
    json["speedModifier"] = JsonValue(movement.speedModifier);
    json["canMove"] = JsonValue(movement.canMove);
    json["canJump"] = JsonValue(movement.canJump);
    return json;
}

CatGame::MovementComponent readMovement(const JsonValue& json) {
    CatGame::MovementComponent movement;
    if (json.has("moveSpeed")) movement.moveSpeed = static_cast<float>(json["moveSpeed"].asNumber());
    if (json.has("maxSpeed")) movement.maxSpeed = static_cast<float>(json["maxSpeed"].asNumber());
    if (json.has("acceleration")) movement.acceleration = static_cast<float>(json["acceleration"].asNumber());
    if (json.has("deceleration")) movement.deceleration = static_cast<float>(json["deceleration"].asNumber());
    if (json.has("velocity")) movement.velocity = readVec3(json["velocity"]);
    if (json.has("jumpForce")) movement.jumpForce = static_cast<float>(json["jumpForce"].asNumber());
    if (json.has("gravityMultiplier")) movement.gravityMultiplier = static_cast<float>(json["gravityMultiplier"].asNumber());
    if (json.has("isGrounded")) movement.isGrounded = json["isGrounded"].asBool();
    if (json.has("speedModifier")) movement.speedModifier = static_cast<float>(json["speedModifier"].asNumber());
    if (json.has("canMove")) movement.canMove = json["canMove"].asBool();
    if (json.has("canJump")) movement.canJump = json["canJump"].asBool();
    return movement;
}

JsonValue writeCombat(const CatGame::CombatComponent& combat) {
    JsonValue json = JsonValue::object();
    json["attackDamage"] = JsonValue(combat.attackDamage);
    json["attackSpeed"] = JsonValue(combat.attackSpeed);
    json["attackRange"] = JsonValue(combat.attackRange);
    json["attackCooldown"] = JsonValue(combat.attackCooldown);
    json["equippedWeapon"] = JsonValue(static_cast<int>(combat.equippedWeapon));
    json["damageMultiplier"] = JsonValue(combat.damageMultiplier);
    json["attackSpeedMultiplier"] = JsonValue(combat.attackSpeedMultiplier);
    json["canAttack"] = JsonValue(combat.canAttack);
    return json;
}

CatGame::CombatComponent readCombat(const JsonValue& json) {
    CatGame::CombatComponent combat;
    if (json.has("attackDamage")) combat.attackDamage = static_cast<float>(json["attackDamage"].asNumber());
    if (json.has("attackSpeed")) combat.attackSpeed = static_cast<float>(json["attackSpeed"].asNumber());
    if (json.has("attackRange")) combat.attackRange = static_cast<float>(json["attackRange"].asNumber());
    if (json.has("attackCooldown")) combat.attackCooldown = static_cast<float>(json["attackCooldown"].asNumber());
    if (json.has("equippedWeapon")) combat.equippedWeapon = static_cast<CatGame::WeaponType>(static_cast<int>(json["equippedWeapon"].asNumber()));
    if (json.has("damageMultiplier")) combat.damageMultiplier = static_cast<float>(json["damageMultiplier"].asNumber());
    if (json.has("attackSpeedMultiplier")) combat.attackSpeedMultiplier = static_cast<float>(json["attackSpeedMultiplier"].asNumber());
    if (json.has("canAttack")) combat.canAttack = json["canAttack"].asBool();
    return combat;
}

JsonValue writeEnemy(const CatGame::EnemyComponent& enemy) {
    JsonValue json = JsonValue::object();
    json["type"] = JsonValue(static_cast<int>(enemy.type));
    json["state"] = JsonValue(static_cast<int>(enemy.state));
    // Target entity is stored by id; resolution happens post-load via
    // entityRemap_ if the referenced entity lives in the same scene file.
    json["targetId"] = JsonValue(static_cast<double>(enemy.target.id));
    json["aggroRange"] = JsonValue(enemy.aggroRange);
    json["attackRange"] = JsonValue(enemy.attackRange);
    json["attackDamage"] = JsonValue(enemy.attackDamage);
    json["attackCooldown"] = JsonValue(enemy.attackCooldown);
    json["attackCooldownTimer"] = JsonValue(enemy.attackCooldownTimer);
    json["stateTimer"] = JsonValue(enemy.stateTimer);
    json["idleWaitTime"] = JsonValue(enemy.idleWaitTime);
    json["moveSpeed"] = JsonValue(enemy.moveSpeed);
    json["scoreValue"] = JsonValue(enemy.scoreValue);
    return json;
}

CatGame::EnemyComponent readEnemy(const JsonValue& json) {
    CatGame::EnemyComponent enemy;
    if (json.has("type")) enemy.type = static_cast<CatGame::EnemyType>(static_cast<int>(json["type"].asNumber()));
    if (json.has("state")) enemy.state = static_cast<CatGame::AIState>(static_cast<int>(json["state"].asNumber()));
    if (json.has("targetId")) enemy.target = Entity(static_cast<uint64_t>(json["targetId"].asNumber()));
    if (json.has("aggroRange")) enemy.aggroRange = static_cast<float>(json["aggroRange"].asNumber());
    if (json.has("attackRange")) enemy.attackRange = static_cast<float>(json["attackRange"].asNumber());
    if (json.has("attackDamage")) enemy.attackDamage = static_cast<float>(json["attackDamage"].asNumber());
    if (json.has("attackCooldown")) enemy.attackCooldown = static_cast<float>(json["attackCooldown"].asNumber());
    if (json.has("attackCooldownTimer")) enemy.attackCooldownTimer = static_cast<float>(json["attackCooldownTimer"].asNumber());
    if (json.has("stateTimer")) enemy.stateTimer = static_cast<float>(json["stateTimer"].asNumber());
    if (json.has("idleWaitTime")) enemy.idleWaitTime = static_cast<float>(json["idleWaitTime"].asNumber());
    if (json.has("moveSpeed")) enemy.moveSpeed = static_cast<float>(json["moveSpeed"].asNumber());
    if (json.has("scoreValue")) enemy.scoreValue = static_cast<int>(json["scoreValue"].asNumber());
    return enemy;
}

JsonValue writeProjectile(const CatGame::ProjectileComponent& projectile) {
    JsonValue json = JsonValue::object();
    json["type"] = JsonValue(static_cast<int>(projectile.type));
    json["velocity"] = writeVec3(projectile.velocity);
    json["damage"] = JsonValue(projectile.damage);
    json["lifetime"] = JsonValue(projectile.lifetime);
    json["lifetimeRemaining"] = JsonValue(projectile.lifetimeRemaining);
    json["ownerId"] = JsonValue(static_cast<double>(projectile.owner.id));
    json["hasHit"] = JsonValue(projectile.hasHit);
    json["radius"] = JsonValue(projectile.radius);
    json["isHoming"] = JsonValue(projectile.isHoming);
    json["homingTargetId"] = JsonValue(static_cast<double>(projectile.homingTarget.id));
    json["homingStrength"] = JsonValue(projectile.homingStrength);
    return json;
}

CatGame::ProjectileComponent readProjectile(const JsonValue& json) {
    CatGame::ProjectileComponent projectile;
    if (json.has("type")) projectile.type = static_cast<CatGame::ProjectileType>(static_cast<int>(json["type"].asNumber()));
    if (json.has("velocity")) projectile.velocity = readVec3(json["velocity"]);
    if (json.has("damage")) projectile.damage = static_cast<float>(json["damage"].asNumber());
    if (json.has("lifetime")) projectile.lifetime = static_cast<float>(json["lifetime"].asNumber());
    if (json.has("lifetimeRemaining")) projectile.lifetimeRemaining = static_cast<float>(json["lifetimeRemaining"].asNumber());
    if (json.has("ownerId")) projectile.owner = Entity(static_cast<uint64_t>(json["ownerId"].asNumber()));
    if (json.has("hasHit")) projectile.hasHit = json["hasHit"].asBool();
    if (json.has("radius")) projectile.radius = static_cast<float>(json["radius"].asNumber());
    if (json.has("isHoming")) projectile.isHoming = json["isHoming"].asBool();
    if (json.has("homingTargetId")) projectile.homingTarget = Entity(static_cast<uint64_t>(json["homingTargetId"].asNumber()));
    if (json.has("homingStrength")) projectile.homingStrength = static_cast<float>(json["homingStrength"].asNumber());
    return projectile;
}

// Tag strings used in the "type" field of each serialized component. Keep
// these stable across versions — changing them breaks scene files on disk.
constexpr const char* kTagHealth     = "Health";
constexpr const char* kTagMovement   = "Movement";
constexpr const char* kTagCombat     = "Combat";
constexpr const char* kTagEnemy      = "Enemy";
constexpr const char* kTagProjectile = "Projectile";

JsonValue makeComponentEnvelope(const char* tag, const JsonValue& data) {
    JsonValue envelope = JsonValue::object();
    envelope["type"] = JsonValue(tag);
    envelope["data"] = data;
    return envelope;
}

} // namespace

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

    // Entity id is the packed generational index; deserialize remaps it into
    // the fresh ECS via entityRemap_ so cross-component Entity references
    // (e.g. EnemyComponent::target) can be patched after load.
    json["id"] = JsonValue(static_cast<double>(entity.id));

    JsonValue components = JsonValue::array();

    if (const auto* health = ecs.getComponent<CatGame::HealthComponent>(entity)) {
        components.push(makeComponentEnvelope(kTagHealth, writeHealth(*health)));
    }
    if (const auto* movement = ecs.getComponent<CatGame::MovementComponent>(entity)) {
        components.push(makeComponentEnvelope(kTagMovement, writeMovement(*movement)));
    }
    if (const auto* combat = ecs.getComponent<CatGame::CombatComponent>(entity)) {
        components.push(makeComponentEnvelope(kTagCombat, writeCombat(*combat)));
    }
    if (const auto* enemy = ecs.getComponent<CatGame::EnemyComponent>(entity)) {
        components.push(makeComponentEnvelope(kTagEnemy, writeEnemy(*enemy)));
    }
    if (const auto* projectile = ecs.getComponent<CatGame::ProjectileComponent>(entity)) {
        components.push(makeComponentEnvelope(kTagProjectile, writeProjectile(*projectile)));
    }

    json["components"] = components;

    return json;
}

Entity SceneSerializer::deserializeEntity(const JsonValue& json, ECS& ecs) {
    Entity entity = ecs.createEntity();

    if (json.has("id")) {
        uint64_t originalId = static_cast<uint64_t>(json["id"].asNumber());
        entityRemap_[originalId] = entity;
    }

    if (json.has("components")) {
        const JsonValue& components = json["components"];
        for (size_t i = 0; i < components.size(); ++i) {
            const JsonValue& componentJson = components[i];

            if (!componentJson.has("type") || !componentJson.has("data")) {
                continue;
            }

            const std::string& typeName = componentJson["type"].asString();
            const JsonValue& data = componentJson["data"];

            // Tag dispatch mirrors serializeEntity. New component types must be
            // added in both places — there is no registration shortcut until
            // the ECS gains a type-erased insert path.
            if (typeName == kTagHealth) {
                ecs.addComponent<CatGame::HealthComponent>(entity, readHealth(data));
            } else if (typeName == kTagMovement) {
                ecs.addComponent<CatGame::MovementComponent>(entity, readMovement(data));
            } else if (typeName == kTagCombat) {
                ecs.addComponent<CatGame::CombatComponent>(entity, readCombat(data));
            } else if (typeName == kTagEnemy) {
                ecs.addComponent<CatGame::EnemyComponent>(entity, readEnemy(data));
            } else if (typeName == kTagProjectile) {
                ecs.addComponent<CatGame::ProjectileComponent>(entity, readProjectile(data));
            }
        }
    }

    return entity;
}

} // namespace CatEngine
