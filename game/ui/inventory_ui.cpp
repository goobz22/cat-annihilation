#include "inventory_ui.hpp"
#include "../../engine/core/Logger.hpp"
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

    // Calculate centered window position
    // TODO: Get actual screen dimensions from renderer
    float screenWidth = 1920.0f;
    float screenHeight = 1080.0f;
    m_windowX = (screenWidth - m_windowWidth) / 2.0f;
    m_windowY = (screenHeight - m_windowHeight) / 2.0f;

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

    // TODO: Implement item use logic based on item type
    // For consumables: apply effect, remove from inventory
    Engine::Logger::info("InventoryUI: Used item '{}'", item->name);

    // Remove one from inventory if consumable
    if (item->category == CatGame::ItemCategory::Consumable) {
        m_merchantSystem->removeItem(slot.itemId, 1);
        rebuildSlots();
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

    // TODO: Implement equipment system
    Engine::Logger::info("InventoryUI: Equipped item '{}'", item->name);

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

    // TODO: Replace with actual input handling
    // Example structure:
    /*
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
    }
    */
}

void InventoryUI::handleMouseInput() {
    // TODO: Implement mouse input for drag and drop
    // Track mouse position
    // Handle click to select slot
    // Handle drag start/end
}

// ============================================================================
// Private Rendering Methods
// ============================================================================

void InventoryUI::renderBackground(CatEngine::Renderer::Renderer& renderer) {
    // TODO: Implement using renderer's 2D drawing API
    // Draw inventory window background
    // Apply m_openAnimation for scale/fade
}

void InventoryUI::renderInventoryGrid(CatEngine::Renderer::Renderer& renderer) {
    // TODO: Implement grid rendering
    // For each slot:
    //   - Draw slot background
    //   - Draw item icon if slot has item
    //   - Draw quantity text if stacked
    //   - Draw rarity border
    //   - Highlight if selected
}

void InventoryUI::renderCategoryFilters(CatEngine::Renderer::Renderer& renderer) {
    // TODO: Implement category filter buttons
    // Buttons: All, Weapons, Armor, Consumables, Materials, Quest, Misc
}

void InventoryUI::renderItemTooltip(CatEngine::Renderer::Renderer& renderer) {
    // TODO: Implement tooltip rendering
    // Show item name, description, stats, rarity, sell price
    // Position near cursor or hovered slot
}

void InventoryUI::renderCurrency(CatEngine::Renderer::Renderer& renderer) {
    // TODO: Implement currency display
    // Show player's current currency amount
}

void InventoryUI::renderCapacity(CatEngine::Renderer::Renderer& renderer) {
    // TODO: Implement capacity bar
    // Show used slots / total slots
    // Visual bar showing fullness
}

void InventoryUI::renderActionButtons(CatEngine::Renderer::Renderer& renderer) {
    // TODO: Implement action buttons
    // Buttons: Use, Equip, Drop, Sort
}

void InventoryUI::renderDraggedItem(CatEngine::Renderer::Renderer& renderer) {
    // TODO: Implement dragged item rendering
    // Draw item icon following cursor
    // Draw semi-transparent
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
    // TODO: Implement partial item moving for stacking
    swapSlots(fromSlot, toSlot);
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
