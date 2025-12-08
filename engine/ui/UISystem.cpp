#include "UISystem.hpp"
#include "../renderer/passes/UIPass.hpp"
#include <algorithm>

namespace Engine::UI {

UISystem::UISystem()
    : m_input(nullptr)
    , m_root(nullptr)
    , m_modalWidget(nullptr)
    , m_focusedWidget(nullptr)
    , m_hoveredWidget(nullptr)
    , m_pressedWidget(nullptr)
    , m_screenWidth(1920)
    , m_screenHeight(1080)
    , m_mousePosition(0.0f, 0.0f)
    , m_initialized(false)
{
    m_mouseButtonDown[0] = false;
    m_mouseButtonDown[1] = false;
    m_mouseButtonDown[2] = false;
}

UISystem::~UISystem() {
    Shutdown();
}

void UISystem::Initialize(Input* input, u32 screenWidth, u32 screenHeight) {
    m_input = input;
    m_screenWidth = screenWidth;
    m_screenHeight = screenHeight;

    // Create root widget (fullscreen container)
    m_root = std::make_shared<UIPanel>();
    m_root->SetPosition(0.0f, 0.0f);
    m_root->SetSize(static_cast<float>(screenWidth), static_cast<float>(screenHeight));
    m_root->SetAnchor(0.0f, 0.0f);
    m_root->SetPivot(0.0f, 0.0f);

    m_initialized = true;
}

void UISystem::Shutdown() {
    if (!m_initialized) return;

    m_modalWidget.reset();
    m_root.reset();
    m_focusedWidget = nullptr;
    m_hoveredWidget = nullptr;
    m_pressedWidget = nullptr;

    m_initialized = false;
}

void UISystem::Update(float deltaTime) {
    if (!m_initialized || !m_root) return;

    // Update root size to match screen
    m_root->SetSize(static_cast<float>(m_screenWidth), static_cast<float>(m_screenHeight));

    // Update layout
    m_root->UpdateLayout();

    // Process input
    ProcessInput();

    // Update widgets
    if (m_modalWidget) {
        // Update only modal widget when active
        UpdateWidgetsRecursive(m_modalWidget.get(), deltaTime);
    } else {
        // Update all widgets
        UpdateWidgetsRecursive(m_root.get(), deltaTime);
    }
}

void UISystem::Draw(CatEngine::Renderer::UIPass* uiPass) {
    if (!m_initialized || !m_root || !uiPass) return;

    // Draw widgets
    if (m_modalWidget) {
        // Draw root (dimmed/disabled)
        DrawWidgetsRecursive(m_root.get(), uiPass);

        // Draw modal on top
        DrawWidgetsRecursive(m_modalWidget.get(), uiPass);
    } else {
        // Draw all widgets normally
        DrawWidgetsRecursive(m_root.get(), uiPass);
    }
}

void UISystem::OnResize(u32 width, u32 height) {
    m_screenWidth = width;
    m_screenHeight = height;

    if (m_root) {
        m_root->SetSize(static_cast<float>(width), static_cast<float>(height));
    }
}

void UISystem::AddWidget(std::shared_ptr<UIWidget> widget) {
    if (m_root && widget) {
        m_root->AddChild(widget);
    }
}

void UISystem::RemoveWidget(std::shared_ptr<UIWidget> widget) {
    if (m_root && widget) {
        m_root->RemoveChild(widget);
    }
}

void UISystem::SetFocusedWidget(UIWidget* widget) {
    if (m_focusedWidget == widget) return;

    // Clear previous focus
    if (m_focusedWidget) {
        m_focusedWidget->m_isFocused = false;
        m_focusedWidget->OnFocusLost();
    }

    // Set new focus
    m_focusedWidget = widget;

    if (m_focusedWidget) {
        m_focusedWidget->m_isFocused = true;
        m_focusedWidget->OnFocusGained();
    }
}

void UISystem::ClearFocus() {
    SetFocusedWidget(nullptr);
}

void UISystem::ShowModal(std::shared_ptr<UIWidget> modal) {
    m_modalWidget = modal;

    if (m_modalWidget) {
        m_modalWidget->UpdateLayout();
    }

    // Clear focus when showing modal
    ClearFocus();
}

void UISystem::CloseModal() {
    m_modalWidget.reset();
}

UIWidget* UISystem::FindWidgetAtPosition(float x, float y) {
    if (!m_root) return nullptr;

    // Check modal first if active
    if (m_modalWidget) {
        UIWidget* widget = FindWidgetAtPositionRecursive(m_modalWidget.get(), x, y);
        if (widget) return widget;
        return nullptr; // Modal blocks interaction with widgets below
    }

    return FindWidgetAtPositionRecursive(m_root.get(), x, y);
}

void UISystem::ProcessInput() {
    if (!m_input) return;

    // Get mouse position
    double mouseX, mouseY;
    m_input->getMousePosition(mouseX, mouseY);
    m_mousePosition = vec2(static_cast<float>(mouseX), static_cast<float>(mouseY));

    // Update hover state
    UpdateHoverState();

    // Handle mouse clicks
    if (m_input->isMouseButtonPressed(Input::MouseButton::Left)) {
        HandleMouseClick(0);
    }
    if (m_input->isMouseButtonPressed(Input::MouseButton::Right)) {
        HandleMouseClick(1);
    }
    if (m_input->isMouseButtonPressed(Input::MouseButton::Middle)) {
        HandleMouseClick(2);
    }

    // Handle mouse releases
    if (m_input->isMouseButtonReleased(Input::MouseButton::Left)) {
        HandleMouseRelease(0);
    }
    if (m_input->isMouseButtonReleased(Input::MouseButton::Right)) {
        HandleMouseRelease(1);
    }
    if (m_input->isMouseButtonReleased(Input::MouseButton::Middle)) {
        HandleMouseRelease(2);
    }

    // Handle keyboard input for focused widget
    HandleKeyboardInput();
}

void UISystem::UpdateHoverState() {
    UIWidget* widgetAtMouse = FindWidgetAtPosition(m_mousePosition.x, m_mousePosition.y);

    if (widgetAtMouse != m_hoveredWidget) {
        // Mouse exit previous widget
        if (m_hoveredWidget) {
            m_hoveredWidget->m_isHovered = false;
            m_hoveredWidget->OnMouseExit();
        }

        // Mouse enter new widget
        m_hoveredWidget = widgetAtMouse;

        if (m_hoveredWidget) {
            m_hoveredWidget->m_isHovered = true;
            m_hoveredWidget->OnMouseEnter();
        }
    }
}

void UISystem::HandleMouseClick(int button) {
    if (button < 0 || button >= 3) return;

    m_mouseButtonDown[button] = true;

    UIWidget* widgetAtMouse = FindWidgetAtPosition(m_mousePosition.x, m_mousePosition.y);

    if (widgetAtMouse && widgetAtMouse->IsEnabled()) {
        m_pressedWidget = widgetAtMouse;
        widgetAtMouse->OnMouseDown(button, m_mousePosition.x, m_mousePosition.y);

        // Set focus on click
        if (button == 0) {
            SetFocusedWidget(widgetAtMouse);
        }
    } else {
        // Click on empty space clears focus
        if (button == 0) {
            ClearFocus();
        }
    }
}

void UISystem::HandleMouseRelease(int button) {
    if (button < 0 || button >= 3) return;

    m_mouseButtonDown[button] = false;

    UIWidget* widgetAtMouse = FindWidgetAtPosition(m_mousePosition.x, m_mousePosition.y);

    if (widgetAtMouse && widgetAtMouse->IsEnabled()) {
        widgetAtMouse->OnMouseUp(button, m_mousePosition.x, m_mousePosition.y);

        // Trigger click if released on same widget as pressed
        if (widgetAtMouse == m_pressedWidget) {
            widgetAtMouse->OnClick(button);
        }
    }

    m_pressedWidget = nullptr;
}

void UISystem::HandleKeyboardInput() {
    if (!m_focusedWidget || !m_input) return;

    // Check for key presses and forward to focused widget
    // This is a simplified version - you might want to implement a more
    // sophisticated input routing system

    // For buttons, check Enter and Space
    UIButton* button = dynamic_cast<UIButton*>(m_focusedWidget);
    if (button) {
        if (m_input->isKeyPressed(Input::Key::Enter)) {
            button->HandleKeyPress(static_cast<int>(Input::Key::Enter));
        }
        if (m_input->isKeyPressed(Input::Key::Space)) {
            button->HandleKeyPress(static_cast<int>(Input::Key::Space));
        }
    }

    // Tab for focus navigation (next widget)
    if (m_input->isKeyPressed(Input::Key::Tab)) {
        // TODO: Implement tab navigation through widgets
        // This would involve maintaining a tab order list
    }
}

UIWidget* UISystem::FindWidgetAtPositionRecursive(UIWidget* widget, float x, float y) {
    if (!widget || !widget->IsVisible()) return nullptr;

    // Check children first (front to back, reverse order)
    const auto& children = widget->GetChildren();
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
        UIWidget* found = FindWidgetAtPositionRecursive(it->get(), x, y);
        if (found) return found;
    }

    // Check this widget
    if (widget->ContainsPoint(x, y) && widget->IsEnabled()) {
        return widget;
    }

    return nullptr;
}

void UISystem::UpdateWidgetsRecursive(UIWidget* widget, float deltaTime) {
    if (!widget || !widget->IsVisible()) return;

    widget->OnUpdate(deltaTime);

    // Children are updated by widget's OnUpdate
}

void UISystem::DrawWidgetsRecursive(UIWidget* widget, CatEngine::Renderer::UIPass* uiPass) {
    if (!widget || !widget->IsVisible()) return;

    // Draw this widget
    DrawWidget(widget, uiPass);

    // Draw children
    for (const auto& child : widget->GetChildren()) {
        if (child) {
            DrawWidgetsRecursive(child.get(), uiPass);
        }
    }
}

void UISystem::DrawWidget(UIWidget* widget, CatEngine::Renderer::UIPass* uiPass) {
    if (!widget) return;

    // Call widget's OnDraw (for custom rendering logic)
    widget->OnDraw();

    // Generate draw commands based on widget type
    UIImage* image = dynamic_cast<UIImage*>(widget);
    if (image) {
        DrawImageWidget(image, uiPass);
        return;
    }

    UIText* text = dynamic_cast<UIText*>(widget);
    if (text) {
        DrawTextWidget(text, uiPass);
        return;
    }

    // UIButton and UIPanel are drawn through their child widgets
}

void UISystem::DrawImageWidget(UIImage* image, CatEngine::Renderer::UIPass* uiPass) {
    if (!image || !image->GetTexture()) return;

    Rect rect = image->GetScreenRect();
    const vec4& tint = image->GetTintColor();
    const UVRect& uv = image->GetUVRect();

    CatEngine::Renderer::UIPass::QuadDesc quad;
    quad.x = rect.x;
    quad.y = rect.y;
    quad.width = rect.width;
    quad.height = rect.height;
    quad.uvX = uv.x;
    quad.uvY = uv.y;
    quad.uvWidth = uv.width;
    quad.uvHeight = uv.height;
    quad.r = tint.x;
    quad.g = tint.y;
    quad.b = tint.z;
    quad.a = tint.w;
    quad.depth = image->GetDepth();
    quad.texture = static_cast<CatEngine::RHI::IRHITexture*>(image->GetTexture());

    uiPass->DrawQuad(quad);
}

void UISystem::DrawTextWidget(UIText* text, CatEngine::Renderer::UIPass* uiPass) {
    if (!text || text->GetText().empty() || !text->GetFont()) return;

    Rect rect = text->GetScreenRect();
    const vec4& color = text->GetColor();

    CatEngine::Renderer::UIPass::TextDesc textDesc;
    textDesc.text = text->GetText().c_str();
    textDesc.x = rect.x;
    textDesc.y = rect.y;
    textDesc.fontSize = text->GetFontSize();
    textDesc.r = color.x;
    textDesc.g = color.y;
    textDesc.b = color.z;
    textDesc.a = color.w;
    textDesc.depth = text->GetDepth();
    textDesc.fontAtlas = static_cast<CatEngine::RHI::IRHITexture*>(nullptr); // Would need to convert FontAtlas to texture

    uiPass->DrawText(textDesc);
}

} // namespace Engine::UI
