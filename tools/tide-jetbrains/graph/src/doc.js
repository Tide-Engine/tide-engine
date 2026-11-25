// A .tidegraph's contents with what an editor does to them: every element with an id found by it, graphs by the
// element and key holding them, nodes made, wired, deleted, copied and pasted, declarations and members made, and
// the board's view of it: which declarations are cards and which are frames, a frame's pins (its parameters), and
// which nodes are shown in the input that reads them (a number, a name, a path of fields) and so aren't drawn.
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
            if (!n || !source) return s.word('why.wire.graph');
            if (s.isExecPath(n, path)) {
                if (!s.isExec(source.k)) return s.word('why.wire.value', { what: s.title(source.k) });
                if (from === node) return s.word('why.wire.self');
                if (g.entry === from) return s.word('why.wire.first');
                if (this.goesTo(g, from, node)) return s.word('why.wire.loop');
                const other = this.execSourceOf(g, from);
                if (other && !(other.node === node && G.samePath(other.path, path))) {
                    s.setWire(Doc.nodeIn(g, other.node), other.path, 0);
                }
            } else {
                if (!s.hasValue(source.k)) return s.word('why.wire.statement', { what: s.title(source.k) });
                if (from === node || this.reads(g, from, node)) return s.word('why.wire.itself');
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
                if (!n || !this.schema.isExec(n.k)) return this.schema.word('why.wire.value', { what: this.schema.title(n?.k) });
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
                if (!n || !this.schema.hasValue(n.k)) return this.schema.word('why.wire.statement', { what: this.schema.title(n?.k) });
            }
            g.root = id || null;
            return null;
        }

        // Removes nodes and notes, and every wire to them; a statement taken from a chain leaves the statements
        // around it joined, and a reroute taken from a wire leaves what was wired into it wired to what it went to
        deleteNodes(g, ids) {
            const s = this.schema;
            const gone = new Set(ids);
            const knots = new Map(); // A reroute that goes -> what's wired into it, past others that go too
            for (const id of ids) {
                const n = Doc.nodeIn(g, id);
                if (n?.k !== 'reroute') continue;
                let from = n.value || 0;
                for (let depth = 0; from && gone.has(from) && depth < 256; depth++) {
                    const next = Doc.nodeIn(g, from);
                    if (next?.k !== 'reroute') { from = 0; break; }
                    from = next.value || 0;
                }
                knots.set(id, from);
            }
            if (knots.size) {
                for (const n of Doc.nodes(g)) {
                    if (gone.has(n.id)) continue;
                    for (const p of s.ports(n)) {
                        if (!p.input || p.exec || !p.path) continue;
                        const w = s.wire(n, p.path);
                        if (knots.get(w)) s.setWire(n, p.path, knots.get(w));
                    }
                }
                if (knots.get(g.root)) g.root = knots.get(g.root);
            }
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
        newElement(kind, member, name, at) {
            const s = this.schema;
            const spec = member ? s.memberSpec(kind) : s.declSpec(kind);
            const order = spec?.keys || ['kind', 'id', 'name'];
            const e = {};
            set(e, 'kind', kind, order);
            set(e, 'id', this.newId(), order);
            if (at && order.includes('x')) {
                set(e, 'x', Math.round(at.x), order);
                set(e, 'y', Math.round(at.y), order);
            }
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
                    if (kind === 'const') return 'int';
                    return 'float';
                case 'returnType':
                    if (kind === 'function' || kind === 'method') return 'void';
                    if (kind === 'operator') return 'int';
                    return undefined;
                case 'op': return kind === 'operator' ? '+' : undefined;
                case 'text': return '// Tide text';
                case 'accessors': return [this.newAccessor('get')];
                case 'trigger': {
                    // The first event the file has, to start with: the entry's picker changes it
                    const event = [...this.index.values()].find(e => e.role === 'decl' && e.value.kind === 'event')?.value.name || 'Event';
                    return { type: event, name: event[0].toLowerCase() + event.slice(1) };
                }
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

        newParam(type, name, mode = 'read') {
            const p = { id: this.newId(), mode };
            if (type !== undefined) p.type = type;
            if (name !== undefined) p.name = name;
            return p;
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

        // ---------------------------------------------------------------------------------------------------------
        // The board (notes/visual-scripting.md, The experience: one board)

        // What's on the board, in the file's order: a card for each declaration of data, a frame for each of code
        // and for each member with code of its own (a method, an accessor...), and namespace blocks around theirs.
        // { id, value, card | frame, spec, type (a member's or an accessor's type), property (an accessor's),
        //   namespace (the block it's in, or null), list (the list it's in) }
        items() {
            const s = this.schema;
            const out = [];
            const visit = (decls, namespace) => {
                if (!Array.isArray(decls)) return;
                for (const d of decls) {
                    if (!isObject(d)) continue;
                    if (d.kind === 'namespace') {
                        out.push({ id: d.id, value: d, region: true, spec: s.declSpec('namespace'), namespace, list: decls });
                        visit(d.decls, d);
                        continue;
                    }
                    const spec = s.declSpec(d.kind);
                    if (spec?.body) {
                        out.push({ id: d.id, value: d, frame: true, spec, namespace, list: decls });
                        continue;
                    }
                    out.push({ id: d.id, value: d, card: true, spec, namespace, list: decls });
                    for (const m of Array.isArray(d.members) ? d.members : []) {
                        if (!isObject(m)) continue;
                        const mspec = s.memberSpec(m.kind);
                        if (mspec?.body) out.push({ id: m.id, value: m, frame: true, spec: mspec, member: true, type: d, namespace, list: d.members });
                        for (const a of Array.isArray(m.accessors) ? m.accessors : []) {
                            if (isObject(a)) out.push({ id: a.id, value: a, frame: true, accessor: true, property: m, type: d, namespace, list: m.accessors });
                        }
                    }
                }
            };
            visit(this.root.decls, null);
            return out;
        }

        // What a frame's code takes, each a pin on its entry that its nodes read by name: a handler's event, a
        // command's values, parameters (a query by its own name), and `value` in a property's set.
        // { id (the element's, 0 for one with none), name, type, element, from: 'trigger' | 'payload' | 'param' }
        pins(routine, accessor) {
            const out = [];
            if (accessor) {
                if (accessor.word === 'set') out.push({ id: 0, name: 'value', type: routine?.type, from: 'value' });
                return out;
            }
            if (!routine) return out;
            const t = routine.trigger;
            if (isObject(t) && t.name) out.push({ id: 0, name: t.name, type: t.changed ? undefined : t.type, element: t, from: 'trigger' });
            for (const p of Array.isArray(routine.payload) ? routine.payload : []) {
                if (isObject(p) && p.name) out.push({ id: p.id, name: p.name, type: p.type, element: p, from: 'payload' });
            }
            for (const p of Array.isArray(routine.params) ? routine.params : []) {
                if (!isObject(p) || !p.name || this.schema.modeIsFilter(p.mode)) continue;
                out.push({ id: p.id, name: p.name, type: p.mode === 'query' ? undefined : p.type, element: p, from: 'param' });
            }
            return out;
        }

        // The pins of the frame a graph's slot is: none for a value's graph
        pinsOfSlot(slot) {
            if (!slot || slot.key !== 'body') return [];
            const e = this.find(slot.owner);
            if (!e) return [];
            return e.role === 'accessor' ? this.pins(e.owner, e.value) : this.pins(e.value);
        }

        // The nodes that go with `ids` when they're copied, moved or deleted: a statement's call, and the values shown
        // in their inputs (a name, a path, a number), which are nodes of the file though not of the board
        withFolded(g, ids, a = this.analyse(g)) {
            const out = new Set(ids);
            const stack = [...ids];
            while (stack.length) {
                const id = stack.pop();
                const n = a.byId.get(id);
                if (!n) continue;
                const also = [...this.schema.dataWires(n).filter(w => a.folded.has(w))];
                if (a.merged.has(id)) also.push(a.merged.get(id));
                for (const w of also) {
                    if (out.has(w)) continue;
                    out.add(w);
                    stack.push(w);
                }
            }
            return out;
        }

        // How a graph's nodes are drawn (notes/visual-scripting.md, Round four: full Blueprint). What never changes
        // is in the input that reads it, as a Blueprint pin's default, and is no node of the board: a literal (the
        // schema's `inline.always`), a name that starts with a capital (`named`: a type, a namespace, an enum, a
        // constant), a member of what is itself shown there (`path`: Math.PI, .Run), a `-` over a number
        // (`negative`), a vector or a colour made of numbers alone (`numbers`: float2(16, 14)). A variable is a Get
        // node, which any number of inputs read. What a Set or a step changes, a name or a member read by it alone, is
        // in its title (`titled`: Set score). These are `folded`, as tidec's layout folds them (graph_layout_fold),
        // so its sizes are the board's.
        // `shown`: a folded node's id -> { node, path }, the drawn node and its input that shows it. `titled`: a Set's
        // or a step's id -> what it changes. A call that one statement alone runs is drawn as that statement
        // (`merged`: the statement's id -> the call's, `mergedInto` the other way). `rootReads` counts a value graph's
        // root as read once, by its Result.
        analyse(g, { rootReads = false } = {}) {
            const s = this.schema;
            const nodes = Doc.nodes(g);
            const byId = new Map(nodes.map(n => [n.id, n]));
            const readers = new Map();
            const wiresOf = new Map();
            const readBy = new Map(); // A node read once: by which node, at which path
            for (const n of nodes) {
                const wires = [];
                for (const p of s.ports(n)) {
                    if (!p.input || p.exec || !p.path) continue;
                    const w = s.wire(n, p.path);
                    if (!w) continue;
                    wires.push(w);
                    readers.set(w, (readers.get(w) || 0) + 1);
                    readBy.set(w, { node: n.id, path: p.path });
                }
                wiresOf.set(n.id, wires);
            }
            if (rootReads && g.root) {
                readers.set(g.root, (readers.get(g.root) || 0) + 1);
                readBy.set(g.root, { node: -2, path: null });
            }
            const state = new Map(); // 1 being worked out, 2 no, 3 yes
            // What a Set or a step changes (its target), a name, a local or a member read by it alone, is in its title
            const titled = new Map();
            for (const n of nodes) {
                if (!s.titled(n.k)) continue;
                const at = s.wire(n, ['target']);
                const target = at ? byId.get(at) : null;
                if (!target || readers.get(at) !== 1 || !['name', 'local', 'member'].includes(target.k)) continue;
                state.set(at, 3);
                titled.set(n.id, at);
            }
            const folds = n => {
                if (state.has(n.id)) return state.get(n.id) === 3;
                state.set(n.id, 1);
                const kind = readers.get(n.id) === 1 ? s.inlineKind(n.k) : 0;
                const inputs = wiresOf.get(n.id) || [];
                const of = inputs.length === 1 ? byId.get(inputs[0]) : null;
                let yes = false;
                if (kind === 1) yes = inputs.length === 0;
                else if (kind === 2) yes = inputs.length === 0 || (!!of && s.inlineKind(of.k) !== 3 && folds(of));
                else if (kind === 5) yes = inputs.length === 0 && capital(n.name);
                else if (kind === 3) yes = n.op === '-' && !!of && (of.k === 'int' || of.k === 'float') && folds(of);
                else if (kind === 4 && s.numbersOf.has(n.name)) { // float2(16, -14): each argument a number shown there
                    yes = inputs.length > 0;
                    for (let i = 0; yes && i < inputs.length; i++) {
                        const arg = byId.get(inputs[i]);
                        yes = !!arg && (arg.k === 'int' || arg.k === 'float' || s.inlineKind(arg.k) === 3) && folds(arg);
                    }
                }
                state.set(n.id, yes ? 3 : 2);
                return yes;
            };
            const folded = new Set();
            for (const n of nodes) if (folds(n)) folded.add(n.id);
            const shown = new Map();
            for (const id of folded) {
                let at = readBy.get(id);
                for (let depth = 0; at && folded.has(at.node) && depth < 256; depth++) at = readBy.get(at.node);
                if (at) shown.set(id, at);
            }
            const merged = new Map(), mergedInto = new Map();
            for (const n of nodes) {
                if (n.k !== 'eval' || !n.value) continue;
                const call = byId.get(n.value);
                if (!call || (call.k !== 'call' && call.k !== 'method') || readers.get(call.id) !== 1) continue;
                merged.set(n.id, call.id);
                mergedInto.set(call.id, n.id);
            }
            return { folded, shown, titled, merged, mergedInto, readers, byId };
        }

        // How big tidec takes a graph's nodes to be (graph_layout_measure, graph_layout_fold): what's laid out and what
        // isn't drawn go by it. A value shown in an input has no size, and what reads it is as wide as its widest
        // row. Returns a node's id -> { w, h }, for the nodes that are drawn.
        sizes(g, analysis = this.analyse(g)) {
            const schema = this.schema;
            const text = n => {
                let widest = 0;
                for (const v of Object.values(n)) if (typeof v === 'string' && v.length > widest) widest = v.length;
                return widest;
            };
            const shown = (n, depth) => {
                const wires = schema.dataWires(n);
                const of = wires.length === 1 && depth < 64 ? analysis.byId.get(wires[0]) : null;
                return text(n) + (of && analysis.folded.has(of.id) ? 1 + shown(of, depth + 1) : 0);
            };
            const sizes = new Map();
            for (const n of Doc.nodes(g)) {
                if (analysis.folded.has(n.id)) continue;
                const wires = schema.dataWires(n);
                if (n.k === 'reroute') { // A knot
                    sizes.set(n.id, { w: 34, h: 18 });
                    continue;
                }
                if (n.k === 'name' || n.k === 'local' || n.k === 'member' || n.k === 'this') { // A Get, and a member's Target pin
                    sizes.set(n.id, { w: 48 + Math.min(200, text(n) * 8) + (n.k === 'member' && wires.length ? 60 : 0), h: 32 });
                    continue;
                }
                let w = 140 + Math.min(260, text(n) * 8);
                for (const id of wires) {
                    const read = analysis.byId.get(id);
                    if (read && analysis.folded.has(id)) w = Math.max(w, 100 + Math.min(260, shown(read, 0) * 8 + 24));
                }
                sizes.set(n.id, { w, h: 48 + 24 * (wires.length + schema.execWires(n).length + 1) });
            }
            return sizes;
        }

        // A simple value as it reads where it's shown: 12, "text", wobble.phase, .Run, -4
        valueText(g, id, depth = 0) {
            const n = Doc.nodeIn(g, id);
            if (!n || depth > 64) return '';
            switch (n.k) {
                case 'int': case 'float': case 'char': return String(n.text ?? '');
                case 'string': return `"${n.text ?? ''}"`;
                case 'bool': return n.value ? 'true' : 'false';
                case 'this': return 'self';
                case 'default': case 'null': return n.k;
                case 'name': return String(n.name ?? '');
                case 'local': return this.schema.declaredLocal(Doc.nodeIn(g, n.ref) || {}) || '?';
                case 'member': return `${n.shorthand || !n.object ? '' : this.valueText(g, n.object, depth + 1)}.${n.member ?? ''}`;
                case 'unary': return `${n.op ?? ''}${this.valueText(g, n.value, depth + 1)}`;
                case 'call': return `${n.name ?? ''}(${(Array.isArray(n.args) ? n.args : []).map(a => this.valueText(g, a, depth + 1)).join(', ')})`;
                default: return this.schema.title(n.k);
            }
        }

        // The nodes of a path, from what it starts at to its last member: [name wobble, member phase]
        pathNodes(g, id) {
            const out = [];
            for (let n = Doc.nodeIn(g, id), depth = 0; n && depth < 64; depth++) {
                out.unshift(n);
                n = n.k === 'member' && !n.shorthand && n.object ? Doc.nodeIn(g, n.object) : null;
            }
            return out;
        }

        // A knot put on the data wire into a node's input (`path`; null for a value graph's result), at a place.
        // Returns it, or null when nothing is wired there.
        addReroute(g, node, path, at) {
            const s = this.schema;
            const reader = path ? Doc.nodeIn(g, node) : null;
            const from = path ? (reader ? s.wire(reader, path) : 0) : g.root;
            if (!from) return null;
            const knot = this.newNode('reroute', at.x, at.y);
            s.setWire(knot, ['value'], from);
            Doc.nodes(g).push(knot);
            if (path) s.setWire(reader, path, knot.id);
            else g.root = knot.id;
            this.reindex();
            return knot;
        }

        // The statements of a body, as its code runs them: id -> { id, chain, key, index, parent }, where `chain`
        // names the run of statements it's in (the body's, or a statement's exec output), `key` that output,
        // `index` its place in the run and `parent` the statement the run is in (0 for the body's)
        statementTree(g) {
            const s = this.schema;
            const info = new Map();
            const walk = (start, chain, key, parent, depth) => {
                let index = 0;
                for (let id = start; id && !info.has(id) && depth < 256;) {
                    const n = Doc.nodeIn(g, id);
                    if (!n || !s.isExec(n.k)) break;
                    info.set(id, { id, chain, key, index: index++, parent });
                    for (const p of s.ports(n)) {
                        if (p.input || !p.exec || G.samePath(p.path, ['next'])) continue;
                        const w = s.wire(n, p.path);
                        if (w) walk(w, `${id}:${p.path.join('.')}`, p.path[p.path.length - 1], id, depth + 1);
                    }
                    id = n.next || 0;
                }
            };
            if (g.entry) walk(g.entry, 'body', null, 0, 0);
            return info;
        }

        // The statements that read a node, through any number of values: [id]
        statementsReading(g, id) {
            const s = this.schema;
            const readers = new Map();
            for (const n of Doc.nodes(g)) for (const w of s.dataWires(n)) (readers.get(w) || readers.set(w, []).get(w)).push(n.id);
            const out = new Set(), seen = new Set(), stack = [...(readers.get(id) || [])];
            while (stack.length) {
                const at = stack.pop();
                if (seen.has(at)) continue;
                seen.add(at);
                const n = Doc.nodeIn(g, at);
                if (n && s.isExec(n.k)) out.add(at);
                else stack.push(...(readers.get(at) || []));
            }
            return [...out];
        }

        // Keeps a worked-out value as a local (Blueprint's Promote to Variable): a Local named `name` declared just
        // before the statement that first reads it, in the innermost run of statements that has all its reads, the
        // node its value, and one Get of the local wired where the node's wires went. Returns { local, get, before }
        // (`before` the statement it's declared before), or { why } when it can't be.
        keepAsLocal(g, id, name) {
            const s = this.schema;
            const n = Doc.nodeIn(g, id);
            if (!n || s.isExec(n.k) || !s.hasValue(n.k)) return { why: s.word('why.keep.workedOut') };
            const tree = this.statementTree(g);
            const lineOf = st => {
                const out = [];
                for (let at = tree.get(st), depth = 0; at && depth < 256; at = at.parent ? tree.get(at.parent) : null, depth++) out.unshift(at);
                return out;
            };
            const lines = this.statementsReading(g, id).filter(st => tree.has(st)).map(lineOf);
            if (!lines.length) return { why: s.word('why.keep.unread') };
            for (const n2 of Doc.nodes(g)) {
                if ((n2.k === 'eval' || n2.k === 'discard') && n2.value === id) return { why: s.word('why.keep.statement') };
            }
            // The innermost run of statements every read is in
            let depth = 0;
            while (lines.every(l => l.length > depth + 1 && l[depth + 1].chain === lines[0][depth + 1].chain)) depth++;
            // A For's start and step are one statement each, and a call stacked on another is that one's block alone:
            // the Local goes before what holds them
            const firstOf = chain => [...tree.values()].find(t => t.chain === chain && t.index === 0);
            while (depth > 0 && (['init', 'step'].includes(lines[0][depth].key) || Doc.nodeIn(g, firstOf(lines[0][depth].chain)?.id)?.stacked)) depth--;
            let before = null;
            for (const l of lines) if (!before || l[depth].index < before.index) before = l[depth];
            // What it reads of the code's locals is there before that statement
            const line = lineOf(before.id);
            const visible = d => {
                const where = s.isExec(d.k) ? [d.id] : this.statementsReading(g, d.id);
                return where.some(st => {
                    let at = tree.get(st);
                    if (at?.key === 'init' && at.parent) at = tree.get(at.parent); // A For's own counter
                    if (!at) return false;
                    return line.some((l, i) => l.chain === at.chain && (at.index < l.index || (at.id === l.id && i < line.length - 1)));
                });
            };
            const seen = new Set(), stack = [id];
            while (stack.length) {
                const at = stack.pop();
                if (seen.has(at)) continue;
                seen.add(at);
                const v = Doc.nodeIn(g, at);
                if (!v) continue;
                if (v.k === 'local') {
                    const d = Doc.nodeIn(g, v.ref);
                    if (d && !visible(d)) return { why: s.word('why.keep.later', { name: s.declaredLocal(d) || s.word('why.keep.aLocal') }) };
                }
                stack.push(...s.dataWires(v));
            }
            // The Local, run just before that statement, with the node as its value
            const from = this.execSourceOf(g, before.id);
            const local = this.newNode('var', Number(n.x) || 0, Number(n.y) || 0, { name });
            Doc.nodes(g).push(local);
            if (from) s.setWire(Doc.nodeIn(g, from.node), from.path, local.id);
            else if (g.entry === before.id) g.entry = local.id;
            s.setWire(local, ['next'], before.id);
            // One Get of it, where the node's wires went
            const get = this.newNode('local', Number(n.x) || 0, Number(n.y) || 0, { ref: local.id });
            Doc.nodes(g).push(get);
            for (const r of Doc.nodes(g)) {
                if (r === local || r === get) continue;
                for (const p of s.ports(r)) {
                    if (p.input && !p.exec && p.path && s.wire(r, p.path) === id) s.setWire(r, p.path, get.id);
                }
            }
            if (g.root === id) g.root = get.id;
            s.setWire(local, ['value'], id);
            this.reindex();
            return { local, get, before: before.id };
        }

        // Renames what a frame's code takes, and every read of it in the frame's nodes
        renamePin(g, old, name) {
            if (!g || !old || old === name) return;
            for (const n of Doc.nodes(g)) {
                if (n.k === 'name' && !n.local && n.name === old) n.name = name;
            }
        }

        // A declaration renamed: what this file says of it by its name follows (a type of a field or a parameter, a
        // call of a function, a read of a constant, a literal of a struct). Other files are the check's to say.
        renameUses(old, name) {
            if (!old || !name || old === name) return;
            if (!/^[A-Za-z_][A-Za-z0-9_]*$/.test(old)) return;
            // The name as a word of a type's text: Wobble, List<Wobble>, Game.Wobble
            const word = new RegExp('(^|[^A-Za-z0-9_])' + old + '(?![A-Za-z0-9_])', 'g');
            const swap = text => text.replace(word, (_, before) => before + name);
            const TYPES = ['type', 'returnType', 'fails', 'backing'];
            const visit = v => {
                if (Array.isArray(v)) { v.forEach(visit); return; }
                if (!isObject(v)) return;
                for (const key of TYPES) if (typeof v[key] === 'string') v[key] = swap(v[key]);
                if (Array.isArray(v.typeArgs)) v.typeArgs = v.typeArgs.map(t => (typeof t === 'string' ? swap(t) : t));
                if (v.k === 'call' && v.name === old) v.name = name;
                if (v.k === 'name' && !v.local && v.name === old) v.name = name;
                for (const child of Object.values(v)) if (child && typeof child === 'object') visit(child);
            };
            visit(this.root.decls);
        }

        // Whether a node is of what's shown in an input, by what it is (whoever reads it): a literal, a capitalised
        // name, a member of one of those, a minus over a number, a vector of numbers
        foldable(g, n, depth = 0) {
            const s = this.schema;
            const kind = n && depth < 64 ? s.inlineKind(n.k) : 0;
            const wires = kind ? s.dataWires(n) : [];
            const of = wires.length === 1 ? Doc.nodeIn(g, wires[0]) : null;
            if (kind === 1) return !wires.length;
            if (kind === 5) return !wires.length && capital(n.name);
            if (kind === 2) return !wires.length || (!!of && s.inlineKind(of.k) !== 3 && this.foldable(g, of, depth + 1));
            if (kind === 3) return n.op === '-' && !!of && (of.k === 'int' || of.k === 'float');
            if (kind === 4) return s.numbersOf.has(n.name) && wires.length > 0 && wires.every(w => { const a = Doc.nodeIn(g, w); return !!a && (a.k === 'int' || a.k === 'float' || (a.k === 'unary' && this.foldable(g, a, depth + 1))); });
            return false;
        }

        // Whether a node is a Get: a variable read (a name that isn't capitalised, a local, self), or a member of one
        // or of anything wired in
        isGet(n) {
            if (!n) return false;
            if (n.k === 'local' || n.k === 'this') return true;
            if (n.k === 'name') return !capital(n.name);
            return n.k === 'member';
        }

        // Removes a simple value nothing reads any more (after its input took another), with what it was made of:
        // a path's members down to its name, a minus's number. A value that was worked out stays, to be wired again,
        // and so does a Get, unless `gets` (a value picked in its place: the Gets it was made of go too).
        dropValue(g, id, { gets = false } = {}) {
            const s = this.schema;
            const gone = [];
            const read = new Set();
            for (const n of Doc.nodes(g)) for (const w of s.dataWires(n)) read.add(w);
            if (g.root) read.add(g.root);
            const drop = at => {
                const n = Doc.nodeIn(g, at);
                if (!n || read.has(at) || gone.includes(at) || !(this.foldable(g, n) || (gets && this.isGet(n)))) return;
                gone.push(at);
                // What it alone read goes with it
                const wires = s.dataWires(n);
                const others = new Set();
                for (const o of Doc.nodes(g)) if (o !== n && !gone.includes(o.id)) for (const w of s.dataWires(o)) others.add(w);
                for (const w of wires) if (!others.has(w) && g.root !== w) { read.delete(w); drop(w); }
            };
            drop(id);
            if (gone.length) this.deleteNodes(g, gone);
            return gone;
        }

        // Puts a value into an input (a path of an input of a node, or null for a value graph's result): `make(doc, g)`
        // makes the value's nodes and returns the id of the one whose value it is, or 0 for no value. What was there
        // goes if it was simple; a Get that nothing else reads goes when another value takes its place, and stays
        // when the input is emptied. Returns why it can't be, or null.
        setInput(g, node, path, make) {
            const s = this.schema;
            const n = path ? Doc.nodeIn(g, node) : null;
            const old = path ? (n ? s.wire(n, path) : 0) : g.root;
            const id = make ? make(this, g) : 0;
            this.reindex();
            let why = null;
            if (!path) why = this.setRoot(g, id || null);
            else if (id) why = this.connect(g, node, path, id);
            else this.disconnect(g, node, path);
            this.reindex();
            if (why && id) this.dropValue(g, id, { gets: true });
            else if (old && old !== id) this.dropValue(g, old, { gets: !!id });
            return why;
        }

        // A path's nodes made in a graph: what it starts at ({ name } or { local: the declaring node's id } or
        // { word: 'this' }), then its members. Returns the id of its last node.
        makePath(g, start, members, at) {
            const x = Math.round(at?.x ?? 0), y = Math.round(at?.y ?? 0);
            let last;
            if (start.local !== undefined) last = this.newNode('local', x, y, { ref: start.local });
            else if (start.word) last = this.newNode(start.word, x, y);
            else if (start.shorthand) last = this.newNode('member', x, y, { shorthand: true, member: start.shorthand });
            else last = this.newNode('name', x, y, { name: start.name });
            Doc.nodes(g).push(last);
            for (const m of members || []) {
                const node = this.newNode('member', x, y, { member: m });
                this.schema.setWire(node, ['object'], last.id);
                Doc.nodes(g).push(node);
                last = node;
            }
            return last.id;
        }

        // A literal's nodes made in a graph: a number (a minus over it when it's negative), text, yes or no
        makeLiteral(g, kind, text, at) {
            const x = Math.round(at?.x ?? 0), y = Math.round(at?.y ?? 0);
            const order = this.schema.nodeOrder(kind);
            if (kind === 'int' || kind === 'float') {
                const negative = String(text).startsWith('-');
                const n = this.newNode(kind, x, y, { text: negative ? String(text).slice(1) : String(text) });
                Doc.nodes(g).push(n);
                if (!negative) return n.id;
                const minus = this.newNode('unary', x, y, { op: '-' });
                this.schema.setWire(minus, ['value'], n.id);
                Doc.nodes(g).push(minus);
                return minus.id;
            }
            const n = this.newNode(kind, x, y);
            if (kind === 'bool') set(n, 'value', text === true || text === 'true', order);
            else if (text !== undefined) set(n, 'text', String(text), order);
            Doc.nodes(g).push(n);
            return n.id;
        }

        // A declaration of a type by its name, wherever the file has it: { value, list }
        typeDecl(name) {
            if (!name) return undefined;
            const last = String(name).split('.').pop();
            for (const e of this.index.values()) {
                if (e.role === 'decl' && e.value.name === last && (Array.isArray(e.value.members) || e.value.kind === 'alias')) return e;
            }
            return undefined;
        }

        // An enum the file declares: its members' names, or null
        enumMembers(type) {
            const e = this.typeDecl(type);
            return e?.value.kind === 'enum' ? e.value.members.map(m => m.name).filter(Boolean) : null;
        }

        // What a type the file declares has, as the search offers a value's members before tidels answers:
        // [{ name, type, method: its parameters' names, or undefined }]
        membersOf(type) {
            const e = this.typeDecl(type);
            if (!e || e.value.kind === 'enum' || !Array.isArray(e.value.members)) return [];
            const out = [];
            for (const m of e.value.members) {
                if (!isObject(m) || !m.name) continue;
                if (m.kind === 'field' || m.kind === 'property' || m.kind === 'tweak') out.push({ name: m.name, type: m.type });
                else if (m.kind === 'method') out.push({ name: m.name, type: m.returnType, method: (m.params || []).map(p => p.name || 'arg') });
            }
            return out;
        }

        // Every declaration's name with its kind, for the pickers: [{ name, kind, id }]
        declared(kinds) {
            const out = [];
            for (const e of this.index.values()) {
                if (e.role === 'decl' && e.value.name && (!kinds || kinds.includes(e.value.kind))) out.push({ name: e.value.name, kind: e.value.kind, id: e.id });
            }
            return out;
        }

        // A value graph of a call of literals: float3(1, 2, 3), Color(1, 0.5, 0)
        callGraph(name, texts) {
            const args = texts.map((t, i) => this.newNode(/^-?\d+$/.test(t) ? 'int' : 'float', -400, 88 * i, { text: t }));
            const call = this.newNode('call', -180, 0, { name, args: args.map(a => a.id) });
            return { root: call.id, nodes: [call, ...args] };
        }

        // A value graph of an enum's member: Stand.Left
        memberGraph(type, member) {
            const object = this.newNode('name', -400, 0, { name: type });
            const m = this.newNode('member', -180, 0, { member });
            this.schema.setWire(m, ['object'], object.id);
            return { root: m.id, nodes: [m, object] };
        }

        // The title of a board item: a declaration's name, a member's with its type's, `Type.Name get`
        titleOf(item) {
            const v = item.value;
            if (item.accessor) return `${item.type?.name ?? ''}.${item.property?.name ?? ''} ${v.word ?? ''}`;
            const own = v.kind === 'operator' ? `operator ${v.op ?? ''}` : v.name ?? (item.spec?.title || v.kind);
            if (item.member) return `${item.type?.name ?? ''}.${own}`;
            if (v.kind === 'text') return firstLine(v.text) || item.spec?.title || 'text';
            return own;
        }

        // A readable name of an element, for the lists of problems and of contents
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

    // Whether a name is written with a capital first, as Tide names a type, a namespace, an enum or a constant
    function capital(name) {
        return /^[A-Z]/.test(String(name ?? ''));
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
