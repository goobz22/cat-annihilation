// Cat Customization System Integration Example
// This file demonstrates how to integrate the cat customization system into your game

#include "cat_customization.hpp"
#include "../../engine/renderer/Renderer.hpp"
#include "../../engine/renderer/Material.hpp"
#include "../../engine/assets/ModelLoader.hpp"

namespace CatGame {

/**
 * Example: Setting up cat customization in your game
 */
class CatGameExample {
private:
    CatCustomizationSystem m_customization;
    CatEngine::Renderer::Material m_catMaterial;

public:
    void initialize() {
        // Initialize the customization system
        m_customization.initialize();

        // Apply a preset or load saved appearance
        if (/* player has saved cat */) {
            m_customization.loadAppearance("saves/player_cat.json");
        } else {
            // First time player - use a default preset
            m_customization.applyPreset("Orange Tabby");
        }

        // Setup material
        m_catMaterial = CatEngine::Renderer::Material::CreateDefault();
        m_customization.updateCatMaterial(m_catMaterial);
    }

    /**
     * Render the cat with customization applied
     */
    void render(CatEngine::Renderer::Renderer& renderer) {
        // 1. Update material with current customization
        m_customization.updateCatMaterial(m_catMaterial);

        // 2. Get fur shader parameters
        auto furParams = m_customization.getFurShaderParams();

        // 3. Upload to GPU (bind to set 1, binding 6 as per shader)
        // renderer.updateUniformBuffer(furParamsUBO, &furParams, sizeof(furParams));

        // 4. Render cat model with fur shader
        // renderer.drawMesh(catMesh, m_catMaterial, furShaderProgram);

        // 5. Render equipped accessories
        const auto& appearance = m_customization.getAppearance();
        for (const auto& [slot, accessoryId] : appearance.equippedAccessories) {
            const CatAccessory* accessory = m_customization.getAccessory(accessoryId);
            if (accessory) {
                // Get accessory transform
                glm::mat4 transform = m_customization.getAccessoryTransform(slot);

                // Load and render accessory model
                // auto model = loadModel(accessory->modelPath);
                // renderer.drawMesh(model, transform);

                // If accessory has particle effect, spawn particles
                if (!accessory->particleEffect.empty()) {
                    // spawnParticleEffect(accessory->particleEffect, transform);
                }
            }
        }
    }

    /**
     * Example: Customization UI callbacks
     */
    void onPlayerCustomizeCat() {
        // Pattern selection
        if (/* UI: player selected Tabby */) {
            m_customization.setFurPattern(FurPattern::Tabby);
        }

        // Color pickers
        if (/* UI: primary color changed */) {
            glm::vec3 color = /* get color from UI */;
            m_customization.setPrimaryColor(color);
        }

        // Sliders
        if (/* UI: size slider changed */) {
            float size = /* get value from slider (0.8 - 1.2) */;
            m_customization.setSize(size);
        }

        // Accessory selection
        if (/* UI: player clicked on accessory */) {
            std::string accessoryId = /* get selected accessory ID */;

            // Check if unlocked
            if (m_customization.isAccessoryUnlocked(accessoryId)) {
                m_customization.equipAccessory(accessoryId);
            } else {
                // Show "locked" message with unlock requirements
                const CatAccessory* acc = m_customization.getAccessory(accessoryId);
                // displayMessage("Unlock at level " + std::to_string(acc->unlockLevel));
            }
        }
    }

    /**
     * Example: Unlock accessories based on player progress
     */
    void onPlayerLevelUp(int newLevel) {
        // Auto-unlock accessories that are now available
        auto allAccessories = m_customization.getAllAccessories();

        for (const auto& accessory : allAccessories) {
            if (!accessory.isUnlocked && accessory.unlockLevel <= newLevel) {
                m_customization.unlockAccessory(accessory.id);

                // Show notification
                // showNotification("New accessory unlocked: " + accessory.name);
            }
        }
    }

    /**
     * Example: Unlock accessories from quests
     */
    void onQuestComplete(const std::string& questId) {
        // Map quest IDs to unlock conditions
        std::map<std::string, std::string> questUnlocks = {
            {"hero_journey", "medallion_hero"},
            {"explorer_path", "backpack_adventurer"},
            // ... more quest unlocks
        };

        auto it = questUnlocks.find(questId);
        if (it != questUnlocks.end()) {
            m_customization.unlockAccessory(it->second);
            // showNotification("Quest reward: " + accessoryName);
        }
    }

    /**
     * Example: Save cat when player exits
     */
    void onGameExit() {
        m_customization.saveAppearance("saves/player_cat.json");
    }

    /**
     * Example: Quick preset switching
     */
    void onPresetSelected(const std::string& presetName) {
        m_customization.applyPreset(presetName);

        // Update material immediately
        m_customization.updateCatMaterial(m_catMaterial);
    }

    /**
     * Example: Create custom preset
     */
    void onSaveCustomPreset(const std::string& presetName) {
        m_customization.saveAsPreset(presetName);

        // Show confirmation
        // showNotification("Saved preset: " + presetName);
    }

    /**
     * Example: Clan system integration
     */
    void onJoinClan(const std::string& clanName) {
        // Unlock clan-specific collar
        if (clanName == "MistClan") {
            m_customization.unlockAccessory("collar_mistclan");
        } else if (clanName == "StormClan") {
            m_customization.unlockAccessory("collar_stormclan");
        } else if (clanName == "EmberClan") {
            m_customization.unlockAccessory("collar_emberclan");
        } else if (clanName == "FrostClan") {
            m_customization.unlockAccessory("collar_frostclan");
        }
    }

    /**
     * Example: Elemental mastery unlocks
     */
    void onElementalMasteryAchieved(const std::string& element) {
        if (element == "fire") {
            m_customization.unlockAccessory("effect_flame");
        } else if (element == "ice") {
            m_customization.unlockAccessory("effect_ice");
        } else if (element == "lightning") {
            m_customization.unlockAccessory("effect_lightning");
        }
    }

    /**
     * Example: Random cat generator
     */
    void generateRandomCat() {
        // Random pattern
        int patternIndex = rand() % 10;
        m_customization.setFurPattern(static_cast<FurPattern>(patternIndex));

        // Random colors
        auto randomColor = []() -> glm::vec3 {
            return glm::vec3(
                (rand() % 100) / 100.0f,
                (rand() % 100) / 100.0f,
                (rand() % 100) / 100.0f
            );
        };

        m_customization.setPrimaryColor(randomColor());
        m_customization.setSecondaryColor(randomColor());
        m_customization.setBellyColor(randomColor());

        // Random eye color
        int eyeColorIndex = rand() % 6;
        m_customization.setEyeColor(static_cast<EyeColor>(eyeColorIndex));

        // Random proportions
        m_customization.setSize(0.8f + (rand() % 40) / 100.0f);        // 0.8 - 1.2
        m_customization.setTailLength(0.7f + (rand() % 60) / 100.0f);  // 0.7 - 1.3
        m_customization.setEarSize(0.8f + (rand() % 40) / 100.0f);     // 0.8 - 1.2

        // Random name
        std::vector<std::string> names = {
            "Whiskers", "Shadow", "Luna", "Tiger", "Mittens",
            "Simba", "Felix", "Oreo", "Smokey", "Ginger"
        };
        m_customization.setCatName(names[rand() % names.size()]);
    }

    /**
     * Get customization system for UI access
     */
    CatCustomizationSystem& getCustomization() {
        return m_customization;
    }
};

/**
 * Example: UI helper functions
 */
class CatCustomizationUI {
public:
    /**
     * Build accessory selection UI
     */
    static void buildAccessoryUI(CatCustomizationSystem& customization, AccessorySlot slot) {
        // Get all accessories for this slot
        auto accessories = customization.getAccessoriesForSlot(slot);

        for (const auto& accessory : accessories) {
            bool isUnlocked = accessory.isUnlocked;
            bool isEquipped = customization.getEquippedAccessory(slot) &&
                            customization.getEquippedAccessory(slot)->id == accessory.id;

            // Render UI element
            // if (isUnlocked) {
            //     if (renderAccessoryButton(accessory.name, isEquipped)) {
            //         if (isEquipped) {
            //             customization.unequipAccessory(slot);
            //         } else {
            //             customization.equipAccessory(accessory.id);
            //         }
            //     }
            // } else {
            //     renderLockedAccessoryButton(accessory.name, accessory.unlockCondition);
            // }
        }
    }

    /**
     * Build preset selection UI
     */
    static void buildPresetUI(CatCustomizationSystem& customization) {
        auto presets = customization.getPresets();

        for (const auto& presetName : presets) {
            // if (renderPresetButton(presetName)) {
            //     customization.applyPreset(presetName);
            // }
        }
    }

    /**
     * Build color picker UI
     */
    static void buildColorPickerUI(CatCustomizationSystem& customization) {
        // Primary color
        // auto primaryColor = customization.getAppearance().colors.primary;
        // if (renderColorPicker("Primary Color", primaryColor)) {
        //     customization.setPrimaryColor(primaryColor);
        // }

        // Secondary color
        // auto secondaryColor = customization.getAppearance().colors.secondary;
        // if (renderColorPicker("Secondary Color", secondaryColor)) {
        //     customization.setSecondaryColor(secondaryColor);
        // }

        // Belly color
        // auto bellyColor = customization.getAppearance().colors.belly;
        // if (renderColorPicker("Belly Color", bellyColor)) {
        //     customization.setBellyColor(bellyColor);
        // }
    }

    /**
     * Build pattern selection UI
     */
    static void buildPatternUI(CatCustomizationSystem& customization) {
        const char* patternNames[] = {
            "Solid", "Tabby", "Calico", "Tuxedo", "Spotted",
            "Striped", "Siamese", "Tortoiseshell", "Mackerel", "Marbled"
        };

        int currentPattern = static_cast<int>(customization.getAppearance().pattern);

        // if (renderDropdown("Fur Pattern", patternNames, 10, currentPattern)) {
        //     customization.setFurPattern(static_cast<FurPattern>(currentPattern));
        // }

        // Pattern controls
        // float intensity = customization.getAppearance().colors.patternIntensity;
        // if (renderSlider("Pattern Intensity", 0.0f, 1.0f, intensity)) {
        //     customization.setPatternIntensity(intensity);
        // }

        // float scale = customization.getAppearance().colors.patternScale;
        // if (renderSlider("Pattern Scale", 0.5f, 2.0f, scale)) {
        //     customization.setPatternScale(scale);
        // }
    }
};

} // namespace CatGame
