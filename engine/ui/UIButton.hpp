#ifndef ENGINE_UI_BUTTON_HPP
#define ENGINE_UI_BUTTON_HPP

#include "UIWidget.hpp"
#include "UIImage.hpp"
#include "UIText.hpp"
#include <functional>
#include <memory>

namespace Engine::UI {

/**
 * @brief Button state enumeration
 */
enum class ButtonState {
    Normal,     // Default state
    Hovered,    // Mouse is over button
    Pressed,    // Mouse button is down on button
    Disabled    // Button is not interactive
};

/**
 * @brief Button visual configuration for each state
 */
struct ButtonStateVisuals {
    TextureHandle backgroundTexture;
    vec4 backgroundColor;
    vec4 textColor;

    ButtonStateVisuals()
        : backgroundTexture(nullptr)
        , backgroundColor(0.7f, 0.7f, 0.7f, 1.0f)
        , textColor(1.0f, 1.0f, 1.0f, 1.0f)
    {}
};

/**
 * @brief Interactive button widget
 *
 * Features:
 * - Multiple visual states (normal, hovered, pressed, disabled)
 * - Optional background image per state
 * - Optional text label
 * - Click callback
 * - Keyboard activation (Enter/Space when focused)
 */
class UIButton : public UIWidget {
public:
    UIButton();
    ~UIButton() override = default;

    /**
     * @brief Set button text label
     */
    void SetText(const std::string& text);
    const std::string& GetText() const;

    /**
     * @brief Get text widget for direct manipulation
     */
    std::shared_ptr<UIText> GetTextWidget() const { return m_label; }

    /**
     * @brief Set click callback
     */
    void SetOnClickCallback(std::function<void()> callback) {
        m_onClickCallback = callback;
    }

    /**
     * @brief Set visual configuration for a specific state
     */
    void SetStateVisuals(ButtonState state, const ButtonStateVisuals& visuals);
    const ButtonStateVisuals& GetStateVisuals(ButtonState state) const;

    /**
     * @brief Set background texture for a state
     */
    void SetStateTexture(ButtonState state, TextureHandle texture);

    /**
     * @brief Set background color for a state
     */
    void SetStateBackgroundColor(ButtonState state, const vec4& color);

    /**
     * @brief Set text color for a state
     */
    void SetStateTextColor(ButtonState state, const vec4& color);

    /**
     * @brief Get current button state
     */
    ButtonState GetState() const { return m_currentState; }

    /**
     * @brief Enable/disable 9-slice for background images
     */
    void SetNineSlice(const NineSlice& nineSlice);

    /**
     * @brief Update widget (override)
     */
    void OnUpdate(float deltaTime) override;

    /**
     * @brief Draw widget (override)
     */
    void OnDraw() override;

    // Input event handlers (overrides)
    void OnMouseEnter() override;
    void OnMouseExit() override;
    void OnMouseDown(int button, float x, float y) override;
    void OnMouseUp(int button, float x, float y) override;
    void OnClick(int button) override;
    void OnFocusGained() override;
    void OnFocusLost() override;

    /**
     * @brief Handle keyboard input (called by UISystem when button has focus)
     */
    void HandleKeyPress(int key);

private:
    /**
     * @brief Update visual appearance based on current state
     */
    void UpdateVisuals();

    /**
     * @brief Transition to a new state
     */
    void SetState(ButtonState newState);

    // Child widgets
    std::shared_ptr<UIImage> m_background;
    std::shared_ptr<UIText> m_label;

    // Button state
    ButtonState m_currentState;
    ButtonStateVisuals m_stateVisuals[4]; // One for each ButtonState

    // Callback
    std::function<void()> m_onClickCallback;

    // Interaction state
    bool m_isMouseDown;
};

} // namespace Engine::UI

#endif // ENGINE_UI_BUTTON_HPP
