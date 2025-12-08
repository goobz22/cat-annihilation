#ifndef ENGINE_CORE_SAVE_SYSTEM_HPP
#define ENGINE_CORE_SAVE_SYSTEM_HPP

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <ctime>
#include <cstdint>
#include "../math/Vector.hpp"
#include "../math/Quaternion.hpp"
#include "settings_manager.hpp"

namespace Engine {

// Forward declarations
class BinaryWriter;
class BinaryReader;

// ============================================================================
// Save Game Structures
// ============================================================================

/**
 * Cat appearance customization data
 */
struct CatAppearance {
    std::string furColor = "orange";        // Fur color name
    std::string eyeColor = "green";         // Eye color name
    std::string pattern = "tabby";          // Fur pattern type
    int tailStyle = 0;                      // Tail variation
    int earStyle = 0;                       // Ear variation
    float size = 1.0f;                      // Scale multiplier

    void serialize(BinaryWriter& writer) const;
    void deserialize(BinaryReader& reader);
};

/**
 * Cat stats and attributes
 */
struct CatStats {
    // Core stats
    float maxHealth = 100.0f;
    float currentHealth = 100.0f;
    float maxMana = 50.0f;
    float currentMana = 50.0f;

    // Attributes
    int level = 1;
    int experience = 0;
    int experienceToNextLevel = 100;

    // Combat stats
    float strength = 10.0f;         // Physical damage
    float agility = 10.0f;          // Attack speed, dodge
    float vitality = 10.0f;         // Max health
    float intelligence = 10.0f;     // Magic damage, max mana

    // Derived stats
    float defense = 5.0f;
    float magicResist = 5.0f;
    float critChance = 0.05f;       // 5%
    float critDamage = 1.5f;        // 150%

    void serialize(BinaryWriter& writer) const;
    void deserialize(BinaryReader& reader);
};

/**
 * Weapon skill progression
 */
struct WeaponSkills {
    int swordLevel = 1;
    int staffLevel = 1;
    int bowLevel = 1;

    int swordXP = 0;
    int staffXP = 0;
    int bowXP = 0;

    void serialize(BinaryWriter& writer) const;
    void deserialize(BinaryReader& reader);
};

/**
 * Story mode progression
 */
struct StoryModeState {
    int currentChapter = 1;
    int currentMission = 1;
    bool tutorialCompleted = false;

    // Story flags
    std::map<std::string, bool> storyFlags;  // e.g., "met_wizard", "defeated_boss_1"

    // Cutscenes watched
    std::vector<std::string> watchedCutscenes;

    void serialize(BinaryWriter& writer) const;
    void deserialize(BinaryReader& reader);
};

/**
 * Inventory item
 */
struct InventoryItem {
    std::string itemId;             // Unique item identifier
    std::string itemName;           // Display name
    std::string itemType;           // "weapon", "armor", "consumable", etc.
    int quantity = 1;
    int stackSize = 1;              // Max stack size

    // Item stats (for equipment)
    std::map<std::string, float> stats;  // e.g., "damage": 25, "defense": 10

    void serialize(BinaryWriter& writer) const;
    void deserialize(BinaryReader& reader);
};

/**
 * Save game header (fixed size for quick loading)
 */
struct SaveGameHeader {
    uint32_t magic = 0xCA75A4E;     // "CAT SAVE" in hex
    uint32_t version = 1;
    uint64_t timestamp;              // Unix timestamp
    uint32_t checksum;               // CRC32 of save data

    // Preview information
    char playerName[64];             // Fixed size for fast reading
    int playerLevel;
    float playTime;                  // Total seconds played
    char location[128];              // Current location name
    char screenshotPath[256];        // Path to thumbnail image

    // Data section info
    uint32_t dataOffset;             // Offset to compressed data
    uint32_t dataSize;               // Size of compressed data
    uint32_t uncompressedSize;       // Original data size

    SaveGameHeader();
    void serialize(BinaryWriter& writer) const;
    void deserialize(BinaryReader& reader);
};

/**
 * Complete save game data
 */
struct SaveGameData {
    SaveGameHeader header;

    // Player state
    CatStats stats;
    CatAppearance appearance;
    WeaponSkills weaponSkills;
    vec3 position;
    Quaternion rotation;

    // Story progress
    StoryModeState storyState;
    std::vector<std::string> completedQuests;
    std::vector<std::string> activeQuests;
    std::map<std::string, int> questProgress;  // Quest ID -> objective index

    // Inventory
    std::vector<InventoryItem> inventory;
    std::map<std::string, std::string> equippedItems;  // Slot -> Item ID
    int currency = 0;

    // World state
    int currentDay = 1;
    float timeOfDay = 12.0f;        // 0-24 hours
    std::vector<std::string> discoveredLocations;
    std::vector<std::string> unlockedTerritories;
    std::map<std::string, bool> worldFlags;  // For one-time events

    // Game settings (saved with game but can be overridden)
    GameSettings settings;

    void serialize(BinaryWriter& writer) const;
    void deserialize(BinaryReader& reader);
};

// ============================================================================
// Save System
// ============================================================================

/**
 * Save/Load system for game state persistence
 *
 * Features:
 * - Multiple save slots
 * - Auto-save functionality
 * - Quick save/load
 * - Compression and checksums
 * - Platform-specific save paths
 */
class SaveSystem {
public:
    SaveSystem();
    ~SaveSystem();

    /**
     * Initialize the save system
     * Creates save directory if it doesn't exist
     */
    void initialize();

    // ========================================================================
    // Save Operations
    // ========================================================================

    /**
     * Save current game state to a slot
     * @param slotIndex Save slot (0-9)
     * @return true if save succeeded
     */
    bool saveGame(int slotIndex);

    /**
     * Save current game state to a custom file
     * @param filename Custom save filename (without path)
     * @return true if save succeeded
     */
    bool saveGame(const std::string& filename);

    /**
     * Quick save to dedicated quick save slot
     * @return true if save succeeded
     */
    bool quickSave();

    /**
     * Auto save (called automatically)
     * @return true if save succeeded
     */
    bool autoSave();

    // ========================================================================
    // Load Operations
    // ========================================================================

    /**
     * Load game state from a slot
     * @param slotIndex Save slot (0-9)
     * @return true if load succeeded
     */
    bool loadGame(int slotIndex);

    /**
     * Load game state from a custom file
     * @param filename Custom save filename (without path)
     * @return true if load succeeded
     */
    bool loadGame(const std::string& filename);

    /**
     * Quick load from dedicated quick save slot
     * @return true if load succeeded
     */
    bool quickLoad();

    // ========================================================================
    // Save Slot Management
    // ========================================================================

    /**
     * Get information about all save slots
     * @return Vector of save headers (empty header if slot is empty)
     */
    std::vector<SaveGameHeader> getSaveSlots();

    /**
     * Delete a save slot
     * @param slotIndex Save slot to delete
     * @return true if deletion succeeded
     */
    bool deleteSave(int slotIndex);

    /**
     * Check if a save exists in a slot
     * @param slotIndex Save slot to check
     * @return true if save exists
     */
    bool doesSaveExist(int slotIndex) const;

    /**
     * Get save header for preview
     * @param slotIndex Save slot
     * @return Save header (may be invalid if slot is empty)
     */
    SaveGameHeader getSaveHeader(int slotIndex) const;

    // ========================================================================
    // Auto-Save Configuration
    // ========================================================================

    /**
     * Enable or disable auto-save
     * @param enabled Auto-save state
     */
    void enableAutoSave(bool enabled);

    /**
     * Set auto-save interval
     * @param seconds Time between auto-saves
     */
    void setAutoSaveInterval(float seconds);

    /**
     * Update auto-save timer (call each frame)
     * @param deltaTime Frame time in seconds
     */
    void update(float deltaTime);

    // ========================================================================
    // Data Access (Game must provide current state)
    // ========================================================================

    /**
     * Set the current save data (called by game before saving)
     * @param data Current game state
     */
    void setCurrentSaveData(const SaveGameData& data);

    /**
     * Get the loaded save data (called by game after loading)
     * @return Last loaded save data
     */
    const SaveGameData& getLoadedSaveData() const { return m_loadedData; }

    // ========================================================================
    // File Paths
    // ========================================================================

    /**
     * Get the platform-specific save directory path
     * @return Save directory path
     */
    std::string getSavePath() const;

    /**
     * Get the full path for a save slot
     * @param slotIndex Save slot
     * @return Full file path
     */
    std::string getSaveFilename(int slotIndex) const;

    /**
     * Get the quick save file path
     * @return Full file path
     */
    std::string getQuickSaveFilename() const;

    /**
     * Get the auto save file path
     * @return Full file path
     */
    std::string getAutoSaveFilename() const;

    // ========================================================================
    // Events/Callbacks
    // ========================================================================

    std::function<void()> onSaveStart;
    std::function<void(bool)> onSaveComplete;  // Parameter: success
    std::function<void()> onLoadStart;
    std::function<void(bool)> onLoadComplete;  // Parameter: success
    std::function<void(float)> onAutoSave;     // Parameter: time since last save

private:
    // Internal save/load implementation
    bool saveToFile(const std::string& fullPath, const SaveGameData& data);
    bool loadFromFile(const std::string& fullPath, SaveGameData& data);
    SaveGameHeader readHeader(const std::string& fullPath) const;

    // Platform-specific path resolution
    std::string getPlatformSavePath() const;
    void createSaveDirectory();

    // Compression helpers
    bool compressAndWrite(const SaveGameData& data, const std::string& path);
    bool readAndDecompress(const std::string& path, SaveGameData& data);

    // Data
    SaveGameData m_currentData;
    SaveGameData m_loadedData;

    // Auto-save state
    bool m_autoSaveEnabled;
    float m_autoSaveInterval;
    float m_autoSaveTimer;

    // Paths
    std::string m_savePath;

    // Constants
    static constexpr int MAX_SAVE_SLOTS = 10;
    static constexpr const char* SAVE_FILE_EXTENSION = ".catsave";
    static constexpr const char* QUICK_SAVE_NAME = "quicksave";
    static constexpr const char* AUTO_SAVE_NAME = "autosave";
};

} // namespace Engine

#endif // ENGINE_CORE_SAVE_SYSTEM_HPP
