// Original authored leaf spray. No downloaded/scanned art or external packages.
import { writeFileSync } from 'node:fs';
import { deflateSync } from 'node:zlib';
import { writeCoverageDDS, writeMipDDS } from './alpha-mips.mjs';

const clamp = (x, lo = 0, hi = 1) => Math.max(lo, Math.min(hi, x));
const unit = v => { const length = Math.hypot(...v); return v.map(x => x / length); };
const byte = x => Math.round(clamp(x) * 255);

function crc32(bytes) {
  let crc = 0xffffffff;
  for (const value of bytes) {
    crc ^= value;
    for (let i = 0; i < 8; i++) crc = (crc >>> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
  }
  return (crc ^ 0xffffffff) >>> 0;
}

export function writePNG(path, width, height, rgba) {
  const chunk = (name, data) => {
    const label = Buffer.from(name), size = Buffer.alloc(4), crc = Buffer.alloc(4);
    size.writeUInt32BE(data.length);
    crc.writeUInt32BE(crc32(Buffer.concat([label, data])));
    return Buffer.concat([size, label, data, crc]);
  };
  const header = Buffer.alloc(13);
  header.writeUInt32BE(width); header.writeUInt32BE(height, 4);
  header[8] = 8; header[9] = 6;
  const rows = Buffer.alloc(height * (1 + width * 4));
  for (let y = 0; y < height; y++)
    rows.set(rgba.subarray(y * width * 4, (y + 1) * width * 4), y * (1 + width * 4) + 1);
  writeFileSync(path, Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]),
    chunk('IHDR', header), chunk('IDAT', deflateSync(rows, { level: 9 })), chunk('IEND', Buffer.alloc(0))]));
}

export function buildLeafMaps(size = 512) {
  if (!Number.isInteger(size) || size < 32 || size > 2048 || (size & (size - 1)))
    throw new Error('Leaf size must be a power of two from 32 to 2048');
  const count = size * size, color = new Float32Array(count * 4);
  const normal = new Float32Array(count * 3), mask = new Float32Array(count * 4);
  function over(index, coverage, rgb, n, channels) {
    const a = color[index * 4 + 3], result = coverage + a * (1 - coverage);
    const keep = a * (1 - coverage);
    for (let c = 0; c < 3; c++) {
      color[index * 4 + c] = (rgb[c] * coverage + color[index * 4 + c] * keep) / result;
      normal[index * 3 + c] = (n[c] * coverage + normal[index * 3 + c] * keep) / result;
    }
    for (let c = 0; c < 4; c++) mask[index * 4 + c] = (channels[c] * coverage + mask[index * 4 + c] * keep) / result;
    color[index * 4 + 3] = result;
  }
  function line(a, b, width) {
    const dx = b[0] - a[0], dy = b[1] - a[1], square = dx * dx + dy * dy;
    const minX = Math.max(0, Math.floor((Math.min(a[0], b[0]) - width) * size - 1));
    const maxX = Math.min(size - 1, Math.ceil((Math.max(a[0], b[0]) + width) * size + 1));
    const minY = Math.max(0, Math.floor((Math.min(a[1], b[1]) - width) * size - 1));
    const maxY = Math.min(size - 1, Math.ceil((Math.max(a[1], b[1]) + width) * size + 1));
    for (let y = minY; y <= maxY; y++) for (let x = minX; x <= maxX; x++) {
      const u = (x + .5) / size, v = (y + .5) / size;
      const t = clamp(((u - a[0]) * dx + (v - a[1]) * dy) / square);
      const distance = Math.hypot(u - a[0] - t * dx, v - a[1] - t * dy);
      const coverage = clamp((width - distance) * size + .5);
      if (coverage) over(y * size + x, coverage, [.29, .29, .105], [0, 0, 1], [.9, .95, 0, 0]);
    }
  }
  line([.5, .965], [.505, .09], .006);
  const leaves = [];
  for (let i = 0; i < 6; i++) for (const side of [-1, 1]) {
    const y = .81 - i * .115, spread = .225 + Math.sin((i + .5) * Math.PI / 6) * .07;
    const base = [.5 + side * .025, y - .008];
    const tip = [.5 + side * spread, y - .12 - .04 * Math.sin(i * 1.7 + side)];
    line([.5, y + .025], base, .0025);
    leaves.push({ base, tip, width: .057 + .008 * Math.sin(i * 2.1 + side), seed: i * 3 + side + 4 });
  }
  leaves.push({ base: [.505, .245], tip: [.505, .045], width: .064, seed: 47 });
  for (const leaf of leaves) {
    const dx = leaf.tip[0] - leaf.base[0], dy = leaf.tip[1] - leaf.base[1];
    const length = Math.hypot(dx, dy), axis = [dx / length, dy / length], across = [-axis[1], axis[0]];
    const extent = leaf.width * 1.15;
    const minX = Math.max(0, Math.floor((Math.min(leaf.base[0], leaf.tip[0]) - extent) * size));
    const maxX = Math.min(size - 1, Math.ceil((Math.max(leaf.base[0], leaf.tip[0]) + extent) * size));
    const minY = Math.max(0, Math.floor((Math.min(leaf.base[1], leaf.tip[1]) - extent) * size));
    const maxY = Math.min(size - 1, Math.ceil((Math.max(leaf.base[1], leaf.tip[1]) + extent) * size));
    function coordinates(u, v) {
      const rx = u - leaf.base[0], ry = v - leaf.base[1];
      const a = (rx * axis[0] + ry * axis[1]) / length;
      const b = rx * across[0] + ry * across[1] - .012 * Math.sin(Math.PI * a + leaf.seed) * Math.sin(Math.PI * a);
      const width = leaf.width * Math.pow(Math.max(0, Math.sin(Math.PI * a)), .78)
        * (1 + .045 * Math.sin(a * 17 * Math.PI));
      return { a, t: width > 0 ? b / width : 2, inside: a >= 0 && a <= 1 && Math.abs(b) <= width };
    }
    for (let y = minY; y <= maxY; y++) for (let x = minX; x <= maxX; x++) {
      let hits = 0;
      for (const oy of [.25, .75]) for (const ox of [.25, .75])
        hits += coordinates((x + ox) / size, (y + oy) / size).inside ? 1 : 0;
      if (!hits) continue;
      const { a, t } = coordinates((x + .5) / size, (y + .5) / size);
      const rib = Math.exp(-t * t * 240);
      const veins = Math.exp(-Math.pow(Math.sin((a * 9 + Math.abs(t) * 1.35) * Math.PI), 2) * 90) * (1 - clamp(Math.abs(t)));
      const fleck = Math.sin(x * .73 + leaf.seed) * Math.sin(y * .53 + x * .19);
      const variation = .91 + .09 * Math.sin(leaf.seed * 1.73) + .07 * a + .035 * fleck;
      const rgb = [.30, .43, .145].map((value, c) => clamp(value * variation + (rib * .065 + veins * .035) * (c === 2 ? .4 : 1)));
      const along = .12 * Math.cos(Math.PI * a), bend = .52 * clamp(t, -1, 1);
      const n = unit([axis[0] * along + across[0] * bend, axis[1] * along + across[1] * bend, 1]);
      // Artist alpha is transmittance, not opacity: the coverage is in albedo.
      // Thick veins transmit less; fine tissue transmits more.
      const opticalDepth = clamp(.25 + .33 * (1 - a) + .35 * rib + .12 * veins);
      over(y * size + x, hits / 4, rgb, n, [.9 + .075 * Math.abs(t), .91 + .05 * Math.abs(t), 0, Math.exp(-opticalDepth)]);
    }
  }
  // Nearest occupied texel supplies RGB/normal/mask padding, never opacity.
  // This also keeps all source texture edges transparent for the fitted cards.
  const queue = new Uint32Array(count), owner = new Int32Array(count).fill(-1);
  let read = 0, written = 0;
  for (let i = 0; i < count; i++) if (color[i * 4 + 3] > 0) { owner[i] = i; queue[written++] = i; }
  while (read < written) {
    const i = queue[read++], x = i % size, y = Math.floor(i / size);
    for (const next of [x > 0 ? i - 1 : -1, x + 1 < size ? i + 1 : -1,
      y > 0 ? i - size : -1, y + 1 < size ? i + size : -1]) {
      if (next >= 0 && owner[next] < 0) { owner[next] = owner[i]; queue[written++] = next; }
    }
  }
  const albedo = new Uint8Array(count * 4), normals = new Uint8Array(count * 4), masks = new Uint8Array(count * 4);
  for (let i = 0; i < count; i++) {
    const from = owner[i];
    const n = unit(normal.subarray(from * 3, from * 3 + 3));
    for (let c = 0; c < 3; c++) {
      albedo[i * 4 + c] = byte(color[from * 4 + c]);
      normals[i * 4 + c] = byte(n[c] * .5 + .5);
    }
    albedo[i * 4 + 3] = byte(color[i * 4 + 3]); normals[i * 4 + 3] = 255;
    for (let c = 0; c < 4; c++) masks[i * 4 + c] = byte(mask[from * 4 + c]);
  }
  return { width: size, height: size, albedo, normal: normals, mask: masks, leaves: leaves.length };
}

// Circumscribed support polygon: clips blank corners without losing any base
// texel that passes the authored alpha cutoff, with minification padding.
export function leafCardHull(maps, sides) {
  const limits = Array(sides).fill(-Infinity), directions = Array.from({ length: sides }, (_, i) =>
    [Math.cos(i * 2 * Math.PI / sides), Math.sin(i * 2 * Math.PI / sides)]);
  for (let y = 0; y < maps.height; y++) for (let x = 0; x < maps.width; x++) {
    if (maps.albedo[(y * maps.width + x) * 4 + 3] <= .32 * 255) continue;
    for (let i = 0; i < sides; i++) {
      const [a, b] = directions[i];
      // Extra support covers half a minifying texel near the representation's
      // intended transition, rather than cutting a tightly fitted base mask.
      const padding = sides === 8 ? .04 : .07;
      limits[i] = Math.max(limits[i], a * (x + .5) / maps.width + b * (y + .5) / maps.height
        + Math.abs(a) * (padding + 2 / maps.width) + Math.abs(b) * (padding + 2 / maps.height));
    }
  }
  let polygon = [[0, 0], [1, 0], [1, 1], [0, 1]];
  for (let i = 0; i < sides; i++) {
    const next = [], [a, b] = directions[i], limit = limits[i];
    for (let j = 0; j < polygon.length; j++) {
      const p = polygon[j], q = polygon[(j + 1) % polygon.length];
      const dp = a * p[0] + b * p[1] - limit, dq = a * q[0] + b * q[1] - limit;
      if (dp <= 0) next.push(p);
      if ((dp < 0 && dq > 0) || (dp > 0 && dq < 0)) {
        const t = dp / (dp - dq); next.push([p[0] + (q[0] - p[0]) * t, p[1] + (q[1] - p[1]) * t]);
      }
    }
    polygon = next;
  }
  return polygon.reverse(); // Image Y points down; physical card Y points up.
}

export function writeFoliageAssets(directory, maps = buildLeafMaps()) {
  for (const [name, rgba] of [['albedo', maps.albedo], ['normal', maps.normal], ['mask', maps.mask]])
    writePNG(`${directory}/leaf-${name}.png`, maps.width, maps.height, rgba);
  writeCoverageDDS(`${directory}/leaf-albedo.dds`, maps.width, maps.height, maps.albedo, .32);
  writeMipDDS(`${directory}/leaf-normal.dds`, maps.width, maps.height, maps.normal, { raw: true });
  writeMipDDS(`${directory}/leaf-mask.dds`, maps.width, maps.height, maps.mask, { raw: true });
  return maps;
}
