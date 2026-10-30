// A GIF reader for the tests, from the GIF89a specification: a file's
// frames as a player shows them, each drawn over the one before (only
// disposal 1, which is what gif.c writes), the whole picture each time,
// with how long it shows, how much of the picture it drew and `at(x, y)`
// for a pixel's red, green and blue. Throws for what it can't read.

// LZW as GIF has it: codes from min + 1 bits to 12, lowest bit first.
function lzw(data, min, count) {
    const clear = 1 << min, end = clear + 1;
    let at = 0, bits = 0, held = 0;
    const read = (size) => {
        while (held < size) {
            if (at >= data.length) throw new Error('the image data ends before its end code');
            bits |= data[at++] << held;
            held += 8;
        }
        const code = bits & ((1 << size) - 1);
        bits >>>= size;
        held -= size;
        return code;
    };
    let table = [], size = min + 1, previous = null;
    const restart = () => {
        table = [];
        for (let i = 0; i < clear; i++) table.push([i]);
        table.push(null, null);
        size = min + 1;
        previous = null;
    };
    restart();
    const out = [];
    for (;;) {
        const code = read(size);
        if (code === clear) { restart(); continue; }
        if (code === end) break;
        let entry;
        if (code < table.length && table[code]) entry = table[code];
        else if (code === table.length && previous) entry = previous.concat(previous[0]);
        else throw new Error(`code ${code} isn't in the table (${table.length})`);
        for (const v of entry) out.push(v);
        if (previous && table.length < 4096) table.push(previous.concat(entry[0]));
        if (table.length === 1 << size && size < 12) size++;
        previous = entry;
    }
    if (out.length !== count) throw new Error(`${out.length} pixels where the frame has ${count}`);
    return out;
}

// The frames a GIF shows, each the whole picture, and how long they show.
export function readGif(bytes) {
    let at = 0;
    const byte = () => bytes[at++];
    const u16 = () => { const v = bytes[at] | bytes[at + 1] << 8; at += 2; return v; };
    const blocks = () => {
        const parts = [];
        for (let n; (n = byte()) !== 0;) { parts.push(bytes.subarray(at, at + n)); at += n; }
        return Buffer.concat(parts);
    };
    if (bytes.subarray(0, 6).toString('latin1') !== 'GIF89a') throw new Error('no GIF89a header');
    at = 6;
    const width = u16(), height = u16(), flags = byte();
    at += 2;
    if (flags & 0x80) throw new Error('a palette of the whole file, which gif.c never writes');
    const canvas = new Uint8Array(width * height * 3);
    const frames = [];
    let loops = null, control = null;
    for (;;) {
        const kind = byte();
        if (kind === 0x3B) break;
        if (kind === 0x21) {
            const label = byte();
            if (label === 0xF9) {
                if (byte() !== 4) throw new Error('a graphic control block of other than 4 bytes');
                const packed = byte();
                control = { disposal: packed >> 2 & 7, transparent: packed & 1, delay: u16(), index: byte() };
                if (byte() !== 0) throw new Error('graphic control without its end');
            } else if (label === 0xFF) {
                const n = byte();
                const name = bytes.subarray(at, at + n).toString('latin1');
                at += n;
                const data = blocks();
                if (name === 'NETSCAPE2.0' && data[0] === 1) loops = data[1] | data[2] << 8;
            } else {
                blocks();
            }
            continue;
        }
        if (kind !== 0x2C) throw new Error(`byte ${kind} at ${at - 1} starts nothing`);
        const left = u16(), top = u16(), w = u16(), h = u16(), packed = byte();
        if (!(packed & 0x80)) throw new Error('a frame without its palette');
        const colors = 2 << (packed & 7);
        const palette = bytes.subarray(at, at + colors * 3);
        at += colors * 3;
        const min = byte();
        const indices = lzw(blocks(), min, w * h);
        if (!control) throw new Error('a frame without graphic control');
        if (control.disposal !== 1) throw new Error(`disposal ${control.disposal}, where frames stay under the next (1)`);
        if (left + w > width || top + h > height) throw new Error('a frame past the picture');
        for (let y = 0; y < h; y++) {
            for (let x = 0; x < w; x++) {
                const i = indices[y * w + x];
                if (control.transparent && i === control.index) continue;
                if (i >= colors) throw new Error(`index ${i} of a palette of ${colors}`);
                canvas.set(palette.subarray(i * 3, i * 3 + 3), ((top + y) * width + left + x) * 3);
            }
        }
        const pixels = canvas.slice();
        frames.push({ pixels, delay: control.delay, area: w * h, at: (x, y) => pixels.subarray((y * width + x) * 3, (y * width + x) * 3 + 3) });
        control = null;
    }
    return { width, height, frames, loops };
}
