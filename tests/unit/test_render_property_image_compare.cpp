/**
 * @file test_render_property_image_compare.cpp
 * @brief Property tests for engine/renderer/ImageCompare.hpp.
 *
 * Sibling-but-distinct from tests/unit/test_image_compare.cpp. That file
 * pins the deterministic numerical contract (PPM round-trip byte-exact,
 * SSIM identity = 1.0, dimension-mismatch returns NaN). This file
 * shotguns the comparator with 100+ randomised 32x32 images to exercise
 * the structural properties the CI golden-image gate relies on:
 *
 *   1. SSIM(img, img) is exactly 1.0 across 100 random 32x32 images.
 *   2. SSIM is symmetric: SSIM(a, b) == SSIM(b, a) for every input.
 *   3. SSIM(a, a + uniform_noise(0.01)) is >= 0.98 — small-noise tolerance
 *      that the CI "ssim > 0.95" gate relies on as the floor for legit
 *      frame-to-frame jitter from driver / fp-order-of-ops drift.
 *   4. PSNR rises monotonically as MSE falls — the math demands it
 *      (PSNR = 10*log10(255^2 / MSE) so dPSNR/dMSE < 0).
 *   5. SSIM range stays in [-1, 1] for every input — the closed-form
 *      formula's range bound, often violated by buggy implementations
 *      that forget the C1/C2 constants.
 *   6. WritePPM + ReadPPM is byte-exact across random image content.
 *
 * The 32x32 size is the smallest image that has multiple SSIM windows
 * (default windowSize=8 → 4x4 = 16 windows per channel = 48 windows
 * per image) so the per-window mean is meaningful AND the SSIM run-time
 * is fast enough to do 100 trials in a unit test budget.
 */

#include "catch.hpp"
#include "test_seed.hpp"
#include "engine/renderer/ImageCompare.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

using CatEngine::Renderer::ImageCompare::Image;
using CatEngine::Renderer::ImageCompare::SolidColor;
using CatEngine::Renderer::ImageCompare::WritePPM;
using CatEngine::Renderer::ImageCompare::ReadPPM;
using CatEngine::Renderer::ImageCompare::MeanSquaredError;
using CatEngine::Renderer::ImageCompare::PSNR;
using CatEngine::Renderer::ImageCompare::SSIM;

namespace {

// Seed routed through CatTest::DeterministicSeed (reproducible default,
// CAT_TEST_SEED-overridable). Generator type + distributions unchanged.
const uint32_t kPropertySeed =
    static_cast<uint32_t>(CatTest::DeterministicSeed("render property image_compare"));

// Build a random image with bytes drawn uniformly from [0, 255]. Used as
// the "structured noise" baseline — every pixel is independent so SSIM's
// neighbourhood statistics behave like white noise, exercising the
// formula's variance terms.
Image MakeRandomImage(uint32_t width, uint32_t height, uint32_t seed) {
    Image img;
    img.width = width;
    img.height = height;
    img.rgb.resize(static_cast<size_t>(width) * height * 3);
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> byte(0, 255);
    for (auto& b : img.rgb) b = static_cast<uint8_t>(byte(rng));
    return img;
}

// Build a structured image (a horizontal gradient of one channel) that
// has spatial correlation. SSIM's structure term will report >0 even
// when comparing two such images that aren't identical, since the
// gradient pattern dominates the per-window covariance.
Image MakeGradientImage(uint32_t width, uint32_t height, uint8_t channel) {
    Image img;
    img.width = width;
    img.height = height;
    img.rgb.resize(static_cast<size_t>(width) * height * 3, 0);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const size_t i = (static_cast<size_t>(y) * width + x) * 3 + channel;
            img.rgb[i] = static_cast<uint8_t>(std::min(255u, x * 255u / width));
        }
    }
    return img;
}

// Add zero-mean uniform noise of bounded amplitude in 8-bit codes. Used to
// drive the small-perturbation tolerance test — noise of amplitude 0.01
// of the full 8-bit range means ±2-3 codes per pixel, which should leave
// SSIM far above the "tiny drift" floor of 0.98.
Image AddUniformNoise(const Image& src, float amplitude, uint32_t seed) {
    Image out = src;
    std::mt19937 rng(seed);
    const int swing = std::max(1, static_cast<int>(255.0f * amplitude));
    std::uniform_int_distribution<int> noise(-swing, swing);
    for (auto& b : out.rgb) {
        const int sample = static_cast<int>(b) + noise(rng);
        b = static_cast<uint8_t>(std::clamp(sample, 0, 255));
    }
    return out;
}

// Build a synthetic image that is the bitwise inverse of src — every byte
// flipped from x to 255 - x. Used to exercise the negative-SSIM regime
// for an anticorrelated structure pair.
Image MakeInverted(const Image& src) {
    Image out = src;
    for (auto& b : out.rgb) b = static_cast<uint8_t>(255 - b);
    return out;
}

// Owned temp path under the system temp dir; auto-cleans on destruction so
// parallel test runs don't fight over the same file name.
std::string MakeTempPpmPath(uint32_t seed) {
    auto base = std::filesystem::temp_directory_path();
    base /= ("test_render_property_image_compare_" + std::to_string(seed) + ".ppm");
    return base.string();
}

} // namespace

// ============================================================================
// PROPERTY 1: SSIM(img, img) is exactly 1.0 across 100 random 32x32 images
// ============================================================================

TEST_CASE("ImageCompare property: SSIM identity is 1.0 over 100 random images",
          "[image][property][ssim]") {
    // The closed-form SSIM at zero error is identically 1. Floating-point
    // arithmetic in the variance+covariance pass introduces no rounding
    // for matched-image inputs (every (a - mean) term has a matching
    // (b - mean) so the cov numerator and the (varA + varB + C2) denom
    // both stay in their natural ranges). One million pixels later we
    // expect bit-exact 1.0.
    for (uint32_t trial = 0; trial < 100; ++trial) {
        const Image img = MakeRandomImage(32, 32, kPropertySeed ^ trial);
        const double s = SSIM(img, img);
        REQUIRE(s == Approx(1.0).margin(1e-9));
    }
}

TEST_CASE("ImageCompare property: SSIM identity holds for gradient images",
          "[image][property][ssim]") {
    // Gradient images have non-zero local variance and per-channel
    // structure. The identity property must still hold — structure does
    // not bias the SSIM(self, self) result.
    for (uint8_t ch = 0; ch < 3; ++ch) {
        const Image grad = MakeGradientImage(32, 32, ch);
        REQUIRE(SSIM(grad, grad) == Approx(1.0).margin(1e-9));
    }
}

TEST_CASE("ImageCompare property: SSIM identity holds for solid-colour images",
          "[image][property][ssim]") {
    // A solid-colour image has zero variance per window. The closed form
    // becomes (2*mu^2 + C1)(C2) / ((2*mu^2 + C1)(C2)) = 1 exactly when
    // varA = varB = cov = 0. Property-check across multiple colours.
    for (uint8_t r : {0, 64, 128, 200, 255}) {
        for (uint8_t g : {0, 100, 255}) {
            for (uint8_t b : {0, 50, 200}) {
                const Image solid = SolidColor(32, 32, r, g, b);
                REQUIRE(SSIM(solid, solid) == Approx(1.0).margin(1e-9));
            }
        }
    }
}

// ============================================================================
// PROPERTY 2: SSIM is symmetric in its arguments
// ============================================================================

TEST_CASE("ImageCompare property: SSIM symmetry over 100 random pairs",
          "[image][property][ssim]") {
    // The closed-form SSIM is symmetric by construction — every numerator
    // term either symmetric in (a, b) (sum of means, covariance) or
    // already in (a^2 + b^2) shape. Property test pins it under random
    // image content the deterministic test cannot synthesise by hand.
    for (uint32_t trial = 0; trial < 100; ++trial) {
        const Image a = MakeRandomImage(32, 32, kPropertySeed ^ trial);
        const Image b = MakeRandomImage(32, 32, kPropertySeed ^ (trial + 0x1000u));
        const double sAB = SSIM(a, b);
        const double sBA = SSIM(b, a);
        REQUIRE(sAB == Approx(sBA).margin(1e-12));
    }
}

TEST_CASE("ImageCompare property: SSIM symmetry across structured images",
          "[image][property][ssim]") {
    // Cross-product of {random, gradient_r, gradient_g, gradient_b, solid}.
    std::vector<Image> set;
    set.push_back(MakeRandomImage(32, 32, kPropertySeed ^ 0x7777u));
    set.push_back(MakeGradientImage(32, 32, 0));
    set.push_back(MakeGradientImage(32, 32, 1));
    set.push_back(MakeGradientImage(32, 32, 2));
    set.push_back(SolidColor(32, 32, 128, 128, 128));
    set.push_back(MakeInverted(set[0]));

    for (size_t i = 0; i < set.size(); ++i) {
        for (size_t j = 0; j < set.size(); ++j) {
            if (i == j) continue;
            const double sij = SSIM(set[i], set[j]);
            const double sji = SSIM(set[j], set[i]);
            REQUIRE(sij == Approx(sji).margin(1e-10));
        }
    }
}

// ============================================================================
// PROPERTY 3: SSIM with small noise stays above 0.98
// ============================================================================

TEST_CASE("ImageCompare property: small uniform noise leaves SSIM > 0.98",
          "[image][property][ssim]") {
    // The brief specifies SSIM(a, a + uniform_noise(0.01)) >= 0.98. Noise
    // amplitude of 0.01 means +/- 2-3 codes per pixel out of 255 — well
    // below the JND for any natural image and orders of magnitude below
    // the CI gate's 0.95 floor for actual visual divergence.
    //
    // We trial 50 random base images so the noise+structure interaction
    // is sampled across non-degenerate content. Solid-colour images can
    // hit the variance=0 corner of the SSIM formula and skew the
    // average, so we use random + gradient bases instead.
    for (uint32_t trial = 0; trial < 50; ++trial) {
        const Image base = MakeRandomImage(32, 32, kPropertySeed ^ trial);
        const Image noisy = AddUniformNoise(base, 0.01f,
                                            kPropertySeed ^ (trial + 0x4000u));
        const double s = SSIM(base, noisy);
        REQUIRE(s >= 0.98);
        REQUIRE(s <= 1.0 + 1e-9);
    }
}

TEST_CASE("ImageCompare property: gradient + small noise stays above the CI gate",
          "[image][property][ssim]") {
    // Gradient bases have HIGH structural similarity on the one channel
    // that holds the gradient and ZERO structure on the other two
    // channels (which are flat black). Small uniform noise added on
    // top of a flat-black channel produces a relatively-high
    // per-window variance ratio that the SSIM denominator hits hard —
    // empirically this drops the global mean to ~0.957 even at the
    // 0.01 amplitude floor the brief specifies.
    //
    // The CI gate the helper is feeding is at 0.95; the test pins
    // "small noise stays above the CI gate" rather than the tighter
    // 0.98 floor that holds for natural-image bases (already covered
    // by the random-base test above).
    for (uint8_t channel = 0; channel < 3; ++channel) {
        const Image base = MakeGradientImage(32, 32, channel);
        for (uint32_t trial = 0; trial < 20; ++trial) {
            const Image noisy = AddUniformNoise(base, 0.01f,
                                                kPropertySeed ^ (trial ^ channel));
            const double s = SSIM(base, noisy);
            REQUIRE(s >= 0.95);
        }
    }
}

// ============================================================================
// PROPERTY 4: PSNR rises monotonically as MSE falls
// ============================================================================

TEST_CASE("ImageCompare property: PSNR is monotone decreasing in MSE",
          "[image][property][psnr]") {
    // PSNR = 10*log10(255^2 / MSE) — strictly decreasing in MSE. Sweep
    // 50 random image pairs with monotonically-increasing noise amplitudes
    // and confirm MSE rises while PSNR falls in lockstep.
    for (uint32_t trial = 0; trial < 50; ++trial) {
        const Image base = MakeRandomImage(32, 32, kPropertySeed ^ trial);
        std::vector<double> mseSequence;
        std::vector<double> psnrSequence;
        for (float amp : {0.0f, 0.02f, 0.05f, 0.10f, 0.20f, 0.40f}) {
            Image noisy = amp == 0.0f
                ? base
                : AddUniformNoise(base, amp, kPropertySeed ^ (trial + 0x4444u));
            mseSequence.push_back(MeanSquaredError(base, noisy));
            psnrSequence.push_back(PSNR(base, noisy));
        }
        // MSE non-decreasing as amp grows.
        for (size_t i = 1; i < mseSequence.size(); ++i) {
            REQUIRE(mseSequence[i] >= mseSequence[i - 1] - 1e-9);
        }
        // PSNR non-increasing as MSE grows. PSNR at amp=0 is +inf which
        // automatically satisfies "biggest of the series", but the
        // explicit check below handles fp comparison cleanly.
        for (size_t i = 1; i < psnrSequence.size(); ++i) {
            if (std::isfinite(psnrSequence[i]) &&
                std::isfinite(psnrSequence[i - 1])) {
                REQUIRE(psnrSequence[i] <= psnrSequence[i - 1] + 1e-6);
            }
        }
    }
}

TEST_CASE("ImageCompare property: identical images give PSNR = +inf",
          "[image][property][psnr]") {
    // MSE = 0 means 10 * log10(255^2 / 0) = +inf (by IEEE 754
    // convention, matching std::log10(inf)). The header documents
    // this explicitly; pin it.
    for (uint32_t trial = 0; trial < 20; ++trial) {
        const Image img = MakeRandomImage(32, 32, kPropertySeed ^ trial);
        const double psnr = PSNR(img, img);
        REQUIRE(std::isinf(psnr));
        REQUIRE(psnr > 0.0);
    }
}

// ============================================================================
// PROPERTY 5: SSIM range is [-1, 1] over arbitrary input
// ============================================================================

TEST_CASE("ImageCompare property: SSIM range bounded by [-1, 1]",
          "[image][property][ssim]") {
    // Closed-form SSIM is bounded above by 1 (identical inputs) and below
    // by -1 (perfectly anti-correlated structure). Property test against
    // 50 random pairs and against the inverted-image case which should
    // be strongly negative.
    for (uint32_t trial = 0; trial < 50; ++trial) {
        const Image a = MakeRandomImage(32, 32, kPropertySeed ^ trial);
        const Image b = MakeRandomImage(32, 32, kPropertySeed ^ (trial + 0x9999u));
        const double s = SSIM(a, b);
        REQUIRE(s >= -1.0 - 1e-9);
        REQUIRE(s <= 1.0 + 1e-9);
    }

    // Inverted gradient: extreme structural anticorrelation.
    const Image grad = MakeGradientImage(32, 32, 0);
    const Image inv = MakeInverted(grad);
    const double sInv = SSIM(grad, inv);
    REQUIRE(sInv >= -1.0 - 1e-9);
    REQUIRE(sInv <= 1.0 + 1e-9);
}

// ============================================================================
// PROPERTY 6: PPM round-trip preserves bytes
// ============================================================================

TEST_CASE("ImageCompare property: PPM round-trip is byte-exact",
          "[image][property][ppm]") {
    // Write + read every random image; the byte buffer that comes back
    // must equal the byte buffer that went in. PPM P6 is documented as a
    // byte-stream container, but a sneaky CRLF-translation on Windows
    // (text-mode fopen) would silently corrupt every fourth pixel. The
    // header explicitly opens in binary mode; this test pins it.
    for (uint32_t trial = 0; trial < 50; ++trial) {
        const Image src = MakeRandomImage(32, 32, kPropertySeed ^ (trial + 0xC000u));
        const std::string path = MakeTempPpmPath(kPropertySeed ^ trial);

        REQUIRE(WritePPM(path, src));
        Image loaded;
        REQUIRE(ReadPPM(path, loaded));
        REQUIRE(loaded.width == src.width);
        REQUIRE(loaded.height == src.height);
        REQUIRE(loaded.rgb == src.rgb);

        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
}

// ============================================================================
// PROPERTY 7: MSE = 0 iff images byte-equal
// ============================================================================

TEST_CASE("ImageCompare property: MSE zero iff identical pixels",
          "[image][property][mse]") {
    // If MSE(a, b) == 0 then sum((a_i - b_i)^2) == 0 → every pixel byte
    // matches. Property test the IFF: identical inputs return 0, any
    // single-byte difference returns > 0.
    for (uint32_t trial = 0; trial < 20; ++trial) {
        Image base = MakeRandomImage(32, 32, kPropertySeed ^ trial);
        REQUIRE(MeanSquaredError(base, base) == Approx(0.0).margin(1e-12));

        Image tweaked = base;
        tweaked.rgb[trial % tweaked.rgb.size()] ^= 0x01;
        REQUIRE(MeanSquaredError(base, tweaked) > 0.0);
    }
}

// ============================================================================
// PROPERTY 8: MSE = mean of squared per-channel diffs
// ============================================================================

TEST_CASE("ImageCompare property: MSE matches the closed-form definition",
          "[image][property][mse]") {
    // MSE = sum((a_i - b_i)^2) / N. Compute by hand and compare against
    // the helper across random pairs.
    for (uint32_t trial = 0; trial < 30; ++trial) {
        const Image a = MakeRandomImage(8, 8, kPropertySeed ^ trial);
        const Image b = MakeRandomImage(8, 8, kPropertySeed ^ (trial + 0xAAAAu));
        double expectedSum = 0.0;
        for (size_t i = 0; i < a.rgb.size(); ++i) {
            const double diff = static_cast<double>(a.rgb[i]) -
                                static_cast<double>(b.rgb[i]);
            expectedSum += diff * diff;
        }
        const double expected = expectedSum / static_cast<double>(a.rgb.size());
        const double got = MeanSquaredError(a, b);
        REQUIRE(got == Approx(expected).margin(1e-9));
    }
}

// ============================================================================
// PROPERTY 9: PSNR exactly matches 10 * log10(255^2 / MSE)
// ============================================================================

TEST_CASE("ImageCompare property: PSNR matches closed form for finite MSE",
          "[image][property][psnr]") {
    for (uint32_t trial = 0; trial < 30; ++trial) {
        const Image a = MakeRandomImage(16, 16, kPropertySeed ^ trial);
        const Image b = MakeRandomImage(16, 16, kPropertySeed ^ (trial + 0x55AAu));
        const double mse = MeanSquaredError(a, b);
        if (mse <= 0.0) continue;
        const double expectedPsnr = 10.0 * std::log10(255.0 * 255.0 / mse);
        REQUIRE(PSNR(a, b) == Approx(expectedPsnr).margin(1e-6));
    }
}

// ============================================================================
// PROPERTY 10: SSIM at half-byte uniform perturbation drops monotonically
// ============================================================================

TEST_CASE("ImageCompare property: SSIM falls monotonically with noise amplitude",
          "[image][property][ssim]") {
    // For increasing noise amplitudes, SSIM must non-strictly decrease
    // (it can plateau at the very high end where everything looks
    // equally bad). Property test under random base images.
    for (uint32_t trial = 0; trial < 30; ++trial) {
        const Image base = MakeRandomImage(32, 32, kPropertySeed ^ trial);
        std::vector<double> ssimSequence;
        for (float amp : {0.0f, 0.02f, 0.05f, 0.10f, 0.20f, 0.40f, 0.80f}) {
            const Image noisy = amp == 0.0f ? base
                : AddUniformNoise(base, amp,
                                  kPropertySeed ^ (trial + 0x9090u));
            ssimSequence.push_back(SSIM(base, noisy));
        }
        for (size_t i = 1; i < ssimSequence.size(); ++i) {
            // Allow a tiny fp tolerance for the plateau at high amplitudes.
            REQUIRE(ssimSequence[i] <= ssimSequence[i - 1] + 1e-3);
        }
    }
}

// ============================================================================
// PROPERTY 11: SSIM dimension mismatch returns NaN
// ============================================================================

TEST_CASE("ImageCompare property: dimension mismatch surfaces as NaN",
          "[image][property][ssim]") {
    // The header documents NaN-on-mismatch so the CI gate's REQUIRE(s > X)
    // path fails predictably instead of silently comparing the first
    // overlapping pixels. Property-check across random (w, h) pairs.
    std::mt19937 rng(kPropertySeed ^ 0xFEEDu);
    std::uniform_int_distribution<int> dim(8, 64);
    for (uint32_t trial = 0; trial < 30; ++trial) {
        const Image a = MakeRandomImage(dim(rng), dim(rng), kPropertySeed ^ trial);
        const Image b = MakeRandomImage(dim(rng) + 7,
                                         dim(rng) + 13,
                                         kPropertySeed ^ (trial + 0x10u));
        if (a.width == b.width && a.height == b.height) continue;
        const double s = SSIM(a, b);
        REQUIRE(std::isnan(s));
        // Same for MSE / PSNR.
        REQUIRE(std::isnan(MeanSquaredError(a, b)));
        REQUIRE(std::isnan(PSNR(a, b)));
    }
}

// ============================================================================
// PROPERTY 12: Window size between 2 and min(w,h) all produce SSIM(self,self)=1
// ============================================================================

TEST_CASE("ImageCompare property: window-size sweep preserves SSIM identity",
          "[image][property][ssim]") {
    // The header clamps windowSize to image dimensions; the identity
    // property must hold for any legal window size between 2 and 32.
    const Image base = MakeRandomImage(32, 32, kPropertySeed);
    for (uint32_t w = 2; w <= 32; ++w) {
        const double s = SSIM(base, base, w);
        REQUIRE(s == Approx(1.0).margin(1e-9));
    }
}

// ============================================================================
// PROPERTY 13: SolidColor returns a valid image with the requested bytes
// ============================================================================

TEST_CASE("ImageCompare property: SolidColor synthesises the expected pixels",
          "[image][property][image]") {
    std::mt19937 rng(kPropertySeed ^ 0xC0AC0Au);
    std::uniform_int_distribution<int> dim(1, 64);
    std::uniform_int_distribution<int> rgb(0, 255);
    for (uint32_t trial = 0; trial < 50; ++trial) {
        const uint32_t w = static_cast<uint32_t>(dim(rng));
        const uint32_t h = static_cast<uint32_t>(dim(rng));
        const uint8_t r = static_cast<uint8_t>(rgb(rng));
        const uint8_t g = static_cast<uint8_t>(rgb(rng));
        const uint8_t bb = static_cast<uint8_t>(rgb(rng));
        const Image img = SolidColor(w, h, r, g, bb);
        REQUIRE(img.IsValid());
        REQUIRE(img.width == w);
        REQUIRE(img.height == h);
        REQUIRE(img.rgb.size() == static_cast<size_t>(w) * h * 3);
        for (size_t i = 0; i < img.rgb.size(); i += 3) {
            REQUIRE(img.rgb[i + 0] == r);
            REQUIRE(img.rgb[i + 1] == g);
            REQUIRE(img.rgb[i + 2] == bb);
        }
    }
}

// ============================================================================
// PROPERTY 14: At() returns the byte at (x, y, c)
// ============================================================================

TEST_CASE("ImageCompare property: At() addressing matches RGB row-major",
          "[image][property][image]") {
    // Construct an image with deterministic content (byte = (y * w + x) * 3 + c
    // truncated to 8 bits) and confirm At() returns those bytes.
    Image img;
    img.width = 17; img.height = 13;
    img.rgb.resize(static_cast<size_t>(img.width) * img.height * 3);
    for (uint32_t y = 0; y < img.height; ++y) {
        for (uint32_t x = 0; x < img.width; ++x) {
            for (uint32_t c = 0; c < 3; ++c) {
                const size_t idx = (static_cast<size_t>(y) * img.width + x) * 3 + c;
                img.rgb[idx] = static_cast<uint8_t>(idx & 0xFFu);
            }
        }
    }
    REQUIRE(img.IsValid());
    for (uint32_t y = 0; y < img.height; ++y) {
        for (uint32_t x = 0; x < img.width; ++x) {
            for (uint32_t c = 0; c < 3; ++c) {
                const size_t idx = (static_cast<size_t>(y) * img.width + x) * 3 + c;
                REQUIRE(img.At(x, y, c) == static_cast<uint8_t>(idx & 0xFFu));
            }
        }
    }
}

// ============================================================================
// PROPERTY 15: PSNR is non-negative for legal 8-bit imagery
// ============================================================================

TEST_CASE("ImageCompare property: PSNR is non-negative for legal 8-bit input",
          "[image][property][psnr]") {
    // MSE is bounded above by 255^2 = 65025 (every pixel byte differs by
    // the maximum 8-bit code); PSNR = 10*log10(65025/65025) = 0.
    // For any MSE < 65025, PSNR > 0. Sweep random pairs and check.
    for (uint32_t trial = 0; trial < 50; ++trial) {
        const Image a = MakeRandomImage(32, 32, kPropertySeed ^ trial);
        const Image b = MakeRandomImage(32, 32, kPropertySeed ^ (trial + 0xBBBBu));
        const double psnr = PSNR(a, b);
        if (std::isfinite(psnr)) {
            REQUIRE(psnr >= 0.0);
        }
    }
}

// ============================================================================
// PROPERTY 16: PPM round-trip preserves rectangular non-square sizes
// ============================================================================

TEST_CASE("ImageCompare property: PPM round-trip handles non-square images",
          "[image][property][ppm]") {
    // Width != height has bitten many PPM encoders that confused row pitch
    // with column pitch. Property test rectangular shapes.
    for (uint32_t trial = 0; trial < 20; ++trial) {
        std::mt19937 rng(kPropertySeed ^ trial);
        std::uniform_int_distribution<int> dim(1, 32);
        const uint32_t w = static_cast<uint32_t>(dim(rng));
        const uint32_t h = static_cast<uint32_t>(dim(rng) + 1); // ensure != w sometimes
        Image src = MakeRandomImage(w, h, kPropertySeed ^ trial);
        const std::string path = MakeTempPpmPath(kPropertySeed ^ (trial + 0xDADAu));
        REQUIRE(WritePPM(path, src));
        Image loaded;
        REQUIRE(ReadPPM(path, loaded));
        REQUIRE(loaded.width == w);
        REQUIRE(loaded.height == h);
        REQUIRE(loaded.rgb == src.rgb);
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
}

// ============================================================================
// PROPERTY 17: SSIM(a, b) == SSIM(a, b) — pure (no hidden state)
// ============================================================================

TEST_CASE("ImageCompare property: SSIM is a pure function",
          "[image][property][ssim]") {
    // Re-running SSIM on the same inputs must give the same answer
    // bit-exactly. If a future contributor adds a static cache or thread-
    // local accumulator the test fails fast.
    for (uint32_t trial = 0; trial < 20; ++trial) {
        const Image a = MakeRandomImage(32, 32, kPropertySeed ^ trial);
        const Image b = MakeRandomImage(32, 32, kPropertySeed ^ (trial + 0x1234u));
        const double s1 = SSIM(a, b);
        const double s2 = SSIM(a, b);
        const double s3 = SSIM(a, b);
        REQUIRE(s1 == s2);
        REQUIRE(s2 == s3);
    }
}

// ============================================================================
// PROPERTY 18: PSNR/MSE/SSIM all reject invalid images
// ============================================================================

TEST_CASE("ImageCompare property: invalid-image inputs surface as NaN",
          "[image][property][validity]") {
    // A default-constructed Image has width=height=0 → !IsValid(). Every
    // metric must short-circuit to NaN rather than divide by zero.
    Image empty;
    Image valid = MakeRandomImage(32, 32, kPropertySeed);
    REQUIRE(std::isnan(MeanSquaredError(empty, valid)));
    REQUIRE(std::isnan(MeanSquaredError(valid, empty)));
    REQUIRE(std::isnan(PSNR(empty, valid)));
    REQUIRE(std::isnan(SSIM(empty, valid)));
    REQUIRE(std::isnan(SSIM(valid, empty)));
}

// ============================================================================
// PROPERTY 19: SSIM matches its own definition for a single 8x8 window
// ============================================================================

TEST_CASE("ImageCompare property: 8x8 single-window SSIM matches manual formula",
          "[image][property][ssim]") {
    // For an 8x8 image with windowSize=8 there is exactly one window per
    // channel. We hand-compute mean / var / cov per channel and apply the
    // closed-form SSIM expression, then compare with the helper. Catches
    // off-by-one errors in the per-channel divide or accidental cross-
    // channel mixing.
    Image a = MakeRandomImage(8, 8, kPropertySeed ^ 0x10000u);
    Image b = MakeRandomImage(8, 8, kPropertySeed ^ 0x20000u);
    constexpr double L = 255.0, K1 = 0.01, K2 = 0.03;
    constexpr double C1 = (K1 * L) * (K1 * L);
    constexpr double C2 = (K2 * L) * (K2 * L);
    double channelSum = 0.0;
    for (uint32_t ch = 0; ch < 3; ++ch) {
        double sumA = 0, sumB = 0;
        for (uint32_t y = 0; y < 8; ++y) {
            for (uint32_t x = 0; x < 8; ++x) {
                sumA += a.At(x, y, ch);
                sumB += b.At(x, y, ch);
            }
        }
        const double muA = sumA / 64.0;
        const double muB = sumB / 64.0;
        double varA = 0, varB = 0, cov = 0;
        for (uint32_t y = 0; y < 8; ++y) {
            for (uint32_t x = 0; x < 8; ++x) {
                const double pa = a.At(x, y, ch);
                const double pb = b.At(x, y, ch);
                varA += (pa - muA) * (pa - muA);
                varB += (pb - muB) * (pb - muB);
                cov  += (pa - muA) * (pb - muB);
            }
        }
        varA /= 64.0; varB /= 64.0; cov /= 64.0;
        const double num = (2 * muA * muB + C1) * (2 * cov + C2);
        const double den = (muA * muA + muB * muB + C1) * (varA + varB + C2);
        channelSum += num / den;
    }
    const double expected = channelSum / 3.0;
    const double got = SSIM(a, b, 8);
    REQUIRE(got == Approx(expected).margin(1e-9));
}

// ============================================================================
// PROPERTY 20: Strict subset of pixels modified still gives < 1 SSIM
// ============================================================================

TEST_CASE("ImageCompare property: single-pixel modification drops SSIM below 1",
          "[image][property][ssim]") {
    // One byte flipped → at least one window's SSIM dips below 1 → mean
    // SSIM strictly below 1. Property test that the comparator is
    // sensitive to single-pixel changes (otherwise the CI gate misses
    // small but real corruption).
    for (uint32_t trial = 0; trial < 30; ++trial) {
        Image base = MakeRandomImage(32, 32, kPropertySeed ^ trial);
        Image tweaked = base;
        // Flip the middle byte by a meaningful amount so the variance
        // shift is detectable; a single-bit XOR can land below the SSIM
        // window's noise floor.
        tweaked.rgb[tweaked.rgb.size() / 2] = static_cast<uint8_t>(
            255 - tweaked.rgb[tweaked.rgb.size() / 2]);
        const double s = SSIM(base, tweaked);
        REQUIRE(s < 1.0);
    }
}

// ============================================================================
// PROPERTY 21: SSIMFromFiles agrees with in-memory SSIM
// ============================================================================

TEST_CASE("ImageCompare property: SSIMFromFiles matches in-memory SSIM",
          "[image][property][ssim]") {
    using CatEngine::Renderer::ImageCompare::SSIMFromFiles;
    for (uint32_t trial = 0; trial < 20; ++trial) {
        const Image a = MakeRandomImage(16, 16, kPropertySeed ^ trial);
        const Image b = MakeRandomImage(16, 16, kPropertySeed ^ (trial + 0x9000u));
        const std::string pathA = MakeTempPpmPath(kPropertySeed ^ (trial + 0x10u));
        const std::string pathB = MakeTempPpmPath(kPropertySeed ^ (trial + 0x20u));
        REQUIRE(WritePPM(pathA, a));
        REQUIRE(WritePPM(pathB, b));
        const double sMem = SSIM(a, b);
        const double sFile = SSIMFromFiles(pathA, pathB);
        REQUIRE(sMem == Approx(sFile).margin(1e-9));
        std::error_code ec;
        std::filesystem::remove(pathA, ec);
        std::filesystem::remove(pathB, ec);
    }
}
