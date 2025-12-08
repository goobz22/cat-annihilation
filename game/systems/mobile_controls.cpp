#include "mobile_controls.hpp"
#include "../../engine/core/Logger.hpp"
#include "../../engine/ui/UISystem.hpp"
#include <fstream>
#include <algorithm>
#include <cmath>

// For JSON parsing (assuming nlohmann/json is available)
#ifdef __has_include
#  if __has_include(<nlohmann/json.hpp>)
#    include <nlohmann/json.hpp>
#    define HAS_JSON
#  endif
#endif

namespace Game {

MobileControlsSystem::MobileControlsSystem()
    : m_touchInput(nullptr)
    , m_uiSystem(nullptr)
    , m_screenWidth(1920)
    , m_screenHeight(1080)
    , m_initialized(false)
    , m_enabled(false)
    , m_visible(true)
    , m_customizationMode(false)
    , m_globalOpacity(0.5f)
    , m_globalScale(1.0f)
    , m_selectedElementType(TouchControlType::Button)
{
}

MobileControlsSystem::~MobileControlsSystem() {
    shutdown();
}

void MobileControlsSystem::initialize(Engine::TouchInput* touchInput,
                                     Engine::UI::UISystem* uiSystem,
                                     u32 screenWidth,
                                     u32 screenHeight) {
    if (m_initialized) {
        Engine::Logger::warn("MobileControlsSystem already initialized");
        return;
    }

    m_touchInput = touchInput;
    m_uiSystem = uiSystem;
    m_screenWidth = screenWidth;
    m_screenHeight = screenHeight;

    // Set up touch callbacks
    if (m_touchInput) {
        m_touchInput->setTouchDownCallback([this](int id, glm::vec2 pos) {
            onTouchDown(id, pos);
        });
        m_touchInput->setTouchMoveCallback([this](int id, glm::vec2 pos) {
            onTouchMove(id, pos);
        });
        m_touchInput->setTouchUpCallback([this](int id, glm::vec2 pos) {
            onTouchUp(id, pos);
        });
        m_touchInput->setSwipeCallback([this](const Engine::SwipeGesture& swipe) {
            onSwipe(swipe);
        });
        m_touchInput->setPinchCallback([this](float scale) {
            onPinch(scale);
        });
    }

    // Create default layouts
    createDefaultLayouts();

    // Apply default layout
    applyLayoutPreset("default");

    m_initialized = true;
    Engine::Logger::info("MobileControlsSystem initialized");
}

void MobileControlsSystem::shutdown() {
    if (!m_initialized) {
        return;
    }

    m_joysticks.clear();
    m_buttons.clear();
    m_swipeZones.clear();
    m_presets.clear();

    m_initialized = false;
    Engine::Logger::info("MobileControlsSystem shutdown");
}

void MobileControlsSystem::update(float deltaTime) {
    if (!m_enabled || !m_visible) {
        return;
    }

    updateJoysticks(deltaTime);
    updateButtons(deltaTime);
    updateSwipeZones(deltaTime);
}

void MobileControlsSystem::render(CatEngine::Renderer::UIPass* uiPass) {
    if (!m_enabled || !m_visible || !uiPass) {
        return;
    }

    // Render all joysticks
    for (const auto& joystick : m_joysticks) {
        if (joystick.visible) {
            renderJoystick(joystick, uiPass);
        }
    }

    // Render all buttons
    for (const auto& button : m_buttons) {
        if (button.visible) {
            renderButton(button, uiPass);
        }
    }

    // Render swipe zones (only in customization mode)
    if (m_customizationMode) {
        for (const auto& zone : m_swipeZones) {
            renderSwipeZone(zone, uiPass);
        }
    }
}

void MobileControlsSystem::onResize(u32 width, u32 height) {
    float scaleX = static_cast<float>(width) / static_cast<float>(m_screenWidth);
    float scaleY = static_cast<float>(height) / static_cast<float>(m_screenHeight);

    m_screenWidth = width;
    m_screenHeight = height;

    // Scale all control positions
    for (auto& joystick : m_joysticks) {
        joystick.centerPosition.x *= scaleX;
        joystick.centerPosition.y *= scaleY;
    }

    for (auto& button : m_buttons) {
        button.position.x *= scaleX;
        button.position.y *= scaleY;
    }

    for (auto& zone : m_swipeZones) {
        zone.position.x *= scaleX;
        zone.position.y *= scaleY;
        zone.size.x *= scaleX;
        zone.size.y *= scaleY;
    }
}

// Joystick Management
void MobileControlsSystem::addJoystick(const VirtualJoystick& joystick) {
    m_joysticks.push_back(joystick);
}

void MobileControlsSystem::removeJoystick(const std::string& joystickId) {
    m_joysticks.erase(
        std::remove_if(m_joysticks.begin(), m_joysticks.end(),
            [&joystickId](const VirtualJoystick& j) { return j.id == joystickId; }),
        m_joysticks.end()
    );
}

void MobileControlsSystem::setJoystickPosition(const std::string& joystickId, glm::vec2 position) {
    VirtualJoystick* joystick = findJoystick(joystickId);
    if (joystick) {
        joystick->centerPosition = position;
    }
}

glm::vec2 MobileControlsSystem::getJoystickInput(const std::string& joystickId) const {
    for (const auto& joystick : m_joysticks) {
        if (joystick.id == joystickId && joystick.isActive) {
            return joystick.normalizedInput;
        }
    }
    return glm::vec2(0.0f, 0.0f);
}

void MobileControlsSystem::setMovementJoystickPosition(glm::vec2 center) {
    setJoystickPosition("movement", center);
}

void MobileControlsSystem::setCameraJoystickPosition(glm::vec2 center) {
    setJoystickPosition("camera", center);
}

// Button Management
void MobileControlsSystem::addButton(const VirtualButton& button) {
    m_buttons.push_back(button);
}

void MobileControlsSystem::removeButton(const std::string& buttonId) {
    m_buttons.erase(
        std::remove_if(m_buttons.begin(), m_buttons.end(),
            [&buttonId](const VirtualButton& b) { return b.id == buttonId; }),
        m_buttons.end()
    );
}

void MobileControlsSystem::setButtonPosition(const std::string& buttonId, glm::vec2 position) {
    VirtualButton* button = findButton(buttonId);
    if (button) {
        button->position = position;
    }
}

void MobileControlsSystem::setButtonEnabled(const std::string& buttonId, bool enabled) {
    VirtualButton* button = findButton(buttonId);
    if (button) {
        button->isEnabled = enabled;
    }
}

bool MobileControlsSystem::isButtonPressed(const std::string& buttonId) const {
    for (const auto& button : m_buttons) {
        if (button.id == buttonId) {
            return button.isPressed && button.isEnabled;
        }
    }
    return false;
}

void MobileControlsSystem::setButtonCooldown(const std::string& buttonId, float cooldown) {
    VirtualButton* button = findButton(buttonId);
    if (button) {
        button->currentCooldown = cooldown;
    }
}

// Swipe Zone Management
void MobileControlsSystem::addSwipeZone(const SwipeZone& zone) {
    m_swipeZones.push_back(zone);
}

void MobileControlsSystem::removeSwipeZone(const std::string& zoneId) {
    m_swipeZones.erase(
        std::remove_if(m_swipeZones.begin(), m_swipeZones.end(),
            [&zoneId](const SwipeZone& z) { return z.id == zoneId; }),
        m_swipeZones.end()
    );
}

// Layout Presets
void MobileControlsSystem::applyLayoutPreset(const std::string& presetName) {
    auto it = m_presets.find(presetName);
    if (it == m_presets.end()) {
        Engine::Logger::warn("Layout preset '{}' not found", presetName);
        return;
    }

    const MobileLayoutPreset& preset = it->second;

    m_joysticks = preset.joysticks;
    m_buttons = preset.buttons;
    m_swipeZones = preset.swipeZones;
    m_currentPreset = presetName;

    Engine::Logger::info("Applied mobile layout preset: {}", presetName);
}

void MobileControlsSystem::saveCurrentLayout(const std::string& presetName) {
    MobileLayoutPreset preset;
    preset.name = presetName;
    preset.description = "Custom layout";
    preset.joysticks = m_joysticks;
    preset.buttons = m_buttons;
    preset.swipeZones = m_swipeZones;

    m_presets[presetName] = preset;
    Engine::Logger::info("Saved current layout as preset: {}", presetName);
}

std::vector<std::string> MobileControlsSystem::getLayoutPresets() const {
    std::vector<std::string> presetNames;
    for (const auto& pair : m_presets) {
        presetNames.push_back(pair.first);
    }
    return presetNames;
}

bool MobileControlsSystem::loadPresetsFromFile(const std::string& filePath) {
#ifdef HAS_JSON
    try {
        std::ifstream file(filePath);
        if (!file.is_open()) {
            Engine::Logger::error("Failed to open mobile layouts file: {}", filePath);
            return false;
        }

        nlohmann::json json;
        file >> json;

        // Parse presets from JSON
        // (Implementation would depend on JSON structure)
        Engine::Logger::info("Loaded mobile layouts from: {}", filePath);
        return true;
    } catch (const std::exception& e) {
        Engine::Logger::error("Error loading mobile layouts: {}", e.what());
        return false;
    }
#else
    Engine::Logger::warn("JSON support not available for loading mobile layouts");
    return false;
#endif
}

bool MobileControlsSystem::savePresetsToFile(const std::string& filePath) {
#ifdef HAS_JSON
    try {
        // Convert presets to JSON and save
        // (Implementation would depend on JSON structure)
        Engine::Logger::info("Saved mobile layouts to: {}", filePath);
        return true;
    } catch (const std::exception& e) {
        Engine::Logger::error("Error saving mobile layouts: {}", e.what());
        return false;
    }
#else
    Engine::Logger::warn("JSON support not available for saving mobile layouts");
    return false;
#endif
}

// Customization Mode
void MobileControlsSystem::enterCustomizationMode() {
    m_customizationMode = true;
    Engine::Logger::info("Entered mobile controls customization mode");
}

void MobileControlsSystem::exitCustomizationMode() {
    m_customizationMode = false;
    m_selectedElementId.clear();
    Engine::Logger::info("Exited mobile controls customization mode");
}

void MobileControlsSystem::moveElement(const std::string& elementId, glm::vec2 newPosition) {
    if (!m_customizationMode) {
        return;
    }

    // Try to find and move joystick
    VirtualJoystick* joystick = findJoystick(elementId);
    if (joystick) {
        joystick->centerPosition = newPosition;
        return;
    }

    // Try to find and move button
    VirtualButton* button = findButton(elementId);
    if (button) {
        button->position = newPosition;
        return;
    }
}

void MobileControlsSystem::resizeElement(const std::string& elementId, float newScale) {
    if (!m_customizationMode) {
        return;
    }

    // Try to find and resize joystick
    VirtualJoystick* joystick = findJoystick(elementId);
    if (joystick) {
        joystick->outerRadius *= newScale;
        joystick->innerRadius *= newScale;
        return;
    }

    // Try to find and resize button
    VirtualButton* button = findButton(elementId);
    if (button) {
        button->radius *= newScale;
        return;
    }
}

// Rendering Options
void MobileControlsSystem::setOpacity(float opacity) {
    m_globalOpacity = glm::clamp(opacity, 0.0f, 1.0f);
}

void MobileControlsSystem::setScale(float scale) {
    m_globalScale = glm::max(scale, 0.1f);
}

void MobileControlsSystem::setVisible(bool visible) {
    m_visible = visible;
}

// Device Detection
bool MobileControlsSystem::isMobileDevice() const {
    // Platform detection (would need proper implementation)
    // Check for touch support and screen size
    return m_touchInput && m_touchInput->hasTouchSupport();
}

bool MobileControlsSystem::hasTouchScreen() const {
    return m_touchInput && m_touchInput->hasTouchSupport();
}

void MobileControlsSystem::setEnabled(bool enabled) {
    m_enabled = enabled;
}

void MobileControlsSystem::autoDetectAndEnable() {
    if (isMobileDevice() || hasTouchScreen()) {
        setEnabled(true);
        Engine::Logger::info("Mobile controls auto-enabled (touch device detected)");
    } else {
        setEnabled(false);
    }
}

// Touch Event Handlers
void MobileControlsSystem::onTouchDown(int touchId, glm::vec2 position) {
    if (!m_enabled) {
        return;
    }

    // Check joysticks first
    for (auto& joystick : m_joysticks) {
        if (!joystick.visible) continue;

        glm::vec2 center = joystick.isDynamic ? position : joystick.centerPosition;
        float distance = glm::distance(position, center);

        if (distance <= joystick.outerRadius * m_globalScale) {
            joystick.isActive = true;
            joystick.touchId = touchId;
            if (joystick.isDynamic) {
                joystick.centerPosition = position;
            }
            return;  // Consume touch
        }
    }

    // Check buttons
    for (auto& button : m_buttons) {
        if (!button.visible || !button.isEnabled) continue;

        if (isPointInCircle(position, button.position, button.radius * m_globalScale)) {
            button.isPressed = true;
            button.touchId = touchId;

            // Trigger onPress callback
            if (button.onPress) {
                button.onPress();
            }
            return;  // Consume touch
        }
    }
}

void MobileControlsSystem::onTouchMove(int touchId, glm::vec2 position) {
    if (!m_enabled) {
        return;
    }

    // Update joysticks
    for (auto& joystick : m_joysticks) {
        if (joystick.isActive && joystick.touchId == touchId) {
            glm::vec2 offset = position - joystick.centerPosition;
            float distance = glm::length(offset);

            // Clamp to outer radius
            if (distance > joystick.outerRadius) {
                offset = glm::normalize(offset) * joystick.outerRadius;
                distance = joystick.outerRadius;
            }

            joystick.currentOffset = offset;

            // Calculate normalized input
            if (distance > joystick.deadzone * joystick.outerRadius) {
                float normalizedDistance = (distance - joystick.deadzone * joystick.outerRadius) /
                                          (joystick.outerRadius * (1.0f - joystick.deadzone));
                joystick.normalizedInput = glm::normalize(offset) * normalizedDistance;
            } else {
                joystick.normalizedInput = glm::vec2(0.0f, 0.0f);
            }
        }
    }
}

void MobileControlsSystem::onTouchUp(int touchId, glm::vec2 position) {
    if (!m_enabled) {
        return;
    }

    // Reset joysticks
    for (auto& joystick : m_joysticks) {
        if (joystick.touchId == touchId) {
            joystick.isActive = false;
            joystick.touchId = -1;
            joystick.currentOffset = glm::vec2(0.0f, 0.0f);
            joystick.normalizedInput = glm::vec2(0.0f, 0.0f);
        }
    }

    // Reset buttons
    for (auto& button : m_buttons) {
        if (button.touchId == touchId) {
            button.isPressed = false;
            button.touchId = -1;

            // Trigger onRelease callback
            if (button.onRelease) {
                button.onRelease();
            }
        }
    }
}

void MobileControlsSystem::onSwipe(const Engine::SwipeGesture& swipe) {
    // Forward to swipe zones
    for (auto& zone : m_swipeZones) {
        if (zone.detectSwipe && zone.onSwipe) {
            zone.onSwipe(swipe);
        }
    }
}

void MobileControlsSystem::onPinch(float scale) {
    // Forward to swipe zones
    for (auto& zone : m_swipeZones) {
        if (zone.detectPinch && zone.onPinch) {
            zone.onPinch(scale);
        }
    }
}

// Update Functions
void MobileControlsSystem::updateJoysticks(float deltaTime) {
    // Nothing special needed here currently
}

void MobileControlsSystem::updateButtons(float deltaTime) {
    for (auto& button : m_buttons) {
        // Update cooldown
        if (button.currentCooldown > 0.0f) {
            button.currentCooldown -= deltaTime;
            if (button.currentCooldown < 0.0f) {
                button.currentCooldown = 0.0f;
            }
        }

        // Call onHold if button is held
        if (button.isPressed && button.onHold) {
            button.onHold();
        }
    }
}

void MobileControlsSystem::updateSwipeZones(float deltaTime) {
    // Nothing special needed here currently
}

// Helper Functions
VirtualJoystick* MobileControlsSystem::findJoystick(const std::string& id) {
    for (auto& joystick : m_joysticks) {
        if (joystick.id == id) {
            return &joystick;
        }
    }
    return nullptr;
}

VirtualButton* MobileControlsSystem::findButton(const std::string& id) {
    for (auto& button : m_buttons) {
        if (button.id == id) {
            return &button;
        }
    }
    return nullptr;
}

SwipeZone* MobileControlsSystem::findSwipeZone(const std::string& id) {
    for (auto& zone : m_swipeZones) {
        if (zone.id == id) {
            return &zone;
        }
    }
    return nullptr;
}

bool MobileControlsSystem::isPointInCircle(glm::vec2 point, glm::vec2 center, float radius) const {
    return glm::distance(point, center) <= radius;
}

bool MobileControlsSystem::isPointInRect(glm::vec2 point, glm::vec2 topLeft, glm::vec2 size) const {
    return point.x >= topLeft.x && point.x <= topLeft.x + size.x &&
           point.y >= topLeft.y && point.y <= topLeft.y + size.y;
}

// Rendering (Note: Actual rendering would use the UIPass interface)
void MobileControlsSystem::renderJoystick(const VirtualJoystick& joystick, CatEngine::Renderer::UIPass* uiPass) {
    // Render outer circle (base)
    // Render inner circle (thumb) at current offset
    // This would use the UIPass to submit draw commands

    // Placeholder - actual implementation would submit geometry to UIPass
    float effectiveOpacity = m_globalOpacity * joystick.opacity;

    // Submit base circle
    // uiPass->drawCircle(joystick.centerPosition, joystick.outerRadius * m_globalScale, joystick.baseColor * effectiveOpacity);

    // Submit thumb circle
    glm::vec2 thumbPos = joystick.centerPosition + joystick.currentOffset * m_globalScale;
    // uiPass->drawCircle(thumbPos, joystick.innerRadius * m_globalScale, joystick.thumbColor * effectiveOpacity);
}

void MobileControlsSystem::renderButton(const VirtualButton& button, CatEngine::Renderer::UIPass* uiPass) {
    // Choose color based on state
    glm::vec4 color = button.normalColor;
    if (!button.isEnabled) {
        color = button.disabledColor;
    } else if (button.isPressed) {
        color = button.pressedColor;
    }

    float effectiveOpacity = m_globalOpacity * button.opacity;

    // Submit button circle
    // uiPass->drawCircle(button.position, button.radius * m_globalScale, color * effectiveOpacity);

    // Submit button label if exists
    // if (!button.label.empty()) {
    //     uiPass->drawText(button.label, button.position, ...);
    // }

    // Draw cooldown overlay if active
    if (button.currentCooldown > 0.0f && button.cooldown > 0.0f) {
        float cooldownPercent = button.currentCooldown / button.cooldown;
        // uiPass->drawCooldownOverlay(button.position, button.radius * m_globalScale, cooldownPercent);
    }
}

void MobileControlsSystem::renderSwipeZone(const SwipeZone& zone, CatEngine::Renderer::UIPass* uiPass) {
    // Only render in customization mode for debugging
    if (m_customizationMode) {
        // uiPass->drawRect(zone.position, zone.size, glm::vec4(1.0f, 1.0f, 0.0f, 0.2f));
    }
}

// Create Default Layouts
void MobileControlsSystem::createDefaultLayouts() {
    m_presets["default"] = createDefaultLayout();
    m_presets["lefty"] = createLeftyLayout();
    m_presets["simple"] = createSimpleLayout();
    m_presets["pro"] = createProLayout();
}

MobileLayoutPreset MobileControlsSystem::createDefaultLayout() {
    MobileLayoutPreset preset;
    preset.name = "default";
    preset.description = "Standard mobile control layout";

    // Movement joystick (bottom left)
    VirtualJoystick moveJoystick;
    moveJoystick.id = "movement";
    moveJoystick.centerPosition = glm::vec2(120.0f, m_screenHeight - 150.0f);
    moveJoystick.outerRadius = 80.0f;
    moveJoystick.innerRadius = 30.0f;
    moveJoystick.deadzone = 0.15f;
    moveJoystick.visible = true;
    preset.joysticks.push_back(moveJoystick);

    // Camera joystick (bottom right) - optional
    VirtualJoystick cameraJoystick;
    cameraJoystick.id = "camera";
    cameraJoystick.centerPosition = glm::vec2(m_screenWidth - 120.0f, m_screenHeight - 150.0f);
    cameraJoystick.outerRadius = 80.0f;
    cameraJoystick.innerRadius = 30.0f;
    cameraJoystick.deadzone = 0.15f;
    cameraJoystick.visible = false;  // Hidden by default, use swipe for camera
    preset.joysticks.push_back(cameraJoystick);

    // Attack button (large, bottom right)
    VirtualButton attackButton;
    attackButton.id = "attack";
    attackButton.label = "Attack";
    attackButton.position = glm::vec2(m_screenWidth - 100.0f, m_screenHeight - 100.0f);
    attackButton.radius = 50.0f;
    attackButton.visible = true;
    preset.buttons.push_back(attackButton);

    // Dodge button
    VirtualButton dodgeButton;
    dodgeButton.id = "dodge";
    dodgeButton.label = "Dodge";
    dodgeButton.position = glm::vec2(m_screenWidth - 200.0f, m_screenHeight - 100.0f);
    dodgeButton.radius = 40.0f;
    dodgeButton.visible = true;
    preset.buttons.push_back(dodgeButton);

    // Jump button
    VirtualButton jumpButton;
    jumpButton.id = "jump";
    jumpButton.label = "Jump";
    jumpButton.position = glm::vec2(m_screenWidth - 100.0f, m_screenHeight - 200.0f);
    jumpButton.radius = 40.0f;
    jumpButton.visible = true;
    preset.buttons.push_back(jumpButton);

    // Spell/Special button
    VirtualButton spellButton;
    spellButton.id = "spell";
    spellButton.label = "Spell";
    spellButton.position = glm::vec2(m_screenWidth - 200.0f, m_screenHeight - 200.0f);
    spellButton.radius = 40.0f;
    spellButton.visible = true;
    preset.buttons.push_back(spellButton);

    // Block/Shield button (left side, above joystick)
    VirtualButton blockButton;
    blockButton.id = "block";
    blockButton.label = "Block";
    blockButton.position = glm::vec2(120.0f, m_screenHeight - 300.0f);
    blockButton.radius = 40.0f;
    blockButton.visible = true;
    preset.buttons.push_back(blockButton);

    // Sprint button (left side)
    VirtualButton sprintButton;
    sprintButton.id = "sprint";
    sprintButton.label = "Sprint";
    sprintButton.position = glm::vec2(220.0f, m_screenHeight - 300.0f);
    sprintButton.radius = 35.0f;
    sprintButton.visible = true;
    preset.buttons.push_back(sprintButton);

    // Pause button (top right)
    VirtualButton pauseButton;
    pauseButton.id = "pause";
    pauseButton.label = "||";
    pauseButton.position = glm::vec2(m_screenWidth - 50.0f, 50.0f);
    pauseButton.radius = 30.0f;
    pauseButton.visible = true;
    preset.buttons.push_back(pauseButton);

    // Camera swipe zone (center/top area)
    SwipeZone cameraZone;
    cameraZone.id = "camera_swipe";
    cameraZone.position = glm::vec2(m_screenWidth * 0.3f, 0.0f);
    cameraZone.size = glm::vec2(m_screenWidth * 0.4f, m_screenHeight * 0.5f);
    cameraZone.detectSwipe = true;
    preset.swipeZones.push_back(cameraZone);

    return preset;
}

MobileLayoutPreset MobileControlsSystem::createLeftyLayout() {
    MobileLayoutPreset preset = createDefaultLayout();
    preset.name = "lefty";
    preset.description = "Left-handed control layout";

    // Mirror all controls horizontally
    for (auto& joystick : preset.joysticks) {
        joystick.centerPosition.x = m_screenWidth - joystick.centerPosition.x;
    }
    for (auto& button : preset.buttons) {
        button.position.x = m_screenWidth - button.position.x;
    }

    return preset;
}

MobileLayoutPreset MobileControlsSystem::createSimpleLayout() {
    MobileLayoutPreset preset;
    preset.name = "simple";
    preset.description = "Simplified gesture-based layout";

    // Only movement joystick
    VirtualJoystick moveJoystick;
    moveJoystick.id = "movement";
    moveJoystick.centerPosition = glm::vec2(120.0f, m_screenHeight - 150.0f);
    moveJoystick.outerRadius = 80.0f;
    moveJoystick.innerRadius = 30.0f;
    moveJoystick.deadzone = 0.15f;
    moveJoystick.isDynamic = true;  // Appears where you touch
    moveJoystick.visible = true;
    preset.joysticks.push_back(moveJoystick);

    // Single action button
    VirtualButton actionButton;
    actionButton.id = "action";
    actionButton.label = "Action";
    actionButton.position = glm::vec2(m_screenWidth - 100.0f, m_screenHeight - 100.0f);
    actionButton.radius = 60.0f;
    actionButton.visible = true;
    preset.buttons.push_back(actionButton);

    // Large swipe zone for gestures
    SwipeZone gestureZone;
    gestureZone.id = "gestures";
    gestureZone.position = glm::vec2(m_screenWidth * 0.25f, 0.0f);
    gestureZone.size = glm::vec2(m_screenWidth * 0.5f, m_screenHeight);
    gestureZone.detectSwipe = true;
    gestureZone.detectPinch = true;
    preset.swipeZones.push_back(gestureZone);

    return preset;
}

MobileLayoutPreset MobileControlsSystem::createProLayout() {
    MobileLayoutPreset preset = createDefaultLayout();
    preset.name = "pro";
    preset.description = "Advanced layout with all options";

    // Enable camera joystick
    for (auto& joystick : preset.joysticks) {
        if (joystick.id == "camera") {
            joystick.visible = true;
        }
    }

    // Add extra action buttons
    VirtualButton extra1;
    extra1.id = "extra1";
    extra1.label = "E1";
    extra1.position = glm::vec2(m_screenWidth - 300.0f, m_screenHeight - 100.0f);
    extra1.radius = 35.0f;
    extra1.visible = true;
    preset.buttons.push_back(extra1);

    VirtualButton extra2;
    extra2.id = "extra2";
    extra2.label = "E2";
    extra2.position = glm::vec2(m_screenWidth - 300.0f, m_screenHeight - 200.0f);
    extra2.radius = 35.0f;
    extra2.visible = true;
    preset.buttons.push_back(extra2);

    return preset;
}

} // namespace Game
