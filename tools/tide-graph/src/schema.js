// What a .tidegraph holds, from the schema tidels serves (tide/graphSchema, compiler/src/graph_schema.json): every
// kind of declaration, member and node, their keys in tidec's order, each node's ports, and the page's wording: what
// each kind is called and is for, what it can have, the labels of keys, modifiers and parameter modes, the types to
// offer first. The Add menu, the cards, the frames, the ports and the save order all come from it.
(function (G) {
    'use strict';
    const { set } = G.json;

    // What a key holds, where the schema doesn't say (its keys are named, not typed). By name: the format uses each
    // name for one kind of thing, except a node's `value`, which is a wire, or a bool node's flag.
    const FLAGS = new Set(['mut', 'shared', 'extern', 'indexed', 'postfix', 'local', 'verified', 'previous', 'changed',
        'withBlock', 'stacked', 'shorthand', 'ownership', 'scene']);
    const STRINGS = new Set(['typeParams', 'typeArgs', 'argNames', 'using', 'rules', 'parts', 'formats', 'modifiers']);
    const GRAPHS = { value: 'value', default: 'value', body: 'body' }; // A declaration's, member's or element's
    const TYPES = new Set(['type', 'returnType', 'fails', 'backing', 'typeArgs']);
    // Keys of elements that are lists of elements, with the element's order
    const ELEMENT_LISTS = {
        params: 'param', query: 'param', payload: 'payload', attributes: 'attribute', accessors: 'accessor',
        entries: 'entry', where: 'where', children: 'prefabNode', notes: 'note',
    };

    class Schema {
        constructor(json) {
            this.json = json;
            this.root = json.root;
            this.elements = json.elements;
            this.declarations = json.declarations;
            this.members = json.members;
            this.categories = json.categories;
            this.operators = json.operators;
            this.types = json.types;
            this.rules = json.rules || [];
            this.paramModes = json.paramModes;
            this.allModifiers = json.allModifiers;
            this.nodes = json.nodes;
            this.byK = new Map(json.nodes.map(n => [n.k, n]));
            this.declByKind = new Map(json.declarations.map(d => [d.kind, d]));
            this.memberByKind = new Map(json.members.map(m => [m.kind, m]));
            this.categoryByName = new Map(json.categories.map(c => [c.name, c]));
            this.nodeOrders = new Map();
            for (const n of json.nodes) {
                this.nodeOrders.set(n.k, [...json.elements.node, ...n.keys, ...json.elements.nodeEnd]);
            }
            // The wording and the rules of the experience (notes/visual-scripting.md, The experience: one board)
            this.groups = json.groups || [];
            this.keyInfo = json.keyInfo || {};
            this.modifierInfo = json.modifierInfo || {};
            this.paramModeInfo = json.paramModeInfo || {};
            this.typePicker = json.typePicker || { common: [], builders: [] };
            this.flagInfo = json.flagInfo || {};
            this.portInfo = json.portInfo || {};
            this.accessorInfo = json.accessorInfo || {};
            // Which kinds of value are shown in the input that reads them (1 always, 2 as a path, 3 a minus over a
            // number, 4 a vector or a colour made of numbers: a call named one of `numbersOf`, 5 a name that starts
            // with a capital: a type, a namespace, an enum, a constant), as tidec's layout takes them
            // (graph_schema_inline, graph_schema_numbers); and the kinds whose first input, a name or a member read by
            // them alone, is in their title (`titled`: Set score, graph_schema_titled)
            this.inlineKinds = new Map();
            ['always', 'path', 'negative', 'numbers', 'named'].forEach((list, i) => {
                for (const k of json.inline?.[list] || []) this.inlineKinds.set(k, i + 1);
            });
            this.numbersOf = new Set(json.inline?.numbersOf || []);
            this.titledKinds = new Set(json.inline?.titled || []);
            // Which control edits a type's value
            this.controlOf = new Map();
            for (const c of json.valueControls || []) for (const t of c.types || []) this.controlOf.set(t, c.control);
            this.enumControl = (json.valueControls || []).find(c => c.of === 'enum')?.control || 'choice';
            // How the editors draw (colours, sizes, the font) and what they say: the same in each
            this.look = json.look || {};
            this.words = json.words || {};
        }

        // A node's title bar's colour, by its kind's category, in the page's dark or light (G.mode)
        headerColor(k) {
            const category = this.byK.get(k)?.category;
            return this.look.headers?.[category]?.[G.mode || 'dark'] || null;
        }

        // An editor's own words (the schema's `words`): `key`'s text, its {names} filled from `vars`; a menu's entry is
        // { label, keys }, and `w` gives its label
        word(key, vars) {
            let text = this.words[key];
            if (text && typeof text === 'object') text = text.label;
            if (typeof text !== 'string') {
                if (!this.missing?.has(key)) {
                    (this.missing ||= new Set()).add(key);
                    console.error(`Tide graph: no words for '${key}' in the schema`);
                }
                return key;
            }
            return vars ? text.replace(/\{(\w+)\}/g, (all, name) => (vars[name] !== undefined && vars[name] !== null ? String(vars[name]) : all)) : text;
        }

        // A menu entry's keys, as its words say them ('' for none)
        wordKeys(key) {
            const entry = this.words[key];
            return entry && typeof entry === 'object' ? entry.keys || '' : '';
        }

        // What a node's flag, an input or an exec output is called on its node; a key with no entry is shown as it is
        flagLabel(key) { return this.flagInfo[key]?.label || this.keyLabel(key); }
        flagHelp(key) { return this.flagInfo[key]?.help || ''; }
        flagAdvanced(key) { return !!this.flagInfo[key]?.advanced; }
        portLabel(key) { return this.portInfo[key]?.label || key; }
        // 1 to 5 for a kind of value shown in the input that reads it (as the schema's lists are numbered above), 0
        // for a node of its own
        inlineKind(k) { return this.inlineKinds.get(k) || 0; }
        // Whether what a node of this kind changes is drawn in its title
        titled(k) { return this.titledKinds.has(k); }
        // The control that edits a type's value ('whole', 'decimal', 'tick', 'text', 'char', 'vector', 'colour'), or
        // undefined for a type with none of its own (an enum's is `enumControl`, by what the game declares)
        control(type) { return this.controlOf.get(String(type || '').replace(/\?$/, '')); }
        // A group's title in the type picker
        typeGroup(name) { return this.typePicker.groups?.[name] || name; }
        // The words of the row that adds to a kind's rows ("+ field", "+ needs…")
        addRow(kind, member) { return (member ? this.memberByKind.get(kind) : this.declByKind.get(kind))?.addRow || '+'; }

        // What the page calls a key, a modifier and a way of taking a parameter, and what it says of each
        keyLabel(key) { return this.keyInfo[key]?.label || key; }
        keyHelp(key) { return this.keyInfo[key]?.help || ''; }
        keyAdvanced(key) { return !!this.keyInfo[key]?.advanced; }
        modifierLabel(m) { return this.modifierInfo[m]?.label || m; }
        modifierHelp(m) { return this.modifierInfo[m]?.help || ''; }
        modeLabel(mode) { return this.paramModeInfo[mode]?.label || mode; }
        modeHelp(mode) { return this.paramModeInfo[mode]?.help || ''; }
        modeAdvanced(mode) { return !!this.paramModeInfo[mode]?.advanced; }
        modifierAdvanced(m) { return !!this.modifierInfo[m]?.advanced; }
        // A parameter taken this way is a filter: it has no value to wire (only with, without)
        modeIsFilter(mode) { return mode === 'with' || mode === 'without'; }

        // Whether a kind of declaration (or member) is code: a frame on the board, where the rest are cards
        isCode(kind, member) { return !!(member ? this.memberByKind.get(kind) : this.declByKind.get(kind))?.body; }
        // The kinds of member a kind of declaration can have, as their specs
        memberKinds(kind) {
            return (this.declByKind.get(kind)?.memberKinds || []).map(k => this.memberByKind.get(k)).filter(Boolean);
        }
        // The declarations of a group, in the schema's order
        declsOf(group) { return this.declarations.filter(d => d.group === group); }
        // Whether a kind's keys have one: nothing is offered that its kind can't have
        declHas(kind, key) { return !!this.declByKind.get(kind)?.keys.includes(key); }
        memberHas(kind, key) { return !!this.memberByKind.get(kind)?.keys.includes(key); }
        help(k) { return this.byK.get(k)?.help || ''; }

        node(k) { return this.byK.get(k); }
        isExec(k) { return !!this.byK.get(k)?.exec; }
        // Whether a node of the kind has a value to wire on
        hasValue(k) { return !!this.byK.get(k)?.value; }
        isKnown(k) { return this.byK.has(k); }
        nodeOrder(k) { return this.nodeOrders.get(k) || [...this.elements.node, ...this.elements.nodeEnd]; }
        declOrder(kind) { return this.declByKind.get(kind)?.keys || ['kind', 'id', 'comment', 'name']; }
        memberOrder(kind) { return this.memberByKind.get(kind)?.keys || ['kind', 'id', 'comment', 'name']; }
        element(name) { return this.elements[name] || []; }
        category(k) { return this.categoryByName.get(this.byK.get(k)?.category); }
        color(k) { return this.category(k)?.color || '#777777'; }
        title(k) { return this.byK.get(k)?.title || k; }

        // What a key of an element holds: 'flag', 'strings', 'graph:value', 'graph:body', 'list:<element>',
        // 'type', 'trigger', 'root' (a prefab's), 'decls', 'members', or 'string'
        keyType(key, owner) {
            if (owner && owner.k !== undefined) {
                const spec = this.node(owner.k);
                if (key === 'value' && owner.k === 'bool') return 'flag';
                if (spec && this.wireKeys(spec).has(key)) return 'wire';
                if (key === 'inputs' && spec?.map === 'inputs') return 'inputs';
                if (key === 'params' && owner.k === 'lambda') return 'list:lambdaParam';
                if (key === 'next') return 'wire';
            }
            if (key === 'kind' || key === 'id' || key === 'k' || key === 'x' || key === 'y') return 'fixed';
            if (key === 'comment') return 'comment';
            if (key === 'text') return 'text';
            if (FLAGS.has(key)) return 'flag';
            if (key === 'typeArgs') return 'strings';
            if (STRINGS.has(key)) return 'strings';
            if (TYPES.has(key)) return 'type';
            if (key === 'components') return 'graphs';
            if (GRAPHS[key]) return 'graph:' + GRAPHS[key];
            if (key === 'members') return owner?.kind === 'enum' ? 'list:enumMember' : 'members';
            if (key === 'decls') return 'decls';
            if (key === 'trigger') return 'trigger';
            if (key === 'root') return 'prefabRoot';
            if (ELEMENT_LISTS[key]) return 'list:' + ELEMENT_LISTS[key];
            return 'string';
        }

        // The keys of a node kind that hold wires: single inputs, lists, items, map, exec outputs, next
        wireKeys(spec) {
            if (!spec._wires) {
                const keys = new Set([...(spec.inputs || []), ...(spec.outputs || []), 'next']);
                for (const l of spec.lists || []) keys.add(l.key);
                for (const i of spec.items || []) keys.add(i.key);
                if (spec.map) keys.add(spec.map);
                spec._wires = keys;
            }
            return spec._wires;
        }

        // The key order of an item of a node's `items` list (a switch's case, a literal's init, a switch value's
        // arm), by the element the schema names for it
        itemOrder(k, key) {
            const item = this.node(k)?.items?.find(i => i.key === key);
            return (item && this.elements[item.element]) || [];
        }

        // Whether a node's input isn't there, as its kind's `absentWhen` says: a shorthand member's object
        absent(n, key) {
            const flag = this.node(n.k)?.absentWhen?.[key];
            return !!flag && !!n[flag];
        }

        // Every port of a node, in the order the canvas shows them: the exec in, the value out, data inputs (single,
        // lists, items' wires, the map's), then exec outputs (its own, items', next). `path` is where its wire is
        // kept in the node: ["cond"], ["args", 2], ["cases", 0, "labels", 1]
        ports(n) {
            const spec = this.node(n.k);
            const id = n.id;
            const ports = [];
            if (!spec) {
                if (n.next !== undefined) ports.push({ node: id, input: false, exec: true, path: ['next'], label: 'next' });
                return ports;
            }
            if (spec.exec) ports.push({ node: id, input: true, exec: true, path: null, label: '' });
            if (spec.value) ports.push({ node: id, input: false, exec: false, path: null, label: '' });
            for (const key of spec.inputs || []) {
                if (this.absent(n, key)) continue;
                ports.push({ node: id, input: true, exec: false, path: [key], label: key, key });
            }
            for (const list of spec.lists || []) {
                const a = Array.isArray(n[list.key]) ? n[list.key] : [];
                const names = list.names && Array.isArray(n[list.names]) ? n[list.names] : null;
                for (let i = 0; i < a.length; i++) {
                    const name = names?.[i];
                    const label = name ? name + ':' : a.length === 1 && list.key !== 'args' ? list.label : `${list.label} ${i + 1}`;
                    ports.push({ node: id, input: true, exec: false, path: [list.key, i], label, group: list.key, index: i, key: list.key });
                }
            }
            for (const item of spec.items || []) {
                const a = Array.isArray(n[item.key]) ? n[item.key] : [];
                for (let i = 0; i < a.length; i++) {
                    const o = a[i] || {};
                    const named = item.label && typeof o[item.label] === 'string' ? o[item.label] : null;
                    const title = named ?? `${item.label} ${i + 1}`;
                    for (const wire of item.wires || []) {
                        if (wire.endsWith('[]')) {
                            const key = wire.slice(0, -2);
                            const labels = Array.isArray(o[key]) ? o[key] : [];
                            for (let l = 0; l < labels.length; l++) {
                                ports.push({ node: id, input: true, exec: false, path: [item.key, i, key, l], label: labels.length > 1 ? `${title} ${key.replace(/s$/, '')} ${l + 1}` : title, group: item.key, index: i, key: item.key });
                            }
                        } else {
                            const label = named !== null ? named : (item.wires.length > 1 ? `${title} ${wire}` : title);
                            ports.push({ node: id, input: true, exec: false, path: [item.key, i, wire], label, group: item.key, index: i, key: item.key });
                        }
                    }
                }
            }
            if (spec.map && n[spec.map] && typeof n[spec.map] === 'object') {
                for (const name of Object.keys(n[spec.map])) {
                    ports.push({ node: id, input: true, exec: false, path: [spec.map, name], label: name, group: spec.map, key: spec.map });
                }
            }
            if (spec.exec) {
                for (const key of spec.outputs || []) ports.push({ node: id, input: false, exec: true, path: [key], label: key, key });
                for (const item of spec.items || []) {
                    if (!item.exec) continue;
                    const a = Array.isArray(n[item.key]) ? n[item.key] : [];
                    for (let i = 0; i < a.length; i++) {
                        ports.push({ node: id, input: false, exec: true, path: [item.key, i, item.exec], label: `${item.label} ${i + 1}`, group: item.key, index: i, key: item.key });
                    }
                }
                if (!spec.ends || n.next != null) ports.push({ node: id, input: false, exec: true, path: ['next'], label: 'next', key: 'next' });
            }
            return ports;
        }

        // The id a port's wire holds, or 0
        wire(n, path) {
            let v = n;
            for (const p of path) {
                if (v === null || typeof v !== 'object') return 0;
                v = v[p];
            }
            return typeof v === 'number' ? v : 0;
        }

        // Sets a wire, 0 for none. A node's own key goes when it's 0, as tidec writes them; a list's item and a
        // named input stay, as null, and so do an item's data wires (an arm's pattern, an init's value); an item's
        // exec output (a case's body) goes.
        setWire(n, path, id) {
            if (path.length === 1) {
                set(n, path[0], id ? id : undefined, this.nodeOrder(n.k));
                return;
            }
            let parent = n;
            for (let i = 0; i < path.length - 1; i++) {
                parent = parent?.[path[i]];
            }
            if (!parent || typeof parent !== 'object') return;
            const last = path[path.length - 1];
            if (Array.isArray(parent)) {
                if (last < parent.length) parent[last] = id ? id : null;
                return;
            }
            if (path.length === 2) { // A named input
                parent[last] = id ? id : null;
                return;
            }
            const spec = this.node(n.k);
            const item = spec?.items?.find(i => i.key === path[0]);
            const order = this.itemOrder(n.k, path[0]);
            if (item && item.exec === last) set(parent, last, id ? id : undefined, order);
            else set(parent, last, id ? id : null, order);
        }

        // The ids a node's data inputs are wired from (tidec's graph_data_wires)
        dataWires(n) {
            const out = [];
            for (const p of this.ports(n)) {
                if (!p.input || p.exec || !p.path) continue;
                const w = this.wire(n, p.path);
                if (w) out.push(w);
            }
            return out;
        }

        // The statements a node goes on to: next and its code's
        execWires(n) {
            const out = [];
            for (const p of this.ports(n)) {
                if (p.input || !p.exec) continue;
                const w = this.wire(n, p.path);
                if (w) out.push(w);
            }
            return out;
        }

        // Whether a path of a node is one of its exec outputs
        isExecPath(n, path) {
            return this.ports(n).some(p => !p.input && p.exec && samePath(p.path, path));
        }

        // The name of the local a node declares, or null
        declaredLocal(n) {
            const key = this.node(n.k)?.declares;
            return key && typeof n[key] === 'string' && n[key] ? n[key] : null;
        }

        declSpec(kind) { return this.declByKind.get(kind); }
        memberSpec(kind) { return this.memberByKind.get(kind); }

        // The modifiers a declaration's or member's kind takes
        modifiers(kind, member) {
            return (member ? this.memberByKind.get(kind) : this.declByKind.get(kind))?.modifiers || [];
        }

        // The operators the `op` of an element takes
        ops(owner) {
            if (owner.k === 'binary') return this.operators.binary.map(o => o.op);
            if (owner.k === 'unary') return this.operators.unary.map(o => o.op);
            if (owner.k === 'assign') return this.operators.assign;
            if (owner.k === 'step') return this.operators.step;
            if (owner.kind === 'operator') return this.operators.overloadable;
            return null;
        }

        // What one of Set's operators does, in words: += "goes up by"
        assignLabel(op) { return this.operators.assignInfo?.[op]?.label || ''; }

        // What an operator is called: `unary` for one over a single value (- is Negate there, Subtract between two)
        opTitle(op, unary = false) {
            const own = unary ? this.operators.unary : this.operators.binary, other = unary ? this.operators.binary : this.operators.unary;
            return own.find(o => o.op === op)?.title || other.find(o => o.op === op)?.title || op;
        }
    }

    function samePath(a, b) {
        return !!a && !!b && a.length === b.length && a.every((p, i) => p === b[i]);
    }

    G.Schema = Schema;
    G.samePath = samePath;
    // The page's words (the schema in use's: the app sets G.schemaNow), `w('menu.rename')`, `w('toast.deleted', { what })`;
    // a menu entry's keys by wk
    G.w = (key, vars) => (G.schemaNow ? G.schemaNow.word(key, vars) : key);
    G.wk = key => (G.schemaNow ? G.schemaNow.wordKeys(key) : '');
})(globalThis.TideGraph = globalThis.TideGraph || {});
