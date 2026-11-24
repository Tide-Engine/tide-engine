// A .tidegraph's contents with what an editor does to them: every element with an id found by it, graphs by the
// element and key holding them, nodes made, wired, deleted, copied and pasted, declarations and members made.
// Edits change the JSON in place; the editor (app.js) writes the file after each, and keeps the texts for undo.
(function (G) {
    'use strict';
    const { set, clone, isObject } = G.json;

    // A graph of the file: a body (a routine's, an accessor's) or a value (a constant's, a field's default, a prefab
    // entity's component...) by the id of the element holding it and its key, with an index for a prefab's components
    function slotKey(slot) {
        return slot ? `${slot.owner}.${slot.key}${slot.index !== undefined && slot.index >= 0 ? `[${slot.index}]` : ''}` : '';
    }

    class Doc {
        constructor(root, schema) {
            this.root = root;
            this.schema = schema;
            this.index = new Map();
            this.reindex();
        }

        static parse(text, schema) {
            return new Doc(G.json.parse(text), schema);
        }

        static empty(schema) {
            return new Doc({ tidegraph: 1, nextId: 1, decls: [] }, schema);
        }

        get text() { return G.json.writeFile(this.root); }

        get decls() {
            if (!Array.isArray(this.root.decls)) set(this.root, 'decls', [], this.schema.root);
            return this.root.decls;
        }

        // ---------------------------------------------------------------------------------------------------------
        // Ids

        newId() {
            const id = Math.max(Number(this.root.nextId) || 1, this.maxId + 1);
            set(this.root, 'nextId', id + 1, this.schema.root);
            this.maxId = id;
            return id;
        }

        find(id) { return id ? this.index.get(id) : undefined; }
        get(id) { return this.find(id)?.value; }

        // Finds every element again, after an edit; nextId stays above every id
        reindex() {
            this.index.clear();
            this.maxId = 0;
            this.walkDecls(this.root.decls, null, null);
            if (!(Number(this.root.nextId) > this.maxId)) set(this.root, 'nextId', this.maxId + 1, this.schema.root);
        }

        add(v, role, list, owner, graph, slot) {
            const id = v && typeof v.id === 'number' ? v.id : 0;
            if (!id) return;
            if (id > this.maxId) this.maxId = id;
            this.index.set(id, { id, role, value: v, list, owner, graph, slot });
        }

        walkDecls(decls, owner) {
            if (!Array.isArray(decls)) return;
            for (const d of decls) {
                if (!isObject(d)) continue;
                this.add(d, 'decl', decls, owner);
                this.walkElement(d, 'decl');
            }
        }

        walkElement(d, role) {
            const kind = d.kind;
            this.walkList(d, 'attributes', 'attribute');
            if (kind === 'namespace' && role === 'decl') this.walkDecls(d.decls, d);
            this.walkParams(d, d.params, kind === 'sample' ? 'sampleParam' : 'param');
            if (Array.isArray(d.members)) {
                for (const m of d.members) {
                    if (!isObject(m)) continue;
                    if (kind === 'enum') {
                        this.add(m, 'enumMember', d.members, d);
                        this.walkGraph(m.value, m, { owner: m.id, key: 'value' });
                    } else {
                        this.add(m, 'member', d.members, d);
                        this.walkElement(m, 'member');
                    }
                }
            }
            for (const [key, role2] of [['entries', 'entry'], ['payload', 'payload'], ['accessors', 'accessor']]) {
                if (!Array.isArray(d[key])) continue;
                for (const e of d[key]) {
                    if (!isObject(e)) continue;
                    this.add(e, role2, d[key], d);
                    this.walkList(e, 'attributes', 'attribute');
                    for (const g of ['value', 'default', 'body']) this.walkGraph(e[g], e, { owner: e.id, key: g });
                }
            }
            if (kind === 'prefab' && isObject(d.root)) this.walkPrefab(d.root, null, d);
            for (const g of ['value', 'default', 'body']) this.walkGraph(d[g], d, { owner: d.id, key: g });
        }

        walkList(owner, key, role) {
            if (!Array.isArray(owner[key])) return;
            for (const a of owner[key]) if (isObject(a)) this.add(a, role, owner[key], owner);
        }

        walkParams(owner, list, role) {
            if (!Array.isArray(list)) return;
            for (const p of list) {
                if (!isObject(p)) continue;
                this.add(p, role, list, owner);
                this.walkGraph(p.default, p, { owner: p.id, key: 'default' });
                if (Array.isArray(p.query)) this.walkParams(owner, p.query, 'param');
            }
        }

        walkPrefab(p, list, owner) {
            this.add(p, 'prefabNode', list, owner);
            if (Array.isArray(p.components)) {
                p.components.forEach((c, i) => this.walkGraph(c, p, { owner: p.id, key: 'components', index: i }));
            }
            if (Array.isArray(p.children)) for (const c of p.children) if (isObject(c)) this.walkPrefab(c, p.children, owner);
        }

        walkGraph(g, owner, slot) {
            if (!isObject(g)) return;
            if (Array.isArray(g.nodes)) for (const n of g.nodes) if (isObject(n)) this.add(n, 'node', g.nodes, owner, g, slot);
            if (Array.isArray(g.notes)) for (const n of g.notes) if (isObject(n)) this.add(n, 'note', g.notes, owner, g, slot);
        }

        // ---------------------------------------------------------------------------------------------------------
        // Graphs

        // The graph a slot names, or undefined
        graph(slot) {
            if (!slot) return undefined;
            const owner = this.get(slot.owner);
            if (!owner) return undefined;
            const g = slot.key === 'components' ? owner.components?.[slot.index] : owner[slot.key];
            return isObject(g) ? g : undefined;
        }

        // The graph of a slot, made empty if it isn't there
        ensureGraph(slot) {
            const have = this.graph(slot);
            if (have) return have;
            const e = this.find(slot.owner);
            if (!e) return undefined;
            const g = slot.key === 'body' ? { entry: null, nodes: [] } : { root: null, nodes: [] };
            if (slot.key === 'components') {
                if (!Array.isArray(e.value.components)) e.value.components = [];
                e.value.components[slot.index] = g;
            } else {
                set(e.value, slot.key, g, this.orderOf(e));
            }
            this.reindex();
            return g;
        }

        // Every graph of the file, with its slot and what holds it
        graphs() {
            const out = [];
            for (const e of this.index.values()) {
                if (e.role === 'node' || e.role === 'note') continue;
                for (const key of ['body', 'value', 'default']) {
                    if (isObject(e.value[key]) && Array.isArray(e.value[key].nodes)) out.push({ slot: { owner: e.id, key }, owner: e });
                }
                if (e.role === 'prefabNode' && Array.isArray(e.value.components)) {
                    e.value.components.forEach((c, index) => isObject(c) && out.push({ slot: { owner: e.id, key: 'components', index }, owner: e }));
                }
            }
            return out;
        }

        static isBody(g) { return !!g && !('root' in g); }

        // The key order of an element's kind
        orderOf(e) {
            const s = this.schema;
            switch (e.role) {
                case 'decl': return s.declOrder(e.value.kind);
                case 'member': return s.memberOrder(e.value.kind);
                case 'node': return s.nodeOrder(e.value.k);
                case 'param': return s.element('param');
                case 'sampleParam': return s.element('sampleParam');
                case 'payload': return s.element('payload');
                case 'attribute': return s.element('attribute');
                case 'enumMember': return s.element('enumMember');
                case 'entry': return s.element('entry');
                case 'accessor': return s.element('accessor');
                case 'prefabNode': return s.element('prefabNode');
                case 'note': return s.element('note');
                default: return null;
            }
        }

        static nodes(g) {
            if (!Array.isArray(g.nodes)) g.nodes = [];
            return g.nodes;
        }

        static nodeIn(g, id) {
            return g && Array.isArray(g.nodes) ? g.nodes.find(n => n && n.id === id) : undefined;
        }

        // A new node of a kind, at a place, with what its kind starts with: a list or an item list empty (one case
        // for a switch, one arm for a switch value), a map empty, and the fields it can't be without
        newNode(k, x, y, fields) {
            const s = this.schema;
            const spec = s.node(k);
            const order = s.nodeOrder(k);
            const n = {};
            set(n, 'id', this.newId(), order);
            set(n, 'k', k, order);
            set(n, 'x', Math.round(x), order);
            set(n, 'y', Math.round(y), order);
            for (const list of spec?.lists || []) set(n, list.key, [], order);
            for (const item of spec?.items || []) {
                const blank = {};
                for (const w of item.wires || []) {
                    if (w.endsWith('[]')) blank[w.slice(0, -2)] = [null];
                    else if (k !== 'literal') blank[w] = null;
                }
                set(n, item.key, k === 'literal' ? [] : [blank], order);
            }
            if (spec?.map) set(n, spec.map, {}, order);
            for (const key of spec?.keys || []) {
                if (n[key] !== undefined || s.wireKeys(spec).has(key)) continue;
                const v = defaultField(k, key, s);
                if (v !== undefined) set(n, key, v, order);
            }
            for (const [key, v] of Object.entries(fields || {})) set(n, key, v, order);
            return n;
        }

        addNode(g, k, x, y, fields) {
            const n = this.newNode(k, x, y, fields);
            Doc.nodes(g).push(n);
            this.reindex();
            return n;
        }

        // The exec output that goes to a statement, if one does: { node, path }
        execSourceOf(g, target) {
            for (const n of Doc.nodes(g)) {
                if (!this.schema.isExec(n.k)) continue;
                for (const p of this.schema.ports(n)) {
                    if (!p.input && p.exec && this.schema.wire(n, p.path) === target) return { node: n.id, path: p.path };
                }
            }
            return null;
        }

        // Whether node `a` reads node `b`, through any of its inputs
        reads(g, a, b) {
            const seen = new Set();
            const stack = [a];
            while (stack.length) {
                const id = stack.pop();
                if (seen.has(id)) continue;
                seen.add(id);
                if (id === b) return true;
                const n = Doc.nodeIn(g, id);
                if (n) stack.push(...this.schema.dataWires(n));
            }
            return false;
        }

        // Whether statement `a` goes on, along exec wires, to statement `b`
        goesTo(g, a, b) {
            const seen = new Set();
            const stack = [a];
            while (stack.length) {
                const id = stack.pop();
                if (seen.has(id)) continue;
                seen.add(id);
                if (id === b) return true;
                const n = Doc.nodeIn(g, id);
                if (n && this.schema.isExec(n.k)) stack.push(...this.schema.execWires(n));
            }
            return false;
        }

        // Wires `from` into a node's port at `path`: a value into a data input, or a statement after an exec output.
        // The wiring rules of the schema: an input takes one wire (the new one replaces it), a statement runs from one
        // place (its old wire goes), and no value is made of itself. Returns null, or why it can't be.
        connect(g, node, path, from) {
            const s = this.schema;
            const n = Doc.nodeIn(g, node);
            const source = Doc.nodeIn(g, from);
            if (!n || !source) return 'a wire joins two nodes of one graph';
            if (s.isExecPath(n, path)) {
                if (!s.isExec(source.k)) return `${s.title(source.k)} is a value, not a statement`;
                if (from === node) return "a statement can't follow itself";
                if (g.entry === from) return "the body's first statement runs from its start";
                if (this.goesTo(g, from, node)) return 'that would run in a loop: a loop is a While or a For';
                const other = this.execSourceOf(g, from);
                if (other && !(other.node === node && G.samePath(other.path, path))) {
                    s.setWire(Doc.nodeIn(g, other.node), other.path, 0);
                }
            } else {
                if (!s.hasValue(source.k)) return `${s.title(source.k)} is a statement, which has no value`;
                if (from === node || this.reads(g, from, node)) return "a value can't be made of itself";
            }
            s.setWire(n, path, from);
            return null;
        }

        disconnect(g, node, path) {
            const n = Doc.nodeIn(g, node);
            if (n) this.schema.setWire(n, path, 0);
        }

        // Wires a statement to run first in a body (null for none); the statement's old wire goes
        setEntry(g, id) {
            if (id) {
                const n = Doc.nodeIn(g, id);
                if (!n || !this.schema.isExec(n.k)) return `${this.schema.title(n?.k)} is a value, not a statement`;
                const other = this.execSourceOf(g, id);
                if (other) this.schema.setWire(Doc.nodeIn(g, other.node), other.path, 0);
            }
            g.entry = id || null;
            return null;
        }

        // Wires a value graph's value (null for none)
        setRoot(g, id) {
            if (id) {
                const n = Doc.nodeIn(g, id);
                if (!n || !this.schema.hasValue(n.k)) return `${this.schema.title(n?.k)} is a statement, which has no value`;
            }
            g.root = id || null;
            return null;
        }

        // Removes nodes and notes, and every wire to them; a statement taken from a chain leaves the statements
        // around it joined
        deleteNodes(g, ids) {
            const s = this.schema;
            const gone = new Set(ids);
            for (const id of ids) {
                const n = Doc.nodeIn(g, id);
                if (!n || !s.isExec(n.k)) continue;
                let next = n.next || 0;
                while (next && gone.has(next)) next = Doc.nodeIn(g, next)?.next || 0;
                if (g.entry === id) g.entry = next || null;
                const from = this.execSourceOf(g, id);
                if (from && !gone.has(from.node)) s.setWire(Doc.nodeIn(g, from.node), from.path, next);
                delete n.next;
            }
            g.nodes = Doc.nodes(g).filter(n => !gone.has(n.id));
            if (Array.isArray(g.notes)) {
                g.notes = g.notes.filter(n => !gone.has(n.id));
                if (!g.notes.length) delete g.notes;
            }
            for (const n of g.nodes) {
                for (const p of s.ports(n)) if (p.path && gone.has(s.wire(n, p.path))) s.setWire(n, p.path, 0);
            }
            if ('root' in g && gone.has(g.root)) g.root = null;
            if ('entry' in g && gone.has(g.entry)) g.entry = null;
            this.reindex();
        }

        // The nodes `ids` and notes `noteIds` as text to copy, as Unity's editor copies them: wires to nodes not
        // among them are dropped when pasted, and a local's read keeps its declaration's id
        static copy(g, ids, noteIds) {
            const clip = { tidegraphClip: 1, nodes: Doc.nodes(g).filter(n => ids.has(n.id)).map(clone) };
            const notes = (g.notes || []).filter(n => noteIds && noteIds.has(n.id)).map(clone);
            if (notes.length) clip.notes = notes;
            return G.json.write(clip);
        }

        static isClip(text) {
            if (typeof text !== 'string' || !text.trimStart().startsWith('{')) return false;
            try {
                return 'tidegraphClip' in JSON.parse(text);
            } catch {
                return false;
            }
        }

        // Pastes copied nodes into a graph, `dx, dy` from where they were, with new ids: their wires between them
        // follow, and the rest are dropped. Returns the new nodes' and notes' ids.
        paste(g, text, dx, dy) {
            let clip;
            try {
                clip = JSON.parse(text);
            } catch {
                return [];
            }
            if (!clip || !Array.isArray(clip.nodes)) return [];
            const s = this.schema;
            const map = new Map();
            for (const n of clip.nodes) map.set(n.id, this.newId());
            const made = [];
            for (const original of clip.nodes) {
                const n = clone(original);
                n.id = map.get(original.id);
                n.x = Math.round((Number(n.x) || 0) + dx);
                n.y = Math.round((Number(n.y) || 0) + dy);
                for (const p of s.ports(n)) {
                    if (!p.path) continue;
                    const w = s.wire(n, p.path);
                    if (w) s.setWire(n, p.path, map.get(w) || 0);
                }
                if (n.k === 'local' && map.has(n.ref)) n.ref = map.get(n.ref);
                Doc.nodes(g).push(n);
                made.push(n.id);
            }
            if (Array.isArray(clip.notes) && clip.notes.length) {
                if (!Array.isArray(g.notes)) set(g, 'notes', [], Doc.isBody(g) ? s.element('body') : s.element('value'));
                for (const original of clip.notes) {
                    const n = clone(original);
                    n.id = this.newId();
                    n.x = Math.round((Number(n.x) || 0) + dx);
                    n.y = Math.round((Number(n.y) || 0) + dy);
                    g.notes.push(n);
                    made.push(n.id);
                }
            }
            this.reindex();
            return made;
        }

        // What runs and what's read: the statements reached from the entry and the values they (or the root) read.
        // tidec leaves the rest out, and the canvas dims it.
        reached(g) {
            const s = this.schema;
            const reached = new Set();
            const byId = new Map(Doc.nodes(g).map(n => [n.id, n]));
            const stack = [];
            if (g.entry) stack.push(g.entry);
            if (g.root) stack.push(g.root);
            while (stack.length) {
                const id = stack.pop();
                const n = byId.get(id);
                if (!n || reached.has(id)) continue;
                reached.add(id);
                stack.push(...s.dataWires(n));
                if (s.isExec(n.k)) stack.push(...s.execWires(n));
            }
            return reached;
        }

        // The nodes of a graph that declare locals: what a local node can read
        locals(g) {
            return Doc.nodes(g).filter(n => this.schema.declaredLocal(n));
        }

        // The declaration or member a graph's slot is in: its routine, for its parameters
        routineOf(slot) {
            const e = this.find(slot?.owner);
            if (!e) return undefined;
            if (e.role === 'accessor') return e.owner;
            if (e.role === 'param' || e.role === 'payload') return e.owner;
            return e.value;
        }

        // ---------------------------------------------------------------------------------------------------------
        // Declarations and members

        // A name of `base` no element of the list has
        uniqueName(list, base) {
            const names = new Set((list || []).map(e => e?.name));
            if (!names.has(base)) return base;
            for (let i = 2; ; i++) if (!names.has(base + i)) return base + i;
        }

        // A new declaration or member of a kind, with what its kind takes: each of its keys the schema lists, made
        // the way the format writes an empty one
        newElement(kind, member, name) {
            const s = this.schema;
            const spec = member ? s.memberSpec(kind) : s.declSpec(kind);
            const order = spec?.keys || ['kind', 'id', 'name'];
            const e = {};
            set(e, 'kind', kind, order);
            set(e, 'id', this.newId(), order);
            for (const key of order) {
                if (e[key] !== undefined) continue;
                const v = this.defaultKey(kind, member, key, spec, name);
                if (v !== undefined) set(e, key, v, order);
            }
            return e;
        }

        defaultKey(kind, member, key, spec, name) {
            switch (key) {
                case 'name':
                    if (kind === 'sample') return 'Sample';
                    return name;
                case 'params': return spec?.params || kind === 'sample' ? [] : undefined;
                case 'body': return spec?.body ? { entry: null, nodes: [] } : undefined;
                case 'members': return spec?.members || kind === 'enum' ? [] : undefined;
                case 'decls': return kind === 'namespace' ? [] : undefined;
                case 'entries': return [];
                case 'payload': return [];
                case 'type':
                    if (kind === 'asset') return 'Texture';
                    if (kind === 'tweak') return 'float';
                    return 'int';
                case 'returnType':
                    if (kind === 'function' || kind === 'method') return 'void';
                    if (kind === 'operator') return 'int';
                    return undefined;
                case 'op': return kind === 'operator' ? '+' : undefined;
                case 'text': return '// Tide text';
                case 'accessors': return [this.newAccessor('get')];
                case 'trigger': return { type: 'Event', name: 'e' };
                case 'root': return kind === 'prefab' ? this.newPrefabNode(null) : undefined;
                case 'value':
                    if (kind === 'const') return this.valueGraph('int', '0');
                    if (kind === 'tweak') return this.valueGraph('float', '1.0');
                    if (kind === 'asset') return this.valueGraph('string', 'file.png');
                    return undefined;
                default: return undefined;
            }
        }

        newAccessor(word) {
            return { id: this.newId(), word, body: { entry: null, nodes: [] } };
        }

        newParam(type, name) {
            return { id: this.newId(), mode: 'read', type, name };
        }

        newNamed(name, valueKind, valueText) {
            const e = { id: this.newId(), name };
            if (valueKind) e.value = this.valueGraph(valueKind, valueText);
            return e;
        }

        newAttribute(name) {
            return { id: this.newId(), name };
        }

        newPrefabNode(name) {
            const p = { id: this.newId() };
            if (name) p.name = name;
            p.components = [];
            p.children = [];
            return p;
        }

        // A value graph of one literal node
        valueGraph(k, text) {
            const order = this.schema.nodeOrder(k);
            const n = this.newNode(k, -180, 0);
            if (k === 'bool') set(n, 'value', text === 'true', order);
            else if (text !== undefined) set(n, 'text', text, order);
            return { root: n.id, nodes: [n] };
        }

        // A copy of an element with new ids for everything with one in it, its nodes' wires and locals' reads
        // following
        duplicate(v) {
            const copy = clone(v);
            this.renumber(copy);
            return copy;
        }

        renumber(v) {
            if (Array.isArray(v)) {
                for (const item of v) this.renumber(item);
                return;
            }
            if (!isObject(v)) return;
            if (Array.isArray(v.nodes) && ('entry' in v || 'root' in v)) {
                const s = this.schema;
                const map = new Map();
                for (const n of v.nodes) map.set(n.id, this.newId());
                for (const n of v.nodes) {
                    n.id = map.get(n.id);
                    for (const p of s.ports(n)) {
                        if (!p.path) continue;
                        const w = s.wire(n, p.path);
                        if (w && map.has(w)) s.setWire(n, p.path, map.get(w));
                    }
                    if (n.k === 'local' && map.has(n.ref)) n.ref = map.get(n.ref);
                }
                for (const key of ['entry', 'root']) if (map.has(v[key])) v[key] = map.get(v[key]);
                for (const n of v.notes || []) n.id = this.newId();
                return;
            }
            if (typeof v.id === 'number') v.id = this.newId();
            for (const child of Object.values(v)) this.renumber(child);
        }

        // The names of the types the file declares, for the type lists
        declaredTypes() {
            const kinds = new Set(['component', 'singleton', 'struct', 'event', 'input', 'scene', 'enum', 'alias']);
            const out = [];
            for (const e of this.index.values()) {
                if (e.role === 'decl' && kinds.has(e.value.kind) && e.value.name) out.push(e.value.name);
            }
            return out;
        }

        // A readable name of an element, for the outline and the canvas's title
        describe(e) {
            if (!e) return '';
            const v = e.value;
            const name = v.name;
            switch (e.role) {
                case 'decl':
                    if (v.kind === 'settings') return 'settings';
                    if (v.kind === 'text') return 'text: ' + firstLine(v.text);
                    if (v.kind === 'handler') return `event(${v.trigger?.type ?? ''}) ${name ?? ''}`;
                    return `${v.kind} ${name ?? ''}`.trim();
                case 'member':
                    switch (v.kind) {
                        case 'field': return `${v.type ?? ''} ${name ?? ''}`.trim();
                        case 'property': return `${v.type ?? ''} ${name ?? ''} { }`.trim();
                        case 'method': return `${name ?? ''}()`;
                        case 'operator': return `operator ${v.op ?? ''}`;
                        case 'tweak': return `tweak ${name ?? ''}`;
                        case 'sample': return `${name ?? 'Sample'}()`;
                        case 'sanitize': return 'Sanitize()';
                        case 'predict': return 'Predict()';
                        case 'prepare': return 'prepare()';
                        default: return 'text: ' + firstLine(v.text);
                    }
                case 'accessor': return v.word ?? '';
                case 'prefabNode': return name ? `entity ${name}` : 'root entity';
                case 'param':
                case 'sampleParam':
                case 'payload': return `${v.type ?? ''} ${name ?? ''}`.trim();
                case 'attribute': return `[${name ?? ''}${v.args !== undefined ? `(${v.args})` : ''}]`;
                case 'node': return this.schema.title(v.k);
                case 'note': return 'note';
                default: return name ?? e.role;
            }
        }
    }

    function firstLine(s) {
        if (!s) return '';
        const i = s.indexOf('\n');
        return i < 0 ? s : s.slice(0, i) + ' ...';
    }

    // A field a new node starts with, so it's Tide: by key, as the format names them
    function defaultField(k, key, s) {
        switch (key) {
            case 'text':
                if (k === 'int') return '0';
                if (k === 'float') return '0.0';
                if (k === 'char') return "'a'";
                return '';
            case 'value': return k === 'bool' ? false : undefined;
            case 'name':
                if (k === 'call') return 'Function';
                if (k === 'method') return 'Method';
                if (k === 'outVar') return 'result';
                if (k === 'foreach') return 'item';
                if (k === 'parallel') return 'at';
                if (k === 'var') return 'value';
                return 'name';
            case 'member': return 'member';
            case 'op': return s.ops({ k })?.[0];
            case 'type': return k === 'is' || k === 'literal' ? 'Type' : k === 'has' ? 'float3' : undefined;
            case 'subject': return 'V';
            case 'field': return 'normal';
            case 'parts': return [''];
            case 'params': return k === 'lambda' ? [] : undefined;
            default: return undefined;
        }
    }

    G.Doc = Doc;
    G.slotKey = slotKey;
})(globalThis.TideGraph = globalThis.TideGraph || {});
