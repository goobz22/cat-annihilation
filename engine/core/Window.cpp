#include "Window.hpp"
#include <stdexcept>
#include <iostream>

namespace Engine {

bool Window::s_glfwInitialized = false;
u32 Window::s_windowCount = 0;

void Window::initGLFW() {
    if (!s_glfwInitialized) {
        if (!glfwInit()) {
            throw std::runtime_error("Failed to initialize GLFW");
        }

        // Set error callback
        glfwSetErrorCallback([](int error, const char* description) {
            std::cerr << "GLFW Error (" << error << "): " << description << std::endl;
        });

        s_glfwInitialized = true;
    }
}

// Delegating ctor for the no-arg overload. Keeps the default-Config payload
// in one place (Config's in-class initializers) so a future field added to
// Config doesn't need a parallel update here.
Window::Window() : Window(Config{}) {}

Window::Window(const Config& config)
    : m_width(config.width)
    , m_height(config.height)
    , m_isFullscreen(config.fullscreen)
    , m_windowedWidth(config.width)
    , m_windowedHeight(config.height) {

    initGLFW();

    // Configure GLFW for Vulkan
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, config.resizable ? GLFW_TRUE : GLFW_FALSE);

    // 2026-04-26 SURVIVAL-PORT — autoplay/background mode opts out of
    // focus stealing. Without these hints GLFW creates a window that
    // steals foreground from whatever the user is doing — disruptive
    // when cat-verify spawns the exe in the middle of a play session,
    // a recording, or just normal desktop work.
    //
    // GLFW_FOCUSED=FALSE: don't request focus when the window appears.
    // GLFW_FOCUS_ON_SHOW=FALSE: don't grab focus when the window is
    //   first shown (some platforms re-request focus at show-time
    //   even if FOCUSED was false at create-time).
    if (config.noFocusSteal) {
        glfwWindowHint(GLFW_FOCUSED, GLFW_FALSE);
        glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);
    }

    // Determine monitor for fullscreen
    GLFWmonitor* monitor = config.fullscreen ? glfwGetPrimaryMonitor() : nullptr;

    // Create window
    m_window = glfwCreateWindow(
        static_cast<int>(m_width),
        static_cast<int>(m_height),
        config.title.c_str(),
        monitor,
        nullptr
    );

    if (!m_window) {
        throw std::runtime_error("Failed to create GLFW window");
    }

    // Store this pointer for callbacks
    glfwSetWindowUserPointer(m_window, this);

    // Setup callbacks
    setupCallbacks();

    // Get actual framebuffer size
    int fbWidth, fbHeight;
    glfwGetFramebufferSize(m_window, &fbWidth, &fbHeight);
    m_width = static_cast<u32>(fbWidth);
    m_height = static_cast<u32>(fbHeight);

    s_windowCount++;
}

Window::~Window() {
    if (m_window) {
        glfwDestroyWindow(m_window);
        m_window = nullptr;
        s_windowCount--;

        // Terminate GLFW when last window is destroyed
        if (s_windowCount == 0 && s_glfwInitialized) {
            glfwTerminate();
            s_glfwInitialized = false;
        }
    }
}

Window::Window(Window&& other) noexcept
    : m_window(other.m_window)
    , m_width(other.m_width)
    , m_height(other.m_height)
    , m_minimized(other.m_minimized)
    , m_focused(other.m_focused)
    , m_resizeCallback(std::move(other.m_resizeCallback))
    , m_minimizeCallback(std::move(other.m_minimizeCallback))
    , m_closeCallback(std::move(other.m_closeCallback))
    , m_focusCallback(std::move(other.m_focusCallback))
    , m_isFullscreen(other.m_isFullscreen)
    , m_windowedX(other.m_windowedX)
    , m_windowedY(other.m_windowedY)
    , m_windowedWidth(other.m_windowedWidth)
    , m_windowedHeight(other.m_windowedHeight) {

    other.m_window = nullptr;

    if (m_window) {
        glfwSetWindowUserPointer(m_window, this);
    }
}

Window& Window::operator=(Window&& other) noexcept {
    if (this != &other) {
        // Destroy current window
        if (m_window) {
            glfwDestroyWindow(m_window);
            s_windowCount--;
        }

        // Move data
        m_window = other.m_window;
        m_width = other.m_width;
        m_height = other.m_height;
        m_minimized = other.m_minimized;
        m_focused = other.m_focused;
        m_resizeCallback = std::move(other.m_resizeCallback);
        m_minimizeCallback = std::move(other.m_minimizeCallback);
        m_closeCallback = std::move(other.m_closeCallback);
        m_focusCallback = std::move(other.m_focusCallback);

        other.m_window = nullptr;

        if (m_window) {
            glfwSetWindowUserPointer(m_window, this);
        }
    }
    return *this;
}

bool Window::shouldClose() const {
    return m_window && glfwWindowShouldClose(m_window);
}

void Window::pollEvents() {
    glfwPollEvents();
}

VkSurfaceKHR Window::createVulkanSurface(VkInstance instance) const {
    if (!m_window) {
        throw std::runtime_error("Cannot create surface: window is null");
    }

    VkSurfaceKHR surface;
    VkResult result = glfwCreateWindowSurface(instance, m_window, nullptr, &surface);

    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan surface");
    }

    return surface;
}

void Window::setTitle(const std::string& title) {
    if (m_window) {
        glfwSetWindowTitle(m_window, title.c_str());
    }
}

void Window::setSize(u32 width, u32 height) {
    if (m_window) {
        glfwSetWindowSize(m_window, static_cast<int>(width), static_cast<int>(height));
    }
}

void Window::setFullscreen(bool enable) {
    if (!m_window || m_isFullscreen == enable) {
        return;
    }

    if (enable) {
        // Snapshot the current windowed geometry before we lose it — GLFW
        // replaces the window's monitor/size atomically in glfwSetWindowMonitor,
        // so without caching here we'd have no way to restore the user's
        // chosen layout when toggling back.
        int posX = 0;
        int posY = 0;
        glfwGetWindowPos(m_window, &posX, &posY);
        m_windowedX = posX;
        m_windowedY = posY;
        m_windowedWidth = m_width;
        m_windowedHeight = m_height;

        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        if (monitor == nullptr) {
            return; // No monitor available — leave the window alone
        }
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        if (mode == nullptr) {
            return;
        }
        // Fullscreen at the desktop's current video mode so we don't force a
        // refresh-rate change on the user's display.
        glfwSetWindowMonitor(m_window, monitor, 0, 0,
                             mode->width, mode->height, mode->refreshRate);
    } else {
        // Fall back to sane defaults if we somehow never cached a windowed
        // geometry (e.g. the window was born fullscreen from command-line
        // args and setFullscreen(false) is the first toggle we see).
        const u32 restoreWidth  = m_windowedWidth  > 0 ? m_windowedWidth  : 1280;
        const u32 restoreHeight = m_windowedHeight > 0 ? m_windowedHeight : 720;
        glfwSetWindowMonitor(m_window, nullptr,
                             m_windowedX, m_windowedY,
                             static_cast<int>(restoreWidth),
                             static_cast<int>(restoreHeight),
                             GLFW_DONT_CARE);
    }

    m_isFullscreen = enable;
}

void Window::setupCallbacks() {
    glfwSetFramebufferSizeCallback(m_window, framebufferSizeCallback);
    glfwSetWindowIconifyCallback(m_window, windowIconifyCallback);
    glfwSetWindowCloseCallback(m_window, windowCloseCallback);
    glfwSetWindowFocusCallback(m_window, windowFocusCallback);
}

void Window::framebufferSizeCallback(GLFWwindow* window, int width, int height) {
    auto* win = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (win) {
        win->m_width = static_cast<u32>(width);
        win->m_height = static_cast<u32>(height);

        if (win->m_resizeCallback) {
            win->m_resizeCallback(win->m_width, win->m_height);
        }
    }
}

void Window::windowIconifyCallback(GLFWwindow* window, int iconified) {
    auto* win = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (win) {
        win->m_minimized = (iconified == GLFW_TRUE);

        if (win->m_minimizeCallback) {
            win->m_minimizeCallback(win->m_minimized);
        }
    }
}

void Window::windowCloseCallback(GLFWwindow* window) {
    auto* win = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (win && win->m_closeCallback) {
        win->m_closeCallback();
    }
}

void Window::windowFocusCallback(GLFWwindow* window, int focused) {
    auto* win = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (win) {
        win->m_focused = (focused == GLFW_TRUE);

        if (win->m_focusCallback) {
            win->m_focusCallback(win->m_focused);
        }
    }
}

} // namespace Engine
