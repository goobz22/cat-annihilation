#pragma once

#include "../../engine/math/Vector.hpp"
#include "../../engine/math/Matrix.hpp"
#include "../../engine/renderer/Material.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>
#include <map>
#include <vector>
#include <memory>

namespace CatGame {

// ============================================================================
// Fur Pattern Types
// ============================================================================

enum class FurPattern {
    Solid,      // Single solid color
    Tabby,      // Classic tabby stripes
    Calico,     // Orange, black, and white patches
    Tuxedo,     // Black with white chest/paws
    Spotted,    // Leopard-like spots
    Striped,    // Tiger-like stripes
    Siamese,    // Cream with dark points (ears, face, paws, tail)
    Tortoiseshell, // Mixed orange and black patches
    Mackerel,   // Thin parallel stripes
    Marbled     // Swirled pattern
};

enum class EyeColor {
    Yellow,
    Green,
    Blue,
    Amber,
    Copper,
    Heterochromia  // Different colored eyes (left=green, right=blue)
};

enum class AccessorySlot {
    Head,
    Neck,
    Back,
    Tail
};

// ============================================================================
// Fur Color Configuration
// ============================================================================

struct FurColors {
    glm::vec3 primary;       // Main fur color
    glm::vec3 secondary;     // Pattern color (for tabby, calico, etc.)
    glm::vec3 belly;         // Underbelly color (often lighter)
    glm::vec3 accent;        // Additional accent color (for calico/tortoiseshell)
    float patternIntensity;  // 0-1 how prominent the pattern is
    float patternScale;      // Pattern size multiplier (0.5 - 2.0)
    float glossiness;        // Fur shininess (0.0 = matte, 1.0 = glossy)

    FurColors()
        : primary(0.8f, 0.6f, 0.4f)      // Default orange
        , secondary(0.4f, 0.3f, 0.2f)    // Brown
        , belly(0.95f, 0.9f, 0.85f)      // Light cream
        , accent(0.2f, 0.2f, 0.2f)       // Dark
        , patternIntensity(0.7f)
        , patternScale(1.0f)
        , glossiness(0.3f)
    {}

    FurColors(const glm::vec3& primary, const glm::vec3& secondary, const glm::vec3& belly)
        : primary(primary)
        , secondary(secondary)
        , belly(belly)
        , accent(0.2f)
        , patternIntensity(0.7f)
        , patternScale(1.0f)
        , glossiness(0.3f)
    {}
};

// ============================================================================
// Accessory System
// ============================================================================

struct CatAccessory {
    std::string id;
    std::string name;
    std::string description;
    AccessorySlot slot;
    std::string modelPath;          // Path to 3D model
    glm::vec3 positionOffset;       // Offset from attachment point
    glm::vec3 rotationOffset;       // Euler angles in degrees
    float scale;
    bool isUnlocked;
    std::string unlockCondition;    // "level_10", "quest_warrior", "achievement_champion", etc.
    int unlockLevel;                // Required level (0 = no level requirement)

    // Visual properties
    glm::vec3 tintColor;            // Color tint for the accessory
    bool allowColorCustomization;   // Can the player recolor it?
    float metallic;                 // PBR metallic value
    float roughness;                // PBR roughness value

    // Particle effects (optional)
    std::string particleEffect;     // "fire", "ice", "sparkles", etc.

    CatAccessory()
        : scale(1.0f)
        , isUnlocked(false)
        , unlockLevel(0)
        , tintColor(1.0f)
        , allowColorCustomization(false)
        , metallic(0.0f)
        , roughness(0.5f)
    {}
};

// ============================================================================
// Cat Appearance Configuration
// ============================================================================

struct CatAppearance {
    // Identity
    std::string name;

    // Fur properties
    FurPattern pattern;
    FurColors colors;

    // Eyes
    EyeColor eyeColor;
    glm::vec3 leftEyeColor;   // For heterochromia
    glm::vec3 rightEyeColor;  // For heterochromia
    float eyeGlow;            // 0-1 intensity for glowing eyes (cool effect!)

    // Body proportions
    float size;               // 0.8 - 1.2 overall scale
    float tailLength;         // 0.7 - 1.3 tail length multiplier
    float earSize;            // 0.8 - 1.2 ear size multiplier
    float bodyLength;         // 0.9 - 1.1 body length (chonky vs sleek)
    float legLength;          // 0.9 - 1.1 leg length
    float whiskerLength;      // 0.5 - 1.5 whisker length

    // Equipped accessories
    std::map<AccessorySlot, std::string> equippedAccessories;

    // Accessory colors (if customizable)
    std::map<std::string, glm::vec3> accessoryColors;

    CatAppearance()
        : name("Whiskers")
        , pattern(FurPattern::Tabby)
        , eyeColor(EyeColor::Green)
        , leftEyeColor(0.2f, 0.8f, 0.3f)
        , rightEyeColor(0.2f, 0.8f, 0.3f)
        , eyeGlow(0.0f)
        , size(1.0f)
        , tailLength(1.0f)
        , earSize(1.0f)
        , bodyLength(1.0f)
        , legLength(1.0f)
        , whiskerLength(1.0f)
    {}
};

// ============================================================================
// Cat Customization System
// ============================================================================

class CatCustomizationSystem {
public:
    CatCustomizationSystem();
    ~CatCustomizationSystem();

    // Initialization
    void initialize();
    void shutdown();

    // ========================================================================
    // Appearance Modification
    // ========================================================================

    void setFurPattern(FurPattern pattern);
    void setFurColors(const FurColors& colors);
    void setPrimaryColor(const glm::vec3& color);
    void setSecondaryColor(const glm::vec3& color);
    void setBellyColor(const glm::vec3& color);
    void setAccentColor(const glm::vec3& color);
    void setPatternIntensity(float intensity);
    void setPatternScale(float scale);
    void setGlossiness(float glossiness);

    void setEyeColor(EyeColor color);
    void setCustomEyeColors(const glm::vec3& leftEye, const glm::vec3& rightEye);
    void setEyeGlow(float intensity);

    void setSize(float size);
    void setTailLength(float length);
    void setEarSize(float size);
    void setBodyLength(float length);
    void setLegLength(float length);
    void setWhiskerLength(float length);

    void setCatName(const std::string& name);

    // ========================================================================
    // Accessory Management
    // ========================================================================

    void equipAccessory(const std::string& accessoryId);
    void unequipAccessory(AccessorySlot slot);
    void unlockAccessory(const std::string& accessoryId);
    void setAccessoryColor(const std::string& accessoryId, const glm::vec3& color);

    std::vector<CatAccessory> getUnlockedAccessories() const;
    std::vector<CatAccessory> getAllAccessories() const;
    std::vector<CatAccessory> getAccessoriesForSlot(AccessorySlot slot) const;
    const CatAccessory* getAccessory(const std::string& accessoryId) const;
    const CatAccessory* getEquippedAccessory(AccessorySlot slot) const;

    bool isAccessoryUnlocked(const std::string& accessoryId) const;
    bool canUnlockAccessory(const std::string& accessoryId, int playerLevel) const;

    // ========================================================================
    // Preset Management
    // ========================================================================

    void applyPreset(const std::string& presetName);
    void saveAsPreset(const std::string& presetName);
    void deletePreset(const std::string& presetName);
    std::vector<std::string> getPresets() const;
    bool hasPreset(const std::string& presetName) const;

    // ========================================================================
    // Rendering Integration
    // ========================================================================

    // Update the cat material with current appearance settings
    void updateCatMaterial(CatEngine::Renderer::Material& material) const;

    // Get transform matrix for an accessory attachment point
    glm::mat4 getAccessoryTransform(AccessorySlot slot) const;

    // Get shader parameters for fur rendering
    struct FurShaderParams {
        glm::vec4 primaryColor;
        glm::vec4 secondaryColor;
        glm::vec4 bellyColor;
        glm::vec4 accentColor;
        int patternType;        // Matches FurPattern enum
        float patternIntensity;
        float patternScale;
        float glossiness;
        glm::vec4 leftEyeColor;
        glm::vec4 rightEyeColor;
        float eyeGlow;
        float padding[1];
    };

    FurShaderParams getFurShaderParams() const;

    // ========================================================================
    // Serialization
    // ========================================================================

    void saveAppearance(const std::string& filepath);
    void loadAppearance(const std::string& filepath);

    void saveToJSON(const std::string& filepath) const;
    void loadFromJSON(const std::string& filepath);

    // ========================================================================
    // Accessors
    // ========================================================================

    const CatAppearance& getAppearance() const { return m_appearance; }
    CatAppearance& getAppearance() { return m_appearance; }

    // Get body proportions for animation/physics
    float getSize() const { return m_appearance.size; }
    float getTailLength() const { return m_appearance.tailLength; }
    float getEarSize() const { return m_appearance.earSize; }

    // ========================================================================
    // Validation
    // ========================================================================

    // Clamp values to valid ranges
    static float clampSize(float size) { return glm::clamp(size, 0.8f, 1.2f); }
    static float clampTailLength(float length) { return glm::clamp(length, 0.7f, 1.3f); }
    static float clampEarSize(float size) { return glm::clamp(size, 0.8f, 1.2f); }
    static float clampBodyLength(float length) { return glm::clamp(length, 0.9f, 1.1f); }
    static float clampLegLength(float length) { return glm::clamp(length, 0.9f, 1.1f); }
    static float clampWhiskerLength(float length) { return glm::clamp(length, 0.5f, 1.5f); }
    static float clampPatternIntensity(float intensity) { return glm::clamp(intensity, 0.0f, 1.0f); }
    static float clampPatternScale(float scale) { return glm::clamp(scale, 0.5f, 2.0f); }
    static float clampGlossiness(float glossiness) { return glm::clamp(glossiness, 0.0f, 1.0f); }
    static float clampEyeGlow(float glow) { return glm::clamp(glow, 0.0f, 1.0f); }

private:
    // Current appearance
    CatAppearance m_appearance;

    // Accessory database
    std::map<std::string, CatAccessory> m_accessories;

    // Preset database
    std::map<std::string, CatAppearance> m_presets;

    // Helper methods
    void loadAccessories();
    void loadPresets();
    void createDefaultPresets();
    glm::vec3 getEyeColorRGB(EyeColor color) const;

    // Attachment point offsets (relative to cat skeleton)
    static glm::vec3 getAttachmentPoint(AccessorySlot slot);
};

} // namespace CatGame
