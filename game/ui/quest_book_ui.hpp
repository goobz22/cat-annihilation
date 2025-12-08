#ifndef GAME_UI_QUEST_BOOK_UI_HPP
#define GAME_UI_QUEST_BOOK_UI_HPP

#include "../../engine/core/Input.hpp"
#include "../../engine/renderer/Renderer.hpp"
#include "../systems/quest_system.hpp"
#include <string>
#include <vector>
#include <memory>

namespace Game {

// Forward declaration
namespace CatGame {
    class QuestSystem;
}

/**
 * @brief Quest book tab types
 */
enum class QuestBookTab {
    Active,      // Currently active quests
    Available,   // Quests available to accept
    Completed    // Completed quests
};

/**
 * @brief Quest Book UI - Full-screen quest management interface
 *
 * Features:
 * - Tabbed interface (Active, Available, Completed)
 * - Quest list with filtering and sorting
 * - Detailed quest view with objectives and rewards
 * - Quest tracking toggle
 * - Quest acceptance/abandonment
 * - Scrolling support for large quest lists
 */
class QuestBookUI {
public:
    explicit QuestBookUI(Engine::Input& input, CatGame::QuestSystem* questSystem);
    ~QuestBookUI();

    /**
     * @brief Initialize quest book UI
     * @return true if successful
     */
    bool initialize();

    /**
     * @brief Shutdown quest book UI
     */
    void shutdown();

    /**
     * @brief Update quest book UI (call once per frame)
     * @param deltaTime Time since last frame in seconds
     */
    void update(float deltaTime);

    /**
     * @brief Render quest book UI
     * @param renderer Renderer to use for drawing
     */
    void render(CatEngine::Renderer::Renderer& renderer);

    // ========================================================================
    // Visibility
    // ========================================================================

    /**
     * @brief Open the quest book
     */
    void open();

    /**
     * @brief Close the quest book
     */
    void close();

    /**
     * @brief Toggle quest book open/closed
     */
    void toggle();

    /**
     * @brief Check if quest book is open
     */
    bool isOpen() const { return m_isOpen; }

    // ========================================================================
    // Navigation
    // ========================================================================

    /**
     * @brief Set active tab
     * @param tabIndex Tab index (0 = Active, 1 = Available, 2 = Completed)
     */
    void setActiveTab(int tabIndex);

    /**
     * @brief Set active tab by enum
     * @param tab Tab type
     */
    void setActiveTab(QuestBookTab tab);

    /**
     * @brief Get active tab
     */
    QuestBookTab getActiveTab() const { return m_activeTab; }

    /**
     * @brief Select a quest in the list
     * @param questId Quest ID to select
     */
    void selectQuest(const std::string& questId);

    /**
     * @brief Get selected quest ID
     */
    std::string getSelectedQuestId() const { return m_selectedQuestId; }

    /**
     * @brief Scroll up in quest list
     */
    void scrollUp();

    /**
     * @brief Scroll down in quest list
     */
    void scrollDown();

    /**
     * @brief Page up in quest list
     */
    void pageUp();

    /**
     * @brief Page down in quest list
     */
    void pageDown();

    // ========================================================================
    // Quest Tracking
    // ========================================================================

    /**
     * @brief Track a quest (show on HUD)
     * @param questId Quest ID to track
     */
    void trackQuest(const std::string& questId);

    /**
     * @brief Untrack a quest
     * @param questId Quest ID to untrack
     */
    void untrackQuest(const std::string& questId);

    /**
     * @brief Toggle tracking for selected quest
     */
    void toggleTrackingSelectedQuest();

    /**
     * @brief Check if quest is tracked
     * @param questId Quest ID
     */
    bool isQuestTracked(const std::string& questId) const;

    /**
     * @brief Get all tracked quest IDs
     */
    std::vector<std::string> getTrackedQuests() const { return m_trackedQuests; }

    /**
     * @brief Set maximum number of tracked quests
     */
    void setMaxTrackedQuests(int max) { m_maxTrackedQuests = max; }

    // ========================================================================
    // Quest Actions
    // ========================================================================

    /**
     * @brief Accept the selected available quest
     * @return true if quest was accepted
     */
    bool acceptSelectedQuest();

    /**
     * @brief Abandon the selected active quest
     * @return true if quest was abandoned
     */
    bool abandonSelectedQuest();

    /**
     * @brief Turn in the selected completed quest
     * @return true if quest was turned in
     */
    bool turnInSelectedQuest();

    // ========================================================================
    // Filtering and Sorting
    // ========================================================================

    /**
     * @brief Set quest type filter
     * @param type Quest type to filter by (empty for all)
     */
    void setQuestTypeFilter(CatGame::QuestType type);

    /**
     * @brief Clear quest type filter
     */
    void clearQuestTypeFilter();

    /**
     * @brief Sort quests by name
     */
    void sortByName();

    /**
     * @brief Sort quests by level
     */
    void sortByLevel();

    /**
     * @brief Sort quests by type
     */
    void sortByType();

    // ========================================================================
    // Input Handling
    // ========================================================================

    /**
     * @brief Handle input events
     * Call this each frame when quest book is open
     */
    void handleInput();

private:
    /**
     * @brief Render quest book background
     */
    void renderBackground(CatEngine::Renderer::Renderer& renderer);

    /**
     * @brief Render tab buttons
     */
    void renderTabs(CatEngine::Renderer::Renderer& renderer);

    /**
     * @brief Render quest list
     */
    void renderQuestList(CatEngine::Renderer::Renderer& renderer);

    /**
     * @brief Render quest details panel
     */
    void renderQuestDetails(CatEngine::Renderer::Renderer& renderer);

    /**
     * @brief Render quest objectives
     */
    void renderQuestObjectives(CatEngine::Renderer::Renderer& renderer,
                               const CatGame::Quest* quest);

    /**
     * @brief Render quest rewards
     */
    void renderQuestRewards(CatEngine::Renderer::Renderer& renderer,
                           const CatGame::QuestReward& rewards);

    /**
     * @brief Render quest lore text
     */
    void renderQuestLore(CatEngine::Renderer::Renderer& renderer,
                        const CatGame::Quest* quest);

    /**
     * @brief Render action buttons (Accept/Abandon/Turn In)
     */
    void renderActionButtons(CatEngine::Renderer::Renderer& renderer);

    /**
     * @brief Get quests for current tab
     */
    std::vector<const CatGame::Quest*> getQuestsForCurrentTab() const;

    /**
     * @brief Get filtered and sorted quest list
     */
    std::vector<const CatGame::Quest*> getFilteredQuests() const;

    /**
     * @brief Update quest list scroll position
     */
    void updateScrollPosition();

    /**
     * @brief Get selected quest pointer
     */
    const CatGame::Quest* getSelectedQuest() const;

    /**
     * @brief Select next quest in list
     */
    void selectNextQuest();

    /**
     * @brief Select previous quest in list
     */
    void selectPreviousQuest();

    /**
     * @brief Get quest icon path based on type
     */
    std::string getQuestIconPath(CatGame::QuestType type) const;

    /**
     * @brief Get quest type color
     */
    Engine::vec4 getQuestTypeColor(CatGame::QuestType type) const;

    Engine::Input& m_input;
    CatGame::QuestSystem* m_questSystem;

    // UI State
    bool m_isOpen = false;
    QuestBookTab m_activeTab = QuestBookTab::Active;
    std::string m_selectedQuestId;

    // Tracking
    std::vector<std::string> m_trackedQuests;
    int m_maxTrackedQuests = 5;

    // Filtering and sorting
    std::optional<CatGame::QuestType> m_typeFilter;
    enum class SortMode {
        None,
        Name,
        Level,
        Type
    };
    SortMode m_sortMode = SortMode::None;

    // Scrolling
    int m_scrollOffset = 0;
    int m_maxVisibleQuests = 10;  // How many quests fit on screen

    // Layout (in pixels)
    float m_windowWidth = 1200.0f;
    float m_windowHeight = 800.0f;
    float m_windowX = 0.0f;  // Calculated to center
    float m_windowY = 0.0f;  // Calculated to center

    float m_questListWidth = 400.0f;
    float m_detailsPanelWidth = 0.0f;  // Calculated

    // Animation
    float m_openAnimation = 0.0f;  // 0 = closed, 1 = open
    float m_openAnimSpeed = 5.0f;

    // Input cooldown (prevent double-inputs)
    float m_inputCooldown = 0.0f;
    static constexpr float INPUT_COOLDOWN_TIME = 0.15f;

    bool m_initialized = false;
};

} // namespace Game

#endif // GAME_UI_QUEST_BOOK_UI_HPP
