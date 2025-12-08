#include "Input.hpp"
#include <cstring>

namespace Engine {

Input::Input(GLFWwindow* window)
    : m_window(window) {

    if (!m_window) {
        throw std::runtime_error("Input system requires valid GLFW window");
    }

    // Store this pointer for callbacks
    glfwSetWindowUserPointer(m_window, this);

    // Setup callbacks
    setupCallbacks();

    // Initialize mouse position
    glfwGetCursorPos(m_window, &m_mouseX, &m_mouseY);
    m_mousePrevX = m_mouseX;
    m_mousePrevY = m_mouseY;
}

void Input::update() {
    // Update previous frame state
    m_keysPreviousFrame = m_keysCurrentFrame;
    m_mousePreviousFrame = m_mouseCurrentFrame;

    // Update current keyboard state
    for (u32 i = 0; i < KEY_COUNT; ++i) {
        int state = glfwGetKey(m_window, static_cast<int>(i));
        m_keysCurrentFrame[i] = (state == GLFW_PRESS || state == GLFW_REPEAT);
    }

    // Update current mouse button state
    for (u32 i = 0; i < MOUSE_BUTTON_COUNT; ++i) {
        int state = glfwGetMouseButton(m_window, static_cast<int>(i));
        m_mouseCurrentFrame[i] = (state == GLFW_PRESS);
    }

    // Update mouse position and delta
    m_mousePrevX = m_mouseX;
    m_mousePrevY = m_mouseY;
    glfwGetCursorPos(m_window, &m_mouseX, &m_mouseY);

    // Reset scroll delta (updated via callback)
    m_scrollX = 0.0;
    m_scrollY = 0.0;

    // Update gamepad state
    for (u32 gamepadId = 0; gamepadId < MAX_GAMEPADS; ++gamepadId) {
        auto& gamepad = m_gamepads[gamepadId];
        gamepad.buttonsPreviousFrame = gamepad.buttonsCurrentFrame;

        // Check if gamepad is connected
        int jid = GLFW_JOYSTICK_1 + gamepadId;
        gamepad.connected = glfwJoystickPresent(jid) && glfwJoystickIsGamepad(jid);

        if (gamepad.connected) {
            GLFWgamepadstate state;
            if (glfwGetGamepadState(jid, &state)) {
                // Update button state
                for (u32 i = 0; i < GAMEPAD_BUTTON_COUNT; ++i) {
                    gamepad.buttonsCurrentFrame[i] = (state.buttons[i] == GLFW_PRESS);
                }

                // Update axes
                for (u32 i = 0; i < static_cast<u32>(GamepadAxis::AxisCount); ++i) {
                    gamepad.axes[i] = state.axes[i];
                }
            }
        } else {
            // Clear state if disconnected
            gamepad.buttonsCurrentFrame.reset();
            gamepad.axes.fill(0.0f);
        }
    }
}

bool Input::isKeyDown(Key key) const {
    u32 keyCode = static_cast<u32>(key);
    return keyCode < KEY_COUNT && m_keysCurrentFrame[keyCode];
}

bool Input::isKeyPressed(Key key) const {
    u32 keyCode = static_cast<u32>(key);
    if (keyCode >= KEY_COUNT) return false;
    return m_keysCurrentFrame[keyCode] && !m_keysPreviousFrame[keyCode];
}

bool Input::isKeyReleased(Key key) const {
    u32 keyCode = static_cast<u32>(key);
    if (keyCode >= KEY_COUNT) return false;
    return !m_keysCurrentFrame[keyCode] && m_keysPreviousFrame[keyCode];
}

bool Input::isMouseButtonDown(MouseButton button) const {
    u32 buttonCode = static_cast<u32>(button);
    return buttonCode < MOUSE_BUTTON_COUNT && m_mouseCurrentFrame[buttonCode];
}

bool Input::isMouseButtonPressed(MouseButton button) const {
    u32 buttonCode = static_cast<u32>(button);
    if (buttonCode >= MOUSE_BUTTON_COUNT) return false;
    return m_mouseCurrentFrame[buttonCode] && !m_mousePreviousFrame[buttonCode];
}

bool Input::isMouseButtonReleased(MouseButton button) const {
    u32 buttonCode = static_cast<u32>(button);
    if (buttonCode >= MOUSE_BUTTON_COUNT) return false;
    return !m_mouseCurrentFrame[buttonCode] && m_mousePreviousFrame[buttonCode];
}

void Input::getMousePosition(f64& x, f64& y) const {
    x = m_mouseX;
    y = m_mouseY;
}

void Input::getMouseDelta(f64& dx, f64& dy) const {
    dx = m_mouseX - m_mousePrevX;
    dy = m_mouseY - m_mousePrevY;
}

void Input::getScrollDelta(f64& dx, f64& dy) const {
    dx = m_scrollX;
    dy = m_scrollY;
}

void Input::setCursorDisabled(bool disabled) {
    if (m_window) {
        int mode = disabled ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL;
        glfwSetInputMode(m_window, GLFW_CURSOR, mode);
    }
}

bool Input::isGamepadConnected(u8 gamepadId) const {
    return gamepadId < MAX_GAMEPADS && m_gamepads[gamepadId].connected;
}

bool Input::isGamepadButtonDown(GamepadButton button, u8 gamepadId) const {
    if (gamepadId >= MAX_GAMEPADS || !m_gamepads[gamepadId].connected) {
        return false;
    }

    u32 buttonCode = static_cast<u32>(button);
    return buttonCode < GAMEPAD_BUTTON_COUNT &&
           m_gamepads[gamepadId].buttonsCurrentFrame[buttonCode];
}

bool Input::isGamepadButtonPressed(GamepadButton button, u8 gamepadId) const {
    if (gamepadId >= MAX_GAMEPADS || !m_gamepads[gamepadId].connected) {
        return false;
    }

    u32 buttonCode = static_cast<u32>(button);
    if (buttonCode >= GAMEPAD_BUTTON_COUNT) return false;

    const auto& gamepad = m_gamepads[gamepadId];
    return gamepad.buttonsCurrentFrame[buttonCode] &&
           !gamepad.buttonsPreviousFrame[buttonCode];
}

bool Input::isGamepadButtonReleased(GamepadButton button, u8 gamepadId) const {
    if (gamepadId >= MAX_GAMEPADS || !m_gamepads[gamepadId].connected) {
        return false;
    }

    u32 buttonCode = static_cast<u32>(button);
    if (buttonCode >= GAMEPAD_BUTTON_COUNT) return false;

    const auto& gamepad = m_gamepads[gamepadId];
    return !gamepad.buttonsCurrentFrame[buttonCode] &&
           gamepad.buttonsPreviousFrame[buttonCode];
}

f32 Input::getGamepadAxis(GamepadAxis axis, u8 gamepadId) const {
    if (gamepadId >= MAX_GAMEPADS || !m_gamepads[gamepadId].connected) {
        return 0.0f;
    }

    u32 axisCode = static_cast<u32>(axis);
    if (axisCode >= static_cast<u32>(GamepadAxis::AxisCount)) {
        return 0.0f;
    }

    return m_gamepads[gamepadId].axes[axisCode];
}

const char* Input::getGamepadName(u8 gamepadId) const {
    if (gamepadId >= MAX_GAMEPADS || !m_gamepads[gamepadId].connected) {
        return "";
    }

    int jid = GLFW_JOYSTICK_1 + gamepadId;
    return glfwGetGamepadName(jid);
}

void Input::setupCallbacks() {
    glfwSetKeyCallback(m_window, keyCallback);
    glfwSetMouseButtonCallback(m_window, mouseButtonCallback);
    glfwSetScrollCallback(m_window, scrollCallback);
}

void Input::keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    // Key state is polled in update(), so this callback is optional
    // Can be used for text input or immediate response if needed
}

void Input::mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    // Mouse button state is polled in update()
}

void Input::scrollCallback(GLFWwindow* window, f64 xoffset, f64 yoffset) {
    auto* input = static_cast<Input*>(glfwGetWindowUserPointer(window));
    if (input) {
        input->m_scrollX = xoffset;
        input->m_scrollY = yoffset;
    }
}

} // namespace Engine
