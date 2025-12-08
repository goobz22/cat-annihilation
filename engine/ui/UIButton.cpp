#include "UIButton.hpp"
#include "../core/Input.hpp"

namespace Engine::UI {

UIButton::UIButton()
    : UIWidget()
    , m_background(std::make_shared<UIImage>())
    , m_label(std::make_shared<UIText>())
    , m_currentState(ButtonState::Normal)
    , m_onClickCallback(nullptr)
    , m_isMouseDown(false)
{
    // Setup background
    m_background->SetSize(1.0f, 1.0f);  // Will be sized to match button
    m_background->SetAnchor(0.0f, 0.0f);
    m_background->SetPivot(0.0f, 0.0f);
    AddChild(m_background);

    // Setup label
    m_label->SetAlignment(TextAlignment::Center);
    m_label->SetVerticalAlignment(VerticalAlignment::Middle);
    m_label->SetAnchor(0.5f, 0.5f);
    m_label->SetPivot(0.5f, 0.5f);
    AddChild(m_label);

    // Initialize default state visuals
    // Normal state
    m_stateVisuals[static_cast<int>(ButtonState::Normal)].backgroundColor = vec4(0.7f, 0.7f, 0.7f, 1.0f);
    m_stateVisuals[static_cast<int>(ButtonState::Normal)].textColor = vec4(1.0f, 1.0f, 1.0f, 1.0f);

    // Hovered state
    m_stateVisuals[static_cast<int>(ButtonState::Hovered)].backgroundColor = vec4(0.8f, 0.8f, 0.8f, 1.0f);
    m_stateVisuals[static_cast<int>(ButtonState::Hovered)].textColor = vec4(1.0f, 1.0f, 1.0f, 1.0f);

    // Pressed state
    m_stateVisuals[static_cast<int>(ButtonState::Pressed)].backgroundColor = vec4(0.5f, 0.5f, 0.5f, 1.0f);
    m_stateVisuals[static_cast<int>(ButtonState::Pressed)].textColor = vec4(0.9f, 0.9f, 0.9f, 1.0f);

    // Disabled state
    m_stateVisuals[static_cast<int>(ButtonState::Disabled)].backgroundColor = vec4(0.4f, 0.4f, 0.4f, 1.0f);
    m_stateVisuals[static_cast<int>(ButtonState::Disabled)].textColor = vec4(0.5f, 0.5f, 0.5f, 1.0f);

    UpdateVisuals();
}

void UIButton::SetText(const std::string& text) {
    if (m_label) {
        m_label->SetText(text);
    }
}

const std::string& UIButton::GetText() const {
    static std::string empty;
    if (m_label) {
        return m_label->GetText();
    }
    return empty;
}

void UIButton::SetStateVisuals(ButtonState state, const ButtonStateVisuals& visuals) {
    m_stateVisuals[static_cast<int>(state)] = visuals;
    if (m_currentState == state) {
        UpdateVisuals();
    }
}

const ButtonStateVisuals& UIButton::GetStateVisuals(ButtonState state) const {
    return m_stateVisuals[static_cast<int>(state)];
}

void UIButton::SetStateTexture(ButtonState state, TextureHandle texture) {
    m_stateVisuals[static_cast<int>(state)].backgroundTexture = texture;
    if (m_currentState == state) {
        UpdateVisuals();
    }
}

void UIButton::SetStateBackgroundColor(ButtonState state, const vec4& color) {
    m_stateVisuals[static_cast<int>(state)].backgroundColor = color;
    if (m_currentState == state) {
        UpdateVisuals();
    }
}

void UIButton::SetStateTextColor(ButtonState state, const vec4& color) {
    m_stateVisuals[static_cast<int>(state)].textColor = color;
    if (m_currentState == state) {
        UpdateVisuals();
    }
}

void UIButton::SetNineSlice(const NineSlice& nineSlice) {
    if (m_background) {
        m_background->SetNineSlice(nineSlice);
    }
}

void UIButton::OnUpdate(float deltaTime) {
    UIWidget::OnUpdate(deltaTime);

    // Ensure background matches button size
    if (m_background) {
        m_background->SetSize(m_size);
    }

    // Update label size to match button (for text wrapping/alignment)
    if (m_label) {
        m_label->SetSize(m_size);
    }
}

void UIButton::OnDraw() {
    // Children (background and label) will be drawn by UISystem
}

void UIButton::OnMouseEnter() {
    if (m_enabled && m_currentState != ButtonState::Disabled) {
        SetState(m_isMouseDown ? ButtonState::Pressed : ButtonState::Hovered);
    }
}

void UIButton::OnMouseExit() {
    if (m_enabled && m_currentState != ButtonState::Disabled) {
        SetState(ButtonState::Normal);
        m_isMouseDown = false;
    }
}

void UIButton::OnMouseDown(int button, float x, float y) {
    if (button == 0 && m_enabled && m_currentState != ButtonState::Disabled) {
        m_isMouseDown = true;
        SetState(ButtonState::Pressed);
    }
}

void UIButton::OnMouseUp(int button, float x, float y) {
    if (button == 0 && m_enabled && m_currentState != ButtonState::Disabled) {
        m_isMouseDown = false;
        if (m_isHovered) {
            SetState(ButtonState::Hovered);
        } else {
            SetState(ButtonState::Normal);
        }
    }
}

void UIButton::OnClick(int button) {
    if (button == 0 && m_enabled && m_currentState != ButtonState::Disabled) {
        if (m_onClickCallback) {
            m_onClickCallback();
        }
    }
}

void UIButton::OnFocusGained() {
    // Could add focus visual feedback here
}

void UIButton::OnFocusLost() {
    // Could remove focus visual feedback here
}

void UIButton::HandleKeyPress(int key) {
    // Activate button with Enter or Space when focused
    if (m_isFocused && m_enabled && m_currentState != ButtonState::Disabled) {
        if (key == static_cast<int>(Input::Key::Enter) ||
            key == static_cast<int>(Input::Key::Space)) {
            if (m_onClickCallback) {
                m_onClickCallback();
            }
        }
    }
}

void UIButton::UpdateVisuals() {
    const ButtonStateVisuals& visuals = m_stateVisuals[static_cast<int>(m_currentState)];

    if (m_background) {
        m_background->SetTexture(visuals.backgroundTexture);
        m_background->SetTintColor(visuals.backgroundColor);
    }

    if (m_label) {
        m_label->SetColor(visuals.textColor);
    }
}

void UIButton::SetState(ButtonState newState) {
    if (m_currentState != newState) {
        m_currentState = newState;
        UpdateVisuals();
    }
}

} // namespace Engine::UI
