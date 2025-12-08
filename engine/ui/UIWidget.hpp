#ifndef ENGINE_UI_WIDGET_HPP
#define ENGINE_UI_WIDGET_HPP

#include "../math/Vector.hpp"
#include <vector>
#include <memory>
#include <functional>

namespace Engine::UI {

/**
 * @brief Rectangle structure for UI layout
 */
struct Rect {
    float x, y, width, height;

    Rect() : x(0.0f), y(0.0f), width(0.0f), height(0.0f) {}
    Rect(float x, float y, float w, float h) : x(x), y(y), width(w), height(h) {}

    bool contains(float px, float py) const {
        return px >= x && px <= (x + width) && py >= y && py <= (y + height);
    }

    bool contains(const vec2& point) const {
        return contains(point.x, point.y);
    }
};

/**
 * @brief Margin structure for widget spacing
 */
struct Margin {
    float left, right, top, bottom;

    Margin() : left(0.0f), right(0.0f), top(0.0f), bottom(0.0f) {}
    Margin(float all) : left(all), right(all), top(all), bottom(all) {}
    Margin(float horizontal, float vertical)
        : left(horizontal), right(horizontal), top(vertical), bottom(vertical) {}
    Margin(float l, float r, float t, float b)
        : left(l), right(r), top(t), bottom(b) {}
};

/**
 * @brief Base class for all UI widgets
 *
 * Provides layout system with anchoring, positioning, and parent-child hierarchy.
 * All positions are in screen space (pixels).
 *
 * Anchor system:
 * - Anchor: Point on parent where this widget attaches (0-1 normalized)
 * - Pivot: Point on this widget that aligns with anchor (0-1 normalized)
 * - Position: Offset from anchor point in pixels
 *
 * Example:
 * - anchor(0.5, 0.5), pivot(0.5, 0.5) = centered on parent
 * - anchor(0, 0), pivot(0, 0) = top-left aligned
 * - anchor(1, 1), pivot(1, 1) = bottom-right aligned
 */
class UIWidget {
public:
    UIWidget();
    virtual ~UIWidget();

    // Disable copying
    UIWidget(const UIWidget&) = delete;
    UIWidget& operator=(const UIWidget&) = delete;

    // Allow moving
    UIWidget(UIWidget&&) = default;
    UIWidget& operator=(UIWidget&&) = default;

    /**
     * @brief Update widget logic and children
     * @param deltaTime Time since last frame in seconds
     */
    virtual void OnUpdate(float deltaTime);

    /**
     * @brief Called when widget should generate draw commands
     * Override in derived classes to implement rendering
     */
    virtual void OnDraw() {}

    /**
     * @brief Called when mouse enters widget bounds
     */
    virtual void OnMouseEnter() {}

    /**
     * @brief Called when mouse exits widget bounds
     */
    virtual void OnMouseExit() {}

    /**
     * @brief Called when mouse button is pressed while over widget
     * @param button Mouse button index (0=left, 1=right, 2=middle)
     * @param x Mouse X position
     * @param y Mouse Y position
     */
    virtual void OnMouseDown(int button, float x, float y) {}

    /**
     * @brief Called when mouse button is released while over widget
     * @param button Mouse button index
     * @param x Mouse X position
     * @param y Mouse Y position
     */
    virtual void OnMouseUp(int button, float x, float y) {}

    /**
     * @brief Called when widget is clicked (mouse down and up in same widget)
     * @param button Mouse button index
     */
    virtual void OnClick(int button) {}

    /**
     * @brief Called when widget gains focus
     */
    virtual void OnFocusGained() {}

    /**
     * @brief Called when widget loses focus
     */
    virtual void OnFocusLost() {}

    // Layout properties
    void SetPosition(const vec2& pos) { m_position = pos; m_needsLayout = true; }
    void SetPosition(float x, float y) { m_position = vec2(x, y); m_needsLayout = true; }
    const vec2& GetPosition() const { return m_position; }

    void SetSize(const vec2& size) { m_size = size; m_needsLayout = true; }
    void SetSize(float w, float h) { m_size = vec2(w, h); m_needsLayout = true; }
    const vec2& GetSize() const { return m_size; }

    void SetAnchor(const vec2& anchor) { m_anchor = anchor; m_needsLayout = true; }
    void SetAnchor(float x, float y) { m_anchor = vec2(x, y); m_needsLayout = true; }
    const vec2& GetAnchor() const { return m_anchor; }

    void SetPivot(const vec2& pivot) { m_pivot = pivot; m_needsLayout = true; }
    void SetPivot(float x, float y) { m_pivot = vec2(x, y); m_needsLayout = true; }
    const vec2& GetPivot() const { return m_pivot; }

    void SetMargin(const Margin& margin) { m_margin = margin; m_needsLayout = true; }
    const Margin& GetMargin() const { return m_margin; }

    void SetVisible(bool visible) { m_visible = visible; }
    bool IsVisible() const { return m_visible; }

    void SetEnabled(bool enabled) { m_enabled = enabled; }
    bool IsEnabled() const { return m_enabled; }

    // Hierarchy
    void AddChild(std::shared_ptr<UIWidget> child);
    void RemoveChild(std::shared_ptr<UIWidget> child);
    void RemoveAllChildren();

    UIWidget* GetParent() const { return m_parent; }
    const std::vector<std::shared_ptr<UIWidget>>& GetChildren() const { return m_children; }

    /**
     * @brief Find child by predicate
     */
    template<typename Predicate>
    std::shared_ptr<UIWidget> FindChild(Predicate pred) {
        for (auto& child : m_children) {
            if (pred(child)) {
                return child;
            }
        }
        return nullptr;
    }

    /**
     * @brief Compute final screen rectangle from layout properties
     * Takes into account parent position, anchor, pivot, and margins
     */
    Rect ComputeScreenRect() const;

    /**
     * @brief Get cached screen rectangle (updated during layout)
     */
    const Rect& GetScreenRect() const { return m_screenRect; }

    /**
     * @brief Update layout for this widget and all children
     * Called automatically by UI system
     */
    void UpdateLayout();

    /**
     * @brief Check if point is inside widget bounds
     */
    bool ContainsPoint(float x, float y) const;
    bool ContainsPoint(const vec2& point) const { return ContainsPoint(point.x, point.y); }

    /**
     * @brief Get depth/z-order for rendering (0 = back, higher = front)
     */
    float GetDepth() const { return m_depth; }
    void SetDepth(float depth) { m_depth = depth; }

protected:
    // Position relative to anchor point (pixels)
    vec2 m_position;

    // Size in pixels
    vec2 m_size;

    // Anchor point on parent (0-1 normalized, 0,0 = top-left, 1,1 = bottom-right)
    vec2 m_anchor;

    // Pivot point on this widget (0-1 normalized)
    vec2 m_pivot;

    // Margins
    Margin m_margin;

    // Visibility and interaction
    bool m_visible;
    bool m_enabled;

    // Depth for rendering order
    float m_depth;

    // Hierarchy
    UIWidget* m_parent;
    std::vector<std::shared_ptr<UIWidget>> m_children;

    // Cached screen rectangle
    Rect m_screenRect;
    bool m_needsLayout;

    // Internal state
    bool m_isHovered;
    bool m_isPressed;
    bool m_isFocused;

    friend class UISystem;
};

} // namespace Engine::UI

#endif // ENGINE_UI_WIDGET_HPP
