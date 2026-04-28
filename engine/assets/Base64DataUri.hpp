#pragma once

// -----------------------------------------------------------------------------
// Base64DataUri.hpp — inline RFC 2397 `data:` URI decoder.
//
// WHY this exists:
//   Every shipping .gltf in assets/models/ (cat, dog, trees, rocks, sword,
//   arrows, spell projectile) embeds its binary buffer inline as a
//   `data:application/octet-stream;base64,<payload>` URI rather than pointing
//   at an external .bin sidecar. glTF 2.0 explicitly allows both forms
//   (§3.6.1.1 "URIs"), but the original ModelLoader only handled the sidecar
//   path — it concatenated `baseDir + uri` and tried to open a file literally
//   named `assets/models/data:application/octet-stream;base64,hetR...`, which
//   of course did not exist. Every model on screen silently fell back to
//   placeholder geometry and the game's signature visual was missing.
//
//   This header closes that gap with a small, dependency-free decoder that
//   ModelLoader calls before deciding whether to hit the filesystem. It is
//   header-only on purpose so that the no-GPU Catch2 test build can exercise
//   it without pulling in the rest of ModelLoader (which transitively depends
//   on nlohmann::json, GLM, and the engine's mesh/material types).
//
// Scope:
//   - `IsDataUri(uri)`     — cheap prefix check used to fork the loading path.
//   - `DecodeBase64(uri)`  — full RFC 4648 base64 decode of the payload.
//                            Whitespace-tolerant (CR/LF/space/tab are skipped),
//                            accepts `=` padding, and throws std::runtime_error
//                            on malformed input so the caller's try/catch
//                            surfaces a clear error message.
//
// Not supported (deliberately):
//   - URL-encoded (percent-encoded) text data URIs. glTF buffer URIs are
//     always binary and in practice always base64 — supporting ASCII text
//     URIs would be dead code with a real security footprint (decoding
//     attacker-controlled percent-sequences).
//   - `data:image/<...>` decoding. Images in the shipping assets use external
//     files, not data URIs. When that changes, this helper is reusable as-is:
//     the decode step is mime-agnostic.
// -----------------------------------------------------------------------------

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace CatEngine {
namespace Base64DataUri {

// True iff `uri` begins with the RFC 2397 scheme prefix `data:`. Callers use
// this as a cheap discriminator before committing to the (more expensive)
// decode path, so it must not throw and must not allocate.
inline bool IsDataUri(std::string_view uri) noexcept {
    return uri.size() >= 5 && uri.substr(0, 5) == "data:";
}

// Decode the base64 payload of a `data:` URI into raw bytes.
//
// Expected shape: `data:<mediatype>;base64,<payload>` (the `<mediatype>` part
// is optional per RFC 2397 but glTF buffers always name one). We don't
// interpret the media type — callers downstream already know they asked for a
// glTF buffer and the result is bytes either way.
//
// Throws std::runtime_error on:
//   - Missing `data:` prefix (caller should have checked IsDataUri first).
//   - Missing `,` separating header from payload (malformed URI).
//   - Missing `;base64` in the header (we refuse percent-encoded text URIs
//     for the reasons noted at the top of this file).
//   - Any non-base64, non-whitespace, non-padding character in the payload.
inline std::vector<uint8_t> DecodeBase64(std::string_view uri) {
    if (!IsDataUri(uri)) {
        throw std::runtime_error(
            "Base64DataUri::DecodeBase64: input is not a data: URI");
    }

    // Locate the ',' that separates the header ("data:<mime>[;base64]") from
    // the payload. Searching the whole string is fine — the first ',' in a
    // well-formed data URI is always the separator; commas inside the payload
    // itself would be invalid base64 and would have been caught below anyway.
    const auto commaIndex = uri.find(',');
    if (commaIndex == std::string_view::npos) {
        throw std::runtime_error(
            "Base64DataUri::DecodeBase64: malformed URI — no ',' separator");
    }

    // `uri.substr(5, ...)` skips the literal "data:" scheme. Anything between
    // the scheme and the comma is the media-type declaration; we only care
    // that ";base64" appears in it, because percent-encoded text URIs are out
    // of scope for this loader.
    const auto headerView = uri.substr(5, commaIndex - 5);
    if (headerView.find(";base64") == std::string_view::npos) {
        throw std::runtime_error(
            "Base64DataUri::DecodeBase64: only ';base64' data URIs are "
            "supported (percent-encoded text URIs are not)");
    }

    const auto payloadView = uri.substr(commaIndex + 1);

    // Preallocate to the maximum possible decoded size. Base64 packs 6 bits
    // into each ASCII byte, so the decoded size is at most payload_len * 3/4.
    // The shipping cat.gltf has ~500 KB of base64, so this reserve avoids
    // ~17 reallocs during decode.
    std::vector<uint8_t> output;
    output.reserve(payloadView.size() * 3 / 4);

    // RFC 4648 base64 decoder. Two knobs:
    //   - `accumulator` is a 24-bit sliding window (we only ever hold <= 24
    //     bits live, but a uint32_t leaves slack for the shift-before-mask).
    //   - `bitsInAccumulator` tracks how many low-order bits of the window
    //     contain un-emitted data.
    //
    // Every valid base64 character contributes 6 bits; whenever we have
    // accumulated at least 8, we emit one byte from the top of the window.
    // `=` is padding and marks end-of-stream — we break out immediately so
    // that any trailing bits that don't add up to a full byte are discarded,
    // matching the reference behaviour of e.g. Python's base64.b64decode.
    uint32_t accumulator = 0;
    int bitsInAccumulator = 0;

    for (char c : payloadView) {
        int sixBitValue;
        if      (c >= 'A' && c <= 'Z') sixBitValue = c - 'A';
        else if (c >= 'a' && c <= 'z') sixBitValue = c - 'a' + 26;
        else if (c >= '0' && c <= '9') sixBitValue = c - '0' + 52;
        else if (c == '+')             sixBitValue = 62;
        else if (c == '/')             sixBitValue = 63;
        else if (c == '=')             break;  // RFC 4648 padding — stream end
        else if (c == '\r' || c == '\n' || c == ' ' || c == '\t') {
            // Tolerate line-wrapped base64. Some generators (OpenSSL-style)
            // wrap payloads every 64/76 columns; glTF doesn't, but paying the
            // cost of a continue here is cheaper than refusing to load assets
            // produced by pipelines that do.
            continue;
        }
        else {
            // Any other character is a hard error: the stream is corrupt,
            // and silently discarding garbage would mean silently loading
            // a wrong mesh. Fail loud so the caller's exception handler can
            // log "which model failed" with the real reason.
            throw std::runtime_error(
                std::string("Base64DataUri::DecodeBase64: invalid character '")
                + c + "' in base64 payload");
        }

        accumulator = (accumulator << 6) | static_cast<uint32_t>(sixBitValue);
        bitsInAccumulator += 6;
        if (bitsInAccumulator >= 8) {
            bitsInAccumulator -= 8;
            output.push_back(static_cast<uint8_t>(
                (accumulator >> bitsInAccumulator) & 0xFFu));
        }
    }

    return output;
}

}  // namespace Base64DataUri
}  // namespace CatEngine
