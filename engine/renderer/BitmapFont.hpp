#pragma once

#include "../rhi/RHI.hpp"
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <memory>

namespace CatEngine::Renderer {

/**
 * Glyph metrics for a single character
 */
struct GlyphInfo {
    float u0, v0;       // Top-left UV
    float u1, v1;       // Bottom-right UV
    float width;        // Glyph width in pixels
    float height;       // Glyph height in pixels
    float xOffset;      // X offset when rendering
    float yOffset;      // Y offset when rendering
    float xAdvance;     // How far to advance cursor after this glyph
};

/**
 * Bitmap font for UI text rendering
 * Generates a simple procedural ASCII font atlas
 */
class BitmapFont {
public:
    BitmapFont() = default;
    ~BitmapFont() = default;

    /**
     * Initialize the font with the given RHI
     * Generates a procedural font atlas texture
     */
    bool Initialize(RHI::IRHI* rhi, uint32_t fontSize = 32);

    /**
     * Cleanup resources
     */
    void Cleanup();

    /**
     * Get the font atlas texture (R8_UNORM, one byte per texel representing
     * the rasterized glyph mask).
     */
    RHI::IRHITexture* GetAtlas() const { return m_atlasTexture; }

    /**
     * Get glyph info for a character
     */
    const GlyphInfo* GetGlyph(char c) const;

    /**
     * Get the line height
     */
    float GetLineHeight() const { return lineHeight_; }

    /**
     * Get font size
     */
    uint32_t GetFontSize() const { return fontSize_; }

    /**
     * Calculate text width in pixels
     */
    float CalculateTextWidth(const char* text) const;

private:
    RHI::IRHI* rhi_ = nullptr;
    std::unordered_map<char, GlyphInfo> glyphs_;
    uint32_t fontSize_ = 32;
    float lineHeight_ = 32.0f;

    // R8_UNORM atlas on the GPU plus the CPU-side byte buffer we rasterized
    // into. The CPU copy is retained so callers that want to re-upload after a
    // device loss (or inspect for tests) don't need to re-rasterize.
    RHI::IRHITexture* m_atlasTexture = nullptr;
    std::vector<uint8_t> m_atlasPixels;

    static constexpr uint32_t ATLAS_WIDTH = 512;
    static constexpr uint32_t ATLAS_HEIGHT = 512;
    static constexpr uint32_t CHARS_PER_ROW = 16;
    static constexpr uint32_t CHAR_ROWS = 6;  // ASCII 32-127 = 96 chars
};

} // namespace CatEngine::Renderer

