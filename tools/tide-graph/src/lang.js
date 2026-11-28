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
            this.calls = {};       // a call's id -> { params, types, args }: its callee's parameters (tide/graphCalls)
            this.callsKnown = false; // Whether tidels has said which calls have parameters to name
            this.choices = {};     // an enum the rest of the game declares -> its choices (tide/graphMembers by name)
            this.names = { game: {}, packages: {} }; // What the game itself declares, by name (tide/graphNames)
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
                // An answer that doesn't come (a server that stopped) isn't waited for for ever
                let timer;
                const late = new Promise((_, reject) => { timer = setTimeout(() => reject(new Error("Tide's language server didn't answer")), 30000); });
                // What's newer than a tidels may not be answered: the page then goes without it
                const maybe = method => this.request(method, this.params()).catch(() => null);
                const [text, types, calls, names] = await Promise.race([Promise.all([
                    this.request('tide/graphText', this.params()),
                    this.request('tide/graphTypes', this.params()),
                    maybe('tide/graphCalls'),
                    maybe('tide/graphNames'),
                ]), late]).finally(() => clearTimeout(timer));
                if (generation !== this.generation) return;
                // A half-made graph is still read, an input with nothing in it as a hole: only a file tidec can't
                // read at all (a node of no kind, a wire to nothing) has no Tide, and its diagnostics say why
                this.text = typeof text?.text === 'string' ? text.text : null;
                this.spans = Array.isArray(text?.spans) ? text.spans : [];
                this.types = types && typeof types === 'object' ? types : {};
                this.calls = calls && typeof calls === 'object' ? calls : {};
                this.callsKnown = !!calls && typeof calls === 'object';
                this.names = { game: names?.game || {}, packages: names?.packages || {} };
                // The choices of the game's enums, for their fields' controls on the cards: asked once each, and
                // again when the game's names change
                const enums = Object.entries({ ...this.names.packages, ...this.names.game }).filter(([, kind]) => kind === 'enum').map(([name]) => name);
                const key = enums.join('\n');
                if (key !== this.enumsKey) {
                    const found = await Promise.all(enums.map(name => this.membersOfName(name)));
                    if (generation !== this.generation) return;
                    this.enumsKey = key;
                    this.choices = {};
                    enums.forEach((name, i) => {
                        const names = found[i].filter(item => item.kind === KIND.EnumMember || item.kind === KIND.Constant || item.kind === KIND.Value).map(item => String(item.label));
                        if (names.length) this.choices[name] = names;
                    });
                }
                this.status = 'checked';
                this.why = null;
            } catch (e) {
                if (generation !== this.generation) return;
                this.status = 'unavailable';
                this.why = e?.message || String(e);
                this.text = null;
                this.spans = [];
                this.types = {};
                this.calls = {};
                this.callsKnown = false;
                this.diagnostics = [];
            }
            this.emit('check');
        }

        // The diagnostics tidels published for the document, as LSP has them
        setDiagnostics(list) {
            // What's shown is the last check's, never kept: each list replaces the one before
            this.diagnostics = (list || []).map(d => {
                // The compiler's message, and its note (what to do about it) after it
                const text = String(d.message ?? '');
                const at = text.indexOf('\nnote: ');
                return {
                    severity: d.severity ?? 1,
                    message: at < 0 ? text : text.slice(0, at),
                    // An input with nothing in it: its key, and its place among the key's items (the page says it
                    // on that input, in its own words)
                    port: typeof d.data?.port === 'string' ? d.data.port : null,
                    index: typeof d.data?.index === 'number' ? d.data.index : -1,
                    note: at < 0 ? '' : text.slice(at + 7).trim(),
                    node: typeof d.data?.node === 'number' ? d.data.node : (typeof d.node === 'number' ? d.node : 0),
                    range: d.range,
                };
            });
            this.emit('diagnostics');
        }

        // Without tidels nothing is checked, and nothing it said before stands
        unavailable(why) {
            this.generation++;
            this.status = 'unavailable';
            this.why = why || null;
            this.text = null;
            this.spans = [];
            this.types = {};
            this.calls = {};
            this.diagnostics = [];
            this.emit('check');
        }

        // What a call's arguments are called and take: { names, types } by argument, from its callee's parameters,
        // or null before a check has said
        callArgs(id) {
            const c = this.calls[id] ?? this.calls[String(id)];
            if (!c || !Array.isArray(c.params)) return null;
            const args = Array.isArray(c.args) ? c.args : [];
            return { params: c.params, types: c.types || [], at: i => (typeof args[i] === 'number' ? args[i] : i) };
        }

        // What an input is called on its node: the schema's word for its key (`portInfo`), a call's argument by its
        // callee's parameter (tide/graphCalls), a named argument by its name. An argument is "argument 1" until a
        // check has said; one tidels has no name for (Spawn's) then has none, but a vector's or a colour's parts
        // (float2(x, y)). `always` gives a name to say whatever (a problem's words).
        inputLabel(schema, node, p, always = false) {
            if (!p.group) return schema.portLabel(p.key);
            const list = schema.node(node.k)?.lists?.find(l => l.key === p.group);
            if (list?.names) {
                const named = Array.isArray(node[list.names]) ? node[list.names][p.index] : '';
                if (named) return named;
                const c = this.callArgs(node.id);
                const known = c?.params[c.at(p.index)];
                if (known) return known;
                const count = Array.isArray(node[list.key]) ? node[list.key].length : 0;
                const control = node.k === 'call' ? schema.control(node.name) : null;
                if (control === 'vector' && count > 1 && count <= 4) return 'xyzw'[p.index];
                if (control === 'colour' && count >= 3 && count <= 4) return ['red', 'green', 'blue', 'alpha'][p.index];
                return this.callsKnown && !always ? '' : `argument ${p.index + 1}`;
            }
            return p.label;
        }

        // Whether the game itself (or one of its packages) declares a name: what's offered and isn't is the engine's.
        // Before tidels has said, nothing is known to be the game's.
        declares(name) {
            return this.names.game[name] || this.names.packages[name] || null;
        }

        // What a value of a type has: its fields and methods, as completion items
        async membersOfType(type) {
            if (!type) return [];
            try {
                const c = await this.request('tide/graphMembers', this.params({ type }));
                return Array.isArray(c) ? c : c?.items || [];
            } catch {
                return [];
            }
        }

        // Which of the schema's operators the checker takes for a value of `type`, with a value of `other` on the
        // right (of `type` when it's left out): { assign, step, binary, unary } (tide/graphOperators), or null when
        // tidels doesn't know the type there or doesn't answer, and then every operator is offered. Asked once a check.
        async operators(type, other) {
            if (!type || this.status === 'unavailable') return null;
            const key = `${this.generation}
${type}
${other || ''}`;
            if (!this.operatorsAsked || this.operatorsAsked.generation !== this.generation) this.operatorsAsked = { generation: this.generation, map: new Map() };
            const asked = this.operatorsAsked.map;
            if (!asked.has(key)) {
                asked.set(key, this.request('tide/graphOperators', this.params(other ? { type, other } : { type })).then(
                    r => (r && typeof r === 'object' && (Array.isArray(r.assign) || Array.isArray(r.binary)) ? r : null), () => null));
            }
            return asked.get(key);
        }

        // What a name itself has after a dot: an enum's choices, a namespace's functions (Math), as completion items
        async membersOfName(name) {
            if (!name) return [];
            try {
                const c = await this.request('tide/graphMembers', this.params({ name }));
                return Array.isArray(c) ? c : c?.items || [];
            } catch {
                return [];
            }
        }

        // The parameter a call's next argument would be for (tide/graphCalls): its name; '' when every parameter has
        // its argument; null when tidels hasn't said what the call takes
        nextParam(node, count) {
            const c = this.callArgs(node.id);
            if (!c) return null;
            const used = new Set();
            for (let i = 0; i < count; i++) used.add(c.at(i));
            return c.params.find((_, i) => !used.has(i)) || '';
        }

        // What tidels offers where an element's own text starts, which is where its type is written (a field's, a
        // parameter's, a constant's): the types valid there
        async completionAt(id) {
            const s = this.span(id);
            return s ? this.completion(s[0]) : [];
        }

        // ...and where a routine's next parameter would go: just inside the parentheses after its name
        async completionInParams(routine) {
            const s = this.span(routine.id);
            if (!s || this.text === null) return [];
            const first = (routine.params || []).map(p => this.span(p.id)).find(Boolean);
            if (first) return this.completion(first[0]);
            const text = this.text.slice(s[0], s[1]);
            const name = routine.name ? text.indexOf(routine.name + '(') : -1;
            const open = name >= 0 ? name + routine.name.length : text.indexOf('(');
            return open < 0 ? [] : this.completion(s[0] + open + 1);
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

    // The engine's types made by a call of their name (the spec's constructors), and the kinds written as a literal
    const CONSTRUCTED = /^(?:bool|byte|char|int|long|float|double|(?:int|float|double)[234](?:x[234])?|quaternion|Color|Rect|PlayerID)$/;
    const LITERAL = ['struct', 'component', 'event'];

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
                return { title: name + '(' + paramNames(item).join(', ') + ')', detail, group: member ? G.w('search.members') : G.w('search.functions'), node, statement: flow, params: paramNames(item) };
            }
            case KIND.Field:
            case KIND.Property:
            case KIND.Constant:
            case KIND.Variable:
            case KIND.EnumMember:
            case KIND.Value:
            case KIND.Event:
                if (member) return { title: name, detail, group: G.w('search.members'), node: { k: 'member', fields: { member: name } } };
                if (item.kind === KIND.EnumMember) return { title: '.' + name, detail, group: G.w('search.values'), node: { k: 'member', fields: { shorthand: true, member: name } } };
                // An event is sent as a value of it: Hit { who = e }
                if (item.kind === KIND.Event) return flow ? null : { title: name + ' { }', detail: detail || G.w('search.literal'), group: G.w('search.types'), node: { k: 'literal', fields: { type: name, inits: [] } } };
                return { title: name, detail, group: G.w('search.names'), node: { k: 'name', fields: { name } } };
            case KIND.Struct:
            case KIND.Class:
                if (member) return { title: name, detail, group: G.w('search.members'), node: { k: 'member', fields: { member: name } } };
                // The engine's value types are made by calling them: float3(0, 0, 0), Color(1, 1, 1), float(...); the
                // others (string, List, Skeleton...) are made otherwise, and a value made is no step
                if (detail === 'built-in type') {
                    if (flow || !CONSTRUCTED.test(name)) return null;
                    const size = Number(/^(?:float|int|double)([234])$/.exec(name)?.[1]) || 0;
                    const fill = size ? Array(size).fill('0') : name === 'Color' ? ['1.0', '1.0', '1.0'] : null;
                    return { title: name + '(…)', detail, group: G.w('search.types'), node: { k: 'call', fields: { name, args: fill ? [] : [null] } }, fill };
                }
                // The game's structs, components and events are written as values of them; nothing else is
                if (flow || (detail && !LITERAL.includes(String(detail).replace(/^local /, '')))) return null;
                return { title: name + ' { }', detail: detail || G.w('search.literal'), group: G.w('search.types'), node: { k: 'literal', fields: { type: name, inits: [] } } };
            case KIND.Module:
                // A namespace alone is no value: its functions are offered by their own names (pickers.js,
                // namespaceCalls)
                if (!member) return null;
                return { title: name, detail, group: G.w('search.members'), node: { k: 'member', fields: { member: name } } };
            case KIND.Enum:
                // An enum alone is no value: its choices are offered by their own names (pickers.js, namespaceCalls)
                if (!member) return null;
            // fall through
            case KIND.Interface:
            case KIND.TypeParameter:
                return { title: name, detail, group: member ? G.w('search.members') : G.w('search.names'), node: member ? { k: 'member', fields: { member: name } } : { k: 'name', fields: { name } } };
            default:
                return null; // Keywords and snippets: the schema's nodes are those
        }
    }

    G.Lang = Lang;
    G.lang = { entryOf, paramNames, hoverText, KIND };
})(globalThis.TideGraph = globalThis.TideGraph || {});
