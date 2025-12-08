#ifndef ENGINE_UI_TEXT_HPP
#define ENGINE_UI_TEXT_HPP

#include "UIWidget.hpp"
#include "FontRenderer.hpp"
#include <string>
#include <memory>

namespace Engine::UI {

/**
 * @brief Text alignment options
 */
enum class TextAlignment {
    Left,
    Center,
    Right
};

/**
 * @brief Vertical alignment options
 */
enum class VerticalAlignment {
    Top,
    Middle,
    Bottom
};

/**
 * @brief Text widget for rendering text using SDF fonts
 */
class UIText : public UIWidget {
public:
    UIText();
    ~UIText() override = default;

    /**
     * @brief Set text content
     */
    void SetText(const std::string& text);
    const std::string& GetText() const { return m_text; }

    /**
     * @brief Set font atlas to use for rendering
     */
    void SetFont(std::shared_ptr<FontAtlas> font);
    std::shared_ptr<FontAtlas> GetFont() const { return m_font; }

    /**
     * @brief Set font size in pixels
     */
    void SetFontSize(float size);
    float GetFontSize() const { return m_fontSize; }

    /**
     * @brief Set text color
     */
    void SetColor(const vec4& color) { m_color = color; }
    void SetColor(float r, float g, float b, float a = 1.0f) {
        m_color = vec4(r, g, b, a);
    }
    const vec4& GetColor() const { return m_color; }

    /**
     * @brief Set horizontal text alignment
     */
    void SetAlignment(TextAlignment alignment);
    TextAlignment GetAlignment() const { return m_alignment; }

    /**
     * @brief Set vertical text alignment
     */
    void SetVerticalAlignment(VerticalAlignment alignment);
    VerticalAlignment GetVerticalAlignment() const { return m_verticalAlignment; }

    /**
     * @brief Enable/disable word wrapping
     */
    void SetWordWrap(bool enabled);
    bool GetWordWrap() const { return m_wordWrap; }

    /**
     * @brief Set outline color and width (optional effect)
     */
    void SetOutline(const vec4& color, float width);
    void EnableOutline(bool enabled) { m_outlineEnabled = enabled; }
    bool IsOutlineEnabled() const { return m_outlineEnabled; }
    const vec4& GetOutlineColor() const { return m_outlineColor; }
    float GetOutlineWidth() const { return m_outlineWidth; }

    /**
     * @brief Set shadow offset and color (optional effect)
     */
    void SetShadow(const vec2& offset, const vec4& color);
    void EnableShadow(bool enabled) { m_shadowEnabled = enabled; }
    bool IsShadowEnabled() const { return m_shadowEnabled; }
    const vec2& GetShadowOffset() const { return m_shadowOffset; }
    const vec4& GetShadowColor() const { return m_shadowColor; }

    /**
     * @brief Get measured text dimensions
     */
    vec2 GetTextDimensions() const;

    /**
     * @brief Update widget (override)
     */
    void OnUpdate(float deltaTime) override;

    /**
     * @brief Draw widget (override)
     */
    void OnDraw() override;

private:
    /**
     * @brief Update text layout when text or font changes
     */
    void UpdateLayout();

    /**
     * @brief Calculate aligned position based on alignment settings
     */
    vec2 CalculateAlignedPosition() const;

    std::string m_text;
    std::shared_ptr<FontAtlas> m_font;
    float m_fontSize;
    vec4 m_color;

    TextAlignment m_alignment;
    VerticalAlignment m_verticalAlignment;
    bool m_wordWrap;

    // Optional effects
    bool m_outlineEnabled;
    vec4 m_outlineColor;
    float m_outlineWidth;

    bool m_shadowEnabled;
    vec2 m_shadowOffset;
    vec4 m_shadowColor;

    // Cached layout
    TextLayout m_layout;
    bool m_layoutDirty;

    // Font renderer (shared instance)
    static FontRenderer s_fontRenderer;
};

} // namespace Engine::UI

#endif // ENGINE_UI_TEXT_HPP
