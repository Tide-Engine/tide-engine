// What tidels says of the graph (notes/visual-scripting.md, Language server), asked through the host: the Tide it
// stands for and where each element is in it (tide/graphText), each node's type (tide/graphTypes), diagnostics on
// nodes (published, `data.node`), hovers, completion at a place, and what a node's value has (tide/graphMembers).
(function (G) {
    'use strict';

    // LSP's CompletionItemKind
    const KIND = {
        Method: 2, Function: 3, Constructor: 4, Field: 5, Variable: 6, Class: 7, Interface: 8, Module: 9, Property: 10,
        Value: 12, Enum: 13, Keyword: 14, Snippet: 15, EnumMember: 20, Constant: 21, Struct: 22, Event: 23,
        TypeParameter: 25,
    };

    class Lang {
        // `request(method, params)` asks tidels through the host and resolves with its answer (rejects without one)
        constructor(request) {
            this.request = request;
            this.listeners = new Set();
            this.uri = null;
            this.text = null;      // The Tide the graph stands for, or null before a check (or without tidels)
            this.spans = [];       // [start, end, id], inner after outer
            this.types = {};       // id -> type
            this.diagnostics = []; // { severity, message, node, line, range }
            this.status = 'unchecked'; // 'checking', 'checked', 'unavailable'
            this.why = null;       // Why tidels didn't answer
            this.generation = 0;
        }

        on(listener) {
            this.listeners.add(listener);
            return () => this.listeners.delete(listener);
        }

        emit(what) {
            for (const l of this.listeners) l(what, this);
        }

        params(extra) {
            return { textDocument: { uri: this.uri }, ...extra };
        }

        // Asks for the text and the types of the graph as the host's document has it now
        async check() {
            const generation = ++this.generation;
            this.status = 'checking';
            this.emit('status');
            try {
                const [text, types] = await Promise.all([
                    this.request('tide/graphText', this.params()),
                    this.request('tide/graphTypes', this.params()),
                ]);
                if (generation !== this.generation) return;
                this.text = text?.text ?? null;
                this.spans = Array.isArray(text?.spans) ? text.spans : [];
                this.types = types && typeof types === 'object' ? types : {};
                this.status = this.text === null ? 'unavailable' : 'checked';
                this.why = this.text === null ? 'tidels has no Tide for this graph' : null;
            } catch (e) {
                if (generation !== this.generation) return;
                this.status = 'unavailable';
                this.why = e?.message || String(e);
                this.text = null;
                this.spans = [];
                this.types = {};
            }
            this.emit('check');
        }

        // The diagnostics tidels published for the document, as LSP has them
        setDiagnostics(list) {
            this.diagnostics = (list || []).map(d => ({
                severity: d.severity ?? 1,
                message: d.message ?? '',
                node: typeof d.data?.node === 'number' ? d.data.node : (typeof d.node === 'number' ? d.node : 0),
                range: d.range,
                detail: d.detail,
            }));
            this.emit('diagnostics');
        }

        diagnosticsOf(id) {
            return this.diagnostics.filter(d => d.node === id);
        }

        // The widest stretch of the text an element stands for: [start, end], or null
        span(id) {
            let found = null;
            for (const s of this.spans) {
                if (s[2] === id && (!found || s[1] - s[0] > found[1] - found[0])) found = [s[0], s[1]];
            }
            return found;
        }

        // The innermost element whose text has `offset` in it (the narrowest stretch), or 0
        idAt(offset) {
            let found = null;
            for (const s of this.spans) {
                if (s[0] <= offset && offset < s[1] && (!found || s[1] - s[0] < found[1] - found[0])) found = s;
            }
            return found ? found[2] : 0;
        }

        typeOf(id) {
            return this.types[id] ?? this.types[String(id)] ?? null;
        }

        // Where to ask about a node: its name (a member's member, a method's after its object's '.', a call's)
        nameOffset(n) {
            const span = this.span(n.id);
            if (!span || this.text === null) return -1;
            const text = this.text.slice(span[0], span[1]);
            const name = n.k === 'member' ? n.member : n.k === 'method' || n.k === 'call' || n.k === 'name' ? n.name : null;
            if (name) {
                const after = n.k === 'member' || n.k === 'method' ? text.lastIndexOf('.' + name) : -1;
                if (after >= 0) return span[0] + after + 1;
                const at = text.indexOf(name);
                if (at >= 0) return span[0] + at;
            }
            return span[0];
        }

        async hover(n) {
            const offset = this.nameOffset(n);
            if (offset < 0) return null;
            try {
                const h = await this.request('tide/graphHover', this.params({ offset }));
                return hoverText(h);
            } catch {
                return null;
            }
        }

        async completion(offset) {
            if (offset < 0 || this.text === null) return [];
            try {
                const c = await this.request('tide/graphCompletion', this.params({ offset }));
                return Array.isArray(c) ? c : c?.items || [];
            } catch {
                return [];
            }
        }

        async members(id) {
            try {
                const c = await this.request('tide/graphMembers', this.params({ node: id }));
                return { items: Array.isArray(c) ? c : c?.items || [], type: c?.type ?? null };
            } catch {
                return { items: [], type: null };
            }
        }

        async signature(offset) {
            try {
                return await this.request('tide/graphSignatureHelp', this.params({ offset }));
            } catch {
                return null;
            }
        }

        // Where a node to be added would go in the text, for completion there: after the statement a flow comes
        // from, before the one it goes to, at the start of the statement a value is for, else just inside the end of
        // what the graph is in
        placeFor(context, owner) {
            if (this.text === null) return -1;
            if (context?.after) {
                const s = this.span(context.after);
                if (s) return s[1];
            }
            if (context?.before) {
                const s = this.span(context.before);
                if (s) return s[0];
            }
            if (context?.valueFor) {
                const s = this.span(context.valueFor);
                if (s) return s[0];
            }
            const s = this.span(owner);
            if (!s) return -1;
            const close = this.text.lastIndexOf('}', s[1] - 1);
            return close > s[0] ? close : s[1];
        }
    }

    function hoverText(h) {
        if (!h) return null;
        const c = h.contents;
        if (typeof c === 'string') return c;
        if (Array.isArray(c)) return c.map(x => typeof x === 'string' ? x : x.value).join('\n\n');
        return c?.value ?? null;
    }

    // The parameters' names of a completion's signature: `void Bounce(float by)` -> ['by']
    function paramNames(item) {
        const text = item.detail || item.label || '';
        const open = text.indexOf('(');
        if (open < 0) return [];
        let depth = 0, close = -1;
        for (let i = open; i < text.length; i++) {
            if ('(<['.includes(text[i])) depth++;
            else if (')>]'.includes(text[i]) && --depth === 0) { close = i; break; }
        }
        const inside = text.slice(open + 1, close < 0 ? text.length : close).trim();
        if (!inside) return [];
        const parts = [];
        depth = 0;
        let start = 0;
        for (let i = 0; i <= inside.length; i++) {
            const c = inside[i];
            if (c === undefined || (c === ',' && depth === 0)) {
                parts.push(inside.slice(start, i).trim());
                start = i + 1;
            } else if ('(<['.includes(c)) depth++;
            else if (')>]'.includes(c)) depth--;
        }
        return parts.map(p => {
            const words = p.replace(/=.*$/, '').trim().split(/\s+/);
            return words[words.length - 1] || 'arg';
        });
    }

    // A completion item as what the search adds: a node kind and its fields, or null for one it doesn't offer.
    // `member` for items of a value's members (wired to it); `flow` where a statement is wanted.
    function entryOf(item, { member = false, flow = false } = {}) {
        const label = item.label;
        if (typeof label !== 'string' || !label) return null;
        const name = label.replace(/\(.*$/, '');
        const detail = item.detail || '';
        switch (item.kind) {
            case KIND.Method:
            case KIND.Function:
            case KIND.Constructor: {
                const args = paramNames(item).map(() => null);
                const node = member ? { k: 'method', fields: { name, args } } : { k: 'call', fields: { name, args } };
                return { title: name + '(' + paramNames(item).join(', ') + ')', detail, group: member ? 'Members' : 'Functions', node, statement: flow, params: paramNames(item) };
            }
            case KIND.Field:
            case KIND.Property:
            case KIND.Constant:
            case KIND.Variable:
            case KIND.EnumMember:
            case KIND.Value:
            case KIND.Event:
                if (member) return { title: name, detail, group: 'Members', node: { k: 'member', fields: { member: name } } };
                if (item.kind === KIND.EnumMember) return { title: '.' + name, detail, group: 'Values', node: { k: 'member', fields: { shorthand: true, member: name } } };
                return { title: name, detail, group: 'Names', node: { k: 'name', fields: { name } } };
            case KIND.Struct:
            case KIND.Class:
                if (member) return { title: name, detail, group: 'Members', node: { k: 'member', fields: { member: name } } };
                return { title: name + ' { }', detail: detail || 'a value of the type', group: 'Types', node: { k: 'literal', fields: { type: name, inits: [] } } };
            case KIND.Enum:
            case KIND.Interface:
            case KIND.Module:
            case KIND.TypeParameter:
                return { title: name, detail, group: member ? 'Members' : 'Names', node: member ? { k: 'member', fields: { member: name } } : { k: 'name', fields: { name } } };
            default:
                return null; // Keywords and snippets: the schema's nodes are those
        }
    }

    G.Lang = Lang;
    G.lang = { entryOf, paramNames, hoverText, KIND };
})(globalThis.TideGraph = globalThis.TideGraph || {});
