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
#include "../engine/rhi/vulkan/VulkanSwapchain.hpp"
#include "../engine/rhi/vulkan/VulkanDevice.hpp"
#include "../engine/renderer/Renderer.hpp"
#include "../engine/renderer/passes/UIPass.hpp"
#include "../engine/audio/AudioEngine.hpp"
#include "../engine/cuda/CudaContext.hpp"
#include "../engine/ui/ImGuiLayer.hpp"
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

        if (arg == "--help") {
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
    CatEngine::RHI::RHIDesc rhiDesc;
    rhiDesc.enableValidation = cmdArgs.enableValidation;
    rhiDesc.applicationName = "Cat Annihilation";
    rhiDesc.applicationVersion = 1;

    auto rhi = std::make_unique<CatEngine::RHI::VulkanRHI>();
    if (!rhi->Initialize(rhiDesc)) {
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
    rendererConfig.windowHandle = &window;  // Pass window pointer for Vulkan surface creation

    auto renderer = std::make_unique<CatEngine::Renderer::Renderer>(rendererConfig);
    if (!renderer->Initialize(rhi.get())) {
        Engine::Logger::error("Failed to initialize renderer");
        return 1;
    }
    Engine::Logger::info("Renderer initialized");

    // ========================================================================
    // Initialize Dear ImGui (shares the UI render pass with our UIPass).
    // ========================================================================
    Engine::ImGuiLayer imguiLayer;
    {
        auto* vulkanSwapchain = static_cast<CatEngine::RHI::VulkanSwapchain*>(renderer->GetSwapchain());
        auto* vulkanDevice = rhi->GetDevice();
        if (vulkanSwapchain == nullptr || vulkanDevice == nullptr) {
            Engine::Logger::error("ImGui init: missing Vulkan swapchain or device");
            return 1;
        }

        Engine::ImGuiLayer::InitInfo imguiInit{};
        imguiInit.window = window.getHandle();
        imguiInit.instance = rhi->GetInstance();
        imguiInit.physicalDevice = vulkanDevice->GetPhysicalDevice();
        imguiInit.device = vulkanDevice->GetVkDevice();
        imguiInit.graphicsQueueFamily = vulkanDevice->GetQueueFamilyIndices().graphics.value_or(0);
        imguiInit.graphicsQueue = vulkanDevice->GetGraphicsQueue();
        imguiInit.renderPass = vulkanSwapchain->GetUIRenderPass();
        imguiInit.minImageCount = 2;
        imguiInit.imageCount = vulkanSwapchain->GetImageCount();
        imguiInit.regularFontPath = "assets/fonts/OpenSans-Regular.ttf";
        imguiInit.boldFontPath = "assets/fonts/OpenSans-Bold.ttf";

        if (!imguiLayer.Init(imguiInit)) {
            Engine::Logger::error("Failed to initialize ImGui layer");
            return 1;
        }
        Engine::Logger::info("ImGui layer initialized");

        // Hand the layer to the UIPass so it draws inside the existing UI render pass.
        if (auto* uiPass = renderer->GetUIPass()) {
            uiPass->SetImGuiLayer(&imguiLayer);
        }
    }

    // ========================================================================
    // Initialize CUDA Context
    // ========================================================================
    std::cout << "[main] Creating CUDA context..." << std::endl;
    std::unique_ptr<CatEngine::CUDA::CudaContext> cudaContext;
    try {
        cudaContext.reset(new CatEngine::CUDA::CudaContext());
        std::cout << "[main] CUDA context created" << std::endl;
        Engine::Logger::info("CUDA context initialized successfully");
    } catch (const CatEngine::CUDA::CudaException& e) {
        std::cerr << "[main] CUDA initialization failed: " << e.what() << std::endl;
        Engine::Logger::warn("CUDA initialization failed, continuing without GPU acceleration");
        // Continue without CUDA - it's not critical for basic functionality
    } catch (const std::exception& e) {
        std::cerr << "[main] Exception during CUDA init: " << e.what() << std::endl;
        Engine::Logger::warn("CUDA initialization failed, continuing without GPU acceleration");
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
    audioEngine->getMixer().setMasterVolume(gameConfig.audio.masterVolume);
    if (gameConfig.audio.masterMuted) {
        audioEngine->getMixer().setMasterMuted(true);
    }

    // ========================================================================
    // Create Cat Annihilation Game Instance
    // ========================================================================
    auto game = std::make_unique<CatGame::CatAnnihilation>(&input, renderer.get(), audioEngine.get(), &imguiLayer);
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
    std::cout << "[main] About to enter main loop" << std::endl;

    CatEngine::Timer timer;
    timer.Start();
    float deltaTime = 0.0f;
    float fpsTimer = 0.0f;
    uint32_t frameCount = 0;
    float currentFPS = 0.0f;

    bool running = true;

    while (running && !window.shouldClose()) {
        // Log first few frames for debugging
        if (frameCount < 5) {
            std::cout << "[main] Frame " << frameCount << " starting..." << std::endl;
        }

        // Update timer and get delta time
        deltaTime = static_cast<float>(timer.Update());

        if (frameCount < 5) {
            std::cout << "[main] Frame " << frameCount << " - polling events..." << std::endl;
        }

        // Poll events
        window.pollEvents();

        if (frameCount < 5) {
            std::cout << "[main] Frame " << frameCount << " - updating input..." << std::endl;
        }

        // Update input
        input.update();

        // Start a new ImGui frame so update() can emit widgets via MainMenu etc.
        imguiLayer.BeginFrame();

        if (frameCount < 5) {
            std::cout << "[main] Frame " << frameCount << " - updating game..." << std::endl;
        }

        // Update game
        game->update(deltaTime);

        if (frameCount < 5) {
            std::cout << "[main] Frame " << frameCount << " - checking window minimized..." << std::endl;
        }

        // Render
        if (!window.isMinimized()) {
            if (frameCount < 5) {
                std::cout << "[main] Frame " << frameCount << " - calling BeginFrame..." << std::endl;
            }

            if (renderer->BeginFrame()) {
                if (frameCount < 5) {
                    std::cout << "[main] Frame " << frameCount << " - BeginFrame succeeded, calling game->render()..." << std::endl;
                }

                // Render game (world, entities, effects)
                game->render();

                if (frameCount < 5) {
                    std::cout << "[main] Frame " << frameCount << " - game->render() done, calling EndFrame..." << std::endl;
                    std::cout.flush();
                }

                renderer->EndFrame();

                if (frameCount < 5) {
                    std::cout << "[main] Frame " << frameCount << " - EndFrame done" << std::endl;
                    std::cout.flush();
                }
            } else {
                if (frameCount < 5) {
                    std::cout << "[main] Frame " << frameCount << " - BeginFrame returned false" << std::endl;
                }
            }
        }

        // Calculate FPS
        fpsTimer += deltaTime;
        frameCount++;
        if (fpsTimer >= 1.0f) {
            currentFPS = static_cast<float>(frameCount) / fpsTimer;
            if (gameConfig.graphics.showFPS) {
                Engine::Logger::info("FPS: " + std::to_string(static_cast<int>(currentFPS)));
            }
            std::cout << "[main] FPS: " << static_cast<int>(currentFPS) << std::endl;
            frameCount = 0;
            fpsTimer = 0.0f;
        }

        // The swapchain is created with VSync enabled, so vkQueuePresentKHR
        // blocks until the next monitor refresh and the main loop is
        // naturally rate-limited to the display refresh rate. An explicit
        // CPU-side frame cap is only needed if the swapchain is
        // reconfigured to IMMEDIATE mode (tearing, uncapped) — wire in a
        // sleep-to-target-frame-time here when that mode is exposed.
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

    // ImGui owns Vulkan descriptors; tear it down before the device goes away.
    imguiLayer.Shutdown();

    // Shutdown engine systems in reverse order
    audioEngine->shutdown();
    audioEngine.reset();

    renderer->Shutdown();
    renderer.reset();

    rhi->Shutdown();
    rhi.reset();

    // CUDA context cleanup happens automatically in destructor

    Engine::Logger::info("===========================================");
    Engine::Logger::info("  CAT ANNIHILATION - Shutdown Complete");
    Engine::Logger::info("===========================================");

    return 0;
}
