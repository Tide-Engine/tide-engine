// Reads a PNG as the specification says, with Node's zlib for what's packed:
// what checks the pictures Tide writes (png.mjs, capture.mjs). Every chunk's
// check is checked, and zlib checks the rest.

import { inflateSync } from 'node:zlib';

const crcTable = new Uint32Array(256);
for (let i = 0; i < 256; i++) {
    let c = i;
    for (let bit = 0; bit < 8; bit++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1;
    crcTable[i] = c >>> 0;
}

function crc32(bytes) {
    let crc = 0xFFFFFFFF;
    for (const byte of bytes) crc = crcTable[(crc ^ byte) & 0xFF] ^ (crc >>> 8);
    return ~crc >>> 0;
}

// The picture in `file` (a Buffer): { width, height, channels, pixels, at(x, y) },
// its pixels a byte a channel, rows from the top; at() gives [red, green, blue].
// Throws when it isn't a PNG of 8 bits a channel, with or without alpha.
export function readPng(file) {
    const signature = Buffer.from([0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A]);
    if (file.length < 8 || !file.subarray(0, 8).equals(signature)) throw new Error('not a PNG: its first bytes are wrong');
    let header = null, ended = false;
    const packed = [];
    for (let at = 8; at < file.length;) {
        if (ended) throw new Error('bytes after IEND');
        if (at + 12 > file.length) throw new Error('a chunk is cut short');
        const size = file.readUInt32BE(at);
        const type = file.toString('latin1', at + 4, at + 8);
        if (at + 12 + size > file.length) throw new Error(`${type} is cut short`);
        const data = file.subarray(at + 8, at + 8 + size);
        if (crc32(file.subarray(at + 4, at + 8 + size)) !== file.readUInt32BE(at + 8 + size)) throw new Error(`${type}'s check is wrong`);
        if (!header && type !== 'IHDR') throw new Error('IHDR is not first');
        if (type === 'IHDR') {
            if (size !== 13) throw new Error('IHDR has the wrong size');
            header = {
                width: data.readUInt32BE(0), height: data.readUInt32BE(4), depth: data[8], color: data[9],
                compression: data[10], filter: data[11], interlace: data[12],
            };
        } else if (type === 'IDAT') packed.push(data);
        else if (type === 'IEND') ended = true;
        at += 12 + size;
    }
    if (!header || !ended) throw new Error('no IHDR, or no IEND');
    const { width, height, depth, color, compression, filter, interlace } = header;
    if (depth !== 8 || (color !== 2 && color !== 6) || compression !== 0 || filter !== 0 || interlace !== 0) {
        throw new Error(`not 8 bits a channel of red, green and blue: ${JSON.stringify(header)}`);
    }
    const channels = color === 2 ? 3 : 4;
    const stride = width * channels;
    const raw = inflateSync(Buffer.concat(packed));
    if (raw.length !== (stride + 1) * height) throw new Error(`${raw.length} bytes unpacked for ${width} by ${height}`);

    // Each row after its filter: none, or what it differs by from the pixel to its left, the row above, or both
    const pixels = new Uint8Array(stride * height);
    for (let y = 0; y < height; y++) {
        const kind = raw[y * (stride + 1)];
        if (kind > 4) throw new Error(`row ${y} has filter ${kind}`);
        for (let x = 0; x < stride; x++) {
            const left = x >= channels ? pixels[y * stride + x - channels] : 0;
            const up = y ? pixels[(y - 1) * stride + x] : 0;
            const corner = y && x >= channels ? pixels[(y - 1) * stride + x - channels] : 0;
            let guess = 0;
            if (kind === 1) guess = left;
            else if (kind === 2) guess = up;
            else if (kind === 3) guess = (left + up) >> 1;
            else if (kind === 4) {
                const p = left + up - corner;
                const a = Math.abs(p - left), b = Math.abs(p - up), c = Math.abs(p - corner);
                guess = a <= b && a <= c ? left : b <= c ? up : corner;
            }
            pixels[y * stride + x] = (raw[y * (stride + 1) + 1 + x] + guess) & 0xFF;
        }
    }
    const at = (x, y) => Array.from(pixels.subarray((y * width + x) * channels, (y * width + x) * channels + 3));
    return { width, height, channels, pixels, at };
}
