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
            // The between-waves transition panel. The GAME state stays
            // Playing through the entire clear→gate→popup→spawn window (the
            // dedicated WaveComplete UI state below is never entered during
            // live play), so gating the popup on THAT state meant it never
            // rendered once in a real run — showWaveComplete() fired, the
            // panel stayed invisible, and no capture ever showed it
            // (diagnosed 2026-07-17 via the wavescan probe). The popup
            // early-returns unless visible, so this line is inert outside
            // the transition window.
            m_wavePopup->render(uiPass, screenWidth, screenHeight);
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
        case GameState::Victory:
            // Only the HUD (dimmed under the modal) is drawn here — the
            // web-parity YOU DIED / end card is a Dear ImGui modal rendered by
            // CatAnnihilation::renderEndScreenOverlay, the single end screen.
            m_hud->render(uiPass, screenWidth, screenHeight);
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
            // Intentionally no input handled here. The web death screen
            // restarts the run on Space/click (GameOverScreen.tsx), NOT return
            // to menu — and the authoritative handler is
            // CatAnnihilation::updateGameOver (Space/Enter/R → restart, the
            // TRY AGAIN modal button → restart, Esc/Q → menu). The old
            // "any key → main menu after 2 s" branch here CONFLICTED with that
            // restart (a single Space would both restart and try to leave), so
            // it was removed with the rest of the duplicate end screen.
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

} // namespace Game
