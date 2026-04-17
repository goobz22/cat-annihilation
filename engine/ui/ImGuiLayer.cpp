#include "ImGuiLayer.hpp"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"

#include <GLFW/glfw3.h>
#include <array>
#include <cstdio>
#include <stdexcept>

namespace Engine {

namespace {

void CheckVkResult(VkResult err) {
    if (err == VK_SUCCESS) {
        return;
    }
    std::fprintf(stderr, "[ImGuiLayer] Vulkan error: %d\n", static_cast<int>(err));
}

VkDescriptorPool CreateImGuiDescriptorPool(VkDevice device) {
    // ImGui's Vulkan backend binds one combined image sampler per font / UserTexture.
    // Pool sizes here are generous but cheap; they're allocated once.
    const std::array<VkDescriptorPoolSize, 11> poolSizes = {{
        {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000},
    }};

    VkDescriptorPoolCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    info.maxSets = 1000 * static_cast<uint32_t>(poolSizes.size());
    info.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    info.pPoolSizes = poolSizes.data();

    VkDescriptorPool pool = VK_NULL_HANDLE;
    CheckVkResult(vkCreateDescriptorPool(device, &info, nullptr, &pool));
    return pool;
}

} // namespace

ImGuiLayer::~ImGuiLayer() {
    Shutdown();
}

bool ImGuiLayer::Init(const InitInfo& info) {
    if (m_initialized) {
        return true;
    }
    if (info.window == nullptr || info.device == VK_NULL_HANDLE) {
        std::fprintf(stderr, "[ImGuiLayer] Init failed: missing window or device\n");
        return false;
    }

    m_device = info.device;
    m_descriptorPool = CreateImGuiDescriptorPool(m_device);
    if (m_descriptorPool == VK_NULL_HANDLE) {
        std::fprintf(stderr, "[ImGuiLayer] Init failed: could not create descriptor pool\n");
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 8.0F;
    style.FrameRounding = 6.0F;
    style.GrabRounding = 6.0F;
    style.ScrollbarRounding = 6.0F;
    style.FramePadding = ImVec2(12.0F, 8.0F);
    style.ItemSpacing = ImVec2(10.0F, 10.0F);

    // Warm orange accent to match the existing menu highlight color.
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Button] = ImVec4(0.25F, 0.25F, 0.30F, 0.85F);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.65F, 0.45F, 0.15F, 0.95F);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.85F, 0.60F, 0.20F, 1.00F);
    colors[ImGuiCol_Header] = ImVec4(0.65F, 0.45F, 0.15F, 0.80F);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.75F, 0.50F, 0.20F, 0.95F);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.85F, 0.60F, 0.20F, 1.00F);
    colors[ImGuiCol_WindowBg] = ImVec4(0.08F, 0.08F, 0.12F, 0.0F); // transparent; game owns clear
    colors[ImGuiCol_Border] = ImVec4(1.00F, 0.80F, 0.20F, 0.40F);

    ImGuiIO& imguiIo = ImGui::GetIO();
    imguiIo.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    imguiIo.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

    // Load fonts (optional; fall back to default if paths are missing or load fails).
    if (info.regularFontPath != nullptr) {
        m_regularFont = imguiIo.Fonts->AddFontFromFileTTF(info.regularFontPath, 20.0F);
    }
    if (info.boldFontPath != nullptr) {
        m_boldFont = imguiIo.Fonts->AddFontFromFileTTF(info.boldFontPath, 30.0F);
        m_titleFont = imguiIo.Fonts->AddFontFromFileTTF(info.boldFontPath, 80.0F);
    }
    if (m_regularFont == nullptr) {
        m_regularFont = imguiIo.Fonts->AddFontDefault();
    }
    if (m_boldFont == nullptr) {
        m_boldFont = m_regularFont;
    }
    if (m_titleFont == nullptr) {
        m_titleFont = m_boldFont;
    }

    if (!ImGui_ImplGlfw_InitForVulkan(info.window, true)) {
        std::fprintf(stderr, "[ImGuiLayer] ImGui_ImplGlfw_InitForVulkan failed\n");
        Shutdown();
        return false;
    }

    ImGui_ImplVulkan_InitInfo vkInit = {};
    vkInit.Instance = info.instance;
    vkInit.PhysicalDevice = info.physicalDevice;
    vkInit.Device = info.device;
    vkInit.QueueFamily = info.graphicsQueueFamily;
    vkInit.Queue = info.graphicsQueue;
    vkInit.PipelineCache = VK_NULL_HANDLE;
    vkInit.DescriptorPool = m_descriptorPool;
    vkInit.Subpass = 0;
    vkInit.MinImageCount = info.minImageCount;
    vkInit.ImageCount = info.imageCount;
    vkInit.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    vkInit.Allocator = nullptr;
    vkInit.CheckVkResultFn = CheckVkResult;
    vkInit.RenderPass = info.renderPass;

    if (!ImGui_ImplVulkan_Init(&vkInit)) {
        std::fprintf(stderr, "[ImGuiLayer] ImGui_ImplVulkan_Init failed\n");
        Shutdown();
        return false;
    }

    // Build font atlas on the GPU. Newer ImGui Vulkan backends auto-upload on first
    // frame, but calling this explicitly keeps startup deterministic.
    ImGui_ImplVulkan_CreateFontsTexture();

    m_initialized = true;
    return true;
}

void ImGuiLayer::Shutdown() {
    if (!m_initialized && m_descriptorPool == VK_NULL_HANDLE) {
        return;
    }

    if (m_device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device);
    }

    if (m_initialized) {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }
    if (m_descriptorPool != VK_NULL_HANDLE && m_device != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
    }

    m_descriptorPool = VK_NULL_HANDLE;
    m_device = VK_NULL_HANDLE;
    m_regularFont = nullptr;
    m_boldFont = nullptr;
    m_titleFont = nullptr;
    m_initialized = false;
    m_frameStarted = false;
}

void ImGuiLayer::BeginFrame() {
    if (!m_initialized || m_frameStarted) {
        return;
    }
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    m_frameStarted = true;
}

void ImGuiLayer::RenderDrawData(VkCommandBuffer cmd) {
    if (!m_initialized) {
        return;
    }
    if (!m_frameStarted) {
        // No BeginFrame this frame; keep ImGui state consistent by starting + ending an empty frame.
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
    }
    ImGui::Render();
    ImDrawData* drawData = ImGui::GetDrawData();
    if (drawData != nullptr && drawData->CmdListsCount > 0) {
        ImGui_ImplVulkan_RenderDrawData(drawData, cmd);
    }
    m_frameStarted = false;
}

} // namespace Engine
