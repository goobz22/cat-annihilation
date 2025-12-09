#ifndef ENGINE_CORE_INPUT_HPP
#define ENGINE_CORE_INPUT_HPP

#include "Types.hpp"
#include <GLFW/glfw3.h>
#include <array>
#include <bitset>

namespace Engine {

/**
 * @brief Input state management for keyboard, mouse, and gamepad
 *
 * Tracks current and previous frame state for edge detection.
 * Provides methods for checking key presses, releases, and holds.
 */
class Input {
public:
    /**
     * @brief Key codes matching GLFW key definitions
     */
    enum class Key : i32 {
        Unknown = GLFW_KEY_UNKNOWN,

        // Printable keys
        Space = GLFW_KEY_SPACE,
        Apostrophe = GLFW_KEY_APOSTROPHE,
        Comma = GLFW_KEY_COMMA,
        Minus = GLFW_KEY_MINUS,
        Period = GLFW_KEY_PERIOD,
        Slash = GLFW_KEY_SLASH,

        Num0 = GLFW_KEY_0,
        Num1 = GLFW_KEY_1,
        Num2 = GLFW_KEY_2,
        Num3 = GLFW_KEY_3,
        Num4 = GLFW_KEY_4,
        Num5 = GLFW_KEY_5,
        Num6 = GLFW_KEY_6,
        Num7 = GLFW_KEY_7,
        Num8 = GLFW_KEY_8,
        Num9 = GLFW_KEY_9,

        Semicolon = GLFW_KEY_SEMICOLON,
        Equal = GLFW_KEY_EQUAL,

        A = GLFW_KEY_A,
        B = GLFW_KEY_B,
        C = GLFW_KEY_C,
        D = GLFW_KEY_D,
        E = GLFW_KEY_E,
        F = GLFW_KEY_F,
        G = GLFW_KEY_G,
        H = GLFW_KEY_H,
        I = GLFW_KEY_I,
        J = GLFW_KEY_J,
        K = GLFW_KEY_K,
        L = GLFW_KEY_L,
        M = GLFW_KEY_M,
        N = GLFW_KEY_N,
        O = GLFW_KEY_O,
        P = GLFW_KEY_P,
        Q = GLFW_KEY_Q,
        R = GLFW_KEY_R,
        S = GLFW_KEY_S,
        T = GLFW_KEY_T,
        U = GLFW_KEY_U,
        V = GLFW_KEY_V,
        W = GLFW_KEY_W,
        X = GLFW_KEY_X,
        Y = GLFW_KEY_Y,
        Z = GLFW_KEY_Z,

        LeftBracket = GLFW_KEY_LEFT_BRACKET,
        Backslash = GLFW_KEY_BACKSLASH,
        RightBracket = GLFW_KEY_RIGHT_BRACKET,
        GraveAccent = GLFW_KEY_GRAVE_ACCENT,

        // Function keys
        Escape = GLFW_KEY_ESCAPE,
        Enter = GLFW_KEY_ENTER,
        Tab = GLFW_KEY_TAB,
        Backspace = GLFW_KEY_BACKSPACE,
        Insert = GLFW_KEY_INSERT,
        Delete = GLFW_KEY_DELETE,
        Right = GLFW_KEY_RIGHT,
        Left = GLFW_KEY_LEFT,
        Down = GLFW_KEY_DOWN,
        Up = GLFW_KEY_UP,
        PageUp = GLFW_KEY_PAGE_UP,
        PageDown = GLFW_KEY_PAGE_DOWN,
        Home = GLFW_KEY_HOME,
        End = GLFW_KEY_END,
        CapsLock = GLFW_KEY_CAPS_LOCK,
        ScrollLock = GLFW_KEY_SCROLL_LOCK,
        NumLock = GLFW_KEY_NUM_LOCK,
        PrintScreen = GLFW_KEY_PRINT_SCREEN,
        Pause = GLFW_KEY_PAUSE,

        F1 = GLFW_KEY_F1,
        F2 = GLFW_KEY_F2,
        F3 = GLFW_KEY_F3,
        F4 = GLFW_KEY_F4,
        F5 = GLFW_KEY_F5,
        F6 = GLFW_KEY_F6,
        F7 = GLFW_KEY_F7,
        F8 = GLFW_KEY_F8,
        F9 = GLFW_KEY_F9,
        F10 = GLFW_KEY_F10,
        F11 = GLFW_KEY_F11,
        F12 = GLFW_KEY_F12,

        // Keypad
        Keypad0 = GLFW_KEY_KP_0,
        Keypad1 = GLFW_KEY_KP_1,
        Keypad2 = GLFW_KEY_KP_2,
        Keypad3 = GLFW_KEY_KP_3,
        Keypad4 = GLFW_KEY_KP_4,
        Keypad5 = GLFW_KEY_KP_5,
        Keypad6 = GLFW_KEY_KP_6,
        Keypad7 = GLFW_KEY_KP_7,
        Keypad8 = GLFW_KEY_KP_8,
        Keypad9 = GLFW_KEY_KP_9,
        KeypadDecimal = GLFW_KEY_KP_DECIMAL,
        KeypadDivide = GLFW_KEY_KP_DIVIDE,
        KeypadMultiply = GLFW_KEY_KP_MULTIPLY,
        KeypadSubtract = GLFW_KEY_KP_SUBTRACT,
        KeypadAdd = GLFW_KEY_KP_ADD,
        KeypadEnter = GLFW_KEY_KP_ENTER,
        KeypadEqual = GLFW_KEY_KP_EQUAL,

        // Modifiers
        LeftShift = GLFW_KEY_LEFT_SHIFT,
        LeftControl = GLFW_KEY_LEFT_CONTROL,
        LeftAlt = GLFW_KEY_LEFT_ALT,
        LeftSuper = GLFW_KEY_LEFT_SUPER,
        RightShift = GLFW_KEY_RIGHT_SHIFT,
        RightControl = GLFW_KEY_RIGHT_CONTROL,
        RightAlt = GLFW_KEY_RIGHT_ALT,
        RightSuper = GLFW_KEY_RIGHT_SUPER,
        Menu = GLFW_KEY_MENU,

        KeyCount = GLFW_KEY_LAST + 1
    };

    /**
     * @brief Mouse button codes
     */
    enum class MouseButton : u8 {
        Left = GLFW_MOUSE_BUTTON_LEFT,
        Right = GLFW_MOUSE_BUTTON_RIGHT,
        Middle = GLFW_MOUSE_BUTTON_MIDDLE,
        Button4 = GLFW_MOUSE_BUTTON_4,
        Button5 = GLFW_MOUSE_BUTTON_5,
        Button6 = GLFW_MOUSE_BUTTON_6,
        Button7 = GLFW_MOUSE_BUTTON_7,
        Button8 = GLFW_MOUSE_BUTTON_8,

        ButtonCount = GLFW_MOUSE_BUTTON_LAST + 1
    };

    /**
     * @brief Gamepad button codes
     */
    enum class GamepadButton : u8 {
        A = GLFW_GAMEPAD_BUTTON_A,
        B = GLFW_GAMEPAD_BUTTON_B,
        X = GLFW_GAMEPAD_BUTTON_X,
        Y = GLFW_GAMEPAD_BUTTON_Y,
        LeftBumper = GLFW_GAMEPAD_BUTTON_LEFT_BUMPER,
        RightBumper = GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER,
        Back = GLFW_GAMEPAD_BUTTON_BACK,
        Start = GLFW_GAMEPAD_BUTTON_START,
        Guide = GLFW_GAMEPAD_BUTTON_GUIDE,
        LeftThumb = GLFW_GAMEPAD_BUTTON_LEFT_THUMB,
        RightThumb = GLFW_GAMEPAD_BUTTON_RIGHT_THUMB,
        DpadUp = GLFW_GAMEPAD_BUTTON_DPAD_UP,
        DpadRight = GLFW_GAMEPAD_BUTTON_DPAD_RIGHT,
        DpadDown = GLFW_GAMEPAD_BUTTON_DPAD_DOWN,
        DpadLeft = GLFW_GAMEPAD_BUTTON_DPAD_LEFT,

        ButtonCount = GLFW_GAMEPAD_BUTTON_LAST + 1
    };

    /**
     * @brief Gamepad axis codes
     */
    enum class GamepadAxis : u8 {
        LeftX = GLFW_GAMEPAD_AXIS_LEFT_X,
        LeftY = GLFW_GAMEPAD_AXIS_LEFT_Y,
        RightX = GLFW_GAMEPAD_AXIS_RIGHT_X,
        RightY = GLFW_GAMEPAD_AXIS_RIGHT_Y,
        LeftTrigger = GLFW_GAMEPAD_AXIS_LEFT_TRIGGER,
        RightTrigger = GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER,

        AxisCount = GLFW_GAMEPAD_AXIS_LAST + 1
    };

    /**
     * @brief Construct input system for a GLFW window
     * @param window GLFW window handle
     */
    explicit Input(GLFWwindow* window);

    /**
     * @brief Update input state (call once per frame)
     */
    void update();

    // Keyboard input
    /**
     * @brief Check if key is currently down
     * @param key Key to check
     * @return true if key is down
     */
    bool isKeyDown(Key key) const;

    /**
     * @brief Check if key was just pressed this frame
     * @param key Key to check
     * @return true if key was just pressed
     */
    bool isKeyPressed(Key key) const;

    /**
     * @brief Check if key was just released this frame
     * @param key Key to check
     * @return true if key was just released
     */
    bool isKeyReleased(Key key) const;

    // Mouse input
    /**
     * @brief Check if mouse button is currently down
     * @param button Mouse button to check
     * @return true if button is down
     */
    bool isMouseButtonDown(MouseButton button) const;

    /**
     * @brief Check if mouse button was just pressed this frame
     * @param button Mouse button to check
     * @return true if button was just pressed
     */
    bool isMouseButtonPressed(MouseButton button) const;

    /**
     * @brief Check if mouse button was just released this frame
     * @param button Mouse button to check
     * @return true if button was just released
     */
    bool isMouseButtonReleased(MouseButton button) const;

    /**
     * @brief Get current mouse position
     * @param x Output X coordinate
     * @param y Output Y coordinate
     */
    void getMousePosition(f64& x, f64& y) const;

    /**
     * @brief Get mouse position delta since last frame
     * @param dx Output X delta
     * @param dy Output Y delta
     */
    void getMouseDelta(f64& dx, f64& dy) const;

    /**
     * @brief Get mouse scroll wheel delta
     * @param dx Output horizontal scroll
     * @param dy Output vertical scroll
     */
    void getScrollDelta(f64& dx, f64& dy) const;

    /**
     * @brief Set mouse cursor mode
     * @param disabled true to disable and hide cursor
     */
    void setCursorDisabled(bool disabled);

    // Gamepad input
    /**
     * @brief Check if gamepad is connected
     * @param gamepadId Gamepad index (0-15)
     * @return true if gamepad is connected
     */
    bool isGamepadConnected(u8 gamepadId = 0) const;

    /**
     * @brief Check if gamepad button is down
     * @param button Gamepad button to check
     * @param gamepadId Gamepad index (0-15)
     * @return true if button is down
     */
    bool isGamepadButtonDown(GamepadButton button, u8 gamepadId = 0) const;

    /**
     * @brief Check if gamepad button was just pressed
     * @param button Gamepad button to check
     * @param gamepadId Gamepad index (0-15)
     * @return true if button was just pressed
     */
    bool isGamepadButtonPressed(GamepadButton button, u8 gamepadId = 0) const;

    /**
     * @brief Check if gamepad button was just released
     * @param button Gamepad button to check
     * @param gamepadId Gamepad index (0-15)
     * @return true if button was just released
     */
    bool isGamepadButtonReleased(GamepadButton button, u8 gamepadId = 0) const;

    /**
     * @brief Get gamepad axis value
     * @param axis Axis to read
     * @param gamepadId Gamepad index (0-15)
     * @return Axis value (-1.0 to 1.0)
     */
    f32 getGamepadAxis(GamepadAxis axis, u8 gamepadId = 0) const;

    /**
     * @brief Get gamepad name
     * @param gamepadId Gamepad index (0-15)
     * @return Gamepad name or empty string if not connected
     */
    const char* getGamepadName(u8 gamepadId = 0) const;

private:
    // GLFW callbacks
    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void scrollCallback(GLFWwindow* window, f64 xoffset, f64 yoffset);

    void setupCallbacks();

    GLFWwindow* m_window;

    // Keyboard state
    static constexpr size_t KEY_COUNT = static_cast<size_t>(Key::KeyCount);
    std::bitset<KEY_COUNT> m_keysCurrentFrame;
    std::bitset<KEY_COUNT> m_keysPreviousFrame;

    // Mouse state
    static constexpr u32 MOUSE_BUTTON_COUNT = static_cast<u32>(MouseButton::ButtonCount);
    std::bitset<MOUSE_BUTTON_COUNT> m_mouseCurrentFrame;
    std::bitset<MOUSE_BUTTON_COUNT> m_mousePreviousFrame;

    f64 m_mouseX = 0.0;
    f64 m_mouseY = 0.0;
    f64 m_mousePrevX = 0.0;
    f64 m_mousePrevY = 0.0;
    f64 m_scrollX = 0.0;
    f64 m_scrollY = 0.0;

    // Gamepad state (up to 16 gamepads)
    static constexpr u32 MAX_GAMEPADS = GLFW_JOYSTICK_LAST + 1;
    static constexpr u32 GAMEPAD_BUTTON_COUNT = static_cast<u32>(GamepadButton::ButtonCount);

    struct GamepadState {
        bool connected = false;
        std::bitset<GAMEPAD_BUTTON_COUNT> buttonsCurrentFrame;
        std::bitset<GAMEPAD_BUTTON_COUNT> buttonsPreviousFrame;
        std::array<f32, static_cast<u32>(GamepadAxis::AxisCount)> axes{};
    };

    std::array<GamepadState, MAX_GAMEPADS> m_gamepads;
};

} // namespace Engine

#endif // ENGINE_CORE_INPUT_HPP
