#ifndef ENGINE_UI_SYSTEM_HPP
#define ENGINE_UI_SYSTEM_HPP

#include "UIWidget.hpp"
#include "UIText.hpp"
#include "UIImage.hpp"
#include "UIButton.hpp"
#include "UIPanel.hpp"
#include "../core/Input.hpp"
#include "../core/Types.hpp"
#include <memory>
#include <vector>
#include <functional>

// Forward declarations for renderer integration
namespace CatEngine::Renderer {
    class UIPass;
}

namespace Engine::UI {

using Engine::u32;

/**
 * @brief UI System - manages widget tree, input, and rendering
 *
 * Features:
 * - Root widget (fullscreen container)
 * - Update all widgets (handle input, animations)
 * - Generate draw commands for UIPass
 * - Handle input events (mouse click, hover)
 * - Focus management (keyboard input to focused widget)
 * - Modal dialogs support
 */
class UISystem {
public:
    UISystem();
    ~UISystem();

    /**
     * @brief Initialize UI system
     * @param input Input system for handling user input
     * @param screenWidth Initial screen width
     * @param screenHeight Initial screen height
     */
    void Initialize(Input* input, u32 screenWidth, u32 screenHeight);

    /**
     * @brief Shutdown UI system
     */
    void Shutdown();

    /**
     * @brief Update UI system (call once per frame)
     * @param deltaTime Time since last frame in seconds
     */
    void Update(float deltaTime);

    /**
     * @brief Generate draw commands for rendering
     * @param uiPass UIPass to receive draw commands
     */
    void Draw(CatEngine::Renderer::UIPass* uiPass);

    /**
     * @brief Handle screen resize
     * @param width New screen width
     * @param height New screen height
     */
    void OnResize(u32 width, u32 height);

    /**
     * @brief Get root widget (fullscreen container)
     */
    std::shared_ptr<UIWidget> GetRoot() const { return m_root; }

    /**
     * @brief Add widget to root
     */
    void AddWidget(std::shared_ptr<UIWidget> widget);

    /**
     * @brief Remove widget from root
     */
    void RemoveWidget(std::shared_ptr<UIWidget> widget);

    /**
     * @brief Get currently focused widget
     */
    UIWidget* GetFocusedWidget() const { return m_focusedWidget; }

    /**
     * @brief Set focused widget
     */
    void SetFocusedWidget(UIWidget* widget);

    /**
     * @brief Clear focus
     */
    void ClearFocus();

    /**
     * @brief Show modal dialog
     * Blocks input to all other widgets until modal is closed
     */
    void ShowModal(std::shared_ptr<UIWidget> modal);

    /**
     * @brief Close current modal dialog
     */
    void CloseModal();

    /**
     * @brief Check if modal is active
     */
    bool IsModalActive() const { return m_modalWidget != nullptr; }

    /**
     * @brief Get current modal widget
     */
    std::shared_ptr<UIWidget> GetModalWidget() const { return m_modalWidget; }

    /**
     * @brief Find widget at screen position
     * @param x Screen X coordinate
     * @param y Screen Y coordinate
     * @return Widget at position, or nullptr
     */
    UIWidget* FindWidgetAtPosition(float x, float y);

    /**
     * @brief Get screen dimensions
     */
    u32 GetScreenWidth() const { return m_screenWidth; }
    u32 GetScreenHeight() const { return m_screenHeight; }

private:
    /**
     * @brief Process input events
     */
    void ProcessInput();

    /**
     * @brief Update hover state for widgets
     */
    void UpdateHoverState();

    /**
     * @brief Handle mouse click
     */
    void HandleMouseClick(int button);

    /**
     * @brief Handle mouse release
     */
    void HandleMouseRelease(int button);

    /**
     * @brief Handle keyboard input for focused widget
     */
    void HandleKeyboardInput();

    /**
     * @brief Recursively find widget at position
     */
    UIWidget* FindWidgetAtPositionRecursive(UIWidget* widget, float x, float y);

    /**
     * @brief Recursively update widgets
     */
    void UpdateWidgetsRecursive(UIWidget* widget, float deltaTime);

    /**
     * @brief Recursively generate draw commands
     */
    void DrawWidgetsRecursive(UIWidget* widget, CatEngine::Renderer::UIPass* uiPass);

    /**
     * @brief Draw a specific widget
     */
    void DrawWidget(UIWidget* widget, CatEngine::Renderer::UIPass* uiPass);

    /**
     * @brief Draw UIImage widget
     */
    void DrawImageWidget(UIImage* image, CatEngine::Renderer::UIPass* uiPass);

    /**
     * @brief Draw UIText widget
     */
    void DrawTextWidget(UIText* text, CatEngine::Renderer::UIPass* uiPass);

    /**
     * @brief Recursively collect all focusable widgets
     * @param widget Starting widget
     * @param out Output vector for focusable widgets
     */
    void CollectFocusableWidgets(UIWidget* widget, std::vector<UIWidget*>& out);

    /**
     * @brief Check if a widget is focusable
     * @param widget Widget to check
     * @return true if widget can receive focus
     */
    bool IsWidgetFocusable(UIWidget* widget) const;

    // Input system
    Input* m_input;

    // Root widget
    std::shared_ptr<UIWidget> m_root;

    // Modal widget
    std::shared_ptr<UIWidget> m_modalWidget;

    // Focus management
    UIWidget* m_focusedWidget;

    // Hover tracking
    UIWidget* m_hoveredWidget;
    UIWidget* m_pressedWidget;

    // Screen dimensions
    u32 m_screenWidth;
    u32 m_screenHeight;

    // Mouse state
    vec2 m_mousePosition;
    bool m_mouseButtonDown[3]; // Left, Right, Middle

    // Initialization state
    bool m_initialized;
};

} // namespace Engine::UI

#endif // ENGINE_UI_SYSTEM_HPP
