import assert from 'node:assert/strict';
import { inflateSync } from 'node:zlib';

// Decode the RGB/RGBA screenshots produced by the native host and Chrome.
// Compare scene pixels only: labels, menus, frame counters and notifications
// cannot make a missing material change pass.
export function materialScene(png) {
    let width, height, channels;
    const chunks = [];
    for (let at = 8; at < png.length;) {
        const length = png.readUInt32BE(at), type = png.toString('ascii', at + 4, at + 8);
        if (type === 'IHDR') {
            width = png.readUInt32BE(at + 8); height = png.readUInt32BE(at + 12);
            assert.equal(png[at + 16], 8);
            channels = png[at + 17] === 2 ? 3 : png[at + 17] === 6 ? 4 : 0;
            assert.ok(channels, '8-bit RGB/RGBA screenshot');
            assert.equal(png[at + 20], 0, 'noninterlaced screenshot');
        } else if (type === 'IDAT') chunks.push(png.subarray(at + 8, at + 8 + length));
        at += length + 12;
    }
    assert.equal(width, 1280); assert.equal(height, 800);
    const packed = inflateSync(Buffer.concat(chunks)), stride = width * channels;
    const result = Buffer.alloc(920 * 480 * 3);
    let previous = Buffer.alloc(stride), output = 0;
    const paeth = (a, b, c) => {
        const p = a + b - c, pa = Math.abs(p - a), pb = Math.abs(p - b), pc = Math.abs(p - c);
        return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
    };
    for (let y = 0; y < height; y++) {
        const at = y * (stride + 1), filter = packed[at], row = Buffer.alloc(stride);
        assert.ok(filter <= 4, 'valid PNG filter');
        for (let x = 0; x < stride; x++) {
            const a = x >= channels ? row[x - channels] : 0, b = previous[x], c = x >= channels ? previous[x - channels] : 0;
            const predictor = filter === 0 ? 0 : filter === 1 ? a : filter === 2 ? b : filter === 3 ? Math.floor((a + b) / 2) : paeth(a, b, c);
            row[x] = (packed[at + 1 + x] + predictor) & 255;
        }
        if (y >= 180 && y < 660) for (let x = 180; x < 1100; x++) {
            result[output++] = row[x * channels];
            result[output++] = row[x * channels + 1];
            result[output++] = row[x * channels + 2];
        }
        previous = row;
    }
    return result;
}

export function checkMaterials(images, label) {
    const scenes = images.map(materialScene), baseline = scenes[0];
    assert.equal(scenes.length, 7, 'original, five looks, restored original');
    assert.equal(Buffer.compare(baseline, scenes[6]), 0, `${label}: original appearance restores exactly`);
    assert.ok(new Set(baseline).size > 100, `${label}: baseline contains a rendered scene`);
    const differences = [];
    for (let i = 0; i < 6; i++) for (let j = i + 1; j < 6; j++) {
        let pixels = 0, total = 0;
        for (let at = 0; at < baseline.length; at += 3) {
            const delta = Math.max(Math.abs(scenes[i][at] - scenes[j][at]),
                Math.abs(scenes[i][at + 1] - scenes[j][at + 1]), Math.abs(scenes[i][at + 2] - scenes[j][at + 2]));
            if (delta >= 4) pixels++;
            total += delta;
        }
        // A single text glyph or one changed trim pixel is insufficient.
        assert.ok(pixels > 1000 && total / (baseline.length / 3) > 0.1,
            `${label}: appearances ${i - 1}/${j - 1} visibly differ (${pixels} pixels, mean ${total / (baseline.length / 3)})`);
        differences.push({ first: i - 1, second: j - 1, pixels, mean: total / (baseline.length / 3) });
    }
    return differences;
}
