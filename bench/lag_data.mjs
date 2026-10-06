// Compact raw samples and refer to them independently of the checkout path.
import { createHash } from 'node:crypto';
import { writeFileSync } from 'node:fs';
import { relative } from 'node:path';
import { gzipSync } from 'node:zlib';

export function saveRaw(path, text, root) {
    const bytes = gzipSync(text);
    writeFileSync(path, bytes);
    return { raw_frames: relative(root, path).replaceAll('\\', '/'),
        raw_frames_sha256: createHash('sha256').update(bytes).digest('hex'),
        raw_content_sha256: createHash('sha256').update(text).digest('hex') };
}
