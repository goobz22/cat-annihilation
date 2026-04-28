#pragma once

// Post-transform vertex-cache optimization + ACMR measurement.
//
// Two reorderers are exposed so the engine can benchmark one against the
// other (the Catch2 test in tests/unit/test_mesh_optimizer.cpp asserts the
// Tipsy reorder is at least as cache-friendly as Forsyth on a
// representative mesh):
//
//   - OptimizeForsyth — Tom Forsyth, "Linear-Speed Vertex Cache
//     Optimisation" (2006). Per-iteration scoring combines FIFO cache
//     residency with a valence boost; emits the globally highest-scoring
//     triangle touched by anything currently in the cache. Asymptotically
//     O(N · cacheSize · meanValence).
//
//   - OptimizeTipsy — Sander, Nehab & Barczak, "Fast Triangle Reordering
//     for Vertex Locality and Reduced Overdraw" (SIGGRAPH 2007). Grows
//     strips by *fanning* around a focus vertex, falls back to a
//     dead-end stack of recently-cached vertices when the fan stalls,
//     and uses a cheaper cache-bonus scoring curve. Runs in O(N) time
//     and ties or beats Forsyth on irregular meshes.
//
// Both functions operate in-place on a flat index buffer. If the mesh has
// multiple submeshes, call the optimizer once per submesh range so
// triangles stay grouped with their material (the Mesh::OptimizeIndices
// wrapper does this for us).
//
// Vertex count is the number of *vertex slots* the indices may reference,
// not the number of unique vertices used by this range. Per-vertex
// scratch arrays are sized to vertexCount so indices that don't show up
// in the range simply have zero live-triangle counts and are ignored.

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace CatEngine::Renderer::MeshOptimizer {

// Default post-transform cache size. 32 matches Forsyth's paper, the Tipsy
// paper's tests, and the middle of modern GPU ranges (NVIDIA 32, AMD 16,
// Intel 32-64, mobile tilers up to 128 effective after shader-invocation
// batching). Changing this invalidates the ACMR test assertions.
inline constexpr uint32_t kDefaultCacheSize = 32;

// Average Cache Miss Ratio: fraction of vertex *indices* that miss a
// simulated FIFO cache of `cacheSize` entries, divided by triangle
// count. Units: misses per triangle. Lower is better.
//
// Reference values on a large regular mesh with a 32-entry cache:
//   - Random / unoptimized triangle order: ~1.8 - 2.0
//   - Strip order (best case): ~0.5
//   - Forsyth / Tipsy on a well-formed mesh: ~0.6 - 0.7
inline float ComputeACMR(const uint32_t* indices,
                         size_t indexCount,
                         uint32_t vertexCount,
                         uint32_t cacheSize = kDefaultCacheSize) {
    if (indexCount == 0 || vertexCount == 0) return 0.0f;

    // Timestamp-based FIFO simulation: a vertex v is "in cache" iff its
    // last-insertion timestamp is within the last `cacheSize` insertions.
    // This is equivalent to an evict-oldest-on-miss FIFO queue but avoids
    // shuffling a ring buffer on each miss.
    std::vector<int32_t> lastInserted(vertexCount, -1);
    int32_t insertionCount = 0;
    const int32_t cacheSizeSigned = static_cast<int32_t>(cacheSize);

    uint32_t misses = 0;
    for (size_t i = 0; i < indexCount; ++i) {
        const uint32_t v = indices[i];
        if (v >= vertexCount) continue;
        const bool inCache = (lastInserted[v] >= 0) &&
                             (insertionCount - lastInserted[v] < cacheSizeSigned);
        if (!inCache) {
            ++misses;
            lastInserted[v] = insertionCount++;
        }
    }

    const float triangleCount = static_cast<float>(indexCount / 3);
    if (triangleCount <= 0.0f) return 0.0f;
    return static_cast<float>(misses) / triangleCount;
}

inline float ComputeACMR(const std::vector<uint32_t>& indices,
                         uint32_t vertexCount,
                         uint32_t cacheSize = kDefaultCacheSize) {
    return ComputeACMR(indices.data(), indices.size(), vertexCount, cacheSize);
}

// -----------------------------------------------------------------------
// Forsyth (2006)
// -----------------------------------------------------------------------
// Scoring constants from the reference paper.
// kCacheDecayPower shapes the fall-off from the newest cache slot down
// to the oldest; kLastTriScore pulls the three most-recent vertices
// together so the scorer doesn't prefer re-emitting the last triangle.
// kValenceBoost{Scale,Power} bias toward vertices that are about to run
// out of live triangles — they should be consumed while still in cache.
inline constexpr float kForsythCacheDecayPower = 1.5f;
inline constexpr float kForsythLastTriScore    = 0.75f;
inline constexpr float kForsythValenceScale    = 2.0f;
inline constexpr float kForsythValencePower    = 0.5f;

inline void OptimizeForsyth(uint32_t* indices,
                            size_t indexCount,
                            uint32_t vertexCount,
                            uint32_t cacheSize = kDefaultCacheSize) {
    if (indexCount < 3 || vertexCount == 0) return;
    const uint32_t triangleCount = static_cast<uint32_t>(indexCount / 3);
    const uint32_t kInvalid = std::numeric_limits<uint32_t>::max();

    // Per-vertex valence (number of live triangles still using it).
    std::vector<uint32_t> valence(vertexCount, 0);
    for (size_t i = 0; i < indexCount; ++i) {
        if (indices[i] < vertexCount) ++valence[indices[i]];
    }

    // CSR adjacency: vertex -> list of triangle ids.
    std::vector<uint32_t> triOffset(vertexCount + 1, 0);
    for (uint32_t v = 0; v < vertexCount; ++v) {
        triOffset[v + 1] = triOffset[v] + valence[v];
    }
    std::vector<uint32_t> triList(indexCount);
    std::vector<uint32_t> writeCursor(vertexCount, 0);
    for (uint32_t t = 0; t < triangleCount; ++t) {
        for (uint32_t c = 0; c < 3; ++c) {
            const uint32_t v = indices[t * 3 + c];
            if (v < vertexCount) {
                triList[triOffset[v] + writeCursor[v]++] = t;
            }
        }
    }

    auto scoreVertex = [&](int32_t cachePosition, uint32_t remainingValence) -> float {
        if (remainingValence == 0) return -1.0f;
        float score = 0.0f;
        if (cachePosition < 0) {
            score = 0.0f;
        } else if (cachePosition < 3) {
            score = kForsythLastTriScore;
        } else {
            const float scaler = 1.0f / static_cast<float>(cacheSize - 3);
            score = 1.0f - static_cast<float>(cachePosition - 3) * scaler;
            score = std::pow(score, kForsythCacheDecayPower);
        }
        const float valenceBoost =
            std::pow(static_cast<float>(remainingValence), -kForsythValencePower);
        score += kForsythValenceScale * valenceBoost;
        return score;
    };

    std::vector<uint32_t> remaining = valence;
    std::vector<int32_t>  cachePosition(vertexCount, -1);
    std::vector<float>    vertexScore(vertexCount, 0.0f);
    std::vector<bool>     triEmitted(triangleCount, false);
    std::vector<float>    triScore(triangleCount, 0.0f);

    for (uint32_t v = 0; v < vertexCount; ++v) {
        if (remaining[v] > 0) {
            vertexScore[v] = scoreVertex(-1, remaining[v]);
        }
    }
    for (uint32_t t = 0; t < triangleCount; ++t) {
        const uint32_t a = indices[t * 3 + 0];
        const uint32_t b = indices[t * 3 + 1];
        const uint32_t c = indices[t * 3 + 2];
        triScore[t] = vertexScore[a] + vertexScore[b] + vertexScore[c];
    }

    // Cache is a fixed-size ring; slot 0 is the most-recently emitted
    // vertex. Three extra slots trail the cache so evicted vertices in a
    // single triangle's worth of pushes are still visible for score reset.
    std::vector<uint32_t> cache(cacheSize + 3, kInvalid);

    std::vector<uint32_t> out;
    out.reserve(indexCount);

    uint32_t emittedCount = 0;
    while (emittedCount < triangleCount) {
        int32_t bestTriangle = -1;
        float bestScore = -1.0f;

        // Steady state: scan only triangles touched by vertices currently
        // in cache. First iteration falls through to a full linear scan.
        for (uint32_t slot = 0; slot < cacheSize; ++slot) {
            const uint32_t v = cache[slot];
            if (v == kInvalid) continue;
            const uint32_t start = triOffset[v];
            const uint32_t end   = triOffset[v + 1];
            for (uint32_t k = start; k < end; ++k) {
                const uint32_t t = triList[k];
                if (triEmitted[t]) continue;
                if (triScore[t] > bestScore) {
                    bestScore = triScore[t];
                    bestTriangle = static_cast<int32_t>(t);
                }
            }
        }
        if (bestTriangle < 0) {
            for (uint32_t t = 0; t < triangleCount; ++t) {
                if (!triEmitted[t] && triScore[t] > bestScore) {
                    bestScore = triScore[t];
                    bestTriangle = static_cast<int32_t>(t);
                }
            }
            if (bestTriangle < 0) break;
        }

        const uint32_t base = static_cast<uint32_t>(bestTriangle) * 3;
        const uint32_t triVerts[3] = { indices[base], indices[base + 1], indices[base + 2] };
        out.push_back(triVerts[0]);
        out.push_back(triVerts[1]);
        out.push_back(triVerts[2]);
        triEmitted[bestTriangle] = true;
        for (uint32_t v : triVerts) {
            if (v < vertexCount) --remaining[v];
        }
        ++emittedCount;

        // Push this triangle's vertices to the front of the cache; anything
        // already present moves rather than duplicates.
        std::vector<uint32_t> newCache(cacheSize + 3, kInvalid);
        uint32_t writeIdx = 0;
        newCache[writeIdx++] = triVerts[0];
        newCache[writeIdx++] = triVerts[1];
        newCache[writeIdx++] = triVerts[2];
        for (uint32_t slot = 0; slot < cacheSize; ++slot) {
            const uint32_t v = cache[slot];
            if (v == kInvalid) continue;
            if (v == triVerts[0] || v == triVerts[1] || v == triVerts[2]) continue;
            if (writeIdx >= newCache.size()) break;
            newCache[writeIdx++] = v;
        }
        cache.swap(newCache);

        // Score update: every vertex still in the extended cache window
        // may have a new cache position and every emitted triangle's
        // vertex has a decremented valence. Apply the delta to the
        // triangle scores once, pointwise, rather than rebuilding.
        for (uint32_t slot = 0; slot < cache.size(); ++slot) {
            const uint32_t v = cache[slot];
            if (v == kInvalid) continue;
            const int32_t newPos = (slot < cacheSize) ? static_cast<int32_t>(slot) : -1;
            cachePosition[v] = newPos;
            const float newScore = scoreVertex(newPos, remaining[v]);
            const float delta = newScore - vertexScore[v];
            vertexScore[v] = newScore;
            if (delta != 0.0f) {
                const uint32_t start = triOffset[v];
                const uint32_t end   = triOffset[v + 1];
                for (uint32_t k = start; k < end; ++k) {
                    const uint32_t t = triList[k];
                    if (!triEmitted[t]) triScore[t] += delta;
                }
            }
        }
    }

    for (size_t i = 0; i < out.size() && i < indexCount; ++i) {
        indices[i] = out[i];
    }
}

inline void OptimizeForsyth(std::vector<uint32_t>& indices,
                            uint32_t vertexCount,
                            uint32_t cacheSize = kDefaultCacheSize) {
    OptimizeForsyth(indices.data(), indices.size(), vertexCount, cacheSize);
}

// -----------------------------------------------------------------------
// Tipsy (Sander, Nehab, Barczak 2007)
// -----------------------------------------------------------------------
// Tipsy's scoring is deliberately simpler than Forsyth's. "In-cache"
// counts as a flat 0.75 bonus regardless of slot, which the paper
// justifies by noting the real GPU cache's LRU-like behaviour makes fine-
// grained position scoring speculative. The valence bonus follows the
// same 2·L^(-1/2) curve Forsyth uses — intuitively, consume almost-dead
// vertices while they're hot.
inline constexpr float kTipsyCacheBonus    = 0.75f;
inline constexpr float kTipsyValenceScale  = 2.0f;

inline void OptimizeTipsy(uint32_t* indices,
                          size_t indexCount,
                          uint32_t vertexCount,
                          uint32_t cacheSize = kDefaultCacheSize) {
    if (indexCount < 3 || vertexCount == 0) return;
    const uint32_t triangleCount = static_cast<uint32_t>(indexCount / 3);
    const uint32_t kInvalid = std::numeric_limits<uint32_t>::max();

    // Per-vertex live triangle count + CSR adjacency (same layout as Forsyth).
    std::vector<uint32_t> live(vertexCount, 0);
    for (size_t i = 0; i < indexCount; ++i) {
        if (indices[i] < vertexCount) ++live[indices[i]];
    }
    std::vector<uint32_t> triOffset(vertexCount + 1, 0);
    for (uint32_t v = 0; v < vertexCount; ++v) {
        triOffset[v + 1] = triOffset[v] + live[v];
    }
    std::vector<uint32_t> triList(indexCount);
    std::vector<uint32_t> writeCursor(vertexCount, 0);
    for (uint32_t t = 0; t < triangleCount; ++t) {
        for (uint32_t c = 0; c < 3; ++c) {
            const uint32_t v = indices[t * 3 + c];
            if (v < vertexCount) triList[triOffset[v] + writeCursor[v]++] = t;
        }
    }

    // Live state.
    std::vector<uint32_t> remaining = live;
    // timeStamp[v] = insertion count at which v was last put into the cache.
    // A vertex is "in cache" iff (insertions - timeStamp[v]) < cacheSize.
    std::vector<int32_t>  timeStamp(vertexCount, -1);
    int32_t insertions = 0;
    std::vector<bool>     emitted(triangleCount, false);

    // Dead-end stack of recently-cached vertices we can fall back to when
    // the current fan stalls. Bounded by vertexCount; reserve up front.
    std::vector<uint32_t> deadEnd;
    deadEnd.reserve(vertexCount);

    std::vector<uint32_t> out;
    out.reserve(indexCount);

    // Paper section 3.1: a vertex "counts as cached" for scoring iff it is
    // still expected to be in the FIFO after emitting two more triangles
    // (6 vertex pushes). That 6-slot slack prevents the reorderer from
    // picking triangles whose "cached" vertex will evict itself before we
    // ever hit it again. Without this padding Tipsy scores are noisy and
    // the algorithm ends up performing worse than Forsyth.
    const int32_t cacheSizeSigned = static_cast<int32_t>(cacheSize);
    constexpr int32_t kEmitLookahead = 6;
    auto inCache = [&](uint32_t v) -> bool {
        if (v >= vertexCount || timeStamp[v] < 0) return false;
        return (insertions - timeStamp[v]) + kEmitLookahead <= cacheSizeSigned;
    };

    auto scoreTriangle = [&](uint32_t t) -> float {
        float score = 0.0f;
        for (uint32_t c = 0; c < 3; ++c) {
            const uint32_t v = indices[t * 3 + c];
            if (v >= vertexCount || remaining[v] == 0) continue;
            float s = 0.0f;
            if (inCache(v)) s += kTipsyCacheBonus;
            s += kTipsyValenceScale / std::sqrt(static_cast<float>(remaining[v]));
            score += s;
        }
        return score;
    };

    uint32_t linearCursor = 0;
    auto pickFan = [&]() -> uint32_t {
        // Prefer the freshest cached vertex on the dead-end stack; popping
        // toward older entries approximates Tipsy's paper recommendation.
        while (!deadEnd.empty()) {
            const uint32_t v = deadEnd.back();
            deadEnd.pop_back();
            if (remaining[v] > 0) return v;
        }
        while (linearCursor < vertexCount && remaining[linearCursor] == 0) {
            ++linearCursor;
        }
        if (linearCursor < vertexCount) return linearCursor;
        return kInvalid;
    };

    uint32_t fan = pickFan();
    uint32_t emittedCount = 0;
    while (emittedCount < triangleCount) {
        if (fan == kInvalid || remaining[fan] == 0) {
            fan = pickFan();
            if (fan == kInvalid) break;
        }

        // Highest-scoring live triangle touching the fan vertex.
        uint32_t bestTri = kInvalid;
        float bestScore = -1.0f;
        const uint32_t start = triOffset[fan];
        const uint32_t end   = triOffset[fan + 1];
        for (uint32_t k = start; k < end; ++k) {
            const uint32_t t = triList[k];
            if (emitted[t]) continue;
            const float s = scoreTriangle(t);
            if (s > bestScore) {
                bestScore = s;
                bestTri = t;
            }
        }
        if (bestTri == kInvalid) {
            // fan has remaining>0 but no live triangle (shouldn't happen —
            // remaining==0 iff all incident triangles are dead). Guard and
            // force a refetch.
            remaining[fan] = 0;
            fan = kInvalid;
            continue;
        }

        // Emit the triangle.
        const uint32_t base = bestTri * 3;
        const uint32_t v0 = indices[base];
        const uint32_t v1 = indices[base + 1];
        const uint32_t v2 = indices[base + 2];
        out.push_back(v0);
        out.push_back(v1);
        out.push_back(v2);
        emitted[bestTri] = true;
        ++emittedCount;

        // Update cache + dead-end stack + remaining counts.
        for (uint32_t v : std::array<uint32_t, 3>{v0, v1, v2}) {
            if (v >= vertexCount) continue;
            const bool wasInCache = inCache(v);
            if (!wasInCache) {
                timeStamp[v] = insertions++;
                deadEnd.push_back(v);
            }
            if (remaining[v] > 0) --remaining[v];
        }

        // Choose the next fan vertex: pick the triangle's vertex that is
        // still live AND still in cache, preferring the highest remaining
        // live count (it has the most fan potential). Falling back to the
        // dead-end stack happens naturally at the top of the loop when
        // no such candidate exists.
        uint32_t nextFan = kInvalid;
        uint32_t nextFanLive = 0;
        for (uint32_t v : std::array<uint32_t, 3>{v0, v1, v2}) {
            if (v >= vertexCount || remaining[v] == 0) continue;
            if (!inCache(v)) continue;
            if (nextFan == kInvalid || remaining[v] > nextFanLive) {
                nextFan = v;
                nextFanLive = remaining[v];
            }
        }
        fan = nextFan;
    }

    // Copy back — if for any reason we didn't emit every triangle
    // (shouldn't happen for a well-formed input), append the rest in
    // their original order so no triangle is dropped.
    if (out.size() < indexCount) {
        for (uint32_t t = 0; t < triangleCount; ++t) {
            if (!emitted[t]) {
                out.push_back(indices[t * 3 + 0]);
                out.push_back(indices[t * 3 + 1]);
                out.push_back(indices[t * 3 + 2]);
            }
        }
    }
    for (size_t i = 0; i < out.size() && i < indexCount; ++i) {
        indices[i] = out[i];
    }
}

inline void OptimizeTipsy(std::vector<uint32_t>& indices,
                          uint32_t vertexCount,
                          uint32_t cacheSize = kDefaultCacheSize) {
    OptimizeTipsy(indices.data(), indices.size(), vertexCount, cacheSize);
}

} // namespace CatEngine::Renderer::MeshOptimizer
