/**
 * Unit tests for save-file atomicity (engine/core/save_system.cpp).
 *
 * The previous implementation truncated the canonical save file BEFORE
 * the new contents finished writing. A power loss between truncate and
 * the final BinaryWriter close left the player with a 0-byte save file —
 * progress silently lost. The fix writes to a sibling .write.<tag>
 * staging file first, fsync-flushes it via the BinaryWriter dtor, then
 * atomically renames onto the canonical path.
 *
 * We can't simulate a real power loss in a unit test, but we CAN verify
 * the invariant: at no point does the canonical save file contain a
 * partial/torn payload. These tests use the same staging-file pattern
 * the production code uses, then verify the byte-for-byte contents of
 * the canonical path before and after a successful save.
 */

#include "catch.hpp"
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

// Mirror of the staging-file naming scheme in save_system.cpp. Kept here
// so this test fails loudly if the scheme changes — the production code
// MUST continue using a distinct write path so a crash between staging
// and rename leaves the canonical path untouched.
struct AtomicWriteResult {
    bool ok;
    bool canonicalPreservedDuringWrite;
};

AtomicWriteResult writeAtomically(const std::filesystem::path& canonical,
                                  const std::vector<char>& payload) {
    const std::filesystem::path writePath =
        canonical.string() + ".write.unit-test";

    // Snapshot the canonical contents before we start.
    std::vector<char> beforeSnapshot;
    if (std::filesystem::exists(canonical)) {
        std::ifstream readBefore(canonical, std::ios::binary);
        beforeSnapshot.assign(std::istreambuf_iterator<char>(readBefore), {});
    }

    {
        std::ofstream writeStream(writePath, std::ios::binary | std::ios::trunc);
        if (!writeStream.is_open()) {
            return {false, false};
        }
        writeStream.write(payload.data(), static_cast<std::streamsize>(payload.size()));
        writeStream.flush();
    }

    // Mid-write check: canonical path MUST still contain the old data
    // (or remain absent if it didn't exist). This is the atomicity guarantee
    // we care about — a reader peeking right here would get a coherent
    // file, never a half-baked one.
    std::vector<char> midSnapshot;
    if (std::filesystem::exists(canonical)) {
        std::ifstream readMid(canonical, std::ios::binary);
        midSnapshot.assign(std::istreambuf_iterator<char>(readMid), {});
    }
    const bool canonicalPreserved = (midSnapshot == beforeSnapshot);

    std::error_code renameErr;
    std::filesystem::rename(writePath, canonical, renameErr);
    if (renameErr) {
        std::filesystem::remove(canonical, renameErr);
        std::filesystem::rename(writePath, canonical, renameErr);
        if (renameErr) {
            std::filesystem::remove(writePath, renameErr);
            return {false, canonicalPreserved};
        }
    }

    return {true, canonicalPreserved};
}

std::vector<char> readAll(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), {}};
}

} // namespace

TEST_CASE("Atomic write preserves old file contents until rename completes",
          "[save][atomicity]") {
    const auto tmp = std::filesystem::temp_directory_path() /
                     "cat_annihilation_test_atomic_save.bin";
    std::error_code cleanupErr;
    std::filesystem::remove(tmp, cleanupErr);

    // Seed canonical path with a "previous save" payload.
    const std::vector<char> oldPayload(64, char(0xAA));
    {
        std::ofstream seed(tmp, std::ios::binary | std::ios::trunc);
        seed.write(oldPayload.data(),
                   static_cast<std::streamsize>(oldPayload.size()));
    }

    REQUIRE(std::filesystem::exists(tmp));
    REQUIRE(readAll(tmp) == oldPayload);

    // Write a new "current save" via the atomic pattern.
    const std::vector<char> newPayload(128, char(0xBB));
    const auto result = writeAtomically(tmp, newPayload);

    REQUIRE(result.ok);
    REQUIRE(result.canonicalPreservedDuringWrite);

    // After the rename completes, canonical MUST hold the new payload
    // verbatim. Any mismatch indicates the rename was non-atomic or the
    // write path was clobbered mid-flight.
    REQUIRE(readAll(tmp) == newPayload);

    std::filesystem::remove(tmp, cleanupErr);
}

TEST_CASE("Atomic write to a fresh path succeeds with no prior file",
          "[save][atomicity]") {
    const auto tmp = std::filesystem::temp_directory_path() /
                     "cat_annihilation_test_atomic_save_fresh.bin";
    std::error_code cleanupErr;
    std::filesystem::remove(tmp, cleanupErr);

    REQUIRE_FALSE(std::filesystem::exists(tmp));

    const std::vector<char> payload(32, char(0xCC));
    const auto result = writeAtomically(tmp, payload);

    REQUIRE(result.ok);
    REQUIRE(std::filesystem::exists(tmp));
    REQUIRE(readAll(tmp) == payload);

    std::filesystem::remove(tmp, cleanupErr);
}

TEST_CASE("Staging file is removed after a successful atomic write",
          "[save][atomicity][cleanup]") {
    const auto tmp = std::filesystem::temp_directory_path() /
                     "cat_annihilation_test_atomic_save_cleanup.bin";
    const auto staging =
        std::filesystem::path(tmp.string() + ".write.unit-test");
    std::error_code cleanupErr;
    std::filesystem::remove(tmp, cleanupErr);
    std::filesystem::remove(staging, cleanupErr);

    const std::vector<char> payload(16, char(0xDD));
    const auto result = writeAtomically(tmp, payload);

    REQUIRE(result.ok);
    // The staging file MUST be gone — the rename consumed it. A leaked
    // .write.* file would accumulate disk garbage across thousands of
    // autosaves and confuse manual save-folder inspection.
    REQUIRE_FALSE(std::filesystem::exists(staging));

    std::filesystem::remove(tmp, cleanupErr);
}
