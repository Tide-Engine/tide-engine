// The one menu of everything on the board (notes/visual-scripting.md, Round three: "One menu, the same everywhere"):
// a right click on a card, a frame, a node, a row or a pin, and its "…" button, open the menu this file builds, in
// the same order for every kind of thing, leaving out what doesn't apply:
//
//   1. what can be added to it (Add a field, Add a node… Space; on a pin: Pick a value…, Work it out…)
//   2. Rename F2
//   3. Cut, Copy, Paste, Duplicate, Delete with their keys (on a pin: Break links)
//   4. Note…
//
// Its options, attributes and a worked-out starting value aren't in it: they're in Details.
(function (G) {
    'use strict';
    const { menu } = G.ui;
    const { ENTRY, RESULT } = G.frames;

    // A menu's entry by its words (menu.<key>: its label, and its keys)
    const w = (key, vars) => G.w(key, vars);
    const M = (key, vars) => ({ label: w('menu.' + key, vars), hint: G.wk('menu.' + key) });

    const article = word => (/^[aeiou]/i.test(word) ? 'an' : 'a');

    // "Add a field", "Add an operator"; one of its own goes by its name ("Add Sample"), and text as it is
    function addMember(spec) {
        if (spec.once || spec.kind === 'text') return w('menu.addOwn', { title: spec.title });
        const word = spec.title.toLowerCase();
        return w('menu.addOne', { article: article(word), word });
    }

    // What a kind's "+" adds, in a menu's words: its addRow ("+ choice") as "Add a choice"; what code takes, as what
    // it is for that code
    function addRow(schema, kind, member) {
        const row = schema.addRow(kind, member);
        const word = row.replace(/^\+\s*/, '').replace(/…$/, '');
        if (!word || word === '+') return null;
        if (word === 'needs') return w('menu.addNeed');
        return w(row.endsWith('…') ? 'menu.addOneMore' : 'menu.addOne', { article: article(word), word });
    }

    // The thing a press was on, as the menu and Details take it: { kind: 'card' | 'frame' | 'region' | 'node' |
    // 'note' | 'row' | 'pin', ... }, or null for the empty board or an empty part of a frame
    function targetAt(app, target) {
        const b = app.board;
        const ed = app.editor;
        if (!target?.closest) return null;
        const frame = b.frameAt(target);
        if (frame && frame.mode === 'near') {
            const wire = target.closest('.tg-wire-hit');
            if (wire) return { kind: 'wire', frame, wire: JSON.parse(wire.dataset.wire) };
            const port = b.portAt(frame, target);
            if (port && (target.closest('.tg-dot') || !port.input || port.exec || frame.hasNodeWire(port) || port.pin !== undefined)) return { kind: 'pin', frame, port };
            const noteEl = target.closest('.tg-note');
            if (noteEl) {
                const note = frame.views.get(Number(noteEl.dataset.note))?.node;
                if (note) return { kind: 'note', frame, note };
            }
            const nodeEl = target.closest('.tg-node');
            const id = nodeEl ? Number(nodeEl.dataset.id) : 0;
            if (id > 0) {
                const node = frame.views.get(id)?.node;
                if (node) return { kind: 'node', frame, node };
            }
            if (!frame.isValue) {
                const row = target.closest('.tg-entry-row[data-id]');
                const e = row ? ed.doc.find(Number(row.dataset.id)) : null;
                if (e && ['param', 'payload', 'sampleParam'].includes(e.role)) return { kind: 'row', item: frame.item, row: e };
                if (id === ENTRY || target.closest('.tg-frame-head')) return { kind: 'frame', item: frame.item };
            }
            if (id === RESULT) return null;
            return null;
        }
        if (frame && !frame.isValue) return { kind: 'frame', item: frame.item };
        const cardEl = target.closest('.tg-card');
        if (cardEl) {
            const c = b.cards.get(Number(cardEl.dataset.id));
            if (!c) return null;
            const rowEl = target.closest('.tg-row[data-id]');
            const e = rowEl ? ed.doc.find(Number(rowEl.dataset.id)) : null;
            if (e && e.id !== c.item.id && ['member', 'enumMember', 'entry', 'prefabNode'].includes(e.role)) return { kind: 'row', item: c.item, row: e };
            return { kind: 'card', item: c.item };
        }
        const region = target.closest('.tg-region-head');
        if (region) {
            const r = b.regions.get(Number(region.parentElement?.dataset.id));
            if (r) return { kind: 'region', item: r.item };
        }
        return null;
    }

    // Makes the target what's selected, as a click on it would: what the menu's Cut, Copy and Delete act on, and
    // what Details shows
    function select(app, t) {
        const ed = app.editor;
        if (t.kind === 'card' || t.kind === 'frame') {
            if (!ed.picked.has(t.item.id) || ed.row) ed.pick([t.item.id]);
        } else if (t.kind === 'row') {
            if (ed.row !== t.row.id) ed.selectRow(t.item.id, t.row.id);
        } else if (t.kind === 'node' || t.kind === 'note') {
            const id = (t.node || t.note).id;
            if (G.slotKey(ed.slot) !== t.frame.key || !ed.selection.has(id)) ed.selectNodes(t.frame.slot, [id]);
        }
    }

    // Whether the thing has a name that Rename changes: its element and the field key of its name on the board
    function nameOf(app, t) {
        const s = app.editor.schema;
        if (t.kind === 'card' || t.kind === 'frame' || t.kind === 'region') {
            const keys = t.item.accessor ? [] : t.item.region ? ['name'] : t.item.spec?.keys || [];
            return keys.includes('name') && t.item.value.kind !== 'operator' ? `${t.item.id}:name` : null;
        }
        if (t.kind === 'row') {
            const v = t.row.value;
            if (t.row.role === 'param' && (s.modeIsFilter(v.mode) || v.ownership)) return null;
            if (t.row.role === 'member' && !s.memberSpec(v.kind)?.keys.includes('name')) return null;
            if (t.row.role === 'prefabNode' && !t.row.list) return null;
            return `${v.id}:name`;
        }
        if (t.kind === 'node') {
            const shown = t.frame.analysis.merged.has(t.node.id) ? t.frame.analysis.byId.get(t.frame.analysis.merged.get(t.node.id)) : t.node;
            return s.declaredLocal(shown) && typeof shown.name === 'string' ? `${shown.id}:name` : null;
        }
        return null;
    }

    // The order a thing's note is written in
    function noteOrder(app, t) {
        const s = app.editor.schema;
        if (t.kind === 'card' || t.kind === 'frame' || t.kind === 'region') {
            const keys = t.item.accessor ? s.element('accessor') : t.item.spec?.keys || [];
            return keys.includes('comment') ? keys : null;
        }
        if (t.kind === 'row') {
            const v = t.row.value;
            const keys = t.row.role === 'member' ? s.memberSpec(v.kind)?.keys || [] : s.element(t.row.role);
            return keys.includes('comment') ? keys : null;
        }
        if (t.kind === 'node') return s.nodeOrder(t.node.k);
        return null;
    }

    // Section 1: what can be added to it, or put in it
    function adds(app, t, x, y) {
        const ed = app.editor;
        const s = ed.schema;
        const out = [];
        const add = (label, run, extra = {}) => out.push({ label, run, section: 1, ...extra });
        if (t.kind === 'card') {
            const v = t.item.value;
            const spec = t.item.spec;
            if (spec?.memberKinds) {
                for (const m of s.memberKinds(v.kind)) {
                    const there = m.once && (v.members || []).some(x2 => x2.kind === m.kind);
                    add(addMember(m), () => app.addMember(t.item, m.kind), { name: 'add-' + m.kind, summary: m.summary, disabled: there, why: w('why.hasOne') });
                }
            } else if (spec?.keys.includes('members') || spec?.keys.includes('entries')) {
                const label = addRow(s, v.kind, false);
                if (label) add(label, () => G.cards.addRowTo(app, t.item, null), { name: 'add-row' });
            }
        } else if (t.kind === 'frame') {
            const f = app.board.frames.get(t.item.id);
            if (f?.graph) {
                add(M('addNode').label, () => {
                    const where = f.mode === 'near' ? f.body.getBoundingClientRect() : null;
                    const inside = where && x >= where.left && x <= where.right && y >= where.top && y <= where.bottom;
                    if (inside) G.pickers.nodeSearch(app, f, x, y, null);
                    else app.board.openAddIn(f);
                }, { hint: M('addNode').hint, name: 'add-node' });
                add(w('menu.addNote'), () => app.addNote(t.item.id), { name: 'add-note' });
            }
            const keys = t.item.accessor ? [] : t.item.spec?.keys || [];
            if (keys.includes('params')) {
                const label = addRow(s, t.item.value.kind, !!t.item.member);
                if (label) add(label, () => app.openNeeds(t.item, x, y), { name: 'add-need' });
            }
        } else if (t.kind === 'region') {
            add(w('menu.addTo'), () => G.pickers.addMenu(app, x, y, { namespace: t.item.value }), { name: 'add-decl' });
        } else if (t.kind === 'row') {
            const role = t.row.role;
            if (role === 'member' && t.row.value.kind === 'property') {
                // A property's code for when it's read and when it's set, where it hasn't that yet
                const m = t.row.value;
                for (const word of ['get', 'set'].filter(x2 => !(m.accessors || []).some(a => a.word === x2))) {
                    add(w('menu.addAccessor', { word }), () => {
                        let made;
                        app.edit(d => {
                            if (!Array.isArray(m.accessors)) G.json.set(m, 'accessors', [], s.memberOrder('property'));
                            made = d.newAccessor(word);
                            m.accessors.push(made);
                        });
                        if (made) app.reveal(made.id);
                    }, { name: 'add-' + word });
                }
            } else if (role === 'param' && t.row.value.mode === 'query') {
                // What the entities a query goes through have
                const p = t.row.value;
                add(w('menu.addQueryNeed'), () => G.pickers.typePicker(app, {
                    x, y, kinds: ['component'], details: ['component'], ask: () => app.lang.completionInParams(t.item.value), title: w('entry.query.title'),
                    onPick: type => app.edit(d => {
                        if (!Array.isArray(p.query)) G.json.set(p, 'query', [], s.element('param'));
                        p.query.push(d.newParam(type, G.pickers.nameFor(type)));
                    }),
                }), { name: 'add-query-need' });
            } else if (role === 'member' && ['field', 'tweak'].includes(t.row.value.kind)) {
                add(addMember(s.memberSpec('field')), () => G.cards.addRowTo(app, t.item, t.row.value), { name: 'add-row' });
            } else if (role === 'enumMember' || role === 'entry') {
                const label = addRow(s, t.item.value.kind, false);
                if (label) add(label, () => G.cards.addRowTo(app, t.item, t.row.value), { name: 'add-row' });
            } else if (role === 'param' || role === 'sampleParam') {
                const label = addRow(s, t.item.value.kind, !!t.item.member);
                if (label) add(label, () => app.openNeeds(t.item, x, y), { name: 'add-need' });
            }
        } else if (t.kind === 'node') {
            const shown = t.frame.analysis.merged.has(t.node.id) ? t.frame.analysis.byId.get(t.frame.analysis.merged.get(t.node.id)) : t.node;
            for (const a of t.frame.adders(shown)) add(a.label, a.run, { name: 'add-' + a.key });
        } else if (t.kind === 'pin') {
            const p = t.port;
            const f = t.frame;
            const row = f.ports.get(G.frames.portKey(p))?.row;
            if (p.input && !p.exec) {
                add(w('menu.pick'), () => f.pickValue(p, row), { name: 'pick' });
                add(w('menu.work'), () => f.pickValue(p, row, { work: true }), { name: 'work' });
                add(M('empty').label, () => f.emptyInput(p), { name: 'empty', hint: M('empty').hint, disabled: !f.wireOf(p), why: w('why.empty') });
            } else if (!p.input && !p.exec) {
                add(w('menu.use'), () => G.pickers.nodeSearch(app, f, x, y, { from: p }), { name: 'use' });
                // A worked-out value, kept as a local (Blueprint's Promote to Variable)
                if (p.node > 0 && p.local === undefined && !f.keepable(p.node)) {
                    const read = app.editor.doc.statementsReading(f.graph, p.node).length > 0;
                    add(w('menu.keepLocal'), () => f.keepAsLocal(p.node), { name: 'keep-local', summary: w('menu.keepLocal.summary'), disabled: !read, why: w('why.unread') });
                }
            } else if (!p.input && p.exec) {
                add(w('menu.next'), () => G.pickers.nodeSearch(app, f, x, y, { from: p }), { name: 'next' });
            } else if (p.node > 0) {
                add(w('menu.before'), () => G.pickers.nodeSearch(app, f, x, y, { from: p }), { name: 'before' });
            }
        }
        return out;
    }

    // The menu of a thing, at a point (or under `anchor`, its "…" button)
    function contextMenu(app, t, x, y, { anchor = null } = {}) {
        const ed = app.editor;
        if (!t || !ed || ed.error) return null;
        if (t.kind === 'wire') {
            // A data wire: a knot on it, or the wire taken away (what it came from stays)
            return menu(x, y, [
                { ...M('reroute'), section: 1, name: 'reroute', summary: w('menu.reroute.summary'), run: () => t.frame.addReroute(t.wire, x, y) },
                { ...M('breakLink'), section: 3, name: 'unlink', run: () => t.frame.breakWire(t.wire) },
            ], { title: w('title.wire'), className: 'tg-context' });
        }
        if (t.kind !== 'pin') select(app, t);
        const items = adds(app, t, x, y);
        const fk = nameOf(app, t);
        if (fk) items.push({ ...M('rename'), section: 2, name: 'rename', run: () => app.rename(fk, t) });
        if (t.kind === 'pin') {
            const linked = t.frame.linksOf(t.port).length > 0;
            items.push({ ...M('breakLinks'), section: 3, name: 'unlink', disabled: !linked, why: w('why.unlinked'), run: () => t.frame.breakLinks(t.port) });
            items.push(...t.frame.pinExtras(t.port, x, y));
        } else if (t.kind === 'region') {
            items.push({ ...M('unwrap'), summary: w('menu.unwrap.summary'), section: 3, name: 'unwrap', run: () => app.edit(() => { const list = t.item.list; list.splice(list.indexOf(t.item.value), 1, ...(t.item.value.decls || [])); }) });
            items.push({ ...M('delete'), summary: w('menu.delete.all'), danger: true, section: 3, name: 'delete', run: () => app.edit(() => { t.item.list.splice(t.item.list.indexOf(t.item.value), 1); }) });
        } else {
            const accessor = t.kind === 'frame' && t.item.accessor;
            const copies = !accessor && !(t.kind === 'row' && t.row.role === 'prefabNode');
            if (copies) {
                items.push({ ...M('cut'), section: 3, name: 'cut', run: () => app.command('cut') });
                items.push({ ...M('copy'), section: 3, name: 'copy', run: () => app.command('copy') });
            }
            if (t.kind !== 'note') items.push({ ...M('paste'), section: 3, name: 'paste', run: () => app.command('paste') });
            if (copies) items.push({ ...M('duplicate'), section: 3, name: 'duplicate', run: () => app.command('duplicate') });
            items.push({ ...M('delete'), danger: true, section: 3, name: 'delete', run: () => app.command('delete') });
        }
        const order = noteOrder(app, t);
        if (order) {
            const holder = t.kind === 'row' ? t.row.value : t.kind === 'node' ? t.node : t.item.value;
            items.push({ label: w('menu.note'), section: 4, name: 'note', summary: holder.comment ? firstLine(holder.comment) : undefined, run: () => G.cards.editNote(app, holder, order, { x, y }) });
        }
        // Each section after the one before, as every menu has them
        const sorted = items.map((item, i) => ({ item, i })).sort((a, b) => a.item.section - b.item.section || a.i - b.i).map(x2 => x2.item);
        return menu(x, y, sorted, { anchor, title: titleOf(app, t), className: 'tg-context' });
    }

    function firstLine(text) {
        const i = String(text).indexOf('\n');
        return i < 0 ? String(text) : String(text).slice(0, i) + ' …';
    }

    // What the menu is of, in words
    function titleOf(app, t) {
        const ed = app.editor;
        const s = ed.schema;
        switch (t.kind) {
            case 'card': case 'frame': case 'region': {
                const spec = t.item.accessor ? s.memberSpec('property') : t.item.spec;
                return `${spec?.title || ''} ${ed.doc.titleOf(t.item)}`.trim();
            }
            case 'row': return ed.doc.describe(t.row);
            case 'node': {
                const a = t.frame.analysis;
                const shown = a.merged.has(t.node.id) ? a.byId.get(a.merged.get(t.node.id)) : t.node;
                if (ed.doc.isGet(shown) || shown.k === 'name') return w('title.get', { name: shown.k === 'member' ? shown.member ?? '' : ed.doc.valueText(t.frame.graph, shown.id) });
                return s.title(shown.k);
            }
            case 'note': return w('title.note');
            case 'pin': return t.port.label || (t.port.exec ? w('title.arrow') : w('title.pin'));
            default: return null;
        }
    }

    G.menus = { contextMenu, targetAt, select, nameOf, addMember, addRow, M };
})(globalThis.TideGraph = globalThis.TideGraph || {});
