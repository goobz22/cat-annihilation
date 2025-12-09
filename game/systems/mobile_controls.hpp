#ifndef GAME_SYSTEMS_MOBILE_CONTROLS_HPP
#define GAME_SYSTEMS_MOBILE_CONTROLS_HPP

#include "../../engine/core/touch_input.hpp"
#include "../../engine/core/Types.hpp"
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>

// Forward declarations
namespace CatEngine::Renderer {
    class UIPass;
}

namespace Engine::UI {
    class UISystem;
}

namespace Game {

using Engine::u32;

/**
 * @brief Type of touch control element
 */
enum class TouchControlType {
    VirtualJoystick,
    Button,
    SwipeZone,
    PinchZoom
};

/**
 * @brief Virtual joystick for movement and camera control
 */
struct VirtualJoystick {
    std::string id;                  // Unique identifier
    glm::vec2 centerPosition;        // Center position in screen space
    float outerRadius = 80.0f;       // Outer circle radius (pixels)
    float innerRadius = 30.0f;       // Inner thumb radius (pixels)
    float deadzone = 0.1f;           // Deadzone threshold (0-1)

    glm::vec2 currentOffset;         // Current stick position (relative to center)
    glm::vec2 normalizedInput;       // Normalized input (-1 to 1 on each axis)
    glm::vec4 baseColor;             // Base circle color
    glm::vec4 thumbColor;            // Thumb circle color

    bool isActive = false;           // Whether joystick is being touched
    bool isDynamic = false;          // Whether joystick appears on touch
    int touchId = -1;                // ID of touch controlling this joystick

    float opacity = 0.5f;            // Control opacity
    bool visible = true;             // Visibility flag

    VirtualJoystick()
        : centerPosition(100.0f, 100.0f)
        , currentOffset(0.0f, 0.0f)
        , normalizedInput(0.0f, 0.0f)
        , baseColor(1.0f, 1.0f, 1.0f, 0.3f)
        , thumbColor(1.0f, 1.0f, 1.0f, 0.6f) {}
};

/**
 * @brief Virtual button for actions
 */
struct VirtualButton {
    std::string id;                  // Unique identifier
    std::string label;               // Button text
    glm::vec2 position;              // Center position in screen space
    float radius = 40.0f;            // Button radius (pixels)
    std::string iconPath;            // Path to icon texture

    glm::vec4 normalColor;           // Color when not pressed
    glm::vec4 pressedColor;          // Color when pressed
    glm::vec4 disabledColor;         // Color when disabled

    bool isPressed = false;          // Whether button is currently pressed
    bool isEnabled = true;           // Whether button is enabled
    bool visible = true;             // Visibility flag

    float cooldown = 0.0f;           // Cooldown duration (seconds)
    float currentCooldown = 0.0f;    // Current cooldown timer

    int touchId = -1;                // ID of touch pressing this button

    std::function<void()> onPress;   // Called when button pressed
    std::function<void()> onRelease; // Called when button released
    std::function<void()> onHold;    // Called while button held

    float opacity = 0.5f;            // Control opacity

    VirtualButton()
        : position(100.0f, 100.0f)
        , normalColor(1.0f, 1.0f, 1.0f, 0.4f)
        , pressedColor(1.0f, 1.0f, 0.0f, 0.7f)
        , disabledColor(0.5f, 0.5f, 0.5f, 0.2f) {}
};

/**
 * @brief Swipe zone for gesture detection
 */
struct SwipeZone {
    std::string id;                  // Unique identifier
    glm::vec2 position;              // Top-left corner
    glm::vec2 size;                  // Width and height
    bool detectSwipe = true;         // Detect swipe gestures
    bool detectPinch = false;        // Detect pinch gestures
    bool detectRotation = false;     // Detect rotation gestures

    std::function<void(const Engine::SwipeGesture&)> onSwipe;
    std::function<void(float)> onPinch;
    std::function<void(float)> onRotation;

    SwipeZone()
        : position(0.0f, 0.0f)
        , size(100.0f, 100.0f) {}
};

/**
 * @brief Mobile control layout preset
 */
struct MobileLayoutPreset {
    std::string name;
    std::string description;
    std::vector<VirtualJoystick> joysticks;
    std::vector<VirtualButton> buttons;
    std::vector<SwipeZone> swipeZones;
};

/**
 * @brief Mobile Controls System
 *
 * Provides virtual controls for mobile/touchscreen devices:
 * - Virtual joysticks for movement and camera
 * - Virtual buttons for actions
 * - Gesture zones for swipes, pinches, etc.
 * - Customizable layouts and presets
 */
class MobileControlsSystem {
public:
    MobileControlsSystem();
    ~MobileControlsSystem();

    /**
     * @brief Initialize mobile controls
     * @param touchInput Touch input system
     * @param uiSystem UI system for rendering
     * @param screenWidth Screen width
     * @param screenHeight Screen height
     */
    void initialize(Engine::TouchInput* touchInput,
                   Engine::UI::UISystem* uiSystem,
                   u32 screenWidth,
                   u32 screenHeight);

    /**
     * @brief Shutdown mobile controls
     */
    void shutdown();

    /**
     * @brief Update mobile controls (call once per frame)
     * @param deltaTime Time since last frame in seconds
     */
    void update(float deltaTime);

    /**
     * @brief Render mobile controls
     * @param uiPass UI rendering pass
     */
    void render(CatEngine::Renderer::UIPass* uiPass);

    /**
     * @brief Handle screen resize
     */
    void onResize(u32 width, u32 height);

    // Virtual Joystick Management
    /**
     * @brief Add a virtual joystick
     */
    void addJoystick(const VirtualJoystick& joystick);

    /**
     * @brief Remove a virtual joystick
     */
    void removeJoystick(const std::string& joystickId);

    /**
     * @brief Set joystick position
     */
    void setJoystickPosition(const std::string& joystickId, glm::vec2 position);

    /**
     * @brief Get joystick input (normalized -1 to 1)
     */
    glm::vec2 getJoystickInput(const std::string& joystickId) const;

    /**
     * @brief Set movement joystick position (convenience method)
     */
    void setMovementJoystickPosition(glm::vec2 center);

    /**
     * @brief Set camera joystick position (convenience method)
     */
    void setCameraJoystickPosition(glm::vec2 center);

    /**
     * @brief Get movement input from movement joystick
     */
    glm::vec2 getMovementInput() const { return getJoystickInput("movement"); }

    /**
     * @brief Get camera input from camera joystick
     */
    glm::vec2 getCameraInput() const { return getJoystickInput("camera"); }

    // Virtual Button Management
    /**
     * @brief Add a virtual button
     */
    void addButton(const VirtualButton& button);

    /**
     * @brief Remove a virtual button
     */
    void removeButton(const std::string& buttonId);

    /**
     * @brief Set button position
     */
    void setButtonPosition(const std::string& buttonId, glm::vec2 position);

    /**
     * @brief Set button enabled state
     */
    void setButtonEnabled(const std::string& buttonId, bool enabled);

    /**
     * @brief Check if button is currently pressed
     */
    bool isButtonPressed(const std::string& buttonId) const;

    /**
     * @brief Set button cooldown
     */
    void setButtonCooldown(const std::string& buttonId, float cooldown);

    // Swipe Zone Management
    /**
     * @brief Add a swipe zone
     */
    void addSwipeZone(const SwipeZone& zone);

    /**
     * @brief Remove a swipe zone
     */
    void removeSwipeZone(const std::string& zoneId);

    // Layout Presets
    /**
     * @brief Apply a layout preset by name
     */
    void applyLayoutPreset(const std::string& presetName);

    /**
     * @brief Save current layout as preset
     */
    void saveCurrentLayout(const std::string& presetName);

    /**
     * @brief Get list of available preset names
     */
    std::vector<std::string> getLayoutPresets() const;

    /**
     * @brief Load presets from JSON file
     */
    bool loadPresetsFromFile(const std::string& filePath);

    /**
     * @brief Save presets to JSON file
     */
    bool savePresetsToFile(const std::string& filePath);

    // Customization Mode
    /**
     * @brief Enter customization mode (allows repositioning controls)
     */
    void enterCustomizationMode();

    /**
     * @brief Exit customization mode
     */
    void exitCustomizationMode();

    /**
     * @brief Check if in customization mode
     */
    bool isInCustomizationMode() const { return m_customizationMode; }

    /**
     * @brief Move element in customization mode
     */
    void moveElement(const std::string& elementId, glm::vec2 newPosition);

    /**
     * @brief Resize element in customization mode
     */
    void resizeElement(const std::string& elementId, float newScale);

    // Rendering Options
    /**
     * @brief Set global opacity for all controls
     */
    void setOpacity(float opacity);

    /**
     * @brief Set global scale for all controls
     */
    void setScale(float scale);

    /**
     * @brief Set controls visibility
     */
    void setVisible(bool visible);

    /**
     * @brief Get controls visibility
     */
    bool isVisible() const { return m_visible; }

    // Device Detection
    /**
     * @brief Check if running on mobile device
     */
    bool isMobileDevice() const;

    /**
     * @brief Check if device has touchscreen
     */
    bool hasTouchScreen() const;

    /**
     * @brief Enable/disable mobile controls
     */
    void setEnabled(bool enabled);

    /**
     * @brief Check if mobile controls are enabled
     */
    bool isEnabled() const { return m_enabled; }

    /**
     * @brief Auto-detect and enable on mobile devices
     */
    void autoDetectAndEnable();

private:
    // Touch event handlers
    void onTouchDown(int touchId, glm::vec2 position);
    void onTouchMove(int touchId, glm::vec2 position);
    void onTouchUp(int touchId, glm::vec2 position);

    // Gesture handlers
    void onSwipe(const Engine::SwipeGesture& swipe);
    void onPinch(float scale);

    // Update functions
    void updateJoysticks(float deltaTime);
    void updateButtons(float deltaTime);
    void updateSwipeZones(float deltaTime);

    // Helper functions
    VirtualJoystick* findJoystick(const std::string& id);
    VirtualButton* findButton(const std::string& id);
    SwipeZone* findSwipeZone(const std::string& id);

    bool isPointInCircle(glm::vec2 point, glm::vec2 center, float radius) const;
    bool isPointInRect(glm::vec2 point, glm::vec2 topLeft, glm::vec2 size) const;

    // Rendering helpers
    void renderJoystick(const VirtualJoystick& joystick, CatEngine::Renderer::UIPass* uiPass);
    void renderButton(const VirtualButton& button, CatEngine::Renderer::UIPass* uiPass);
    void renderSwipeZone(const SwipeZone& zone, CatEngine::Renderer::UIPass* uiPass);

    // Create default layouts
    void createDefaultLayouts();
    MobileLayoutPreset createDefaultLayout();
    MobileLayoutPreset createLeftyLayout();
    MobileLayoutPreset createSimpleLayout();
    MobileLayoutPreset createProLayout();

    // State
    Engine::TouchInput* m_touchInput;
    Engine::UI::UISystem* m_uiSystem;

    u32 m_screenWidth;
    u32 m_screenHeight;

    std::vector<VirtualJoystick> m_joysticks;
    std::vector<VirtualButton> m_buttons;
    std::vector<SwipeZone> m_swipeZones;

    std::unordered_map<std::string, MobileLayoutPreset> m_presets;
    std::string m_currentPreset;

    bool m_initialized;
    bool m_enabled;
    bool m_visible;
    bool m_customizationMode;

    float m_globalOpacity;
    float m_globalScale;

    // Customization state
    std::string m_selectedElementId;
    TouchControlType m_selectedElementType;
};

} // namespace Game

#endif // GAME_SYSTEMS_MOBILE_CONTROLS_HPP
