/**
 * Mobile Controls Integration Example
 *
 * This file demonstrates how to integrate the Mobile Controls System
 * into the Cat Annihilation game.
 */

#include "mobile_controls.hpp"
#include "../../engine/core/touch_input.hpp"
#include "../../engine/ui/UISystem.hpp"
#include "../../engine/core/Logger.hpp"

// Example game integration
class CatAnnihilationGame {
private:
    // Systems
    Engine::TouchInput* m_touchInput;
    Game::MobileControlsSystem* m_mobileControls;
    Engine::UI::UISystem* m_uiSystem;

    // Game state
    bool m_isPaused;
    bool m_isMobileDevice;

public:
    void initialize(GLFWwindow* window, u32 screenWidth, u32 screenHeight) {
        Engine::Logger::info("Initializing mobile controls...");

        // Create touch input system
        m_touchInput = new Engine::TouchInput(window);
        m_touchInput->initialize();

        // Create mobile controls system
        m_mobileControls = new Game::MobileControlsSystem();
        m_mobileControls->initialize(m_touchInput, m_uiSystem, screenWidth, screenHeight);

        // Auto-detect and enable for mobile devices
        m_mobileControls->autoDetectAndEnable();
        m_isMobileDevice = m_mobileControls->isMobileDevice();

        // Load custom layouts from JSON
        m_mobileControls->loadPresetsFromFile("assets/config/mobile_layouts.json");

        // Set up control callbacks
        setupControlCallbacks();

        // Apply appropriate layout based on device
        if (m_isMobileDevice) {
            if (screenWidth < 768) {
                m_mobileControls->applyLayoutPreset("default");
            } else if (screenWidth < 1366) {
                m_mobileControls->applyLayoutPreset("tablet");
            } else {
                m_mobileControls->applyLayoutPreset("pro");
            }
        }

        // Customize appearance
        m_mobileControls->setOpacity(0.6f);
        m_mobileControls->setScale(1.0f);

        Engine::Logger::info("Mobile controls initialized successfully");
    }

    void setupControlCallbacks() {
        // Get references to buttons and set up callbacks
        // Note: In actual implementation, you'd modify the buttons after applying preset

        // Attack button - fires weapon
        // m_mobileControls->findButton("attack")->onPress = [this]() {
        //     this->player->startAttack();
        // };
        // m_mobileControls->findButton("attack")->onRelease = [this]() {
        //     this->player->endAttack();
        // };

        // Dodge button - dodge roll with cooldown
        // m_mobileControls->findButton("dodge")->onPress = [this]() {
        //     if (this->player->canDodge()) {
        //         this->player->dodge();
        //         m_mobileControls->setButtonCooldown("dodge", 1.0f);
        //     }
        // };

        // Jump button
        // m_mobileControls->findButton("jump")->onPress = [this]() {
        //     this->player->jump();
        // };

        // Spell button - cast spell with cooldown
        // m_mobileControls->findButton("spell")->onPress = [this]() {
        //     if (this->player->canCastSpell()) {
        //         this->player->castSpell();
        //         m_mobileControls->setButtonCooldown("spell", 5.0f);
        //     }
        // };

        // Block button - hold to block
        // m_mobileControls->findButton("block")->onPress = [this]() {
        //     this->player->startBlocking();
        // };
        // m_mobileControls->findButton("block")->onRelease = [this]() {
        //     this->player->stopBlocking();
        // };

        // Sprint button - toggle sprint
        // m_mobileControls->findButton("sprint")->onPress = [this]() {
        //     this->player->toggleSprint();
        // };

        // Pause button
        // m_mobileControls->findButton("pause")->onPress = [this]() {
        //     this->togglePause();
        // };

        // Set up gesture callbacks
        m_touchInput->setSwipeCallback([this](const Engine::SwipeGesture& swipe) {
            handleSwipe(swipe);
        });

        m_touchInput->setTapCallback([this](glm::vec2 position, int tapCount) {
            handleTap(position, tapCount);
        });

        m_touchInput->setPinchCallback([this](float scale) {
            handlePinch(scale);
        });
    }

    void handleSwipe(const Engine::SwipeGesture& swipe) {
        // Determine swipe direction
        float absX = std::abs(swipe.direction.x);
        float absY = std::abs(swipe.direction.y);

        if (absY > absX) {
            // Vertical swipe
            if (swipe.direction.y < 0) {
                // Swipe up - jump
                // player->jump();
                Engine::Logger::debug("Swipe up detected - Jump!");
            } else {
                // Swipe down - dodge
                // player->dodge();
                Engine::Logger::debug("Swipe down detected - Dodge!");
            }
        } else {
            // Horizontal swipe
            if (swipe.direction.x < 0) {
                // Swipe left
                // player->dodgeLeft();
                Engine::Logger::debug("Swipe left detected - Dodge left!");
            } else {
                // Swipe right
                // player->dodgeRight();
                Engine::Logger::debug("Swipe right detected - Dodge right!");
            }
        }
    }

    void handleTap(glm::vec2 position, int tapCount) {
        if (tapCount == 2) {
            // Double tap - target lock
            // combat->toggleTargetLock();
            Engine::Logger::debug("Double tap detected - Toggle target lock!");
        }
    }

    void handlePinch(float scale) {
        // Pinch to zoom (if camera supports it)
        // camera->adjustZoom(scale);
        Engine::Logger::debug("Pinch detected - Scale: {}", scale);
    }

    void update(float deltaTime) {
        // Update touch input first
        m_touchInput->update(deltaTime);

        // Update mobile controls
        if (m_mobileControls->isEnabled()) {
            m_mobileControls->update(deltaTime);

            // Get joystick input
            glm::vec2 movementInput = m_mobileControls->getMovementInput();
            glm::vec2 cameraInput = m_mobileControls->getCameraInput();

            // Apply movement
            if (glm::length(movementInput) > 0.01f) {
                // player->setMovementInput(movementInput);
                // Example: move player based on joystick
                // player->velocity.x = movementInput.x * player->moveSpeed;
                // player->velocity.z = movementInput.y * player->moveSpeed;
            }

            // Apply camera rotation (if camera joystick is enabled)
            if (glm::length(cameraInput) > 0.01f) {
                // camera->rotate(cameraInput.x * deltaTime * cameraSensitivity,
                //               cameraInput.y * deltaTime * cameraSensitivity);
            }

            // Check button states (alternative to callbacks)
            // if (m_mobileControls->isButtonPressed("attack")) {
            //     player->attack();
            // }
        }

        // Regular game update logic...
    }

    void render() {
        // Render game...

        // Render mobile controls overlay (always rendered last)
        if (m_mobileControls->isEnabled() && m_mobileControls->isVisible()) {
            // Assuming you have a UIPass for rendering
            // m_mobileControls->render(m_uiPass);
        }
    }

    void onResize(u32 newWidth, u32 newHeight) {
        // Update mobile controls for new screen size
        if (m_mobileControls) {
            m_mobileControls->onResize(newWidth, newHeight);
        }
    }

    void togglePause() {
        m_isPaused = !m_isPaused;

        if (m_isPaused) {
            // Show pause menu
            // Hide mobile controls or make them translucent
            m_mobileControls->setOpacity(0.3f);
        } else {
            // Resume game
            // Restore mobile controls opacity
            m_mobileControls->setOpacity(0.6f);
        }
    }

    void showControlsCustomization() {
        // Enter customization mode
        m_mobileControls->enterCustomizationMode();

        // Show customization UI
        // - Allow dragging controls to new positions
        // - Allow resizing controls
        // - Provide reset button
        // - Provide save button

        // Example: Save custom layout
        // m_mobileControls->saveCurrentLayout("my_custom_layout");
    }

    void applyUserPreferences() {
        // Load user's preferred layout
        std::string preferredLayout = "default"; // Load from settings
        m_mobileControls->applyLayoutPreset(preferredLayout);

        // Apply user's opacity preference
        float opacity = 0.6f; // Load from settings
        m_mobileControls->setOpacity(opacity);

        // Apply user's scale preference
        float scale = 1.0f; // Load from settings
        m_mobileControls->setScale(scale);
    }

    void shutdown() {
        if (m_mobileControls) {
            m_mobileControls->shutdown();
            delete m_mobileControls;
            m_mobileControls = nullptr;
        }

        if (m_touchInput) {
            m_touchInput->shutdown();
            delete m_touchInput;
            m_touchInput = nullptr;
        }
    }
};

// Example: Conditional compilation for mobile platforms
#ifdef PLATFORM_MOBILE
    #define ENABLE_MOBILE_CONTROLS 1
#else
    #define ENABLE_MOBILE_CONTROLS 0
#endif

// Example main.cpp integration
/*
int main() {
    // ... window creation ...

    CatAnnihilationGame game;
    game.initialize(window, 1920, 1080);

    // Game loop
    while (!glfwWindowShouldClose(window)) {
        float deltaTime = calculateDeltaTime();

        game.update(deltaTime);
        game.render();

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    game.shutdown();
    return 0;
}
*/

// Example: Dynamic enabling based on input device
/*
void detectInputMethod() {
    if (touchInput->hasTouchSupport()) {
        // Touch detected - enable mobile controls
        mobileControls->setEnabled(true);
        mobileControls->setVisible(true);
    } else {
        // No touch - use keyboard/mouse
        mobileControls->setEnabled(false);
        mobileControls->setVisible(false);
    }
}
*/

// Example: Platform-specific initialization
/*
#ifdef __ANDROID__
void initializeMobileControls() {
    // Android-specific setup
    mobileControls->applyLayoutPreset("default");
    mobileControls->setOpacity(0.5f);

    // Enable haptic feedback
    // touchInput->setHapticEnabled(true);
}
#elif defined(__APPLE__) && TARGET_OS_IPHONE
void initializeMobileControls() {
    // iOS-specific setup
    mobileControls->applyLayoutPreset("default");

    // Account for notch/safe area
    // mobileControls->setSafeAreaInsets(safeInsets);
}
#else
void initializeMobileControls() {
    // Desktop with touch support
    mobileControls->applyLayoutPreset("pro");
    mobileControls->setOpacity(0.4f); // More transparent on desktop
}
#endif
*/
