#ifndef ENGINE_UI_FONT_RENDERER_HPP
#define ENGINE_UI_FONT_RENDERER_HPP

#include "../math/Vector.hpp"
#include "../core/Types.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

namespace Engine::UI {

using Engine::u8;
using Engine::u16;
using Engine::u32;
using Engine::u64;

/**
 * @brief Glyph metrics for text layout
 */
struct GlyphMetrics {
    u32 codepoint;          // Unicode codepoint
    float advance;          // Horizontal advance to next character
    float bearingX;         // Horizontal bearing (offset from cursor)
    float bearingY;         // Vertical bearing (offset from baseline)
    float width;            // Glyph width
    float height;           // Glyph height

    // UV coordinates in atlas (0-1 range)
    float uvX, uvY;         // Top-left UV
    float uvWidth, uvHeight; // UV size

    GlyphMetrics()
        : codepoint(0), advance(0.0f)
        , bearingX(0.0f), bearingY(0.0f)
        , width(0.0f), height(0.0f)
        , uvX(0.0f), uvY(0.0f)
        , uvWidth(0.0f), uvHeight(0.0f)
    {}
};

/**
 * @brief Text layout result for rendering
 */
struct TextLayout {
    struct GlyphInstance {
        vec2 position;      // Position in pixels
        vec2 size;          // Size in pixels
        vec2 uvMin;         // Top-left UV
        vec2 uvMax;         // Bottom-right UV
        u32 codepoint;      // Unicode codepoint
    };

    std::vector<GlyphInstance> glyphs;
    vec2 bounds;            // Total text bounds (width, height)
    float baseline;         // Baseline Y position
};

/**
 * @brief Font atlas containing SDF glyphs
 */
class FontAtlas {
public:
    FontAtlas(u32 width, u32 height);
    ~FontAtlas();

    /**
     * @brief Get atlas dimensions
     */
    u32 GetWidth() const { return m_width; }
    u32 GetHeight() const { return m_height; }

    /**
     * @brief Get atlas pixel data (grayscale SDF)
     */
    const u8* GetData() const { return m_data.data(); }
    u8* GetData() { return m_data.data(); }

    /**
     * @brief Get glyph metrics for a codepoint
     */
    const GlyphMetrics* GetGlyph(u32 codepoint) const;

    /**
     * @brief Add glyph to atlas
     */
    void AddGlyph(u32 codepoint, const GlyphMetrics& metrics);

    /**
     * @brief Get kerning between two characters
     */
    float GetKerning(u32 first, u32 second) const;

    /**
     * @brief Set kerning between two characters
     */
    void SetKerning(u32 first, u32 second, float kerning);

    /**
     * @brief Get font size this atlas was generated for
     */
    float GetFontSize() const { return m_fontSize; }
    void SetFontSize(float size) { m_fontSize = size; }

    /**
     * @brief Get line height in pixels
     */
    float GetLineHeight() const { return m_lineHeight; }
    void SetLineHeight(float height) { m_lineHeight = height; }

    /**
     * @brief Get ascent (distance from baseline to top)
     */
    float GetAscent() const { return m_ascent; }
    void SetAscent(float ascent) { m_ascent = ascent; }

    /**
     * @brief Get descent (distance from baseline to bottom, usually negative)
     */
    float GetDescent() const { return m_descent; }
    void SetDescent(float descent) { m_descent = descent; }

private:
    u32 m_width;
    u32 m_height;
    std::vector<u8> m_data;
    std::unordered_map<u32, GlyphMetrics> m_glyphs;
    std::unordered_map<u64, float> m_kerningPairs; // Packed (first << 32 | second)

    float m_fontSize;
    float m_lineHeight;
    float m_ascent;
    float m_descent;
};

/**
 * @brief Font renderer with SDF (Signed Distance Field) support
 *
 * Loads TrueType fonts and generates high-quality SDF atlases for resolution-independent text rendering.
 * Uses stb_truetype for font parsing and generates distance fields for smooth scaling.
 */
class FontRenderer {
public:
    FontRenderer();
    ~FontRenderer();

    /**
     * @brief Load font from TTF file
     * @param filepath Path to TTF font file
     * @param fontSize Font size to generate atlas for (pixels)
     * @param atlasWidth Atlas texture width
     * @param atlasHeight Atlas texture height
     * @param sdfPadding Padding around glyphs for SDF (default 4)
     * @param sdfOnEdgeValue Distance field on-edge value (default 128)
     * @return Shared pointer to font atlas, or nullptr on failure
     */
    std::shared_ptr<FontAtlas> LoadFont(
        const std::string& filepath,
        float fontSize = 32.0f,
        u32 atlasWidth = 1024,
        u32 atlasHeight = 1024,
        u32 sdfPadding = 4,
        u8 sdfOnEdgeValue = 128
    );

    /**
     * @brief Load font from memory buffer
     * @param data Font data buffer
     * @param dataSize Size of font data
     * @param fontSize Font size to generate atlas for
     * @param atlasWidth Atlas texture width
     * @param atlasHeight Atlas texture height
     * @param sdfPadding Padding around glyphs for SDF
     * @param sdfOnEdgeValue Distance field on-edge value
     * @return Shared pointer to font atlas, or nullptr on failure
     */
    std::shared_ptr<FontAtlas> LoadFontFromMemory(
        const u8* data,
        size_t dataSize,
        float fontSize = 32.0f,
        u32 atlasWidth = 1024,
        u32 atlasHeight = 1024,
        u32 sdfPadding = 4,
        u8 sdfOnEdgeValue = 128
    );

    /**
     * @brief Layout text for rendering
     * @param atlas Font atlas to use
     * @param text UTF-8 text string
     * @param fontSize Desired font size (can differ from atlas size)
     * @param maxWidth Maximum width before wrapping (0 = no wrap)
     * @return Text layout with positioned glyphs
     */
    TextLayout LayoutText(
        const FontAtlas& atlas,
        const std::string& text,
        float fontSize,
        float maxWidth = 0.0f
    ) const;

    /**
     * @brief Measure text dimensions without full layout
     * @param atlas Font atlas to use
     * @param text UTF-8 text string
     * @param fontSize Desired font size
     * @return Text bounds (width, height)
     */
    vec2 MeasureText(
        const FontAtlas& atlas,
        const std::string& text,
        float fontSize
    ) const;

    /**
     * @brief Render text to vertex buffer
     * @param atlas Font atlas to use
     * @param text UTF-8 text string
     * @param position Top-left position in pixels
     * @param fontSize Font size in pixels
     * @param color Text color
     * @param outVertices Output vertex positions (x,y)
     * @param outUVs Output UV coordinates
     * @param outIndices Output triangle indices
     */
    void RenderText(
        const FontAtlas& atlas,
        const std::string& text,
        const vec2& position,
        float fontSize,
        const vec4& color,
        std::vector<vec2>& outVertices,
        std::vector<vec2>& outUVs,
        std::vector<u16>& outIndices
    ) const;

private:
    /**
     * @brief Generate SDF for a single glyph
     */
    void GenerateGlyphSDF(
        const u8* bitmap,
        u32 width,
        u32 height,
        u8* output,
        u32 outputWidth,
        u32 outputHeight,
        u32 padding,
        u8 onEdgeValue
    );

    /**
     * @brief Decode UTF-8 codepoint from string
     * @param text UTF-8 string
     * @param offset Current offset in string (will be advanced)
     * @return Unicode codepoint
     */
    u32 DecodeUTF8(const std::string& text, size_t& offset) const;

    /**
     * @brief Get font scale factor for target size
     */
    float GetFontScale(const FontAtlas& atlas, float targetSize) const;
};

} // namespace Engine::UI

#endif // ENGINE_UI_FONT_RENDERER_HPP
