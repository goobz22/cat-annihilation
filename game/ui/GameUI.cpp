#include "GameUI.hpp"
#include "HUD.hpp"
#include "MainMenu.hpp"
#include "PauseMenu.hpp"
#include "WavePopup.hpp"
#include "../audio/GameAudio.hpp"
#include "../../engine/core/Logger.hpp"

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
            // TODO: Add game over/victory screen
            break;
    }
}

void GameUI::render(CatEngine::Renderer::Renderer& renderer) {
    if (!m_initialized) {
        return;
    }

    // Render screens based on state
    switch (m_currentState) {
        case GameState::MainMenu:
            m_mainMenu->render(renderer);
            break;

        case GameState::Playing:
            m_hud->render(renderer);
            break;

        case GameState::Paused:
            m_hud->render(renderer);
            m_pauseMenu->render(renderer);
            break;

        case GameState::WaveComplete:
            m_hud->render(renderer);
            m_wavePopup->render(renderer);
            break;

        case GameState::GameOver:
        case GameState::Victory:
            m_hud->render(renderer);
            // TODO: Render game over/victory screen
            break;
    }

    // Render transition overlay if active
    if (m_isTransitioning && m_transitionProgress > 0.0f) {
        // TODO: Render fade overlay
        // renderer.drawQuad({0, 0}, {screenWidth, screenHeight},
        //                   {0, 0, 0, transitionProgress});
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

    // Check for pause key (ESC)
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
    m_transitionTimer = 0.0f;
    m_transitionProgress = 0.0f;
}

// ============================================================================
// Private Methods
// ============================================================================

void GameUI::updateScreenVisibility() {
    // This would set visibility flags on each screen
    // For now, screens handle their own visibility in render
}

void GameUI::updateTransition(float deltaTime) {
    m_transitionTimer += deltaTime;
    m_transitionProgress = std::min(m_transitionTimer / m_transitionDuration, 1.0f);

    if (m_transitionProgress >= 1.0f) {
        m_isTransitioning = false;
        m_transitionProgress = 0.0f;
    }
}

} // namespace Game
