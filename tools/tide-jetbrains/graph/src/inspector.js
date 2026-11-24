// The inspector: what's selected, every key of it in its kind's order, as fields of what the key holds (names and
// types as text, flags as checkboxes, modifiers by the kind's, lists of elements to add to and open, graphs to show).
(function (G) {
    'use strict';
    const { el, field, checkbox, select, button, menu } = G.ui;
    const { set } = G.json;

    // What a list of elements makes as a new one, and is called
    const NEW = {
        param: (doc, list) => doc.newParam('int', doc.uniqueName(list, 'value')),
        sampleParam: (doc, list) => ({ id: doc.newId(), type: 'float', name: doc.uniqueName(list, 'value') }),
        payload: (doc, list) => ({ id: doc.newId(), type: 'int', name: doc.uniqueName(list, 'value') }),
        attribute: doc => doc.newAttribute('Attribute'),
        accessor: (doc, list) => doc.newAccessor(list.some(a => a.word === 'get') ? 'set' : 'get'),
        entry: (doc, list) => doc.newNamed(doc.uniqueName(list, 'name'), 'int', '0'),
        enumMember: (doc, list) => ({ id: doc.newId(), name: doc.uniqueName(list, 'Member') }),
        where: () => ({ param: 'T', rules: [] }),
        prefabNode: (doc, list) => doc.newPrefabNode(doc.uniqueName(list, 'child')),
        lambdaParam: (doc, list) => ({ type: 'float', name: doc.uniqueName(list, 'p') }),
    };

    class Inspector {
        constructor(app) {
            this.app = app;
            this.el = el('div.tg-inspector');
        }

        get editor() { return this.app.editor; }
        get schema() { return this.app.editor.schema; }

        render() {
            const ed = this.editor;
            this.el.replaceChildren();
            if (ed.error) return;
            const e = ed.doc.find(ed.selected);
            if (!e) {
                this.renderFile();
                return;
            }
            const doc = ed.doc;
            const v = e.value;
            const parent = this.parentOf(e);
            this.el.append(el('div.tg-inspector-head', null,
                parent ? el('a.tg-link', { href: '#', text: '‹ ' + doc.describe(parent), onclick: ev => { ev.preventDefault(); ed.select(parent.id, { open: false }); } }) : null,
                el('div.tg-inspector-title', null, el('span.tg-kind', { text: e.role === 'node' ? v.k : v.kind ?? roleName(e.role) }), ' ', el('span', { text: doc.describe(e) })),
                el('div.tg-inspector-id', { text: `id ${v.id}` })));
            const order = doc.orderOf(e) || Object.keys(v);
            const keys = [...order, ...Object.keys(v).filter(k => !order.includes(k))];
            for (const key of keys) {
                const row = this.fieldOf(e, key, order);
                if (row) this.el.append(row);
            }
            this.el.append(this.actions(e));
            for (const d of e.role === 'node' ? this.app.lang.diagnosticsOf(v.id) : []) {
                this.el.append(el('div.tg-diagnostic' + (d.severity === 1 ? '.tg-error' : '.tg-warning'), { text: d.message }));
            }
        }

        // The file's own: its namespace and usings
        renderFile() {
            const doc = this.editor.doc;
            const root = doc.root;
            const order = this.schema.root;
            this.el.append(el('div.tg-inspector-head', null, el('div.tg-inspector-title', { text: 'This file' })),
                this.row('namespace', field(root.namespace, v => this.app.edit(() => set(root, 'namespace', v || undefined, order)), { placeholder: 'Game.Combat' })),
                this.row('using', this.strings(root.using, v => this.app.edit(() => set(root, 'using', v.length ? v : undefined, order)), 'one namespace a line')),
                el('div.tg-hint', { text: 'Select a declaration, a member or a node to see what it has.' }));
        }

        parentOf(e) {
            const doc = this.editor.doc;
            if (e.role === 'node' || e.role === 'note') return doc.find(e.slot?.owner);
            if (e.owner && typeof e.owner.id === 'number') return doc.find(e.owner.id);
            return null;
        }

        row(label, content, title) {
            return el('div.tg-row', { title }, el('label.tg-label', { text: label }), el('div.tg-value', null, content));
        }

        strings(list, commit, placeholder) {
            return field((list || []).join('\n'), v => commit(v.split('\n').map(x => x.trim()).filter(x => x !== '')), { multiline: true, rows: Math.max(2, (list || []).length + 1), placeholder });
        }

        // The field of one key, or null for keys that aren't edited here (wires, places)
        fieldOf(e, key, order) {
            const s = this.schema;
            const doc = this.editor.doc;
            const v = e.value;
            const type = s.keyType(key, e.role === 'node' ? v : { kind: v.kind });
            const edit = change => this.app.edit(change);
            const put = value => edit(() => set(v, key, value, order));
            const node = e.role === 'node';
            if (['id', 'k', 'kind', 'x', 'y', 'next'].includes(key)) return null;
            if (node) {
                const spec = s.node(v.k);
                const list = spec?.lists?.find(l => l.key === key);
                if (list) return this.inputsList(v, list);
                const items = spec?.items?.find(i => i.key === key);
                if (items) return this.itemsList(v, items);
                if (spec?.map === key) return this.mapInputs(v, key);
                if (type === 'wire') return null;
                if (key === 'ref' && v.k === 'local') {
                    const locals = doc.locals(this.editor.graph);
                    return this.row('reads', select(String(v.ref ?? ''), locals.map(d => [String(d.id), s.declaredLocal(d)]), x => put(Number(x))));
                }
            }
            switch (key === 'op' ? 'op' : key === 'mode' ? 'mode' : key === 'word' ? 'word' : key === 'modifiers' ? 'modifiers' : type) {
                case 'comment':
                    return this.row('comment', field(v.comment, x => put(x || undefined), { multiline: true, rows: 2, placeholder: 'Written as // lines before it' }));
                case 'text':
                    return this.row('text', field(v.text, x => put(x), { multiline: true, rows: 6, className: 'tg-input.tg-mono' }), 'Tide text, as written');
                case 'flag': {
                    const bool = node && v.k === 'bool' && key === 'value';
                    return this.row(key, checkbox(v[key], x => put(bool ? x : x ? true : undefined)));
                }
                case 'op': {
                    const ops = s.ops(v) || [];
                    return this.row('operator', select(v.op ?? '', ops.map(o => [o, `${o}  ${s.opTitle(o)}`]), put));
                }
                case 'mode':
                    return this.row('mode', select(v.mode ?? 'read', s.paramModes, put));
                case 'word':
                    return this.row('word', select(v.word ?? 'get', ['get', 'set'], put));
                case 'modifiers': {
                    const member = e.role === 'member';
                    const allowed = s.modifiers(v.kind, member);
                    const have = new Set(v.modifiers || []);
                    const words = [...allowed, ...[...have].filter(m => !allowed.includes(m))];
                    if (!words.length) return null;
                    return this.row('words', el('div.tg-checks', null, words.map(m => checkbox(have.has(m), on => edit(() => {
                        const next = new Set(v.modifiers || []);
                        if (on) next.add(m); else next.delete(m);
                        const ordered = s.allModifiers.filter(x => next.has(x));
                        set(v, 'modifiers', ordered.length ? ordered : undefined, order);
                    }), m))));
                }
                case 'strings':
                    return this.row(key, this.strings(v[key], x => put(x.length || key === 'parts' ? x : undefined), 'one a line'));
                case 'type':
                    return this.row(key, field(v[key], x => put(x || undefined), { list: 'tg-types', placeholder: key === 'fails' ? 'an error type' : 'a type' }));
                case 'graph:value':
                case 'graph:body':
                    return this.graphRow(e, key, type === 'graph:body');
                case 'graphs':
                    return this.componentsRow(e);
                case 'members':
                    return this.membersRow(e);
                case 'decls':
                    return this.declsRow(e);
                case 'trigger': {
                    const t = v.trigger || {};
                    const torder = s.element('trigger');
                    const tput = (k, x) => edit(() => { if (!v.trigger) v.trigger = {}; set(v.trigger, k, x, torder); });
                    return this.row('event', el('div.tg-sub', null,
                        field(t.type, x => tput('type', x), { placeholder: 'Event type, or a field for Changed', list: 'tg-types' }),
                        field(t.name, x => tput('name', x), { placeholder: 'name' }),
                        checkbox(t.verified, x => tput('verified', x || undefined), 'verified'),
                        checkbox(t.changed, x => tput('changed', x || undefined), 'Changed<>')));
                }
                case 'prefabRoot':
                    return v.root ? this.row('root', button(doc.describe(doc.find(v.root.id)), () => this.editor.select(v.root.id, { open: false }))) : null;
                case 'inputs':
                    return this.mapInputs(v, key);
                case 'wire':
                    return null;
                case 'fixed':
                    return null;
                default:
                    if (type.startsWith('list:')) return this.elementList(e, key, type.slice(5), order);
                    if (typeof v[key] === 'object' && v[key] !== null) {
                        return this.row(key, el('pre.tg-json', { text: G.json.write(v[key]) }), 'Kept as it is: the editor does not know this key');
                    }
                    return this.row(key, field(v[key] === undefined ? '' : String(v[key]), x => put(x === '' && key !== 'name' ? undefined : x), { placeholder: key }));
            }
        }

        // A graph of an element: shown on the canvas, added, removed
        graphRow(e, key, body) {
            const ed = this.editor;
            const v = e.value;
            const has = v[key] && typeof v[key] === 'object';
            const slot = { owner: v.id, key };
            const shown = has && G.slotKey(slot) === G.slotKey(ed.slot);
            const label = key === 'body' ? 'code' : key;
            return this.row(label, el('div.tg-sub.tg-inline', null,
                has ? button(shown ? 'Shown' : 'Show', () => ed.show(slot), 'Show it on the canvas') : null,
                !has ? button('Add', () => { this.app.edit(doc => doc.ensureGraph(slot)); ed.show(slot); }, body ? 'Give it code' : 'Give it a value') : null,
                has && !body ? button('Remove', () => this.app.edit(() => delete v[key]), 'Take the value away') : null));
        }

        componentsRow(e) {
            const ed = this.editor;
            const v = e.value;
            const list = v.components || [];
            return this.row('components', el('div.tg-list', null,
                list.map((c, i) => el('div.tg-list-row', null,
                    el('span.tg-list-name', { text: this.rootTitle(c) || `component ${i + 1}` }),
                    button('Show', () => ed.show({ owner: v.id, key: 'components', index: i })),
                    button('×', () => this.app.edit(() => v.components.splice(i, 1)), 'Remove', 'tg-icon'))),
                button('+ component', () => {
                    const i = list.length;
                    this.app.edit(doc => {
                        if (!Array.isArray(v.components)) v.components = [];
                        v.components.push(doc.valueGraph('literal'));
                        const n = v.components[i].nodes[0];
                        n.type = 'Component';
                    });
                    ed.show({ owner: v.id, key: 'components', index: i });
                })));
        }

        rootTitle(g) {
            const n = G.Doc.nodeIn(g, g?.root);
            return n ? (n.type || n.name || this.schema.title(n.k)) : '';
        }

        // A type's members, a kind of member to add from the schema's
        membersRow(e) {
            const v = e.value;
            const ed = this.editor;
            const doc = ed.doc;
            if (v.members === null) {
                return this.row('members', button('Give it members', () => this.app.edit(() => { v.members = []; })), 'A scene without members is written `scene Name;`');
            }
            const list = v.members || [];
            return this.row('members', el('div.tg-list', null,
                list.map(m => el('div.tg-list-row', null,
                    el('a.tg-link', { href: '#', text: doc.describe(doc.find(m.id)), onclick: ev => { ev.preventDefault(); ed.select(m.id); } }))),
                button('+ member', ev => menu(ev.clientX, ev.clientY, this.schema.members.map(spec => ({
                    label: spec.title,
                    run: () => this.app.addMember(e, spec.kind),
                }))))));
        }

        declsRow(e) {
            const doc = this.editor.doc;
            return this.row('declarations', el('div.tg-list', null,
                (e.value.decls || []).map(d => el('div.tg-list-row', null,
                    el('a.tg-link', { href: '#', text: doc.describe(doc.find(d.id)), onclick: ev => { ev.preventDefault(); this.editor.select(d.id); } }))),
                button('+ declaration', ev => this.app.declMenu(ev.clientX, ev.clientY, e.value))));
        }

        // A list of elements of one kind: params, attributes, accessors, entries...
        elementList(e, key, element, order) {
            const doc = this.editor.doc;
            const v = e.value;
            const role = element === 'param' && v.kind === 'sample' ? 'sampleParam' : element;
            const list = Array.isArray(v[key]) ? v[key] : [];
            const make = NEW[role] || NEW[element];
            const rows = list.map((item, i) => {
                const controls = [
                    i > 0 ? button('↑', () => this.app.edit(() => { list.splice(i - 1, 0, list.splice(i, 1)[0]); }), 'Move up', 'tg-icon') : null,
                    button('×', () => this.app.edit(() => { list.splice(i, 1); if (!list.length && key !== 'params') delete v[key]; }), 'Remove', 'tg-icon'),
                ];
                if (element === 'lambdaParam' || element === 'where') return el('div.tg-list-row', null, this.inlineElement(item, element), ...controls);
                const label = doc.describe(doc.find(item.id)) || `${element} ${i + 1}`;
                return el('div.tg-list-row', null, el('a.tg-link', { href: '#', text: label, onclick: ev => { ev.preventDefault(); this.editor.select(item.id, { open: false }); } }), ...controls);
            });
            return this.row(key, el('div.tg-list', null, rows, make ? button('+ ' + (element === 'lambdaParam' ? 'parameter' : roleName(role)), () => {
                let made;
                this.app.edit(d => {
                    if (!Array.isArray(v[key])) set(v, key, [], order);
                    made = make(d, v[key]);
                    v[key].push(made);
                });
                if (made?.id && element !== 'where') this.editor.select(made.id, { open: false });
            }) : null));
        }

        inlineElement(item, element) {
            const edit = change => this.app.edit(change);
            if (element === 'where') {
                return el('div.tg-sub.tg-inline', null,
                    field(item.param, x => edit(() => { item.param = x; }), { placeholder: 'T' }),
                    field((item.rules || []).join(', '), x => edit(() => { item.rules = x.split(',').map(r => r.trim()).filter(Boolean); }), { placeholder: 'comparable, hashable' }));
            }
            const order = this.schema.element('lambdaParam');
            return el('div.tg-sub.tg-inline', null,
                checkbox(item.mut, x => edit(() => set(item, 'mut', x || undefined, order)), 'mut'),
                field(item.type, x => edit(() => set(item, 'type', x || undefined, order)), { placeholder: 'type', list: 'tg-types' }),
                field(item.name, x => edit(() => set(item, 'name', x, order)), { placeholder: 'name' }));
        }

        // A node's list of inputs (a call's arguments, a list's items): how many, and their names where it has them
        inputsList(n, list) {
            const a = Array.isArray(n[list.key]) ? n[list.key] : [];
            const order = this.schema.nodeOrder(n.k);
            const names = list.names ? n[list.names] : null;
            const rows = a.map((_, i) => el('div.tg-list-row', null,
                list.names ? field(names?.[i] ?? '', x => this.app.edit(() => {
                    const next = a.map((__, j) => (j === i ? x : names?.[j] ?? ''));
                    set(n, list.names, next.some(s => s) ? next : undefined, order);
                }), { placeholder: `${list.label} ${i + 1} (unnamed)` }) : el('span.tg-list-name', { text: `${list.label} ${i + 1}` }),
                button('×', () => this.app.edit(() => {
                    a.splice(i, 1);
                    if (Array.isArray(names)) {
                        names.splice(i, 1);
                        if (!names.some(s => s)) delete n[list.names];
                    }
                }), 'Remove', 'tg-icon')));
            return this.row(list.key, el('div.tg-list', null, rows, button('+ ' + list.label, () => this.app.edit(() => {
                if (!Array.isArray(n[list.key])) set(n, list.key, [], order);
                n[list.key].push(null);
                if (Array.isArray(n[list.names])) n[list.names].push('');
            }))));
        }

        // A node's items (a switch's cases, a literal's fields, a switch value's arms)
        itemsList(n, item) {
            const s = this.schema;
            const a = Array.isArray(n[item.key]) ? n[item.key] : [];
            const order = s.itemOrder(n.k, item.key);
            const named = a.some(o => o && typeof o[item.label] === 'string') || n.k === 'literal';
            const rows = a.map((o, i) => {
                const parts = [];
                if (named) parts.push(field(o[item.label], x => this.app.edit(() => set(o, item.label, x, order)), { placeholder: item.label }));
                else parts.push(el('span.tg-list-name', { text: `${item.label} ${i + 1}` }));
                for (const w of item.wires || []) {
                    if (!w.endsWith('[]')) continue;
                    const key = w.slice(0, -2);
                    const labels = Array.isArray(o[key]) ? o[key] : [];
                    parts.push(el('span.tg-count', { text: `${labels.length} ${key}` }),
                        button('+', () => this.app.edit(() => { if (!Array.isArray(o[key])) o[key] = []; o[key].push(null); }), `Another ${key.replace(/s$/, '')} (null for default)`, 'tg-icon'),
                        labels.length > 1 ? button('−', () => this.app.edit(() => o[key].pop()), `One ${key.replace(/s$/, '')} less`, 'tg-icon') : null);
                }
                parts.push(button('×', () => this.app.edit(() => a.splice(i, 1)), 'Remove', 'tg-icon'));
                return el('div.tg-list-row', null, parts);
            });
            return this.row(item.key, el('div.tg-list', null, rows, button('+ ' + item.label, () => this.app.edit(d => {
                if (!Array.isArray(n[item.key])) set(n, item.key, [], s.nodeOrder(n.k));
                const blank = {};
                if (n.k === 'literal') set(blank, item.label, d.uniqueName(n[item.key], 'field'), order);
                for (const w of item.wires || []) {
                    if (w.endsWith('[]')) set(blank, w.slice(0, -2), [null], order);
                    else set(blank, w, null, order);
                }
                n[item.key].push(blank);
            }))));
        }

        // A text or code node's named inputs
        mapInputs(n, key) {
            const map = n[key] && typeof n[key] === 'object' ? n[key] : {};
            const rows = Object.keys(map).map(name => el('div.tg-list-row', null,
                field(name, x => this.app.edit(() => {
                    if (!x || x === name || x in map) return;
                    const entries = Object.entries(map).map(([k, v]) => [k === name ? x : k, v]);
                    for (const k of Object.keys(map)) delete map[k];
                    for (const [k, v] of entries) map[k] = v;
                }), { placeholder: 'name' }),
                button('×', () => this.app.edit(() => { delete map[name]; }), 'Remove', 'tg-icon')));
            return this.row('inputs', el('div.tg-list', null, rows, button('+ input', () => this.app.edit(d => {
                if (!n[key] || typeof n[key] !== 'object') set(n, key, {}, this.schema.nodeOrder(n.k));
                const names = Object.keys(n[key]);
                let name = 'a';
                for (let c = 97; names.includes(name); c++) name = String.fromCharCode(c);
                n[key][name] = null;
            }))), 'Each name in the text stands for the value wired to it');
        }

        actions(e) {
            const app = this.app;
            const v = e.value;
            const out = el('div.tg-actions');
            const list = e.list;
            if (e.role === 'node' || e.role === 'note') {
                out.append(button('Delete', () => app.command('delete')), button('Duplicate', () => app.command('duplicate')));
                if (v.k === 'member' || v.k === 'method') out.append(button('Pick member…', ev => app.pickMember(v, ev.clientX, ev.clientY)));
                return out;
            }
            if (Array.isArray(list)) {
                const i = list.indexOf(v);
                out.append(
                    button('Move up', () => app.edit(() => { list.splice(i - 1, 0, list.splice(i, 1)[0]); }), 'Earlier in the file', i > 0 ? 'tg-button' : 'tg-button.tg-hidden'),
                    button('Move down', () => app.edit(() => { list.splice(i + 1, 0, list.splice(i, 1)[0]); }), 'Later in the file', i < list.length - 1 ? 'tg-button' : 'tg-button.tg-hidden'),
                    ['decl', 'member'].includes(e.role) ? button('Duplicate', () => {
                        let copy;
                        app.edit(doc => { copy = doc.duplicate(v); if (copy.name) copy.name = doc.uniqueName(list, copy.name); list.splice(i + 1, 0, copy); });
                        if (copy) this.editor.select(copy.id);
                    }) : null,
                    button('Delete', () => {
                        const parent = this.parentOf(e);
                        app.edit(() => list.splice(list.indexOf(v), 1));
                        this.editor.select(parent?.id ?? null, { open: false });
                    }));
            }
            return out;
        }

        // Puts the cursor in a key's field
        focus(key) {
            const label = [...this.el.querySelectorAll('.tg-label')].find(l => l.textContent === key);
            label?.parentElement.querySelector('input, textarea, select')?.focus();
        }
    }

    function roleName(role) {
        return { decl: 'declaration', member: 'member', param: 'parameter', sampleParam: 'parameter', payload: 'value', attribute: 'attribute',
            accessor: 'accessor', entry: 'entry', enumMember: 'member', prefabNode: 'entity', where: 'rule', note: 'note' }[role] || role;
    }

    G.Inspector = Inspector;
})(globalThis.TideGraph = globalThis.TideGraph || {});
