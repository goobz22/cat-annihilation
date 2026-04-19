#ifndef ENGINE_CORE_WINDOW_HPP
#define ENGINE_CORE_WINDOW_HPP

#include "Types.hpp"
// Include Vulkan directly rather than relying on GLFW_INCLUDE_VULKAN — the
// macro only takes effect if no earlier TU has already pulled in
// <GLFW/glfw3.h>, so the order-dependent behavior breaks any consumer that
// includes Window.hpp after something that forward-declared GLFW without
// the Vulkan define. Including vulkan.h explicitly makes VkSurfaceKHR and
// VkInstance visible regardless of include ordering.
#include <vulkan/vulkan.h>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <string>
#include <functional>

namespace Engine {

/**
 * @brief Window creation and management using GLFW
 *
 * Handles window lifecycle, event callbacks, and Vulkan surface creation.
 * Provides callbacks for resize, minimize, close, and focus events.
 */
class Window {
public:
    /**
     * @brief Window configuration structure
     */
    struct Config {
        std::string title = "Engine Window";
        u32 width = 1280;
        u32 height = 720;
        bool resizable = true;
        bool vsync = true;
        bool fullscreen = false;
    };

    /**
     * @brief Event callback function types
     */
    using ResizeCallback = std::function<void(u32 width, u32 height)>;
    using MinimizeCallback = std::function<void(bool minimized)>;
    using CloseCallback = std::function<void()>;
    using FocusCallback = std::function<void(bool focused)>;

    /**
     * @brief Construct a new Window object
     * @param config Window configuration
     */
    explicit Window(const Config& config = Config{});

    /**
     * @brief Destroy the Window object
     */
    ~Window();

    // Disable copy
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // Enable move
    Window(Window&& other) noexcept;
    Window& operator=(Window&& other) noexcept;

    /**
     * @brief Check if window should close
     * @return true if window close was requested
     */
    bool shouldClose() const;

    /**
     * @brief Poll window events
     */
    void pollEvents();

    /**
     * @brief Get window width
     * @return Current window width in pixels
     */
    u32 getWidth() const { return m_width; }

    /**
     * @brief Get window height
     * @return Current window height in pixels
     */
    u32 getHeight() const { return m_height; }

    /**
     * @brief Get window aspect ratio
     * @return Width / Height ratio
     */
    f32 getAspectRatio() const { return static_cast<f32>(m_width) / static_cast<f32>(m_height); }

    /**
     * @brief Check if window is minimized
     * @return true if window is minimized
     */
    bool isMinimized() const { return m_minimized; }

    /**
     * @brief Check if window is focused
     * @return true if window has input focus
     */
    bool isFocused() const { return m_focused; }

    /**
     * @brief Get raw GLFW window handle
     * @return Pointer to GLFWwindow
     */
    GLFWwindow* getHandle() const { return m_window; }

    /**
     * @brief Create Vulkan surface for this window
     * @param instance VkInstance handle
     * @return VkSurfaceKHR surface handle
     */
    VkSurfaceKHR createVulkanSurface(VkInstance instance) const;

    /**
     * @brief Set resize callback
     * @param callback Function to call on window resize
     */
    void setResizeCallback(ResizeCallback callback) { m_resizeCallback = callback; }

    /**
     * @brief Set minimize callback
     * @param callback Function to call on window minimize/restore
     */
    void setMinimizeCallback(MinimizeCallback callback) { m_minimizeCallback = callback; }

    /**
     * @brief Set close callback
     * @param callback Function to call on window close request
     */
    void setCloseCallback(CloseCallback callback) { m_closeCallback = callback; }

    /**
     * @brief Set focus callback
     * @param callback Function to call on window focus change
     */
    void setFocusCallback(FocusCallback callback) { m_focusCallback = callback; }

    /**
     * @brief Set window title
     * @param title New window title
     */
    void setTitle(const std::string& title);

    /**
     * @brief Set window size
     * @param width New width in pixels
     * @param height New height in pixels
     */
    void setSize(u32 width, u32 height);

    /**
     * @brief Toggle between fullscreen and windowed mode
     *
     * When entering fullscreen the window snaps to the primary monitor at its
     * current video mode. When returning to windowed mode we restore the last
     * windowed size/position captured before the previous fullscreen switch.
     * @param enable true to switch to fullscreen, false for windowed
     */
    void setFullscreen(bool enable);

    /**
     * @brief Query whether the window is currently in fullscreen mode
     */
    bool isFullscreen() const { return m_isFullscreen; }

private:
    /**
     * @brief Initialize GLFW library (called once)
     */
    static void initGLFW();

    /**
     * @brief Setup GLFW callbacks
     */
    void setupCallbacks();

    // GLFW callback functions
    static void framebufferSizeCallback(GLFWwindow* window, int width, int height);
    static void windowIconifyCallback(GLFWwindow* window, int iconified);
    static void windowCloseCallback(GLFWwindow* window);
    static void windowFocusCallback(GLFWwindow* window, int focused);

    GLFWwindow* m_window = nullptr;
    u32 m_width = 0;
    u32 m_height = 0;
    bool m_minimized = false;
    bool m_focused = true;

    // Cached windowed-mode geometry so returning from fullscreen lands on the
    // same size + position the user had before. Updated lazily inside
    // setFullscreen(true) right before handing the window to the monitor.
    bool m_isFullscreen = false;
    int m_windowedX = 0;
    int m_windowedY = 0;
    u32 m_windowedWidth = 0;
    u32 m_windowedHeight = 0;

    // Event callbacks
    ResizeCallback m_resizeCallback;
    MinimizeCallback m_minimizeCallback;
    CloseCallback m_closeCallback;
    FocusCallback m_focusCallback;

    static bool s_glfwInitialized;
    static u32 s_windowCount;
};

} // namespace Engine

#endif // ENGINE_CORE_WINDOW_HPP
