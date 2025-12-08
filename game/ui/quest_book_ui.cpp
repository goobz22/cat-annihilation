#include "quest_book_ui.hpp"
#include "../../engine/core/Logger.hpp"
#include <algorithm>

namespace Game {

QuestBookUI::QuestBookUI(Engine::Input& input, CatGame::QuestSystem* questSystem)
    : m_input(input)
    , m_questSystem(questSystem) {
}

QuestBookUI::~QuestBookUI() {
    shutdown();
}

bool QuestBookUI::initialize() {
    if (m_initialized) {
        Engine::Logger::warn("QuestBookUI already initialized");
        return true;
    }

    if (!m_questSystem) {
        Engine::Logger::error("QuestBookUI: QuestSystem is null");
        return false;
    }

    // Calculate centered window position
    // TODO: Get actual screen dimensions from renderer
    float screenWidth = 1920.0f;
    float screenHeight = 1080.0f;
    m_windowX = (screenWidth - m_windowWidth) / 2.0f;
    m_windowY = (screenHeight - m_windowHeight) / 2.0f;

    m_detailsPanelWidth = m_windowWidth - m_questListWidth - 60.0f;  // Padding

    m_initialized = true;
    Engine::Logger::info("QuestBookUI initialized successfully");
    return true;
}

void QuestBookUI::shutdown() {
    if (!m_initialized) {
        return;
    }

    m_trackedQuests.clear();
    m_selectedQuestId.clear();

    m_initialized = false;
    Engine::Logger::info("QuestBookUI shutdown");
}

void QuestBookUI::update(float deltaTime) {
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

    // Handle input if open
    if (m_isOpen && m_openAnimation >= 0.99f) {
        handleInput();
    }

    // Update scroll position
    updateScrollPosition();
}

void QuestBookUI::render(CatEngine::Renderer::Renderer& renderer) {
    if (!m_initialized || m_openAnimation <= 0.01f) {
        return;
    }

    // Render background
    renderBackground(renderer);

    // Render tabs
    renderTabs(renderer);

    // Render quest list
    renderQuestList(renderer);

    // Render quest details
    renderQuestDetails(renderer);

    // Render action buttons
    renderActionButtons(renderer);
}

// ============================================================================
// Visibility
// ============================================================================

void QuestBookUI::open() {
    if (!m_isOpen) {
        m_isOpen = true;
        m_scrollOffset = 0;

        // Select first quest if none selected
        auto quests = getFilteredQuests();
        if (!quests.empty() && m_selectedQuestId.empty()) {
            m_selectedQuestId = quests[0]->id;
        }

        Engine::Logger::debug("QuestBookUI opened");
    }
}

void QuestBookUI::close() {
    if (m_isOpen) {
        m_isOpen = false;
        Engine::Logger::debug("QuestBookUI closed");
    }
}

void QuestBookUI::toggle() {
    if (m_isOpen) {
        close();
    } else {
        open();
    }
}

// ============================================================================
// Navigation
// ============================================================================

void QuestBookUI::setActiveTab(int tabIndex) {
    if (tabIndex >= 0 && tabIndex <= 2) {
        setActiveTab(static_cast<QuestBookTab>(tabIndex));
    }
}

void QuestBookUI::setActiveTab(QuestBookTab tab) {
    if (m_activeTab != tab) {
        m_activeTab = tab;
        m_scrollOffset = 0;
        m_selectedQuestId.clear();

        // Select first quest in new tab
        auto quests = getFilteredQuests();
        if (!quests.empty()) {
            m_selectedQuestId = quests[0]->id;
        }

        Engine::Logger::debug("QuestBookUI: Switched to tab {}", static_cast<int>(tab));
    }
}

void QuestBookUI::selectQuest(const std::string& questId) {
    m_selectedQuestId = questId;
}

void QuestBookUI::scrollUp() {
    if (m_scrollOffset > 0) {
        m_scrollOffset--;
    }
}

void QuestBookUI::scrollDown() {
    auto quests = getFilteredQuests();
    int maxScroll = std::max(0, static_cast<int>(quests.size()) - m_maxVisibleQuests);
    if (m_scrollOffset < maxScroll) {
        m_scrollOffset++;
    }
}

void QuestBookUI::pageUp() {
    m_scrollOffset = std::max(0, m_scrollOffset - m_maxVisibleQuests);
}

void QuestBookUI::pageDown() {
    auto quests = getFilteredQuests();
    int maxScroll = std::max(0, static_cast<int>(quests.size()) - m_maxVisibleQuests);
    m_scrollOffset = std::min(maxScroll, m_scrollOffset + m_maxVisibleQuests);
}

// ============================================================================
// Quest Tracking
// ============================================================================

void QuestBookUI::trackQuest(const std::string& questId) {
    // Check if already tracked
    auto it = std::find(m_trackedQuests.begin(), m_trackedQuests.end(), questId);
    if (it != m_trackedQuests.end()) {
        return;  // Already tracked
    }

    // Check if we're at max capacity
    if (static_cast<int>(m_trackedQuests.size()) >= m_maxTrackedQuests) {
        // Remove oldest tracked quest
        m_trackedQuests.erase(m_trackedQuests.begin());
        Engine::Logger::debug("QuestBookUI: Removed oldest tracked quest (max capacity reached)");
    }

    m_trackedQuests.push_back(questId);
    Engine::Logger::debug("QuestBookUI: Now tracking quest '{}'", questId);
}

void QuestBookUI::untrackQuest(const std::string& questId) {
    auto it = std::find(m_trackedQuests.begin(), m_trackedQuests.end(), questId);
    if (it != m_trackedQuests.end()) {
        m_trackedQuests.erase(it);
        Engine::Logger::debug("QuestBookUI: Stopped tracking quest '{}'", questId);
    }
}

void QuestBookUI::toggleTrackingSelectedQuest() {
    if (!m_selectedQuestId.empty()) {
        if (isQuestTracked(m_selectedQuestId)) {
            untrackQuest(m_selectedQuestId);
        } else {
            trackQuest(m_selectedQuestId);
        }
    }
}

bool QuestBookUI::isQuestTracked(const std::string& questId) const {
    return std::find(m_trackedQuests.begin(), m_trackedQuests.end(), questId) != m_trackedQuests.end();
}

// ============================================================================
// Quest Actions
// ============================================================================

bool QuestBookUI::acceptSelectedQuest() {
    if (m_selectedQuestId.empty() || m_activeTab != QuestBookTab::Available) {
        return false;
    }

    bool success = m_questSystem->activateQuest(m_selectedQuestId);
    if (success) {
        Engine::Logger::info("QuestBookUI: Accepted quest '{}'", m_selectedQuestId);
        // Auto-track newly accepted quest
        trackQuest(m_selectedQuestId);
        // Switch to Active tab
        setActiveTab(QuestBookTab::Active);
    }
    return success;
}

bool QuestBookUI::abandonSelectedQuest() {
    if (m_selectedQuestId.empty() || m_activeTab != QuestBookTab::Active) {
        return false;
    }

    bool success = m_questSystem->abandonQuest(m_selectedQuestId);
    if (success) {
        Engine::Logger::info("QuestBookUI: Abandoned quest '{}'", m_selectedQuestId);
        untrackQuest(m_selectedQuestId);
        selectNextQuest();
    }
    return success;
}

bool QuestBookUI::turnInSelectedQuest() {
    if (m_selectedQuestId.empty()) {
        return false;
    }

    const auto* quest = m_questSystem->getQuest(m_selectedQuestId);
    if (!quest || !quest->areAllObjectivesComplete()) {
        return false;
    }

    bool success = m_questSystem->completeQuest(m_selectedQuestId);
    if (success) {
        Engine::Logger::info("QuestBookUI: Turned in quest '{}'", m_selectedQuestId);
        untrackQuest(m_selectedQuestId);
        selectNextQuest();
    }
    return success;
}

// ============================================================================
// Filtering and Sorting
// ============================================================================

void QuestBookUI::setQuestTypeFilter(CatGame::QuestType type) {
    m_typeFilter = type;
    m_scrollOffset = 0;
    Engine::Logger::debug("QuestBookUI: Set type filter");
}

void QuestBookUI::clearQuestTypeFilter() {
    m_typeFilter.reset();
    m_scrollOffset = 0;
    Engine::Logger::debug("QuestBookUI: Cleared type filter");
}

void QuestBookUI::sortByName() {
    m_sortMode = SortMode::Name;
}

void QuestBookUI::sortByLevel() {
    m_sortMode = SortMode::Level;
}

void QuestBookUI::sortByType() {
    m_sortMode = SortMode::Type;
}

// ============================================================================
// Input Handling
// ============================================================================

void QuestBookUI::handleInput() {
    if (m_inputCooldown > 0.0f) {
        return;
    }

    // TODO: Replace with actual input handling
    // Example structure:
    /*
    if (m_input.isKeyPressed(Key::Escape) || m_input.isKeyPressed(Key::Q)) {
        close();
        m_inputCooldown = INPUT_COOLDOWN_TIME;
    } else if (m_input.isKeyPressed(Key::Up) || m_input.isKeyPressed(Key::W)) {
        selectPreviousQuest();
        m_inputCooldown = INPUT_COOLDOWN_TIME;
    } else if (m_input.isKeyPressed(Key::Down) || m_input.isKeyPressed(Key::S)) {
        selectNextQuest();
        m_inputCooldown = INPUT_COOLDOWN_TIME;
    } else if (m_input.isKeyPressed(Key::Enter) || m_input.isKeyPressed(Key::E)) {
        // Accept/Turn In quest
        if (m_activeTab == QuestBookTab::Available) {
            acceptSelectedQuest();
        } else if (m_activeTab == QuestBookTab::Active) {
            turnInSelectedQuest();
        }
        m_inputCooldown = INPUT_COOLDOWN_TIME;
    } else if (m_input.isKeyPressed(Key::T)) {
        toggleTrackingSelectedQuest();
        m_inputCooldown = INPUT_COOLDOWN_TIME;
    } else if (m_input.isKeyPressed(Key::Tab)) {
        // Cycle tabs
        int tabIndex = (static_cast<int>(m_activeTab) + 1) % 3;
        setActiveTab(tabIndex);
        m_inputCooldown = INPUT_COOLDOWN_TIME;
    }
    */
}

// ============================================================================
// Private Rendering Methods
// ============================================================================

void QuestBookUI::renderBackground(CatEngine::Renderer::Renderer& renderer) {
    // TODO: Implement using renderer's 2D drawing API
    // Draw quest book background with animation
    // Apply m_openAnimation for scale/fade effect
}

void QuestBookUI::renderTabs(CatEngine::Renderer::Renderer& renderer) {
    // TODO: Implement tab buttons
    // Tabs: Active, Available, Completed
    // Highlight active tab
}

void QuestBookUI::renderQuestList(CatEngine::Renderer::Renderer& renderer) {
    // TODO: Implement quest list rendering
    // Show filtered quests with icons and titles
    // Highlight selected quest
    // Show tracking indicator
}

void QuestBookUI::renderQuestDetails(CatEngine::Renderer::Renderer& renderer) {
    const auto* quest = getSelectedQuest();
    if (!quest) {
        return;
    }

    // TODO: Implement quest details panel
    // Show quest title, description, lore
    renderQuestObjectives(renderer, quest);
    renderQuestRewards(renderer, quest->rewards);
    renderQuestLore(renderer, quest);
}

void QuestBookUI::renderQuestObjectives(CatEngine::Renderer::Renderer& renderer,
                                       const CatGame::Quest* quest) {
    // TODO: Implement objective list rendering
    // Show each objective with progress bar
    // Mark completed objectives
}

void QuestBookUI::renderQuestRewards(CatEngine::Renderer::Renderer& renderer,
                                    const CatGame::QuestReward& rewards) {
    // TODO: Implement rewards display
    // Show XP, currency, items, unlocks
}

void QuestBookUI::renderQuestLore(CatEngine::Renderer::Renderer& renderer,
                                 const CatGame::Quest* quest) {
    // TODO: Implement lore text rendering
    // Show quest narrative/story text
}

void QuestBookUI::renderActionButtons(CatEngine::Renderer::Renderer& renderer) {
    // TODO: Implement action buttons
    // Available tab: "Accept" button
    // Active tab: "Abandon", "Track/Untrack" buttons
    // Completed tab: "Turn In" button (if objectives complete)
}

// ============================================================================
// Private Helper Methods
// ============================================================================

std::vector<const CatGame::Quest*> QuestBookUI::getQuestsForCurrentTab() const {
    switch (m_activeTab) {
        case QuestBookTab::Active:
            return m_questSystem->getActiveQuests();
        case QuestBookTab::Available:
            return m_questSystem->getAvailableQuests();
        case QuestBookTab::Completed:
            return m_questSystem->getCompletedQuests();
        default:
            return {};
    }
}

std::vector<const CatGame::Quest*> QuestBookUI::getFilteredQuests() const {
    auto quests = getQuestsForCurrentTab();

    // Apply type filter
    if (m_typeFilter.has_value()) {
        quests.erase(
            std::remove_if(quests.begin(), quests.end(),
                [this](const CatGame::Quest* q) { return q->type != m_typeFilter.value(); }),
            quests.end()
        );
    }

    // Apply sorting
    switch (m_sortMode) {
        case SortMode::Name:
            std::sort(quests.begin(), quests.end(),
                [](const CatGame::Quest* a, const CatGame::Quest* b) {
                    return a->title < b->title;
                });
            break;
        case SortMode::Level:
            std::sort(quests.begin(), quests.end(),
                [](const CatGame::Quest* a, const CatGame::Quest* b) {
                    return a->requiredLevel < b->requiredLevel;
                });
            break;
        case SortMode::Type:
            std::sort(quests.begin(), quests.end(),
                [](const CatGame::Quest* a, const CatGame::Quest* b) {
                    return static_cast<int>(a->type) < static_cast<int>(b->type);
                });
            break;
        case SortMode::None:
        default:
            // No sorting
            break;
    }

    return quests;
}

void QuestBookUI::updateScrollPosition() {
    auto quests = getFilteredQuests();
    int maxScroll = std::max(0, static_cast<int>(quests.size()) - m_maxVisibleQuests);
    m_scrollOffset = std::clamp(m_scrollOffset, 0, maxScroll);
}

const CatGame::Quest* QuestBookUI::getSelectedQuest() const {
    if (m_selectedQuestId.empty()) {
        return nullptr;
    }
    return m_questSystem->getQuest(m_selectedQuestId);
}

void QuestBookUI::selectNextQuest() {
    auto quests = getFilteredQuests();
    if (quests.empty()) {
        m_selectedQuestId.clear();
        return;
    }

    // Find current selection
    auto it = std::find_if(quests.begin(), quests.end(),
        [this](const CatGame::Quest* q) { return q->id == m_selectedQuestId; });

    if (it == quests.end() || (it + 1) == quests.end()) {
        // Select first quest
        m_selectedQuestId = quests[0]->id;
    } else {
        // Select next quest
        m_selectedQuestId = (*(it + 1))->id;
    }
}

void QuestBookUI::selectPreviousQuest() {
    auto quests = getFilteredQuests();
    if (quests.empty()) {
        m_selectedQuestId.clear();
        return;
    }

    // Find current selection
    auto it = std::find_if(quests.begin(), quests.end(),
        [this](const CatGame::Quest* q) { return q->id == m_selectedQuestId; });

    if (it == quests.end() || it == quests.begin()) {
        // Select last quest
        m_selectedQuestId = quests.back()->id;
    } else {
        // Select previous quest
        m_selectedQuestId = (*(it - 1))->id;
    }
}

std::string QuestBookUI::getQuestIconPath(CatGame::QuestType type) const {
    using namespace CatGame;
    switch (type) {
        case QuestType::MainStory:
            return "assets/textures/ui/quest_icons/main.png";
        case QuestType::SideQuest:
            return "assets/textures/ui/quest_icons/side.png";
        case QuestType::Daily:
            return "assets/textures/ui/quest_icons/daily.png";
        case QuestType::ClanMission:
            return "assets/textures/ui/quest_icons/clan.png";
        case QuestType::Bounty:
            return "assets/textures/ui/quest_icons/bounty.png";
        default:
            return "assets/textures/ui/quest_icons/default.png";
    }
}

Engine::vec4 QuestBookUI::getQuestTypeColor(CatGame::QuestType type) const {
    using namespace CatGame;
    switch (type) {
        case QuestType::MainStory:
            return {1.0f, 0.9f, 0.3f, 1.0f};  // Gold
        case QuestType::SideQuest:
            return {0.8f, 0.8f, 0.8f, 1.0f};  // Silver
        case QuestType::Daily:
            return {0.3f, 0.8f, 1.0f, 1.0f};  // Light blue
        case QuestType::ClanMission:
            return {0.8f, 0.3f, 1.0f, 1.0f};  // Purple
        case QuestType::Bounty:
            return {1.0f, 0.3f, 0.3f, 1.0f};  // Red
        default:
            return {1.0f, 1.0f, 1.0f, 1.0f};  // White
    }
}

} // namespace Game
