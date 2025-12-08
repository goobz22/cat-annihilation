#ifndef GAME_UI_SPELLBOOK_UI_HPP
#define GAME_UI_SPELLBOOK_UI_HPP

#include "../../engine/core/Input.hpp"
#include "../../engine/renderer/Renderer.hpp"
#include "../systems/elemental_magic.hpp"
#include "../systems/spell_definitions.hpp"
#include "../../engine/ecs/Entity.hpp"
#include <string>
#include <vector>
#include <array>
#include <memory>

namespace Game {

// Forward declaration
namespace CatGame {
    class ElementalMagicSystem;
}

/**
 * @brief Spellbook UI - Full-screen spell management interface
 *
 * Features:
 * - Element tabs (Water, Air, Earth, Fire)
 * - Spell list for selected element
 * - Detailed spell information
 * - Quick cast slot assignment (1-4)
 * - Spell cooldown display
 * - Elemental level progression
 * - Spell unlock requirements
 */
class SpellbookUI {
public:
    explicit SpellbookUI(Engine::Input& input, CatGame::ElementalMagicSystem* magicSystem);
    ~SpellbookUI();

    /**
     * @brief Initialize spellbook UI
     * @return true if successful
     */
    bool initialize();

    /**
     * @brief Shutdown spellbook UI
     */
    void shutdown();

    /**
     * @brief Update spellbook UI (call once per frame)
     * @param deltaTime Time since last frame in seconds
     */
    void update(float deltaTime);

    /**
     * @brief Render spellbook UI
     * @param renderer Renderer to use for drawing
     */
    void render(CatEngine::Renderer::Renderer& renderer);

    /**
     * @brief Set the player entity (for checking spell availability)
     * @param playerEntity Player entity
     */
    void setPlayerEntity(CatEngine::Entity playerEntity) { m_playerEntity = playerEntity; }

    // ========================================================================
    // Visibility
    // ========================================================================

    /**
     * @brief Open the spellbook
     */
    void open();

    /**
     * @brief Close the spellbook
     */
    void close();

    /**
     * @brief Toggle spellbook open/closed
     */
    void toggle();

    /**
     * @brief Check if spellbook is open
     */
    bool isOpen() const { return m_isOpen; }

    // ========================================================================
    // Navigation
    // ========================================================================

    /**
     * @brief Select element tab
     * @param element Element type
     */
    void selectElement(CatGame::ElementType element);

    /**
     * @brief Get selected element
     */
    CatGame::ElementType getSelectedElement() const { return m_selectedElement; }

    /**
     * @brief Select a spell in the list
     * @param spellId Spell ID to select
     */
    void selectSpell(const std::string& spellId);

    /**
     * @brief Get selected spell ID
     */
    std::string getSelectedSpellId() const { return m_selectedSpellId; }

    /**
     * @brief Select next element tab
     */
    void selectNextElement();

    /**
     * @brief Select previous element tab
     */
    void selectPreviousElement();

    /**
     * @brief Select next spell in list
     */
    void selectNextSpell();

    /**
     * @brief Select previous spell in list
     */
    void selectPreviousSpell();

    // ========================================================================
    // Spell Slots (Quick Cast)
    // ========================================================================

    /**
     * @brief Assign spell to quick cast slot
     * @param spellId Spell ID
     * @param slot Slot number (0-3 for slots 1-4)
     * @return true if spell was assigned
     */
    bool assignSpellToSlot(const std::string& spellId, int slot);

    /**
     * @brief Get spell ID in quick cast slot
     * @param slot Slot number (0-3 for slots 1-4)
     * @return Spell ID, or empty string if slot is empty
     */
    std::string getSpellInSlot(int slot) const;

    /**
     * @brief Clear quick cast slot
     * @param slot Slot number (0-3 for slots 1-4)
     */
    void clearSlot(int slot);

    /**
     * @brief Assign selected spell to slot
     * @param slot Slot number (0-3 for slots 1-4)
     */
    void assignSelectedSpellToSlot(int slot);

    /**
     * @brief Get all spell slots
     * @return Array of spell IDs (empty strings for empty slots)
     */
    std::array<std::string, 4> getSpellSlots() const { return m_spellSlots; }

    // ========================================================================
    // Spell Information
    // ========================================================================

    /**
     * @brief Get spell name
     * @param spellId Spell ID
     */
    std::string getSpellName(const std::string& spellId) const;

    /**
     * @brief Get spell description
     * @param spellId Spell ID
     */
    std::string getSpellDescription(const std::string& spellId) const;

    /**
     * @brief Get spell mana cost
     * @param spellId Spell ID
     */
    int getSpellManaCost(const std::string& spellId) const;

    /**
     * @brief Get spell cooldown
     * @param spellId Spell ID
     */
    float getSpellCooldown(const std::string& spellId) const;

    /**
     * @brief Get spell damage
     * @param spellId Spell ID
     */
    float getSpellDamage(const std::string& spellId) const;

    /**
     * @brief Get spell range
     * @param spellId Spell ID
     */
    float getSpellRange(const std::string& spellId) const;

    /**
     * @brief Check if spell is unlocked for player
     * @param spellId Spell ID
     */
    bool isSpellUnlocked(const std::string& spellId) const;

    /**
     * @brief Check if spell is on cooldown
     * @param spellId Spell ID
     */
    bool isSpellOnCooldown(const std::string& spellId) const;

    /**
     * @brief Get remaining cooldown time
     * @param spellId Spell ID
     */
    float getRemainingCooldown(const std::string& spellId) const;

    // ========================================================================
    // Input Handling
    // ========================================================================

    /**
     * @brief Handle input events
     * Call this each frame when spellbook is open
     */
    void handleInput();

private:
    /**
     * @brief Render spellbook background
     */
    void renderBackground(CatEngine::Renderer::Renderer& renderer);

    /**
     * @brief Render element tabs
     */
    void renderElementTabs(CatEngine::Renderer::Renderer& renderer);

    /**
     * @brief Render spell list for selected element
     */
    void renderSpellList(CatEngine::Renderer::Renderer& renderer);

    /**
     * @brief Render spell details panel
     */
    void renderSpellDetails(CatEngine::Renderer::Renderer& renderer);

    /**
     * @brief Render spell slots (bottom of screen)
     */
    void renderSpellSlots(CatEngine::Renderer::Renderer& renderer);

    /**
     * @brief Render elemental level/progression
     */
    void renderElementalProgression(CatEngine::Renderer::Renderer& renderer);

    /**
     * @brief Render spell stats (damage, cooldown, mana cost, etc.)
     */
    void renderSpellStats(CatEngine::Renderer::Renderer& renderer,
                         const CatGame::ElementalSpell* spell);

    /**
     * @brief Render spell unlock requirements
     */
    void renderUnlockRequirements(CatEngine::Renderer::Renderer& renderer,
                                 const CatGame::ElementalSpell* spell);

    /**
     * @brief Get spells for selected element
     */
    std::vector<const CatGame::ElementalSpell*> getSpellsForSelectedElement() const;

    /**
     * @brief Get selected spell pointer
     */
    const CatGame::ElementalSpell* getSelectedSpell() const;

    /**
     * @brief Get spell icon path
     */
    std::string getSpellIconPath(const std::string& spellId) const;

    /**
     * @brief Get element icon path
     */
    std::string getElementIconPath(CatGame::ElementType element) const;

    /**
     * @brief Generate spell description text
     */
    std::string generateSpellDescription(const CatGame::ElementalSpell* spell) const;

    /**
     * @brief Get player's elemental level for element
     */
    int getPlayerElementalLevel(CatGame::ElementType element) const;

    Engine::Input& m_input;
    CatGame::ElementalMagicSystem* m_magicSystem;
    CatEngine::Entity m_playerEntity;

    // UI State
    bool m_isOpen = false;
    CatGame::ElementType m_selectedElement = CatGame::ElementType::Water;
    std::string m_selectedSpellId;

    // Quick cast slots (1-4)
    std::array<std::string, 4> m_spellSlots = {"", "", "", ""};

    // Layout (in pixels)
    float m_windowWidth = 1200.0f;
    float m_windowHeight = 800.0f;
    float m_windowX = 0.0f;  // Calculated to center
    float m_windowY = 0.0f;  // Calculated to center

    float m_tabHeight = 60.0f;
    float m_spellListWidth = 350.0f;
    float m_detailsPanelWidth = 0.0f;  // Calculated
    float m_slotsHeight = 100.0f;

    // Animation
    float m_openAnimation = 0.0f;  // 0 = closed, 1 = open
    float m_openAnimSpeed = 5.0f;
    float m_tabGlowAnimation = 0.0f;

    // Input cooldown (prevent double-inputs)
    float m_inputCooldown = 0.0f;
    static constexpr float INPUT_COOLDOWN_TIME = 0.15f;

    // Drag and drop state (for assigning to slots)
    bool m_isDragging = false;
    std::string m_draggedSpellId;

    bool m_initialized = false;
};

} // namespace Game

#endif // GAME_UI_SPELLBOOK_UI_HPP
