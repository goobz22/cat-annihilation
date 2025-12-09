#ifndef GAME_UI_INVENTORY_UI_HPP
#define GAME_UI_INVENTORY_UI_HPP

#include "../../engine/core/Input.hpp"
#include "../../engine/renderer/Renderer.hpp"
#include "../systems/MerchantSystem.hpp"
#include <string>
#include <vector>
#include <memory>

namespace Game {

// Forward declaration
namespace CatGame {
    class MerchantSystem;
}

/**
 * @brief Inventory slot structure
 */
struct InventorySlot {
    int slotIndex = -1;
    std::string itemId;
    int quantity = 0;
    bool isEmpty() const { return itemId.empty() || quantity == 0; }
};

/**
 * @brief Inventory UI - Full-screen inventory management interface
 *
 * Features:
 * - Grid-based inventory display
 * - Item tooltips with stats and description
 * - Drag and drop item management
 * - Category filtering
 * - Sorting options (name, rarity, type)
 * - Item stacking
 * - Quick use/equip/drop
 * - Currency display
 * - Weight/capacity system (optional)
 */
class InventoryUI {
public:
    explicit InventoryUI(Engine::Input& input, CatGame::MerchantSystem* merchantSystem);
    ~InventoryUI();

    /**
     * @brief Initialize inventory UI
     * @return true if successful
     */
    bool initialize();

    /**
     * @brief Shutdown inventory UI
     */
    void shutdown();

    /**
     * @brief Update inventory UI (call once per frame)
     * @param deltaTime Time since last frame in seconds
     */
    void update(float deltaTime);

    /**
     * @brief Render inventory UI
     * @param renderer Renderer to use for drawing
     */
    void render(CatEngine::Renderer::Renderer& renderer);

    // ========================================================================
    // Visibility
    // ========================================================================

    /**
     * @brief Open the inventory
     */
    void open();

    /**
     * @brief Close the inventory
     */
    void close();

    /**
     * @brief Toggle inventory open/closed
     */
    void toggle();

    /**
     * @brief Check if inventory is open
     */
    bool isOpen() const { return m_isOpen; }

    // ========================================================================
    // Item Interaction
    // ========================================================================

    /**
     * @brief Select an inventory slot
     * @param slotIndex Slot index
     */
    void selectSlot(int slotIndex);

    /**
     * @brief Get selected slot index
     */
    int getSelectedSlot() const { return m_selectedSlot; }

    /**
     * @brief Use the selected item
     * @return true if item was used
     */
    bool useSelectedItem();

    /**
     * @brief Drop the selected item
     * @param quantity Quantity to drop (0 = all)
     * @return true if item was dropped
     */
    bool dropSelectedItem(int quantity = 1);

    /**
     * @brief Equip the selected item
     * @return true if item was equipped
     */
    bool equipSelectedItem();

    /**
     * @brief Get item in slot
     * @param slotIndex Slot index
     * @return Pointer to inventory item, or nullptr
     */
    const CatGame::InventoryItem* getItemInSlot(int slotIndex) const;

    // ========================================================================
    // Drag and Drop
    // ========================================================================

    /**
     * @brief Start dragging an item from slot
     * @param slotIndex Source slot index
     */
    void startDrag(int slotIndex);

    /**
     * @brief End drag and drop item to target slot
     * @param targetSlot Target slot index
     */
    void endDrag(int targetSlot);

    /**
     * @brief Cancel current drag operation
     */
    void cancelDrag();

    /**
     * @brief Check if currently dragging
     */
    bool isDragging() const { return m_isDragging; }

    /**
     * @brief Get dragged slot index
     */
    int getDraggedSlot() const { return m_draggedSlot; }

    // ========================================================================
    // Filtering and Sorting
    // ========================================================================

    /**
     * @brief Set category filter
     * @param category Category to filter by
     */
    void setCategory(CatGame::ItemCategory category);

    /**
     * @brief Clear category filter (show all)
     */
    void clearCategoryFilter();

    /**
     * @brief Get current category filter
     */
    std::optional<CatGame::ItemCategory> getCategoryFilter() const { return m_categoryFilter; }

    /**
     * @brief Sort inventory by name
     */
    void sortByName();

    /**
     * @brief Sort inventory by rarity
     */
    void sortByRarity();

    /**
     * @brief Sort inventory by type/category
     */
    void sortByType();

    /**
     * @brief Sort inventory by quantity
     */
    void sortByQuantity();

    // ========================================================================
    // Inventory Configuration
    // ========================================================================

    /**
     * @brief Set inventory capacity (max slots)
     * @param capacity Number of slots
     */
    void setCapacity(int capacity);

    /**
     * @brief Get inventory capacity
     */
    int getCapacity() const { return m_capacity; }

    /**
     * @brief Get number of used slots
     */
    int getUsedSlots() const;

    /**
     * @brief Get number of free slots
     */
    int getFreeSlots() const;

    /**
     * @brief Check if inventory is full
     */
    bool isFull() const;

    // ========================================================================
    // Input Handling
    // ========================================================================

    /**
     * @brief Handle input events
     * Call this each frame when inventory is open
     */
    void handleInput();

    /**
     * @brief Handle mouse input for drag and drop
     */
    void handleMouseInput();

private:
    /**
     * @brief Render inventory background
     */
    void renderBackground(CatEngine::Renderer::Renderer& renderer);

    /**
     * @brief Render inventory grid
     */
    void renderInventoryGrid(CatEngine::Renderer::Renderer& renderer);

    /**
     * @brief Render category filter buttons
     */
    void renderCategoryFilters(CatEngine::Renderer::Renderer& renderer);

    /**
     * @brief Render item tooltip (when hovering)
     */
    void renderItemTooltip(CatEngine::Renderer::Renderer& renderer);

    /**
     * @brief Render currency display
     */
    void renderCurrency(CatEngine::Renderer::Renderer& renderer);

    /**
     * @brief Render capacity/weight bar
     */
    void renderCapacity(CatEngine::Renderer::Renderer& renderer);

    /**
     * @brief Render action buttons (Use, Equip, Drop)
     */
    void renderActionButtons(CatEngine::Renderer::Renderer& renderer);

    /**
     * @brief Render dragged item (follows cursor)
     */
    void renderDraggedItem(CatEngine::Renderer::Renderer& renderer);

    /**
     * @brief Get inventory slots (filtered and sorted)
     */
    std::vector<InventorySlot> getFilteredSlots() const;

    /**
     * @brief Build slot list from merchant system inventory
     */
    void rebuildSlots();

    /**
     * @brief Get item icon path
     */
    std::string getItemIconPath(const std::string& itemId) const;

    /**
     * @brief Get rarity color
     */
    Engine::vec4 getRarityColor(CatGame::ItemRarity rarity) const;

    /**
     * @brief Get category icon path
     */
    std::string getCategoryIconPath(CatGame::ItemCategory category) const;

    /**
     * @brief Calculate slot position on screen
     * @param slotIndex Slot index
     * @return Screen position (x, y)
     */
    Engine::vec2 getSlotPosition(int slotIndex) const;

    /**
     * @brief Get slot index at screen position
     * @param screenPos Screen position
     * @return Slot index, or -1 if not over a slot
     */
    int getSlotAtPosition(const Engine::vec2& screenPos) const;

    /**
     * @brief Swap items between two slots
     */
    void swapSlots(int slotA, int slotB);

    /**
     * @brief Move item from one slot to another
     */
    void moveItem(int fromSlot, int toSlot, int quantity);

    /**
     * @brief Can item be used
     */
    bool canUseItem(const CatGame::InventoryItem* item) const;

    /**
     * @brief Can item be equipped
     */
    bool canEquipItem(const CatGame::InventoryItem* item) const;

    Engine::Input& m_input;
    CatGame::MerchantSystem* m_merchantSystem;

    // UI State
    bool m_isOpen = false;
    int m_selectedSlot = -1;
    int m_hoveredSlot = -1;

    // Drag and drop
    bool m_isDragging = false;
    int m_draggedSlot = -1;
    Engine::vec2 m_dragStartPos = {0.0f, 0.0f};
    Engine::vec2 m_currentMousePos = {0.0f, 0.0f};

    // Inventory slots
    std::vector<InventorySlot> m_slots;
    int m_capacity = 60;  // Default 60 slots (6x10 grid)

    // Filtering and sorting
    std::optional<CatGame::ItemCategory> m_categoryFilter;
    enum class SortMode {
        None,
        Name,
        Rarity,
        Type,
        Quantity
    };
    SortMode m_sortMode = SortMode::None;

    // Grid layout
    int m_gridColumns = 10;
    int m_gridRows = 6;
    float m_slotSize = 64.0f;
    float m_slotSpacing = 8.0f;

    // Window layout (in pixels)
    float m_windowWidth = 1000.0f;
    float m_windowHeight = 700.0f;
    float m_windowX = 0.0f;  // Calculated to center
    float m_windowY = 0.0f;  // Calculated to center

    // Screen dimensions (updated from renderer)
    float m_screenWidth = 1920.0f;
    float m_screenHeight = 1080.0f;

    // Animation
    float m_openAnimation = 0.0f;  // 0 = closed, 1 = open
    float m_openAnimSpeed = 5.0f;

    // Input cooldown
    float m_inputCooldown = 0.0f;
    static constexpr float INPUT_COOLDOWN_TIME = 0.15f;

    // Tooltip
    float m_tooltipDelay = 0.5f;      // Time before tooltip appears
    float m_tooltipTimer = 0.0f;       // Current hover time
    bool m_showTooltip = false;

    bool m_initialized = false;
};

} // namespace Game

#endif // GAME_UI_INVENTORY_UI_HPP
