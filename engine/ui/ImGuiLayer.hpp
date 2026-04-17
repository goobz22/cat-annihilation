#ifndef ENGINE_UI_IMGUI_LAYER_HPP
#define ENGINE_UI_IMGUI_LAYER_HPP

#include <vulkan/vulkan.h>
#include <cstdint>

struct GLFWwindow;
struct ImFont;

namespace Engine {

// ImGuiLayer owns the Dear ImGui context + its Vulkan/GLFW backends.
// Lifecycle: Init() after the renderer is up, Shutdown() before teardown.
// Per-frame: call BeginFrame() once, build UI, then RenderDrawData() inside
// an active render pass on the recording command buffer.
class ImGuiLayer {
public:
    struct InitInfo {
        GLFWwindow* window = nullptr;
        VkInstance instance = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        uint32_t graphicsQueueFamily = 0;
        VkQueue graphicsQueue = VK_NULL_HANDLE;
        VkRenderPass renderPass = VK_NULL_HANDLE;
        uint32_t minImageCount = 2;
        uint32_t imageCount = 2;
        const char* regularFontPath = nullptr;  // e.g. "assets/fonts/OpenSans-Regular.ttf"
        const char* boldFontPath = nullptr;     // e.g. "assets/fonts/OpenSans-Bold.ttf"
    };

    ImGuiLayer() = default;
    ~ImGuiLayer();

    ImGuiLayer(const ImGuiLayer&) = delete;
    ImGuiLayer& operator=(const ImGuiLayer&) = delete;

    bool Init(const InitInfo& info);
    void Shutdown();

    // Starts a new ImGui frame. Call once per frame BEFORE building UI.
    void BeginFrame();

    // Finalize UI build (ImGui::Render) and record draw commands into cmd.
    // cmd must already be in a render pass compatible with the one passed at Init.
    void RenderDrawData(VkCommandBuffer cmd);

    [[nodiscard]] ImFont* GetRegularFont() const { return m_regularFont; }
    [[nodiscard]] ImFont* GetBoldFont() const { return m_boldFont; }
    [[nodiscard]] ImFont* GetTitleFont() const { return m_titleFont; }

    [[nodiscard]] bool IsInitialized() const { return m_initialized; }

private:
    bool m_initialized = false;
    bool m_frameStarted = false;
    VkDevice m_device = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    ImFont* m_regularFont = nullptr;  // Body UI text (~18pt OpenSans Regular).
    ImFont* m_boldFont = nullptr;     // Button labels (~28pt OpenSans Bold).
    ImFont* m_titleFont = nullptr;    // Screen titles (~72pt OpenSans Bold).
};

} // namespace Engine

#endif // ENGINE_UI_IMGUI_LAYER_HPP
