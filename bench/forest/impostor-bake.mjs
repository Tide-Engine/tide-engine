// Rest-pose hemisphere impostors baked from the forest's actual source meshes.
// Uses Node 24 built-ins only. Runtime sampling lives in Render3D/impostor.tide.
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { resolve, dirname, basename } from 'node:path';
import { pathToFileURL } from 'node:url';
import { createHash } from 'node:crypto';
import { deflateSync, inflateSync } from 'node:zlib';

const TAU = Math.PI * 2;
const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
const subtract = (a, b) => a.map((x, i) => x - b[i]);
const normalized = a => { const length = Math.hypot(...a); return length > 1e-12 ? a.map(x => x / length) : [0, 1, 0]; };
const clamp = (x, low = 0, high = 1) => Math.max(low, Math.min(high, x));
const linear = x => x <= 0.04045 ? x / 12.92 : ((x + 0.055) / 1.055) ** 2.4;
const srgb = x => x <= 0.0031308 ? x * 12.92 : 1.055 * x ** (1 / 2.4) - 0.055;
const byte = x => Math.round(clamp(x) * 255);

export const atlasLayout = Object.freeze({ columns: 8, rows: 4, elevations: 3, tile: 128, gutter: 2 });

function crc32(bytes) {
  let crc = 0xffffffff;
  for (const value of bytes) {
    crc ^= value;
    for (let i = 0; i < 8; i++) crc = (crc >>> 1) ^ (crc & 1 ? 0xedb88320 : 0);
  }
  return (crc ^ 0xffffffff) >>> 0;
}

function pngChunk(kind, data) {
  const name = Buffer.from(kind), chunk = Buffer.alloc(data.length + 12);
  chunk.writeUInt32BE(data.length, 0);
  name.copy(chunk, 4); data.copy(chunk, 8);
  chunk.writeUInt32BE(crc32(chunk.subarray(4, -4)), chunk.length - 4);
  return chunk;
}

export function encodePNG(width, height, rgba) {
  if (!Number.isSafeInteger(width) || !Number.isSafeInteger(height) || width < 1 || height < 1
      || width > 8192 || height > 8192 || rgba.length !== width * height * 4) throw new Error('Invalid PNG extent');
  const header = Buffer.alloc(13);
  header.writeUInt32BE(width, 0); header.writeUInt32BE(height, 4); header[8] = 8; header[9] = 6;
  const rows = Buffer.alloc(height * (width * 4 + 1));
  for (let y = 0; y < height; y++) Buffer.from(rgba.buffer, rgba.byteOffset + y * width * 4, width * 4).copy(rows, y * (width * 4 + 1) + 1);
  return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), pngChunk('IHDR', header),
    pngChunk('IDAT', deflateSync(rows, { level: 9 })), pngChunk('IEND', Buffer.alloc(0))]);
}

// The authored foliage source is an ordinary non-interlaced 8-bit RGB/RGBA PNG.
// All PNG row filters are supported; malformed lengths/CRC/critical chunks fail.
export function decodePNG(bytes) {
  const data = Buffer.from(bytes);
  if (data.length < 33 || !data.subarray(0, 8).equals(Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]))) throw new Error('Not a PNG');
  const packed = [];
  let at = 8, width = 0, height = 0, channels = 0, ended = false;
  while (at + 12 <= data.length) {
    const length = data.readUInt32BE(at), kind = data.toString('ascii', at + 4, at + 8);
    if (length > data.length - at - 12) throw new Error('Truncated PNG chunk');
    const chunk = data.subarray(at + 8, at + 8 + length);
    if (crc32(data.subarray(at + 4, at + 8 + length)) !== data.readUInt32BE(at + 8 + length)) throw new Error('PNG CRC mismatch');
    if (kind === 'IHDR') {
      if (at !== 8 || length !== 13) throw new Error('Invalid PNG header');
      width = chunk.readUInt32BE(0); height = chunk.readUInt32BE(4);
      channels = chunk[9] === 6 ? 4 : chunk[9] === 2 ? 3 : 0;
      if (!channels || chunk[8] !== 8 || chunk[10] || chunk[11] || chunk[12]
          || !width || !height || width > 8192 || height > 8192) throw new Error('Expected bounded, non-interlaced 8-bit RGB/RGBA PNG');
    } else if (kind === 'IDAT') packed.push(chunk);
    else if (kind === 'IEND') { if (length) throw new Error('Invalid PNG end'); ended = true; break; }
    else if (kind[0] === kind[0].toUpperCase()) throw new Error(`Unsupported critical PNG chunk ${kind}`);
    at += length + 12;
  }
  if (!ended || !channels || !packed.length) throw new Error('Incomplete PNG');
  const stride = width * channels;
  const filtered = inflateSync(Buffer.concat(packed), { maxOutputLength: height * (stride + 1) });
  if (filtered.length !== height * (stride + 1)) throw new Error('Invalid PNG row data');
  const decoded = Buffer.alloc(width * height * channels);
  for (let y = 0; y < height; y++) {
    const filter = filtered[y * (stride + 1)];
    if (filter > 4) throw new Error('Invalid PNG row filter');
    for (let x = 0; x < stride; x++) {
      const a = x >= channels ? decoded[y * stride + x - channels] : 0;
      const b = y ? decoded[(y - 1) * stride + x] : 0;
      const c = y && x >= channels ? decoded[(y - 1) * stride + x - channels] : 0;
      const p = a + b - c, pa = Math.abs(p - a), pb = Math.abs(p - b), pc = Math.abs(p - c);
      const prediction = filter === 0 ? 0 : filter === 1 ? a : filter === 2 ? b : filter === 3 ? Math.floor((a + b) / 2)
        : pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
      decoded[y * stride + x] = filtered[y * (stride + 1) + x + 1] + prediction;
    }
  }
  const rgba = new Uint8Array(width * height * 4);
  for (let i = 0; i < width * height; i++) {
    rgba.set(decoded.subarray(i * channels, i * channels + 3), i * 4);
    rgba[i * 4 + 3] = channels === 4 ? decoded[i * channels + 3] : 255;
  }
  return { width, height, rgba };
}

function objIndex(word, length) {
  const value = Number(word);
  if (!Number.isSafeInteger(value) || value === 0) throw new Error(`Invalid OBJ index ${word}`);
  const at = value > 0 ? value - 1 : length + value;
  if (at < 0 || at >= length) throw new Error(`OBJ index ${word} is out of range`);
  return at;
}

// Match Tide's OBJ importer: reflect X, flip V, and reverse the winding.
// Authored vertex RGB belongs to wind data and is deliberately not display tint.
export function parseOBJ(text, { leaf = false } = {}) {
  const positions = [], normals = [], uvs = [], triangles = [];
  for (const line of text.split(/\r?\n/)) {
    const words = line.trim().split(/\s+/), kind = words.shift();
    if (!kind || kind.startsWith('#')) continue;
    if (kind === 'v' || kind === 'vn' || kind === 'vt') {
      const count = kind === 'vt' ? 2 : 3, numbers = words.slice(0, count).map(Number);
      if (numbers.length !== count || numbers.some(x => !Number.isFinite(x))) throw new Error(`Invalid OBJ ${kind}`);
      if (kind === 'v') positions.push([-numbers[0], numbers[1], numbers[2]]);
      else if (kind === 'vn') normals.push(normalized([-numbers[0], numbers[1], numbers[2]]));
      else uvs.push([numbers[0], 1 - numbers[1]]);
    } else if (kind === 'f') {
      if (words.length < 3 || words.length > 256) throw new Error('OBJ face needs 3..256 corners');
      const face = words.map(word => {
        const [v, t, n] = word.split('/');
        return { position: positions[objIndex(v, positions.length)],
          uv: t ? uvs[objIndex(t, uvs.length)] : [0, 0], normal: n ? normals[objIndex(n, normals.length)] : null };
      });
      for (let i = 1; i < face.length - 1; i++) {
        const vertices = [face[0], face[i + 1], face[i]], geometric = normalized(cross(subtract(vertices[1].position, vertices[0].position), subtract(vertices[2].position, vertices[0].position)));
        triangles.push({ leaf, vertices: vertices.map(v => ({ ...v, normal: v.normal ?? geometric })) });
      }
    }
  }
  if (!triangles.length) throw new Error('OBJ has no triangles');
  return triangles;
}

export function viewBasis(frame) {
  if (!Number.isInteger(frame) || frame < 0 || frame >= 24) throw new Error('Expected one of 24 hemisphere frames');
  const azimuth = frame % 8 * TAU / 8, elevation = Math.floor(frame / 8) * Math.PI / 4;
  const right = [Math.cos(azimuth), 0, -Math.sin(azimuth)];
  const toward = [Math.sin(azimuth) * Math.cos(elevation), Math.sin(elevation), Math.cos(azimuth) * Math.cos(elevation)];
  return { right, up: cross(toward, right), toward, azimuth, elevation };
}

function sample(image, u, v, color = false) {
  const x = clamp(u) * image.width - 0.5, y = clamp(v) * image.height - 0.5;
  const ix = Math.floor(x), iy = Math.floor(y), fx = x - ix, fy = y - iy;
  const out = [0, 0, 0, 0];
  for (let dy = 0; dy < 2; dy++) for (let dx = 0; dx < 2; dx++) {
    const at = (clamp(iy + dy, 0, image.height - 1) * image.width + clamp(ix + dx, 0, image.width - 1)) * 4;
    const weight = (dx ? fx : 1 - fx) * (dy ? fy : 1 - fy);
    const alpha = image.rgba[at + 3] / 255;
    for (let c = 0; c < 4; c++) out[c] += image.rgba[at + c] / 255 * weight * (color && c < 3 ? alpha : 1);
  }
  if (color && out[3] > 0) for (let c = 0; c < 3; c++) out[c] /= out[3];
  return out;
}

export function meshBounds(triangles, center) {
  const minimum = [Infinity, Infinity, Infinity], maximum = [-Infinity, -Infinity, -Infinity];
  for (const triangle of triangles) for (const { position } of triangle.vertices) for (let c = 0; c < 3; c++) {
    minimum[c] = Math.min(minimum[c], position[c]); maximum[c] = Math.max(maximum[c], position[c]);
  }
  const middle = center ?? minimum.map((x, i) => (x + maximum[i]) * 0.5);
  let radius = 0;
  for (const triangle of triangles) for (const { position } of triangle.vertices) radius = Math.max(radius, Math.hypot(...subtract(position, middle)));
  return { minimum, maximum, center: middle, span: radius * 2.05, depth: radius * 1.025 };
}

function tangentBasis(vertices) {
  const e1 = subtract(vertices[1].position, vertices[0].position), e2 = subtract(vertices[2].position, vertices[0].position);
  const a = vertices[1].uv.map((x, i) => x - vertices[0].uv[i]), b = vertices[2].uv.map((x, i) => x - vertices[0].uv[i]);
  const determinant = a[0] * b[1] - a[1] * b[0];
  if (Math.abs(determinant) < 1e-10) return null;
  const t = e1.map((x, i) => (x * b[1] - e2[i] * a[1]) / determinant);
  const bitangent = e2.map((x, i) => (x * a[0] - e1[i] * b[0]) / determinant);
  return { tangent: normalized(t), bitangent: normalized(bitangent) };
}

function rasterView(triangles, leafImage, normalImage, maskImage, options, frame) {
  const { right, up, toward } = viewBasis(frame), size = options.inner * options.supersample;
  const depth = new Float32Array(size * size).fill(-Infinity), colors = new Float32Array(size * size * 3), normals = new Float32Array(size * size * 3), masks = new Float32Array(size * size * 4);
  let accepted = 0;
  for (const triangle of triangles) {
    const vertices = triangle.vertices, geometric = cross(subtract(vertices[1].position, vertices[0].position), subtract(vertices[2].position, vertices[0].position));
    if (!triangle.leaf && dot(geometric, toward) <= 0) continue;
    const projected = vertices.map(v => {
      const p = subtract(v.position, options.center);
      return [(0.5 + dot(p, right) / options.span) * size, (0.5 - dot(p, up) / options.span) * size, dot(p, toward)];
    });
    const [a, b, c] = projected, denominator = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1]);
    if (Math.abs(denominator) < 1e-9) continue;
    const minX = Math.max(0, Math.floor(Math.min(a[0], b[0], c[0]))), maxX = Math.min(size - 1, Math.ceil(Math.max(a[0], b[0], c[0])));
    const minY = Math.max(0, Math.floor(Math.min(a[1], b[1], c[1]))), maxY = Math.min(size - 1, Math.ceil(Math.max(a[1], b[1], c[1])));
    const basis = triangle.leaf && normalImage ? tangentBasis(vertices) : null;
    const tint = triangle.leaf ? options.leafColor : options.barkColor;
    for (let y = minY; y <= maxY; y++) for (let x = minX; x <= maxX; x++) {
      const wa = ((b[1] - c[1]) * (x + 0.5 - c[0]) + (c[0] - b[0]) * (y + 0.5 - c[1])) / denominator;
      const wb = ((c[1] - a[1]) * (x + 0.5 - c[0]) + (a[0] - c[0]) * (y + 0.5 - c[1])) / denominator;
      const wc = 1 - wa - wb;
      if (wa < -1e-7 || wb < -1e-7 || wc < -1e-7) continue;
      const z = wa * a[2] + wb * b[2] + wc * c[2], at = y * size + x;
      if (z <= depth[at]) continue;
      const uv = [0, 1].map(i => wa * vertices[0].uv[i] + wb * vertices[1].uv[i] + wc * vertices[2].uv[i]);
      const texel = triangle.leaf ? sample(leafImage, ...uv, true) : [1, 1, 1, 1];
      if (texel[3] < options.cutoff) continue;
      let normal = normalized([0, 1, 2].map(i => wa * vertices[0].normal[i] + wb * vertices[1].normal[i] + wc * vertices[2].normal[i]));
      const back = triangle.leaf && dot(normal, toward) < 0;
      if (basis) {
        const tangent = normalized(basis.tangent.map((x, i) => x - normal[i] * dot(basis.tangent, normal)));
        const handed = dot(cross(normal, tangent), basis.bitangent) < 0 ? -1 : 1;
        const bitangent = cross(normal, tangent).map(x => x * handed * (back ? -1 : 1)), mapped = sample(normalImage, ...uv).slice(0, 3).map(x => x * 2 - 1);
        if (back) normal = normal.map(x => -x);
        normal = normalized(normal.map((x, i) => tangent[i] * mapped[0] + bitangent[i] * mapped[1] + x * mapped[2]));
      } else if (back) normal = normal.map(x => -x);
      // Two-sided leaves use the source material's view-facing tangent frame.
      // The resulting vector is still encoded in source MODEL coordinates.
      depth[at] = z;
      for (let i = 0; i < 3; i++) { colors[at * 3 + i] = linear(texel[i]) * linear(tint[i]); normals[at * 3 + i] = normal[i]; }
      const mask = triangle.leaf ? maskImage ? sample(maskImage, ...uv) : [1, 1, 0, 1] : [1, 1, 0, 0];
      for (let i = 0; i < 4; i++) masks[at * 4 + i] = mask[i];
      accepted++;
    }
  }
  const albedo = new Uint8Array(options.inner * options.inner * 4), normalDepth = new Uint8Array(albedo.length), mask = new Uint8Array(albedo.length);
  let visible = 0;
  for (let y = 0; y < options.inner; y++) for (let x = 0; x < options.inner; x++) {
    let count = 0, z = 0;
    const color = [0, 0, 0], normal = [0, 0, 0], orm = [0, 0, 0, 0];
    for (let sy = 0; sy < options.supersample; sy++) for (let sx = 0; sx < options.supersample; sx++) {
      const at = (y * options.supersample + sy) * size + x * options.supersample + sx;
      if (!Number.isFinite(depth[at])) continue;
      count++; z += depth[at];
      for (let i = 0; i < 3; i++) { color[i] += colors[at * 3 + i]; normal[i] += normals[at * 3 + i]; }
      for (let i = 0; i < 4; i++) orm[i] += masks[at * 4 + i];
    }
    const at = (y * options.inner + x) * 4, n = count ? normalized(normal) : [0, 1, 0];
    albedo[at + 3] = byte(count / (options.supersample ** 2));
    normalDepth[at + 3] = count ? byte(0.5 + z / count / (options.depth * 2)) : 128;
    for (let i = 0; i < 3; i++) { albedo[at + i] = count ? byte(srgb(color[i] / count)) : 0; normalDepth[at + i] = byte(n[i] * 0.5 + 0.5); }
    for (let i = 0; i < 4; i++) mask[at + i] = byte(count ? orm[i] / count : i < 2 ? 1 : 0);
    if (count) visible++;
  }
  return { albedo, normalDepth, mask, visible, accepted };
}

// Spread only RGB/depth into empty texels; alpha remains the true silhouette.
// Work within one tile so neighboring view images never contaminate each other.
function dilate(albedo, normalDepth, mask, size, passes = 6) {
  let covered = Uint8Array.from({ length: size * size }, (_, i) => albedo[i * 4 + 3] > 0 ? 1 : 0);
  for (let pass = 0; pass < passes; pass++) {
    const next = covered.slice();
    for (let y = 0; y < size; y++) for (let x = 0; x < size; x++) {
      const pixel = y * size + x;
      if (covered[pixel]) continue;
      const neighbor = [[x - 1, y], [x + 1, y], [x, y - 1], [x, y + 1]].find(([nx, ny]) => nx >= 0 && ny >= 0 && nx < size && ny < size && covered[ny * size + nx]);
      if (!neighbor) continue;
      const from = (neighbor[1] * size + neighbor[0]) * 4, at = pixel * 4;
      albedo.set(albedo.subarray(from, from + 3), at); normalDepth.set(normalDepth.subarray(from, from + 4), at);
      mask.set(mask.subarray(from, from + 4), at);
      next[pixel] = 1;
    }
    covered = next;
  }
}

export function bakeImpostor(triangles, leafImage, options = {}) {
  const layout = { ...atlasLayout, ...options.layout };
  if (layout.columns !== 8 || layout.rows !== 4 || layout.elevations !== 3 || !Number.isInteger(layout.tile)
      || layout.tile < 8 || layout.tile > 512 || (layout.tile & (layout.tile - 1)) || layout.gutter !== 2) throw new Error('Expected 8x4 POT tiles with two-pixel gutters');
  const bounds = meshBounds(triangles, options.center);
  const settings = { ...bounds, ...options, inner: layout.tile - layout.gutter * 2, supersample: options.supersample ?? 2,
    cutoff: options.cutoff ?? 0.32, barkColor: options.barkColor ?? [0.3, 0.22, 0.14], leafColor: options.leafColor ?? [1, 1, 1] };
  if (![settings.span, settings.depth].every(x => Number.isFinite(x) && x > 0)
      || !Array.isArray(settings.center) || settings.center.length !== 3 || settings.center.some(x => !Number.isFinite(x))
      || ![1, 2, 4].includes(settings.supersample) || !Number.isFinite(settings.cutoff) || settings.cutoff <= 0 || settings.cutoff >= 1)
    throw new Error('Invalid bake settings');
  for (const color of [settings.barkColor, settings.leafColor]) if (color.length !== 3 || color.some(x => !Number.isFinite(x) || x < 0 || x > 1)) throw new Error('Expected RGB tint in 0..1');
  for (const triangle of triangles) for (const vertex of triangle.vertices) {
    const p = subtract(vertex.position, settings.center);
    if (Math.hypot(...p) > settings.span * 0.5 + 1e-6 || Math.hypot(...p) > settings.depth + 1e-6) throw new Error('Bake bounds do not contain source geometry');
  }
  const width = layout.columns * layout.tile, height = layout.rows * layout.tile;
  const albedo = new Uint8Array(width * height * 4), normalDepth = new Uint8Array(albedo.length), mask = new Uint8Array(albedo.length), views = [];
  for (let frame = 0; frame < 24; frame++) {
    const image = rasterView(triangles, leafImage, options.normalImage, options.maskImage, settings, frame);
    dilate(image.albedo, image.normalDepth, image.mask, settings.inner);
    const tileX = frame % 8 * layout.tile, tileY = Math.floor(frame / 8) * layout.tile;
    for (let y = 0; y < layout.tile; y++) for (let x = 0; x < layout.tile; x++) {
      const from = (clamp(y - layout.gutter, 0, settings.inner - 1) * settings.inner + clamp(x - layout.gutter, 0, settings.inner - 1)) * 4;
      const at = ((tileY + y) * width + tileX + x) * 4;
      albedo.set(image.albedo.subarray(from, from + 4), at); normalDepth.set(image.normalDepth.subarray(from, from + 4), at);
      mask.set(image.mask.subarray(from, from + 4), at);
    }
    const basis = viewBasis(frame);
    views.push({ frame, azimuth: basis.azimuth, elevation: basis.elevation, visiblePixels: image.visible,
      coverage: image.visible / (settings.inner ** 2), sourceFragments: image.accepted });
  }
  return { width, height, albedo, normalDepth, mask, metadata: { version: 1, layout: 'azimuth-elevation-hemisphere', ...layout,
    inner: settings.inner, width, height, viewCount: 24, center: settings.center, span: settings.span, depth: settings.depth,
    sourceBounds: { minimum: bounds.minimum, maximum: bounds.maximum }, cutoff: settings.cutoff,
    supersample: settings.supersample, barkColor: settings.barkColor, leafColor: settings.leafColor,
    coordinates: 'Tide model space: OBJ X reflected, V flipped, positive depth toward bake camera',
    normalEncoding: 'raw RGB = model normal * .5 + .5; raw A = signed camera depth / (2 * depth) + .5',
    maskEncoding: 'raw RGBA = occlusion, roughness multiplier, metallic multiplier, optical transmittance multiplier; bark A=0',
    albedoEncoding: 'straight display RGB with silhouette alpha; author vertex RGB ignored as wind metadata',
    limitations: ['Rest pose; branch and individual leaf animation are not baked.',
      'One nearest visible surface per sample; blended views and depth-plane parallax approximate disocclusion.',
      'No baked lighting/shadows or volumetric scattering.',
      'Per-view alpha mip coverage is kept until each tile is one texel; lower tail mips mix views.',
      'Shrinking mip gutters require footprint-aware runtime view clamps.'], views } };
}

export function cardOBJ(center, span) {
  // Source X is reflected by the importer; normals/winding below match its rule.
  const h = span / 2, [x, y, z] = [-center[0], center[1], center[2]];
  return '# Rest-pose impostor card; generated by impostor-bake.mjs\n'
    + [[x - h, y - h, z], [x + h, y - h, z], [x + h, y + h, z], [x - h, y + h, z]].map(p => `v ${p.join(' ')}\n`).join('')
    + 'vt 0 1\nvt 1 1\nvt 1 0\nvt 0 0\nvn 0 0 1\nf 1/1/1 2/2/1 3/3/1\nf 1/1/1 3/3/1 4/4/1\n';
}

async function main(args) {
  const options = { bark: 'bench/forest/bark-0.obj', leaves: 'bench/forest/leaves-0.obj',
    albedo: 'bench/forest/leaf-albedo.png', normal: 'bench/forest/leaf-normal.png', mask: 'bench/forest/leaf-mask.png', output: 'bench/forest/impostor', dds: true };
  for (let i = 0; i < args.length; i++) {
    const name = args[i];
    if (name === '--no-dds') { options.dds = false; continue; }
    if (!['--bark', '--leaves', '--albedo', '--normal', '--mask', '--output', '--center', '--span', '--depth', '--cutoff', '--supersample', '--bark-color', '--leaf-color'].includes(name) || !args[i + 1])
      throw new Error('Usage: node impostor-bake.mjs [--bark OBJ] [--leaves OBJ] [--albedo PNG] [--normal PNG|none] [--mask PNG|none] [--output PREFIX] [--center x,y,z] [--span n] [--depth n] [--cutoff n] [--supersample 1|2|4] [--bark-color r,g,b] [--leaf-color r,g,b] [--no-dds]');
    const key = name.slice(2).replace(/-([a-z])/g, (_, x) => x.toUpperCase()), value = args[++i];
    options[key] = ['center', 'barkColor', 'leafColor'].includes(key) ? value.split(',').map(Number)
      : ['span', 'depth', 'cutoff', 'supersample'].includes(key) ? Number(value) : value;
  }
  const barkBytes = readFileSync(options.bark), leavesBytes = readFileSync(options.leaves), albedoBytes = readFileSync(options.albedo);
  const normalBytes = options.normal === 'none' ? null : readFileSync(options.normal);
  const maskBytes = options.mask === 'none' ? null : readFileSync(options.mask);
  const triangles = [...parseOBJ(barkBytes.toString()), ...parseOBJ(leavesBytes.toString(), { leaf: true })];
  const baked = bakeImpostor(triangles, decodePNG(albedoBytes), { ...options, normalImage: normalBytes ? decodePNG(normalBytes) : null,
    maskImage: maskBytes ? decodePNG(maskBytes) : null });
  baked.metadata.sources = [options.bark, options.leaves, options.albedo, ...(normalBytes ? [options.normal] : []), ...(maskBytes ? [options.mask] : [])].map(path => ({ path,
    sha256: createHash('sha256').update(readFileSync(path)).digest('hex') }));
  baked.metadata.sourceTriangles = { bark: parseOBJ(barkBytes.toString()).length, leaves: parseOBJ(leavesBytes.toString(), { leaf: true }).length };
  const prefix = resolve(options.output);
  mkdirSync(dirname(prefix), { recursive: true });
  writeFileSync(`${prefix}-albedo.png`, encodePNG(baked.width, baked.height, baked.albedo));
  writeFileSync(`${prefix}-normal-depth.png`, encodePNG(baked.width, baked.height, baked.normalDepth));
  writeFileSync(`${prefix}-mask.png`, encodePNG(baked.width, baked.height, baked.mask));
  writeFileSync(`${prefix}-card.obj`, cardOBJ(baked.metadata.center, baked.metadata.span));
  const number = n => { const text = Number(n.toPrecision(10)).toString(); return /[.e]/.test(text) ? text : `${text}.0`; };
  writeFileSync(`${prefix}-settings.tide`, '// Generated from actual source mesh bounds by impostor-bake.mjs.\n'
    + 'Render3D.ImpostorSettings ForestImpostorSettings()\n{\n'
    + `    return Render3D.ImpostorSettings { enabled = true, center = float3(${baked.metadata.center.map(number).join(', ')}),\n`
    + `        span = ${number(baked.metadata.span)}, depth = ${number(baked.metadata.depth)}, parallax = 1 };\n}\n`);
  if (options.dds) {
    const { writeCoverageDDS, writeMipDDS } = await import('./alpha-mips.mjs');
    writeCoverageDDS(`${prefix}-albedo.dds`, baked.width, baked.height, baked.albedo, baked.metadata.cutoff,
      { tileWidth: atlasLayout.tile, tileHeight: atlasLayout.tile });
    writeMipDDS(`${prefix}-normal-depth.dds`, baked.width, baked.height, baked.normalDepth, { raw: true });
    writeMipDDS(`${prefix}-mask.dds`, baked.width, baked.height, baked.mask, { raw: true });
  }
  writeFileSync(`${prefix}.json`, JSON.stringify(baked.metadata, null, 2) + '\n');
  console.log(`${basename(prefix)}: ${baked.metadata.sourceTriangles.bark + baked.metadata.sourceTriangles.leaves} source triangles ->24 views, ${baked.width}x${baked.height}, center=${baked.metadata.center.join(',')} span=${baked.metadata.span} depth=${baked.metadata.depth}`);
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  try { await main(process.argv.slice(2)); }
  catch (error) { console.error(error.message); process.exitCode = 1; }
}
