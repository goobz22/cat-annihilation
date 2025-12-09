#include "quest_book_ui.hpp"
#include "../../engine/core/Logger.hpp"
#include "../../engine/renderer/passes/UIPass.hpp"
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

    // Calculate centered window position with default screen dimensions
    // These will be updated in render() when actual renderer dimensions are available
    m_screenWidth = 1920.0f;
    m_screenHeight = 1080.0f;
    m_windowX = (m_screenWidth - m_windowWidth) / 2.0f;
    m_windowY = (m_screenHeight - m_windowHeight) / 2.0f;

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

    using Key = Engine::Input::Key;

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
    } else if (m_input.isKeyPressed(Key::Delete) || m_input.isKeyPressed(Key::X)) {
        // Abandon quest
        if (m_activeTab == QuestBookTab::Active) {
            abandonSelectedQuest();
        }
        m_inputCooldown = INPUT_COOLDOWN_TIME;
    } else if (m_input.isKeyPressed(Key::PageUp)) {
        pageUp();
        m_inputCooldown = INPUT_COOLDOWN_TIME;
    } else if (m_input.isKeyPressed(Key::PageDown)) {
        pageDown();
        m_inputCooldown = INPUT_COOLDOWN_TIME;
    }
}

// ============================================================================
// Private Rendering Methods
// ============================================================================

void QuestBookUI::renderBackground(CatEngine::Renderer::Renderer& renderer) {
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
    overlay.b = 0.0F;
    overlay.a = 0.5F * animAlpha;
    overlay.depth = 0.0F;
    uiPass->DrawQuad(overlay);

    // Draw quest book background (parchment style)
    CatEngine::Renderer::UIPass::QuadDesc windowBg;
    windowBg.x = scaledX;
    windowBg.y = scaledY;
    windowBg.width = scaledWidth;
    windowBg.height = scaledHeight;
    windowBg.r = 0.85F;
    windowBg.g = 0.78F;
    windowBg.b = 0.65F;
    windowBg.a = 0.95F * animAlpha;
    windowBg.depth = 0.1F;
    uiPass->DrawQuad(windowBg);

    // Draw window border (leather binding)
    float borderWidth = 4.0F;
    CatEngine::Renderer::UIPass::QuadDesc border;
    border.r = 0.35F;
    border.g = 0.22F;
    border.b = 0.12F;
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
    titleBar.r = 0.45F;
    titleBar.g = 0.30F;
    titleBar.b = 0.18F;
    titleBar.a = animAlpha;
    titleBar.depth = 0.2F;
    uiPass->DrawQuad(titleBar);

    // Draw title text
    CatEngine::Renderer::UIPass::TextDesc titleText;
    titleText.text = "Quest Journal";
    titleText.x = scaledX + scaledWidth / 2.0F - 70.0F;
    titleText.y = scaledY + borderWidth + 12.0F * animScale;
    titleText.fontSize = 26.0F * animScale;
    titleText.r = 1.0F;
    titleText.g = 0.95F;
    titleText.b = 0.85F;
    titleText.a = animAlpha;
    titleText.depth = 0.3F;
    uiPass->DrawText(titleText);
}

void QuestBookUI::renderTabs(CatEngine::Renderer::Renderer& renderer) {
    auto* uiPass = renderer.GetUIPass();
    if (!uiPass) return;

    float animAlpha = m_openAnimation;
    float tabY = m_windowY + 55.0F;
    float tabX = m_windowX + 20.0F;
    float tabWidth = 120.0F;
    float tabHeight = 35.0F;
    float tabSpacing = 10.0F;

    const char* tabNames[] = {"Active", "Available", "Completed"};
    int numTabs = 3;

    for (int i = 0; i < numTabs; ++i) {
        bool isActive = (static_cast<int>(m_activeTab) == i);

        CatEngine::Renderer::UIPass::QuadDesc tabBg;
        tabBg.x = tabX + static_cast<float>(i) * (tabWidth + tabSpacing);
        tabBg.y = tabY;
        tabBg.width = tabWidth;
        tabBg.height = tabHeight;

        if (isActive) {
            tabBg.r = 0.55F;
            tabBg.g = 0.40F;
            tabBg.b = 0.25F;
        } else {
            tabBg.r = 0.40F;
            tabBg.g = 0.28F;
            tabBg.b = 0.18F;
        }
        tabBg.a = animAlpha;
        tabBg.depth = 0.3F;
        uiPass->DrawQuad(tabBg);

        // Tab text
        CatEngine::Renderer::UIPass::TextDesc tabText;
        tabText.text = tabNames[i];
        tabText.x = tabBg.x + 15.0F;
        tabText.y = tabBg.y + 10.0F;
        tabText.fontSize = 14.0F;
        if (isActive) {
            tabText.r = 1.0F;
            tabText.g = 0.95F;
            tabText.b = 0.85F;
        } else {
            tabText.r = 0.8F;
            tabText.g = 0.75F;
            tabText.b = 0.65F;
        }
        tabText.a = animAlpha;
        tabText.depth = 0.35F;
        uiPass->DrawText(tabText);
    }
}

void QuestBookUI::renderQuestList(CatEngine::Renderer::Renderer& renderer) {
    auto* uiPass = renderer.GetUIPass();
    if (!uiPass) return;

    float animAlpha = m_openAnimation;
    auto quests = getFilteredQuests();

    // Quest list panel background
    float listX = m_windowX + 20.0F;
    float listY = m_windowY + 100.0F;
    float listHeight = m_windowHeight - 160.0F;

    CatEngine::Renderer::UIPass::QuadDesc listBg;
    listBg.x = listX;
    listBg.y = listY;
    listBg.width = m_questListWidth;
    listBg.height = listHeight;
    listBg.r = 0.75F;
    listBg.g = 0.68F;
    listBg.b = 0.55F;
    listBg.a = animAlpha;
    listBg.depth = 0.25F;
    uiPass->DrawQuad(listBg);

    // Render visible quests
    float questItemHeight = 50.0F;
    float questSpacing = 5.0F;
    float questY = listY + 10.0F;

    int visibleCount = 0;
    for (size_t i = m_scrollOffset; i < quests.size() && visibleCount < m_maxVisibleQuests; ++i, ++visibleCount) {
        const auto* quest = quests[i];
        bool isSelected = (quest->id == m_selectedQuestId);
        bool isTracked = isQuestTracked(quest->id);

        // Quest item background
        CatEngine::Renderer::UIPass::QuadDesc questBg;
        questBg.x = listX + 5.0F;
        questBg.y = questY;
        questBg.width = m_questListWidth - 10.0F;
        questBg.height = questItemHeight;

        if (isSelected) {
            questBg.r = 0.6F;
            questBg.g = 0.5F;
            questBg.b = 0.35F;
        } else {
            questBg.r = 0.7F;
            questBg.g = 0.62F;
            questBg.b = 0.5F;
        }
        questBg.a = animAlpha;
        questBg.depth = 0.3F;
        uiPass->DrawQuad(questBg);

        // Quest type color indicator (left edge)
        Engine::vec4 typeColor = getQuestTypeColor(quest->type);
        CatEngine::Renderer::UIPass::QuadDesc typeIndicator;
        typeIndicator.x = listX + 5.0F;
        typeIndicator.y = questY;
        typeIndicator.width = 4.0F;
        typeIndicator.height = questItemHeight;
        typeIndicator.r = typeColor.x;
        typeIndicator.g = typeColor.y;
        typeIndicator.b = typeColor.z;
        typeIndicator.a = animAlpha;
        typeIndicator.depth = 0.35F;
        uiPass->DrawQuad(typeIndicator);

        // Quest title
        CatEngine::Renderer::UIPass::TextDesc titleText;
        titleText.text = quest->title.c_str();
        titleText.x = listX + 15.0F;
        titleText.y = questY + 8.0F;
        titleText.fontSize = 14.0F;
        titleText.r = typeColor.x;
        titleText.g = typeColor.y;
        titleText.b = typeColor.z;
        titleText.a = animAlpha;
        titleText.depth = 0.4F;
        uiPass->DrawText(titleText);

        // Quest level requirement
        char levelBuf[32];
        snprintf(levelBuf, sizeof(levelBuf), "Lv.%d", quest->requiredLevel);
        CatEngine::Renderer::UIPass::TextDesc levelText;
        levelText.text = levelBuf;
        levelText.x = listX + 15.0F;
        levelText.y = questY + 28.0F;
        levelText.fontSize = 11.0F;
        levelText.r = 0.5F;
        levelText.g = 0.45F;
        levelText.b = 0.4F;
        levelText.a = animAlpha;
        levelText.depth = 0.4F;
        uiPass->DrawText(levelText);

        // Tracking indicator
        if (isTracked) {
            CatEngine::Renderer::UIPass::QuadDesc trackIndicator;
            trackIndicator.x = listX + m_questListWidth - 25.0F;
            trackIndicator.y = questY + 15.0F;
            trackIndicator.width = 15.0F;
            trackIndicator.height = 15.0F;
            trackIndicator.r = 0.2F;
            trackIndicator.g = 0.8F;
            trackIndicator.b = 0.3F;
            trackIndicator.a = animAlpha;
            trackIndicator.depth = 0.4F;
            uiPass->DrawQuad(trackIndicator);
        }

        questY += questItemHeight + questSpacing;
    }

    // Scroll indicators
    if (m_scrollOffset > 0) {
        CatEngine::Renderer::UIPass::TextDesc upArrow;
        upArrow.text = "^";
        upArrow.x = listX + m_questListWidth / 2.0F - 5.0F;
        upArrow.y = listY - 15.0F;
        upArrow.fontSize = 16.0F;
        upArrow.r = 0.4F;
        upArrow.g = 0.35F;
        upArrow.b = 0.3F;
        upArrow.a = animAlpha;
        upArrow.depth = 0.4F;
        uiPass->DrawText(upArrow);
    }

    int maxScroll = std::max(0, static_cast<int>(quests.size()) - m_maxVisibleQuests);
    if (m_scrollOffset < maxScroll) {
        CatEngine::Renderer::UIPass::TextDesc downArrow;
        downArrow.text = "v";
        downArrow.x = listX + m_questListWidth / 2.0F - 5.0F;
        downArrow.y = listY + listHeight + 2.0F;
        downArrow.fontSize = 16.0F;
        downArrow.r = 0.4F;
        downArrow.g = 0.35F;
        downArrow.b = 0.3F;
        downArrow.a = animAlpha;
        downArrow.depth = 0.4F;
        uiPass->DrawText(downArrow);
    }
}

void QuestBookUI::renderQuestDetails(CatEngine::Renderer::Renderer& renderer) {
    const auto* quest = getSelectedQuest();
    if (!quest) {
        return;
    }

    auto* uiPass = renderer.GetUIPass();
    if (!uiPass) return;

    float animAlpha = m_openAnimation;

    // Details panel position
    float detailsX = m_windowX + m_questListWidth + 40.0F;
    float detailsY = m_windowY + 100.0F;
    float detailsHeight = m_windowHeight - 160.0F;

    // Details panel background
    CatEngine::Renderer::UIPass::QuadDesc detailsBg;
    detailsBg.x = detailsX;
    detailsBg.y = detailsY;
    detailsBg.width = m_detailsPanelWidth;
    detailsBg.height = detailsHeight;
    detailsBg.r = 0.78F;
    detailsBg.g = 0.72F;
    detailsBg.b = 0.60F;
    detailsBg.a = animAlpha;
    detailsBg.depth = 0.25F;
    uiPass->DrawQuad(detailsBg);

    // Quest title with type color
    Engine::vec4 typeColor = getQuestTypeColor(quest->type);
    CatEngine::Renderer::UIPass::TextDesc titleText;
    titleText.text = quest->title.c_str();
    titleText.x = detailsX + 15.0F;
    titleText.y = detailsY + 15.0F;
    titleText.fontSize = 20.0F;
    titleText.r = typeColor.x;
    titleText.g = typeColor.y;
    titleText.b = typeColor.z;
    titleText.a = animAlpha;
    titleText.depth = 0.4F;
    uiPass->DrawText(titleText);

    // Quest description
    CatEngine::Renderer::UIPass::TextDesc descText;
    descText.text = quest->description.c_str();
    descText.x = detailsX + 15.0F;
    descText.y = detailsY + 50.0F;
    descText.fontSize = 13.0F;
    descText.r = 0.3F;
    descText.g = 0.25F;
    descText.b = 0.2F;
    descText.a = animAlpha;
    descText.depth = 0.4F;
    uiPass->DrawText(descText);

    // Render sub-sections
    renderQuestObjectives(renderer, quest);
    renderQuestRewards(renderer, quest->rewards);
    renderQuestLore(renderer, quest);
}

void QuestBookUI::renderQuestObjectives(CatEngine::Renderer::Renderer& renderer,
                                       const CatGame::Quest* quest) {
    auto* uiPass = renderer.GetUIPass();
    if (!uiPass || !quest) return;

    float animAlpha = m_openAnimation;
    float detailsX = m_windowX + m_questListWidth + 40.0F;
    float objectivesY = m_windowY + 180.0F;

    // Objectives header
    CatEngine::Renderer::UIPass::TextDesc headerText;
    headerText.text = "Objectives:";
    headerText.x = detailsX + 15.0F;
    headerText.y = objectivesY;
    headerText.fontSize = 15.0F;
    headerText.r = 0.4F;
    headerText.g = 0.35F;
    headerText.b = 0.25F;
    headerText.a = animAlpha;
    headerText.depth = 0.4F;
    uiPass->DrawText(headerText);

    float objY = objectivesY + 25.0F;
    float objSpacing = 35.0F;

    for (const auto& objective : quest->objectives) {
        bool isComplete = objective.isComplete();

        // Checkbox/indicator
        CatEngine::Renderer::UIPass::QuadDesc checkbox;
        checkbox.x = detailsX + 20.0F;
        checkbox.y = objY;
        checkbox.width = 16.0F;
        checkbox.height = 16.0F;
        if (isComplete) {
            checkbox.r = 0.2F;
            checkbox.g = 0.7F;
            checkbox.b = 0.3F;
        } else {
            checkbox.r = 0.5F;
            checkbox.g = 0.45F;
            checkbox.b = 0.4F;
        }
        checkbox.a = animAlpha;
        checkbox.depth = 0.4F;
        uiPass->DrawQuad(checkbox);

        // Objective description
        CatEngine::Renderer::UIPass::TextDesc objText;
        objText.text = objective.description.c_str();
        objText.x = detailsX + 45.0F;
        objText.y = objY;
        objText.fontSize = 12.0F;
        if (isComplete) {
            objText.r = 0.4F;
            objText.g = 0.55F;
            objText.b = 0.4F;
        } else {
            objText.r = 0.35F;
            objText.g = 0.3F;
            objText.b = 0.25F;
        }
        objText.a = animAlpha;
        objText.depth = 0.4F;
        uiPass->DrawText(objText);

        // Progress bar for countable objectives
        if (objective.targetCount > 1) {
            float progressBarX = detailsX + 45.0F;
            float progressBarY = objY + 16.0F;
            float progressBarWidth = 150.0F;
            float progressBarHeight = 8.0F;

            // Background
            CatEngine::Renderer::UIPass::QuadDesc progressBg;
            progressBg.x = progressBarX;
            progressBg.y = progressBarY;
            progressBg.width = progressBarWidth;
            progressBg.height = progressBarHeight;
            progressBg.r = 0.3F;
            progressBg.g = 0.28F;
            progressBg.b = 0.25F;
            progressBg.a = animAlpha;
            progressBg.depth = 0.4F;
            uiPass->DrawQuad(progressBg);

            // Fill
            float progress = static_cast<float>(objective.currentCount) / static_cast<float>(objective.targetCount);
            CatEngine::Renderer::UIPass::QuadDesc progressFill;
            progressFill.x = progressBarX;
            progressFill.y = progressBarY;
            progressFill.width = progressBarWidth * progress;
            progressFill.height = progressBarHeight;
            progressFill.r = 0.3F;
            progressFill.g = 0.6F;
            progressFill.b = 0.3F;
            progressFill.a = animAlpha;
            progressFill.depth = 0.42F;
            uiPass->DrawQuad(progressFill);

            // Progress text
            char progressBuf[32];
            snprintf(progressBuf, sizeof(progressBuf), "%d/%d", objective.currentCount, objective.targetCount);
            CatEngine::Renderer::UIPass::TextDesc progressText;
            progressText.text = progressBuf;
            progressText.x = progressBarX + progressBarWidth + 10.0F;
            progressText.y = progressBarY - 2.0F;
            progressText.fontSize = 10.0F;
            progressText.r = 0.5F;
            progressText.g = 0.45F;
            progressText.b = 0.4F;
            progressText.a = animAlpha;
            progressText.depth = 0.4F;
            uiPass->DrawText(progressText);
        }

        objY += objSpacing;
    }
}

void QuestBookUI::renderQuestRewards(CatEngine::Renderer::Renderer& renderer,
                                    const CatGame::QuestReward& rewards) {
    auto* uiPass = renderer.GetUIPass();
    if (!uiPass) return;

    float animAlpha = m_openAnimation;
    float detailsX = m_windowX + m_questListWidth + 40.0F;
    float rewardsY = m_windowY + 380.0F;

    // Rewards header
    CatEngine::Renderer::UIPass::TextDesc headerText;
    headerText.text = "Rewards:";
    headerText.x = detailsX + 15.0F;
    headerText.y = rewardsY;
    headerText.fontSize = 15.0F;
    headerText.r = 0.4F;
    headerText.g = 0.35F;
    headerText.b = 0.25F;
    headerText.a = animAlpha;
    headerText.depth = 0.4F;
    uiPass->DrawText(headerText);

    float rewardY = rewardsY + 25.0F;
    float rewardSpacing = 22.0F;

    // XP reward
    if (rewards.xp > 0) {
        char xpBuf[64];
        snprintf(xpBuf, sizeof(xpBuf), "+ %d XP", rewards.xp);
        CatEngine::Renderer::UIPass::TextDesc xpText;
        xpText.text = xpBuf;
        xpText.x = detailsX + 25.0F;
        xpText.y = rewardY;
        xpText.fontSize = 13.0F;
        xpText.r = 0.3F;
        xpText.g = 0.6F;
        xpText.b = 0.9F;
        xpText.a = animAlpha;
        xpText.depth = 0.4F;
        uiPass->DrawText(xpText);
        rewardY += rewardSpacing;
    }

    // Currency reward
    if (rewards.currency > 0) {
        char goldBuf[64];
        snprintf(goldBuf, sizeof(goldBuf), "+ %d Gold", rewards.currency);
        CatEngine::Renderer::UIPass::TextDesc goldText;
        goldText.text = goldBuf;
        goldText.x = detailsX + 25.0F;
        goldText.y = rewardY;
        goldText.fontSize = 13.0F;
        goldText.r = 1.0F;
        goldText.g = 0.85F;
        goldText.b = 0.3F;
        goldText.a = animAlpha;
        goldText.depth = 0.4F;
        uiPass->DrawText(goldText);
        rewardY += rewardSpacing;
    }

    // Item rewards
    for (const auto& itemId : rewards.itemIds) {
        char itemBuf[128];
        snprintf(itemBuf, sizeof(itemBuf), "+ Item: %s", itemId.c_str());
        CatEngine::Renderer::UIPass::TextDesc itemText;
        itemText.text = itemBuf;
        itemText.x = detailsX + 25.0F;
        itemText.y = rewardY;
        itemText.fontSize = 12.0F;
        itemText.r = 0.5F;
        itemText.g = 0.8F;
        itemText.b = 0.5F;
        itemText.a = animAlpha;
        itemText.depth = 0.4F;
        uiPass->DrawText(itemText);
        rewardY += rewardSpacing;
    }

    // Unlock rewards
    for (const auto& unlock : rewards.unlocks) {
        char unlockBuf[128];
        snprintf(unlockBuf, sizeof(unlockBuf), "+ Unlocks: %s", unlock.c_str());
        CatEngine::Renderer::UIPass::TextDesc unlockText;
        unlockText.text = unlockBuf;
        unlockText.x = detailsX + 25.0F;
        unlockText.y = rewardY;
        unlockText.fontSize = 12.0F;
        unlockText.r = 0.8F;
        unlockText.g = 0.5F;
        unlockText.b = 0.9F;
        unlockText.a = animAlpha;
        unlockText.depth = 0.4F;
        uiPass->DrawText(unlockText);
        rewardY += rewardSpacing;
    }
}

void QuestBookUI::renderQuestLore(CatEngine::Renderer::Renderer& renderer,
                                 const CatGame::Quest* quest) {
    auto* uiPass = renderer.GetUIPass();
    if (!uiPass || !quest || quest->lore.empty()) return;

    float animAlpha = m_openAnimation;
    float detailsX = m_windowX + m_questListWidth + 40.0F;
    float loreY = m_windowY + m_windowHeight - 120.0F;

    // Lore section with decorative separator
    CatEngine::Renderer::UIPass::QuadDesc separator;
    separator.x = detailsX + 15.0F;
    separator.y = loreY - 10.0F;
    separator.width = m_detailsPanelWidth - 30.0F;
    separator.height = 2.0F;
    separator.r = 0.5F;
    separator.g = 0.45F;
    separator.b = 0.35F;
    separator.a = animAlpha * 0.5F;
    separator.depth = 0.4F;
    uiPass->DrawQuad(separator);

    // Lore text (italic-style rendering with different color)
    CatEngine::Renderer::UIPass::TextDesc loreText;
    loreText.text = quest->lore.c_str();
    loreText.x = detailsX + 20.0F;
    loreText.y = loreY + 5.0F;
    loreText.fontSize = 11.0F;
    loreText.r = 0.45F;
    loreText.g = 0.40F;
    loreText.b = 0.35F;
    loreText.a = animAlpha * 0.9F;
    loreText.depth = 0.4F;
    uiPass->DrawText(loreText);
}

void QuestBookUI::renderActionButtons(CatEngine::Renderer::Renderer& renderer) {
    auto* uiPass = renderer.GetUIPass();
    if (!uiPass) return;

    const auto* quest = getSelectedQuest();
    if (!quest) return;

    float animAlpha = m_openAnimation;
    float buttonX = m_windowX + m_windowWidth - 150.0F;
    float buttonY = m_windowY + m_windowHeight - 60.0F;
    float buttonWidth = 120.0F;
    float buttonHeight = 35.0F;
    float buttonSpacing = 10.0F;

    // Draw buttons based on active tab
    if (m_activeTab == QuestBookTab::Available) {
        // Accept button
        CatEngine::Renderer::UIPass::QuadDesc acceptBtn;
        acceptBtn.x = buttonX;
        acceptBtn.y = buttonY;
        acceptBtn.width = buttonWidth;
        acceptBtn.height = buttonHeight;
        acceptBtn.r = 0.2F;
        acceptBtn.g = 0.6F;
        acceptBtn.b = 0.3F;
        acceptBtn.a = animAlpha;
        acceptBtn.depth = 0.5F;
        uiPass->DrawQuad(acceptBtn);

        CatEngine::Renderer::UIPass::TextDesc acceptText;
        acceptText.text = "Accept [E]";
        acceptText.x = buttonX + 20.0F;
        acceptText.y = buttonY + 10.0F;
        acceptText.fontSize = 14.0F;
        acceptText.r = 1.0F;
        acceptText.g = 1.0F;
        acceptText.b = 1.0F;
        acceptText.a = animAlpha;
        acceptText.depth = 0.55F;
        uiPass->DrawText(acceptText);
    } else if (m_activeTab == QuestBookTab::Active) {
        // Track/Untrack button
        bool isTracked = isQuestTracked(quest->id);
        CatEngine::Renderer::UIPass::QuadDesc trackBtn;
        trackBtn.x = buttonX;
        trackBtn.y = buttonY - buttonHeight - buttonSpacing;
        trackBtn.width = buttonWidth;
        trackBtn.height = buttonHeight;
        if (isTracked) {
            trackBtn.r = 0.6F;
            trackBtn.g = 0.5F;
            trackBtn.b = 0.2F;
        } else {
            trackBtn.r = 0.3F;
            trackBtn.g = 0.5F;
            trackBtn.b = 0.6F;
        }
        trackBtn.a = animAlpha;
        trackBtn.depth = 0.5F;
        uiPass->DrawQuad(trackBtn);

        CatEngine::Renderer::UIPass::TextDesc trackText;
        trackText.text = isTracked ? "Untrack [T]" : "Track [T]";
        trackText.x = trackBtn.x + 15.0F;
        trackText.y = trackBtn.y + 10.0F;
        trackText.fontSize = 13.0F;
        trackText.r = 1.0F;
        trackText.g = 1.0F;
        trackText.b = 1.0F;
        trackText.a = animAlpha;
        trackText.depth = 0.55F;
        uiPass->DrawText(trackText);

        // Turn In button (if quest is complete)
        if (quest->areAllObjectivesComplete()) {
            CatEngine::Renderer::UIPass::QuadDesc turnInBtn;
            turnInBtn.x = buttonX;
            turnInBtn.y = buttonY;
            turnInBtn.width = buttonWidth;
            turnInBtn.height = buttonHeight;
            turnInBtn.r = 0.2F;
            turnInBtn.g = 0.7F;
            turnInBtn.b = 0.3F;
            turnInBtn.a = animAlpha;
            turnInBtn.depth = 0.5F;
            uiPass->DrawQuad(turnInBtn);

            CatEngine::Renderer::UIPass::TextDesc turnInText;
            turnInText.text = "Turn In [E]";
            turnInText.x = buttonX + 15.0F;
            turnInText.y = buttonY + 10.0F;
            turnInText.fontSize = 13.0F;
            turnInText.r = 1.0F;
            turnInText.g = 1.0F;
            turnInText.b = 1.0F;
            turnInText.a = animAlpha;
            turnInText.depth = 0.55F;
            uiPass->DrawText(turnInText);
        }

        // Abandon button
        CatEngine::Renderer::UIPass::QuadDesc abandonBtn;
        abandonBtn.x = buttonX - buttonWidth - buttonSpacing;
        abandonBtn.y = buttonY;
        abandonBtn.width = buttonWidth;
        abandonBtn.height = buttonHeight;
        abandonBtn.r = 0.7F;
        abandonBtn.g = 0.25F;
        abandonBtn.b = 0.2F;
        abandonBtn.a = animAlpha;
        abandonBtn.depth = 0.5F;
        uiPass->DrawQuad(abandonBtn);

        CatEngine::Renderer::UIPass::TextDesc abandonText;
        abandonText.text = "Abandon [X]";
        abandonText.x = abandonBtn.x + 10.0F;
        abandonText.y = abandonBtn.y + 10.0F;
        abandonText.fontSize = 13.0F;
        abandonText.r = 1.0F;
        abandonText.g = 1.0F;
        abandonText.b = 1.0F;
        abandonText.a = animAlpha;
        abandonText.depth = 0.55F;
        uiPass->DrawText(abandonText);
    }
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
