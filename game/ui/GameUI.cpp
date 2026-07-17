#include "GameUI.hpp"
#include "HUD.hpp"
#include "MainMenu.hpp"
#include "PauseMenu.hpp"
#include "WavePopup.hpp"
#include "../audio/GameAudio.hpp"
#include "../../engine/core/Logger.hpp"
#include <cmath>
#include <algorithm>

namespace Game {

namespace {
// Web parity: the death screen reveals ~500 ms after death
// (GameOverScreen.tsx:23) and immediately shows its "Try Again" action. The
// native death overlay uses the same 0.5 s reveal for its restart prompt —
// the old 2 s gate outlasted the 0.5 s fade-in and left the player on a
// promptless screen even though the restart input is already live.
constexpr float kEndGameRevealSeconds = 0.5F;
} // namespace

GameUI::GameUI(Engine::Input& input, GameAudio& audio)
    : m_input(input)
    , m_audio(audio) {
}

GameUI::~GameUI() {
    shutdown();
}

bool GameUI::initialize() {
    if (m_initialized) {
        Engine::Logger::warn("GameUI already initialized");
        return true;
    }

    // Create UI screens
    m_hud = std::make_unique<HUD>(m_input, m_audio);
    m_mainMenu = std::make_unique<MainMenu>(m_input, m_audio);
    m_pauseMenu = std::make_unique<PauseMenu>(m_input, m_audio);
    m_wavePopup = std::make_unique<WavePopup>(m_input, m_audio);

    // Initialize all screens
    if (!m_hud->initialize()) {
        Engine::Logger::error("Failed to initialize HUD");
        return false;
    }

    if (!m_mainMenu->initialize()) {
        Engine::Logger::error("Failed to initialize Main Menu");
        return false;
    }

    if (!m_pauseMenu->initialize()) {
        Engine::Logger::error("Failed to initialize Pause Menu");
        return false;
    }

    if (!m_wavePopup->initialize()) {
        Engine::Logger::error("Failed to initialize Wave Popup");
        return false;
    }

    // Set initial state
    setGameState(GameState::MainMenu);

    m_initialized = true;
    Engine::Logger::info("GameUI initialized successfully");
    return true;
}

void GameUI::shutdown() {
    if (!m_initialized) {
        return;
    }

    m_hud.reset();
    m_mainMenu.reset();
    m_pauseMenu.reset();
    m_wavePopup.reset();

    m_initialized = false;
    Engine::Logger::info("GameUI shutdown");
}

void GameUI::update(float deltaTime) {
    if (!m_initialized) {
        return;
    }

    // Update transition animation
    if (m_isTransitioning) {
        updateTransition(deltaTime);
    }

    // Update active screens based on state
    switch (m_currentState) {
        case GameState::MainMenu:
            m_mainMenu->update(deltaTime);
            break;

        case GameState::Playing:
            m_hud->update(deltaTime);
            break;

        case GameState::Paused:
            m_hud->update(deltaTime);
            m_pauseMenu->update(deltaTime);
            break;

        case GameState::WaveComplete:
            m_hud->update(deltaTime);
            m_wavePopup->update(deltaTime);
            break;

        case GameState::GameOver:
        case GameState::Victory:
            m_hud->update(deltaTime);
            m_endGameTimer += deltaTime;
            break;
    }
}

void GameUI::render(CatEngine::Renderer::UIPass& uiPass, uint32_t screenWidth, uint32_t screenHeight) {
    if (!m_initialized) {
        return;
    }

    // Cache screen dimensions
    m_screenWidth = screenWidth;
    m_screenHeight = screenHeight;

    // Render screens based on state
    switch (m_currentState) {
        case GameState::MainMenu:
            m_mainMenu->render(uiPass, screenWidth, screenHeight);
            break;

        case GameState::Playing:
            m_hud->render(uiPass, screenWidth, screenHeight);
            break;

        case GameState::Paused:
            m_hud->render(uiPass, screenWidth, screenHeight);
            m_pauseMenu->render(uiPass, screenWidth, screenHeight);
            break;

        case GameState::WaveComplete:
            m_hud->render(uiPass, screenWidth, screenHeight);
            m_wavePopup->render(uiPass, screenWidth, screenHeight);
            break;

        case GameState::GameOver:
            m_hud->render(uiPass, screenWidth, screenHeight);
            renderGameOver(uiPass);
            break;

        case GameState::Victory:
            m_hud->render(uiPass, screenWidth, screenHeight);
            renderVictory(uiPass);
            break;
    }

    // Render transition overlay if active
    if (m_isTransitioning && m_transitionProgress > 0.0F) {
        renderTransitionOverlay(uiPass);
    }
}

// ============================================================================
// State Management
// ============================================================================

void GameUI::setGameState(GameState state) {
    if (m_currentState == state) {
        return;
    }

    m_previousState = m_currentState;
    m_currentState = state;

    // Reset state-specific timers
    if (state == GameState::GameOver || state == GameState::Victory) {
        m_endGameTimer = 0.0F;
    }

    // Handle state-specific logic
    switch (state) {
        case GameState::MainMenu:
            m_audio.playMenuMusic();
            break;

        case GameState::Playing:
            if (m_previousState == GameState::MainMenu) {
                m_audio.playGameplayMusic();
            }
            break;

        case GameState::Paused:
            // Music continues playing, just pause game
            break;

        case GameState::WaveComplete:
            m_audio.playWaveComplete();
            break;

        case GameState::GameOver:
            m_audio.playDefeatMusic();
            break;

        case GameState::Victory:
            m_audio.playVictoryMusic();
            break;
    }

    updateScreenVisibility();
    Engine::Logger::info("Game state changed to: " + std::to_string(static_cast<int>(state)));
}

// ============================================================================
// Input Handling
// ============================================================================

void GameUI::handleInput() {
    if (!m_initialized) {
        return;
    }

    // Check for pause key (ESC).
    //
    // Web parity note (PauseMenu.tsx:32-37): the web toggles pause on ESC OR
    // P. The native P trigger is deliberately NOT added here. This GameUI
    // toggle is not the authoritative one — CatAnnihilation::handleInput
    // (CatAnnihilation.cpp:1629) also handles ESC, calls the real
    // pause()/unpause(), and MIRRORS the resulting state back into this
    // GameUI. The two only stay consistent because both react to the SAME
    // key: if GameUI toggled on a key CatAnnihilation ignores (P), GameUI
    // would flip to Paused while CatAnnihilation kept simulating in Playing —
    // a state desync that runs the game behind the pause overlay. So P must
    // be added at the authoritative site (CatAnnihilation::handleInput, and
    // updatePaused for keyboard un-pause), not here; see the risk flagged to
    // the orchestrator. This handler stays ESC-only to avoid introducing that
    // desync from a file that cannot also update CatAnnihilation.
    if (m_input.isKeyPressed(Engine::Input::Key::Escape)) {
        if (m_currentState == GameState::Playing) {
            setGameState(GameState::Paused);
            m_audio.playMenuClick();
        } else if (m_currentState == GameState::Paused) {
            setGameState(GameState::Playing);
            m_audio.playMenuClick();
        }
    }

    // Route input to active screens
    switch (m_currentState) {
        case GameState::MainMenu:
            m_mainMenu->handleInput();
            break;

        case GameState::Paused:
            m_pauseMenu->handleInput();
            break;

        case GameState::WaveComplete:
            m_wavePopup->handleInput();
            break;

        case GameState::GameOver:
        case GameState::Victory:
            // Any key to return to main menu after delay
            if (m_endGameTimer >= 2.0F) {
                if (m_input.isKeyPressed(Engine::Input::Key::Space) ||
                    m_input.isKeyPressed(Engine::Input::Key::Enter) ||
                    m_input.isMouseButtonPressed(Engine::Input::MouseButton::Left)) {
                    m_audio.playMenuClick();
                    setGameState(GameState::MainMenu);
                }
            }
            break;

        default:
            break;
    }
}

bool GameUI::isConsumingInput() const {
    return m_currentState != GameState::Playing;
}

// ============================================================================
// Transitions
// ============================================================================

void GameUI::startTransition(float duration) {
    m_isTransitioning = true;
    m_transitionDuration = duration;
    m_transitionTimer = 0.0F;
    m_transitionProgress = 0.0F;
}

// ============================================================================
// Private Methods
// ============================================================================

void GameUI::updateScreenVisibility() {
    // Visibility is owned per-screen: each screen's render method reads the
    // relevant game state (paused? in menu? wave-transition?) and returns
    // early when it shouldn't be drawn. Centralising that logic here would
    // duplicate state the screens already have, so this method is an
    // intentional no-op kept for API symmetry with updateTransition and
    // for a future per-frame visibility gate if UI load-balancing is
    // added (e.g., skip low-priority screens when frame time spikes).
}

void GameUI::updateTransition(float deltaTime) {
    m_transitionTimer += deltaTime;
    m_transitionProgress = std::min(m_transitionTimer / m_transitionDuration, 1.0F);

    if (m_transitionProgress >= 1.0F) {
        m_isTransitioning = false;
        m_transitionProgress = 0.0F;
    }
}

void GameUI::renderTransitionOverlay(CatEngine::Renderer::UIPass& uiPass) {
    // Fade to black transition
    CatEngine::Renderer::UIPass::QuadDesc overlay;
    overlay.x = 0.0F;
    overlay.y = 0.0F;
    overlay.width = static_cast<float>(m_screenWidth);
    overlay.height = static_cast<float>(m_screenHeight);
    overlay.r = 0.0F;
    overlay.g = 0.0F;
    overlay.b = 0.0F;
    overlay.a = m_transitionProgress;
    overlay.depth = 0.95F;
    overlay.texture = nullptr;
    uiPass.DrawQuad(overlay);
}

void GameUI::renderGameOver(CatEngine::Renderer::UIPass& uiPass) {
    float centerX = static_cast<float>(m_screenWidth) / 2.0F;
    float centerY = static_cast<float>(m_screenHeight) / 2.0F;

    // Fade in effect
    float fadeAlpha = std::min(m_endGameTimer / 0.5F, 1.0F);

    // Semi-transparent overlay
    CatEngine::Renderer::UIPass::QuadDesc overlay;
    overlay.x = 0.0F;
    overlay.y = 0.0F;
    overlay.width = static_cast<float>(m_screenWidth);
    overlay.height = static_cast<float>(m_screenHeight);
    overlay.r = 0.2F;
    overlay.g = 0.0F;
    overlay.b = 0.0F;
    overlay.a = 0.7F * fadeAlpha;
    overlay.depth = 0.5F;
    overlay.texture = nullptr;
    uiPass.DrawQuad(overlay);

    // Title — "YOU DIED" (web GameOverScreen.tsx:57). The subtle scale pulse
    // is a native flourish with no web analog; it animates only the size, not
    // the string, so a side-by-side comparison still reads the same words.
    float titleScale = 1.0F + std::sin(m_endGameTimer * 2.0F) * 0.05F;

    CatEngine::Renderer::UIPass::TextDesc titleText;
    titleText.text = "YOU DIED";
    titleText.x = centerX - 150.0F;
    titleText.y = centerY - 120.0F;
    titleText.fontSize = 64.0F * titleScale;
    titleText.r = 1.0F;
    titleText.g = 0.2F;
    titleText.b = 0.2F;
    titleText.a = fadeAlpha;
    titleText.depth = 0.6F;
    titleText.fontAtlas = nullptr;
    uiPass.DrawText(titleText);

    // Death message (web GameOverScreen.tsx:60).
    CatEngine::Renderer::UIPass::TextDesc messageText;
    messageText.text = "Your cat has fallen in battle!";
    messageText.x = centerX - 160.0F;
    messageText.y = centerY - 50.0F;
    messageText.fontSize = 22.0F;
    messageText.r = 1.0F;
    messageText.g = 0.8F;
    messageText.b = 0.8F;
    messageText.a = fadeAlpha;
    messageText.depth = 0.6F;
    messageText.fontAtlas = nullptr;
    uiPass.DrawText(messageText);

    // Stats — EXACTLY the web death screen's two lines (GameOverScreen.tsx:
    // 62-63): survival time as "{m}m {s}s" (space-separated, seconds NOT
    // zero-padded — the web prints `survivalTime % 60` raw) and the count of
    // enemies still ALIVE at death (enemies.length, not a kill tally).
    //
    // Deliberate parity decision (surfaced for docs/parity/PARITY_MATRIX.md):
    // the native death screen DROPS its former "Wave Reached" and "Final
    // Score" lines. The web death screen shows neither, and both native
    // members are currently unwired (they render 0), so keeping them would
    // only add two false "0" lines that break the 1:1 read. Wave/score still
    // appear on the native Victory screen — a story-mode superset the web
    // survival build has no equivalent for.
    const int survivalMinutes = static_cast<int>(m_survivalTime) / 60;
    const int survivalSeconds = static_cast<int>(m_survivalTime) % 60;
    std::string timeStr = "Survival Time: " + std::to_string(survivalMinutes) +
                          "m " + std::to_string(survivalSeconds) + "s";
    CatEngine::Renderer::UIPass::TextDesc timeText;
    timeText.text = timeStr.c_str();
    timeText.x = centerX - 110.0F;
    timeText.y = centerY + 10.0F;
    timeText.fontSize = 22.0F;
    timeText.r = 0.85F;
    timeText.g = 0.85F;
    timeText.b = 0.85F;
    timeText.a = fadeAlpha;
    timeText.depth = 0.6F;
    timeText.fontAtlas = nullptr;
    uiPass.DrawText(timeText);

    std::string enemiesStr = "Enemies Remaining: " + std::to_string(m_enemiesRemaining);
    CatEngine::Renderer::UIPass::TextDesc enemiesText;
    enemiesText.text = enemiesStr.c_str();
    enemiesText.x = centerX - 110.0F;
    enemiesText.y = centerY + 45.0F;
    enemiesText.fontSize = 22.0F;
    enemiesText.r = 0.85F;
    enemiesText.g = 0.85F;
    enemiesText.b = 0.85F;
    enemiesText.a = fadeAlpha;
    enemiesText.depth = 0.6F;
    enemiesText.fontAtlas = nullptr;
    uiPass.DrawText(enemiesText);

    // Action hint — web shows a "Try Again" button plus "Press Space or click
    // to restart" (GameOverScreen.tsx:68-77) the instant the screen reveals.
    // The native death overlay draws through the UIPass text path (no
    // clickable ImGui button here), so the button label folds into the single
    // restart hint. The actual restart is handled by CatAnnihilation::
    // updateGameOver (Space / Enter / click → restart the run, matching the
    // web reload), which already accepts input immediately; this prompt just
    // tells the player, now revealed at 0.5 s to match the web reveal.
    if (m_endGameTimer >= kEndGameRevealSeconds) {
        float promptAlpha = (std::sin(m_endGameTimer * 4.0F) + 1.0F) * 0.5F * fadeAlpha;

        CatEngine::Renderer::UIPass::TextDesc promptText;
        promptText.text = "Try Again  -  Press Space or click to restart";
        promptText.x = centerX - 200.0F;
        promptText.y = centerY + 110.0F;
        promptText.fontSize = 18.0F;
        promptText.r = 1.0F;
        promptText.g = 0.9F;
        promptText.b = 0.5F;
        promptText.a = promptAlpha;
        promptText.depth = 0.6F;
        promptText.fontAtlas = nullptr;
        uiPass.DrawText(promptText);
    }
}

void GameUI::renderVictory(CatEngine::Renderer::UIPass& uiPass) {
    float centerX = static_cast<float>(m_screenWidth) / 2.0F;
    float centerY = static_cast<float>(m_screenHeight) / 2.0F;

    // Fade in effect
    float fadeAlpha = std::min(m_endGameTimer / 0.5F, 1.0F);

    // Semi-transparent overlay (golden tint)
    CatEngine::Renderer::UIPass::QuadDesc overlay;
    overlay.x = 0.0F;
    overlay.y = 0.0F;
    overlay.width = static_cast<float>(m_screenWidth);
    overlay.height = static_cast<float>(m_screenHeight);
    overlay.r = 0.1F;
    overlay.g = 0.08F;
    overlay.b = 0.0F;
    overlay.a = 0.6F * fadeAlpha;
    overlay.depth = 0.5F;
    overlay.texture = nullptr;
    uiPass.DrawQuad(overlay);

    // Title - VICTORY
    float titleScale = 1.0F + std::sin(m_endGameTimer * 3.0F) * 0.1F;
    float titleBounce = std::sin(m_endGameTimer * 2.0F) * 5.0F;

    CatEngine::Renderer::UIPass::TextDesc titleText;
    titleText.text = "VICTORY!";
    titleText.x = centerX - 150.0F;
    titleText.y = centerY - 100.0F + titleBounce;
    titleText.fontSize = 72.0F * titleScale;
    titleText.r = 1.0F;
    titleText.g = 0.85F;
    titleText.b = 0.0F;
    titleText.a = fadeAlpha;
    titleText.depth = 0.6F;
    titleText.fontAtlas = nullptr;
    uiPass.DrawText(titleText);

    // Subtitle
    CatEngine::Renderer::UIPass::TextDesc subtitleText;
    subtitleText.text = "All waves completed!";
    subtitleText.x = centerX - 110.0F;
    subtitleText.y = centerY - 30.0F;
    subtitleText.fontSize = 24.0F;
    subtitleText.r = 1.0F;
    subtitleText.g = 1.0F;
    subtitleText.b = 0.8F;
    subtitleText.a = fadeAlpha;
    subtitleText.depth = 0.6F;
    subtitleText.fontAtlas = nullptr;
    uiPass.DrawText(subtitleText);

    // Stats
    std::string scoreStr = "Final Score: " + std::to_string(m_finalScore);
    CatEngine::Renderer::UIPass::TextDesc scoreText;
    scoreText.text = scoreStr.c_str();
    scoreText.x = centerX - 100.0F;
    scoreText.y = centerY + 20.0F;
    scoreText.fontSize = 28.0F;
    scoreText.r = 1.0F;
    scoreText.g = 0.9F;
    scoreText.b = 0.3F;
    scoreText.a = fadeAlpha;
    scoreText.depth = 0.6F;
    scoreText.fontAtlas = nullptr;
    uiPass.DrawText(scoreText);

    int survivalMinutes = static_cast<int>(m_survivalTime) / 60;
    int survivalSeconds = static_cast<int>(m_survivalTime) % 60;
    std::string timeStr = "Total Time: " + std::to_string(survivalMinutes) + ":" +
                          (survivalSeconds < 10 ? "0" : "") + std::to_string(survivalSeconds);
    CatEngine::Renderer::UIPass::TextDesc timeText;
    timeText.text = timeStr.c_str();
    timeText.x = centerX - 100.0F;
    timeText.y = centerY + 60.0F;
    timeText.fontSize = 20.0F;
    timeText.r = 0.9F;
    timeText.g = 0.9F;
    timeText.b = 0.7F;
    timeText.a = fadeAlpha;
    timeText.depth = 0.6F;
    timeText.fontAtlas = nullptr;
    uiPass.DrawText(timeText);

    // Continue prompt (after delay)
    if (m_endGameTimer >= 2.0F) {
        float promptAlpha = (std::sin(m_endGameTimer * 4.0F) + 1.0F) * 0.5F * fadeAlpha;

        CatEngine::Renderer::UIPass::TextDesc promptText;
        promptText.text = "Press SPACE to continue";
        promptText.x = centerX - 130.0F;
        promptText.y = centerY + 120.0F;
        promptText.fontSize = 18.0F;
        promptText.r = 0.9F;
        promptText.g = 0.9F;
        promptText.b = 0.7F;
        promptText.a = promptAlpha;
        promptText.depth = 0.6F;
        promptText.fontAtlas = nullptr;
        uiPass.DrawText(promptText);
    }
}

} // namespace Game
