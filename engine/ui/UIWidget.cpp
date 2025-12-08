#include "UIWidget.hpp"
#include <algorithm>

namespace Engine::UI {

UIWidget::UIWidget()
    : m_position(0.0f, 0.0f)
    , m_size(100.0f, 100.0f)
    , m_anchor(0.0f, 0.0f)
    , m_pivot(0.0f, 0.0f)
    , m_margin()
    , m_visible(true)
    , m_enabled(true)
    , m_depth(0.0f)
    , m_parent(nullptr)
    , m_screenRect()
    , m_needsLayout(true)
    , m_isHovered(false)
    , m_isPressed(false)
    , m_isFocused(false)
{
}

UIWidget::~UIWidget() {
    RemoveAllChildren();
}

void UIWidget::OnUpdate(float deltaTime) {
    // Update all children
    for (auto& child : m_children) {
        if (child && child->IsVisible()) {
            child->OnUpdate(deltaTime);
        }
    }
}

void UIWidget::AddChild(std::shared_ptr<UIWidget> child) {
    if (!child) return;

    // Remove from previous parent
    if (child->m_parent) {
        child->m_parent->RemoveChild(child);
    }

    child->m_parent = this;
    m_children.push_back(child);
    child->m_needsLayout = true;
    m_needsLayout = true;
}

void UIWidget::RemoveChild(std::shared_ptr<UIWidget> child) {
    if (!child) return;

    auto it = std::find(m_children.begin(), m_children.end(), child);
    if (it != m_children.end()) {
        (*it)->m_parent = nullptr;
        m_children.erase(it);
        m_needsLayout = true;
    }
}

void UIWidget::RemoveAllChildren() {
    for (auto& child : m_children) {
        if (child) {
            child->m_parent = nullptr;
        }
    }
    m_children.clear();
    m_needsLayout = true;
}

Rect UIWidget::ComputeScreenRect() const {
    Rect rect;

    if (m_parent) {
        // Get parent's screen rectangle
        Rect parentRect = m_parent->GetScreenRect();

        // Calculate anchor point on parent
        float anchorX = parentRect.x + parentRect.width * m_anchor.x;
        float anchorY = parentRect.y + parentRect.height * m_anchor.y;

        // Calculate pivot offset
        float pivotOffsetX = m_size.x * m_pivot.x;
        float pivotOffsetY = m_size.y * m_pivot.y;

        // Apply margins to position
        float marginOffsetX = m_margin.left - m_margin.right;
        float marginOffsetY = m_margin.top - m_margin.bottom;

        // Final position = anchor + position - pivot + margin
        rect.x = anchorX + m_position.x - pivotOffsetX + marginOffsetX;
        rect.y = anchorY + m_position.y - pivotOffsetY + marginOffsetY;
    } else {
        // No parent, position is absolute
        float pivotOffsetX = m_size.x * m_pivot.x;
        float pivotOffsetY = m_size.y * m_pivot.y;

        rect.x = m_position.x - pivotOffsetX;
        rect.y = m_position.y - pivotOffsetY;
    }

    // Apply margins to size
    rect.width = m_size.x - (m_margin.left + m_margin.right);
    rect.height = m_size.y - (m_margin.top + m_margin.bottom);

    return rect;
}

void UIWidget::UpdateLayout() {
    if (m_needsLayout) {
        m_screenRect = ComputeScreenRect();
        m_needsLayout = false;
    }

    // Update children layouts
    for (auto& child : m_children) {
        if (child) {
            child->UpdateLayout();
        }
    }
}

bool UIWidget::ContainsPoint(float x, float y) const {
    return m_screenRect.contains(x, y);
}

} // namespace Engine::UI
