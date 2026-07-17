// Convert a binary PPM (Netpbm P6) frame dump into a PNG so humans and
// agents can actually look at it — the engine's --frame-dump writes P6
// because that's a 30-line dependency-free encoder on the C++ side, but
// nothing on this machine previews PPM. Zero native deps here either:
// PNG is emitted manually (stored-deflate zlib blocks + CRC32), which is
// plenty fast for one 1920x1080 debug frame and keeps `bun install`-free
// usage working in fresh clones.
//
// Usage: bun scripts/ppm_to_png.ts <in.ppm> <out.png>

import { readFileSync, writeFileSync } from "fs";

function parsePpm(buffer: Buffer): { width: number; height: number; pixels: Buffer } {
    // P6 header = magic, width, height, maxval — whitespace/comment
    // separated, then a SINGLE whitespace byte before binary pixel data.
    let offset = 0;
    const tokens: string[] = [];
    while (tokens.length < 4) {
        // Skip whitespace and '#' comment lines between tokens.
        while (offset < buffer.length) {
            const byte = buffer[offset];
            if (byte === 0x23 /* # */) {
                while (offset < buffer.length && buffer[offset] !== 0x0a) offset++;
            } else if (byte === 0x20 || byte === 0x09 || byte === 0x0a || byte === 0x0d) {
                offset++;
            } else {
                break;
            }
        }
        const start = offset;
        while (
            offset < buffer.length &&
            ![0x20, 0x09, 0x0a, 0x0d].includes(buffer[offset])
        ) {
            offset++;
        }
        tokens.push(buffer.subarray(start, offset).toString("ascii"));
    }
    offset++; // the single whitespace after maxval
    if (tokens[0] !== "P6") throw new Error(`not a P6 PPM (magic '${tokens[0]}')`);
    const width = Number(tokens[1]);
    const height = Number(tokens[2]);
    const pixels = buffer.subarray(offset, offset + width * height * 3);
    if (pixels.length !== width * height * 3) {
        throw new Error(`truncated pixel data: ${pixels.length} of ${width * height * 3} bytes`);
    }
    return { width, height, pixels };
}

const CRC_TABLE = new Uint32Array(256);
for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    CRC_TABLE[n] = c >>> 0;
}
function crc32(data: Buffer): number {
    let crc = 0xffffffff;
    for (const byte of data) crc = CRC_TABLE[(crc ^ byte) & 0xff] ^ (crc >>> 8);
    return (crc ^ 0xffffffff) >>> 0;
}

function pngChunk(type: string, data: Buffer): Buffer {
    const header = Buffer.alloc(8);
    header.writeUInt32BE(data.length, 0);
    header.write(type, 4, "ascii");
    const crcBuffer = Buffer.alloc(4);
    crcBuffer.writeUInt32BE(crc32(Buffer.concat([header.subarray(4), data])), 0);
    return Buffer.concat([header, data, crcBuffer]);
}

// zlib stream of STORED (uncompressed) deflate blocks. Adler-32 trailer
// per RFC 1950; 65535-byte block cap per RFC 1951.
function zlibStored(raw: Buffer): Buffer {
    const parts: Buffer[] = [Buffer.from([0x78, 0x01])];
    for (let position = 0; position < raw.length; position += 65535) {
        const chunk = raw.subarray(position, Math.min(position + 65535, raw.length));
        const isLast = position + 65535 >= raw.length ? 1 : 0;
        const blockHeader = Buffer.alloc(5);
        blockHeader[0] = isLast;
        blockHeader.writeUInt16LE(chunk.length, 1);
        blockHeader.writeUInt16LE(~chunk.length & 0xffff, 3);
        parts.push(blockHeader, chunk);
    }
    let s1 = 1, s2 = 0;
    for (const byte of raw) {
        s1 = (s1 + byte) % 65521;
        s2 = (s2 + s1) % 65521;
    }
    const adler = Buffer.alloc(4);
    adler.writeUInt32BE(((s2 << 16) | s1) >>> 0, 0);
    parts.push(adler);
    return Buffer.concat(parts);
}

export function convertPpmToPng(inputPath: string, outputPath: string): { width: number; height: number } {
    const { width, height, pixels } = parsePpm(readFileSync(inputPath));

    // PNG scanlines need a leading filter byte (0 = None) per row.
    const raw = Buffer.alloc(height * (1 + width * 3));
    for (let y = 0; y < height; y++) {
        pixels.copy(raw, y * (1 + width * 3) + 1, y * width * 3, (y + 1) * width * 3);
    }

    const ihdr = Buffer.alloc(13);
    ihdr.writeUInt32BE(width, 0);
    ihdr.writeUInt32BE(height, 4);
    ihdr[8] = 8;  // bit depth
    ihdr[9] = 2;  // color type: truecolor RGB
    const png = Buffer.concat([
        Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
        pngChunk("IHDR", ihdr),
        pngChunk("IDAT", zlibStored(raw)),
        pngChunk("IEND", Buffer.alloc(0)),
    ]);
    writeFileSync(outputPath, png);
    return { width, height };
}

if (import.meta.main) {
    const [inputPath, outputPath] = process.argv.slice(2);
    if (!inputPath || !outputPath) {
        console.error("usage: bun scripts/ppm_to_png.ts <in.ppm> <out.png>");
        process.exit(2);
    }
    const { width, height } = convertPpmToPng(inputPath, outputPath);
    console.log(`${outputPath}: ${width}x${height}`);
}
