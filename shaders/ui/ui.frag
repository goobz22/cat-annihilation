#version 450

// UI Fragment Shader with built-in bitmap font
// Text: UV.x > 50 means character code (UV.x - 100)
//       UV.y encodes row.column (integer=row 0-4, fract=column 0-1)

layout(location = 0) in vec2 inTexCoord;
layout(location = 1) in vec4 inColor;
layout(location = 0) out vec4 outColor;

// 5x5 bitmap font patterns - each uint has 25 bits (5 rows x 5 cols)
// Bit 24 = row0,col0 (top-left), bit 0 = row4,col4 (bottom-right)
// Reading order: left-to-right, top-to-bottom
uint getPattern(int c) {
    // 0-9
    if (c == 48) return 0x0E9D72Eu; // 0
    if (c == 49) return 0x046108Eu; // 1
    if (c == 50) return 0x0E8991Fu; // 2
    if (c == 51) return 0x1E0B83Eu; // 3
    if (c == 52) return 0x118FC21u; // 4
    if (c == 53) return 0x1F8783Eu; // 5
    if (c == 54) return 0x0E87A2Eu; // 6
    if (c == 55) return 0x1F08884u; // 7
    if (c == 56) return 0x0E8BA2Eu; // 8
    if (c == 57) return 0x0E8BC2Eu; // 9

    // A-Z
    if (c == 65) return 0x0E8FE31u; // A
    if (c == 66) return 0x1E8FA3Eu; // B
    if (c == 67) return 0x0F8420Fu; // C
    if (c == 68) return 0x1E8C63Eu; // D
    if (c == 69) return 0x1F87A1Fu; // E
    if (c == 70) return 0x1F87A10u; // F
    if (c == 71) return 0x0F85E2Eu; // G
    if (c == 72) return 0x118FE31u; // H
    if (c == 73) return 0x0E2108Eu; // I
    if (c == 74) return 0x0710A4Cu; // J
    if (c == 75) return 0x1197251u; // K
    if (c == 76) return 0x108421Fu; // L
    if (c == 77) return 0x11DD631u; // M
    if (c == 78) return 0x11CD671u; // N
    if (c == 79) return 0x0E8C62Eu; // O
    if (c == 80) return 0x1E8FA10u; // P
    if (c == 81) return 0x0E8C64Du; // Q
    if (c == 82) return 0x1E8FA51u; // R
    if (c == 83) return 0x0F8383Eu; // S
    if (c == 84) return 0x1F21084u; // T
    if (c == 85) return 0x118C62Eu; // U
    if (c == 86) return 0x118C544u; // V
    if (c == 87) return 0x118D771u; // W
    if (c == 88) return 0x1151151u; // X
    if (c == 89) return 0x1151084u; // Y
    if (c == 90) return 0x1F1111Fu; // Z

    // lowercase a-z (same patterns as uppercase)
    if (c == 97) return 0x0E8FE31u; // a
    if (c == 98) return 0x1E8FA3Eu; // b
    if (c == 99) return 0x0F8420Fu; // c
    if (c == 100) return 0x1E8C63Eu; // d
    if (c == 101) return 0x1F87A1Fu; // e
    if (c == 102) return 0x1F87A10u; // f
    if (c == 103) return 0x0F85E2Eu; // g
    if (c == 104) return 0x118FE31u; // h
    if (c == 105) return 0x0E2108Eu; // i
    if (c == 106) return 0x0710A4Cu; // j
    if (c == 107) return 0x1197251u; // k
    if (c == 108) return 0x108421Fu; // l
    if (c == 109) return 0x11DD631u; // m
    if (c == 110) return 0x11CD671u; // n
    if (c == 111) return 0x0E8C62Eu; // o
    if (c == 112) return 0x1E8FA10u; // p
    if (c == 113) return 0x0E8C64Du; // q
    if (c == 114) return 0x1E8FA51u; // r
    if (c == 115) return 0x0F8383Eu; // s
    if (c == 116) return 0x1F21084u; // t
    if (c == 117) return 0x118C62Eu; // u
    if (c == 118) return 0x118C544u; // v
    if (c == 119) return 0x118D771u; // w
    if (c == 120) return 0x1151151u; // x
    if (c == 121) return 0x1151084u; // y
    if (c == 122) return 0x1F1111Fu; // z

    // Space and common punctuation
    if (c == 32) return 0x0000000u; // space
    if (c == 33) return 0x0421004u; // !
    if (c == 46) return 0x0000004u; // .
    if (c == 44) return 0x0000088u; // ,
    if (c == 45) return 0x0007C00u; // -
    if (c == 58) return 0x0020080u; // :
    if (c == 39) return 0x0420000u; // '
    if (c == 40) return 0x0221082u; // (
    if (c == 41) return 0x0821088u; // )
    if (c == 62) return 0x1041110u; // >
    if (c == 60) return 0x0222082u; // <

    return 0x0A050A0u; // Unknown = checkerboard
}

void main() {
    // Check if text (charCode encoded as UV.x > 50)
    if (inTexCoord.x > 50.0) {
        int charCode = int(inTexCoord.x - 100.0);
        uint pattern = getPattern(charCode);

        // Decode row and column from UV.y
        // Integer part = row (0-4), fractional part = column (0-1)
        int row = int(inTexCoord.y);
        float colF = fract(inTexCoord.y);
        int col = int(colF * 5.0);

        // Clamp to valid range
        row = clamp(row, 0, 4);
        col = clamp(col, 0, 4);

        // Get bit index: row 0 is top, col 0 is left
        // Pattern stored as: bit 24 = row0,col0, bit 0 = row4,col4
        int bitIdx = (4 - row) * 5 + (4 - col);
        bool pixelOn = ((pattern >> bitIdx) & 1u) != 0u;

        if (pixelOn) {
            outColor = inColor;
        } else {
            discard;
        }
    } else {
        // Regular quad - solid color
        outColor = inColor;
        if (outColor.a < 0.01) {
            discard;
        }
    }
}
