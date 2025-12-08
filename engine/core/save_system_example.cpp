/**
 * Save System Usage Example
 *
 * This example demonstrates how to integrate the save/load system
 * into the Cat Annihilation game.
 */

#include "save_system.hpp"
#include "settings_manager.hpp"
#include <iostream>

// Simple logging helpers
namespace {
    void logInfo(const std::string& msg) {
        std::cout << "[INFO] " << msg << std::endl;
    }

    void logError(const std::string& msg) {
        std::cerr << "[ERROR] " << msg << std::endl;
    }
}

using namespace Engine;

class GameStateManager {
public:
    SaveSystem saveSystem;
    SettingsManager settingsManager;

    void initialize() {
        // Initialize settings first
        settingsManager.initialize();
        settingsManager.applySettings();

        // Initialize save system
        saveSystem.initialize();

        // Configure auto-save
        saveSystem.enableAutoSave(true);
        saveSystem.setAutoSaveInterval(300.0f);  // 5 minutes

        // Set up callbacks
        setupCallbacks();
    }

    void setupCallbacks() {
        // Save callbacks
        saveSystem.onSaveStart = []() {
            logInfo("Game: Starting save operation...");
            // Show save icon/notification
        };

        saveSystem.onSaveComplete = [](bool success) {
            if (success) {
                logInfo("Game: Save completed successfully");
                // Show "Game Saved" notification
            } else {
                logError("Game: Save failed!");
                // Show error message to player
            }
        };

        // Load callbacks
        saveSystem.onLoadStart = []() {
            logInfo("Game: Loading save file...");
            // Show loading screen
        };

        saveSystem.onLoadComplete = [this](bool success) {
            if (success) {
                logInfo("Game: Load completed successfully");
                // Apply loaded data to game
                applySaveDataToGame();
            } else {
                logError("Game: Load failed!");
                // Show error message
            }
        };

        // Auto-save callback
        saveSystem.onAutoSave = [](float timeSinceLastSave) {
            logInfo("Game: Auto-saving (last save was " +
                        std::to_string(timeSinceLastSave) + " seconds ago)");
        };

        // Settings callbacks
        settingsManager.onGraphicsSettingsChanged = []() {
            logInfo("Game: Graphics settings changed, applying...");
            // Reload renderer settings
        };

        settingsManager.onAudioSettingsChanged = []() {
            logInfo("Game: Audio settings changed, applying...");
            // Update audio engine
        };
    }

    void update(float deltaTime) {
        // Update auto-save timer
        saveSystem.update(deltaTime);
    }

    // ========================================================================
    // Saving Game State
    // ========================================================================

    void saveCurrentGame(int slotIndex) {
        // Gather current game state
        SaveGameData data = collectGameState();

        // Set the data in save system
        saveSystem.setCurrentSaveData(data);

        // Save to slot
        saveSystem.saveGame(slotIndex);
    }

    void quickSaveGame() {
        SaveGameData data = collectGameState();
        saveSystem.setCurrentSaveData(data);
        saveSystem.quickSave();
    }

    SaveGameData collectGameState() {
        SaveGameData data;

        // Collect player stats
        data.stats.level = 5;
        data.stats.currentHealth = 85.0f;
        data.stats.maxHealth = 100.0f;
        data.stats.currentMana = 30.0f;
        data.stats.maxMana = 50.0f;
        data.stats.experience = 450;
        data.stats.experienceToNextLevel = 500;
        data.stats.strength = 15.0f;
        data.stats.agility = 12.0f;
        data.stats.vitality = 14.0f;
        data.stats.intelligence = 10.0f;

        // Collect appearance
        data.appearance.furColor = "orange";
        data.appearance.eyeColor = "green";
        data.appearance.pattern = "tabby";
        data.appearance.tailStyle = 1;
        data.appearance.earStyle = 0;
        data.appearance.size = 1.0f;

        // Collect weapon skills
        data.weaponSkills.swordLevel = 3;
        data.weaponSkills.staffLevel = 1;
        data.weaponSkills.bowLevel = 2;
        data.weaponSkills.swordXP = 250;

        // Collect position and rotation
        data.position = vec3(10.0f, 0.0f, 20.0f);
        data.rotation = Quaternion(0.0f, 0.707f, 0.0f, 0.707f);  // Facing north

        // Story progress
        data.storyState.currentChapter = 2;
        data.storyState.currentMission = 3;
        data.storyState.tutorialCompleted = true;
        data.storyState.storyFlags["met_wizard"] = true;
        data.storyState.storyFlags["defeated_boss_1"] = true;

        // Quests
        data.completedQuests.push_back("tutorial_quest");
        data.completedQuests.push_back("first_battle");
        data.activeQuests.push_back("find_ancient_temple");
        data.activeQuests.push_back("collect_herbs");
        data.questProgress["find_ancient_temple"] = 1;
        data.questProgress["collect_herbs"] = 3;

        // Inventory
        InventoryItem sword;
        sword.itemId = "iron_sword";
        sword.itemName = "Iron Sword";
        sword.itemType = "weapon";
        sword.quantity = 1;
        sword.stats["damage"] = 25.0f;
        data.inventory.push_back(sword);

        InventoryItem healthPotion;
        healthPotion.itemId = "health_potion";
        healthPotion.itemName = "Health Potion";
        healthPotion.itemType = "consumable";
        healthPotion.quantity = 5;
        healthPotion.stackSize = 10;
        healthPotion.stats["heal"] = 50.0f;
        data.inventory.push_back(healthPotion);

        // Equipped items
        data.equippedItems["weapon"] = "iron_sword";
        data.equippedItems["armor"] = "leather_armor";

        data.currency = 1250;

        // World state
        data.currentDay = 15;
        data.timeOfDay = 14.5f;  // 2:30 PM
        data.discoveredLocations.push_back("starting_village");
        data.discoveredLocations.push_back("dark_forest");
        data.discoveredLocations.push_back("mountain_pass");
        data.unlockedTerritories.push_back("northern_region");
        data.worldFlags["bridge_repaired"] = true;
        data.worldFlags["merchant_rescued"] = true;

        // Include current settings
        data.settings = settingsManager.getSettings();

        return data;
    }

    // ========================================================================
    // Loading Game State
    // ========================================================================

    void loadGameFromSlot(int slotIndex) {
        if (saveSystem.loadGame(slotIndex)) {
            // Data is loaded and validated by callbacks
            logInfo("Game: Successfully loaded save slot " + std::to_string(slotIndex));
        }
    }

    void quickLoadGame() {
        saveSystem.quickLoad();
    }

    void applySaveDataToGame() {
        const SaveGameData& data = saveSystem.getLoadedSaveData();

        // Apply player stats
        logInfo("Game: Restoring player level " + std::to_string(data.stats.level));
        logInfo("Game: Restoring health: " + std::to_string(data.stats.currentHealth) +
                    "/" + std::to_string(data.stats.maxHealth));

        // TODO: Apply to actual game entities
        // player->setStats(data.stats);
        // player->setAppearance(data.appearance);
        // player->setPosition(data.position);
        // player->setRotation(data.rotation);

        // Restore inventory
        logInfo("Game: Restoring " + std::to_string(data.inventory.size()) + " inventory items");
        // inventorySystem->setItems(data.inventory);

        // Restore world state
        logInfo("Game: Restoring world state (Day " + std::to_string(data.currentDay) + ")");
        // worldManager->setDay(data.currentDay);
        // worldManager->setTimeOfDay(data.timeOfDay);

        // Restore story progress
        logInfo("Game: Restoring story progress (Chapter " +
                    std::to_string(data.storyState.currentChapter) + ", Mission " +
                    std::to_string(data.storyState.currentMission) + ")");

        // Apply settings (optional - user may want to keep their current settings)
        // settingsManager.getSettings() = data.settings;
        // settingsManager.applySettings();
    }

    // ========================================================================
    // Save Slot Management
    // ========================================================================

    void displaySaveSlots() {
        std::vector<SaveGameHeader> slots = saveSystem.getSaveSlots();

        logInfo("=== Save Slots ===");
        for (size_t i = 0; i < slots.size(); ++i) {
            const auto& header = slots[i];

            if (header.magic == 0xCA75A4E) {
                // Valid save
                std::time_t timestamp = static_cast<std::time_t>(header.timestamp);
                char timeStr[100];
                std::strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S",
                             std::localtime(&timestamp));

                logInfo("Slot " + std::to_string(i) + ": " +
                            std::string(header.playerName) +
                            " (Level " + std::to_string(header.playerLevel) + ") - " +
                            std::string(header.location) +
                            " - Saved: " + std::string(timeStr));
            } else {
                logInfo("Slot " + std::to_string(i) + ": [Empty]");
            }
        }
    }

    void deleteSaveSlot(int slotIndex) {
        if (saveSystem.deleteSave(slotIndex)) {
            logInfo("Game: Deleted save slot " + std::to_string(slotIndex));
        } else {
            logError("Game: Failed to delete save slot " + std::to_string(slotIndex));
        }
    }

    // ========================================================================
    // Settings Management
    // ========================================================================

    void changeGraphicsSettings() {
        // Change individual settings
        settingsManager.setResolution(1920, 1080, false);
        settingsManager.setFullscreen(true, false);
        settingsManager.setVSync(true, false);

        // Or use a preset
        settingsManager.setQualityPreset(GraphicsQuality::High, false);

        // Apply all changes at once
        settingsManager.applySettings();

        // Save settings to disk
        settingsManager.saveSettings();
    }

    void changeAudioSettings() {
        settingsManager.setVolume(VolumeType::Master, 0.8f);
        settingsManager.setVolume(VolumeType::Music, 0.6f);
        settingsManager.setVolume(VolumeType::SFX, 0.9f);

        settingsManager.saveSettings();
    }

    void changeControlSettings() {
        settingsManager.setMouseSensitivity(1.5f);
        settingsManager.setKeybinding("jump", ' ');  // Space
        settingsManager.setKeybinding("attack", 0);   // Left mouse button

        settingsManager.saveSettings();
    }
};

// ============================================================================
// Example Usage
// ============================================================================

int main() {
    logInfo("=== Save System Example ===\n");

    GameStateManager gameManager;

    // Initialize systems
    gameManager.initialize();

    // Display available save slots
    gameManager.displaySaveSlots();

    // Save current game to slot 1
    logInfo("\n--- Saving Game ---");
    gameManager.saveCurrentGame(1);

    // Quick save
    logInfo("\n--- Quick Save ---");
    gameManager.quickSaveGame();

    // Load game from slot 1
    logInfo("\n--- Loading Game ---");
    gameManager.loadGameFromSlot(1);

    // Change settings
    logInfo("\n--- Changing Settings ---");
    gameManager.changeGraphicsSettings();
    gameManager.changeAudioSettings();

    // Simulate game loop with auto-save
    logInfo("\n--- Simulating Game Loop ---");
    for (int i = 0; i < 10; ++i) {
        float deltaTime = 30.0f;  // 30 seconds per iteration
        gameManager.update(deltaTime);

        // Auto-save will trigger after 5 minutes (300 seconds / 30 = 10 iterations)
    }

    logInfo("\n=== Example Complete ===");

    return 0;
}
