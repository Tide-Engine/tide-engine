// Writes tiny PSDs from Adobe's file-format specification, without Photoshop:
// `node compiler/tests/e2e/asset_psds_make.mjs`. Files are checked in so the
// build and the tests need no image application. Expected pixels are worked
// out separately in asset_psds_test.c and psd_files_test.c.

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { deflateSync } from 'node:zlib';

const out = path.join(path.dirname(fileURLToPath(import.meta.url)), 'assets', 'psds');
fs.mkdirSync(path.join(out, 'bad'), { recursive: true });
fs.mkdirSync(path.join(out, 'cases'), { recursive: true });
const be16 = v => { const b = Buffer.alloc(2); b.writeUInt16BE(v & 65535); return b; };
const be32 = v => { const b = Buffer.alloc(4); b.writeUInt32BE(v >>> 0); return b; };
const block = b => Buffer.concat([be32(b.length), b]);

function packBits(bytes) {
    const parts = [Buffer.from([128])]; // -128 is a no-op, including at a row's beginning.
    let at = 0;
    while (at < bytes.length) {
        let run = 1;
        while (at + run < bytes.length && bytes[at + run] === bytes[at] && run < 128) run++;
        if (run >= 2) {
            parts.push(Buffer.from([257 - run, bytes[at]]));
            at += run;
        } else {
            const first = at++;
            while (at < bytes.length && at - first < 128 && !(at + 1 < bytes.length && bytes[at] === bytes[at + 1])) at++;
            parts.push(Buffer.from([at - first - 1]), bytes.subarray(first, at));
        }
    }
    return Buffer.concat(parts);
}

function plane(values, depth) {
    return depth === 8 ? Buffer.from(values) : Buffer.concat(values.map(be16));
}

function layerInfo(width, height, depth, channels, negative = true) {
    // A real layer record and its four raw channel streams. The negative count
    // declares that the composite's fourth channel is merged transparency.
    const data = channels.slice(0, 4).map(values => Buffer.concat([be16(0), plane(values, depth)]));
    const record = Buffer.concat([
        be32(0), be32(0), be32(height), be32(width), be16(data.length),
        ...data.map((b, i) => Buffer.concat([be16(i === 3 ? -1 : i), be32(b.length)])),
        Buffer.from('8BIMnorm'), Buffer.from([255, 0, 0, 0]),
        block(Buffer.concat([be32(0), be32(0), Buffer.from([1, 65, 0, 0])])),
    ]);
    let bytes = Buffer.concat([be16(negative ? -1 : 1), record, ...data]);
    if (bytes.length % 2) bytes = Buffer.concat([bytes, Buffer.alloc(1)]);
    return bytes;
}

function resource(id, data) {
    const padded = data.length % 2 ? Buffer.concat([data, Buffer.alloc(1)]) : data;
    return Buffer.concat([Buffer.from('8BIM'), be16(id), Buffer.alloc(2), be32(data.length), padded]);
}

function psd({ width = 2, height = 2, depth = 8, channels, compression = 0, transparency = false, layer16 = false,
               version = 1, mode = 3, realComposite = true, alphaMarker, marker64 = false, positiveLayer = false,
               noGlobalMask = false, truncatedGlobalMask = 0 }) {
    const header = Buffer.concat([Buffer.from('8BPS'), be16(version), Buffer.alloc(6), be16(channels.length),
                                  be32(height), be32(width), be16(depth), be16(mode)]);
    const resources = realComposite ? resource(1000, Buffer.from([1, 2, 3]))
        : resource(1057, Buffer.concat([be32(1), Buffer.from([0]), be32(0), be32(0), be32(1)]));
    let layers = Buffer.alloc(0);
    if (transparency) {
        const info = layerInfo(width, height, depth, channels, !positiveLayer);
        // PSD v1 uses a four-byte length for either tag signature. An Mt16
        // marker placed before a positive Lr16 count must still identify alpha.
        const marker = alphaMarker ? Buffer.concat([Buffer.from(marker64 ? '8B64' : '8BIM'),
                                                     Buffer.from(alphaMarker), be32(0)]) : Buffer.alloc(0);
        if (layer16) {
            // Photoshop's 16-bit layer information lives in the Lr16 block.
            const tagged = Buffer.concat([Buffer.from('8BIMLr16'), block(info)]);
            layers = Buffer.concat([be32(0), be32(0), marker, tagged]);
        } else layers = Buffer.concat([block(info), noGlobalMask ? Buffer.alloc(0)
            : truncatedGlobalMask ? Buffer.alloc(truncatedGlobalMask) : be32(0), marker]);
    }
    const planes = channels.map(values => plane(values, depth));
    let body;
    if (compression === 0) body = Buffer.concat(planes);
    else if (compression === 1) {
        const rows = planes.flatMap(p => Array.from({ length: height }, (_, y) =>
            packBits(p.subarray(y * width * (depth / 8), (y + 1) * width * (depth / 8)))));
        body = Buffer.concat([...rows.map(row => be16(row.length)), ...rows]);
    } else {
        const predicted = compression === 3 ? planes.map(p => {
            const b = Buffer.from(p);
            for (let y = 0; y < height; y++) {
                for (let x = width - 1; x > 0; x--) {
                    const at = (y * width + x) * (depth / 8);
                    if (depth === 8) b[at] = (p[at] - p[at - 1]) & 255;
                    else b.writeUInt16BE((p.readUInt16BE(at) - p.readUInt16BE(at - 2)) & 65535, at);
                }
            }
            return b;
        }) : planes;
        body = deflateSync(Buffer.concat(predicted));
    }
    return Buffer.concat([header, be32(0), block(resources), block(layers), be16(compression), body]);
}

const rgb8 = [[255, 0, 0, 51], [0, 255, 0, 102], [0, 0, 255, 153]];
// Values not divisible by 257 pin the full 16-bit sample, rather than only its high byte.
const rgb16 = [[0, 1, 12345, 65535], [65535, 32768, 45678, 0], [1, 65534, 20000, 32767]];
// RGB composite values have Photoshop's white matte. The intended straight
// colors are red, (51,102,153), clear black, green, blue, and (51,102,153).
const rgba8 = [[255, 187, 255, 85, 85, 119], [170, 204, 255, 255, 85, 153],
               [170, 221, 255, 85, 255, 187], [85, 85, 0, 170, 170, 170]];
const write = (name, options) => { const bytes = psd(options); fs.writeFileSync(path.join(out, name), bytes); return bytes; };
const raw = write('raw8.psd', { channels: rgb8 });
write('raw16.psd', { channels: rgb16, depth: 16 });
write('rle16.psd', { channels: rgb16, depth: 16, compression: 1 });
const rle = write('rle8.psd', { channels: rgba8, width: 3, transparency: true, compression: 1 });
write('spot8.psd', { channels: [...rgb8, [0, 85, 170, 255], [255, 170, 85, 0]] });
const zip = write('zip8.psd', { channels: rgb8, compression: 2 });
write('zip16.psd', { channels: rgb16, depth: 16, compression: 2 });
write('pred8.psd', { channels: rgb8, compression: 3 });
write('pred16.psd', { channels: rgb16, depth: 16, compression: 3 });
write('lr16.psd', { channels: rgba8.map(p => p.map(v => v * 257)), width: 3, depth: 16,
                      transparency: true, layer16: true, compression: 2 });
write('cases/tag8.psd', { channels: rgba8, width: 3, transparency: true, positiveLayer: true,
                          alphaMarker: 'Mtrn', marker64: true });
write('cases/tag16.psd', { channels: rgba8.map(p => p.map(v => v * 257)), width: 3, depth: 16,
                           transparency: true, layer16: true, positiveLayer: true,
                           alphaMarker: 'Mt16', marker64: true, compression: 3 });
// Older writers finish the layer/mask section immediately after LayerInfo.
write('cases/no_global_mask.psd', { channels: rgba8, width: 3, transparency: true, noGlobalMask: true, compression: 1 });
write('cases/no_global_tag.psd', { channels: rgba8, width: 3, transparency: true, noGlobalMask: true,
                                 positiveLayer: true, alphaMarker: 'Mtrn', compression: 1 });
write('cases/no_global_tag64.psd', { channels: rgba8, width: 3, transparency: true, noGlobalMask: true,
                                   positiveLayer: true, alphaMarker: 'Mtrn', marker64: true, compression: 1 });
for (let n = 1; n < 4; n++) {
    write(`bad/partial_global_mask${n}.psd`, { channels: rgba8, width: 3, transparency: true,
                                             truncatedGlobalMask: n, compression: 1 });
}

fs.writeFileSync(path.join(out, 'bad', 'cut.psd'), raw.subarray(0, raw.length - 1));
fs.writeFileSync(path.join(out, 'bad', 'rle_cut.psd'), rle.subarray(0, rle.length - 1));
write('bad/cmyk.psd', { channels: [...rgb8, [255, 255, 255, 255]], mode: 4 });
write('bad/large.psb', { channels: rgb8, version: 2 });
write('bad/no_composite.psd', { channels: rgb8, realComposite: false });
const absent = raw.subarray(0, raw.length - 12 - 2);
fs.writeFileSync(path.join(out, 'bad', 'absent.psd'), absent);

function compositeAt(bytes) {
    let at = 26;
    for (let section = 0; section < 3; section++) at += 4 + bytes.readUInt32BE(at);
    return at;
}
// RLE row tables and streams must agree; matching byte counts alone don't
// validate an oversized repeat, short literal, or a missing repeat value.
const simpleRle = psd({ channels: rgb8, compression: 1 });
const rowsAt = compositeAt(simpleRle) + 2;
const pixelsAt = rowsAt + 2 * 2 * 3;
const longRun = Buffer.from(simpleRle);
longRun[pixelsAt + 1] = 252; // A five-byte repeat into a two-byte row.
fs.writeFileSync(path.join(out, 'bad', 'rle_long_run.psd'), longRun);
const shortLiteral = Buffer.concat([simpleRle.subarray(0, pixelsAt), Buffer.from([127, 0]),
                                    simpleRle.subarray(pixelsAt + simpleRle.readUInt16BE(rowsAt))]);
shortLiteral.writeUInt16BE(2, rowsAt);
fs.writeFileSync(path.join(out, 'bad', 'rle_short_literal.psd'), shortLiteral);
const missingRepeat = Buffer.from(shortLiteral);
missingRepeat[pixelsAt] = 128;
missingRepeat[pixelsAt + 1] = 255;
fs.writeFileSync(path.join(out, 'bad', 'rle_missing_value.psd'), missingRepeat);
const badLengths = Buffer.from(simpleRle);
badLengths.writeUInt16BE(0, rowsAt);
fs.writeFileSync(path.join(out, 'bad', 'rle_lengths.psd'), badLengths);
const badChecksum = Buffer.from(zip);
badChecksum[badChecksum.length - 1] ^= 1;
fs.writeFileSync(path.join(out, 'bad', 'zip_checksum.psd'), badChecksum);
fs.writeFileSync(path.join(out, 'bad', 'zip_cut.psd'), zip.subarray(0, zip.length - 1));
// A valid zlib stream that expands past the header's channel/row count.
const excess = Buffer.concat([zip.subarray(0, compositeAt(zip) + 2), deflateSync(Buffer.alloc(1024, 255))]);
fs.writeFileSync(path.join(out, 'bad', 'zip_excess.psd'), excess);
