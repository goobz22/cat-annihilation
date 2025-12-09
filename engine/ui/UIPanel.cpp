#include "UIPanel.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Engine::UI {

UIPanel::UIPanel()
    : UIWidget()
    , m_background(std::make_shared<UIImage>())
    , m_layoutMode(LayoutMode::None)
    , m_spacing(0.0f)
    , m_padding()
    , m_gridLayout()
    , m_scrollEnabled(false)
    , m_scrollOffset(0.0f, 0.0f)
    , m_contentSize(0.0f, 0.0f)
    , m_clipChildren(false)
    , m_layoutDirty(true)
{
    // Setup background
    m_background->SetAnchor(0.0f, 0.0f);
    m_background->SetPivot(0.0f, 0.0f);
    m_background->SetPosition(0.0f, 0.0f);
    UIWidget::AddChild(m_background);  // Use base class to avoid layout trigger
}

void UIPanel::SetBackgroundColor(const vec4& color) {
    if (m_background) {
        m_background->SetTintColor(color);
    }
}

const vec4& UIPanel::GetBackgroundColor() const {
    static vec4 defaultColor(1.0f, 1.0f, 1.0f, 1.0f);
    if (m_background) {
        return m_background->GetTintColor();
    }
    return defaultColor;
}

void UIPanel::SetBackgroundTexture(TextureHandle texture) {
    if (m_background) {
        m_background->SetTexture(texture);
    }
}

TextureHandle UIPanel::GetBackgroundTexture() const {
    if (m_background) {
        return m_background->GetTexture();
    }
    return nullptr;
}

void UIPanel::SetLayoutMode(LayoutMode mode) {
    if (m_layoutMode != mode) {
        m_layoutMode = mode;
        m_layoutDirty = true;
    }
}

void UIPanel::SetSpacing(float spacing) {
    if (m_spacing != spacing) {
        m_spacing = spacing;
        m_layoutDirty = true;
    }
}

void UIPanel::SetPadding(const Margin& padding) {
    m_padding = padding;
    m_layoutDirty = true;
}

void UIPanel::SetGridLayout(const GridLayout& gridLayout) {
    m_gridLayout = gridLayout;
    if (m_layoutMode == LayoutMode::Grid) {
        m_layoutDirty = true;
    }
}

void UIPanel::SetScrollOffset(const vec2& offset) {
    m_scrollOffset = offset;

    // Clamp scroll offset to valid range
    if (m_scrollEnabled) {
        m_scrollOffset.x = std::max(0.0f, std::min(m_scrollOffset.x, std::max(0.0f, m_contentSize.x - m_size.x)));
        m_scrollOffset.y = std::max(0.0f, std::min(m_scrollOffset.y, std::max(0.0f, m_contentSize.y - m_size.y)));
    }
}

vec2 UIPanel::GetContentSize() const {
    return m_contentSize;
}

void UIPanel::Scroll(const vec2& delta) {
    if (m_scrollEnabled) {
        SetScrollOffset(m_scrollOffset + delta);
    }
}

void UIPanel::SetNineSlice(const NineSlice& nineSlice) {
    if (m_background) {
        m_background->SetNineSlice(nineSlice);
    }
}

void UIPanel::AddLayoutChild(std::shared_ptr<UIWidget> child) {
    UIWidget::AddChild(child);
    m_layoutDirty = true;
}

void UIPanel::OnUpdate(float deltaTime) {
    // Update background to match panel size
    if (m_background) {
        m_background->SetSize(m_size);
    }

    // Apply layout if needed
    if (m_layoutDirty) {
        ApplyLayout();
        m_layoutDirty = false;
    }

    UIWidget::OnUpdate(deltaTime);
}

void UIPanel::OnDraw() {
    // Background will be drawn by UISystem
    // Children will be drawn with clipping if enabled
}

void UIPanel::ApplyLayout() {
    if (m_layoutMode == LayoutMode::None) {
        UpdateContentSize();
        return;
    }

    switch (m_layoutMode) {
        case LayoutMode::Vertical:
            ApplyVerticalLayout();
            break;
        case LayoutMode::Horizontal:
            ApplyHorizontalLayout();
            break;
        case LayoutMode::Grid:
            ApplyGridLayout();
            break;
        default:
            break;
    }

    UpdateContentSize();
}

void UIPanel::ApplyVerticalLayout() {
    float currentY = m_padding.top;

    for (auto& child : m_children) {
        if (!child || child == m_background) continue;
        if (!child->IsVisible()) continue;

        child->SetAnchor(0.0f, 0.0f);
        child->SetPivot(0.0f, 0.0f);
        child->SetPosition(m_padding.left - m_scrollOffset.x, currentY - m_scrollOffset.y);

        currentY += child->GetSize().y + m_spacing;
    }
}

void UIPanel::ApplyHorizontalLayout() {
    float currentX = m_padding.left;

    for (auto& child : m_children) {
        if (!child || child == m_background) continue;
        if (!child->IsVisible()) continue;

        child->SetAnchor(0.0f, 0.0f);
        child->SetPivot(0.0f, 0.0f);
        child->SetPosition(currentX - m_scrollOffset.x, m_padding.top - m_scrollOffset.y);

        currentX += child->GetSize().x + m_spacing;
    }
}

void UIPanel::ApplyGridLayout() {
    if (m_gridLayout.columns == 0) return;

    float availableWidth = m_size.x - m_padding.left - m_padding.right;
    float cellWidth = (availableWidth - (m_gridLayout.columns - 1) * m_gridLayout.spacing) / m_gridLayout.columns;

    uint32_t col = 0;
    uint32_t row = 0;
    float maxRowHeight = 0.0f;

    for (auto& child : m_children) {
        if (!child || child == m_background) continue;
        if (!child->IsVisible()) continue;

        float x = m_padding.left + col * (cellWidth + m_gridLayout.spacing);
        float y = m_padding.top + row * (maxRowHeight + m_gridLayout.spacing);

        child->SetAnchor(0.0f, 0.0f);
        child->SetPivot(0.0f, 0.0f);
        child->SetPosition(x - m_scrollOffset.x, y - m_scrollOffset.y);

        maxRowHeight = std::max(maxRowHeight, child->GetSize().y);

        col++;
        if (col >= m_gridLayout.columns) {
            col = 0;
            row++;
        }
    }
}

void UIPanel::UpdateContentSize() {
    float maxX = 0.0f;
    float maxY = 0.0f;

    for (auto& child : m_children) {
        if (!child || child == m_background) continue;
        if (!child->IsVisible()) continue;

        vec2 childPos = child->GetPosition();
        vec2 childSize = child->GetSize();

        maxX = std::max(maxX, childPos.x + childSize.x + m_padding.right);
        maxY = std::max(maxY, childPos.y + childSize.y + m_padding.bottom);
    }

    m_contentSize = vec2(maxX, maxY);
}

} // namespace Engine::UI
