#include "UIImage.hpp"
#include <algorithm>

namespace Engine::UI {

UIImage::UIImage()
    : UIWidget()
    , m_texture(nullptr)
    , m_tintColor(1.0f, 1.0f, 1.0f, 1.0f)
    , m_uvRect()
    , m_fillMode(ImageFillMode::Stretch)
    , m_nineSliceEnabled(false)
    , m_nineSlice()
    , m_preserveAspectRatio(false)
    , m_textureSize(100.0f, 100.0f)
{
}

void UIImage::SetTexture(TextureHandle texture) {
    m_texture = texture;
}

void UIImage::SetUVRect(const UVRect& uvRect) {
    m_uvRect = uvRect;
}

void UIImage::SetFillMode(ImageFillMode mode) {
    m_fillMode = mode;
}

void UIImage::SetNineSlice(const NineSlice& nineSlice) {
    m_nineSlice = nineSlice;
    m_nineSliceEnabled = nineSlice.IsEnabled();
}

void UIImage::OnDraw() {
    if (!m_visible || !m_texture) {
        return;
    }

    // Note: Actual rendering is deferred to UISystem which will generate
    // appropriate draw commands for the UIPass.
    // This OnDraw method signals that this widget needs rendering.
}

Rect UIImage::CalculateRenderRect() const {
    Rect screenRect = GetScreenRect();

    if (!m_preserveAspectRatio || m_fillMode == ImageFillMode::Stretch) {
        return screenRect;
    }

    // Calculate aspect ratios
    float widgetAspect = screenRect.width / screenRect.height;
    float textureAspect = m_textureSize.x / m_textureSize.y;

    Rect renderRect = screenRect;

    switch (m_fillMode) {
        case ImageFillMode::Fit: {
            // Scale to fit inside widget bounds
            if (textureAspect > widgetAspect) {
                // Texture is wider - fit to width
                renderRect.height = screenRect.width / textureAspect;
                renderRect.y += (screenRect.height - renderRect.height) * 0.5f;
            } else {
                // Texture is taller - fit to height
                renderRect.width = screenRect.height * textureAspect;
                renderRect.x += (screenRect.width - renderRect.width) * 0.5f;
            }
            break;
        }

        case ImageFillMode::Fill: {
            // Scale to fill widget bounds (may crop)
            if (textureAspect > widgetAspect) {
                // Texture is wider - fit to height
                renderRect.width = screenRect.height * textureAspect;
                renderRect.x += (screenRect.width - renderRect.width) * 0.5f;
            } else {
                // Texture is taller - fit to width
                renderRect.height = screenRect.width / textureAspect;
                renderRect.y += (screenRect.height - renderRect.height) * 0.5f;
            }
            break;
        }

        case ImageFillMode::Tile:
            // Tiling uses original size
            renderRect.width = m_textureSize.x;
            renderRect.height = m_textureSize.y;
            break;

        case ImageFillMode::Stretch:
        default:
            // Already handled above
            break;
    }

    return renderRect;
}

} // namespace Engine::UI
