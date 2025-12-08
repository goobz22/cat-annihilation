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

Window::Window(const Config& config)
    : m_width(config.width), m_height(config.height) {

    initGLFW();

    // Configure GLFW for Vulkan
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, config.resizable ? GLFW_TRUE : GLFW_FALSE);

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
    , m_focusCallback(std::move(other.m_focusCallback)) {

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
