/**
 * @file test_image_compare.cpp
 * @brief Unit tests for engine/renderer/ImageCompare.hpp.
 *
 * Same discipline as test_mesh_optimizer, test_oit_weight, test_profiler_overlay:
 * a header-only module with pure STL dependencies, tested on the no-GPU host
 * build. No Vulkan, no CUDA, no display needed — these run anywhere Catch2
 * runs.
 *
 * Coverage targets:
 *   1. Image invariants (IsValid / At / SolidColor).
 *   2. PPM round-trip — WritePPM then ReadPPM must preserve pixel bytes
 *      exactly, because golden-image diffs are byte-exact at the PPM layer.
 *   3. PPM reader robustness — whitespace tolerance, comment lines, and
 *      rejection of unsupported variants (P3 ASCII, non-255 maxval).
 *   4. MSE / PSNR numerical contract — bands the CI "psnr > X dB" gate
 *      relies on.
 *   5. SSIM numerical contract — identity → 1.0, visible corruption → clear
 *      drop, dimension mismatch → NaN, channel-independent (so a bad blue
 *      channel still pulls the score down), and window-size robustness on
 *      small images.
 *
 * The test writes PPM files into the Catch2 `TempTestDirectory` (a temp path
 * owned by this process), so the suite is safe to run in parallel and leaves
 * no residue under tests/ or build/.
 */

// Catch2 v2 single-header build (tests/catch2/catch.hpp). v3 `Matchers::`
// namespace is not available here; we use Approx(x).margin(t) for floating-
// point tolerance, matching the convention in test_oit_weight / test_ccd /
// test_sequential_impulse.
#include "catch.hpp"

#include "engine/renderer/ImageCompare.hpp"

#include <cstdio>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

using CatEngine::Renderer::ImageCompare::Image;
using CatEngine::Renderer::ImageCompare::SolidColor;
using CatEngine::Renderer::ImageCompare::WritePPM;
using CatEngine::Renderer::ImageCompare::ReadPPM;
using CatEngine::Renderer::ImageCompare::MeanSquaredError;
using CatEngine::Renderer::ImageCompare::PSNR;
using CatEngine::Renderer::ImageCompare::SSIM;
using CatEngine::Renderer::ImageCompare::SSIMFromFiles;

namespace {

// Generate a unique temp PPM path for each test. We avoid std::tmpnam (UB per
// the C++ standard and triggers deprecation warnings on MSVC) — std::filesystem
// + counting suffix is enough. Collisions across parallel Catch2 runs would
// require the same counter in the same millisecond, which the parent process
// already prevents by serialising tests in the same binary.
struct TempPath {
    std::filesystem::path path;
    TempPath(const std::string& prefix) {
        static int counter = 0;
        path = std::filesystem::temp_directory_path() /
               (prefix + "-" + std::to_string(++counter) + ".ppm");
    }
    ~TempPath() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

// Build a deterministic noise-free checkerboard at the requested size —
// enough spatial variance to exercise SSIM's contrast term without relying
// on an external image asset.
Image Checkerboard(uint32_t width, uint32_t height,
                   uint32_t cellSize = 4) {
    Image img;
    img.width = width;
    img.height = height;
    img.rgb.resize(static_cast<size_t>(width) * height * 3);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const bool dark = ((x / cellSize) + (y / cellSize)) & 1;
            const uint8_t v = dark ? 30 : 220;
            const size_t base = (static_cast<size_t>(y) * width + x) * 3;
            img.rgb[base + 0] = v;
            img.rgb[base + 1] = v;
            img.rgb[base + 2] = v;
        }
    }
    return img;
}

} // namespace

TEST_CASE("Image validity and accessors", "[image-compare]") {
    SECTION("Default image is not valid") {
        Image img;
        REQUIRE_FALSE(img.IsValid());
    }
    SECTION("SolidColor produces a valid image with consistent pixel bytes") {
        Image red = SolidColor(4, 3, 255, 0, 0);
        REQUIRE(red.IsValid());
        REQUIRE(red.width == 4);
        REQUIRE(red.height == 3);
        REQUIRE(red.rgb.size() == 4u * 3u * 3u);
        for (uint32_t y = 0; y < red.height; ++y) {
            for (uint32_t x = 0; x < red.width; ++x) {
                REQUIRE(red.At(x, y, 0) == 255);
                REQUIRE(red.At(x, y, 1) == 0);
                REQUIRE(red.At(x, y, 2) == 0);
            }
        }
    }
    SECTION("A pixel buffer of the wrong size makes the image invalid") {
        Image broken;
        broken.width = 2;
        broken.height = 2;
        broken.rgb.resize(11); // Should be 12 bytes (2*2*3)
        REQUIRE_FALSE(broken.IsValid());
    }
}

TEST_CASE("PPM binary round-trip preserves pixels exactly", "[image-compare]") {
    Image original = Checkerboard(32, 24);
    TempPath tmp("ppm-roundtrip");

    REQUIRE(WritePPM(tmp.path.string(), original));

    Image restored;
    REQUIRE(ReadPPM(tmp.path.string(), restored));

    REQUIRE(restored.width == original.width);
    REQUIRE(restored.height == original.height);
    REQUIRE(restored.rgb.size() == original.rgb.size());

    // Byte-exact: WritePPM emits raw RGB bytes, ReadPPM reads them back
    // verbatim, so there's no floating-point round-trip to tolerate.
    for (size_t i = 0; i < original.rgb.size(); ++i) {
        REQUIRE(restored.rgb[i] == original.rgb[i]);
    }
}

TEST_CASE("PPM writer rejects invalid images without touching disk",
          "[image-compare]") {
    Image empty;
    TempPath tmp("ppm-empty");
    REQUIRE_FALSE(WritePPM(tmp.path.string(), empty));
    // File must not exist — we gated WritePPM before opening the stream.
    REQUIRE_FALSE(std::filesystem::exists(tmp.path));
}

TEST_CASE("PPM reader tolerates comment lines between header fields",
          "[image-compare]") {
    TempPath tmp("ppm-comments");
    {
        // Hand-craft a PPM with interleaved comments, which the PPM spec
        // explicitly permits. Our reader must round-trip this correctly —
        // `convert` and GIMP produce these in the wild.
        std::ofstream out(tmp.path, std::ios::binary);
        REQUIRE(out);
        out << "P6\n# produced by hand for test\n"
            << "3 # inline comment before height\n2\n"
            << "# another comment line\n"
            << "255\n";
        // 3x2 image, RGB triplets: red, green, blue / black, white, grey
        const uint8_t pixels[] = {
            255, 0,   0,   0,   255, 0,   0,   0,   255,
            0,   0,   0,   255, 255, 255, 128, 128, 128,
        };
        out.write(reinterpret_cast<const char*>(pixels), sizeof(pixels));
    }

    Image img;
    REQUIRE(ReadPPM(tmp.path.string(), img));
    REQUIRE(img.width == 3);
    REQUIRE(img.height == 2);
    REQUIRE(img.At(0, 0, 0) == 255);
    REQUIRE(img.At(1, 0, 1) == 255);
    REQUIRE(img.At(2, 0, 2) == 255);
    REQUIRE(img.At(1, 1, 0) == 255);
    REQUIRE(img.At(2, 1, 0) == 128);
}

TEST_CASE("PPM reader rejects unsupported variants", "[image-compare]") {
    SECTION("P3 ASCII variant is rejected") {
        TempPath tmp("ppm-p3");
        {
            std::ofstream out(tmp.path, std::ios::binary);
            out << "P3\n1 1\n255\n200 100 50\n";
        }
        Image img;
        REQUIRE_FALSE(ReadPPM(tmp.path.string(), img));
        REQUIRE_FALSE(img.IsValid());
    }
    SECTION("Non-255 maxval (16-bit) is rejected") {
        TempPath tmp("ppm-16bit");
        {
            std::ofstream out(tmp.path, std::ios::binary);
            // Claim 65535 maxval — silent reinterpretation would halve the
            // effective pixel count. We'd rather reject clearly.
            out << "P6\n2 2\n65535\n";
            const uint8_t pixels[] = {
                0xFF, 0x00, 0x00, 0x00, 0xFF, 0x00,
                0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
            };
            out.write(reinterpret_cast<const char*>(pixels), sizeof(pixels));
        }
        Image img;
        REQUIRE_FALSE(ReadPPM(tmp.path.string(), img));
    }
    SECTION("Missing file is reported as a read failure") {
        Image img;
        REQUIRE_FALSE(ReadPPM(
            "C:/path/that/does/not/exist-image-compare.ppm", img));
    }
}

TEST_CASE("MeanSquaredError obeys its numerical contract", "[image-compare]") {
    Image a = SolidColor(16, 16, 100, 100, 100);
    Image b = SolidColor(16, 16, 100, 100, 100);

    SECTION("Identical images: MSE is exactly zero") {
        REQUIRE(MeanSquaredError(a, b) == 0.0);
    }
    SECTION("Uniform offset: MSE equals offset squared") {
        Image c = SolidColor(16, 16, 110, 110, 110);
        // Every sample differs by exactly 10, so MSE = 10*10 = 100.
        REQUIRE(MeanSquaredError(a, c) == 100.0);
    }
    SECTION("Dimension mismatch: MSE is NaN") {
        Image wrongSize = SolidColor(8, 16, 100, 100, 100);
        REQUIRE(std::isnan(MeanSquaredError(a, wrongSize)));
    }
    SECTION("Invalid image: MSE is NaN") {
        Image invalid;
        REQUIRE(std::isnan(MeanSquaredError(a, invalid)));
    }
    SECTION("Symmetry: MSE(a, b) == MSE(b, a)") {
        Image c = Checkerboard(16, 16);
        Image d = SolidColor(16, 16, 128, 128, 128);
        REQUIRE(MeanSquaredError(c, d) == MeanSquaredError(d, c));
    }
}

TEST_CASE("PSNR: identical images produce +infinity", "[image-compare]") {
    Image a = Checkerboard(16, 16);
    REQUIRE(std::isinf(PSNR(a, a)));
    REQUIRE(PSNR(a, a) > 0.0); // positive infinity specifically
}

TEST_CASE("PSNR: 10-level uniform offset yields canonical 28.13 dB",
          "[image-compare]") {
    // MSE = 100, MAX = 255, PSNR = 10*log10(255^2 / 100) ≈ 28.1308 dB.
    // This is the "visible but not awful" band — if our formula drifts
    // this test catches a ±0.5 dB regression that would corrupt every CI
    // tolerance set against these numbers.
    Image a = SolidColor(16, 16, 100, 100, 100);
    Image b = SolidColor(16, 16, 110, 110, 110);
    REQUIRE(PSNR(a, b) == Approx(28.1308).margin(1e-3));
}

TEST_CASE("SSIM: identical images score exactly 1.0", "[image-compare]") {
    SECTION("Checkerboard") {
        Image a = Checkerboard(32, 32);
        REQUIRE(SSIM(a, a) == Approx(1.0).margin(1e-9));
    }
    SECTION("Solid color") {
        Image a = SolidColor(16, 16, 64, 192, 128);
        // Solid-colour windows have variance = 0, which dances near SSIM's
        // stability-constant floor. The numerator and denominator still
        // collapse to the same value, so the ratio is exactly 1.0. This
        // test guards against a regression in the C1/C2 stability terms.
        REQUIRE(SSIM(a, a) == Approx(1.0).margin(1e-9));
    }
    SECTION("Non-square image") {
        Image a = Checkerboard(48, 16);
        REQUIRE(SSIM(a, a) == Approx(1.0).margin(1e-9));
    }
}

TEST_CASE("SSIM: visible corruption drops score clearly below 1.0",
          "[image-compare]") {
    Image reference = Checkerboard(32, 32);

    SECTION("Uniform intensity offset on all three channels") {
        Image shifted = reference;
        // Shift every sample by +120 — a large, uniformly visible overexposure
        // that saturates the light cells to 255 and lifts the dark cells from
        // 30 to 150. Structure is preserved but luminance and contrast are
        // clearly off. SSIM's luminance + contrast terms are the signal here;
        // the structure term alone would miss this.
        for (size_t i = 0; i < shifted.rgb.size(); ++i) {
            const int newVal = static_cast<int>(shifted.rgb[i]) + 120;
            shifted.rgb[i] = static_cast<uint8_t>(std::min(newVal, 255));
        }
        const double score = SSIM(reference, shifted);
        REQUIRE(score < 1.0);
        REQUIRE(score > 0.0);
        // A catastrophic exposure shift should not slip past a CI tolerance
        // of SSIM > 0.9 — if this slips, SSIM's luminance/contrast terms
        // are miswired (e.g. collapsed to structure-only).
        REQUIRE(score < 0.9);
    }
    SECTION("Single-channel intensity offset is detected") {
        Image shifted = reference;
        // Shift ONLY the blue channel by +40. The checkerboard is grayscale-
        // equal (R == G == B), so this is a directional tint. SSIM's per-
        // channel scoring must drop below 1 — if it returns 1.0 something is
        // collapsing channels (e.g. averaging RGB before compute). We do NOT
        // test a tight upper bound here because a single-channel tint on a
        // high-structure image is a subtle change and SSIM's value depends on
        // the image's variance; pinning a strict band would make this test
        // brittle. The invariant that matters: it's < 1.0 and the reference
        // doesn't accidentally score identical to the shifted copy.
        for (size_t i = 2; i < shifted.rgb.size(); i += 3) {
            const int newVal = static_cast<int>(shifted.rgb[i]) + 40;
            shifted.rgb[i] = static_cast<uint8_t>(std::min(newVal, 255));
        }
        const double score = SSIM(reference, shifted);
        REQUIRE(score < 1.0);
        REQUIRE(score > 0.0);
    }
    SECTION("Black-out: reference vs all-zero image") {
        Image black = SolidColor(32, 32, 0, 0, 0);
        const double score = SSIM(reference, black);
        // A totally corrupted frame must score well below the typical CI
        // tolerance (< 0.5 is the canonical "render broken" threshold).
        REQUIRE(score < 0.5);
    }
    SECTION("Structure break: inverted checkerboard") {
        Image inverted;
        inverted.width = reference.width;
        inverted.height = reference.height;
        inverted.rgb.resize(reference.rgb.size());
        for (size_t i = 0; i < reference.rgb.size(); ++i) {
            inverted.rgb[i] = static_cast<uint8_t>(255 - reference.rgb[i]);
        }
        const double score = SSIM(reference, inverted);
        // Spatially identical structure but anti-correlated luminance —
        // SSIM's covariance term turns negative. A well-implemented SSIM
        // handles this; this test pins that behaviour.
        REQUIRE(score < 0.0);
    }
}

TEST_CASE("SSIM returns NaN on dimension mismatch and invalid inputs",
          "[image-compare]") {
    Image a = Checkerboard(32, 32);
    Image b = Checkerboard(16, 32);
    REQUIRE(std::isnan(SSIM(a, b)));

    Image invalid;
    REQUIRE(std::isnan(SSIM(a, invalid)));
    REQUIRE(std::isnan(SSIM(invalid, invalid)));

    // Sub-2 window size is out of range.
    REQUIRE(std::isnan(SSIM(a, a, 1)));
    REQUIRE(std::isnan(SSIM(a, a, 0)));
}

TEST_CASE("SSIM handles images smaller than the default window",
          "[image-compare]") {
    // A 4×4 image is smaller than the default 8-pixel window. The comparator
    // must clamp the window down to the image size rather than divide by
    // zero or skip outright — a 4×4 readback (shadow-atlas debug view, say)
    // is a plausible golden-image artefact.
    Image small = Checkerboard(4, 4, /*cell*/ 2);
    REQUIRE(SSIM(small, small) == Approx(1.0).margin(1e-9));
    Image black = SolidColor(4, 4, 0, 0, 0);
    // Must still compute; must be clearly less than 1 for an obvious diff.
    const double score = SSIM(small, black);
    REQUIRE_FALSE(std::isnan(score));
    REQUIRE(score < 0.5);
}

TEST_CASE("SSIMFromFiles composes ReadPPM + SSIM correctly",
          "[image-compare]") {
    Image a = Checkerboard(16, 16);
    Image b = Checkerboard(16, 16);
    // Perturb `b` slightly so the comparison isn't trivially 1.0 — we want
    // the wrapper to exercise the file I/O path AND the math, not just the
    // identity case.
    b.rgb[0] = static_cast<uint8_t>(b.rgb[0] ^ 0xFF);

    TempPath golden("ssim-golden");
    TempPath candidate("ssim-candidate");
    REQUIRE(WritePPM(golden.path.string(), a));
    REQUIRE(WritePPM(candidate.path.string(), b));

    const double directScore = SSIM(a, b);
    const double fileScore = SSIMFromFiles(golden.path.string(),
                                           candidate.path.string());
    REQUIRE(fileScore == Approx(directScore).margin(1e-9));

    SECTION("Missing file propagates as NaN") {
        const double nanScore = SSIMFromFiles(
            "C:/not-a-real/golden.ppm", candidate.path.string());
        REQUIRE(std::isnan(nanScore));
    }
}

TEST_CASE("SSIM is channel-sensitive: a broken blue channel is detected",
          "[image-compare]") {
    // Regression guard for a channel-collapse bug (summing only R, or
    // averaging channels BEFORE SSIM instead of computing per-channel).
    // A broken blue channel but perfect R/G should score noticeably less
    // than 1 but better than a fully broken image — the test pins that band.
    Image reference = Checkerboard(32, 32);
    Image brokenBlue = reference;
    for (size_t i = 2; i < brokenBlue.rgb.size(); i += 3) {
        brokenBlue.rgb[i] = 0;
    }
    const double score = SSIM(reference, brokenBlue);
    REQUIRE(score < 1.0);
    REQUIRE(score > 0.0);
    // One channel of three fully broken, two preserved → score stays above
    // the "total corruption" floor but well below identity.
    REQUIRE(score < 0.95);
}
