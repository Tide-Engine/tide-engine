// A .tidegraph's JSON (notes/visual-scripting.md, The file): read as any JSON, and written as tidec writes it
// (print_jv in compiler/src/graph_convert.c): LF line endings, two-space indentation, an object or array with
// nothing nested in it on one line, and each node, parameter, payload value and trigger on one line. Keys stay in
// the order they're read; a key the editor adds goes where the schema's order puts it (TideGraph.json.set).
//
// Every file of the editor is a classic script that adds to globalThis.TideGraph, so hosts load them as they are,
// one after the other (files.json), with no build step: JetBrains' build has no Node.
(function (G) {
    'use strict';

    // Thrown for text that isn't JSON, with where
    class ParseError extends Error {
        constructor(message, line, column) {
            super(`${line}:${column}: ${message}`);
            this.line = line;
            this.column = column;
        }
    }

    // The file's value. JSON.parse keeps keys in the order read, except keys that are array indexes, which no
    // element of the format has (its maps are of names).
    function parse(text) {
        const s = text.charCodeAt(0) === 0xfeff ? text.slice(1) : text;
        try {
            return JSON.parse(s);
        } catch (e) {
            // Where, as a line and a column, from the place V8 gives
            const at = /position (\d+)/.exec(e.message);
            let line = 1, column = 1;
            const end = at ? Math.min(Number(at[1]), s.length) : s.length;
            for (let i = 0; i < end; i++) {
                if (s[i] === '\n') { line++; column = 1; } else column++;
            }
            throw new ParseError(e.message.replace(/^JSON\.parse: |^Unexpected/, m => m.startsWith('JSON') ? '' : m), line, column);
        }
    }

    // Arrays under these keys have each of their objects on one line, whatever is in it (tidec's one_line)
    const ONE_LINE_ITEMS = new Set(['nodes', 'params', 'query', 'payload']);
    const ONE_LINE = new Set(['trigger']);

    function isObject(v) {
        return v !== null && typeof v === 'object' && !Array.isArray(v);
    }

    function nested(v) {
        for (const item of Array.isArray(v) ? v : Object.values(v)) {
            if (isObject(item)) return true;
            if (Array.isArray(item) && nested(item)) return true;
        }
        return false;
    }

    function string(s) {
        let out = '"';
        for (let i = 0; i < s.length; i++) {
            const c = s.charCodeAt(i);
            if (c === 0x22) out += '\\"';
            else if (c === 0x5c) out += '\\\\';
            else if (c === 0x0a) out += '\\n';
            else if (c === 0x0d) out += '\\r';
            else if (c === 0x09) out += '\\t';
            else if (c < 0x20) out += '\\u' + c.toString(16).padStart(4, '0');
            else out += s[i];
        }
        return out + '"';
    }

    function number(n) {
        // tidec writes every number as an int; the editor keeps places whole
        return Number.isInteger(n) ? String(n) : String(Math.round(n));
    }

    function indent(depth) {
        return '  '.repeat(depth);
    }

    function value(v, depth, oneLine) {
        if (v === null || v === undefined) return 'null';
        if (typeof v === 'boolean') return v ? 'true' : 'false';
        if (typeof v === 'number') return number(v);
        if (typeof v === 'string') return string(v);
        const object = !Array.isArray(v);
        const flat = oneLine || !nested(v);
        const entries = object ? Object.entries(v).filter(([, x]) => x !== undefined) : v.map(x => [null, x]);
        const parts = [];
        for (const [key, item] of entries) {
            // An empty comment says nothing, and isn't written
            if (object && key === 'comment' && item === '') continue;
            let text = object ? string(key) + ': ' : '';
            if (object && ONE_LINE_ITEMS.has(key) && Array.isArray(item)) text += lines(item, depth + 1, flat);
            else text += value(item, depth + 1, flat || (object && ONE_LINE.has(key) && isObject(item)));
            parts.push(text);
        }
        const [open, close] = object ? ['{', '}'] : ['[', ']'];
        if (!parts.length) return open + close;
        if (flat) return open + parts.join(', ') + close;
        return open + parts.map(p => '\n' + indent(depth + 1) + p).join(',') + '\n' + indent(depth) + close;
    }

    // An array whose objects are each on one line
    function lines(a, depth, oneLine) {
        if (!a.length) return '[]';
        const flat = oneLine || !nested(a);
        const parts = a.map(item => value(item, depth + 1, flat || isObject(item)));
        if (flat) return '[' + parts.join(', ') + ']';
        return '[' + parts.map(p => '\n' + indent(depth + 1) + p).join(',') + '\n' + indent(depth) + ']';
    }

    // A value as tidec writes it, without the newline after
    function write(v) {
        return value(v, 0, false);
    }

    // The file's text: the root, and the newline after it
    function writeFile(root) {
        return write(root) + '\n';
    }

    // Sets `key` on `o`: in place when it's there; else where `order` puts it among the keys there (before the
    // first that the order puts after it), else last. undefined removes it.
    function set(o, key, v, order) {
        if (v === undefined) {
            delete o[key];
            return;
        }
        if (Object.prototype.hasOwnProperty.call(o, key) || !order) {
            o[key] = v;
            return;
        }
        const rank = order.indexOf(key);
        const keys = Object.keys(o);
        const at = rank < 0 ? -1 : keys.findIndex(k => order.indexOf(k) > rank);
        if (at < 0) {
            o[key] = v;
            return;
        }
        const after = keys.slice(at).map(k => [k, o[k]]);
        for (const [k] of after) delete o[k];
        o[key] = v;
        for (const [k, x] of after) o[k] = x;
    }

    function clone(v) {
        return v === undefined ? undefined : JSON.parse(JSON.stringify(v));
    }

    G.json = { parse, write, writeFile, set, clone, isObject, ParseError };
})(globalThis.TideGraph = globalThis.TideGraph || {});
