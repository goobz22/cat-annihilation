#include "save_system.hpp"
#include "serialization.hpp"
#include <filesystem>
#include <ctime>
#include <cstring>
#include <iostream>

// Simple logging helpers
namespace {
    void logInfo(const std::string& msg) {
        std::cout << "[INFO] " << msg << std::endl;
    }

    void logWarn(const std::string& msg) {
        std::cout << "[WARN] " << msg << std::endl;
    }

    void logError(const std::string& msg) {
        std::cerr << "[ERROR] " << msg << std::endl;
    }
}

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#else
#include <unistd.h>
#include <pwd.h>
#endif

namespace Engine {

// ============================================================================
// CatAppearance Implementation
// ============================================================================

void CatAppearance::serialize(BinaryWriter& writer) const {
    writer.writeString(furColor);
    writer.writeString(eyeColor);
    writer.writeString(pattern);
    writer.write(tailStyle);
    writer.write(earStyle);
    writer.write(size);
}

void CatAppearance::deserialize(BinaryReader& reader) {
    furColor = reader.readString();
    eyeColor = reader.readString();
    pattern = reader.readString();
    tailStyle = reader.read<int>();
    earStyle = reader.read<int>();
    size = reader.read<float>();
}

// ============================================================================
// CatStats Implementation
// ============================================================================

void CatStats::serialize(BinaryWriter& writer) const {
    writer.write(maxHealth);
    writer.write(currentHealth);
    writer.write(maxMana);
    writer.write(currentMana);
    writer.write(level);
    writer.write(experience);
    writer.write(experienceToNextLevel);
    writer.write(strength);
    writer.write(agility);
    writer.write(vitality);
    writer.write(intelligence);
    writer.write(defense);
    writer.write(magicResist);
    writer.write(critChance);
    writer.write(critDamage);
}

void CatStats::deserialize(BinaryReader& reader) {
    maxHealth = reader.read<float>();
    currentHealth = reader.read<float>();
    maxMana = reader.read<float>();
    currentMana = reader.read<float>();
    level = reader.read<int>();
    experience = reader.read<int>();
    experienceToNextLevel = reader.read<int>();
    strength = reader.read<float>();
    agility = reader.read<float>();
    vitality = reader.read<float>();
    intelligence = reader.read<float>();
    defense = reader.read<float>();
    magicResist = reader.read<float>();
    critChance = reader.read<float>();
    critDamage = reader.read<float>();
}

// ============================================================================
// WeaponSkills Implementation
// ============================================================================

void WeaponSkills::serialize(BinaryWriter& writer) const {
    writer.write(swordLevel);
    writer.write(staffLevel);
    writer.write(bowLevel);
    writer.write(swordXP);
    writer.write(staffXP);
    writer.write(bowXP);
}

void WeaponSkills::deserialize(BinaryReader& reader) {
    swordLevel = reader.read<int>();
    staffLevel = reader.read<int>();
    bowLevel = reader.read<int>();
    swordXP = reader.read<int>();
    staffXP = reader.read<int>();
    bowXP = reader.read<int>();
}

// ============================================================================
// StoryModeState Implementation
// ============================================================================

void StoryModeState::serialize(BinaryWriter& writer) const {
    writer.write(currentChapter);
    writer.write(currentMission);
    writer.write(tutorialCompleted);
    writer.writeMap(storyFlags);
    writer.writeVector(watchedCutscenes);
}

void StoryModeState::deserialize(BinaryReader& reader) {
    currentChapter = reader.read<int>();
    currentMission = reader.read<int>();
    tutorialCompleted = reader.read<bool>();
    storyFlags = reader.readMap<std::string, bool>();
    watchedCutscenes = reader.readVector<std::string>();
}

// ============================================================================
// InventoryItem Implementation
// ============================================================================

void InventoryItem::serialize(BinaryWriter& writer) const {
    writer.writeString(itemId);
    writer.writeString(itemName);
    writer.writeString(itemType);
    writer.write(quantity);
    writer.write(stackSize);
    writer.writeMap(stats);
}

void InventoryItem::deserialize(BinaryReader& reader) {
    itemId = reader.readString();
    itemName = reader.readString();
    itemType = reader.readString();
    quantity = reader.read<int>();
    stackSize = reader.read<int>();
    stats = reader.readMap<std::string, float>();
}

// ============================================================================
// SaveGameHeader Implementation
// ============================================================================

SaveGameHeader::SaveGameHeader() {
    std::memset(playerName, 0, sizeof(playerName));
    std::memset(location, 0, sizeof(location));
    std::memset(screenshotPath, 0, sizeof(screenshotPath));
    timestamp = std::time(nullptr);
    playerLevel = 1;
    playTime = 0.0f;
    checksum = 0;
    dataOffset = 0;
    dataSize = 0;
    uncompressedSize = 0;
}

void SaveGameHeader::serialize(BinaryWriter& writer) const {
    writer.write(magic);
    writer.write(version);
    writer.write(timestamp);
    writer.write(checksum);
    writer.write(playerName, sizeof(playerName));
    writer.write(playerLevel);
    writer.write(playTime);
    writer.write(location, sizeof(location));
    writer.write(screenshotPath, sizeof(screenshotPath));
    writer.write(dataOffset);
    writer.write(dataSize);
    writer.write(uncompressedSize);
}

void SaveGameHeader::deserialize(BinaryReader& reader) {
    magic = reader.read<uint32_t>();
    version = reader.read<uint32_t>();
    timestamp = reader.read<uint64_t>();
    checksum = reader.read<uint32_t>();
    reader.read(playerName, sizeof(playerName));
    playerLevel = reader.read<int>();
    playTime = reader.read<float>();
    reader.read(location, sizeof(location));
    reader.read(screenshotPath, sizeof(screenshotPath));
    dataOffset = reader.read<uint32_t>();
    dataSize = reader.read<uint32_t>();
    uncompressedSize = reader.read<uint32_t>();
}

// ============================================================================
// SaveGameData Implementation
// ============================================================================

void SaveGameData::serialize(BinaryWriter& writer) const {
    // Player state
    stats.serialize(writer);
    appearance.serialize(writer);
    weaponSkills.serialize(writer);
    writer.writeVec3(position);
    writer.writeQuat(rotation);

    // Story progress
    storyState.serialize(writer);
    writer.writeVector(completedQuests);
    writer.writeVector(activeQuests);
    writer.writeMap(questProgress);

    // Inventory
    writer.write<uint32_t>(static_cast<uint32_t>(inventory.size()));
    for (const auto& item : inventory) {
        item.serialize(writer);
    }
    writer.writeMap(equippedItems);
    writer.write(currency);

    // World state
    writer.write(currentDay);
    writer.write(timeOfDay);
    writer.writeVector(discoveredLocations);
    writer.writeVector(unlockedTerritories);
    writer.writeMap(worldFlags);

    // Settings
    settings.serialize(writer);
}

void SaveGameData::deserialize(BinaryReader& reader) {
    // Player state
    stats.deserialize(reader);
    appearance.deserialize(reader);
    weaponSkills.deserialize(reader);
    position = reader.readVec3();
    rotation = reader.readQuat();

    // Story progress
    storyState.deserialize(reader);
    completedQuests = reader.readVector<std::string>();
    activeQuests = reader.readVector<std::string>();
    questProgress = reader.readMap<std::string, int>();

    // Inventory
    uint32_t inventorySize = reader.read<uint32_t>();
    inventory.clear();
    inventory.reserve(inventorySize);
    for (uint32_t i = 0; i < inventorySize; ++i) {
        InventoryItem item;
        item.deserialize(reader);
        inventory.push_back(item);
    }
    equippedItems = reader.readMap<std::string, std::string>();
    currency = reader.read<int>();

    // World state
    currentDay = reader.read<int>();
    timeOfDay = reader.read<float>();
    discoveredLocations = reader.readVector<std::string>();
    unlockedTerritories = reader.readVector<std::string>();
    worldFlags = reader.readMap<std::string, bool>();

    // Settings
    settings.deserialize(reader);
}

// ============================================================================
// SaveSystem Implementation
// ============================================================================

SaveSystem::SaveSystem()
    : m_autoSaveEnabled(true)
    , m_autoSaveInterval(300.0f)  // 5 minutes default
    , m_autoSaveTimer(0.0f)
{
}

SaveSystem::~SaveSystem() {
}

void SaveSystem::initialize() {
    m_savePath = getSavePath();
    createSaveDirectory();
    logInfo("SaveSystem: Initialized with save path: " + m_savePath);
}

bool SaveSystem::saveGame(int slotIndex) {
    if (slotIndex < 0 || slotIndex >= MAX_SAVE_SLOTS) {
        logError("SaveSystem: Invalid save slot: " + std::to_string(slotIndex));
        return false;
    }

    std::string filename = getSaveFilename(slotIndex);
    return saveGame(filename);
}

bool SaveSystem::saveGame(const std::string& filename) {
    if (onSaveStart) {
        onSaveStart();
    }

    std::string fullPath = m_savePath + "/" + filename + SAVE_FILE_EXTENSION;
    bool success = saveToFile(fullPath, m_currentData);

    if (onSaveComplete) {
        onSaveComplete(success);
    }

    if (success) {
        logInfo("SaveSystem: Game saved successfully to " + fullPath);
    } else {
        logError("SaveSystem: Failed to save game to " + fullPath);
    }

    return success;
}

bool SaveSystem::quickSave() {
    return saveGame(QUICK_SAVE_NAME);
}

bool SaveSystem::autoSave() {
    logInfo("SaveSystem: Auto-saving...");
    return saveGame(AUTO_SAVE_NAME);
}

bool SaveSystem::loadGame(int slotIndex) {
    if (slotIndex < 0 || slotIndex >= MAX_SAVE_SLOTS) {
        logError("SaveSystem: Invalid save slot: " + std::to_string(slotIndex));
        return false;
    }

    std::string filename = getSaveFilename(slotIndex);
    return loadGame(filename);
}

bool SaveSystem::loadGame(const std::string& filename) {
    if (onLoadStart) {
        onLoadStart();
    }

    std::string fullPath = m_savePath + "/" + filename + SAVE_FILE_EXTENSION;
    bool success = loadFromFile(fullPath, m_loadedData);

    if (onLoadComplete) {
        onLoadComplete(success);
    }

    if (success) {
        logInfo("SaveSystem: Game loaded successfully from " + fullPath);
    } else {
        logError("SaveSystem: Failed to load game from " + fullPath);
    }

    return success;
}

bool SaveSystem::quickLoad() {
    return loadGame(QUICK_SAVE_NAME);
}

std::vector<SaveGameHeader> SaveSystem::getSaveSlots() {
    std::vector<SaveGameHeader> headers;
    headers.reserve(MAX_SAVE_SLOTS);

    for (int i = 0; i < MAX_SAVE_SLOTS; ++i) {
        headers.push_back(getSaveHeader(i));
    }

    return headers;
}

bool SaveSystem::deleteSave(int slotIndex) {
    if (slotIndex < 0 || slotIndex >= MAX_SAVE_SLOTS) {
        return false;
    }

    std::string filename = getSaveFilename(slotIndex);
    std::string fullPath = m_savePath + "/" + filename + SAVE_FILE_EXTENSION;

    try {
        if (std::filesystem::exists(fullPath)) {
            std::filesystem::remove(fullPath);
            logInfo("SaveSystem: Deleted save slot " + std::to_string(slotIndex));
            return true;
        }
    } catch (const std::exception& e) {
        logError("SaveSystem: Failed to delete save: " + std::string(e.what()));
    }

    return false;
}

bool SaveSystem::doesSaveExist(int slotIndex) const {
    if (slotIndex < 0 || slotIndex >= MAX_SAVE_SLOTS) {
        return false;
    }

    std::string filename = getSaveFilename(slotIndex);
    std::string fullPath = m_savePath + "/" + filename + SAVE_FILE_EXTENSION;

    return std::filesystem::exists(fullPath);
}

SaveGameHeader SaveSystem::getSaveHeader(int slotIndex) const {
    if (slotIndex < 0 || slotIndex >= MAX_SAVE_SLOTS) {
        return SaveGameHeader();
    }

    std::string filename = getSaveFilename(slotIndex);
    std::string fullPath = m_savePath + "/" + filename + SAVE_FILE_EXTENSION;

    if (!std::filesystem::exists(fullPath)) {
        return SaveGameHeader();
    }

    try {
        return readHeader(fullPath);
    } catch (const std::exception& e) {
        logError("SaveSystem: Failed to read header: " + std::string(e.what()));
        return SaveGameHeader();
    }
}

void SaveSystem::enableAutoSave(bool enabled) {
    m_autoSaveEnabled = enabled;
    m_autoSaveTimer = 0.0f;
    logInfo("SaveSystem: Auto-save " + std::string(enabled ? "enabled" : "disabled"));
}

void SaveSystem::setAutoSaveInterval(float seconds) {
    m_autoSaveInterval = std::max(60.0f, seconds);  // Minimum 1 minute
    logInfo("SaveSystem: Auto-save interval set to " + std::to_string(m_autoSaveInterval) + " seconds");
}

void SaveSystem::update(float deltaTime) {
    if (!m_autoSaveEnabled) {
        return;
    }

    m_autoSaveTimer += deltaTime;

    if (m_autoSaveTimer >= m_autoSaveInterval) {
        m_autoSaveTimer = 0.0f;

        if (onAutoSave) {
            onAutoSave(m_autoSaveInterval);
        }

        autoSave();
    }
}

void SaveSystem::setCurrentSaveData(const SaveGameData& data) {
    m_currentData = data;
}

std::string SaveSystem::getSavePath() const {
    return m_savePath.empty() ? getPlatformSavePath() : m_savePath;
}

std::string SaveSystem::getSaveFilename(int slotIndex) const {
    return "save_slot_" + std::to_string(slotIndex);
}

std::string SaveSystem::getQuickSaveFilename() const {
    return std::string(QUICK_SAVE_NAME);
}

std::string SaveSystem::getAutoSaveFilename() const {
    return std::string(AUTO_SAVE_NAME);
}

// ============================================================================
// Private Implementation
// ============================================================================

bool SaveSystem::saveToFile(const std::string& fullPath, const SaveGameData& data) {
    try {
        // Create a temporary buffer to write data
        std::string tempPath = fullPath + ".tmp";

        // First, serialize the data to get its size
        BinaryWriter tempWriter(tempPath);

        // Reserve space for header (we'll write it later)
        SaveGameHeader header;
        size_t headerStartPos = tempWriter.tell();
        header.serialize(tempWriter);
        size_t dataStartPos = tempWriter.tell();

        // Write the actual save data
        data.serialize(tempWriter);
        size_t dataEndPos = tempWriter.tell();

        tempWriter.close();

        // Calculate data size
        size_t uncompressedSize = dataEndPos - dataStartPos;

        // Read the data section back
        std::ifstream tempRead(tempPath, std::ios::binary);
        tempRead.seekg(dataStartPos);
        std::vector<char> uncompressedData(uncompressedSize);
        tempRead.read(uncompressedData.data(), uncompressedSize);
        tempRead.close();

        // Compress the data
        size_t compressedSize;
        char* compressedData = compressData(uncompressedData.data(), uncompressedSize, compressedSize);

        // Calculate checksum
        uint32_t checksum = calculateCRC32(uncompressedData.data(), uncompressedSize);

        // Fill in header
        header.timestamp = std::time(nullptr);
        header.checksum = checksum;
        header.dataOffset = static_cast<uint32_t>(dataStartPos);
        header.dataSize = static_cast<uint32_t>(compressedSize);
        header.uncompressedSize = static_cast<uint32_t>(uncompressedSize);
        header.playerLevel = data.stats.level;
        header.playTime = 0.0f; // TODO: Track actual play time

        std::strncpy(header.playerName, "Player", sizeof(header.playerName) - 1);
        std::strncpy(header.location, "Forest", sizeof(header.location) - 1);
        std::strncpy(header.screenshotPath, "", sizeof(header.screenshotPath) - 1);

        // Write final file
        BinaryWriter finalWriter(fullPath);
        header.serialize(finalWriter);
        finalWriter.write(compressedData, compressedSize);
        finalWriter.close();

        // Cleanup
        delete[] compressedData;
        std::filesystem::remove(tempPath);

        logInfo("SaveSystem: Saved " + std::to_string(uncompressedSize) + " bytes (compressed to " +
                     std::to_string(compressedSize) + " bytes)");

        return true;

    } catch (const std::exception& e) {
        logError("SaveSystem: Save failed: " + std::string(e.what()));
        return false;
    }
}

bool SaveSystem::loadFromFile(const std::string& fullPath, SaveGameData& data) {
    try {
        if (!std::filesystem::exists(fullPath)) {
            logError("SaveSystem: Save file does not exist: " + fullPath);
            return false;
        }

        BinaryReader reader(fullPath);

        // Read header
        SaveGameHeader header;
        header.deserialize(reader);

        // Verify magic number
        if (header.magic != 0xCA75A4E) {
            logError("SaveSystem: Invalid save file (bad magic number)");
            reader.close();
            return false;
        }

        // Check version
        if (header.version != 1) {
            logWarn("SaveSystem: Save file version mismatch (expected 1, got " +
                        std::to_string(header.version) + ")");
            // Could add version migration here
        }

        // Read compressed data
        std::vector<char> compressedData(header.dataSize);
        reader.read(compressedData.data(), header.dataSize);
        reader.close();

        // Decompress data
        char* uncompressedData = decompressData(compressedData.data(), header.dataSize, header.uncompressedSize);

        // Verify checksum
        uint32_t checksum = calculateCRC32(uncompressedData, header.uncompressedSize);
        if (checksum != header.checksum) {
            logError("SaveSystem: Checksum mismatch (save file may be corrupted)");
            delete[] uncompressedData;
            return false;
        }

        // Write uncompressed data to temporary file for reading
        std::string tempPath = fullPath + ".load_tmp";
        std::ofstream tempWrite(tempPath, std::ios::binary);
        tempWrite.write(uncompressedData, header.uncompressedSize);
        tempWrite.close();

        delete[] uncompressedData;

        // Read the data
        BinaryReader dataReader(tempPath);
        data.deserialize(dataReader);
        dataReader.close();

        // Cleanup
        std::filesystem::remove(tempPath);

        logInfo("SaveSystem: Loaded " + std::to_string(header.uncompressedSize) + " bytes");

        return true;

    } catch (const std::exception& e) {
        logError("SaveSystem: Load failed: " + std::string(e.what()));
        return false;
    }
}

SaveGameHeader SaveSystem::readHeader(const std::string& fullPath) const {
    BinaryReader reader(fullPath);
    SaveGameHeader header;
    header.deserialize(reader);
    reader.close();
    return header;
}

std::string SaveSystem::getPlatformSavePath() const {
#ifdef _WIN32
    // Windows: %APPDATA%/CatAnnihilation/saves/
    char* appData = nullptr;
    size_t len = 0;
    _dupenv_s(&appData, &len, "APPDATA");
    if (appData) {
        std::string path = std::string(appData) + "/CatAnnihilation/saves";
        free(appData);
        return path;
    }
    return "./saves";
#else
    // Linux: ~/.local/share/cat-annihilation/saves/
    const char* home = getenv("HOME");
    if (!home) {
        struct passwd* pw = getpwuid(getuid());
        home = pw->pw_dir;
    }
    return std::string(home) + "/.local/share/cat-annihilation/saves";
#endif
}

void SaveSystem::createSaveDirectory() {
    try {
        if (!std::filesystem::exists(m_savePath)) {
            std::filesystem::create_directories(m_savePath);
            logInfo("SaveSystem: Created save directory: " + m_savePath);
        }
    } catch (const std::exception& e) {
        logError("SaveSystem: Failed to create save directory: " + std::string(e.what()));
    }
}

} // namespace Engine
