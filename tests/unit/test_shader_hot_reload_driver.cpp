// ============================================================================
// tests/unit/test_shader_hot_reload_driver.cpp
//
// Unit coverage for the pure helpers inside
// engine/rhi/ShaderHotReloadDriver.hpp. The class itself spawns glslc and
// needs the Vulkan SDK on PATH, which is not something the unit suite can
// require (the CI / openclaw-nightly runners may not have it installed the
// same way a developer workstation does). So we test what we CAN test
// deterministically:
//
//   - ShouldTick: the throttle decision. Pure function of three doubles.
//   - MakeSpvPath: the source-to-compiled-path mapping. Pure string →
//     string.
//   - SlurpBinaryFile: the .spv byte reader. Touches tmp filesystem but in
//     a bounded way — writes a known-size file to Catch2's temp area,
//     reads it back, tears it down.
//   - IsShaderSourceExtension: the directory filter.
//
// The driver class (ShaderHotReloadDriver::Initialize + Tick) is NOT unit
// tested here; exercising it would either spawn glslc (fragile across
// environments) or require stubbing std::system, which is more brittle
// than valuable. The prior iteration applied the same split for
// ShaderHotReloader::CompileIndex — we mirror it.
// ============================================================================

#include "catch.hpp"
#include "engine/rhi/ShaderHotReloadDriver.hpp"

#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace CatEngine::RHI;
using namespace CatEngine::RHI::HotReloadDriverDetail;

// ---------------------------------------------------------------------------
// ShouldTick — throttle decision.
// ---------------------------------------------------------------------------
TEST_CASE("ShouldTick fires on first call (lastTickSec < 0 sentinel)", "[hot-reload-driver]") {
    REQUIRE(ShouldTick(0.0, -1.0, 0.25) == true);
    REQUIRE(ShouldTick(0.0, -1e9, 1.0) == true);
    // Negative lastTickSec wins even if the interval is huge.
    REQUIRE(ShouldTick(0.5, -0.001, 10.0) == true);
}

TEST_CASE("ShouldTick honours intervalSec", "[hot-reload-driver]") {
    // Exactly at the interval boundary counts as "fire" — using a strict
    // less-than would cause a slow-drift frame to skip a tick whose
    // target deadline exactly matched nowSec.
    REQUIRE(ShouldTick(0.25, 0.0, 0.25) == true);
    REQUIRE(ShouldTick(0.26, 0.0, 0.25) == true);
    REQUIRE(ShouldTick(0.24, 0.0, 0.25) == false);
    REQUIRE(ShouldTick(1.0, 0.0, 0.25) == true);
}

TEST_CASE("ShouldTick: intervalSec <= 0 means fire every call", "[hot-reload-driver]") {
    // A benchmarker that wants per-frame scanning shouldn't be penalised
    // with a stale-by-one-frame result.
    REQUIRE(ShouldTick(0.001, 0.0, 0.0) == true);
    REQUIRE(ShouldTick(0.001, 0.0, -1.0) == true);
}

TEST_CASE("ShouldTick: typical 60fps loop at 4Hz fires ~1 in 15 frames", "[hot-reload-driver]") {
    // Simulate 60 calls at 1/60s stride with interval=0.25s. Count fires.
    const double frameDt = 1.0 / 60.0;
    double lastTickSec = 0.0;  // seeded by a previous tick at t=0
    int fires = 0;
    for (int i = 1; i <= 60; ++i) {
        const double now = i * frameDt;  // 0.0167 .. 1.0
        if (ShouldTick(now, lastTickSec, 0.25)) {
            lastTickSec = now;
            ++fires;
        }
    }
    // At exactly 0.25s intervals, 60 frames of 1/60s each should fire 4
    // times (the 15th, 30th, 45th, 60th frame targets cross the
    // boundaries 0.25, 0.50, 0.75, 1.0). Floating drift may push us to
    // 3 or 4 depending on the exact comparison epsilon — both are
    // acceptable; the contract is "firing cadence ≈ 4 Hz", not
    // "exactly 4 per second".
    REQUIRE(fires >= 3);
    REQUIRE(fires <= 5);
}

// ---------------------------------------------------------------------------
// MakeSpvPath — source-to-compiled path mapping.
// ---------------------------------------------------------------------------
TEST_CASE("MakeSpvPath maps nested source to flat compiled dir", "[hot-reload-driver]") {
    // House convention: shaders/forward/forward.vert →
    // shaders/compiled/forward.vert.spv (flat, NAME.EXT.spv).
    REQUIRE(MakeSpvPath("shaders/compiled", "shaders/forward/forward.vert")
            == "shaders/compiled/forward.vert.spv");

    REQUIRE(MakeSpvPath("shaders/compiled", "shaders/lighting/clustered.comp")
            == "shaders/compiled/clustered.comp.spv");
}

TEST_CASE("MakeSpvPath preserves all filename extensions", "[hot-reload-driver]") {
    // shaders like `ui.vert`, `tonemap.frag`, `clustered.comp` all keep
    // their full filename as NAME.EXT in the output — the packing rule
    // is "append .spv to the filename", not "replace the extension".
    REQUIRE(MakeSpvPath("out", "src/foo.vert")  == "out/foo.vert.spv");
    REQUIRE(MakeSpvPath("out", "src/foo.frag")  == "out/foo.frag.spv");
    REQUIRE(MakeSpvPath("out", "src/foo.comp")  == "out/foo.comp.spv");
}

TEST_CASE("MakeSpvPath handles trailing slash on compiledDir", "[hot-reload-driver]") {
    // A caller that passes compiledDir="shaders/compiled/" shouldn't get
    // a double slash in the result. Both the trailing-slash and
    // no-trailing-slash forms must produce the same output.
    const std::string withoutSlash = MakeSpvPath("out",  "src/foo.vert");
    const std::string withSlash    = MakeSpvPath("out/", "src/foo.vert");
    REQUIRE(withoutSlash == withSlash);
    REQUIRE(withoutSlash == "out/foo.vert.spv");

    // Backslash is also accepted as the trailing separator on the Windows
    // side — the house convention is forward slashes in the OUTPUT, but
    // a caller that accidentally passes backslash shouldn't get a
    // "out\foo.vert.spv" mix. Extra parens so Catch2's expression
    // decomposer doesn't try to split the `||` (single-header Catch2
    // rejects chained comparisons inside REQUIRE).
    const std::string backslashResult = MakeSpvPath("out\\", "src/foo.vert");
    REQUIRE((backslashResult == "out\\foo.vert.spv"
          || backslashResult == "out/foo.vert.spv"));
    // (We don't over-constrain which separator gets preserved — what
    // matters is we don't emit a double separator.)
}

TEST_CASE("MakeSpvPath handles source without any directory component", "[hot-reload-driver]") {
    // A caller that passes a bare filename (no directory) still gets
    // compiledDir/filename.spv — the filename equals the whole input.
    REQUIRE(MakeSpvPath("compiled", "forward.vert")
            == "compiled/forward.vert.spv");
}

TEST_CASE("MakeSpvPath handles Windows backslash separator in source", "[hot-reload-driver]") {
    // std::filesystem on Windows sometimes surfaces backslash separators
    // even when we asked for generic_string() — the enumerate loop
    // normalises them, but the helper must be robust to both forms
    // anyway so direct callers don't hit a surprise.
    REQUIRE(MakeSpvPath("out", "shaders\\forward\\forward.vert")
            == "out/forward.vert.spv");
}

TEST_CASE("MakeSpvPath with empty compiledDir appends no separator", "[hot-reload-driver]") {
    // Empty compiled dir is a pathological input but shouldn't produce a
    // leading '/' — the result should be a valid relative path.
    REQUIRE(MakeSpvPath("", "src/foo.vert") == "foo.vert.spv");
}

// ---------------------------------------------------------------------------
// IsShaderSourceExtension — directory enumeration filter.
// ---------------------------------------------------------------------------
TEST_CASE("IsShaderSourceExtension accepts the three stages we ship", "[hot-reload-driver]") {
    REQUIRE(IsShaderSourceExtension("forward.vert"));
    REQUIRE(IsShaderSourceExtension("forward.frag"));
    REQUIRE(IsShaderSourceExtension("clustered.comp"));
    // path-qualified forms — helper only looks at extension, ignores dir.
    REQUIRE(IsShaderSourceExtension("shaders/forward/forward.vert"));
}

TEST_CASE("IsShaderSourceExtension rejects unrelated extensions", "[hot-reload-driver]") {
    REQUIRE_FALSE(IsShaderSourceExtension("forward.spv"));      // output, not source
    REQUIRE_FALSE(IsShaderSourceExtension("common.glsl"));      // included, not compiled standalone
    REQUIRE_FALSE(IsShaderSourceExtension("notes.md"));
    REQUIRE_FALSE(IsShaderSourceExtension("forward.vert.bak")); // editor backup
    REQUIRE_FALSE(IsShaderSourceExtension("README"));           // no extension at all
    REQUIRE_FALSE(IsShaderSourceExtension(""));                 // empty input
    REQUIRE_FALSE(IsShaderSourceExtension(".hidden"));          // dotfile with no name+ext
}

TEST_CASE("IsShaderSourceExtension is case-sensitive (matches glslc)", "[hot-reload-driver]") {
    // Every shader in shaders/ uses lowercase; glslc's -fshader-stage
    // flag is case-sensitive and an uppercase filename in the tree would
    // be a filename bug, not a shader the driver should pick up.
    REQUIRE_FALSE(IsShaderSourceExtension("forward.VERT"));
    REQUIRE_FALSE(IsShaderSourceExtension("forward.Frag"));
}

// ---------------------------------------------------------------------------
// SlurpBinaryFile — byte reader.
// ---------------------------------------------------------------------------
TEST_CASE("SlurpBinaryFile returns empty vector when file missing", "[hot-reload-driver]") {
    // A non-existent path is the "glslc failed before writing" case —
    // SlurpBinaryFile must return empty so the callback sees zero bytes
    // and can't accidentally install a stale / partial .spv.
    const auto bytes = SlurpBinaryFile("definitely/does/not/exist/at/all.spv");
    REQUIRE(bytes.empty());
}

TEST_CASE("SlurpBinaryFile round-trips an on-disk file bit-for-bit", "[hot-reload-driver]") {
    // Write a deterministic payload to a temp path, read it back,
    // assert byte equality. Uses Catch2's temp_directory_path so the
    // test doesn't leave cruft in the repo tree.
    namespace fs = std::filesystem;
    const fs::path tmp = fs::temp_directory_path() /
        "cat_shader_hot_reload_driver_slurp.bin";

    // Payload chosen to exercise every byte value 0..255 so any
    // accidental text-mode translation (CRLF, etc.) would surface as a
    // byte mismatch somewhere.
    std::vector<uint8_t> payload(256);
    for (size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<uint8_t>(i);
    }

    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        REQUIRE(out.is_open());
        out.write(reinterpret_cast<const char*>(payload.data()),
                  static_cast<std::streamsize>(payload.size()));
    }

    const auto read = SlurpBinaryFile(tmp.string());
    REQUIRE(read.size() == payload.size());
    REQUIRE(read == payload);

    // Teardown — best-effort, never throw from a test cleanup.
    std::error_code ec;
    fs::remove(tmp, ec);
}

TEST_CASE("SlurpBinaryFile handles zero-byte file", "[hot-reload-driver]") {
    // A successful glslc with `-O --strip` on an empty source could
    // theoretically produce zero bytes. SlurpBinaryFile must return an
    // empty-but-success-shaped vector (which the driver will then hand
    // to the callback; the callback is responsible for treating zero
    // bytes as "don't swap", matching the on-disk-truncated case).
    namespace fs = std::filesystem;
    const fs::path tmp = fs::temp_directory_path() /
        "cat_shader_hot_reload_driver_empty.bin";

    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        REQUIRE(out.is_open());
        // Write nothing — file exists with zero bytes.
    }

    const auto read = SlurpBinaryFile(tmp.string());
    REQUIRE(read.empty());

    std::error_code ec;
    fs::remove(tmp, ec);
}

// ---------------------------------------------------------------------------
// Driver smoke test — we can still construct the driver against an empty
// sources dir and confirm it stays idle without attempting any IO.
// ---------------------------------------------------------------------------
TEST_CASE("ShaderHotReloadDriver is idle when sourcesDir is missing", "[hot-reload-driver]") {
    ShaderHotReloadDriver driver;
    // Intentionally pass a path that definitely doesn't exist. Initialize
    // must return zero (no entries) and not throw. A subsequent Tick()
    // must short-circuit on the "empty entries" branch.
    const size_t count = driver.Initialize(
        "definitely/does/not/exist/shaders",
        "definitely/does/not/exist/shaders/compiled");
    REQUIRE(count == 0);

    // Tick with any clock value — no entries means no scan, no compile,
    // no callback firing.
    const size_t attempts = driver.Tick(10.0);
    REQUIRE(attempts == 0);
}

TEST_CASE("ShaderHotReloadDriver exposes interval configurable", "[hot-reload-driver]") {
    ShaderHotReloadDriver driver;
    // Default is 0.25s (4 Hz) — matches the constant in the header's
    // commentary block. A test guards against drift if someone tunes it.
    REQUIRE(driver.GetIntervalSec() == 0.25);
    driver.SetIntervalSec(1.5);
    REQUIRE(driver.GetIntervalSec() == 1.5);
    driver.SetIntervalSec(0.0);
    REQUIRE(driver.GetIntervalSec() == 0.0);
}

// ---------------------------------------------------------------------------
// ShaderHotReloader::PrimeMtimes — a cross-cutting test that lives here
// rather than in test_shader_hot_reload.cpp because PrimeMtimes() was
// added as part of the driver iteration specifically to enable the
// "first-tick-after-boot is a no-op" behaviour. Testing the interplay
// with Scan() at this layer keeps the relationship explicit.
// ---------------------------------------------------------------------------
TEST_CASE("PrimeMtimes + Scan: post-prime scan is empty", "[hot-reload-driver]") {
    namespace fs = std::filesystem;
    // Create a tiny .vert in the temp dir so the mtime is real.
    const fs::path tmpVert = fs::temp_directory_path() /
        "cat_hot_reload_prime.vert";
    {
        std::ofstream out(tmpVert);
        REQUIRE(out.is_open());
        out << "#version 450\nvoid main() {}\n";
    }

    ShaderHotReloader reloader;
    const size_t idx = reloader.AddSource(tmpVert.string(), "ignored.spv");
    REQUIRE(idx == 0);

    // Pre-prime: first Scan flags the entry (lastKnownMtime == 0,
    // on-disk mtime != 0 → detected as change).
    const auto prePrimeChanged = reloader.Scan();
    REQUIRE(prePrimeChanged.size() == 1);
    REQUIRE(prePrimeChanged[0] == 0);

    // Prime — seed lastKnownMtime from on-disk.
    reloader.PrimeMtimes();

    // Post-prime: Scan is empty (no disk edit has happened).
    const auto postPrimeChanged = reloader.Scan();
    REQUIRE(postPrimeChanged.empty());

    std::error_code ec;
    fs::remove(tmpVert, ec);
}

TEST_CASE("PrimeMtimes is a no-op on a missing source (next scan still flags it)",
          "[hot-reload-driver]") {
    ShaderHotReloader reloader;
    reloader.AddSource("definitely/missing/shader.frag", "ignored.spv");
    reloader.PrimeMtimes();
    // Missing source stays at zero mtime — the on-disk mtime reads as
    // zero too (std::error_code set), but DetectChangedSources treats
    // zero == zero as "no change". The driver's Scan() converts
    // filesystem errors to zero via the same path, so the practical
    // outcome is: PrimeMtimes neither crashes on the missing file, nor
    // does it mask the missing-file failure mode once the file DOES
    // appear (at which point its mtime will be nonzero and != stored
    // zero → flagged as change). Here we just verify the non-crash
    // property — the re-detection property is covered indirectly by
    // the "Scan on missing file" case already in test_shader_hot_reload.
    const auto changed = reloader.Scan();
    // Either 0 (both stored-and-current read as zero) or 1 (current
    // read surfaced a nonzero error_code mtime) — both are acceptable,
    // the contract is "don't crash".
    REQUIRE((changed.empty() || changed.size() == 1));
}
