#ifndef ENGINE_UI_IMAGE_HPP
#define ENGINE_UI_IMAGE_HPP

#include "UIWidget.hpp"

namespace Engine::UI {

// Forward declaration - texture reference is a void pointer for flexibility
// In actual usage, this would be RHI::IRHITexture* from the rendering backend
using TextureHandle = void*;

/**
 * @brief Fill modes for image rendering
 */
enum class ImageFillMode {
    Stretch,        // Stretch to fill widget bounds
    Fit,            // Scale to fit inside bounds while maintaining aspect ratio
    Fill,           // Scale to fill bounds while maintaining aspect ratio (may crop)
    Tile            // Tile the image
};

/**
 * @brief 9-slice configuration for scalable UI elements
 *
 * Divides image into 9 sections:
 * +---+-------+---+
 * | TL|  Top  | TR|
 * +---+-------+---+
 * | L |Center | R |
 * +---+-------+---+
 * | BL| Bottom| BR|
 * +---+-------+---+
 *
 * Corners are never scaled, edges are scaled in one direction,
 * center is scaled in both directions.
 */
struct NineSlice {
    float left;     // Left border width in pixels
    float right;    // Right border width in pixels
    float top;      // Top border height in pixels
    float bottom;   // Bottom border height in pixels

    NineSlice() : left(0.0f), right(0.0f), top(0.0f), bottom(0.0f) {}
    NineSlice(float all) : left(all), right(all), top(all), bottom(all) {}
    NineSlice(float horizontal, float vertical)
        : left(horizontal), right(horizontal), top(vertical), bottom(vertical) {}
    NineSlice(float l, float r, float t, float b)
        : left(l), right(r), top(t), bottom(b) {}

    bool IsEnabled() const {
        return left > 0.0f || right > 0.0f || top > 0.0f || bottom > 0.0f;
    }
};

/**
 * @brief UV rectangle for sprite sheets and texture atlases
 */
struct UVRect {
    float x, y;         // Top-left UV coordinate (0-1)
    float width, height; // UV dimensions (0-1)

    UVRect() : x(0.0f), y(0.0f), width(1.0f), height(1.0f) {}
    UVRect(float x, float y, float w, float h) : x(x), y(y), width(w), height(h) {}

    // Get UV corners
    vec2 GetMin() const { return vec2(x, y); }
    vec2 GetMax() const { return vec2(x + width, y + height); }
};

/**
 * @brief Image widget for rendering textures
 *
 * Supports:
 * - Texture rendering with tint color
 * - UV rectangle for sprite sheets
 * - 9-slice scaling for scalable UI
 * - Multiple fill modes (stretch, fit, fill, tile)
 * - Aspect ratio preservation
 */
class UIImage : public UIWidget {
public:
    UIImage();
    ~UIImage() override = default;

    /**
     * @brief Set texture to render
     */
    void SetTexture(TextureHandle texture);
    TextureHandle GetTexture() const { return m_texture; }

    /**
     * @brief Set tint color (multiplied with texture)
     */
    void SetTintColor(const vec4& color) { m_tintColor = color; }
    void SetTintColor(float r, float g, float b, float a = 1.0f) {
        m_tintColor = vec4(r, g, b, a);
    }
    const vec4& GetTintColor() const { return m_tintColor; }

    /**
     * @brief Set UV rectangle (for sprite sheets/atlases)
     */
    void SetUVRect(const UVRect& uvRect);
    const UVRect& GetUVRect() const { return m_uvRect; }

    /**
     * @brief Set fill mode
     */
    void SetFillMode(ImageFillMode mode);
    ImageFillMode GetFillMode() const { return m_fillMode; }

    /**
     * @brief Enable/disable 9-slice scaling
     */
    void SetNineSlice(const NineSlice& nineSlice);
    const NineSlice& GetNineSlice() const { return m_nineSlice; }
    void EnableNineSlice(bool enabled) { m_nineSliceEnabled = enabled; }
    bool IsNineSliceEnabled() const { return m_nineSliceEnabled; }

    /**
     * @brief Set whether to preserve aspect ratio (for Fit/Fill modes)
     */
    void SetPreserveAspectRatio(bool preserve) { m_preserveAspectRatio = preserve; }
    bool GetPreserveAspectRatio() const { return m_preserveAspectRatio; }

    /**
     * @brief Set texture dimensions (used for aspect ratio calculations)
     */
    void SetTextureSize(const vec2& size) { m_textureSize = size; }
    void SetTextureSize(float w, float h) { m_textureSize = vec2(w, h); }
    const vec2& GetTextureSize() const { return m_textureSize; }

    /**
     * @brief Draw widget (override)
     */
    void OnDraw() override;

private:
    /**
     * @brief Calculate final render rectangle based on fill mode
     */
    Rect CalculateRenderRect() const;

    TextureHandle m_texture;
    vec4 m_tintColor;
    UVRect m_uvRect;
    ImageFillMode m_fillMode;

    bool m_nineSliceEnabled;
    NineSlice m_nineSlice;

    bool m_preserveAspectRatio;
    vec2 m_textureSize; // Actual texture dimensions for aspect ratio calculation
};

} // namespace Engine::UI

#endif // ENGINE_UI_IMAGE_HPP
