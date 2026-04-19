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
#include "../engine/assets/AssetManager.hpp"
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

#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
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

    // Bring up the asset manager's loader-thread pool before any system
    // attempts to load a model or texture. LoadModel/LoadModelAsync both
    // hard-require an initialized manager — without this call those paths
    // would throw (async) or hit the cache miss path with no loader (sync).
    CatEngine::AssetManager::GetInstance().Initialize();

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
    std::unique_ptr<CatEngine::CUDA::CudaContext> cudaContext;
    try {
        cudaContext.reset(new CatEngine::CUDA::CudaContext());
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
    auto game = std::make_unique<CatGame::CatAnnihilation>(&input, &window, renderer.get(), audioEngine.get(), &imguiLayer);
    // Wire the config through before initialize() so the menus can bind
    // their Settings panel to it during initializeUI(). Without this the
    // Settings dialog's sliders would mutate file-local storage only and
    // never reach the real audio/video subsystems.
    game->setGameConfig(&gameConfig);
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

    CatEngine::Timer timer;
    timer.Start();
    float deltaTime = 0.0f;
    float fpsTimer = 0.0f;
    uint32_t frameCount = 0;
    float currentFPS = 0.0f;

    using FrameClock = std::chrono::steady_clock;
    auto nextFrameDeadline = FrameClock::now();

    bool running = true;

    while (running && !window.shouldClose()) {
        // Frame-pacing anchor: captured before work begins so the optional
        // sleep at the bottom of the loop targets a stable cadence instead
        // of drifting with the work duration.
        const auto frameStart = FrameClock::now();

        // Update timer and get delta time
        deltaTime = static_cast<float>(timer.Update());

        // Poll events
        window.pollEvents();

        // Update input
        input.update();

        // Start a new ImGui frame so update() can emit widgets via MainMenu etc.
        imguiLayer.BeginFrame();

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

        // Calculate FPS
        fpsTimer += deltaTime;
        frameCount++;
        if (fpsTimer >= 1.0f) {
            currentFPS = static_cast<float>(frameCount) / fpsTimer;
            if (gameConfig.graphics.showFPS) {
                Engine::Logger::info("FPS: " + std::to_string(static_cast<int>(currentFPS)));
            }
            frameCount = 0;
            fpsTimer = 0.0f;
        }

        // CPU-side frame cap.
        //
        // VSync normally pins the cadence via vkQueuePresentKHR blocking on
        // the compositor. But the settings UI can disable VSync at runtime,
        // and some Vulkan drivers silently fall back to IMMEDIATE mode when
        // the requested present mode isn't available — in both cases the
        // loop would otherwise spin as fast as the CPU can dispatch draws,
        // burning ~100% of a core. When the user wants an explicit target
        // (gameConfig.graphics.maxFPS > 0) OR VSync is off, sleep until the
        // start of the next frame. The default maxFPS=60 fallback applies
        // only when VSync is disabled and the config doesn't specify one,
        // so VSync-on users keep their native refresh rate.
        const bool vsyncActive = gameConfig.graphics.vsync;
        const uint32_t configuredCap = gameConfig.graphics.maxFPS;
        const uint32_t targetFPS = (configuredCap > 0)
            ? configuredCap
            : (vsyncActive ? 0u : 60u);

        if (targetFPS > 0) {
            using namespace std::chrono;
            const auto targetFrameTime =
                duration_cast<FrameClock::duration>(
                    duration<double>(1.0 / static_cast<double>(targetFPS)));

            // Advance the deadline from the previous deadline when possible
            // to avoid jitter accumulation; if we fell far behind (e.g.
            // stall, debugger break), resync to the current time so we
            // don't burn the next N frames catching up.
            nextFrameDeadline += targetFrameTime;
            const auto now = FrameClock::now();
            if (nextFrameDeadline < now) {
                nextFrameDeadline = now + targetFrameTime;
            }
            std::this_thread::sleep_until(nextFrameDeadline);
        } else {
            // Keep the deadline tracking current time so the first frame
            // after a toggle-on doesn't try to catch up through history.
            nextFrameDeadline = frameStart;
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

    // Release any cached models/textures and stop the loader thread pool
    // before main exits. Doing this after the renderer/RHI are already
    // down is safe because the AssetManager only holds CPU-side data and
    // the loader tasks are pure-CPU (parsing + uploads are driven by the
    // renderer, which has already waited on the GPU at this point).
    CatEngine::AssetManager::GetInstance().Shutdown();

    Engine::Logger::info("===========================================");
    Engine::Logger::info("  CAT ANNIHILATION - Shutdown Complete");
    Engine::Logger::info("===========================================");

    return 0;
}
