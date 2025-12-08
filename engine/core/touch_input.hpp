#ifndef ENGINE_CORE_TOUCH_INPUT_HPP
#define ENGINE_CORE_TOUCH_INPUT_HPP

#include "Types.hpp"
#include <GLFW/glfw3.h>
#include <vector>
#include <array>
#include <functional>
#include <glm/glm.hpp>

namespace Engine {

/**
 * @brief Touch point representing a single finger contact
 */
struct TouchPoint {
    int id;                          // Unique touch identifier
    glm::vec2 position;              // Current screen position
    glm::vec2 previousPosition;      // Previous frame position
    glm::vec2 startPosition;         // Position where touch began
    float pressure;                  // Touch pressure (0-1)
    float timestamp;                 // Time when touch started
    float lastUpdateTime;            // Time of last position update
    bool isActive;                   // Whether touch is currently active

    TouchPoint()
        : id(-1)
        , position(0.0f, 0.0f)
        , previousPosition(0.0f, 0.0f)
        , startPosition(0.0f, 0.0f)
        , pressure(0.0f)
        , timestamp(0.0f)
        , lastUpdateTime(0.0f)
        , isActive(false) {}
};

/**
 * @brief Swipe gesture data
 */
struct SwipeGesture {
    glm::vec2 startPosition;         // Where swipe started
    glm::vec2 currentPosition;       // Current swipe position
    glm::vec2 endPosition;           // Where swipe ended (if complete)
    glm::vec2 direction;             // Normalized swipe direction
    float distance;                  // Total distance swiped
    float velocity;                  // Swipe velocity (pixels/second)
    float duration;                  // Time swipe took (seconds)
    bool isActive;                   // Whether swipe is in progress
    bool justCompleted;              // True for one frame after completion
    int touchId;                     // ID of touch performing swipe

    SwipeGesture()
        : startPosition(0.0f, 0.0f)
        , currentPosition(0.0f, 0.0f)
        , endPosition(0.0f, 0.0f)
        , direction(0.0f, 0.0f)
        , distance(0.0f)
        , velocity(0.0f)
        , duration(0.0f)
        , isActive(false)
        , justCompleted(false)
        , touchId(-1) {}
};

/**
 * @brief Pinch gesture data (two-finger zoom/scale)
 */
struct PinchGesture {
    glm::vec2 center;                // Center point between fingers
    float initialDistance;           // Starting distance between fingers
    float currentDistance;           // Current distance between fingers
    float scale;                     // Scale factor (1.0 = no change)
    float deltaScale;                // Change in scale this frame
    bool isActive;                   // Whether pinch is in progress
    int touchId1;                    // First touch ID
    int touchId2;                    // Second touch ID

    PinchGesture()
        : center(0.0f, 0.0f)
        , initialDistance(0.0f)
        , currentDistance(0.0f)
        , scale(1.0f)
        , deltaScale(0.0f)
        , isActive(false)
        , touchId1(-1)
        , touchId2(-1) {}
};

/**
 * @brief Rotation gesture data (two-finger rotation)
 */
struct RotationGesture {
    glm::vec2 center;                // Center point between fingers
    float initialAngle;              // Starting angle between fingers
    float currentAngle;              // Current angle
    float deltaAngle;                // Change in angle this frame (radians)
    float totalRotation;             // Total rotation since start (radians)
    bool isActive;                   // Whether rotation is in progress
    int touchId1;                    // First touch ID
    int touchId2;                    // Second touch ID

    RotationGesture()
        : center(0.0f, 0.0f)
        , initialAngle(0.0f)
        , currentAngle(0.0f)
        , deltaAngle(0.0f)
        , totalRotation(0.0f)
        , isActive(false)
        , touchId1(-1)
        , touchId2(-1) {}
};

/**
 * @brief Tap gesture data
 */
struct TapGesture {
    glm::vec2 position;              // Where tap occurred
    int tapCount;                    // Number of taps (1=single, 2=double)
    float timestamp;                 // When tap occurred
    bool detected;                   // True for one frame after detection

    TapGesture()
        : position(0.0f, 0.0f)
        , tapCount(0)
        , timestamp(0.0f)
        , detected(false) {}
};

/**
 * @brief Long press gesture data
 */
struct LongPressGesture {
    glm::vec2 position;              // Where long press occurred
    float duration;                  // How long pressed (seconds)
    float threshold;                 // Time threshold for detection (default 0.5s)
    bool isActive;                   // Whether long press is in progress
    bool justTriggered;              // True for one frame when threshold reached
    int touchId;                     // ID of touch performing long press

    LongPressGesture()
        : position(0.0f, 0.0f)
        , duration(0.0f)
        , threshold(0.5f)
        , isActive(false)
        , justTriggered(false)
        , touchId(-1) {}
};

/**
 * @brief Touch input system for mobile/touchscreen devices
 *
 * Handles raw touch events and provides high-level gesture detection.
 * Integrates with GLFW for touch input on supported platforms.
 */
class TouchInput {
public:
    // Gesture detection thresholds
    struct GestureThresholds {
        float swipeMinDistance = 50.0f;       // Minimum pixels to trigger swipe
        float swipeMinVelocity = 100.0f;      // Minimum velocity for swipe
        float tapMaxMovement = 10.0f;         // Max movement for tap detection
        float tapMaxDuration = 0.3f;          // Max time for tap
        float doubleTapMaxDelay = 0.3f;       // Max time between double taps
        float longPressMinDuration = 0.5f;    // Min time for long press
        float longPressMaxMovement = 10.0f;   // Max movement for long press
        float pinchMinDistance = 20.0f;       // Minimum distance change for pinch
        float rotationMinAngle = 0.1f;        // Minimum rotation (radians)
    };

    /**
     * @brief Construct touch input system
     * @param window GLFW window handle
     */
    explicit TouchInput(GLFWwindow* window);

    /**
     * @brief Destructor
     */
    ~TouchInput();

    /**
     * @brief Initialize touch input system
     */
    void initialize();

    /**
     * @brief Update touch input (call once per frame)
     * @param deltaTime Time since last frame in seconds
     */
    void update(float deltaTime);

    /**
     * @brief Shutdown touch input system
     */
    void shutdown();

    // Raw touch access
    /**
     * @brief Get all active touches
     */
    const std::vector<TouchPoint>& getActiveTouches() const { return m_activeTouches; }

    /**
     * @brief Get specific touch by ID
     * @param touchId Touch identifier
     * @return Pointer to touch point, or nullptr if not found
     */
    const TouchPoint* getTouch(int touchId) const;

    /**
     * @brief Get number of active touches
     */
    int getActiveTouchCount() const { return static_cast<int>(m_activeTouches.size()); }

    // Gesture detection
    /**
     * @brief Detect tap gesture
     * @param outPosition Output tap position
     * @return true if tap detected this frame
     */
    bool detectTap(glm::vec2& outPosition);

    /**
     * @brief Detect double tap gesture
     * @param outPosition Output tap position
     * @return true if double tap detected this frame
     */
    bool detectDoubleTap(glm::vec2& outPosition);

    /**
     * @brief Detect long press gesture
     * @param outPosition Output long press position
     * @return true if long press triggered this frame
     */
    bool detectLongPress(glm::vec2& outPosition);

    /**
     * @brief Detect swipe gesture
     * @param outSwipe Output swipe gesture data
     * @return true if swipe detected this frame
     */
    bool detectSwipe(SwipeGesture& outSwipe);

    /**
     * @brief Detect pinch gesture
     * @param outScale Output scale factor
     * @return true if pinch is active
     */
    bool detectPinch(float& outScale);

    /**
     * @brief Detect rotation gesture
     * @param outAngle Output rotation angle (radians)
     * @return true if rotation is active
     */
    bool detectRotation(float& outAngle);

    /**
     * @brief Get current swipe gesture state
     */
    const SwipeGesture& getCurrentSwipe() const { return m_currentSwipe; }

    /**
     * @brief Get current pinch gesture state
     */
    const PinchGesture& getCurrentPinch() const { return m_currentPinch; }

    /**
     * @brief Get current rotation gesture state
     */
    const RotationGesture& getCurrentRotation() const { return m_currentRotation; }

    /**
     * @brief Get last tap gesture
     */
    const TapGesture& getLastTap() const { return m_lastTap; }

    /**
     * @brief Get current long press gesture
     */
    const LongPressGesture& getCurrentLongPress() const { return m_currentLongPress; }

    // Callbacks
    /**
     * @brief Set callback for touch down events
     */
    void setTouchDownCallback(std::function<void(int, glm::vec2)> callback) {
        m_touchDownCallback = callback;
    }

    /**
     * @brief Set callback for touch move events
     */
    void setTouchMoveCallback(std::function<void(int, glm::vec2)> callback) {
        m_touchMoveCallback = callback;
    }

    /**
     * @brief Set callback for touch up events
     */
    void setTouchUpCallback(std::function<void(int, glm::vec2)> callback) {
        m_touchUpCallback = callback;
    }

    /**
     * @brief Set callback for tap gestures
     */
    void setTapCallback(std::function<void(glm::vec2, int)> callback) {
        m_tapCallback = callback;
    }

    /**
     * @brief Set callback for swipe gestures
     */
    void setSwipeCallback(std::function<void(const SwipeGesture&)> callback) {
        m_swipeCallback = callback;
    }

    /**
     * @brief Set callback for pinch gestures
     */
    void setPinchCallback(std::function<void(float)> callback) {
        m_pinchCallback = callback;
    }

    // Configuration
    /**
     * @brief Get gesture thresholds
     */
    GestureThresholds& getThresholds() { return m_thresholds; }

    /**
     * @brief Set gesture thresholds
     */
    void setThresholds(const GestureThresholds& thresholds) { m_thresholds = thresholds; }

    /**
     * @brief Check if device has touch support
     */
    bool hasTouchSupport() const { return m_hasTouchSupport; }

    /**
     * @brief Enable/disable touch input
     */
    void setEnabled(bool enabled) { m_enabled = enabled; }

    /**
     * @brief Check if touch input is enabled
     */
    bool isEnabled() const { return m_enabled; }

    // Internal touch event handlers (called by GLFW callbacks)
    void onTouchDown(int touchId, glm::vec2 position, float pressure = 1.0f);
    void onTouchMove(int touchId, glm::vec2 position);
    void onTouchUp(int touchId, glm::vec2 position);
    void onTouchCancel(int touchId);

private:
    // Touch tracking
    void updateGestures(float deltaTime);
    void updateSwipeGesture(float deltaTime);
    void updatePinchGesture();
    void updateRotationGesture();
    void updateTapGesture(float deltaTime);
    void updateLongPressGesture(float deltaTime);

    // Helper functions
    TouchPoint* findTouch(int touchId);
    void removeTouchById(int touchId);
    float getTime() const;

    // GLFW callbacks
    static void cursorPositionCallback(GLFWwindow* window, double xpos, double ypos);
    static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);

    GLFWwindow* m_window;
    bool m_initialized;
    bool m_enabled;
    bool m_hasTouchSupport;

    // Active touches
    std::vector<TouchPoint> m_activeTouches;
    int m_nextTouchId;

    // Gesture state
    GestureThresholds m_thresholds;
    SwipeGesture m_currentSwipe;
    PinchGesture m_currentPinch;
    RotationGesture m_currentRotation;
    TapGesture m_lastTap;
    LongPressGesture m_currentLongPress;

    // Tap tracking
    float m_lastTapTime;
    glm::vec2 m_lastTapPosition;

    // Callbacks
    std::function<void(int, glm::vec2)> m_touchDownCallback;
    std::function<void(int, glm::vec2)> m_touchMoveCallback;
    std::function<void(int, glm::vec2)> m_touchUpCallback;
    std::function<void(glm::vec2, int)> m_tapCallback;
    std::function<void(const SwipeGesture&)> m_swipeCallback;
    std::function<void(float)> m_pinchCallback;

    // Simulated touch support (using mouse as single touch)
    bool m_simulateTouch;
    bool m_mouseDown;
    glm::vec2 m_mousePosition;
};

} // namespace Engine

#endif // ENGINE_CORE_TOUCH_INPUT_HPP
