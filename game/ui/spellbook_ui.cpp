#include "spellbook_ui.hpp"
#include "../../engine/core/Logger.hpp"
#include "../../engine/renderer/passes/UIPass.hpp"
#include <algorithm>
#include <sstream>
#include <cmath>

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

    // Calculate centered window position with default screen dimensions
    // These will be updated in render() when actual renderer dimensions are available
    m_screenWidth = 1920.0f;
    m_screenHeight = 1080.0f;
    m_windowX = (m_screenWidth - m_windowWidth) / 2.0f;
    m_windowY = (m_screenHeight - m_windowHeight) / 2.0f;

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

    // Update screen dimensions from renderer and recenter window if changed
    float newScreenWidth = static_cast<float>(renderer.GetWidth());
    float newScreenHeight = static_cast<float>(renderer.GetHeight());
    if (newScreenWidth != m_screenWidth || newScreenHeight != m_screenHeight) {
        m_screenWidth = newScreenWidth;
        m_screenHeight = newScreenHeight;
        m_windowX = (m_screenWidth - m_windowWidth) / 2.0f;
        m_windowY = (m_screenHeight - m_windowHeight) / 2.0f;
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

    using Key = Engine::Input::Key;

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
}

// ============================================================================
// Private Rendering Methods
// ============================================================================

void SpellbookUI::renderBackground(CatEngine::Renderer::Renderer& renderer) {
    auto* uiPass = renderer.GetUIPass();
    if (!uiPass) return;

    float animScale = m_openAnimation;
    float animAlpha = m_openAnimation;

    float centerX = m_windowX + m_windowWidth / 2.0F;
    float centerY = m_windowY + m_windowHeight / 2.0F;

    float scaledWidth = m_windowWidth * animScale;
    float scaledHeight = m_windowHeight * animScale;
    float scaledX = centerX - scaledWidth / 2.0F;
    float scaledY = centerY - scaledHeight / 2.0F;

    // Draw semi-transparent dark overlay
    CatEngine::Renderer::UIPass::QuadDesc overlay;
    overlay.x = 0.0F;
    overlay.y = 0.0F;
    overlay.width = static_cast<float>(renderer.GetWidth());
    overlay.height = static_cast<float>(renderer.GetHeight());
    overlay.r = 0.0F;
    overlay.g = 0.0F;
    overlay.b = 0.1F;
    overlay.a = 0.6F * animAlpha;
    overlay.depth = 0.0F;
    uiPass->DrawQuad(overlay);

    // Draw spellbook background (mystical dark blue)
    CatEngine::Renderer::UIPass::QuadDesc windowBg;
    windowBg.x = scaledX;
    windowBg.y = scaledY;
    windowBg.width = scaledWidth;
    windowBg.height = scaledHeight;
    windowBg.r = 0.08F;
    windowBg.g = 0.10F;
    windowBg.b = 0.18F;
    windowBg.a = 0.95F * animAlpha;
    windowBg.depth = 0.1F;
    uiPass->DrawQuad(windowBg);

    // Draw magical border (glowing)
    float borderWidth = 3.0F;
    float glowIntensity = 0.5F + 0.3F * std::sin(m_tabGlowAnimation);
    CatEngine::Renderer::UIPass::QuadDesc border;
    border.r = 0.3F + 0.2F * glowIntensity;
    border.g = 0.4F + 0.3F * glowIntensity;
    border.b = 0.8F + 0.2F * glowIntensity;
    border.a = animAlpha;
    border.depth = 0.2F;
    // Top border
    border.x = scaledX;
    border.y = scaledY;
    border.width = scaledWidth;
    border.height = borderWidth;
    uiPass->DrawQuad(border);
    // Bottom border
    border.y = scaledY + scaledHeight - borderWidth;
    uiPass->DrawQuad(border);
    // Left border
    border.x = scaledX;
    border.y = scaledY;
    border.width = borderWidth;
    border.height = scaledHeight;
    uiPass->DrawQuad(border);
    // Right border
    border.x = scaledX + scaledWidth - borderWidth;
    uiPass->DrawQuad(border);

    // Draw title bar
    CatEngine::Renderer::UIPass::QuadDesc titleBar;
    titleBar.x = scaledX + borderWidth;
    titleBar.y = scaledY + borderWidth;
    titleBar.width = scaledWidth - 2.0F * borderWidth;
    titleBar.height = 45.0F * animScale;
    titleBar.r = 0.12F;
    titleBar.g = 0.15F;
    titleBar.b = 0.25F;
    titleBar.a = animAlpha;
    titleBar.depth = 0.2F;
    uiPass->DrawQuad(titleBar);

    // Draw title text
    CatEngine::Renderer::UIPass::TextDesc titleText;
    titleText.text = "Spellbook";
    titleText.x = scaledX + scaledWidth / 2.0F - 55.0F;
    titleText.y = scaledY + borderWidth + 12.0F * animScale;
    titleText.fontSize = 26.0F * animScale;
    titleText.r = 0.7F + 0.3F * glowIntensity;
    titleText.g = 0.8F + 0.2F * glowIntensity;
    titleText.b = 1.0F;
    titleText.a = animAlpha;
    titleText.depth = 0.3F;
    uiPass->DrawText(titleText);
}

void SpellbookUI::renderElementTabs(CatEngine::Renderer::Renderer& renderer) {
    auto* uiPass = renderer.GetUIPass();
    if (!uiPass) return;

    float animAlpha = m_openAnimation;
    float tabY = m_windowY + 55.0F;
    float tabX = m_windowX + 20.0F;
    float tabWidth = 100.0F;
    float tabHeight = 40.0F;
    float tabSpacing = 15.0F;

    // Element colors (Water, Air, Earth, Fire)
    struct ElementInfo {
        const char* name;
        float r, g, b;
    };
    ElementInfo elements[] = {
        {"Water", 0.2F, 0.5F, 0.9F},
        {"Air", 0.7F, 0.8F, 0.9F},
        {"Earth", 0.5F, 0.35F, 0.2F},
        {"Fire", 0.9F, 0.3F, 0.1F}
    };
    int numElements = 4;

    float glowIntensity = 0.5F + 0.3F * std::sin(m_tabGlowAnimation);

    for (int i = 0; i < numElements; ++i) {
        bool isSelected = (static_cast<int>(m_selectedElement) == i);

        CatEngine::Renderer::UIPass::QuadDesc tabBg;
        tabBg.x = tabX + static_cast<float>(i) * (tabWidth + tabSpacing);
        tabBg.y = tabY;
        tabBg.width = tabWidth;
        tabBg.height = tabHeight;

        if (isSelected) {
            // Glowing effect for selected element
            tabBg.r = elements[i].r * (0.8F + 0.2F * glowIntensity);
            tabBg.g = elements[i].g * (0.8F + 0.2F * glowIntensity);
            tabBg.b = elements[i].b * (0.8F + 0.2F * glowIntensity);
        } else {
            tabBg.r = elements[i].r * 0.4F;
            tabBg.g = elements[i].g * 0.4F;
            tabBg.b = elements[i].b * 0.4F;
        }
        tabBg.a = animAlpha;
        tabBg.depth = 0.3F;
        uiPass->DrawQuad(tabBg);

        // Element border
        if (isSelected) {
            CatEngine::Renderer::UIPass::QuadDesc tabBorder;
            tabBorder.x = tabBg.x - 2.0F;
            tabBorder.y = tabBg.y - 2.0F;
            tabBorder.width = tabWidth + 4.0F;
            tabBorder.height = tabHeight + 4.0F;
            tabBorder.r = elements[i].r;
            tabBorder.g = elements[i].g;
            tabBorder.b = elements[i].b;
            tabBorder.a = animAlpha * glowIntensity;
            tabBorder.depth = 0.28F;
            uiPass->DrawQuad(tabBorder);
        }

        // Element name
        CatEngine::Renderer::UIPass::TextDesc tabText;
        tabText.text = elements[i].name;
        tabText.x = tabBg.x + 20.0F;
        tabText.y = tabBg.y + 12.0F;
        tabText.fontSize = 14.0F;
        if (isSelected) {
            tabText.r = 1.0F;
            tabText.g = 1.0F;
            tabText.b = 1.0F;
        } else {
            tabText.r = 0.7F;
            tabText.g = 0.7F;
            tabText.b = 0.7F;
        }
        tabText.a = animAlpha;
        tabText.depth = 0.35F;
        uiPass->DrawText(tabText);
    }
}

void SpellbookUI::renderSpellList(CatEngine::Renderer::Renderer& renderer) {
    auto* uiPass = renderer.GetUIPass();
    if (!uiPass) return;

    float animAlpha = m_openAnimation;
    auto spells = getSpellsForSelectedElement();

    // Spell list panel
    float listX = m_windowX + 20.0F;
    float listY = m_windowY + 110.0F;
    float listHeight = m_windowHeight - 220.0F;

    CatEngine::Renderer::UIPass::QuadDesc listBg;
    listBg.x = listX;
    listBg.y = listY;
    listBg.width = m_spellListWidth;
    listBg.height = listHeight;
    listBg.r = 0.06F;
    listBg.g = 0.08F;
    listBg.b = 0.14F;
    listBg.a = animAlpha;
    listBg.depth = 0.25F;
    uiPass->DrawQuad(listBg);

    // Render spells
    float spellItemHeight = 55.0F;
    float spellSpacing = 5.0F;
    float spellY = listY + 10.0F;

    for (const auto* spell : spells) {
        bool isSelected = (spell->id == m_selectedSpellId);
        bool isUnlocked = isSpellUnlocked(spell->id);
        bool isOnCooldown = isSpellOnCooldown(spell->id);

        // Spell item background
        CatEngine::Renderer::UIPass::QuadDesc spellBg;
        spellBg.x = listX + 5.0F;
        spellBg.y = spellY;
        spellBg.width = m_spellListWidth - 10.0F;
        spellBg.height = spellItemHeight;

        if (!isUnlocked) {
            spellBg.r = 0.15F;
            spellBg.g = 0.15F;
            spellBg.b = 0.15F;
        } else if (isSelected) {
            spellBg.r = 0.15F;
            spellBg.g = 0.20F;
            spellBg.b = 0.35F;
        } else {
            spellBg.r = 0.10F;
            spellBg.g = 0.12F;
            spellBg.b = 0.20F;
        }
        spellBg.a = animAlpha;
        spellBg.depth = 0.3F;
        uiPass->DrawQuad(spellBg);

        // Spell name
        CatEngine::Renderer::UIPass::TextDesc nameText;
        nameText.text = spell->name.c_str();
        nameText.x = listX + 15.0F;
        nameText.y = spellY + 8.0F;
        nameText.fontSize = 14.0F;
        if (!isUnlocked) {
            nameText.r = 0.4F;
            nameText.g = 0.4F;
            nameText.b = 0.4F;
        } else {
            nameText.r = 0.9F;
            nameText.g = 0.9F;
            nameText.b = 1.0F;
        }
        nameText.a = animAlpha;
        nameText.depth = 0.4F;
        uiPass->DrawText(nameText);

        // Spell level requirement
        char levelBuf[32];
        snprintf(levelBuf, sizeof(levelBuf), "Lv.%d", spell->requiredLevel);
        CatEngine::Renderer::UIPass::TextDesc levelText;
        levelText.text = levelBuf;
        levelText.x = listX + 15.0F;
        levelText.y = spellY + 28.0F;
        levelText.fontSize = 11.0F;
        levelText.r = 0.5F;
        levelText.g = 0.5F;
        levelText.b = 0.6F;
        levelText.a = animAlpha;
        levelText.depth = 0.4F;
        uiPass->DrawText(levelText);

        // Mana cost
        char manaBuf[32];
        snprintf(manaBuf, sizeof(manaBuf), "%d MP", spell->manaCost);
        CatEngine::Renderer::UIPass::TextDesc manaText;
        manaText.text = manaBuf;
        manaText.x = listX + m_spellListWidth - 60.0F;
        manaText.y = spellY + 8.0F;
        manaText.fontSize = 12.0F;
        manaText.r = 0.3F;
        manaText.g = 0.5F;
        manaText.b = 0.9F;
        manaText.a = animAlpha;
        manaText.depth = 0.4F;
        uiPass->DrawText(manaText);

        // Cooldown overlay
        if (isOnCooldown) {
            float cooldownRemaining = getRemainingCooldown(spell->id);
            CatEngine::Renderer::UIPass::QuadDesc cooldownOverlay;
            cooldownOverlay.x = spellBg.x;
            cooldownOverlay.y = spellBg.y;
            cooldownOverlay.width = spellBg.width;
            cooldownOverlay.height = spellBg.height;
            cooldownOverlay.r = 0.0F;
            cooldownOverlay.g = 0.0F;
            cooldownOverlay.b = 0.0F;
            cooldownOverlay.a = 0.5F * animAlpha;
            cooldownOverlay.depth = 0.45F;
            uiPass->DrawQuad(cooldownOverlay);

            // Cooldown text
            char cdBuf[16];
            snprintf(cdBuf, sizeof(cdBuf), "%.1fs", cooldownRemaining);
            CatEngine::Renderer::UIPass::TextDesc cdText;
            cdText.text = cdBuf;
            cdText.x = spellBg.x + spellBg.width / 2.0F - 15.0F;
            cdText.y = spellBg.y + 18.0F;
            cdText.fontSize = 16.0F;
            cdText.r = 1.0F;
            cdText.g = 0.3F;
            cdText.b = 0.3F;
            cdText.a = animAlpha;
            cdText.depth = 0.5F;
            uiPass->DrawText(cdText);
        }

        // Lock icon for locked spells
        if (!isUnlocked) {
            CatEngine::Renderer::UIPass::TextDesc lockText;
            lockText.text = "[LOCKED]";
            lockText.x = spellBg.x + spellBg.width - 70.0F;
            lockText.y = spellBg.y + 30.0F;
            lockText.fontSize = 10.0F;
            lockText.r = 0.6F;
            lockText.g = 0.3F;
            lockText.b = 0.3F;
            lockText.a = animAlpha;
            lockText.depth = 0.4F;
            uiPass->DrawText(lockText);
        }

        spellY += spellItemHeight + spellSpacing;
    }
}

void SpellbookUI::renderSpellDetails(CatEngine::Renderer::Renderer& renderer) {
    const auto* spell = getSelectedSpell();
    if (!spell) {
        return;
    }

    auto* uiPass = renderer.GetUIPass();
    if (!uiPass) return;

    float animAlpha = m_openAnimation;

    // Details panel
    float detailsX = m_windowX + m_spellListWidth + 40.0F;
    float detailsY = m_windowY + 110.0F;
    float detailsHeight = m_windowHeight - 220.0F;

    CatEngine::Renderer::UIPass::QuadDesc detailsBg;
    detailsBg.x = detailsX;
    detailsBg.y = detailsY;
    detailsBg.width = m_detailsPanelWidth;
    detailsBg.height = detailsHeight;
    detailsBg.r = 0.08F;
    detailsBg.g = 0.10F;
    detailsBg.b = 0.16F;
    detailsBg.a = animAlpha;
    detailsBg.depth = 0.25F;
    uiPass->DrawQuad(detailsBg);

    // Spell name
    CatEngine::Renderer::UIPass::TextDesc nameText;
    nameText.text = spell->name.c_str();
    nameText.x = detailsX + 20.0F;
    nameText.y = detailsY + 15.0F;
    nameText.fontSize = 22.0F;
    nameText.r = 0.8F;
    nameText.g = 0.9F;
    nameText.b = 1.0F;
    nameText.a = animAlpha;
    nameText.depth = 0.4F;
    uiPass->DrawText(nameText);

    // Spell description
    std::string desc = generateSpellDescription(spell);
    CatEngine::Renderer::UIPass::TextDesc descText;
    descText.text = desc.c_str();
    descText.x = detailsX + 20.0F;
    descText.y = detailsY + 50.0F;
    descText.fontSize = 12.0F;
    descText.r = 0.7F;
    descText.g = 0.7F;
    descText.b = 0.8F;
    descText.a = animAlpha;
    descText.depth = 0.4F;
    uiPass->DrawText(descText);

    // Render stats
    renderSpellStats(renderer, spell);

    // Render unlock requirements if locked
    if (!isSpellUnlocked(spell->id)) {
        renderUnlockRequirements(renderer, spell);
    }
}

void SpellbookUI::renderSpellSlots(CatEngine::Renderer::Renderer& renderer) {
    auto* uiPass = renderer.GetUIPass();
    if (!uiPass) return;

    float animAlpha = m_openAnimation;
    float slotSize = 60.0F;
    float slotSpacing = 15.0F;
    float totalWidth = 4 * slotSize + 3 * slotSpacing;
    float slotsX = m_windowX + (m_windowWidth - totalWidth) / 2.0F;
    float slotsY = m_windowY + m_windowHeight - 80.0F;

    // Slots header
    CatEngine::Renderer::UIPass::TextDesc headerText;
    headerText.text = "Spell Slots (1-4)";
    headerText.x = slotsX;
    headerText.y = slotsY - 25.0F;
    headerText.fontSize = 14.0F;
    headerText.r = 0.6F;
    headerText.g = 0.7F;
    headerText.b = 0.8F;
    headerText.a = animAlpha;
    headerText.depth = 0.4F;
    uiPass->DrawText(headerText);

    for (int i = 0; i < 4; ++i) {
        float slotX = slotsX + static_cast<float>(i) * (slotSize + slotSpacing);
        const std::string& spellId = m_spellSlots[i];
        bool hasSpell = !spellId.empty();
        bool isOnCooldown = hasSpell && isSpellOnCooldown(spellId);
        bool isDragTarget = m_isDragging && i == m_dragTargetSlot;

        // Slot background
        CatEngine::Renderer::UIPass::QuadDesc slotBg;
        slotBg.x = slotX;
        slotBg.y = slotsY;
        slotBg.width = slotSize;
        slotBg.height = slotSize;

        if (isDragTarget) {
            slotBg.r = 0.3F;
            slotBg.g = 0.4F;
            slotBg.b = 0.6F;
        } else if (hasSpell) {
            slotBg.r = 0.15F;
            slotBg.g = 0.20F;
            slotBg.b = 0.30F;
        } else {
            slotBg.r = 0.08F;
            slotBg.g = 0.10F;
            slotBg.b = 0.15F;
        }
        slotBg.a = animAlpha;
        slotBg.depth = 0.4F;
        uiPass->DrawQuad(slotBg);

        // Slot border
        CatEngine::Renderer::UIPass::QuadDesc slotBorder;
        slotBorder.x = slotX - 2.0F;
        slotBorder.y = slotsY - 2.0F;
        slotBorder.width = slotSize + 4.0F;
        slotBorder.height = slotSize + 4.0F;
        slotBorder.r = 0.3F;
        slotBorder.g = 0.4F;
        slotBorder.b = 0.6F;
        slotBorder.a = animAlpha * 0.5F;
        slotBorder.depth = 0.38F;
        uiPass->DrawQuad(slotBorder);

        // Slot number
        char slotNumBuf[4];
        snprintf(slotNumBuf, sizeof(slotNumBuf), "%d", i + 1);
        CatEngine::Renderer::UIPass::TextDesc slotNumText;
        slotNumText.text = slotNumBuf;
        slotNumText.x = slotX + 5.0F;
        slotNumText.y = slotsY + 3.0F;
        slotNumText.fontSize = 12.0F;
        slotNumText.r = 0.5F;
        slotNumText.g = 0.6F;
        slotNumText.b = 0.7F;
        slotNumText.a = animAlpha;
        slotNumText.depth = 0.45F;
        uiPass->DrawText(slotNumText);

        // Show spell name if assigned
        if (hasSpell) {
            const auto* spell = m_magicSystem->getSpell(spellId);
            if (spell) {
                // Abbreviated spell name
                CatEngine::Renderer::UIPass::TextDesc spellNameText;
                spellNameText.text = spell->name.substr(0, 8).c_str();
                spellNameText.x = slotX + 5.0F;
                spellNameText.y = slotsY + slotSize - 18.0F;
                spellNameText.fontSize = 10.0F;
                spellNameText.r = 0.8F;
                spellNameText.g = 0.8F;
                spellNameText.b = 0.9F;
                spellNameText.a = animAlpha;
                spellNameText.depth = 0.45F;
                uiPass->DrawText(spellNameText);
            }

            // Cooldown overlay
            if (isOnCooldown) {
                float cooldownRemaining = getRemainingCooldown(spellId);
                CatEngine::Renderer::UIPass::QuadDesc cdOverlay;
                cdOverlay.x = slotX;
                cdOverlay.y = slotsY;
                cdOverlay.width = slotSize;
                cdOverlay.height = slotSize;
                cdOverlay.r = 0.0F;
                cdOverlay.g = 0.0F;
                cdOverlay.b = 0.0F;
                cdOverlay.a = 0.6F * animAlpha;
                cdOverlay.depth = 0.5F;
                uiPass->DrawQuad(cdOverlay);

                char cdBuf[16];
                snprintf(cdBuf, sizeof(cdBuf), "%.1f", cooldownRemaining);
                CatEngine::Renderer::UIPass::TextDesc cdText;
                cdText.text = cdBuf;
                cdText.x = slotX + slotSize / 2.0F - 12.0F;
                cdText.y = slotsY + slotSize / 2.0F - 8.0F;
                cdText.fontSize = 14.0F;
                cdText.r = 1.0F;
                cdText.g = 0.4F;
                cdText.b = 0.4F;
                cdText.a = animAlpha;
                cdText.depth = 0.55F;
                uiPass->DrawText(cdText);
            }
        }
    }
}

void SpellbookUI::renderElementalProgression(CatEngine::Renderer::Renderer& renderer) {
    auto* uiPass = renderer.GetUIPass();
    if (!uiPass) return;

    float animAlpha = m_openAnimation;
    float progX = m_windowX + m_windowWidth - 200.0F;
    float progY = m_windowY + 60.0F;

    // Current element level
    int elementLevel = getPlayerElementalLevel(m_selectedElement);

    // Element name based on selection
    const char* elementNames[] = {"Water", "Air", "Earth", "Fire"};
    int elementIdx = static_cast<int>(m_selectedElement);
    const char* elementName = (elementIdx >= 0 && elementIdx < 4) ? elementNames[elementIdx] : "Unknown";

    // Level display
    char levelBuf[64];
    snprintf(levelBuf, sizeof(levelBuf), "%s Level: %d", elementName, elementLevel);
    CatEngine::Renderer::UIPass::TextDesc levelText;
    levelText.text = levelBuf;
    levelText.x = progX;
    levelText.y = progY;
    levelText.fontSize = 14.0F;
    levelText.r = 0.7F;
    levelText.g = 0.8F;
    levelText.b = 0.9F;
    levelText.a = animAlpha;
    levelText.depth = 0.4F;
    uiPass->DrawText(levelText);

    // XP progress bar
    float barX = progX;
    float barY = progY + 22.0F;
    float barWidth = 150.0F;
    float barHeight = 12.0F;

    // Background
    CatEngine::Renderer::UIPass::QuadDesc barBg;
    barBg.x = barX;
    barBg.y = barY;
    barBg.width = barWidth;
    barBg.height = barHeight;
    barBg.r = 0.1F;
    barBg.g = 0.1F;
    barBg.b = 0.15F;
    barBg.a = animAlpha;
    barBg.depth = 0.4F;
    uiPass->DrawQuad(barBg);

    // Fill (simulated progress - would need actual XP data)
    float xpProgress = 0.65F; // Placeholder
    CatEngine::Renderer::UIPass::QuadDesc barFill;
    barFill.x = barX;
    barFill.y = barY;
    barFill.width = barWidth * xpProgress;
    barFill.height = barHeight;
    barFill.r = 0.3F;
    barFill.g = 0.5F;
    barFill.b = 0.8F;
    barFill.a = animAlpha;
    barFill.depth = 0.42F;
    uiPass->DrawQuad(barFill);

    // Unlocked spells count
    auto spells = getSpellsForSelectedElement();
    int unlockedCount = 0;
    for (const auto* spell : spells) {
        if (isSpellUnlocked(spell->id)) {
            unlockedCount++;
        }
    }

    char spellCountBuf[64];
    snprintf(spellCountBuf, sizeof(spellCountBuf), "Spells: %d/%zu", unlockedCount, spells.size());
    CatEngine::Renderer::UIPass::TextDesc spellCountText;
    spellCountText.text = spellCountBuf;
    spellCountText.x = progX;
    spellCountText.y = progY + 40.0F;
    spellCountText.fontSize = 12.0F;
    spellCountText.r = 0.6F;
    spellCountText.g = 0.7F;
    spellCountText.b = 0.8F;
    spellCountText.a = animAlpha;
    spellCountText.depth = 0.4F;
    uiPass->DrawText(spellCountText);
}

void SpellbookUI::renderSpellStats(CatEngine::Renderer::Renderer& renderer,
                                   const CatGame::ElementalSpell* spell) {
    auto* uiPass = renderer.GetUIPass();
    if (!uiPass || !spell) return;

    float animAlpha = m_openAnimation;
    float detailsX = m_windowX + m_spellListWidth + 40.0F;
    float statsY = m_windowY + 200.0F;
    float statSpacing = 22.0F;

    // Stats header
    CatEngine::Renderer::UIPass::TextDesc headerText;
    headerText.text = "Stats:";
    headerText.x = detailsX + 20.0F;
    headerText.y = statsY;
    headerText.fontSize = 14.0F;
    headerText.r = 0.6F;
    headerText.g = 0.7F;
    headerText.b = 0.8F;
    headerText.a = animAlpha;
    headerText.depth = 0.4F;
    uiPass->DrawText(headerText);

    float statY = statsY + 25.0F;

    // Damage
    if (spell->damage > 0.0F) {
        char damageBuf[64];
        snprintf(damageBuf, sizeof(damageBuf), "Damage: %.0f", spell->damage);
        CatEngine::Renderer::UIPass::TextDesc damageText;
        damageText.text = damageBuf;
        damageText.x = detailsX + 30.0F;
        damageText.y = statY;
        damageText.fontSize = 12.0F;
        damageText.r = 0.9F;
        damageText.g = 0.4F;
        damageText.b = 0.4F;
        damageText.a = animAlpha;
        damageText.depth = 0.4F;
        uiPass->DrawText(damageText);
        statY += statSpacing;
    }

    // Heal amount
    if (spell->healAmount > 0.0F) {
        char healBuf[64];
        snprintf(healBuf, sizeof(healBuf), "Heal: %.0f HP", spell->healAmount);
        CatEngine::Renderer::UIPass::TextDesc healText;
        healText.text = healBuf;
        healText.x = detailsX + 30.0F;
        healText.y = statY;
        healText.fontSize = 12.0F;
        healText.r = 0.4F;
        healText.g = 0.9F;
        healText.b = 0.4F;
        healText.a = animAlpha;
        healText.depth = 0.4F;
        uiPass->DrawText(healText);
        statY += statSpacing;
    }

    // Range
    char rangeBuf[64];
    snprintf(rangeBuf, sizeof(rangeBuf), "Range: %.0fm", spell->range);
    CatEngine::Renderer::UIPass::TextDesc rangeText;
    rangeText.text = rangeBuf;
    rangeText.x = detailsX + 30.0F;
    rangeText.y = statY;
    rangeText.fontSize = 12.0F;
    rangeText.r = 0.7F;
    rangeText.g = 0.7F;
    rangeText.b = 0.8F;
    rangeText.a = animAlpha;
    rangeText.depth = 0.4F;
    uiPass->DrawText(rangeText);
    statY += statSpacing;

    // Cooldown
    char cdBuf[64];
    snprintf(cdBuf, sizeof(cdBuf), "Cooldown: %.1fs", spell->cooldown);
    CatEngine::Renderer::UIPass::TextDesc cdText;
    cdText.text = cdBuf;
    cdText.x = detailsX + 30.0F;
    cdText.y = statY;
    cdText.fontSize = 12.0F;
    cdText.r = 0.7F;
    cdText.g = 0.7F;
    cdText.b = 0.8F;
    cdText.a = animAlpha;
    cdText.depth = 0.4F;
    uiPass->DrawText(cdText);
    statY += statSpacing;

    // Mana cost
    char manaBuf[64];
    snprintf(manaBuf, sizeof(manaBuf), "Mana Cost: %d", spell->manaCost);
    CatEngine::Renderer::UIPass::TextDesc manaText;
    manaText.text = manaBuf;
    manaText.x = detailsX + 30.0F;
    manaText.y = statY;
    manaText.fontSize = 12.0F;
    manaText.r = 0.3F;
    manaText.g = 0.5F;
    manaText.b = 0.9F;
    manaText.a = animAlpha;
    manaText.depth = 0.4F;
    uiPass->DrawText(manaText);
    statY += statSpacing;

    // AOE radius
    if (spell->areaOfEffect > 0.0F) {
        char aoeBuf[64];
        snprintf(aoeBuf, sizeof(aoeBuf), "AOE Radius: %.0fm", spell->areaOfEffect);
        CatEngine::Renderer::UIPass::TextDesc aoeText;
        aoeText.text = aoeBuf;
        aoeText.x = detailsX + 30.0F;
        aoeText.y = statY;
        aoeText.fontSize = 12.0F;
        aoeText.r = 0.8F;
        aoeText.g = 0.6F;
        aoeText.b = 0.2F;
        aoeText.a = animAlpha;
        aoeText.depth = 0.4F;
        uiPass->DrawText(aoeText);
        statY += statSpacing;
    }

    // DOT damage
    if (spell->dotDamage > 0.0F) {
        char dotBuf[64];
        snprintf(dotBuf, sizeof(dotBuf), "DOT: %.0f/s for %.0fs", spell->dotDamage, spell->duration);
        CatEngine::Renderer::UIPass::TextDesc dotText;
        dotText.text = dotBuf;
        dotText.x = detailsX + 30.0F;
        dotText.y = statY;
        dotText.fontSize = 12.0F;
        dotText.r = 0.7F;
        dotText.g = 0.3F;
        dotText.b = 0.7F;
        dotText.a = animAlpha;
        dotText.depth = 0.4F;
        uiPass->DrawText(dotText);
        statY += statSpacing;
    }

    // Knockback
    if (spell->knockbackForce > 0.0F) {
        char kbBuf[64];
        snprintf(kbBuf, sizeof(kbBuf), "Knockback: %.0f", spell->knockbackForce);
        CatEngine::Renderer::UIPass::TextDesc kbText;
        kbText.text = kbBuf;
        kbText.x = detailsX + 30.0F;
        kbText.y = statY;
        kbText.fontSize = 12.0F;
        kbText.r = 0.6F;
        kbText.g = 0.5F;
        kbText.b = 0.3F;
        kbText.a = animAlpha;
        kbText.depth = 0.4F;
        uiPass->DrawText(kbText);
    }
}

void SpellbookUI::renderUnlockRequirements(CatEngine::Renderer::Renderer& renderer,
                                          const CatGame::ElementalSpell* spell) {
    auto* uiPass = renderer.GetUIPass();
    if (!uiPass || !spell) return;

    float animAlpha = m_openAnimation;
    float detailsX = m_windowX + m_spellListWidth + 40.0F;
    float unlockY = m_windowY + m_windowHeight - 150.0F;

    // Unlock requirements box
    CatEngine::Renderer::UIPass::QuadDesc unlockBg;
    unlockBg.x = detailsX + 15.0F;
    unlockBg.y = unlockY;
    unlockBg.width = m_detailsPanelWidth - 30.0F;
    unlockBg.height = 70.0F;
    unlockBg.r = 0.3F;
    unlockBg.g = 0.15F;
    unlockBg.b = 0.15F;
    unlockBg.a = animAlpha * 0.8F;
    unlockBg.depth = 0.45F;
    uiPass->DrawQuad(unlockBg);

    // Header
    CatEngine::Renderer::UIPass::TextDesc headerText;
    headerText.text = "LOCKED";
    headerText.x = detailsX + 25.0F;
    headerText.y = unlockY + 10.0F;
    headerText.fontSize = 14.0F;
    headerText.r = 1.0F;
    headerText.g = 0.4F;
    headerText.b = 0.4F;
    headerText.a = animAlpha;
    headerText.depth = 0.5F;
    uiPass->DrawText(headerText);

    // Current level vs required
    int currentLevel = getPlayerElementalLevel(spell->element);
    char reqBuf[128];
    snprintf(reqBuf, sizeof(reqBuf), "Requires Level %d (Current: %d)", spell->requiredLevel, currentLevel);
    CatEngine::Renderer::UIPass::TextDesc reqText;
    reqText.text = reqBuf;
    reqText.x = detailsX + 25.0F;
    reqText.y = unlockY + 35.0F;
    reqText.fontSize = 12.0F;
    reqText.r = 0.9F;
    reqText.g = 0.7F;
    reqText.b = 0.7F;
    reqText.a = animAlpha;
    reqText.depth = 0.5F;
    uiPass->DrawText(reqText);
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
