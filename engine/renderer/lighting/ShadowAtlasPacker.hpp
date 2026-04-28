#ifndef ENGINE_RENDERER_LIGHTING_SHADOW_ATLAS_PACKER_HPP
#define ENGINE_RENDERER_LIGHTING_SHADOW_ATLAS_PACKER_HPP

// ============================================================================
// ShadowAtlasPacker — variable-size 2-D region packer for the shadow atlas.
// ============================================================================
//
// Backlog reference: ENGINE_BACKLOG.md P0 "Shadow atlas: variable-size region
// packing." The previous ShadowAtlas implementation used a strict shelf
// allocator that pinned each row to the height of the first tile placed on
// it, which fragmented heavily as soon as the atlas had to hold
// cascades + spot shadows + point cubemap faces together. A mixed 2048 /
// 1024 / 512 / 256 workload measured well under 60 % density on the old
// packer, below the 80 % bar the backlog sets.
//
// Algorithm: Guillotine packing (Jylänki 2010, "A Thousand Ways to Pack the
// Bin"). Every failed shelf version of this file reached for a FFDH (First
// Fit Decreasing Height) heuristic first, which works for one-shot batches
// but breaks the moment a light is freed mid-frame and its tile has to be
// reused. Guillotine handles incremental insert-and-free cleanly:
//
//   1. Insertion: scan the free-rect list with Best-Short-Side-Fit (BSSF).
//      Among free rectangles large enough to hold (w, h), pick the one
//      whose smaller leftover side is smallest; this keeps the larger
//      leftover rectangle as one single, big, still-healthy rectangle
//      instead of shaving a thin slice off the biggest free rect.
//
//   2. Split: after placing the tile at the top-left of the chosen free
//      rect, the L-shaped leftover is cut into two disjoint rectangles
//      by one guillotine cut. We use Shorter-Axis-Split (SAS) — cut so
//      the bigger of the two leftover strips becomes the full-extent one
//      along whichever axis has the larger leftover. This is a known
//      pairing with BSSF that produces reliably high density on
//      power-of-two tile mixes typical of shadow atlases.
//
//   3. Free: push the freed rect back into the free set, then coalesce any
//      free rects that share a full edge. Without the coalesce step the
//      free list fragments after a few alloc/free cycles and density
//      drops back toward the shelf-allocator floor. The coalesce is
//      O(N²) per pass and loops until no merges happen, but N on a
//      shadow atlas is tiny (≤ a few dozen free rects) so this is cheap.
//
// The packer is pure-integer and deliberately header-only with zero
// dependency on the RHI or math layer, so:
//   (a) it can be exercised by a standalone Catch2 unit test without
//       pulling in the RHI / CUDA mocks (see tests/unit/
//       test_shadow_atlas_packer.cpp), and
//   (b) the same packer can be reused for any future tile-atlas need
//       (lightmap atlas, virtual-texture page atlas, UI glyph atlas,
//       cooking texture atlas, ...) without dragging in lighting types.
//
// Why not "pick the library meshoptimizer/stb_rect_pack solves this":
// stb_rect_pack is a one-shot packer (no in-place free after pack()),
// and meshoptimizer's scope doesn't include texture atlases. A hand-
// rolled ~200-line Guillotine keeps the engine lean and portfolio-
// documentable — and matches the learning-artifact brief in CLAUDE.md.

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace Engine::Renderer {

/**
 * 2-D integer pixel rectangle. Mirrors the on-GPU meaning of
 * ShadowAtlas::ShadowRegion's spatial fields (no UV / generation /
 * active metadata — that is ShadowAtlas's responsibility).
 */
struct PackedRect {
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t w = 0;
    uint32_t h = 0;

    uint64_t area() const {
        return static_cast<uint64_t>(w) * static_cast<uint64_t>(h);
    }
    bool empty() const { return w == 0 || h == 0; }
    uint32_t right() const { return x + w; }
    uint32_t bottom() const { return y + h; }

    bool operator==(const PackedRect& rhs) const {
        return x == rhs.x && y == rhs.y && w == rhs.w && h == rhs.h;
    }
    bool operator!=(const PackedRect& rhs) const { return !(*this == rhs); }

    /**
     * True if this rectangle and @p other overlap in any pixel. Used by
     * tests to assert the packer never double-hands the same pixel to
     * two live allocations.
     */
    bool intersects(const PackedRect& other) const {
        if (empty() || other.empty()) return false;
        return !(right()  <= other.x      ||
                 other.right() <= x       ||
                 bottom() <= other.y      ||
                 other.bottom() <= y);
    }
};

/**
 * Guillotine BSSF + SAS + free-merge packer.
 *
 * Invariants maintained between public calls:
 *   - The free-rect list tiles the unused portion of the atlas without
 *     overlap. (Enforced by never splitting a placed rect into
 *     overlapping leftovers and by only adding freed rects back
 *     non-overlapping.)
 *   - `usedPixels()` equals the sum of `area()` for every successful
 *     `insert()` minus every matched `free()`.
 *
 * The packer does NOT track per-allocation identity — it hands out
 * PackedRect values by value and trusts the caller to feed the same
 * rect back to `free()`. The shadow atlas already carries
 * (generation, active) bits per allocation so double-free detection
 * lives there.
 */
class GuillotinePacker {
public:
    GuillotinePacker() = default;

    GuillotinePacker(uint32_t width, uint32_t height)
        : m_width(width), m_height(height) {
        reset();
    }

    /**
     * Change the atlas size and drop all existing allocations. Used by
     * ShadowAtlas::initialize() when the caller passes a size that
     * differs from the packer's construction-time dimensions.
     */
    void resize(uint32_t width, uint32_t height) {
        m_width = width;
        m_height = height;
        reset();
    }

    /**
     * Drop every allocation. After reset(), the atlas has one free rect
     * covering its whole extent (or zero, if dimensions are degenerate).
     */
    void reset() {
        m_freeRects.clear();
        m_usedPixels = 0;
        if (m_width > 0 && m_height > 0) {
            m_freeRects.push_back(PackedRect{0, 0, m_width, m_height});
        }
    }

    /**
     * Try to place a (w, h) tile. Returns the placed rect on success or
     * std::nullopt if no free rect is large enough. The packer does not
     * rotate tiles — ShadowResolution::High staying axis-aligned with
     * the texture matters for anisotropic shadow filtering, so rotation
     * is intentionally disabled.
     */
    std::optional<PackedRect> insert(uint32_t w, uint32_t h) {
        if (w == 0 || h == 0) return std::nullopt;
        if (w > m_width || h > m_height) return std::nullopt;

        // BSSF: the free rect that leaves the smaller-of-the-two
        // leftovers smallest. A tighter short-side leftover means a
        // more regular remainder, which compounds favourably over many
        // insertions. scoreBSSF() returns std::min(leftoverW, leftoverH)
        // for the picked rect — smaller is better.
        size_t bestIndex = m_freeRects.size();
        int64_t bestScore = std::numeric_limits<int64_t>::max();
        for (size_t i = 0; i < m_freeRects.size(); ++i) {
            const PackedRect& candidate = m_freeRects[i];
            if (!fits(candidate, w, h)) continue;
            const int64_t score = scoreBSSF(candidate, w, h);
            if (score < bestScore) {
                bestScore = score;
                bestIndex = i;
            }
        }
        if (bestIndex == m_freeRects.size()) return std::nullopt;

        const PackedRect chosen = m_freeRects[bestIndex];
        PackedRect placed{chosen.x, chosen.y, w, h};

        // Remove the consumed free rect by swap-and-pop so the remaining
        // indices stay contiguous without paying for vector shifts.
        m_freeRects[bestIndex] = m_freeRects.back();
        m_freeRects.pop_back();

        splitFreeRect(chosen, placed);
        m_usedPixels += placed.area();
        return placed;
    }

    /**
     * Return a previously-placed rect to the free pool and attempt to
     * merge it with any adjacent free rects. The rect must be a valid
     * sub-region of the atlas; feeding back an unrelated rect corrupts
     * the free set and breaks the no-overlap invariant.
     */
    void free(PackedRect rect) {
        if (rect.empty()) return;
        if (rect.right() > m_width || rect.bottom() > m_height) return;
        const uint64_t area = rect.area();
        // Subtract clamped at zero so repeated double-free bugs from the
        // caller cannot drive usedPixels below zero (it's unsigned — a
        // plain `-=` would underflow and claim the atlas is 99.99 %
        // full). This is belt-and-suspenders: the shadow atlas's own
        // active-bit already guards against double-free, but the packer
        // is a general-purpose tool and shouldn't blow up on misuse.
        m_usedPixels = area >= m_usedPixels ? 0 : m_usedPixels - area;
        m_freeRects.push_back(rect);
        // Merge until stable. Each mergeOnce() pass is O(N²); total
        // worst case is therefore O(N³), but N (free-rect count) is
        // small in practice — a few dozen is an extreme load on a
        // shadow atlas.
        while (mergeOnce()) {
            // Loop body is intentionally empty — mergeOnce() has the
            // side effect of mutating m_freeRects, and the outer loop
            // just drives it to a fixed point.
        }
    }

    /**
     * Non-mutating probe: would a (w, h) tile fit *without* inserting?
     * The old shelf allocator had a bug where the equivalent probe
     * silently grew a new shelf even when the caller only wanted to
     * ask "is there space?". This version is const and side-effect
     * free.
     */
    bool canFit(uint32_t w, uint32_t h) const {
        if (w == 0 || h == 0) return false;
        if (w > m_width || h > m_height) return false;
        for (const PackedRect& candidate : m_freeRects) {
            if (fits(candidate, w, h)) return true;
        }
        return false;
    }

    uint64_t usedPixels() const { return m_usedPixels; }

    float density() const {
        const uint64_t total = static_cast<uint64_t>(m_width) *
                               static_cast<uint64_t>(m_height);
        return total == 0
                 ? 0.0f
                 : static_cast<float>(
                       static_cast<double>(m_usedPixels) /
                       static_cast<double>(total));
    }

    uint32_t width() const { return m_width; }
    uint32_t height() const { return m_height; }

    /** Exposed for tests and debug overlays. */
    const std::vector<PackedRect>& freeRects() const { return m_freeRects; }

private:
    static bool fits(const PackedRect& freeRect, uint32_t w, uint32_t h) {
        return freeRect.w >= w && freeRect.h >= h;
    }

    // BSSF score = min(leftoverW, leftoverH). Lower is better, zero is
    // perfect (the tile exactly matches one side of the free rect).
    static int64_t scoreBSSF(const PackedRect& freeRect, uint32_t w, uint32_t h) {
        const int64_t leftoverW = static_cast<int64_t>(freeRect.w) - static_cast<int64_t>(w);
        const int64_t leftoverH = static_cast<int64_t>(freeRect.h) - static_cast<int64_t>(h);
        return std::min(leftoverW, leftoverH);
    }

    // SAS split. The placed tile sits at the top-left of freeRect; the
    // L-shaped leftover is cut into two rectangles by one guillotine
    // line. We cut so that the larger-leftover axis keeps its
    // full extent as one single rectangle:
    //
    //   leftoverW ≤ leftoverH → cut horizontally through placed.bottom
    //                           (bottom leftover spans freeRect.w; right
    //                           leftover is only placed.h tall)
    //
    //   leftoverW  > leftoverH → cut vertically through placed.right
    //                           (right leftover spans freeRect.h; bottom
    //                           leftover is only placed.w wide)
    void splitFreeRect(const PackedRect& freeRect, const PackedRect& placed) {
        const uint32_t leftoverW = freeRect.w - placed.w;
        const uint32_t leftoverH = freeRect.h - placed.h;
        const bool horizontalCut = leftoverW <= leftoverH;

        PackedRect right{};
        PackedRect bottom{};
        if (horizontalCut) {
            right.x  = freeRect.x + placed.w;
            right.y  = freeRect.y;
            right.w  = leftoverW;
            right.h  = placed.h;
            bottom.x = freeRect.x;
            bottom.y = freeRect.y + placed.h;
            bottom.w = freeRect.w;
            bottom.h = leftoverH;
        } else {
            right.x  = freeRect.x + placed.w;
            right.y  = freeRect.y;
            right.w  = leftoverW;
            right.h  = freeRect.h;
            bottom.x = freeRect.x;
            bottom.y = freeRect.y + placed.h;
            bottom.w = placed.w;
            bottom.h = leftoverH;
        }

        if (right.w  > 0 && right.h  > 0) m_freeRects.push_back(right);
        if (bottom.w > 0 && bottom.h > 0) m_freeRects.push_back(bottom);
    }

    // One pass of adjacent-rectangle merging. Returns true if any merge
    // happened so the caller can re-run until a fixed point. Two free
    // rects can be merged when they share a full edge — i.e. identical
    // horizontal span plus touching Y-edges, OR identical vertical span
    // plus touching X-edges.
    //
    // This is deliberately non-transitive per call: after a merge we
    // bail out and let the outer loop restart from i = 0 / j = i+1. The
    // new super-rectangle may be mergeable with yet another rect we
    // already passed, so resuming from where we stopped would miss
    // those coalesces.
    bool mergeOnce() {
        for (size_t i = 0; i < m_freeRects.size(); ++i) {
            for (size_t j = i + 1; j < m_freeRects.size(); ++j) {
                PackedRect& a = m_freeRects[i];
                PackedRect& b = m_freeRects[j];
                // Horizontal strip neighbours: same y-band, touching
                // along their shared vertical edge.
                if (a.y == b.y && a.h == b.h) {
                    if (a.x + a.w == b.x) {
                        a.w += b.w;
                        m_freeRects[j] = m_freeRects.back();
                        m_freeRects.pop_back();
                        return true;
                    }
                    if (b.x + b.w == a.x) {
                        a.x = b.x;
                        a.w += b.w;
                        m_freeRects[j] = m_freeRects.back();
                        m_freeRects.pop_back();
                        return true;
                    }
                }
                // Vertical strip neighbours: same x-band, touching
                // along their shared horizontal edge.
                if (a.x == b.x && a.w == b.w) {
                    if (a.y + a.h == b.y) {
                        a.h += b.h;
                        m_freeRects[j] = m_freeRects.back();
                        m_freeRects.pop_back();
                        return true;
                    }
                    if (b.y + b.h == a.y) {
                        a.y = b.y;
                        a.h += b.h;
                        m_freeRects[j] = m_freeRects.back();
                        m_freeRects.pop_back();
                        return true;
                    }
                }
            }
        }
        return false;
    }

    uint32_t m_width = 0;
    uint32_t m_height = 0;
    uint64_t m_usedPixels = 0;
    std::vector<PackedRect> m_freeRects;
};

} // namespace Engine::Renderer

#endif // ENGINE_RENDERER_LIGHTING_SHADOW_ATLAS_PACKER_HPP
