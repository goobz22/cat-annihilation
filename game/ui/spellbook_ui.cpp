#include "spellbook_ui.hpp"
#include "../../engine/core/Logger.hpp"
#include <algorithm>
#include <sstream>

namespace Game {

SpellbookUI::SpellbookUI(Engine::Input& input, CatGame::ElementalMagicSystem* magicSystem)
    : m_input(input)
    , m_magicSystem(magicSystem) {
}

SpellbookUI::~SpellbookUI() {
    shutdown();
}

bool SpellbookUI::initialize() {
    if (m_initialized) {
        Engine::Logger::warn("SpellbookUI already initialized");
        return true;
    }

    if (!m_magicSystem) {
        Engine::Logger::error("SpellbookUI: ElementalMagicSystem is null");
        return false;
    }

    // Calculate centered window position
    // TODO: Get actual screen dimensions from renderer
    float screenWidth = 1920.0f;
    float screenHeight = 1080.0f;
    m_windowX = (screenWidth - m_windowWidth) / 2.0f;
    m_windowY = (screenHeight - m_windowHeight) / 2.0f;

    m_detailsPanelWidth = m_windowWidth - m_spellListWidth - 60.0f;  // Padding

    m_initialized = true;
    Engine::Logger::info("SpellbookUI initialized successfully");
    return true;
}

void SpellbookUI::shutdown() {
    if (!m_initialized) {
        return;
    }

    m_spellSlots.fill("");
    m_selectedSpellId.clear();

    m_initialized = false;
    Engine::Logger::info("SpellbookUI shutdown");
}

void SpellbookUI::update(float deltaTime) {
    if (!m_initialized) {
        return;
    }

    // Update open/close animation
    if (m_isOpen) {
        m_openAnimation = std::min(1.0f, m_openAnimation + deltaTime * m_openAnimSpeed);
    } else {
        m_openAnimation = std::max(0.0f, m_openAnimation - deltaTime * m_openAnimSpeed);
    }

    // Update tab glow animation
    m_tabGlowAnimation += deltaTime * 2.0f;
    if (m_tabGlowAnimation > 6.28318f) {  // 2 * PI
        m_tabGlowAnimation -= 6.28318f;
    }

    // Update input cooldown
    if (m_inputCooldown > 0.0f) {
        m_inputCooldown -= deltaTime;
    }

    // Handle input if open
    if (m_isOpen && m_openAnimation >= 0.99f) {
        handleInput();
    }
}

void SpellbookUI::render(CatEngine::Renderer::Renderer& renderer) {
    if (!m_initialized || m_openAnimation <= 0.01f) {
        return;
    }

    // Render background
    renderBackground(renderer);

    // Render element tabs
    renderElementTabs(renderer);

    // Render spell list
    renderSpellList(renderer);

    // Render spell details
    renderSpellDetails(renderer);

    // Render elemental progression
    renderElementalProgression(renderer);

    // Render spell slots
    renderSpellSlots(renderer);
}

// ============================================================================
// Visibility
// ============================================================================

void SpellbookUI::open() {
    if (!m_isOpen) {
        m_isOpen = true;

        // Select first spell if none selected
        auto spells = getSpellsForSelectedElement();
        if (!spells.empty() && m_selectedSpellId.empty()) {
            m_selectedSpellId = spells[0]->id;
        }

        Engine::Logger::debug("SpellbookUI opened");
    }
}

void SpellbookUI::close() {
    if (m_isOpen) {
        m_isOpen = false;
        m_isDragging = false;
        Engine::Logger::debug("SpellbookUI closed");
    }
}

void SpellbookUI::toggle() {
    if (m_isOpen) {
        close();
    } else {
        open();
    }
}

// ============================================================================
// Navigation
// ============================================================================

void SpellbookUI::selectElement(CatGame::ElementType element) {
    if (m_selectedElement != element) {
        m_selectedElement = element;
        m_selectedSpellId.clear();

        // Select first spell for new element
        auto spells = getSpellsForSelectedElement();
        if (!spells.empty()) {
            m_selectedSpellId = spells[0]->id;
        }

        Engine::Logger::debug("SpellbookUI: Selected element {}",
                            CatGame::getElementName(element));
    }
}

void SpellbookUI::selectSpell(const std::string& spellId) {
    m_selectedSpellId = spellId;
}

void SpellbookUI::selectNextElement() {
    int elementIndex = static_cast<int>(m_selectedElement);
    elementIndex = (elementIndex + 1) % static_cast<int>(CatGame::ElementType::COUNT);
    selectElement(static_cast<CatGame::ElementType>(elementIndex));
}

void SpellbookUI::selectPreviousElement() {
    int elementIndex = static_cast<int>(m_selectedElement);
    elementIndex = (elementIndex - 1 + static_cast<int>(CatGame::ElementType::COUNT))
                   % static_cast<int>(CatGame::ElementType::COUNT);
    selectElement(static_cast<CatGame::ElementType>(elementIndex));
}

void SpellbookUI::selectNextSpell() {
    auto spells = getSpellsForSelectedElement();
    if (spells.empty()) {
        m_selectedSpellId.clear();
        return;
    }

    // Find current selection
    auto it = std::find_if(spells.begin(), spells.end(),
        [this](const CatGame::ElementalSpell* s) { return s->id == m_selectedSpellId; });

    if (it == spells.end() || (it + 1) == spells.end()) {
        // Select first spell
        m_selectedSpellId = spells[0]->id;
    } else {
        // Select next spell
        m_selectedSpellId = (*(it + 1))->id;
    }
}

void SpellbookUI::selectPreviousSpell() {
    auto spells = getSpellsForSelectedElement();
    if (spells.empty()) {
        m_selectedSpellId.clear();
        return;
    }

    // Find current selection
    auto it = std::find_if(spells.begin(), spells.end(),
        [this](const CatGame::ElementalSpell* s) { return s->id == m_selectedSpellId; });

    if (it == spells.end() || it == spells.begin()) {
        // Select last spell
        m_selectedSpellId = spells.back()->id;
    } else {
        // Select previous spell
        m_selectedSpellId = (*(it - 1))->id;
    }
}

// ============================================================================
// Spell Slots
// ============================================================================

bool SpellbookUI::assignSpellToSlot(const std::string& spellId, int slot) {
    if (slot < 0 || slot >= 4) {
        Engine::Logger::warn("SpellbookUI: Invalid slot index {}", slot);
        return false;
    }

    // Check if spell exists and is unlocked
    if (!isSpellUnlocked(spellId)) {
        Engine::Logger::warn("SpellbookUI: Cannot assign locked spell '{}'", spellId);
        return false;
    }

    m_spellSlots[slot] = spellId;
    Engine::Logger::info("SpellbookUI: Assigned spell '{}' to slot {}", spellId, slot + 1);
    return true;
}

std::string SpellbookUI::getSpellInSlot(int slot) const {
    if (slot < 0 || slot >= 4) {
        return "";
    }
    return m_spellSlots[slot];
}

void SpellbookUI::clearSlot(int slot) {
    if (slot >= 0 && slot < 4) {
        m_spellSlots[slot] = "";
        Engine::Logger::debug("SpellbookUI: Cleared slot {}", slot + 1);
    }
}

void SpellbookUI::assignSelectedSpellToSlot(int slot) {
    if (!m_selectedSpellId.empty()) {
        assignSpellToSlot(m_selectedSpellId, slot);
    }
}

// ============================================================================
// Spell Information
// ============================================================================

std::string SpellbookUI::getSpellName(const std::string& spellId) const {
    const auto* spell = m_magicSystem->getSpell(spellId);
    return spell ? spell->name : "Unknown";
}

std::string SpellbookUI::getSpellDescription(const std::string& spellId) const {
    const auto* spell = m_magicSystem->getSpell(spellId);
    return spell ? generateSpellDescription(spell) : "";
}

int SpellbookUI::getSpellManaCost(const std::string& spellId) const {
    const auto* spell = m_magicSystem->getSpell(spellId);
    return spell ? spell->manaCost : 0;
}

float SpellbookUI::getSpellCooldown(const std::string& spellId) const {
    const auto* spell = m_magicSystem->getSpell(spellId);
    return spell ? spell->cooldown : 0.0f;
}

float SpellbookUI::getSpellDamage(const std::string& spellId) const {
    const auto* spell = m_magicSystem->getSpell(spellId);
    return spell ? spell->damage : 0.0f;
}

float SpellbookUI::getSpellRange(const std::string& spellId) const {
    const auto* spell = m_magicSystem->getSpell(spellId);
    return spell ? spell->range : 0.0f;
}

bool SpellbookUI::isSpellUnlocked(const std::string& spellId) const {
    const auto* spell = m_magicSystem->getSpell(spellId);
    if (!spell) return false;

    // Check if player has required elemental level
    int playerLevel = getPlayerElementalLevel(spell->element);
    return playerLevel >= spell->requiredLevel;
}

bool SpellbookUI::isSpellOnCooldown(const std::string& spellId) const {
    if (!m_playerEntity.isValid()) return false;
    return m_magicSystem->getSpellCooldownRemaining(m_playerEntity, spellId) > 0.0f;
}

float SpellbookUI::getRemainingCooldown(const std::string& spellId) const {
    if (!m_playerEntity.isValid()) return 0.0f;
    return m_magicSystem->getSpellCooldownRemaining(m_playerEntity, spellId);
}

// ============================================================================
// Input Handling
// ============================================================================

void SpellbookUI::handleInput() {
    if (m_inputCooldown > 0.0f) {
        return;
    }

    // TODO: Replace with actual input handling
    // Example structure:
    /*
    if (m_input.isKeyPressed(Key::Escape) || m_input.isKeyPressed(Key::B)) {
        close();
        m_inputCooldown = INPUT_COOLDOWN_TIME;
    } else if (m_input.isKeyPressed(Key::Left) || m_input.isKeyPressed(Key::A)) {
        selectPreviousElement();
        m_inputCooldown = INPUT_COOLDOWN_TIME;
    } else if (m_input.isKeyPressed(Key::Right) || m_input.isKeyPressed(Key::D)) {
        selectNextElement();
        m_inputCooldown = INPUT_COOLDOWN_TIME;
    } else if (m_input.isKeyPressed(Key::Up) || m_input.isKeyPressed(Key::W)) {
        selectPreviousSpell();
        m_inputCooldown = INPUT_COOLDOWN_TIME;
    } else if (m_input.isKeyPressed(Key::Down) || m_input.isKeyPressed(Key::S)) {
        selectNextSpell();
        m_inputCooldown = INPUT_COOLDOWN_TIME;
    } else if (m_input.isKeyPressed(Key::Num1)) {
        assignSelectedSpellToSlot(0);
        m_inputCooldown = INPUT_COOLDOWN_TIME;
    } else if (m_input.isKeyPressed(Key::Num2)) {
        assignSelectedSpellToSlot(1);
        m_inputCooldown = INPUT_COOLDOWN_TIME;
    } else if (m_input.isKeyPressed(Key::Num3)) {
        assignSelectedSpellToSlot(2);
        m_inputCooldown = INPUT_COOLDOWN_TIME;
    } else if (m_input.isKeyPressed(Key::Num4)) {
        assignSelectedSpellToSlot(3);
        m_inputCooldown = INPUT_COOLDOWN_TIME;
    }
    */
}

// ============================================================================
// Private Rendering Methods
// ============================================================================

void SpellbookUI::renderBackground(CatEngine::Renderer::Renderer& renderer) {
    // TODO: Implement using renderer's 2D drawing API
    // Draw spellbook background with animation
    // Apply m_openAnimation for scale/fade effect
}

void SpellbookUI::renderElementTabs(CatEngine::Renderer::Renderer& renderer) {
    // TODO: Implement element tabs
    // Tabs for Water, Air, Earth, Fire
    // Highlight selected element
    // Show element icons and colors
    // Apply glow effect using m_tabGlowAnimation
}

void SpellbookUI::renderSpellList(CatEngine::Renderer::Renderer& renderer) {
    // TODO: Implement spell list rendering
    // Show spells for selected element
    // Display spell icons, names, and levels
    // Show locked/unlocked status
    // Highlight selected spell
    // Show cooldown overlays
}

void SpellbookUI::renderSpellDetails(CatEngine::Renderer::Renderer& renderer) {
    const auto* spell = getSelectedSpell();
    if (!spell) {
        return;
    }

    // TODO: Implement spell details panel
    // Show spell name, icon, description
    renderSpellStats(renderer, spell);

    if (!isSpellUnlocked(spell->id)) {
        renderUnlockRequirements(renderer, spell);
    }
}

void SpellbookUI::renderSpellSlots(CatEngine::Renderer::Renderer& renderer) {
    // TODO: Implement spell slots rendering
    // Show 4 slots at bottom of screen
    // Display assigned spells with icons
    // Show slot numbers (1-4)
    // Show cooldowns if spells are on cooldown
    // Highlight during drag-and-drop
}

void SpellbookUI::renderElementalProgression(CatEngine::Renderer::Renderer& renderer) {
    // TODO: Implement progression display
    // Show elemental level for selected element
    // Show XP bar to next level
    // Show unlocked spell count
}

void SpellbookUI::renderSpellStats(CatEngine::Renderer::Renderer& renderer,
                                   const CatGame::ElementalSpell* spell) {
    // TODO: Implement spell stats display
    // Show damage, range, cooldown, mana cost
    // Show special effects (heal, knockback, DOT, etc.)
    // Show AOE radius if applicable
}

void SpellbookUI::renderUnlockRequirements(CatEngine::Renderer::Renderer& renderer,
                                          const CatGame::ElementalSpell* spell) {
    // TODO: Implement unlock requirements display
    // Show required elemental level
    // Show current vs required level
}

// ============================================================================
// Private Helper Methods
// ============================================================================

std::vector<const CatGame::ElementalSpell*> SpellbookUI::getSpellsForSelectedElement() const {
    return m_magicSystem->getSpellsForElement(m_selectedElement);
}

const CatGame::ElementalSpell* SpellbookUI::getSelectedSpell() const {
    if (m_selectedSpellId.empty()) {
        return nullptr;
    }
    return m_magicSystem->getSpell(m_selectedSpellId);
}

std::string SpellbookUI::getSpellIconPath(const std::string& spellId) const {
    return "assets/textures/ui/spell_icons/" + spellId + ".png";
}

std::string SpellbookUI::getElementIconPath(CatGame::ElementType element) const {
    using namespace CatGame;
    switch (element) {
        case ElementType::Water:
            return "assets/textures/ui/element_icons/water.png";
        case ElementType::Air:
            return "assets/textures/ui/element_icons/air.png";
        case ElementType::Earth:
            return "assets/textures/ui/element_icons/earth.png";
        case ElementType::Fire:
            return "assets/textures/ui/element_icons/fire.png";
        default:
            return "";
    }
}

std::string SpellbookUI::generateSpellDescription(const CatGame::ElementalSpell* spell) const {
    if (!spell) return "";

    std::ostringstream desc;

    // Base description based on spell type
    if (spell->damage > 0.0f) {
        desc << "Deals " << static_cast<int>(spell->damage) << " damage";
    }

    if (spell->healAmount > 0.0f) {
        if (desc.tellp() > 0) desc << ". ";
        desc << "Heals for " << static_cast<int>(spell->healAmount) << " health";
    }

    if (spell->areaOfEffect > 0.0f) {
        if (desc.tellp() > 0) desc << " ";
        desc << "in a " << static_cast<int>(spell->areaOfEffect) << "m radius";
    }

    if (spell->knockbackForce > 0.0f) {
        if (desc.tellp() > 0) desc << ". ";
        desc << "Knocks back enemies";
    }

    if (spell->dotDamage > 0.0f && spell->duration > 0.0f) {
        if (desc.tellp() > 0) desc << ". ";
        desc << "Deals " << static_cast<int>(spell->dotDamage)
             << " damage per second for " << static_cast<int>(spell->duration) << "s";
    }

    if (spell->speedMultiplier > 1.0f) {
        if (desc.tellp() > 0) desc << ". ";
        int bonus = static_cast<int>((spell->speedMultiplier - 1.0f) * 100.0f);
        desc << "Increases speed by " << bonus << "%";
    }

    if (spell->defenseMultiplier > 1.0f) {
        if (desc.tellp() > 0) desc << ". ";
        int bonus = static_cast<int>((spell->defenseMultiplier - 1.0f) * 100.0f);
        desc << "Reduces damage taken by " << bonus << "%";
    }

    if (spell->createBarrier) {
        if (desc.tellp() > 0) desc << ". ";
        desc << "Creates a barrier for " << static_cast<int>(spell->duration) << "s";
    }

    if (spell->isUltimate) {
        if (desc.tellp() > 0) desc << ". ";
        desc << "[ULTIMATE SPELL]";
    }

    return desc.str();
}

int SpellbookUI::getPlayerElementalLevel(CatGame::ElementType element) const {
    if (!m_playerEntity.isValid()) {
        return 1;  // Default level
    }
    return m_magicSystem->getElementalLevel(m_playerEntity, element);
}

} // namespace Game
