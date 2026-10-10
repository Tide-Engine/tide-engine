// Bundled into the checked-in release runtime; never loaded as a separate URL.
import { gunzipSync } from '../third_party/fflate/browser.js';

let crcTable;

// fflate's synchronous gunzip skips the gzip checksum. Check both trailer
// fields so damaged embedded WASM or pack data fails before the game reads it.
export function gunzip(bytes) {
    if (bytes.length < 18) throw new Error('tide: truncated gzip payload');
    const trailer = new DataView(bytes.buffer, bytes.byteOffset + bytes.length - 8, 8);
    const expectedCRC = trailer.getUint32(0, true);
    const expectedSize = trailer.getUint32(4, true);
    const decoded = gunzipSync(bytes);
    if (decoded.length !== expectedSize) throw new Error('tide: gzip payload length differs');
    if (!crcTable) {
        crcTable = new Uint32Array(256);
        for (let i = 0; i < 256; i++) {
            let crc = i;
            for (let bit = 0; bit < 8; bit++) crc = crc & 1 ? (crc >>> 1) ^ 0xedb88320 : crc >>> 1;
            crcTable[i] = crc;
        }
    }
    let crc = 0xffffffff;
    for (const byte of decoded) crc = (crc >>> 8) ^ crcTable[(crc ^ byte) & 255];
    if ((crc ^ 0xffffffff) >>> 0 !== expectedCRC) throw new Error('tide: gzip payload checksum differs');
    return decoded;
}
