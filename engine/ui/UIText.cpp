#include "UIText.hpp"

namespace Engine::UI {

// Static font renderer instance
FontRenderer UIText::s_fontRenderer;

UIText::UIText()
    : UIWidget()
    , m_text("")
    , m_font(nullptr)
    , m_fontSize(16.0f)
    , m_color(1.0f, 1.0f, 1.0f, 1.0f)
    , m_alignment(TextAlignment::Left)
    , m_verticalAlignment(VerticalAlignment::Top)
    , m_wordWrap(false)
    , m_outlineEnabled(false)
    , m_outlineColor(0.0f, 0.0f, 0.0f, 1.0f)
    , m_outlineWidth(1.0f)
    , m_shadowEnabled(false)
    , m_shadowOffset(2.0f, 2.0f)
    , m_shadowColor(0.0f, 0.0f, 0.0f, 0.5f)
    , m_layoutDirty(true)
{
}

void UIText::SetText(const std::string& text) {
    if (m_text != text) {
        m_text = text;
        m_layoutDirty = true;
    }
}

void UIText::SetFont(std::shared_ptr<FontAtlas> font) {
    if (m_font != font) {
        m_font = font;
        m_layoutDirty = true;
    }
}

void UIText::SetFontSize(float size) {
    if (m_fontSize != size) {
        m_fontSize = size;
        m_layoutDirty = true;
    }
}

void UIText::SetAlignment(TextAlignment alignment) {
    if (m_alignment != alignment) {
        m_alignment = alignment;
        m_layoutDirty = true;
    }
}

void UIText::SetVerticalAlignment(VerticalAlignment alignment) {
    if (m_verticalAlignment != alignment) {
        m_verticalAlignment = alignment;
        m_layoutDirty = true;
    }
}

void UIText::SetWordWrap(bool enabled) {
    if (m_wordWrap != enabled) {
        m_wordWrap = enabled;
        m_layoutDirty = true;
    }
}

void UIText::SetOutline(const vec4& color, float width) {
    m_outlineColor = color;
    m_outlineWidth = width;
}

void UIText::SetShadow(const vec2& offset, const vec4& color) {
    m_shadowOffset = offset;
    m_shadowColor = color;
}

vec2 UIText::GetTextDimensions() const {
    if (!m_font) return vec2(0.0f, 0.0f);

    return s_fontRenderer.MeasureText(*m_font, m_text, m_fontSize);
}

void UIText::OnUpdate(float deltaTime) {
    UIWidget::OnUpdate(deltaTime);

    if (m_layoutDirty) {
        UpdateLayout();
    }
}

void UIText::OnDraw() {
    if (!m_visible || !m_font || m_text.empty()) {
        return;
    }

    // Note: Actual rendering is deferred to UISystem which will call
    // the renderer's DrawText method with the appropriate parameters.
    // This OnDraw method can be used to signal that this widget needs rendering,
    // or to prepare rendering data.
}

void UIText::UpdateLayout() {
    if (!m_font) {
        m_layoutDirty = false;
        return;
    }

    // Calculate max width for word wrap
    float maxWidth = m_wordWrap ? m_size.x : 0.0f;

    // Generate text layout
    m_layout = s_fontRenderer.LayoutText(*m_font, m_text, m_fontSize, maxWidth);

    m_layoutDirty = false;
}

vec2 UIText::CalculateAlignedPosition() const {
    if (!m_font) return vec2(0.0f, 0.0f);

    Rect screenRect = GetScreenRect();
    vec2 textSize = m_layout.bounds;

    vec2 position(screenRect.x, screenRect.y);

    // Horizontal alignment
    switch (m_alignment) {
        case TextAlignment::Left:
            // Already at left
            break;
        case TextAlignment::Center:
            position.x += (screenRect.width - textSize.x) * 0.5f;
            break;
        case TextAlignment::Right:
            position.x += screenRect.width - textSize.x;
            break;
    }

    // Vertical alignment
    switch (m_verticalAlignment) {
        case VerticalAlignment::Top:
            // Already at top
            break;
        case VerticalAlignment::Middle:
            position.y += (screenRect.height - textSize.y) * 0.5f;
            break;
        case VerticalAlignment::Bottom:
            position.y += screenRect.height - textSize.y;
            break;
    }

    return position;
}

} // namespace Engine::UI
