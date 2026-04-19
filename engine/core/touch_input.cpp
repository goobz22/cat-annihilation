#include "touch_input.hpp"
#include "Logger.hpp"
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <numbers>

// MSVC does not expose the POSIX M_PI macro by default, so using it across
// platforms is an inconsistency landmine. The C++20 <numbers> header gives
// us a compile-time constant that works on every target.
namespace {
constexpr float kPi = static_cast<float>(std::numbers::pi);
} // namespace

namespace Engine {

TouchInput::TouchInput(GLFWwindow* window)
    : m_window(window)
    , m_initialized(false)
    , m_enabled(true)
    , m_hasTouchSupport(false)
    , m_nextTouchId(0)
    , m_lastTapTime(0.0f)
    , m_lastTapPosition(0.0f, 0.0f)
    , m_simulateTouch(true)  // Enable mouse simulation by default
    , m_mouseDown(false)
    , m_mousePosition(0.0f, 0.0f)
{
}

TouchInput::~TouchInput() {
    shutdown();
}

void TouchInput::initialize() {
    if (m_initialized) {
        Logger::warn("TouchInput already initialized");
        return;
    }

    // GLFW 3.x has no cross-platform touch API — multi-touch on Windows
    // requires WM_TOUCH wiring, on Linux it requires libinput, and on mobile
    // it requires the platform's native GLFW port. Rather than return
    // "hasTouchSupport == false" silently (which made MobileControlsSystem
    // think touch was universally unavailable even on tablets), we flip
    // m_hasTouchSupport to true when the simulation shim is hot — from the
    // rest of the engine's POV there IS a working single-touch stream; it
    // just happens to be backed by the mouse on desktop builds.
    //
    // A future platform TouchInput subclass can override initialize() to
    // hook real WM_TOUCH / libinput / Android MotionEvent sources and
    // leave m_simulateTouch = false while still reporting hasTouchSupport.
    m_hasTouchSupport = true;
    m_simulateTouch = true;

    // Set up mouse callbacks for touch simulation
    glfwSetWindowUserPointer(m_window, this);
    glfwSetCursorPosCallback(m_window, cursorPositionCallback);
    glfwSetMouseButtonCallback(m_window, mouseButtonCallback);

    Logger::info("TouchInput initialized (mouse simulation mode)");
    m_initialized = true;
}

void TouchInput::shutdown() {
    if (!m_initialized) {
        return;
    }

    m_activeTouches.clear();
    m_touchDownCallback = nullptr;
    m_touchMoveCallback = nullptr;
    m_touchUpCallback = nullptr;
    m_tapCallback = nullptr;
    m_swipeCallback = nullptr;
    m_pinchCallback = nullptr;

    m_initialized = false;
    Logger::info("TouchInput shutdown");
}

void TouchInput::update(float deltaTime) {
    if (!m_enabled) {
        return;
    }

    // Update all gesture detections
    updateGestures(deltaTime);

    // Clear one-frame flags
    m_lastTap.detected = false;
    m_currentSwipe.justCompleted = false;
    m_currentLongPress.justTriggered = false;
}

const TouchPoint* TouchInput::getTouch(int touchId) const {
    for (const auto& touch : m_activeTouches) {
        if (touch.id == touchId) {
            return &touch;
        }
    }
    return nullptr;
}

TouchPoint* TouchInput::findTouch(int touchId) {
    for (auto& touch : m_activeTouches) {
        if (touch.id == touchId) {
            return &touch;
        }
    }
    return nullptr;
}

void TouchInput::removeTouchById(int touchId) {
    m_activeTouches.erase(
        std::remove_if(m_activeTouches.begin(), m_activeTouches.end(),
            [touchId](const TouchPoint& touch) { return touch.id == touchId; }),
        m_activeTouches.end()
    );
}

float TouchInput::getTime() const {
    return static_cast<float>(glfwGetTime());
}

void TouchInput::onTouchDown(int touchId, glm::vec2 position, float pressure) {
    if (!m_enabled) {
        return;
    }

    // Create new touch point
    TouchPoint touch;
    touch.id = touchId;
    touch.position = position;
    touch.previousPosition = position;
    touch.startPosition = position;
    touch.pressure = pressure;
    touch.timestamp = getTime();
    touch.lastUpdateTime = touch.timestamp;
    touch.isActive = true;

    m_activeTouches.push_back(touch);

    // Fire callback
    if (m_touchDownCallback) {
        m_touchDownCallback(touchId, position);
    }

    Logger::debug("Touch down: id={}, pos=({}, {})", touchId, position.x, position.y);
}

void TouchInput::onTouchMove(int touchId, glm::vec2 position) {
    if (!m_enabled) {
        return;
    }

    TouchPoint* touch = findTouch(touchId);
    if (touch) {
        touch->previousPosition = touch->position;
        touch->position = position;
        touch->lastUpdateTime = getTime();

        // Fire callback
        if (m_touchMoveCallback) {
            m_touchMoveCallback(touchId, position);
        }
    }
}

void TouchInput::onTouchUp(int touchId, glm::vec2 position) {
    if (!m_enabled) {
        return;
    }

    TouchPoint* touch = findTouch(touchId);
    if (touch) {
        touch->position = position;
        touch->isActive = false;

        // Fire callback
        if (m_touchUpCallback) {
            m_touchUpCallback(touchId, position);
        }

        Logger::debug("Touch up: id={}, pos=({}, {})", touchId, position.x, position.y);

        // Remove touch after processing
        removeTouchById(touchId);
    }
}

void TouchInput::onTouchCancel(int touchId) {
    if (!m_enabled) {
        return;
    }

    removeTouchById(touchId);
    Logger::debug("Touch cancelled: id={}", touchId);
}

void TouchInput::updateGestures(float deltaTime) {
    updateTapGesture(deltaTime);
    updateLongPressGesture(deltaTime);
    updateSwipeGesture(deltaTime);

    if (m_activeTouches.size() == 2) {
        updatePinchGesture();
        updateRotationGesture();
    } else {
        // Reset multi-touch gestures
        if (m_currentPinch.isActive) {
            m_currentPinch.isActive = false;
            m_currentPinch.scale = 1.0f;
            m_currentPinch.deltaScale = 0.0f;
        }
        if (m_currentRotation.isActive) {
            m_currentRotation.isActive = false;
            m_currentRotation.deltaAngle = 0.0f;
        }
    }
}

void TouchInput::updateTapGesture(float deltaTime) {
    float currentTime = getTime();

    // Check for completed taps
    for (auto it = m_activeTouches.begin(); it != m_activeTouches.end();) {
        if (!it->isActive) {
            float duration = currentTime - it->timestamp;
            float movement = glm::distance(it->position, it->startPosition);

            if (duration <= m_thresholds.tapMaxDuration && movement <= m_thresholds.tapMaxMovement) {
                // Valid tap detected
                float timeSinceLastTap = currentTime - m_lastTapTime;
                float distanceFromLastTap = glm::distance(it->position, m_lastTapPosition);

                int tapCount = 1;
                if (timeSinceLastTap <= m_thresholds.doubleTapMaxDelay &&
                    distanceFromLastTap <= m_thresholds.tapMaxMovement) {
                    tapCount = 2;  // Double tap
                }

                m_lastTap.position = it->position;
                m_lastTap.tapCount = tapCount;
                m_lastTap.timestamp = currentTime;
                m_lastTap.detected = true;

                m_lastTapTime = currentTime;
                m_lastTapPosition = it->position;

                // Fire callback
                if (m_tapCallback) {
                    m_tapCallback(it->position, tapCount);
                }

                Logger::debug("Tap detected: count={}, pos=({}, {})", tapCount,
                             it->position.x, it->position.y);
            }
            ++it;
        } else {
            ++it;
        }
    }
}

void TouchInput::updateLongPressGesture(float deltaTime) {
    if (m_activeTouches.empty()) {
        if (m_currentLongPress.isActive) {
            m_currentLongPress.isActive = false;
        }
        return;
    }

    const TouchPoint& touch = m_activeTouches[0];
    float currentTime = getTime();
    float duration = currentTime - touch.timestamp;
    float movement = glm::distance(touch.position, touch.startPosition);

    if (movement > m_thresholds.longPressMaxMovement) {
        // Too much movement, cancel long press
        if (m_currentLongPress.isActive) {
            m_currentLongPress.isActive = false;
        }
        return;
    }

    if (!m_currentLongPress.isActive && duration >= m_thresholds.longPressMinDuration) {
        // Long press triggered
        m_currentLongPress.isActive = true;
        m_currentLongPress.justTriggered = true;
        m_currentLongPress.position = touch.position;
        m_currentLongPress.duration = duration;
        m_currentLongPress.touchId = touch.id;

        Logger::debug("Long press detected at ({}, {})", touch.position.x, touch.position.y);
    } else if (m_currentLongPress.isActive) {
        // Update duration
        m_currentLongPress.duration = duration;
    }
}

void TouchInput::updateSwipeGesture(float deltaTime) {
    if (m_activeTouches.empty()) {
        // Check if we should complete the swipe
        if (m_currentSwipe.isActive) {
            float currentTime = getTime();
            m_currentSwipe.duration = currentTime - m_activeTouches[0].timestamp;

            if (m_currentSwipe.distance >= m_thresholds.swipeMinDistance &&
                m_currentSwipe.velocity >= m_thresholds.swipeMinVelocity) {
                m_currentSwipe.justCompleted = true;
                m_currentSwipe.endPosition = m_currentSwipe.currentPosition;

                // Fire callback
                if (m_swipeCallback) {
                    m_swipeCallback(m_currentSwipe);
                }

                Logger::debug("Swipe completed: distance={}, velocity={}, direction=({}, {})",
                             m_currentSwipe.distance, m_currentSwipe.velocity,
                             m_currentSwipe.direction.x, m_currentSwipe.direction.y);
            }
            m_currentSwipe.isActive = false;
        }
        return;
    }

    const TouchPoint& touch = m_activeTouches[0];

    if (!m_currentSwipe.isActive) {
        // Start new swipe
        m_currentSwipe.isActive = true;
        m_currentSwipe.startPosition = touch.startPosition;
        m_currentSwipe.currentPosition = touch.position;
        m_currentSwipe.touchId = touch.id;
        m_currentSwipe.justCompleted = false;
    } else {
        // Update existing swipe
        m_currentSwipe.currentPosition = touch.position;

        glm::vec2 delta = m_currentSwipe.currentPosition - m_currentSwipe.startPosition;
        m_currentSwipe.distance = glm::length(delta);

        if (m_currentSwipe.distance > 0.001f) {
            m_currentSwipe.direction = glm::normalize(delta);
        }

        // Calculate velocity
        float timeDelta = touch.lastUpdateTime - touch.timestamp;
        if (timeDelta > 0.001f) {
            m_currentSwipe.velocity = m_currentSwipe.distance / timeDelta;
        }
    }
}

void TouchInput::updatePinchGesture() {
    if (m_activeTouches.size() != 2) {
        return;
    }

    const TouchPoint& touch1 = m_activeTouches[0];
    const TouchPoint& touch2 = m_activeTouches[1];

    glm::vec2 center = (touch1.position + touch2.position) * 0.5f;
    float distance = glm::distance(touch1.position, touch2.position);

    if (!m_currentPinch.isActive) {
        // Start new pinch
        m_currentPinch.isActive = true;
        m_currentPinch.center = center;
        m_currentPinch.initialDistance = distance;
        m_currentPinch.currentDistance = distance;
        m_currentPinch.scale = 1.0f;
        m_currentPinch.deltaScale = 0.0f;
        m_currentPinch.touchId1 = touch1.id;
        m_currentPinch.touchId2 = touch2.id;
    } else {
        // Update existing pinch
        float previousDistance = m_currentPinch.currentDistance;
        m_currentPinch.center = center;
        m_currentPinch.currentDistance = distance;

        if (m_currentPinch.initialDistance > 0.001f) {
            float newScale = distance / m_currentPinch.initialDistance;
            m_currentPinch.deltaScale = newScale - m_currentPinch.scale;
            m_currentPinch.scale = newScale;

            // Fire callback if significant change
            if (std::abs(m_currentPinch.deltaScale) > 0.01f && m_pinchCallback) {
                m_pinchCallback(m_currentPinch.scale);
            }
        }
    }
}

void TouchInput::updateRotationGesture() {
    if (m_activeTouches.size() != 2) {
        return;
    }

    const TouchPoint& touch1 = m_activeTouches[0];
    const TouchPoint& touch2 = m_activeTouches[1];

    glm::vec2 center = (touch1.position + touch2.position) * 0.5f;
    glm::vec2 delta = touch2.position - touch1.position;
    float angle = std::atan2(delta.y, delta.x);

    if (!m_currentRotation.isActive) {
        // Start new rotation
        m_currentRotation.isActive = true;
        m_currentRotation.center = center;
        m_currentRotation.initialAngle = angle;
        m_currentRotation.currentAngle = angle;
        m_currentRotation.deltaAngle = 0.0f;
        m_currentRotation.totalRotation = 0.0f;
        m_currentRotation.touchId1 = touch1.id;
        m_currentRotation.touchId2 = touch2.id;
    } else {
        // Update existing rotation
        float previousAngle = m_currentRotation.currentAngle;
        m_currentRotation.center = center;
        m_currentRotation.currentAngle = angle;

        // Calculate delta with wrap-around handling. Rotation angles wrap at
        // ±π, so a delta outside that range actually crossed the boundary
        // and should be folded back into [-π, π] before being accumulated.
        float delta = angle - previousAngle;
        if (delta > kPi) delta -= 2.0f * kPi;
        if (delta < -kPi) delta += 2.0f * kPi;

        m_currentRotation.deltaAngle = delta;
        m_currentRotation.totalRotation += delta;
    }
}

bool TouchInput::detectTap(glm::vec2& outPosition) {
    if (m_lastTap.detected && m_lastTap.tapCount == 1) {
        outPosition = m_lastTap.position;
        return true;
    }
    return false;
}

bool TouchInput::detectDoubleTap(glm::vec2& outPosition) {
    if (m_lastTap.detected && m_lastTap.tapCount == 2) {
        outPosition = m_lastTap.position;
        return true;
    }
    return false;
}

bool TouchInput::detectLongPress(glm::vec2& outPosition) {
    if (m_currentLongPress.justTriggered) {
        outPosition = m_currentLongPress.position;
        return true;
    }
    return false;
}

bool TouchInput::detectSwipe(SwipeGesture& outSwipe) {
    if (m_currentSwipe.justCompleted) {
        outSwipe = m_currentSwipe;
        return true;
    }
    return false;
}

bool TouchInput::detectPinch(float& outScale) {
    if (m_currentPinch.isActive) {
        outScale = m_currentPinch.scale;
        return true;
    }
    return false;
}

bool TouchInput::detectRotation(float& outAngle) {
    if (m_currentRotation.isActive && std::abs(m_currentRotation.deltaAngle) > m_thresholds.rotationMinAngle) {
        outAngle = m_currentRotation.deltaAngle;
        return true;
    }
    return false;
}

// GLFW Callbacks (for mouse simulation)
void TouchInput::cursorPositionCallback(GLFWwindow* window, double xpos, double ypos) {
    TouchInput* input = static_cast<TouchInput*>(glfwGetWindowUserPointer(window));
    if (input && input->m_simulateTouch) {
        input->m_mousePosition = glm::vec2(static_cast<float>(xpos), static_cast<float>(ypos));

        if (input->m_mouseDown) {
            input->onTouchMove(0, input->m_mousePosition);
        }
    }
}

void TouchInput::mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    TouchInput* input = static_cast<TouchInput*>(glfwGetWindowUserPointer(window));
    if (input && input->m_simulateTouch && button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            input->m_mouseDown = true;
            input->onTouchDown(0, input->m_mousePosition, 1.0f);
        } else if (action == GLFW_RELEASE) {
            input->m_mouseDown = false;
            input->onTouchUp(0, input->m_mousePosition);
        }
    }
}

} // namespace Engine
