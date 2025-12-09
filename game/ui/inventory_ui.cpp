#include "inventory_ui.hpp"
#include "../../engine/core/Logger.hpp"
#include "../../engine/renderer/passes/UIPass.hpp"
#include <algorithm>

namespace Game {

InventoryUI::InventoryUI(Engine::Input& input, CatGame::MerchantSystem* merchantSystem)
    : m_input(input)
    , m_merchantSystem(merchantSystem) {
}

InventoryUI::~InventoryUI() {
    shutdown();
}

bool InventoryUI::initialize() {
    if (m_initialized) {
        Engine::Logger::warn("InventoryUI already initialized");
        return true;
    }

    if (!m_merchantSystem) {
        Engine::Logger::error("InventoryUI: MerchantSystem is null");
        return false;
    }

    // Calculate centered window position with default screen dimensions
    // These will be updated in render() when actual renderer dimensions are available
    m_screenWidth = 1920.0f;
    m_screenHeight = 1080.0f;
    m_windowX = (m_screenWidth - m_windowWidth) / 2.0f;
    m_windowY = (m_screenHeight - m_windowHeight) / 2.0f;

    // Initialize slots
    m_slots.resize(m_capacity);
    for (int i = 0; i < m_capacity; i++) {
        m_slots[i].slotIndex = i;
    }

    // Build initial slot list from inventory
    rebuildSlots();

    m_initialized = true;
    Engine::Logger::info("InventoryUI initialized successfully");
    return true;
}

void InventoryUI::shutdown() {
    if (!m_initialized) {
        return;
    }

    m_slots.clear();
    m_selectedSlot = -1;

    m_initialized = false;
    Engine::Logger::info("InventoryUI shutdown");
}

void InventoryUI::update(float deltaTime) {
    if (!m_initialized) {
        return;
    }

    // Update open/close animation
    if (m_isOpen) {
        m_openAnimation = std::min(1.0f, m_openAnimation + deltaTime * m_openAnimSpeed);
    } else {
        m_openAnimation = std::max(0.0f, m_openAnimation - deltaTime * m_openAnimSpeed);
    }

    // Update input cooldown
    if (m_inputCooldown > 0.0f) {
        m_inputCooldown -= deltaTime;
    }

    // Update tooltip timer
    if (m_hoveredSlot >= 0) {
        m_tooltipTimer += deltaTime;
        if (m_tooltipTimer >= m_tooltipDelay) {
            m_showTooltip = true;
        }
    } else {
        m_tooltipTimer = 0.0f;
        m_showTooltip = false;
    }

    // Handle input if open
    if (m_isOpen && m_openAnimation >= 0.99f) {
        handleInput();
        handleMouseInput();
    }

    // Rebuild slots if inventory changed
    rebuildSlots();
}

void InventoryUI::render(CatEngine::Renderer::Renderer& renderer) {
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

    // Render category filters
    renderCategoryFilters(renderer);

    // Render inventory grid
    renderInventoryGrid(renderer);

    // Render currency
    renderCurrency(renderer);

    // Render capacity bar
    renderCapacity(renderer);

    // Render action buttons
    renderActionButtons(renderer);

    // Render tooltip (if hovering)
    if (m_showTooltip && m_hoveredSlot >= 0) {
        renderItemTooltip(renderer);
    }

    // Render dragged item (follows cursor)
    if (m_isDragging) {
        renderDraggedItem(renderer);
    }
}

// ============================================================================
// Visibility
// ============================================================================

void InventoryUI::open() {
    if (!m_isOpen) {
        m_isOpen = true;
        rebuildSlots();
        Engine::Logger::debug("InventoryUI opened");
    }
}

void InventoryUI::close() {
    if (m_isOpen) {
        m_isOpen = false;
        cancelDrag();  // Cancel any active drag
        Engine::Logger::debug("InventoryUI closed");
    }
}

void InventoryUI::toggle() {
    if (m_isOpen) {
        close();
    } else {
        open();
    }
}

// ============================================================================
// Item Interaction
// ============================================================================

void InventoryUI::selectSlot(int slotIndex) {
    if (slotIndex >= 0 && slotIndex < m_capacity) {
        m_selectedSlot = slotIndex;
    }
}

bool InventoryUI::useSelectedItem() {
    if (m_selectedSlot < 0 || m_selectedSlot >= static_cast<int>(m_slots.size())) {
        return false;
    }

    const auto& slot = m_slots[m_selectedSlot];
    if (slot.isEmpty()) {
        return false;
    }

    const auto* item = m_merchantSystem->getInventoryItem(slot.itemId);
    if (!item || !canUseItem(item)) {
        return false;
    }

    // Item use logic based on item type
    Engine::Logger::info("InventoryUI: Used item '{}'", item->name);

    switch (item->category) {
        case CatGame::ItemCategory::Consumable: {
            // Apply consumable effect based on item stats
            // Common effects: health restore, mana restore, buffs
            auto statIt = item->stats.find("healthRestore");
            if (statIt != item->stats.end()) {
                Engine::Logger::info("InventoryUI: Restored {} health", statIt->second);
                // Would call: playerSystem->restoreHealth(statIt->second);
            }

            statIt = item->stats.find("manaRestore");
            if (statIt != item->stats.end()) {
                Engine::Logger::info("InventoryUI: Restored {} mana", statIt->second);
                // Would call: playerSystem->restoreMana(statIt->second);
            }

            statIt = item->stats.find("buffDuration");
            if (statIt != item->stats.end()) {
                Engine::Logger::info("InventoryUI: Applied buff for {}s", statIt->second);
                // Would call: playerSystem->applyBuff(item->itemId, statIt->second);
            }

            // Remove one from inventory after use
            m_merchantSystem->removeItem(slot.itemId, 1);
            rebuildSlots();
            break;
        }

        case CatGame::ItemCategory::Weapon:
        case CatGame::ItemCategory::Armor:
            // Equip instead of use for equipment
            return equipSelectedItem();

        case CatGame::ItemCategory::Quest:
            // Quest items typically cannot be used directly
            Engine::Logger::warn("InventoryUI: Quest items cannot be used directly");
            return false;

        case CatGame::ItemCategory::Material:
        case CatGame::ItemCategory::Misc:
        default:
            // Materials and misc items have no direct use
            Engine::Logger::warn("InventoryUI: This item cannot be used");
            return false;
    }

    return true;
}

bool InventoryUI::dropSelectedItem(int quantity) {
    if (m_selectedSlot < 0 || m_selectedSlot >= static_cast<int>(m_slots.size())) {
        return false;
    }

    const auto& slot = m_slots[m_selectedSlot];
    if (slot.isEmpty()) {
        return false;
    }

    const auto* item = m_merchantSystem->getInventoryItem(slot.itemId);
    if (!item || !item->canDrop) {
        return false;
    }

    int dropQuantity = (quantity <= 0) ? slot.quantity : std::min(quantity, slot.quantity);

    // Remove from inventory
    bool success = m_merchantSystem->removeItem(slot.itemId, dropQuantity);
    if (success) {
        Engine::Logger::info("InventoryUI: Dropped {} x{}", item->name, dropQuantity);
        rebuildSlots();
    }

    return success;
}

bool InventoryUI::equipSelectedItem() {
    if (m_selectedSlot < 0 || m_selectedSlot >= static_cast<int>(m_slots.size())) {
        return false;
    }

    const auto& slot = m_slots[m_selectedSlot];
    if (slot.isEmpty()) {
        return false;
    }

    const auto* item = m_merchantSystem->getInventoryItem(slot.itemId);
    if (!item || !canEquipItem(item)) {
        return false;
    }

    // Equipment system - determine slot and equip item
    std::string equipSlot;
    switch (item->category) {
        case CatGame::ItemCategory::Weapon:
            equipSlot = "weapon";
            break;
        case CatGame::ItemCategory::Armor: {
            // Determine armor slot from item stats or type
            auto slotIt = item->stats.find("armorSlot");
            if (slotIt != item->stats.end()) {
                int slotType = static_cast<int>(slotIt->second);
                switch (slotType) {
                    case 0: equipSlot = "head"; break;
                    case 1: equipSlot = "chest"; break;
                    case 2: equipSlot = "legs"; break;
                    case 3: equipSlot = "feet"; break;
                    case 4: equipSlot = "hands"; break;
                    default: equipSlot = "chest"; break;
                }
            } else {
                equipSlot = "chest";  // Default armor slot
            }
            break;
        }
        default:
            Engine::Logger::warn("InventoryUI: Item '{}' cannot be equipped", item->name);
            return false;
    }

    // Unequip current item in slot if any
    auto currentEquipIt = m_merchantSystem->getEquippedItems().find(equipSlot);
    if (currentEquipIt != m_merchantSystem->getEquippedItems().end() &&
        !currentEquipIt->second.empty()) {
        Engine::Logger::debug("InventoryUI: Unequipping '{}' from slot '{}'",
                             currentEquipIt->second, equipSlot);
    }

    // Equip the new item
    m_merchantSystem->equipItem(slot.itemId, equipSlot);
    Engine::Logger::info("InventoryUI: Equipped '{}' in slot '{}'", item->name, equipSlot);
    rebuildSlots();

    return true;
}

const CatGame::InventoryItem* InventoryUI::getItemInSlot(int slotIndex) const {
    if (slotIndex < 0 || slotIndex >= static_cast<int>(m_slots.size())) {
        return nullptr;
    }

    const auto& slot = m_slots[slotIndex];
    if (slot.isEmpty()) {
        return nullptr;
    }

    return m_merchantSystem->getInventoryItem(slot.itemId);
}

// ============================================================================
// Drag and Drop
// ============================================================================

void InventoryUI::startDrag(int slotIndex) {
    if (slotIndex < 0 || slotIndex >= static_cast<int>(m_slots.size())) {
        return;
    }

    const auto& slot = m_slots[slotIndex];
    if (slot.isEmpty()) {
        return;
    }

    m_isDragging = true;
    m_draggedSlot = slotIndex;
    m_dragStartPos = m_currentMousePos;

    Engine::Logger::debug("InventoryUI: Started dragging slot {}", slotIndex);
}

void InventoryUI::endDrag(int targetSlot) {
    if (!m_isDragging) {
        return;
    }

    if (targetSlot >= 0 && targetSlot < static_cast<int>(m_slots.size())) {
        if (targetSlot != m_draggedSlot) {
            swapSlots(m_draggedSlot, targetSlot);
            Engine::Logger::debug("InventoryUI: Swapped slots {} and {}", m_draggedSlot, targetSlot);
        }
    }

    m_isDragging = false;
    m_draggedSlot = -1;
}

void InventoryUI::cancelDrag() {
    if (m_isDragging) {
        Engine::Logger::debug("InventoryUI: Cancelled drag");
        m_isDragging = false;
        m_draggedSlot = -1;
    }
}

// ============================================================================
// Filtering and Sorting
// ============================================================================

void InventoryUI::setCategory(CatGame::ItemCategory category) {
    m_categoryFilter = category;
    rebuildSlots();
    Engine::Logger::debug("InventoryUI: Set category filter");
}

void InventoryUI::clearCategoryFilter() {
    m_categoryFilter.reset();
    rebuildSlots();
    Engine::Logger::debug("InventoryUI: Cleared category filter");
}

void InventoryUI::sortByName() {
    m_sortMode = SortMode::Name;
    rebuildSlots();
}

void InventoryUI::sortByRarity() {
    m_sortMode = SortMode::Rarity;
    rebuildSlots();
}

void InventoryUI::sortByType() {
    m_sortMode = SortMode::Type;
    rebuildSlots();
}

void InventoryUI::sortByQuantity() {
    m_sortMode = SortMode::Quantity;
    rebuildSlots();
}

// ============================================================================
// Inventory Configuration
// ============================================================================

void InventoryUI::setCapacity(int capacity) {
    m_capacity = std::max(1, capacity);
    m_slots.resize(m_capacity);
    for (int i = 0; i < m_capacity; i++) {
        m_slots[i].slotIndex = i;
    }
    rebuildSlots();
}

int InventoryUI::getUsedSlots() const {
    int used = 0;
    for (const auto& slot : m_slots) {
        if (!slot.isEmpty()) {
            used++;
        }
    }
    return used;
}

int InventoryUI::getFreeSlots() const {
    return m_capacity - getUsedSlots();
}

bool InventoryUI::isFull() const {
    return getFreeSlots() == 0;
}

// ============================================================================
// Input Handling
// ============================================================================

void InventoryUI::handleInput() {
    if (m_inputCooldown > 0.0f) {
        return;
    }

    using Key = Engine::Input::Key;

    // Handle keyboard navigation and actions
    if (m_input.isKeyPressed(Key::Escape) || m_input.isKeyPressed(Key::I)) {
        close();
        m_inputCooldown = INPUT_COOLDOWN_TIME;
    } else if (m_input.isKeyPressed(Key::Enter) || m_input.isKeyPressed(Key::E)) {
        useSelectedItem();
        m_inputCooldown = INPUT_COOLDOWN_TIME;
    } else if (m_input.isKeyPressed(Key::Delete) || m_input.isKeyPressed(Key::X)) {
        dropSelectedItem();
        m_inputCooldown = INPUT_COOLDOWN_TIME;
    } else if (m_input.isKeyPressed(Key::Q)) {
        equipSelectedItem();
        m_inputCooldown = INPUT_COOLDOWN_TIME;
    } else if (m_input.isKeyPressed(Key::Up) || m_input.isKeyPressed(Key::W)) {
        // Navigate up in grid
        if (m_selectedSlot >= m_gridColumns) {
            selectSlot(m_selectedSlot - m_gridColumns);
        }
        m_inputCooldown = INPUT_COOLDOWN_TIME;
    } else if (m_input.isKeyPressed(Key::Down) || m_input.isKeyPressed(Key::S)) {
        // Navigate down in grid
        if (m_selectedSlot + m_gridColumns < m_capacity) {
            selectSlot(m_selectedSlot + m_gridColumns);
        }
        m_inputCooldown = INPUT_COOLDOWN_TIME;
    } else if (m_input.isKeyPressed(Key::Left) || m_input.isKeyPressed(Key::A)) {
        // Navigate left
        if (m_selectedSlot > 0) {
            selectSlot(m_selectedSlot - 1);
        }
        m_inputCooldown = INPUT_COOLDOWN_TIME;
    } else if (m_input.isKeyPressed(Key::Right) || m_input.isKeyPressed(Key::D)) {
        // Navigate right
        if (m_selectedSlot < m_capacity - 1) {
            selectSlot(m_selectedSlot + 1);
        }
        m_inputCooldown = INPUT_COOLDOWN_TIME;
    }
}

void InventoryUI::handleMouseInput() {
    using MouseButton = Engine::Input::MouseButton;

    // Track mouse position
    double mouseX = 0.0, mouseY = 0.0;
    m_input.getMousePosition(mouseX, mouseY);
    m_currentMousePos = {static_cast<float>(mouseX), static_cast<float>(mouseY)};

    // Determine which slot mouse is over
    int slotUnderCursor = getSlotAtPosition(m_currentMousePos);
    m_hoveredSlot = slotUnderCursor;

    // Handle left mouse button for selection and drag
    if (m_input.isMouseButtonPressed(MouseButton::Left)) {
        if (slotUnderCursor >= 0) {
            if (!m_isDragging) {
                // Start potential drag or just select
                selectSlot(slotUnderCursor);
                startDrag(slotUnderCursor);
            }
        }
    } else if (m_input.isMouseButtonReleased(MouseButton::Left)) {
        if (m_isDragging) {
            // End drag - drop on target slot or cancel
            endDrag(slotUnderCursor);
        }
    }

    // Handle right mouse button for context menu / quick use
    if (m_input.isMouseButtonPressed(MouseButton::Right)) {
        if (slotUnderCursor >= 0) {
            selectSlot(slotUnderCursor);
            useSelectedItem();
        }
    }
}

// ============================================================================
// Private Rendering Methods
// ============================================================================

void InventoryUI::renderBackground(CatEngine::Renderer::Renderer& renderer) {
    auto* uiPass = renderer.GetUIPass();
    if (!uiPass) return;

    // Calculate animated window position and size
    float animScale = m_openAnimation;
    float animAlpha = m_openAnimation;

    float centerX = m_windowX + m_windowWidth / 2.0F;
    float centerY = m_windowY + m_windowHeight / 2.0F;

    float scaledWidth = m_windowWidth * animScale;
    float scaledHeight = m_windowHeight * animScale;
    float scaledX = centerX - scaledWidth / 2.0F;
    float scaledY = centerY - scaledHeight / 2.0F;

    // Draw semi-transparent dark background overlay (dims the game behind)
    CatEngine::Renderer::UIPass::QuadDesc overlay;
    overlay.x = 0.0F;
    overlay.y = 0.0F;
    overlay.width = static_cast<float>(renderer.GetWidth());
    overlay.height = static_cast<float>(renderer.GetHeight());
    overlay.r = 0.0F;
    overlay.g = 0.0F;
    overlay.b = 0.0F;
    overlay.a = 0.5F * animAlpha;
    overlay.depth = 0.0F;
    uiPass->DrawQuad(overlay);

    // Draw inventory window background
    CatEngine::Renderer::UIPass::QuadDesc windowBg;
    windowBg.x = scaledX;
    windowBg.y = scaledY;
    windowBg.width = scaledWidth;
    windowBg.height = scaledHeight;
    windowBg.r = 0.15F;
    windowBg.g = 0.12F;
    windowBg.b = 0.10F;
    windowBg.a = 0.95F * animAlpha;
    windowBg.depth = 0.1F;
    uiPass->DrawQuad(windowBg);

    // Draw window border
    CatEngine::Renderer::UIPass::QuadDesc border;
    float borderWidth = 3.0F;
    // Top border
    border.x = scaledX;
    border.y = scaledY;
    border.width = scaledWidth;
    border.height = borderWidth;
    border.r = 0.6F;
    border.g = 0.5F;
    border.b = 0.3F;
    border.a = animAlpha;
    border.depth = 0.2F;
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
    titleBar.height = 40.0F * animScale;
    titleBar.r = 0.25F;
    titleBar.g = 0.20F;
    titleBar.b = 0.15F;
    titleBar.a = animAlpha;
    titleBar.depth = 0.2F;
    uiPass->DrawQuad(titleBar);

    // Draw title text
    CatEngine::Renderer::UIPass::TextDesc titleText;
    titleText.text = "Inventory";
    titleText.x = scaledX + scaledWidth / 2.0F - 50.0F;
    titleText.y = scaledY + borderWidth + 10.0F * animScale;
    titleText.fontSize = 24.0F * animScale;
    titleText.r = 1.0F;
    titleText.g = 0.9F;
    titleText.b = 0.7F;
    titleText.a = animAlpha;
    titleText.depth = 0.3F;
    uiPass->DrawText(titleText);
}

void InventoryUI::renderInventoryGrid(CatEngine::Renderer::Renderer& renderer) {
    auto* uiPass = renderer.GetUIPass();
    if (!uiPass) return;

    float animAlpha = m_openAnimation;

    // Render each inventory slot
    for (int i = 0; i < m_capacity; ++i) {
        Engine::vec2 slotPos = getSlotPosition(i);
        const auto& slot = m_slots[i];

        // Draw slot background
        CatEngine::Renderer::UIPass::QuadDesc slotBg;
        slotBg.x = slotPos.x;
        slotBg.y = slotPos.y;
        slotBg.width = m_slotSize;
        slotBg.height = m_slotSize;

        // Highlight selected slot
        if (i == m_selectedSlot) {
            slotBg.r = 0.4F;
            slotBg.g = 0.35F;
            slotBg.b = 0.25F;
        } else if (i == m_hoveredSlot) {
            slotBg.r = 0.3F;
            slotBg.g = 0.28F;
            slotBg.b = 0.22F;
        } else {
            slotBg.r = 0.2F;
            slotBg.g = 0.18F;
            slotBg.b = 0.15F;
        }
        slotBg.a = animAlpha;
        slotBg.depth = 0.3F;
        uiPass->DrawQuad(slotBg);

        // Draw slot border
        CatEngine::Renderer::UIPass::QuadDesc slotBorder;
        slotBorder.x = slotPos.x;
        slotBorder.y = slotPos.y;
        slotBorder.width = m_slotSize;
        slotBorder.height = 2.0F;
        slotBorder.r = 0.4F;
        slotBorder.g = 0.35F;
        slotBorder.b = 0.25F;
        slotBorder.a = animAlpha;
        slotBorder.depth = 0.35F;
        uiPass->DrawQuad(slotBorder);

        // If slot has an item, draw item info
        if (!slot.isEmpty()) {
            const auto* item = m_merchantSystem->getInventoryItem(slot.itemId);
            if (item) {
                // Draw rarity-colored border
                Engine::vec4 rarityColor = getRarityColor(item->rarity);
                CatEngine::Renderer::UIPass::QuadDesc rarityBorder;
                rarityBorder.x = slotPos.x + 2.0F;
                rarityBorder.y = slotPos.y + 2.0F;
                rarityBorder.width = m_slotSize - 4.0F;
                rarityBorder.height = m_slotSize - 4.0F;
                rarityBorder.r = rarityColor.x;
                rarityBorder.g = rarityColor.y;
                rarityBorder.b = rarityColor.z;
                rarityBorder.a = 0.3F * animAlpha;
                rarityBorder.depth = 0.36F;
                uiPass->DrawQuad(rarityBorder);

                // Draw quantity if stacked
                if (slot.quantity > 1) {
                    CatEngine::Renderer::UIPass::TextDesc quantityText;
                    std::string qtyStr = std::to_string(slot.quantity);
                    quantityText.text = qtyStr.c_str();
                    quantityText.x = slotPos.x + m_slotSize - 20.0F;
                    quantityText.y = slotPos.y + m_slotSize - 18.0F;
                    quantityText.fontSize = 14.0F;
                    quantityText.r = 1.0F;
                    quantityText.g = 1.0F;
                    quantityText.b = 1.0F;
                    quantityText.a = animAlpha;
                    quantityText.depth = 0.4F;
                    uiPass->DrawText(quantityText);
                }
            }
        }
    }
}

void InventoryUI::renderCategoryFilters(CatEngine::Renderer::Renderer& renderer) {
    auto* uiPass = renderer.GetUIPass();
    if (!uiPass) return;

    float animAlpha = m_openAnimation;
    float filterY = m_windowY + 60.0F;
    float filterX = m_windowX + 20.0F;
    float buttonWidth = 100.0F;
    float buttonHeight = 30.0F;
    float buttonSpacing = 10.0F;

    const char* categories[] = {"All", "Weapons", "Armor", "Consumables", "Materials", "Quest"};
    int numCategories = 6;

    for (int i = 0; i < numCategories; ++i) {
        CatEngine::Renderer::UIPass::QuadDesc button;
        button.x = filterX + static_cast<float>(i) * (buttonWidth + buttonSpacing);
        button.y = filterY;
        button.width = buttonWidth;
        button.height = buttonHeight;

        // Highlight active category
        bool isActive = (!m_categoryFilter.has_value() && i == 0) ||
                       (m_categoryFilter.has_value() && static_cast<int>(m_categoryFilter.value()) == i - 1);
        if (isActive) {
            button.r = 0.5F;
            button.g = 0.4F;
            button.b = 0.2F;
        } else {
            button.r = 0.25F;
            button.g = 0.22F;
            button.b = 0.18F;
        }
        button.a = animAlpha;
        button.depth = 0.35F;
        uiPass->DrawQuad(button);

        // Draw button text
        CatEngine::Renderer::UIPass::TextDesc buttonText;
        buttonText.text = categories[i];
        buttonText.x = button.x + 10.0F;
        buttonText.y = button.y + 8.0F;
        buttonText.fontSize = 14.0F;
        buttonText.r = 1.0F;
        buttonText.g = 0.95F;
        buttonText.b = 0.85F;
        buttonText.a = animAlpha;
        buttonText.depth = 0.4F;
        uiPass->DrawText(buttonText);
    }
}

void InventoryUI::renderItemTooltip(CatEngine::Renderer::Renderer& renderer) {
    auto* uiPass = renderer.GetUIPass();
    if (!uiPass || m_hoveredSlot < 0 || m_hoveredSlot >= static_cast<int>(m_slots.size())) return;

    const auto& slot = m_slots[m_hoveredSlot];
    if (slot.isEmpty()) return;

    const auto* item = m_merchantSystem->getInventoryItem(slot.itemId);
    if (!item) return;

    float animAlpha = m_openAnimation;
    float tooltipWidth = 250.0F;
    float tooltipHeight = 150.0F;

    // Position tooltip near mouse cursor
    float tooltipX = m_currentMousePos.x + 15.0F;
    float tooltipY = m_currentMousePos.y + 15.0F;

    // Keep tooltip on screen
    float screenWidth = static_cast<float>(renderer.GetWidth());
    float screenHeight = static_cast<float>(renderer.GetHeight());
    if (tooltipX + tooltipWidth > screenWidth) {
        tooltipX = m_currentMousePos.x - tooltipWidth - 15.0F;
    }
    if (tooltipY + tooltipHeight > screenHeight) {
        tooltipY = m_currentMousePos.y - tooltipHeight - 15.0F;
    }

    // Draw tooltip background
    CatEngine::Renderer::UIPass::QuadDesc tooltipBg;
    tooltipBg.x = tooltipX;
    tooltipBg.y = tooltipY;
    tooltipBg.width = tooltipWidth;
    tooltipBg.height = tooltipHeight;
    tooltipBg.r = 0.1F;
    tooltipBg.g = 0.08F;
    tooltipBg.b = 0.06F;
    tooltipBg.a = 0.95F * animAlpha;
    tooltipBg.depth = 0.8F;
    uiPass->DrawQuad(tooltipBg);

    // Draw item name with rarity color
    Engine::vec4 rarityColor = getRarityColor(item->rarity);
    CatEngine::Renderer::UIPass::TextDesc nameText;
    nameText.text = item->name.c_str();
    nameText.x = tooltipX + 10.0F;
    nameText.y = tooltipY + 10.0F;
    nameText.fontSize = 18.0F;
    nameText.r = rarityColor.x;
    nameText.g = rarityColor.y;
    nameText.b = rarityColor.z;
    nameText.a = animAlpha;
    nameText.depth = 0.85F;
    uiPass->DrawText(nameText);

    // Draw description
    CatEngine::Renderer::UIPass::TextDesc descText;
    descText.text = item->description.c_str();
    descText.x = tooltipX + 10.0F;
    descText.y = tooltipY + 35.0F;
    descText.fontSize = 12.0F;
    descText.r = 0.8F;
    descText.g = 0.8F;
    descText.b = 0.8F;
    descText.a = animAlpha;
    descText.depth = 0.85F;
    uiPass->DrawText(descText);

    // Draw sell price
    std::string priceStr = "Sell: " + std::to_string(item->sellPrice) + " gold";
    CatEngine::Renderer::UIPass::TextDesc priceText;
    priceText.text = priceStr.c_str();
    priceText.x = tooltipX + 10.0F;
    priceText.y = tooltipY + tooltipHeight - 25.0F;
    priceText.fontSize = 12.0F;
    priceText.r = 1.0F;
    priceText.g = 0.85F;
    priceText.b = 0.3F;
    priceText.a = animAlpha;
    priceText.depth = 0.85F;
    uiPass->DrawText(priceText);
}

void InventoryUI::renderCurrency(CatEngine::Renderer::Renderer& renderer) {
    auto* uiPass = renderer.GetUIPass();
    if (!uiPass) return;

    float animAlpha = m_openAnimation;

    // Draw currency display in bottom right of inventory window
    float currencyX = m_windowX + m_windowWidth - 180.0F;
    float currencyY = m_windowY + m_windowHeight - 50.0F;

    // Draw background
    CatEngine::Renderer::UIPass::QuadDesc currencyBg;
    currencyBg.x = currencyX;
    currencyBg.y = currencyY;
    currencyBg.width = 160.0F;
    currencyBg.height = 35.0F;
    currencyBg.r = 0.2F;
    currencyBg.g = 0.18F;
    currencyBg.b = 0.12F;
    currencyBg.a = animAlpha;
    currencyBg.depth = 0.4F;
    uiPass->DrawQuad(currencyBg);

    // Draw currency text
    int playerCurrency = m_merchantSystem->getPlayerCurrency();
    std::string currencyStr = std::to_string(playerCurrency) + " Gold";
    CatEngine::Renderer::UIPass::TextDesc currencyText;
    currencyText.text = currencyStr.c_str();
    currencyText.x = currencyX + 10.0F;
    currencyText.y = currencyY + 8.0F;
    currencyText.fontSize = 16.0F;
    currencyText.r = 1.0F;
    currencyText.g = 0.85F;
    currencyText.b = 0.3F;
    currencyText.a = animAlpha;
    currencyText.depth = 0.45F;
    uiPass->DrawText(currencyText);
}

void InventoryUI::renderCapacity(CatEngine::Renderer::Renderer& renderer) {
    auto* uiPass = renderer.GetUIPass();
    if (!uiPass) return;

    float animAlpha = m_openAnimation;

    // Draw capacity bar at bottom of inventory window
    float barX = m_windowX + 20.0F;
    float barY = m_windowY + m_windowHeight - 50.0F;
    float barWidth = 200.0F;
    float barHeight = 20.0F;

    // Draw background bar
    CatEngine::Renderer::UIPass::QuadDesc barBg;
    barBg.x = barX;
    barBg.y = barY;
    barBg.width = barWidth;
    barBg.height = barHeight;
    barBg.r = 0.15F;
    barBg.g = 0.12F;
    barBg.b = 0.1F;
    barBg.a = animAlpha;
    barBg.depth = 0.4F;
    uiPass->DrawQuad(barBg);

    // Draw filled portion
    int usedSlots = getUsedSlots();
    float fillPercent = static_cast<float>(usedSlots) / static_cast<float>(m_capacity);
    CatEngine::Renderer::UIPass::QuadDesc barFill;
    barFill.x = barX;
    barFill.y = barY;
    barFill.width = barWidth * fillPercent;
    barFill.height = barHeight;

    // Color based on fullness
    if (fillPercent > 0.9F) {
        barFill.r = 0.8F;
        barFill.g = 0.2F;
        barFill.b = 0.2F;
    } else if (fillPercent > 0.7F) {
        barFill.r = 0.8F;
        barFill.g = 0.6F;
        barFill.b = 0.2F;
    } else {
        barFill.r = 0.3F;
        barFill.g = 0.6F;
        barFill.b = 0.3F;
    }
    barFill.a = animAlpha;
    barFill.depth = 0.42F;
    uiPass->DrawQuad(barFill);

    // Draw text
    std::string capacityStr = std::to_string(usedSlots) + " / " + std::to_string(m_capacity);
    CatEngine::Renderer::UIPass::TextDesc capacityText;
    capacityText.text = capacityStr.c_str();
    capacityText.x = barX + barWidth / 2.0F - 25.0F;
    capacityText.y = barY + 3.0F;
    capacityText.fontSize = 12.0F;
    capacityText.r = 1.0F;
    capacityText.g = 1.0F;
    capacityText.b = 1.0F;
    capacityText.a = animAlpha;
    capacityText.depth = 0.45F;
    uiPass->DrawText(capacityText);
}

void InventoryUI::renderActionButtons(CatEngine::Renderer::Renderer& renderer) {
    auto* uiPass = renderer.GetUIPass();
    if (!uiPass) return;

    float animAlpha = m_openAnimation;
    float buttonX = m_windowX + m_windowWidth - 120.0F;
    float buttonY = m_windowY + 100.0F;
    float buttonWidth = 100.0F;
    float buttonHeight = 30.0F;
    float buttonSpacing = 10.0F;

    const char* buttons[] = {"Use", "Equip", "Drop", "Sort"};
    int numButtons = 4;

    for (int i = 0; i < numButtons; ++i) {
        CatEngine::Renderer::UIPass::QuadDesc button;
        button.x = buttonX;
        button.y = buttonY + static_cast<float>(i) * (buttonHeight + buttonSpacing);
        button.width = buttonWidth;
        button.height = buttonHeight;
        button.r = 0.3F;
        button.g = 0.25F;
        button.b = 0.2F;
        button.a = animAlpha;
        button.depth = 0.35F;
        uiPass->DrawQuad(button);

        // Draw button text
        CatEngine::Renderer::UIPass::TextDesc buttonText;
        buttonText.text = buttons[i];
        buttonText.x = button.x + 25.0F;
        buttonText.y = button.y + 8.0F;
        buttonText.fontSize = 14.0F;
        buttonText.r = 1.0F;
        buttonText.g = 0.95F;
        buttonText.b = 0.85F;
        buttonText.a = animAlpha;
        buttonText.depth = 0.4F;
        uiPass->DrawText(buttonText);
    }
}

void InventoryUI::renderDraggedItem(CatEngine::Renderer::Renderer& renderer) {
    auto* uiPass = renderer.GetUIPass();
    if (!uiPass || m_draggedSlot < 0 || m_draggedSlot >= static_cast<int>(m_slots.size())) return;

    const auto& slot = m_slots[m_draggedSlot];
    if (slot.isEmpty()) return;

    float animAlpha = m_openAnimation * 0.7F; // Semi-transparent when dragging

    // Draw item icon at cursor position
    CatEngine::Renderer::UIPass::QuadDesc draggedItem;
    draggedItem.x = m_currentMousePos.x - m_slotSize / 2.0F;
    draggedItem.y = m_currentMousePos.y - m_slotSize / 2.0F;
    draggedItem.width = m_slotSize;
    draggedItem.height = m_slotSize;
    draggedItem.r = 0.5F;
    draggedItem.g = 0.45F;
    draggedItem.b = 0.35F;
    draggedItem.a = animAlpha;
    draggedItem.depth = 0.9F; // Above everything else
    uiPass->DrawQuad(draggedItem);

    // Draw quantity if stacked
    if (slot.quantity > 1) {
        std::string qtyStr = std::to_string(slot.quantity);
        CatEngine::Renderer::UIPass::TextDesc quantityText;
        quantityText.text = qtyStr.c_str();
        quantityText.x = m_currentMousePos.x + m_slotSize / 2.0F - 20.0F;
        quantityText.y = m_currentMousePos.y + m_slotSize / 2.0F - 18.0F;
        quantityText.fontSize = 14.0F;
        quantityText.r = 1.0F;
        quantityText.g = 1.0F;
        quantityText.b = 1.0F;
        quantityText.a = animAlpha;
        quantityText.depth = 0.95F;
        uiPass->DrawText(quantityText);
    }
}

// ============================================================================
// Private Helper Methods
// ============================================================================

std::vector<InventorySlot> InventoryUI::getFilteredSlots() const {
    std::vector<InventorySlot> filtered;

    for (const auto& slot : m_slots) {
        if (slot.isEmpty()) {
            filtered.push_back(slot);
            continue;
        }

        const auto* item = m_merchantSystem->getInventoryItem(slot.itemId);
        if (!item) continue;

        // Apply category filter
        if (m_categoryFilter.has_value() && item->category != m_categoryFilter.value()) {
            continue;
        }

        filtered.push_back(slot);
    }

    return filtered;
}

void InventoryUI::rebuildSlots() {
    const auto& inventory = m_merchantSystem->getInventory();

    // Clear current slots
    for (auto& slot : m_slots) {
        slot.itemId.clear();
        slot.quantity = 0;
    }

    // Populate slots from inventory
    int slotIndex = 0;
    std::vector<const CatGame::InventoryItem*> items;

    for (const auto& pair : inventory) {
        items.push_back(&pair.second);
    }

    // Apply sorting
    switch (m_sortMode) {
        case SortMode::Name:
            std::sort(items.begin(), items.end(),
                [](const auto* a, const auto* b) { return a->name < b->name; });
            break;
        case SortMode::Rarity:
            std::sort(items.begin(), items.end(),
                [](const auto* a, const auto* b) {
                    return static_cast<int>(a->rarity) > static_cast<int>(b->rarity);
                });
            break;
        case SortMode::Type:
            std::sort(items.begin(), items.end(),
                [](const auto* a, const auto* b) {
                    return static_cast<int>(a->category) < static_cast<int>(b->category);
                });
            break;
        case SortMode::Quantity:
            std::sort(items.begin(), items.end(),
                [](const auto* a, const auto* b) { return a->quantity > b->quantity; });
            break;
        case SortMode::None:
        default:
            // No sorting
            break;
    }

    // Fill slots with sorted items
    for (const auto* item : items) {
        if (slotIndex >= m_capacity) break;

        // Apply category filter
        if (m_categoryFilter.has_value() && item->category != m_categoryFilter.value()) {
            continue;
        }

        m_slots[slotIndex].itemId = item->itemId;
        m_slots[slotIndex].quantity = item->quantity;
        slotIndex++;
    }
}

std::string InventoryUI::getItemIconPath(const std::string& itemId) const {
    return "assets/textures/ui/item_icons/" + itemId + ".png";
}

Engine::vec4 InventoryUI::getRarityColor(CatGame::ItemRarity rarity) const {
    using namespace CatGame;
    switch (rarity) {
        case ItemRarity::Common:
            return {0.8f, 0.8f, 0.8f, 1.0f};  // Gray
        case ItemRarity::Uncommon:
            return {0.3f, 1.0f, 0.3f, 1.0f};  // Green
        case ItemRarity::Rare:
            return {0.3f, 0.5f, 1.0f, 1.0f};  // Blue
        case ItemRarity::Epic:
            return {0.8f, 0.3f, 1.0f, 1.0f};  // Purple
        case ItemRarity::Legendary:
            return {1.0f, 0.6f, 0.0f, 1.0f};  // Orange
        default:
            return {1.0f, 1.0f, 1.0f, 1.0f};  // White
    }
}

std::string InventoryUI::getCategoryIconPath(CatGame::ItemCategory category) const {
    using namespace CatGame;
    switch (category) {
        case ItemCategory::Weapon:
            return "assets/textures/ui/category_icons/weapon.png";
        case ItemCategory::Armor:
            return "assets/textures/ui/category_icons/armor.png";
        case ItemCategory::Consumable:
            return "assets/textures/ui/category_icons/consumable.png";
        case ItemCategory::Material:
            return "assets/textures/ui/category_icons/material.png";
        case ItemCategory::Quest:
            return "assets/textures/ui/category_icons/quest.png";
        case ItemCategory::Misc:
        default:
            return "assets/textures/ui/category_icons/misc.png";
    }
}

Engine::vec2 InventoryUI::getSlotPosition(int slotIndex) const {
    int row = slotIndex / m_gridColumns;
    int col = slotIndex % m_gridColumns;

    float x = m_windowX + 20.0f + col * (m_slotSize + m_slotSpacing);
    float y = m_windowY + 100.0f + row * (m_slotSize + m_slotSpacing);

    return {x, y};
}

int InventoryUI::getSlotAtPosition(const Engine::vec2& screenPos) const {
    for (int i = 0; i < m_capacity; i++) {
        Engine::vec2 slotPos = getSlotPosition(i);

        if (screenPos.x >= slotPos.x && screenPos.x <= slotPos.x + m_slotSize &&
            screenPos.y >= slotPos.y && screenPos.y <= slotPos.y + m_slotSize) {
            return i;
        }
    }
    return -1;
}

void InventoryUI::swapSlots(int slotA, int slotB) {
    if (slotA < 0 || slotA >= static_cast<int>(m_slots.size()) ||
        slotB < 0 || slotB >= static_cast<int>(m_slots.size())) {
        return;
    }

    std::swap(m_slots[slotA].itemId, m_slots[slotB].itemId);
    std::swap(m_slots[slotA].quantity, m_slots[slotB].quantity);
}

void InventoryUI::moveItem(int fromSlot, int toSlot, int quantity) {
    if (fromSlot < 0 || fromSlot >= static_cast<int>(m_slots.size()) ||
        toSlot < 0 || toSlot >= static_cast<int>(m_slots.size()) ||
        fromSlot == toSlot) {
        return;
    }

    auto& sourceSlot = m_slots[fromSlot];
    auto& targetSlot = m_slots[toSlot];

    // If source is empty, nothing to move
    if (sourceSlot.isEmpty()) {
        return;
    }

    // Clamp quantity to available amount
    int moveQuantity = (quantity <= 0) ? sourceSlot.quantity : std::min(quantity, sourceSlot.quantity);

    // If target is empty, just move items
    if (targetSlot.isEmpty()) {
        targetSlot.itemId = sourceSlot.itemId;
        targetSlot.quantity = moveQuantity;
        sourceSlot.quantity -= moveQuantity;
        if (sourceSlot.quantity <= 0) {
            sourceSlot.itemId.clear();
            sourceSlot.quantity = 0;
        }
        return;
    }

    // If target has same item type, try to stack
    if (targetSlot.itemId == sourceSlot.itemId) {
        const auto* item = m_merchantSystem->getInventoryItem(sourceSlot.itemId);
        if (item) {
            int maxStack = item->stackSize;
            int spaceInTarget = maxStack - targetSlot.quantity;
            int transferAmount = std::min(moveQuantity, spaceInTarget);

            if (transferAmount > 0) {
                targetSlot.quantity += transferAmount;
                sourceSlot.quantity -= transferAmount;
                if (sourceSlot.quantity <= 0) {
                    sourceSlot.itemId.clear();
                    sourceSlot.quantity = 0;
                }
                Engine::Logger::debug("InventoryUI: Stacked {} items from slot {} to slot {}",
                                     transferAmount, fromSlot, toSlot);
            }
        }
        return;
    }

    // Different items - swap if moving all
    if (moveQuantity == sourceSlot.quantity) {
        swapSlots(fromSlot, toSlot);
    }
    // Can't partially move to a slot with different item type
}

bool InventoryUI::canUseItem(const CatGame::InventoryItem* item) const {
    if (!item) return false;
    return item->category == CatGame::ItemCategory::Consumable;
}

bool InventoryUI::canEquipItem(const CatGame::InventoryItem* item) const {
    if (!item) return false;
    return item->category == CatGame::ItemCategory::Weapon ||
           item->category == CatGame::ItemCategory::Armor;
}

} // namespace Game
