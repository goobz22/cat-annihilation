#include "cat_customization.hpp"
#include "accessory_data.hpp"
#include "../../engine/core/Logger.hpp"
#include <fstream>
#include <nlohmann/json.hpp>
#include <cmath>

using json = nlohmann::json;
using Logger = CatEngine::Logger;

namespace CatGame {

// ============================================================================
// Constructor / Destructor
// ============================================================================

CatCustomizationSystem::CatCustomizationSystem() {
    // Initialize with default appearance
    m_appearance = CatAppearance();
}

CatCustomizationSystem::~CatCustomizationSystem() {
    shutdown();
}

// ============================================================================
// Initialization
// ============================================================================

void CatCustomizationSystem::initialize() {
    Logger::info("Initializing Cat Customization System");

    // Load accessory database
    loadAccessories();

    // Load preset appearances
    loadPresets();

    Logger::info("Cat Customization System initialized with {} accessories and {} presets",
                 m_accessories.size(), m_presets.size());
}

void CatCustomizationSystem::shutdown() {
    m_accessories.clear();
    m_presets.clear();
}

// ============================================================================
// Appearance Modification - Fur
// ============================================================================

void CatCustomizationSystem::setFurPattern(FurPattern pattern) {
    m_appearance.pattern = pattern;
    Logger::debug("Fur pattern set to {}", static_cast<int>(pattern));
}

void CatCustomizationSystem::setFurColors(const FurColors& colors) {
    m_appearance.colors = colors;
    m_appearance.colors.patternIntensity = clampPatternIntensity(colors.patternIntensity);
    m_appearance.colors.patternScale = clampPatternScale(colors.patternScale);
    m_appearance.colors.glossiness = clampGlossiness(colors.glossiness);
}

void CatCustomizationSystem::setPrimaryColor(const glm::vec3& color) {
    m_appearance.colors.primary = color;
}

void CatCustomizationSystem::setSecondaryColor(const glm::vec3& color) {
    m_appearance.colors.secondary = color;
}

void CatCustomizationSystem::setBellyColor(const glm::vec3& color) {
    m_appearance.colors.belly = color;
}

void CatCustomizationSystem::setAccentColor(const glm::vec3& color) {
    m_appearance.colors.accent = color;
}

void CatCustomizationSystem::setPatternIntensity(float intensity) {
    m_appearance.colors.patternIntensity = clampPatternIntensity(intensity);
}

void CatCustomizationSystem::setPatternScale(float scale) {
    m_appearance.colors.patternScale = clampPatternScale(scale);
}

void CatCustomizationSystem::setGlossiness(float glossiness) {
    m_appearance.colors.glossiness = clampGlossiness(glossiness);
}

// ============================================================================
// Appearance Modification - Eyes
// ============================================================================

void CatCustomizationSystem::setEyeColor(EyeColor color) {
    m_appearance.eyeColor = color;

    glm::vec3 rgb = getEyeColorRGB(color);

    if (color == EyeColor::Heterochromia) {
        m_appearance.leftEyeColor = glm::vec3(0.2f, 0.8f, 0.3f);  // Green
        m_appearance.rightEyeColor = glm::vec3(0.3f, 0.6f, 1.0f); // Blue
    } else {
        m_appearance.leftEyeColor = rgb;
        m_appearance.rightEyeColor = rgb;
    }
}

void CatCustomizationSystem::setCustomEyeColors(const glm::vec3& leftEye, const glm::vec3& rightEye) {
    m_appearance.eyeColor = EyeColor::Heterochromia;
    m_appearance.leftEyeColor = leftEye;
    m_appearance.rightEyeColor = rightEye;
}

void CatCustomizationSystem::setEyeGlow(float intensity) {
    m_appearance.eyeGlow = clampEyeGlow(intensity);
}

glm::vec3 CatCustomizationSystem::getEyeColorRGB(EyeColor color) const {
    switch (color) {
        case EyeColor::Yellow:  return glm::vec3(1.0f, 0.9f, 0.2f);
        case EyeColor::Green:   return glm::vec3(0.2f, 0.8f, 0.3f);
        case EyeColor::Blue:    return glm::vec3(0.3f, 0.6f, 1.0f);
        case EyeColor::Amber:   return glm::vec3(1.0f, 0.6f, 0.1f);
        case EyeColor::Copper:  return glm::vec3(0.9f, 0.4f, 0.2f);
        case EyeColor::Heterochromia:
        default:                return glm::vec3(0.2f, 0.8f, 0.3f); // Default green
    }
}

// ============================================================================
// Appearance Modification - Body Proportions
// ============================================================================

void CatCustomizationSystem::setSize(float size) {
    m_appearance.size = clampSize(size);
}

void CatCustomizationSystem::setTailLength(float length) {
    m_appearance.tailLength = clampTailLength(length);
}

void CatCustomizationSystem::setEarSize(float size) {
    m_appearance.earSize = clampEarSize(size);
}

void CatCustomizationSystem::setBodyLength(float length) {
    m_appearance.bodyLength = clampBodyLength(length);
}

void CatCustomizationSystem::setLegLength(float length) {
    m_appearance.legLength = clampLegLength(length);
}

void CatCustomizationSystem::setWhiskerLength(float length) {
    m_appearance.whiskerLength = clampWhiskerLength(length);
}

void CatCustomizationSystem::setCatName(const std::string& name) {
    m_appearance.name = name;
}

// ============================================================================
// Accessory Management
// ============================================================================

void CatCustomizationSystem::equipAccessory(const std::string& accessoryId) {
    auto it = m_accessories.find(accessoryId);
    if (it == m_accessories.end()) {
        Logger::warn("Accessory '{}' not found", accessoryId);
        return;
    }

    const CatAccessory& accessory = it->second;

    if (!accessory.isUnlocked) {
        Logger::warn("Cannot equip locked accessory '{}'", accessoryId);
        return;
    }

    // Unequip current accessory in this slot if any
    unequipAccessory(accessory.slot);

    // Equip new accessory
    m_appearance.equippedAccessories[accessory.slot] = accessoryId;
    Logger::info("Equipped accessory: {}", accessory.name);
}

void CatCustomizationSystem::unequipAccessory(AccessorySlot slot) {
    auto it = m_appearance.equippedAccessories.find(slot);
    if (it != m_appearance.equippedAccessories.end()) {
        Logger::info("Unequipped accessory from slot {}", static_cast<int>(slot));
        m_appearance.equippedAccessories.erase(it);
    }
}

void CatCustomizationSystem::unlockAccessory(const std::string& accessoryId) {
    auto it = m_accessories.find(accessoryId);
    if (it == m_accessories.end()) {
        Logger::warn("Accessory '{}' not found", accessoryId);
        return;
    }

    it->second.isUnlocked = true;
    Logger::info("Unlocked accessory: {}", it->second.name);
}

void CatCustomizationSystem::setAccessoryColor(const std::string& accessoryId, const glm::vec3& color) {
    auto it = m_accessories.find(accessoryId);
    if (it == m_accessories.end() || !it->second.allowColorCustomization) {
        return;
    }

    m_appearance.accessoryColors[accessoryId] = color;
}

std::vector<CatAccessory> CatCustomizationSystem::getUnlockedAccessories() const {
    std::vector<CatAccessory> unlocked;
    for (const auto& [id, accessory] : m_accessories) {
        if (accessory.isUnlocked) {
            unlocked.push_back(accessory);
        }
    }
    return unlocked;
}

std::vector<CatAccessory> CatCustomizationSystem::getAllAccessories() const {
    std::vector<CatAccessory> all;
    for (const auto& [id, accessory] : m_accessories) {
        all.push_back(accessory);
    }
    return all;
}

std::vector<CatAccessory> CatCustomizationSystem::getAccessoriesForSlot(AccessorySlot slot) const {
    std::vector<CatAccessory> slotAccessories;
    for (const auto& [id, accessory] : m_accessories) {
        if (accessory.slot == slot) {
            slotAccessories.push_back(accessory);
        }
    }
    return slotAccessories;
}

const CatAccessory* CatCustomizationSystem::getAccessory(const std::string& accessoryId) const {
    auto it = m_accessories.find(accessoryId);
    return (it != m_accessories.end()) ? &it->second : nullptr;
}

const CatAccessory* CatCustomizationSystem::getEquippedAccessory(AccessorySlot slot) const {
    auto it = m_appearance.equippedAccessories.find(slot);
    if (it == m_appearance.equippedAccessories.end()) {
        return nullptr;
    }
    return getAccessory(it->second);
}

bool CatCustomizationSystem::isAccessoryUnlocked(const std::string& accessoryId) const {
    auto it = m_accessories.find(accessoryId);
    return (it != m_accessories.end()) && it->second.isUnlocked;
}

bool CatCustomizationSystem::canUnlockAccessory(const std::string& accessoryId, int playerLevel) const {
    auto it = m_accessories.find(accessoryId);
    if (it == m_accessories.end()) {
        return false;
    }
    return playerLevel >= it->second.unlockLevel;
}

// ============================================================================
// Preset Management
// ============================================================================

void CatCustomizationSystem::applyPreset(const std::string& presetName) {
    auto it = m_presets.find(presetName);
    if (it == m_presets.end()) {
        Logger::warn("Preset '{}' not found", presetName);
        return;
    }

    m_appearance = it->second;
    Logger::info("Applied preset: {}", presetName);
}

void CatCustomizationSystem::saveAsPreset(const std::string& presetName) {
    m_presets[presetName] = m_appearance;
    Logger::info("Saved current appearance as preset: {}", presetName);
}

void CatCustomizationSystem::deletePreset(const std::string& presetName) {
    auto it = m_presets.find(presetName);
    if (it != m_presets.end()) {
        m_presets.erase(it);
        Logger::info("Deleted preset: {}", presetName);
    }
}

std::vector<std::string> CatCustomizationSystem::getPresets() const {
    std::vector<std::string> presetNames;
    for (const auto& [name, _] : m_presets) {
        presetNames.push_back(name);
    }
    return presetNames;
}

bool CatCustomizationSystem::hasPreset(const std::string& presetName) const {
    return m_presets.find(presetName) != m_presets.end();
}

// ============================================================================
// Rendering Integration
// ============================================================================

void CatCustomizationSystem::updateCatMaterial(CatEngine::Renderer::Material& material) const {
    // Set base color to primary fur color
    material.albedoColor = Engine::vec4(
        m_appearance.colors.primary.x,
        m_appearance.colors.primary.y,
        m_appearance.colors.primary.z,
        1.0f
    );

    // Adjust roughness based on glossiness
    material.roughness = 1.0f - m_appearance.colors.glossiness;

    // Cats are not metallic
    material.metallic = 0.0f;

    // Set material name
    material.name = "Cat_" + m_appearance.name;
}

glm::mat4 CatCustomizationSystem::getAccessoryTransform(AccessorySlot slot) const {
    auto it = m_appearance.equippedAccessories.find(slot);
    if (it == m_appearance.equippedAccessories.end()) {
        return glm::mat4(1.0f); // Identity matrix
    }

    const CatAccessory* accessory = getAccessory(it->second);
    if (!accessory) {
        return glm::mat4(1.0f);
    }

    // Start with attachment point
    glm::vec3 attachPoint = getAttachmentPoint(slot);

    // Build transform matrix
    glm::mat4 transform = glm::mat4(1.0f);

    // 1. Scale
    transform = glm::scale(transform, glm::vec3(accessory->scale));

    // 2. Rotate (convert degrees to radians)
    transform = glm::rotate(transform, glm::radians(accessory->rotationOffset.x), glm::vec3(1, 0, 0));
    transform = glm::rotate(transform, glm::radians(accessory->rotationOffset.y), glm::vec3(0, 1, 0));
    transform = glm::rotate(transform, glm::radians(accessory->rotationOffset.z), glm::vec3(0, 0, 1));

    // 3. Translate to attachment point with offset
    glm::vec3 finalPos = attachPoint + accessory->positionOffset;
    transform = glm::translate(glm::mat4(1.0f), finalPos) * transform;

    return transform;
}

CatCustomizationSystem::FurShaderParams CatCustomizationSystem::getFurShaderParams() const {
    FurShaderParams params{};

    params.primaryColor = glm::vec4(m_appearance.colors.primary, 1.0f);
    params.secondaryColor = glm::vec4(m_appearance.colors.secondary, 1.0f);
    params.bellyColor = glm::vec4(m_appearance.colors.belly, 1.0f);
    params.accentColor = glm::vec4(m_appearance.colors.accent, 1.0f);

    params.patternType = static_cast<int>(m_appearance.pattern);
    params.patternIntensity = m_appearance.colors.patternIntensity;
    params.patternScale = m_appearance.colors.patternScale;
    params.glossiness = m_appearance.colors.glossiness;

    params.leftEyeColor = glm::vec4(m_appearance.leftEyeColor, 1.0f);
    params.rightEyeColor = glm::vec4(m_appearance.rightEyeColor, 1.0f);
    params.eyeGlow = m_appearance.eyeGlow;

    return params;
}

// ============================================================================
// Serialization
// ============================================================================

void CatCustomizationSystem::saveAppearance(const std::string& filepath) {
    saveToJSON(filepath);
}

void CatCustomizationSystem::loadAppearance(const std::string& filepath) {
    loadFromJSON(filepath);
}

void CatCustomizationSystem::saveToJSON(const std::string& filepath) const {
    json j;

    j["name"] = m_appearance.name;
    j["pattern"] = static_cast<int>(m_appearance.pattern);
    j["eyeColor"] = static_cast<int>(m_appearance.eyeColor);

    // Colors
    j["colors"]["primary"] = {m_appearance.colors.primary.x, m_appearance.colors.primary.y, m_appearance.colors.primary.z};
    j["colors"]["secondary"] = {m_appearance.colors.secondary.x, m_appearance.colors.secondary.y, m_appearance.colors.secondary.z};
    j["colors"]["belly"] = {m_appearance.colors.belly.x, m_appearance.colors.belly.y, m_appearance.colors.belly.z};
    j["colors"]["accent"] = {m_appearance.colors.accent.x, m_appearance.colors.accent.y, m_appearance.colors.accent.z};
    j["colors"]["patternIntensity"] = m_appearance.colors.patternIntensity;
    j["colors"]["patternScale"] = m_appearance.colors.patternScale;
    j["colors"]["glossiness"] = m_appearance.colors.glossiness;

    // Eye colors
    j["leftEyeColor"] = {m_appearance.leftEyeColor.x, m_appearance.leftEyeColor.y, m_appearance.leftEyeColor.z};
    j["rightEyeColor"] = {m_appearance.rightEyeColor.x, m_appearance.rightEyeColor.y, m_appearance.rightEyeColor.z};
    j["eyeGlow"] = m_appearance.eyeGlow;

    // Body proportions
    j["size"] = m_appearance.size;
    j["tailLength"] = m_appearance.tailLength;
    j["earSize"] = m_appearance.earSize;
    j["bodyLength"] = m_appearance.bodyLength;
    j["legLength"] = m_appearance.legLength;
    j["whiskerLength"] = m_appearance.whiskerLength;

    // Accessories
    json accessoriesJson = json::object();
    for (const auto& [slot, accessoryId] : m_appearance.equippedAccessories) {
        accessoriesJson[std::to_string(static_cast<int>(slot))] = accessoryId;
    }
    j["equippedAccessories"] = accessoriesJson;

    // Write to file
    std::ofstream file(filepath);
    if (file.is_open()) {
        file << j.dump(4);
        file.close();
        Logger::info("Saved cat appearance to: {}", filepath);
    } else {
        Logger::error("Failed to save cat appearance to: {}", filepath);
    }
}

void CatCustomizationSystem::loadFromJSON(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        Logger::error("Failed to load cat appearance from: {}", filepath);
        return;
    }

    try {
        json j = json::parse(file);

        m_appearance.name = j.value("name", "Whiskers");
        m_appearance.pattern = static_cast<FurPattern>(j.value("pattern", 1));
        m_appearance.eyeColor = static_cast<EyeColor>(j.value("eyeColor", 1));

        // Colors
        if (j.contains("colors")) {
            auto colors = j["colors"];
            if (colors.contains("primary")) {
                auto p = colors["primary"];
                m_appearance.colors.primary = glm::vec3(p[0], p[1], p[2]);
            }
            if (colors.contains("secondary")) {
                auto s = colors["secondary"];
                m_appearance.colors.secondary = glm::vec3(s[0], s[1], s[2]);
            }
            if (colors.contains("belly")) {
                auto b = colors["belly"];
                m_appearance.colors.belly = glm::vec3(b[0], b[1], b[2]);
            }
            if (colors.contains("accent")) {
                auto a = colors["accent"];
                m_appearance.colors.accent = glm::vec3(a[0], a[1], a[2]);
            }
            m_appearance.colors.patternIntensity = colors.value("patternIntensity", 0.7f);
            m_appearance.colors.patternScale = colors.value("patternScale", 1.0f);
            m_appearance.colors.glossiness = colors.value("glossiness", 0.3f);
        }

        // Eye colors
        if (j.contains("leftEyeColor")) {
            auto l = j["leftEyeColor"];
            m_appearance.leftEyeColor = glm::vec3(l[0], l[1], l[2]);
        }
        if (j.contains("rightEyeColor")) {
            auto r = j["rightEyeColor"];
            m_appearance.rightEyeColor = glm::vec3(r[0], r[1], r[2]);
        }
        m_appearance.eyeGlow = j.value("eyeGlow", 0.0f);

        // Body proportions
        m_appearance.size = j.value("size", 1.0f);
        m_appearance.tailLength = j.value("tailLength", 1.0f);
        m_appearance.earSize = j.value("earSize", 1.0f);
        m_appearance.bodyLength = j.value("bodyLength", 1.0f);
        m_appearance.legLength = j.value("legLength", 1.0f);
        m_appearance.whiskerLength = j.value("whiskerLength", 1.0f);

        Logger::info("Loaded cat appearance from: {}", filepath);
    } catch (const std::exception& e) {
        Logger::error("Error parsing cat appearance JSON: {}", e.what());
    }

    file.close();
}

// ============================================================================
// Private Helpers
// ============================================================================

void CatCustomizationSystem::loadAccessories() {
    // Load accessories from accessory_data.hpp
    m_accessories = AccessoryData::getAllAccessories();
    Logger::info("Loaded {} accessories", m_accessories.size());
}

void CatCustomizationSystem::loadPresets() {
    // Try to load presets from file
    std::string presetPath = "assets/config/cat_presets.json";
    std::ifstream file(presetPath);

    if (!file.is_open()) {
        Logger::warn("No preset file found at {}, using defaults", presetPath);
        createDefaultPresets();
        return;
    }

    try {
        json j = json::parse(file);

        for (auto& [name, presetData] : j.items()) {
            CatAppearance preset;
            preset.name = presetData.value("name", name);
            preset.pattern = static_cast<FurPattern>(presetData.value("pattern", 0));

            if (presetData.contains("colors")) {
                auto colors = presetData["colors"];
                if (colors.contains("primary")) {
                    auto p = colors["primary"];
                    preset.colors.primary = glm::vec3(p[0], p[1], p[2]);
                }
                if (colors.contains("secondary")) {
                    auto s = colors["secondary"];
                    preset.colors.secondary = glm::vec3(s[0], s[1], s[2]);
                }
                if (colors.contains("belly")) {
                    auto b = colors["belly"];
                    preset.colors.belly = glm::vec3(b[0], b[1], b[2]);
                }
            }

            m_presets[name] = preset;
        }

        Logger::info("Loaded {} presets", m_presets.size());
    } catch (const std::exception& e) {
        Logger::error("Error parsing presets JSON: {}", e.what());
        createDefaultPresets();
    }
}

void CatCustomizationSystem::createDefaultPresets() {
    // Create some default presets
    CatAppearance orangeTabby;
    orangeTabby.name = "Orange Tabby";
    orangeTabby.pattern = FurPattern::Tabby;
    orangeTabby.colors.primary = glm::vec3(1.0f, 0.6f, 0.2f);
    orangeTabby.colors.secondary = glm::vec3(0.8f, 0.4f, 0.1f);
    orangeTabby.colors.belly = glm::vec3(1.0f, 0.95f, 0.9f);
    m_presets["Orange Tabby"] = orangeTabby;
}

glm::vec3 CatCustomizationSystem::getAttachmentPoint(AccessorySlot slot) {
    switch (slot) {
        case AccessorySlot::Head:
            return glm::vec3(0.0f, 0.5f, 0.0f);  // Top of head
        case AccessorySlot::Neck:
            return glm::vec3(0.0f, 0.3f, 0.1f);  // Neck area
        case AccessorySlot::Back:
            return glm::vec3(0.0f, 0.2f, -0.2f); // Back/shoulders
        case AccessorySlot::Tail:
            return glm::vec3(0.0f, 0.1f, -0.5f); // Tail base
        default:
            return glm::vec3(0.0f);
    }
}

} // namespace CatGame
