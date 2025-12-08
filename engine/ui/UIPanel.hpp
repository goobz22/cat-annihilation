#ifndef ENGINE_UI_PANEL_HPP
#define ENGINE_UI_PANEL_HPP

#include "UIWidget.hpp"
#include "UIImage.hpp"
#include <memory>

namespace Engine::UI {

/**
 * @brief Layout modes for automatic child positioning
 */
enum class LayoutMode {
    None,       // No automatic layout (manual positioning)
    Vertical,   // Stack children vertically
    Horizontal, // Stack children horizontally
    Grid        // Arrange children in a grid
};

/**
 * @brief Grid layout configuration
 */
struct GridLayout {
    u32 columns;        // Number of columns
    u32 rows;           // Number of rows (0 = auto-calculate)
    float spacing;      // Spacing between cells

    GridLayout() : columns(1), rows(0), spacing(0.0f) {}
    GridLayout(u32 cols, u32 rows = 0, float spacing = 0.0f)
        : columns(cols), rows(rows), spacing(spacing) {}
};

/**
 * @brief Container widget with layout support
 *
 * Features:
 * - Background color or image
 * - Optional scrolling (scroll offset, content size)
 * - Clip children to bounds
 * - Layout modes: None (manual), Vertical, Horizontal, Grid
 * - Padding and spacing
 */
class UIPanel : public UIWidget {
public:
    UIPanel();
    ~UIPanel() override = default;

    /**
     * @brief Set background color
     */
    void SetBackgroundColor(const vec4& color);
    const vec4& GetBackgroundColor() const;

    /**
     * @brief Set background texture
     */
    void SetBackgroundTexture(TextureHandle texture);
    TextureHandle GetBackgroundTexture() const;

    /**
     * @brief Get background image widget for direct manipulation
     */
    std::shared_ptr<UIImage> GetBackgroundWidget() const { return m_background; }

    /**
     * @brief Set layout mode
     */
    void SetLayoutMode(LayoutMode mode);
    LayoutMode GetLayoutMode() const { return m_layoutMode; }

    /**
     * @brief Set spacing between children (for Vertical/Horizontal layouts)
     */
    void SetSpacing(float spacing);
    float GetSpacing() const { return m_spacing; }

    /**
     * @brief Set padding inside panel
     */
    void SetPadding(const Margin& padding);
    const Margin& GetPadding() const { return m_padding; }

    /**
     * @brief Set grid layout configuration (for Grid layout mode)
     */
    void SetGridLayout(const GridLayout& gridLayout);
    const GridLayout& GetGridLayout() const { return m_gridLayout; }

    /**
     * @brief Enable/disable clipping children to panel bounds
     */
    void SetClipChildren(bool clip) { m_clipChildren = clip; }
    bool GetClipChildren() const { return m_clipChildren; }

    /**
     * @brief Enable/disable scrolling
     */
    void SetScrollEnabled(bool enabled) { m_scrollEnabled = enabled; }
    bool IsScrollEnabled() const { return m_scrollEnabled; }

    /**
     * @brief Set scroll offset
     */
    void SetScrollOffset(const vec2& offset);
    const vec2& GetScrollOffset() const { return m_scrollOffset; }

    /**
     * @brief Get content size (calculated from children)
     */
    vec2 GetContentSize() const;

    /**
     * @brief Scroll by delta amount
     */
    void Scroll(const vec2& delta);

    /**
     * @brief Enable/disable 9-slice for background
     */
    void SetNineSlice(const NineSlice& nineSlice);

    /**
     * @brief Add child (override to update layout)
     */
    void AddLayoutChild(std::shared_ptr<UIWidget> child);

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
     * @brief Apply automatic layout to children
     */
    void ApplyLayout();

    /**
     * @brief Apply vertical layout
     */
    void ApplyVerticalLayout();

    /**
     * @brief Apply horizontal layout
     */
    void ApplyHorizontalLayout();

    /**
     * @brief Apply grid layout
     */
    void ApplyGridLayout();

    /**
     * @brief Calculate content size from children
     */
    void UpdateContentSize();

    // Background
    std::shared_ptr<UIImage> m_background;

    // Layout
    LayoutMode m_layoutMode;
    float m_spacing;
    Margin m_padding;
    GridLayout m_gridLayout;

    // Scrolling
    bool m_scrollEnabled;
    vec2 m_scrollOffset;
    vec2 m_contentSize;

    // Clipping
    bool m_clipChildren;

    // Layout state
    bool m_layoutDirty;
};

} // namespace Engine::UI

#endif // ENGINE_UI_PANEL_HPP
