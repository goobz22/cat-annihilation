/**
 * @file main.cpp
 * @brief Entry point for Cat Annihilation game
 *
 * Initializes engine systems, creates game instance, and runs main loop.
 */

#include "../engine/core/Window.hpp"
#include "../engine/core/Input.hpp"
#include "../engine/core/Timer.hpp"
#include "../engine/core/Logger.hpp"
#include "../engine/core/Config.hpp"
#include "../engine/rhi/vulkan/VulkanRHI.hpp"
#include "../engine/renderer/Renderer.hpp"
#include "../engine/audio/AudioEngine.hpp"
#include "../engine/cuda/CudaContext.hpp"
#include "config/GameConfig.hpp"
#include "config/GameplayConfig.hpp"
#include "config/BalanceConfig.hpp"
#include "CatAnnihilation.hpp"

#include <iostream>
#include <memory>
#include <string>
#include <cstdlib>

// Command line argument parsing
struct CommandLineArgs {
    bool fullscreen = false;
    uint32_t width = 1920;
    uint32_t height = 1080;
    bool enableValidation = false;
    bool showHelp = false;
    std::string configPath = "config.json";
};

CommandLineArgs parseCommandLine(int argc, char* argv[]) {
    CommandLineArgs args;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            args.showHelp = true;
        } else if (arg == "--fullscreen" || arg == "-f") {
            args.fullscreen = true;
        } else if (arg == "--width" || arg == "-w") {
            if (i + 1 < argc) {
                args.width = static_cast<uint32_t>(std::atoi(argv[++i]));
            }
        } else if (arg == "--height" || arg == "-h") {
            if (i + 1 < argc) {
                args.height = static_cast<uint32_t>(std::atoi(argv[++i]));
            }
        } else if (arg == "--validation" || arg == "-v") {
            args.enableValidation = true;
        } else if (arg == "--config" || arg == "-c") {
            if (i + 1 < argc) {
                args.configPath = argv[++i];
            }
        }
    }

    return args;
}

void printHelp() {
    std::cout << "Cat Annihilation - Survive the Waves\n\n";
    std::cout << "Usage: cat-annihilation [options]\n\n";
    std::cout << "Options:\n";
    std::cout << "  --help, -h           Show this help message\n";
    std::cout << "  --fullscreen, -f     Start in fullscreen mode\n";
    std::cout << "  --width <pixels>     Set window width (default: 1920)\n";
    std::cout << "  --height <pixels>    Set window height (default: 1080)\n";
    std::cout << "  --validation, -v     Enable Vulkan validation layers\n";
    std::cout << "  --config <path>      Path to config file (default: config.json)\n";
    std::cout << "\n";
}

int main(int argc, char* argv[]) {
    // Parse command line arguments
    CommandLineArgs cmdArgs = parseCommandLine(argc, argv);

    if (cmdArgs.showHelp) {
        printHelp();
        return 0;
    }

    // Initialize logger
    Engine::Logger::info("===========================================");
    Engine::Logger::info("  CAT ANNIHILATION - Starting Up");
    Engine::Logger::info("===========================================");

    // Load game configuration
    Game::GameConfig gameConfig = Game::GameConfig::getDefault();
    if (!gameConfig.load(cmdArgs.configPath)) {
        Engine::Logger::warn("Failed to load config from " + cmdArgs.configPath +
                           ", using defaults");
    } else {
        Engine::Logger::info("Loaded config from " + cmdArgs.configPath);
    }

    // Apply command line overrides
    if (cmdArgs.fullscreen) {
        gameConfig.graphics.fullscreen = true;
    }
    if (cmdArgs.width > 0 && cmdArgs.height > 0) {
        gameConfig.graphics.windowWidth = cmdArgs.width;
        gameConfig.graphics.windowHeight = cmdArgs.height;
    }
    if (cmdArgs.enableValidation) {
        // Enable validation in engine config
    }

    // Validate and clamp config values
    gameConfig.validate();

    // ========================================================================
    // Create Window
    // ========================================================================
    Engine::Window::Config windowConfig;
    windowConfig.title = "Cat Annihilation";
    windowConfig.width = gameConfig.graphics.windowWidth;
    windowConfig.height = gameConfig.graphics.windowHeight;
    windowConfig.fullscreen = gameConfig.graphics.fullscreen;
    windowConfig.vsync = gameConfig.graphics.vsync;
    windowConfig.resizable = true;

    Engine::Window window(windowConfig);
    Engine::Logger::info("Window created: " +
                        std::to_string(windowConfig.width) + "x" +
                        std::to_string(windowConfig.height));

    // ========================================================================
    // Create Input System
    // ========================================================================
    Engine::Input input(window.getHandle());
    Engine::Logger::info("Input system initialized");

    // ========================================================================
    // Create RHI and Renderer
    // ========================================================================
    CatEngine::RHI::VulkanRHI::Config rhiConfig;
    rhiConfig.enableValidation = cmdArgs.enableValidation;
    rhiConfig.applicationName = "Cat Annihilation";
    rhiConfig.applicationVersion = VK_MAKE_VERSION(1, 0, 0);

    auto rhi = std::make_unique<CatEngine::RHI::VulkanRHI>(rhiConfig);
    if (!rhi->Initialize(window.getHandle())) {
        Engine::Logger::error("Failed to initialize Vulkan RHI");
        return 1;
    }
    Engine::Logger::info("Vulkan RHI initialized");

    CatEngine::Renderer::Renderer::Config rendererConfig;
    rendererConfig.width = gameConfig.graphics.windowWidth;
    rendererConfig.height = gameConfig.graphics.windowHeight;
    rendererConfig.enableVSync = gameConfig.graphics.vsync;
    rendererConfig.enableValidation = cmdArgs.enableValidation;
    rendererConfig.maxFramesInFlight = 2;

    auto renderer = std::make_unique<CatEngine::Renderer::Renderer>(rendererConfig);
    if (!renderer->Initialize(rhi->GetDevice())) {
        Engine::Logger::error("Failed to initialize renderer");
        return 1;
    }
    Engine::Logger::info("Renderer initialized");

    // ========================================================================
    // Initialize CUDA Context
    // ========================================================================
    CatEngine::CudaContext cudaContext;
    if (!cudaContext.initialize()) {
        Engine::Logger::warn("Failed to initialize CUDA context - GPU acceleration disabled");
    } else {
        Engine::Logger::info("CUDA context initialized successfully");
    }

    // ========================================================================
    // Create Audio System
    // ========================================================================
    auto audioEngine = std::make_unique<CatEngine::AudioEngine>();
    if (!audioEngine->initialize()) {
        Engine::Logger::error("Failed to initialize audio engine");
        return 1;
    }
    Engine::Logger::info("Audio engine initialized");

    // Apply audio settings from config
    audioEngine->setMasterVolume(gameConfig.audio.masterVolume);
    if (gameConfig.audio.masterMuted) {
        audioEngine->setMasterVolume(0.0f);
    }

    // ========================================================================
    // Create Cat Annihilation Game Instance
    // ========================================================================
    auto game = std::make_unique<CatGame::CatAnnihilation>(&input, renderer.get(), audioEngine.get());
    if (!game->initialize()) {
        Engine::Logger::error("Failed to initialize Cat Annihilation game");
        return 1;
    }
    Engine::Logger::info("Cat Annihilation game initialized successfully");

    // ========================================================================
    // Setup Window Callbacks
    // ========================================================================
    window.setResizeCallback([&](Engine::u32 width, Engine::u32 height) {
        Engine::Logger::info("Window resized: " + std::to_string(width) + "x" +
                           std::to_string(height));
        renderer->OnResize(width, height);
    });

    window.setCloseCallback([&]() {
        Engine::Logger::info("Window close requested");
    });

    // ========================================================================
    // Main Game Loop
    // ========================================================================
    Engine::Logger::info("Entering main loop...");

    Engine::Timer timer;
    float deltaTime = 0.0f;
    float fpsTimer = 0.0f;
    uint32_t frameCount = 0;
    float currentFPS = 0.0f;

    bool running = true;

    while (running && !window.shouldClose()) {
        // Start frame timing
        timer.reset();

        // Poll events
        window.pollEvents();

        // Update input
        input.update();

        // Update game
        game->update(deltaTime);

        // Render
        if (!window.isMinimized()) {
            if (renderer->BeginFrame()) {
                // Render game (world, entities, effects)
                game->render();

                renderer->EndFrame();
            }
        }

        // Frame timing
        deltaTime = timer.getElapsedSeconds();

        // Calculate FPS
        fpsTimer += deltaTime;
        frameCount++;
        if (fpsTimer >= 1.0f) {
            currentFPS = frameCount / fpsTimer;
            if (gameConfig.graphics.showFPS) {
                Engine::Logger::info("FPS: " + std::to_string(static_cast<int>(currentFPS)));
            }
            frameCount = 0;
            fpsTimer = 0.0f;
        }

        // Cap framerate if needed
        if (gameConfig.graphics.maxFPS > 0) {
            float targetFrameTime = 1.0f / gameConfig.graphics.maxFPS;
            float actualFrameTime = timer.getElapsedSeconds();
            if (actualFrameTime < targetFrameTime) {
                // Sleep for remaining time (platform-specific implementation needed)
                // For now, just busy-wait or skip
            }
        }
    }

    // ========================================================================
    // Cleanup
    // ========================================================================
    Engine::Logger::info("Shutting down...");

    // Save configuration
    if (!gameConfig.save(cmdArgs.configPath)) {
        Engine::Logger::warn("Failed to save config");
    }

    // Shutdown game (this will shutdown all game systems)
    game->shutdown();
    game.reset();

    // Shutdown engine systems in reverse order
    audioEngine->shutdown();
    audioEngine.reset();

    renderer->Shutdown();
    renderer.reset();

    rhi->Shutdown();
    rhi.reset();

    // Cleanup CUDA
    cudaContext.cleanup();

    Engine::Logger::info("===========================================");
    Engine::Logger::info("  CAT ANNIHILATION - Shutdown Complete");
    Engine::Logger::info("===========================================");

    return 0;
}
