// Authored RGBA8 DDS mips for cutout foliage. Coverage follows Ignacio Castano's
// Computing Alpha Mipmaps: choose a uniform alpha scale at each level against
// the original cutoff coverage. Finite texel counts/tied alpha values can only
// approximate that coverage. Inputs and file pixels are straight RGBA; tidec
// premultiplies each color mip when it imports the DDS.
import { writeFileSync } from 'node:fs';

function validate(width, height, rgba) {
  const side = n => Number.isInteger(n) && n >= 4 && n <= 16384 && (n & (n - 1)) === 0;
  if (!side(width) || !side(height)) throw new RangeError('DDS sides must be powers of two from 4 through 16384');
  if (!(rgba instanceof Uint8Array) || rgba.length !== width * height * 4)
    throw new RangeError('DDS input must contain exactly width * height * 4 RGBA bytes');
  if (rgba.length > 128 * 1024 * 1024) throw new RangeError('DDS source exceeds the 128 MiB offline image budget');
}

function smaller({ width, height, rgba }, raw) {
  const w = Math.max(1, width >> 1), h = Math.max(1, height >> 1);
  const result = new Uint8Array(w * h * 4);
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
    const samples = [];
    for (let dy = 0; dy < 2; dy++) for (let dx = 0; dx < 2; dx++)
      samples.push((Math.min(height - 1, y * 2 + dy) * width + Math.min(width - 1, x * 2 + dx)) * 4);
    const to = (y * w + x) * 4;
    const alpha = samples.reduce((sum, from) => sum + rgba[from + 3], 0);
    for (let c = 0; c < 3; c++) {
      const sum = samples.reduce((value, from) => value + rgba[from + c] * (raw ? 1 : rgba[from + 3]), 0);
      result[to + c] = Math.round(sum / (raw ? 4 : alpha || 1));
    }
    result[to + 3] = Math.round(alpha / 4);
  }
  return { width: w, height: h, rgba: result };
}

export function buildMipChain(width, height, rgba, { raw = false } = {}) {
  validate(width, height, rgba);
  const levels = [{ width, height, rgba: new Uint8Array(rgba) }];
  while (width > 1 || height > 1) {
    const next = smaller(levels.at(-1), raw);
    levels.push(next);
    ({ width, height } = next);
  }
  return levels;
}

function alphaScale(rgba, reference, coverage) {
  const histogram = new Uint32Array(256);
  for (let i = 3; i < rgba.length; i += 4) histogram[rgba[i]]++;
  const count = rgba.length / 4;
  let current = 0;
  for (let alpha = reference; alpha <= 255; alpha++) current += histogram[alpha];
  let scale = 1, error = Math.abs(current / count - coverage), distance = 0, passed = 0;
  if (coverage < error) { scale = 0; error = coverage; distance = 1; }
  for (let threshold = 255; threshold > 0; threshold--) {
    passed += histogram[threshold];
    if (!histogram[threshold]) continue;
    // Stay just inside the rounding interval: an exact half-byte boundary
    // can multiply back a fraction below it in binary floating point.
    const candidate = (reference - 0.5 + 1e-7) / threshold;
    const difference = Math.abs(passed / count - coverage);
    const change = Math.abs(candidate - 1);
    if (difference < error || (difference === error && change < distance)) {
      scale = candidate; error = difference; distance = change;
    }
  }
  return scale;
}

export function buildCoverageMips(width, height, rgba, cutoff, { tileWidth = width, tileHeight = height } = {}) {
  if (!Number.isFinite(cutoff) || cutoff <= 0 || cutoff > 1)
    throw new RangeError('alpha cutoff must be finite and greater than zero through one');
  const levels = buildMipChain(width, height, rgba);
  const tile = (n, size) => Number.isInteger(n) && n >= 1 && n <= size && (n & (n - 1)) === 0 && size % n === 0;
  if (!tile(tileWidth, width) || !tile(tileHeight, height)) throw new RangeError('coverage tile sizes must be powers of two dividing the image');
  const reference = Math.ceil(cutoff * 255);
  let passed = 0;
  for (let i = 3; i < rgba.length; i += 4) passed += Number(rgba[i] >= reference);
  const coverage = passed / (width * height);
  const columns = width / tileWidth, rows = height / tileHeight, tileCoverage = [];
  for (let ty = 0; ty < rows; ty++) for (let tx = 0; tx < columns; tx++) {
    let count = 0;
    for (let y = 0; y < tileHeight; y++) for (let x = 0; x < tileWidth; x++)
      count += Number(rgba[((ty * tileHeight + y) * width + tx * tileWidth + x) * 4 + 3] >= reference);
    tileCoverage.push(count / (tileWidth * tileHeight));
  }
  // Generate all levels from unscaled inputs first, so the correction is not
  // repeatedly fed back into the next level's filtering.
  for (let i = 1; i < levels.length; i++) {
    const level = levels[i], w = tileWidth >> i, h = tileHeight >> i;
    if (!w || !h) {
      const scale = alphaScale(level.rgba, reference, coverage);
      for (let at = 3; at < level.rgba.length; at += 4)
        level.rgba[at] = Math.min(255, Math.round(level.rgba[at] * scale));
      continue;
    }
    for (let ty = 0; ty < rows; ty++) for (let tx = 0; tx < columns; tx++) {
      const pixels = new Uint8Array(w * h * 4);
      for (let y = 0; y < h; y++) {
        const from = ((ty * h + y) * level.width + tx * w) * 4;
        pixels.set(level.rgba.subarray(from, from + w * 4), y * w * 4);
      }
      const scale = alphaScale(pixels, reference, tileCoverage[ty * columns + tx]);
      for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
        const at = ((ty * h + y) * level.width + tx * w + x) * 4 + 3;
        level.rgba[at] = Math.min(255, Math.round(level.rgba[at] * scale));
      }
    }
  }
  return levels;
}

export function encodeMipDDS(levels) {
  if (!levels.length) throw new RangeError('DDS needs at least one mip');
  const { width, height, rgba } = levels[0];
  validate(width, height, rgba);
  let w = width, h = height;
  for (const level of levels) {
    if (level.width !== w || level.height !== h || level.rgba.length !== w * h * 4)
      throw new RangeError('DDS mip dimensions must halve through 1x1');
    w = Math.max(1, w >> 1); h = Math.max(1, h >> 1);
  }
  if (levels.at(-1).width !== 1 || levels.at(-1).height !== 1
      || levels.length !== Math.log2(Math.max(width, height)) + 1)
    throw new RangeError('DDS must contain one complete mip chain through 1x1');
  const header = Buffer.alloc(128);
  header.write('DDS ', 0, 'ascii');
  for (const [at, value] of [[4, 124], [8, 0x2100f], [12, height], [16, width], [20, width * 4],
    [28, levels.length], [76, 32], [80, 0x41], [88, 32], [92, 0xff], [96, 0xff00],
    [100, 0xff0000], [104, 0xff000000], [108, 0x401008]]) header.writeUInt32LE(value, at);
  return Buffer.concat([header, ...levels.map(level => Buffer.from(level.rgba))]);
}

export function writeCoverageDDS(path, width, height, rgba, cutoff, options) {
  const levels = buildCoverageMips(width, height, rgba, cutoff, options);
  writeFileSync(path, encodeMipDDS(levels));
  return levels;
}

export function writeMipDDS(path, width, height, rgba, options) {
  const levels = buildMipChain(width, height, rgba, options);
  writeFileSync(path, encodeMipDDS(levels));
  return levels;
}
