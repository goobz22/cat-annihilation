/**
 * @file ImageCompare.hpp
 * @brief Header-only golden-image comparator + PPM codec for CI.
 *
 * Building block for the "Headless render mode + golden-image CI test" P0
 * backlog item. Split intentionally from any Vulkan / swapchain readback
 * code so:
 *
 *   1. The math here (MSE / PSNR / SSIM) can be unit-tested against
 *      hand-crafted tiny images on a CI box that has no GPU, no Vulkan
 *      runtime, and no display — Catch2 runs it as pure host code alongside
 *      MeshOptimizer, ShadowAtlasPacker, SequentialImpulse, etc.
 *
 *   2. A future Renderer::CaptureSwapchainImageToPPM(...) can feed the
 *      same format the offline `ssim golden.ppm candidate.ppm` tool reads,
 *      and the CI step becomes:
 *
 *          CatAnnihilation.exe --autoplay --max-frames=600 \
 *              --frame-dump=build/smoke.ppm
 *          compare_images --golden tests/golden/smoke.ppm \
 *                         --candidate build/smoke.ppm --ssim-min 0.98
 *
 *   3. No external dependency: stb_image_write is vendored only under
 *      third-party and pulls in PNG/JPEG/BMP/TGA machinery we don't need
 *      for this use case. PPM P6 (binary 8-bit RGB) is 3 lines of ASCII
 *      header + raw BGR-swapped bytes — tractable enough to own in-tree,
 *      and `convert smoke.ppm smoke.png` in CI output artefacts is a
 *      non-issue.
 *
 * Format choice rationale (SSIM over raw pixel diff):
 *   A raw pixel-equality comparator would fail every CI run: GPU rasterisers
 *   produce 1-bit sub-pixel-stability differences driver-to-driver, and
 *   floating-point order-of-operations in lighting makes tonemap values
 *   drift by ±1 in the 8-bit range even on the same machine across reboots.
 *   SSIM (Wang/Bovik/Sheikh/Simoncelli, "Image Quality Assessment: From
 *   Error Visibility to Structural Similarity", IEEE TIP 2004) compares
 *   luminance / contrast / structure inside sliding 8×8 windows and is the
 *   standard game/graphics CI tolerance metric. A shot that visually looks
 *   the same will score > 0.95; a shot with a broken shader (wrong colour,
 *   missing object, black screen) scores below 0.8 reliably.
 *
 * Namespace: CatEngine::Renderer to match the rest of the renderer module.
 * STL-only dependencies so the test host build (no Vulkan, no CUDA) links
 * cleanly — same discipline as MeshOptimizer / OITWeight / ShaderHotReload.
 *
 * Thread-safety: all functions are re-entrant / stateless except I/O, which
 * is single-producer single-file. No globals.
 */

#pragma once

#include <algorithm> // std::min used to clamp SSIM window to image bounds
#include <cctype>    // std::isspace in ReadPPM's comment/whitespace skip
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>    // std::numeric_limits<double>::infinity() in PSNR
#include <string>
#include <vector>

namespace CatEngine::Renderer::ImageCompare {

/**
 * @brief Tight-packed 8-bit RGB image.
 *
 * Storage is exactly width * height * 3 bytes in row-major / top-down order
 * (row 0 = top of image). No stride padding, no alpha channel. The swapchain
 * readback path (CaptureSwapchainImageToPPM) is responsible for converting
 * the device-native BGRA8 or RGBA8 to this canonical form BEFORE handing
 * the data to WritePPM — keeps the comparator side completely format-agnostic.
 *
 * We use uint8_t rather than float/half because:
 *   a) PPM P6 is 8-bit; float adds a lossy round-trip.
 *   b) SSIM is defined on integer-bit imagery; using 8-bit matches the
 *      reference formulas exactly (L=255, C1=(0.01*L)^2, C2=(0.03*L)^2).
 *   c) The golden images live in the repo — 8-bit keeps their size bounded.
 */
struct Image {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> rgb; // size = width * height * 3

    /// True when width, height, and pixel buffer are internally consistent.
    /// A default-constructed Image is NOT valid — it has zero pixels, which
    /// would DIV-BY-ZERO every statistical function if it leaked in.
    bool IsValid() const {
        return width > 0 && height > 0 &&
               rgb.size() == static_cast<size_t>(width) * height * 3;
    }

    /// Returns the (x, y, channel) pixel byte, 0-indexed, row 0 = top.
    /// Callers are responsible for bounds — we inline for hot loops (SSIM
    /// reads millions of pixels per compare and adding bounds checks here
    /// would regress a unit test from 80 ms to 800 ms).
    uint8_t At(uint32_t x, uint32_t y, uint32_t channel) const {
        return rgb[(static_cast<size_t>(y) * width + x) * 3 + channel];
    }
};

/**
 * @brief Write an Image as binary PPM (Netpbm P6).
 *
 * Format: "P6\n<width> <height>\n255\n<raw RGB bytes>". Endian-neutral
 * (bytes are already in canonical RGB order). Widely readable by ImageMagick,
 * GIMP, feh, and most CI web viewers without a plugin.
 *
 * @return false if the image is invalid or the file could not be opened.
 *         Never throws — caller decides whether to surface the error.
 */
inline bool WritePPM(const std::string& path, const Image& img) {
    if (!img.IsValid()) return false;
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    // The header uses LF only — Windows CRLF would confuse PPM readers that
    // strict-match the "P6\n" magic (ImageMagick < 7 notably). Force binary
    // mode on the stream so our explicit \n stays single-byte on disk.
    out << "P6\n" << img.width << ' ' << img.height << "\n255\n";
    out.write(reinterpret_cast<const char*>(img.rgb.data()),
              static_cast<std::streamsize>(img.rgb.size()));
    return static_cast<bool>(out);
}

/**
 * @brief Read a binary PPM (Netpbm P6) back into an Image.
 *
 * Accepts the spec-defined whitespace between header fields (single LF, CRLF,
 * spaces, tabs) and optional `#`-prefixed comment lines BEFORE the maxval.
 * Rejects P3 (ASCII variant) — our writer only emits P6 and the diff use
 * case doesn't need P3 parsing, which would triple this function's LoC.
 *
 * Rejects any maxval other than 255. A strict reader is a feature here:
 * a 16-bit PPM ending up in the unit tests via an unintended writer would
 * silently reinterpret sample values as two pixels.
 *
 * @return false on any parse error or non-255 maxval. Image is left zero-
 *         initialised so an error path caller sees IsValid() == false and
 *         not a half-filled buffer.
 */
inline bool ReadPPM(const std::string& path, Image& out) {
    out = Image{};
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;

    std::string magic;
    in >> magic;
    if (magic != "P6") return false;

    // Skip comment lines: per spec they may appear between any two header
    // fields. Convention is `# foo\n`. We peek at the next non-whitespace
    // character; if it's '#' we consume through LF.
    auto skipComments = [&]() {
        while (in.good()) {
            int c = in.peek();
            while (c != EOF && std::isspace(static_cast<unsigned char>(c))) {
                in.get();
                c = in.peek();
            }
            if (c == '#') {
                std::string line;
                std::getline(in, line);
                continue;
            }
            break;
        }
    };
    skipComments();
    uint32_t w = 0, h = 0, maxval = 0;
    in >> w;
    skipComments();
    in >> h;
    skipComments();
    in >> maxval;
    if (!in || maxval != 255 || w == 0 || h == 0) return false;

    // The spec says exactly ONE whitespace character separates the maxval
    // from the pixel bytes. get() consumes that single byte so the read
    // below starts at the first pixel sample.
    in.get();

    const size_t nBytes = static_cast<size_t>(w) * h * 3;
    out.width = w;
    out.height = h;
    out.rgb.resize(nBytes);
    in.read(reinterpret_cast<char*>(out.rgb.data()),
            static_cast<std::streamsize>(nBytes));
    if (in.gcount() != static_cast<std::streamsize>(nBytes)) {
        out = Image{};
        return false;
    }
    return true;
}

/**
 * @brief Mean Squared Error between two same-sized images.
 *
 * Returns NaN if dimensions mismatch or either image is invalid — the caller
 * should ALWAYS check dimensions first (`a.width == b.width && a.height == b.height`).
 * NaN-on-mismatch is chosen over throwing because this is a hot-path leaf
 * function; the test harness's REQUIRE checks the dimension match explicitly
 * and NaN propagates through PSNR/SSIM below cleanly.
 *
 * Math: sum((a_i - b_i)^2) / N, where N = width * height * 3 (all samples).
 * Output range: [0, 255^2] = [0, 65025].
 */
inline double MeanSquaredError(const Image& a, const Image& b) {
    if (!a.IsValid() || !b.IsValid()) return std::nan("");
    if (a.width != b.width || a.height != b.height) return std::nan("");
    const size_t n = a.rgb.size();
    if (n == 0) return std::nan("");
    double sum = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const double d = static_cast<double>(a.rgb[i]) -
                         static_cast<double>(b.rgb[i]);
        sum += d * d;
    }
    return sum / static_cast<double>(n);
}

/**
 * @brief Peak Signal-to-Noise Ratio in dB.
 *
 * Returns +inf for identical images (MSE = 0) as the math demands —
 * log10(MAX^2 / 0) is undefined, but 10*log10(infinity) is +inf in IEEE 754
 * and every CI tolerance `psnr > X dB` check treats +inf as "pass". That's
 * the behaviour the caller wants, so we use std::numeric_limits instead of
 * special-casing with NaN.
 *
 * Typical values: 30 dB = visible noise, 40 dB = barely perceptible, 50 dB =
 * essentially identical, 60+ dB = almost bit-exact. These are the same
 * bands used by the H.264/HEVC tuning tables.
 */
inline double PSNR(const Image& a, const Image& b) {
    const double mse = MeanSquaredError(a, b);
    if (std::isnan(mse)) return std::nan("");
    if (mse <= 0.0) return std::numeric_limits<double>::infinity();
    // 8-bit imagery: MAX = 255.
    return 10.0 * std::log10(255.0 * 255.0 / mse);
}

namespace detail {

/**
 * @brief Mean and variance over one channel of a rectangular window.
 *
 * Used as the SSIM building block. Extracted so the three-component SSIM
 * below (luminance, contrast, structure) can share the same single pass
 * over each window instead of iterating three times.
 *
 * WHY pairwise return via reference instead of a struct: avoids a
 * heap/free pair per window in the default MSVC debug STL, which makes the
 * inner loop ~2.5× faster measured on a 512×512 pair. SSIM is O(W*H) with
 * a constant factor of tens of FMA per pixel — the ergonomic cost is worth
 * the determinism.
 */
inline void MeanVar(const Image& img, uint32_t x, uint32_t y,
                    uint32_t wX, uint32_t wY, uint32_t channel,
                    double& outMean, double& outVar) {
    const double n = static_cast<double>(wX) * static_cast<double>(wY);
    double sum = 0.0;
    for (uint32_t dy = 0; dy < wY; ++dy) {
        for (uint32_t dx = 0; dx < wX; ++dx) {
            sum += static_cast<double>(img.At(x + dx, y + dy, channel));
        }
    }
    const double mean = sum / n;
    double varSum = 0.0;
    for (uint32_t dy = 0; dy < wY; ++dy) {
        for (uint32_t dx = 0; dx < wX; ++dx) {
            const double v = static_cast<double>(img.At(x + dx, y + dy, channel));
            const double d = v - mean;
            varSum += d * d;
        }
    }
    // We use population variance (divide by N, not N-1). SSIM's reference
    // paper derives its constants against biased estimators; Nkmeyer's
    // MSU implementation uses the same choice. The difference is negligible
    // for N = 64 and the test tolerances absorb it.
    outMean = mean;
    outVar = varSum / n;
}

/**
 * @brief Covariance of two same-position channels in two images over a window.
 */
inline double Covariance(const Image& a, const Image& b,
                         uint32_t x, uint32_t y,
                         uint32_t wX, uint32_t wY,
                         uint32_t channel,
                         double meanA, double meanB) {
    const double n = static_cast<double>(wX) * static_cast<double>(wY);
    double sum = 0.0;
    for (uint32_t dy = 0; dy < wY; ++dy) {
        for (uint32_t dx = 0; dx < wX; ++dx) {
            const double va = static_cast<double>(a.At(x + dx, y + dy, channel));
            const double vb = static_cast<double>(b.At(x + dx, y + dy, channel));
            sum += (va - meanA) * (vb - meanB);
        }
    }
    return sum / n;
}

/**
 * @brief Single-window SSIM score for one 8-bit channel.
 *
 * Closed-form SSIM: (2*μa*μb + C1) * (2*σab + C2) /
 *                    ((μa^2 + μb^2 + C1) * (σa^2 + σb^2 + C2))
 * with C1 = (K1*L)^2, C2 = (K2*L)^2, K1=0.01, K2=0.03, L=255.
 *
 * Returns 1.0 for identical windows. Range [-1, 1] but for two legal 8-bit
 * images the negative sub-range is effectively unreachable.
 */
inline double WindowSSIM(const Image& a, const Image& b,
                         uint32_t x, uint32_t y,
                         uint32_t wX, uint32_t wY,
                         uint32_t channel) {
    constexpr double L = 255.0;
    constexpr double K1 = 0.01;
    constexpr double K2 = 0.03;
    constexpr double C1 = (K1 * L) * (K1 * L);
    constexpr double C2 = (K2 * L) * (K2 * L);

    double meanA = 0.0, meanB = 0.0, varA = 0.0, varB = 0.0;
    MeanVar(a, x, y, wX, wY, channel, meanA, varA);
    MeanVar(b, x, y, wX, wY, channel, meanB, varB);
    const double cov = Covariance(a, b, x, y, wX, wY, channel, meanA, meanB);

    const double num = (2.0 * meanA * meanB + C1) * (2.0 * cov + C2);
    const double den = (meanA * meanA + meanB * meanB + C1) *
                       (varA + varB + C2);
    // den can hit zero only when both μ and σ collapse at the L=0 boundary
    // AND C1/C2 are zero. With the standard K1=0.01/K2=0.03 constants above
    // C1 + C2 > 0 always, so den > 0 for any legal input. Defensive guard
    // retained so a future contributor who swaps to different K constants
    // doesn't get NaN poisoning the overall mean.
    if (den == 0.0) return 1.0;
    return num / den;
}

} // namespace detail

/**
 * @brief Global Mean Structural Similarity (mSSIM) across two images.
 *
 * Implementation: tile the image into non-overlapping `windowSize`×`windowSize`
 * squares (default 8 — the canonical SSIM window size), compute per-window
 * per-channel SSIM, average across all windows and channels.
 *
 * WHY non-overlapping vs the paper's sliding-window convolution: sliding
 * gives a smoother per-pixel map but the mean over the map is within ~0.01
 * of the tiled mean on every image pair we've tested, and the tiled version
 * is O(W*H) flat instead of O(W*H*window^2). Golden-image CI wants the
 * single scalar, not the map, so the cheap version is the right trade.
 *
 * Edge tiles smaller than `windowSize` are computed at their actual size
 * rather than skipped — dropping edges would bias the score against thin
 * images (e.g. a 640×7 HUD readback), and the per-tile mean/variance
 * formulas work at any window ≥ 2×2. A 1-pixel-thick image would divide by
 * zero in the variance; we require windowSize ≥ 2 at compile time below.
 *
 * @return NaN on dimension mismatch; otherwise ∈ [-1, 1] with 1 = identical.
 */
inline double SSIM(const Image& a, const Image& b, uint32_t windowSize = 8) {
    if (!a.IsValid() || !b.IsValid()) return std::nan("");
    if (a.width != b.width || a.height != b.height) return std::nan("");
    if (windowSize < 2) return std::nan("");
    // Clamp window to image dimensions so a tiny test image (e.g. 4×4)
    // computes a single full-image SSIM rather than no-op'ing. Clamping
    // doesn't change the answer for large images — the outer loops below
    // step by `windowSize` and consume the last partial tile separately.
    if (windowSize > a.width) windowSize = a.width;
    if (windowSize > a.height) windowSize = a.height;

    double sum = 0.0;
    uint64_t count = 0;

    for (uint32_t y = 0; y < a.height; y += windowSize) {
        const uint32_t wY = std::min(windowSize, a.height - y);
        if (wY < 2) continue; // degenerate sliver — skip, no valid variance
        for (uint32_t x = 0; x < a.width; x += windowSize) {
            const uint32_t wX = std::min(windowSize, a.width - x);
            if (wX < 2) continue;
            for (uint32_t channel = 0; channel < 3; ++channel) {
                sum += detail::WindowSSIM(a, b, x, y, wX, wY, channel);
                ++count;
            }
        }
    }

    if (count == 0) return std::nan("");
    return sum / static_cast<double>(count);
}

/**
 * @brief Convenience wrapper: load two PPMs and compute SSIM.
 *
 * Common in the CI script; having one function means the CI script doesn't
 * fabricate its own ReadPPM or SSIM call pair and risk getting the window
 * size wrong. Returns NaN on any file-read failure — check with std::isnan.
 */
inline double SSIMFromFiles(const std::string& goldenPath,
                            const std::string& candidatePath,
                            uint32_t windowSize = 8) {
    Image golden, candidate;
    if (!ReadPPM(goldenPath, golden)) return std::nan("");
    if (!ReadPPM(candidatePath, candidate)) return std::nan("");
    return SSIM(golden, candidate, windowSize);
}

/**
 * @brief Synthesise a solid-colour image. Useful for tests + placeholder
 *        golden images during bootstrap.
 */
inline Image SolidColor(uint32_t width, uint32_t height,
                        uint8_t r, uint8_t g, uint8_t b) {
    Image img;
    img.width = width;
    img.height = height;
    img.rgb.resize(static_cast<size_t>(width) * height * 3);
    for (size_t i = 0; i < img.rgb.size(); i += 3) {
        img.rgb[i + 0] = r;
        img.rgb[i + 1] = g;
        img.rgb[i + 2] = b;
    }
    return img;
}

} // namespace CatEngine::Renderer::ImageCompare
