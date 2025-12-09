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
     * Get the font atlas texture (nullptr for now - text renders as solid rectangles)
     */
    RHI::IRHITexture* GetAtlas() const { return nullptr; }

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
    
    static constexpr uint32_t ATLAS_WIDTH = 512;
    static constexpr uint32_t ATLAS_HEIGHT = 512;
    static constexpr uint32_t CHARS_PER_ROW = 16;
    static constexpr uint32_t CHAR_ROWS = 6;  // ASCII 32-127 = 96 chars
};

} // namespace CatEngine::Renderer

