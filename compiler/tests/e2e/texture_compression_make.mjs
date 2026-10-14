// Specification-written PNGs for automatic compression and exact Raw checks.
import { mkdirSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { deflateSync } from 'node:zlib';
const root = join(dirname(fileURLToPath(import.meta.url)), 'texture_compression_assets');
mkdirSync(root, { recursive: true });
function chunk(type, data) {
  const name = Buffer.from(type), payload = Buffer.concat([name, data]);
  let crc = 0xffffffff;
  for (const byte of payload) {
    crc ^= byte;
    for (let i = 0; i < 8; i++) crc = (crc >>> 1) ^ (crc & 1 ? 0xedb88320 : 0);
  }
  const size = Buffer.alloc(4), end = Buffer.alloc(4);
  size.writeUInt32BE(data.length); end.writeUInt32BE((crc ^ 0xffffffff) >>> 0);
  return Buffer.concat([size, payload, end]);
}
for (const [name, width, alpha] of [['opaque', 128, false], ['alpha', 128, true], ['small', 127, false]]) {
  const rows = Buffer.alloc((width * 4 + 1) * 128);
  for (let y = 0; y < 128; y++) for (let x = 0; x < width; x++) {
    const at = y * (width * 4 + 1) + 1 + x * 4;
    rows[at] = x * 2; rows[at + 1] = y * 2; rows[at + 2] = x + y;
    rows[at + 3] = alpha ? x + y : 255;
  }
  const header = Buffer.alloc(13);
  header.writeUInt32BE(width); header.writeUInt32BE(128, 4); header[8] = 8; header[9] = 6;
  writeFileSync(join(root, name + '.png'), Buffer.concat([Buffer.from([137,80,78,71,13,10,26,10]),
    chunk('IHDR', header), chunk('IDAT', deflateSync(rows)), chunk('IEND', Buffer.alloc(0))]));
}
