import { readFile } from 'node:fs/promises';
import { basename, extname } from 'node:path';

export function safeAssetName(name) {
    return basename(name).replace(/[^a-zA-Z0-9_.-]/g, '_').replace(/^\.+/, '') || 'asset';
}

export function assetBuildPath(asset) {
    if (typeof asset.id !== 'string' || !/^[a-zA-Z0-9_-]{1,100}$/.test(asset.id) || typeof asset.name !== 'string') throw new Error('Invalid uploaded asset metadata.');
    const name = safeAssetName(asset.name);
    if (/^(con|prn|aux|nul|com[0-9]|lpt[0-9])(?:\.|$)/i.test(name) || name.endsWith('.')) throw new Error('This asset filename is reserved by the build platform. Rename and upload it again.');
    return `assets/${asset.id}/${name}`;
}

// This deliberately accepts a smaller language than Tide: compile-time paths
// must be exact uploaded-file literals, never constants, globs or expressions.
// The scanner follows Tide's non-nesting comments, chars, normal strings and
// $"...{expressions}..." strings so their contents cannot disguise a declaration.
function scan(source, file) {
    let at = source.charCodeAt(0) === 0xfeff ? 1 : 0;
    const tokens = [];
    const error = message => { throw new Error(`${file}: ${message}`); };
    const comment = () => {
        if (source.startsWith('//', at)) {
            while (at < source.length && source[at] !== '\n') at++;
            return true;
        }
        if (source.startsWith('/*', at)) {
            const end = source.indexOf('*/', at + 2);
            if (end < 0) error('Unterminated block comment.');
            at = end + 2;
            return true;
        }
        return false;
    };
    const quoted = quote => {
        const start = at++;
        let escaped = false;
        while (at < source.length) {
            const character = source[at++];
            if (character === quote) return { kind: quote === '"' ? 'string' : 'char', value: source.slice(start + 1, at - 1), escaped };
            if (character === '\n' || character === '\r' || character === '\0') error('Unterminated text or character literal.');
            if (character === '\\') {
                escaped = true;
                const escape = source[at++];
                const allowed = quote === '"' ? ['"', '\\', 'n'] : ["'", '"', '\\', 'n', 'r', 't', '0'];
                if (!allowed.includes(escape)) error('Unsupported text escape. Asset paths must be unescaped uploaded-file literals.');
            }
        }
        error('Unterminated text or character literal.');
    };
    const interpolated = (depth = 0) => {
        if (depth > 32) error('Interpolated text nests too deeply.');
        at += 2;
        while (at < source.length) {
            const character = source[at++];
            if (character === '"') return;
            if (character === '\n' || character === '\r' || character === '\0') error('Unterminated interpolated text.');
            if (character === '\\') {
                if (!['"', '\\', 'n'].includes(source[at++])) error('Unsupported text escape.');
            } else if ((character === '{' || character === '}') && source[at] === character) at++;
            else if (character === '}') error('Unescaped closing brace in interpolated text.');
            else if (character === '{') {
                let braces = 1;
                while (at < source.length && braces) {
                    if (comment()) continue;
                    if (source.startsWith('$"', at)) interpolated(depth + 1);
                    else if (source[at] === '"' || source[at] === "'") quoted(source[at]);
                    else {
                        if (source[at] === '{') ++braces;
                        if (source[at] === '}') --braces;
                        if (source[at] === '#') error('Conditional compilation is not supported in generated prototypes.');
                        at++;
                    }
                }
                if (braces) error('Unterminated interpolated expression.');
            }
        }
        error('Unterminated interpolated text.');
    };
    while (at < source.length) {
        const character = source[at];
        if (/[ \t\r\n]/.test(character)) { at++; continue; }
        if (comment()) continue;
        if (source.startsWith('$"', at)) { interpolated(); tokens.push({ kind: 'interpolation' }); continue; }
        if (character === '"' || character === "'") { tokens.push(quoted(character)); continue; }
        if (character === '#') error('Conditional compilation is not supported in generated prototypes.');
        if (/[a-zA-Z_]/.test(character)) {
            const start = at++;
            while (at < source.length && /[a-zA-Z0-9_]/.test(source[at])) at++;
            tokens.push({ kind: 'identifier', value: source.slice(start, at) });
        } else { tokens.push({ kind: 'punctuation', value: character }); at++; }
    }
    return tokens;
}

function referencedPaths(file) {
    const tokens = scan(file.content, file.path);
    const references = [];
    for (let at = 0; at < tokens.length; at++) {
        if (tokens[at].kind !== 'identifier' || tokens[at].value !== 'asset' || tokens[at + 1]?.kind !== 'identifier' || tokens[at - 1]?.value === '.') continue;
        let equals = at + 1;
        while (equals < tokens.length && !['=', ';', '{', '}', '('].includes(tokens[equals].value)) equals++;
        const path = tokens[equals + 1];
        if (tokens[equals]?.value !== '=' || path?.kind !== 'string' || path.escaped || tokens[equals + 2]?.value !== ';') {
            throw new Error(`${file.path}: Asset paths must be a single unescaped literal of an uploaded game asset; constants, interpolation, concatenation and globs are not supported.`);
        }
        references.push(path.value);
        at = equals + 2;
    }
    return references;
}

// JSON.parse alone is insufficient: Tide's JSON reader can choose a different
// duplicate key than JavaScript. Inspect every decoded URI key, including
// escaped spellings and duplicates, before the compiler gets the model.
function validateJsonUris(source) {
    try { JSON.parse(source); }
    catch { throw new Error('The GLB description is not valid JSON.'); }
    let at = 0;
    const string = () => {
        const start = at++;
        while (at < source.length) {
            if (source[at] === '\\') at += 2;
            else if (source[at++] === '"') return JSON.parse(source.slice(start, at));
        }
        throw new Error('The GLB description contains unterminated text.');
    };
    while (at < source.length) {
        if (source[at] !== '"') { at++; continue; }
        const key = string();
        while (/\s/.test(source[at] || '') && at < source.length) at++;
        if (key !== 'uri' || source[at] !== ':') continue;
        at++;
        while (/\s/.test(source[at] || '') && at < source.length) at++;
        if (source[at] !== '"') throw new Error('GLB resource URIs must be embedded data, never external files.');
        const value = string();
        if (!/^data:[^,\x00-\x20]*;base64,[a-zA-Z0-9+/]*={0,2}$/.test(value)) throw new Error('GLB resource URIs must be embedded data, never external files or URLs. Export a self-contained GLB.');
    }
}

function validateGlb(bytes) {
    if (bytes.length < 20 || bytes.toString('ascii', 0, 4) !== 'glTF' || bytes.readUInt32LE(4) !== 2 || bytes.readUInt32LE(8) !== bytes.length) throw new Error('Upload a valid self-contained GLB 2.0 model.');
    let at = 12;
    let json = false, binary = false;
    while (at < bytes.length) {
        if (at + 8 > bytes.length) throw new Error('The GLB contains a truncated chunk.');
        const size = bytes.readUInt32LE(at), kind = bytes.readUInt32LE(at + 4);
        if (size % 4 !== 0 || size > bytes.length - at - 8) throw new Error('The GLB contains an invalid chunk.');
        if (kind === 0x4e4f534a && !json && at === 12) {
            let source;
            try { source = new TextDecoder('utf-8', { fatal: true }).decode(bytes.subarray(at + 8, at + 8 + size)); }
            catch { throw new Error('The GLB description is not valid UTF-8.'); }
            validateJsonUris(source);
            json = true;
        } else if (kind === 0x004e4942 && json && !binary) binary = true;
        else throw new Error('The GLB must contain one JSON description and at most one embedded binary chunk.');
        at += size + 8;
    }
    if (!json) throw new Error('The GLB has no JSON description.');
}

async function validateFile(asset) {
    const extension = extname(safeAssetName(asset.name)).toLowerCase();
    if (!['.png', '.psd', '.wav', '.ogg', '.glb'].includes(extension)) throw new Error(`${asset.name}: This first builder supports PNG/PSD images, WAV/Ogg audio and self-contained GLB models. Keep this upload as a reference or convert it first.`);
    const bytes = await readFile(asset.filePath);
    const signatures = {
        '.png': bytes.length >= 8 && bytes.subarray(0, 8).equals(Buffer.from([137, 80, 78, 71, 13, 10, 26, 10])),
        '.psd': bytes.length >= 6 && bytes.toString('ascii', 0, 4) === '8BPS' && bytes.readUInt16BE(4) === 1,
        '.wav': bytes.length >= 12 && bytes.toString('ascii', 0, 4) === 'RIFF' && bytes.toString('ascii', 8, 12) === 'WAVE',
        '.ogg': bytes.length >= 4 && bytes.toString('ascii', 0, 4) === 'OggS',
    };
    if (extension === '.glb') validateGlb(bytes);
    else if (!signatures[extension]) throw new Error(`${asset.name}: The file bytes do not match its extension. Convert it to a supported format before using it in the game.`);
}

export async function validateAssetReferences(files, assets = []) {
    const allowed = new Map();
    for (const asset of assets.filter(asset => asset.usage === 'game')) allowed.set(assetBuildPath(asset), asset);
    const used = new Map();
    for (const file of files) {
        for (const path of referencedPaths(file)) {
            const asset = allowed.get(path);
            if (!asset) throw new Error(`${file.path}: '${path.slice(0, 180)}' is not an exact uploaded game asset path. Use one of the supplied assets/<id>/<filename> paths; URLs, traversal and globs are not allowed.`);
            if (file.path.includes('/')) throw new Error('Declare uploaded assets in a root-level Tide source file so their exact build paths remain valid.');
            used.set(path, asset);
        }
    }
    for (const asset of used.values()) await validateFile(asset);
    return [...used.values()];
}
