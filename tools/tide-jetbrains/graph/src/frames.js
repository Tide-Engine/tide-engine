// A frame: a titled box on the board that holds the nodes of one piece of code (a system's, a function's, a type's
// method's...). Its first node is the entry: the kind's sentence, a row with a pin for each thing the code takes,
// and the arrow the first statement is wired from. A node's input is a row: its name, then what's in it. A simple
// value (a number, a name, a path of fields) is shown and changed there, and is no node and has no wire; a value
// that's worked out is a node wired in (notes/visual-scripting.md, Simple values are in the input). A value that's
// more than a literal opens as a small frame of its own beside its card, with a last node where its value goes.
(function (G) {
    'use strict';
    const { el, svg, field, select, button, menu, typeColor } = G.ui;
    const { set } = G.json;
    const { Doc } = G;

    const ENTRY = -1;  // The entry, whose exec output is the body's first statement and whose pins are what it takes
    const RESULT = -2; // A value's result, whose input is its value
    // The board's measures, as tidec lays it out (graph_layout.c): a frame's head and the room under its nodes,
    // the entry's width, and where the nodes start to the right of it
    const HEAD = 48, PAD = 16, BOTTOM = 40, ENTRY_W = 320, NODE_X0 = PAD + ENTRY_W + 60;
    const MIN_W = NODE_X0 + 330, MIN_H = 190; // Room in an empty frame to say what to do, and to drop a first wire in

    const ONE = ['++', '--']; // The ways Set changes a value that take no value

    function portKey(p) {
        if (p.pin !== undefined) return `${ENTRY}|o|d|pin:${p.pin}`;
        if (p.local !== undefined) return `${p.local}|o|d|local`;
        return `${p.node}|${p.input ? 'i' : 'o'}|${p.exec ? 'x' : 'd'}|${p.path ? p.path.join('.') : ''}`;
    }

    // How big tidec takes a graph's nodes to be, and what an input is called on its node: the model's (Doc.sizes,
    // Lang.inputLabel)
    const estimate = (schema, g, analysis) => Doc.prototype.sizes.call({ schema }, g, analysis);
    const inputLabel = (app, node, p, always = false) => app.lang.inputLabel(app.editor.schema, node, p, always);

    class FrameView {
        // `item`: the board's item of a frame of code, or null for a value's frame, which `value` describes:
        // { slot, title, holder, key, order, type, anchor }
        constructor(board, item, value) {
            this.board = board;
            this.app = board.app;
            this.item = item;
            this.value = value || null;
            this.slot = value ? value.slot : { owner: item.id, key: 'body' };
            this.key = G.slotKey(this.slot);
            this.el = el('div.tg-frame' + (value ? '.tg-frame-value' : ''), { dataset: { frame: this.key, id: value ? '' : item.id } });
            this.head = el('div.tg-frame-head', { dataset: { handle: value ? '' : item.id } });
            this.body = el('div.tg-frame-body');
            G.ui.put(this.el, this.head, this.body);
            this.views = new Map();  // node id -> { el, node, x, y }
            this.ports = new Map();  // port key -> { port, dot, row, view, rel }
            this.places = new Map(); // Where moved nodes are while they move
            this.mode = 'none';      // 'none', 'near' (nodes drawn) or 'far' (blocks)
            this.size = { w: 400, h: 120 };
            this.origin = { x: 0, y: 0 };
        }

        get editor() { return this.app.editor; }
        get schema() { return this.app.editor.schema; }
        get doc() { return this.app.editor.doc; }
        get graph() { return this.doc.graph(this.slot); }
        get isValue() { return !!this.value; }
        get nodeX() { return this.isValue ? PAD : NODE_X0; }

        // What it is in the document now (after an edit, the document's objects may be new)
        refresh(item) {
            if (item) this.item = item;
            this.pins = this.isValue ? [] : this.doc.pinsOfSlot(this.slot);
            const g = this.graph;
            this.analysis = g ? this.doc.analyse(g, { rootReads: this.isValue }) : { folded: new Set(), shown: new Map(), merged: new Map(), mergedInto: new Map(), readers: new Map(), byId: new Map() };
            this.reached = g ? this.doc.reached(g) : new Set();
            this.sizes = g ? estimate(this.schema, g, this.analysis) : new Map();
            this.estimateSize();
        }

        // The body's size from what tidec takes its nodes to be, until they're drawn and measured
        estimateSize() {
            const g = this.graph;
            const s = this.schema;
            let x0 = 0, y0 = 0, x1 = 0, y1 = 0, first = true;
            for (const n of g ? Doc.nodes(g) : []) {
                if (!this.drawn(n)) continue;
                const e = this.sizes.get(n.id);
                const x = Number(n.x) || 0, y = Number(n.y) || 0;
                if (first || x < x0) x0 = x;
                if (first || y < y0) y0 = y;
                if (first || x + e.w > x1) x1 = x + e.w;
                if (first || y + e.h > y1) y1 = y + e.h;
                first = false;
            }
            for (const note of g?.notes || []) {
                x0 = Math.min(x0, note.x || 0); y0 = Math.min(y0, note.y || 0);
                x1 = Math.max(x1, (note.x || 0) + (note.w || 200)); y1 = Math.max(y1, (note.y || 0) + (note.h || 120));
            }
            this.origin = this.isValue ? { x: first ? 0 : x0, y: first ? 0 : y0 } : { x: Math.min(0, x0), y: Math.min(0, y0) };
            const entryH = this.isValue ? 60 : 60 + 26 * (this.rowCount() + 1);
            const w = this.nodeX + (x1 - this.origin.x) + (this.isValue ? 60 + 160 : 0) + BOTTOM;
            const h = PAD + Math.max(y1 - this.origin.y, entryH) + BOTTOM;
            this.size = { w: Math.max(this.isValue ? 300 : MIN_W, w), h: Math.max(this.isValue ? 130 : MIN_H, h) };
        }

        // Whether a node of the graph is drawn as a node: not a value shown in the input that reads it, nor a call
        // drawn as its statement
        drawn(n) {
            return !this.analysis.folded.has(n.id) && !this.analysis.mergedInto.has(n.id);
        }

        rowCount() {
            const v = this.item?.value;
            if (!v) return 0;
            return (Array.isArray(v.params) ? v.params.length : 0) + (Array.isArray(v.payload) ? v.payload.length : 0) + (v.trigger ? 1 : 0) + ('returnType' in v ? 1 : 0);
        }

        // Where a node is drawn in the body
        dx(n) { return this.nodeX + (this.places.get(n.id)?.x ?? Number(n.x) ?? 0) - this.origin.x; }
        dy(n) { return PAD + (this.places.get(n.id)?.y ?? Number(n.y) ?? 0) - this.origin.y; }

        // A screen point as a place for a node
        toLocal(clientX, clientY) {
            const r = this.body.getBoundingClientRect();
            const z = this.board.zoom;
            return { x: Math.round((clientX - r.left) / z - this.nodeX + this.origin.x), y: Math.round((clientY - r.top) / z - PAD + this.origin.y) };
        }

        // ---------------------------------------------------------------------------------------------------------
        // The head: what it is, its name, its menu

        renderHead() {
            const app = this.app;
            const s = this.schema;
            const head = this.head;
            head.replaceChildren();
            if (this.isValue) {
                G.ui.put(head, el('span.tg-chip.tg-chip-value', { text: s.keyLabel(this.value.key === 'components' ? 'value' : this.value.key || 'value') }),
                    el('span.tg-frame-title', { text: this.value.title }),
                    el('span.tg-grow'),
                    button('Done', () => app.closeValue(), 'Close this value (Esc)', 'tg-button.tg-primary'));
                return;
            }
            const item = this.item;
            const v = item.value;
            const spec = item.spec;
            const order = item.accessor ? s.element('accessor') : spec?.keys || [];
            const group = item.member || item.accessor ? 'behaviour' : (spec?.group || 'More').toLowerCase();
            const property = s.memberSpec('property');
            G.ui.put(head, el('span.tg-chip.tg-chip-' + group, { text: item.accessor ? property?.title || 'Property' : spec?.title || v.kind, title: item.accessor ? property?.summary : spec?.summary }));
            if (item.member || item.accessor) {
                G.ui.put(head, button(item.type?.name ?? '', () => app.reveal(item.type.id), 'Go to its card', 'tg-link.tg-frame-owner'), el('span.tg-frame-dot', { text: '.' }));
            }
            if (item.accessor) {
                G.ui.put(head, el('span.tg-frame-title', { text: `${item.property?.name ?? ''} ${v.word ?? ''}` }));
            } else if (v.kind === 'operator') {
                G.ui.put(head, el('span.tg-frame-title', { text: 'operator' }), select(v.op ?? '+', s.ops({ kind: 'operator' }) || [], op => app.edit(() => { v.op = op; }), 'Which operator', 'tg-select.tg-op'));
            } else if (order.includes('name')) {
                G.ui.put(head, field(v.name ?? '', x => app.edit(() => {
                    v.name = x;
                    // Renamed, a replacement keeps what it replaces only if that was its own name
                    if (v.overrides && !v.overrides.endsWith('.' + x)) delete v.overrides;
                }), {
                    className: 'tg-input.tg-name', placeholder: 'Name', check: G.cards.checkName, fk: `${v.id}:name`, title: `${spec?.title || ''} name`,
                    onEnter: input => {
                        // A new frame's first Enter goes on to what it needs
                        const fresh = !this.graph?.nodes?.length && !(v.params || []).length && !(v.payload || []).length;
                        input.blur();
                        if (!fresh) return;
                        // A handler's event first, when the one it started with isn't one the file has
                        const event = v.trigger && !v.trigger.changed && !this.doc.declared(['event']).some(e => e.name === v.trigger.type);
                        requestAnimationFrame(() => {
                            if (event) this.body.querySelector('[data-row="trigger"] .tg-type')?.click();
                            else if (order.includes('params')) this.openNeeds();
                        });
                    },
                }));
            } else {
                G.ui.put(head, el('span.tg-frame-title', { text: spec?.title || v.kind }));
            }
            G.ui.put(head, el('span.tg-tags', null, G.cards.tagsOf(app, item)),
                el('span.tg-grow'), el('span.tg-badge', { dataset: { badge: item.id } }),
                button('⋯', e => G.cards.moreMenu(app, item, e.clientX, e.clientY, { anchor: e.currentTarget }), 'More: its options, a note, tidy up, delete', 'tg-more'));
        }

        openNeeds() {
            const add = this.body.querySelector('.tg-entry .tg-add-row');
            if (add) G.pickers.needsPicker(this.app, this.item, { anchor: add });
        }

        // ---------------------------------------------------------------------------------------------------------
        // The body

        // Draws what's inside: the entry and the nodes ('near'), blocks where they are ('far'), or nothing
        render(mode) {
            this.mode = mode;
            this.views.clear();
            this.ports.clear();
            this.el.style.width = this.size.w + 'px';
            this.body.style.height = this.size.h + 'px';
            this.el.classList.toggle('tg-far', mode === 'far');
            const g = this.graph;
            if (mode === 'none') {
                this.body.replaceChildren();
                return;
            }
            if (mode === 'far') {
                this.renderFar(g);
                return;
            }
            this.notesLayer = el('div.tg-frame-notes');
            this.wires = svg('svg', { class: 'tg-wires' });
            this.nodesLayer = el('div.tg-frame-nodes');
            this.body.replaceChildren(this.notesLayer, this.wires, this.nodesLayer);
            if (this.isValue) this.renderResult(g);
            else this.renderEntry(g);
            if (g) {
                for (const note of g.notes || []) this.renderNote(note);
                for (const n of Doc.nodes(g)) if (this.drawn(n)) this.renderNode(n);
            }
            if (!this.isValue && g && !Doc.nodes(g).length) {
                G.ui.put(this.nodesLayer, el('div.tg-frame-hint', { style: { left: PAD + ENTRY_W + 30 + 'px', top: PAD + 1 + 'px' } },
                    el('span.tg-frame-hint-arrow', { text: '←' }), 'Drag from the arrow to add what happens'));
            } else if (this.isValue && g && !g.root) {
                G.ui.put(this.nodesLayer, el('div.tg-frame-hint', { style: { left: PAD + 'px', top: PAD + 70 + 'px' } }, 'Click "pick…" for its value; "Work it out…" there makes a node'));
            }
            this.measure();
            this.drawWires();
            this.showSelection();
        }

        // Far away: every node as a block of its kind's colour, enough to know the shape of the code by
        renderFar(g) {
            const s = this.schema;
            const far = svg('svg', { class: 'tg-far-nodes', width: this.size.w, height: this.size.h });
            if (!this.isValue) {
                G.ui.put(far, svg('rect', { x: PAD, y: PAD, width: ENTRY_W, height: 34 + 26 * (this.rowCount() + 1), rx: 10, class: 'tg-far-entry' }));
            }
            for (const n of g ? Doc.nodes(g) : []) {
                if (!this.drawn(n)) continue;
                const e = this.sizes.get(n.id);
                const r = svg('rect', { x: this.dx(n), y: this.dy(n), width: e.w, height: Math.max(40, e.h - 24), rx: 10 });
                r.style.fill = s.color(n.k);
                if (!this.reached.has(n.id)) r.style.opacity = 0.35;
                G.ui.put(far, r);
            }
            this.body.replaceChildren(far);
        }

        // The entry: the kind's sentence, a row for each thing the code takes with a pin to wire from, and the
        // arrow its first statement is wired from
        renderEntry(g) {
            const app = this.app;
            const s = this.schema;
            const doc = this.doc;
            const item = this.item;
            const v = item.value;
            const spec = item.spec;
            const order = item.accessor ? s.element('accessor') : spec?.keys || [];
            const own = [...(item.type?.typeParams || []), ...(v.typeParams || [])];
            const edit = change => app.edit(change);
            const view = el('div.tg-node.tg-entry', { dataset: { id: ENTRY, entry: item.id }, style: { left: PAD + 'px', top: PAD + 'px', width: ENTRY_W + 'px' } });
            const record = { el: view, node: { id: ENTRY }, fixed: true, x: PAD, y: PAD };
            const sentence = item.accessor ? s.accessorInfo[v.word]?.label || v.word || '' : spec?.sentence || (order.includes('params') ? s.keyLabel('params') : spec?.title || '');
            const head = el('div.tg-entry-head', null, el('span.tg-entry-sentence', { text: sentence }));
            if (g) {
                const port = { node: ENTRY, input: false, exec: true, path: null, label: 'start' };
                const dot = el('span.tg-dot.tg-dot-exec');
                const row = el('span.tg-port.tg-out.tg-port-exec.tg-entry-start', { dataset: { port: portKey(port) }, title: 'What happens first: drag from here' }, dot);
                G.ui.put(head, row);
                this.ports.set(portKey(port), { port, dot, row, view: record });
            }
            G.ui.put(view, head);
            const rows = el('div.tg-entry-rows');
            const pinOf = (name, type) => {
                const port = { node: ENTRY, input: false, exec: false, pin: name, label: name, type };
                const dot = el('span.tg-dot');
                const row = el('span.tg-port.tg-out.tg-pin', { dataset: { port: portKey(port), pin: name }, title: `Drag from here into an input to use ${name}` }, dot);
                this.ports.set(portKey(port), { port, dot, row, view: record });
                return row;
            };
            // Renaming what it takes renames its reads
            const rename = (holder, old) => x => edit(d => { holder.name = x; d.renamePin(g, old, x); });

            // A handler's event
            if (v.trigger && typeof v.trigger === 'object') {
                const t = v.trigger;
                const torder = s.element('trigger');
                const lang = app.lang;
                const triggerMenu = e => {
                    e.preventDefault();
                    e.stopPropagation();
                    menu(e.clientX, e.clientY, ['verified', 'changed'].filter(k => torder.includes(k)).map(k => ({
                        label: k === 'changed' ? 'When a field changes (Changed<…>)' : s.keyLabel(k), checked: !!t[k], stay: true,
                        run: on => edit(() => set(t, k, on ? true : undefined, torder)),
                    })), { title: s.keyLabel('trigger') });
                };
                G.ui.put(rows, el('div.tg-entry-row', { dataset: { id: v.id, row: 'trigger' }, oncontextmenu: triggerMenu },
                    el('span.tg-mode.tg-mode-fixed', { text: s.keyLabel('trigger').toLowerCase() }),
                    t.changed
                        ? field(t.type ?? '', x => edit(() => { t.type = x; }), { className: 'tg-input.tg-entry-name', placeholder: 'Component.field', fk: `${v.id}:trigger`, title: 'The field whose change it hears' })
                        : G.cards.typeChip(app, {
                            type: t.type, kinds: ['event'], details: ['event'], fk: `${v.id}:trigger`, placeholder: 'pick an event',
                            ask: () => {
                                const sp = lang.span(v.id);
                                const open = sp ? lang.text.indexOf('(', sp[0]) : -1;
                                return open >= 0 && open < sp[1] ? lang.completion(open + 1) : Promise.resolve([]);
                            },
                            onPick: type => edit(d => {
                                const old = t.name, was = t.type;
                                t.type = type;
                                // A name that was its event's follows the new event, and so do its reads
                                if (!old || old === G.pickers.nameFor(was)) {
                                    set(t, 'name', G.pickers.nameFor(type), torder);
                                    if (old) d.renamePin(g, old, t.name);
                                }
                            }),
                        }),
                    field(t.name ?? '', rename(t, t.name), { className: 'tg-input.tg-entry-name', placeholder: 'name', check: G.cards.checkName, fk: `${v.id}:trigger-name` }),
                    t.name ? pinOf(t.name, t.changed ? undefined : t.type) : null));
            }
            // A command's values
            if (order.includes('payload')) {
                const list = Array.isArray(v.payload) ? v.payload : [];
                const porder = s.element('payload');
                for (const p of list) {
                    G.ui.put(rows, el('div.tg-entry-row', { dataset: { id: p.id } },
                        el('span.tg-mode.tg-mode-fixed', { text: s.keyLabel('payload').toLowerCase() }),
                        G.cards.typeChip(app, { type: p.type, at: p.id, own, fk: `${p.id}:type`, onPick: t => edit(() => set(p, 'type', t, porder)) }),
                        field(p.name ?? '', rename(p, p.name), { className: 'tg-input.tg-entry-name', placeholder: 'name', check: G.cards.checkName, fk: `${p.id}:name` }),
                        button('×', () => edit(() => { list.splice(list.indexOf(p), 1); }), 'Remove', 'tg-icon.tg-row-remove'),
                        p.name ? pinOf(p.name, p.type) : null));
                }
                G.ui.put(rows, button(`+ ${s.keyLabel('payload').toLowerCase()}…`, e => G.pickers.typePicker(app, {
                    anchor: e.currentTarget, title: s.keyLabel('payload'),
                    onPick: type => {
                        let made;
                        edit(d => {
                            if (!Array.isArray(v.payload)) set(v, 'payload', [], order);
                            made = { id: d.newId(), type, name: d.uniqueName(v.payload, G.pickers.nameFor(type)) };
                            v.payload.push(made);
                        });
                        if (made) this.board.focusSoon(`${made.id}:name`, true);
                    },
                }), 'A value the player sends with it', 'tg-add-row'));
            }
            // What it takes
            if (item.accessor) {
                if (v.word === 'set') G.ui.put(rows, el('div.tg-entry-row', null, el('span.tg-mode.tg-mode-fixed', { text: s.keyLabel('value').toLowerCase() }),
                    el('span.tg-type.tg-type-fixed', null, el('span.tg-type-dot', { style: { background: typeColor(item.property?.type) } }), item.property?.type ?? ''),
                    el('span.tg-entry-name.tg-muted', { text: 'value' }), pinOf('value', item.property?.type)));
            } else if (order.includes('params')) {
                const list = Array.isArray(v.params) ? v.params : [];
                for (const p of list) G.ui.put(rows, this.paramRow(v, list, p, 0, pinOf, rename, own));
                G.ui.put(rows, button(s.addRow(v.kind, !!item.member), e => G.pickers.needsPicker(app, item, { anchor: e.currentTarget }),
                    'Add something it takes: pick what', 'tg-add-row'));
            }
            // What it gives back
            if (order.includes('returnType')) {
                G.ui.put(rows, el('div.tg-entry-row.tg-entry-result', { dataset: { row: 'result' } },
                    el('span.tg-mode.tg-mode-fixed', { text: s.keyLabel('returnType').toLowerCase() }),
                    G.cards.typeChip(app, { type: v.returnType, at: v.id, own, allow: { void: true }, fk: `${v.id}:returnType`, onPick: t => edit(() => set(v, 'returnType', t, order)) })));
            }
            if (v.extern) G.ui.put(rows, el('div.tg-entry-note', { text: `${s.keyLabel('extern')}: its code isn't nodes` }));
            G.ui.put(view, rows);
            G.ui.put(this.nodesLayer, view);
            this.views.set(ENTRY, record);
        }

        // A parameter's row: how it's taken, its type, its name, its pin; a filter's has no name and no pin
        paramRow(routine, list, p, depth, pinOf, rename, own) {
            const app = this.app;
            const s = this.schema;
            const porder = routine.kind === 'sample' ? s.element('sampleParam') : s.element('param');
            const edit = change => app.edit(change);
            const g = this.graph;
            const filter = s.modeIsFilter(p.mode);
            const mode = p.mode ?? 'read';
            const modes = e => {
                e.stopPropagation();
                const pick = m => edit(d => {
                    const was = p.mode;
                    set(p, 'mode', m, porder);
                    if (s.modeIsFilter(m)) {
                        if (p.name) { delete p.name; }
                        delete p.default;
                    } else if (!p.name) {
                        set(p, 'name', m === 'query' ? 'entities' : G.pickers.nameFor(p.type), porder);
                    }
                    if (m === 'query' && !Array.isArray(p.query)) { set(p, 'query', [], porder); delete p.type; }
                    if (was === 'query' && m !== 'query') { delete p.query; if (!p.type) set(p, 'type', 'int', porder); }
                });
                const all = s.paramModes.filter(m => depth === 0 || m !== 'query');
                menu(e.clientX, e.clientY, [
                    ...all.filter(m => !s.modeAdvanced(m)).map(m => ({ label: s.modeLabel(m), summary: s.modeHelp(m), checked: m === mode, run: () => pick(m) })),
                    ...all.filter(m => s.modeAdvanced(m)).map(m => ({ label: s.modeLabel(m), summary: s.modeHelp(m), checked: m === mode, group: 'More', run: () => pick(m) })),
                ], { anchor: e.currentTarget, title: 'How it takes it' });
            };
            const more = e => {
                e.preventDefault();
                e.stopPropagation();
                const i = list.indexOf(p);
                menu(e.clientX, e.clientY, [
                    ...['local', 'verified', 'previous'].filter(k => porder.includes(k)).map(k => ({
                        label: s.keyLabel(k), summary: s.keyHelp(k), checked: !!p[k], stay: true, run: on => edit(() => set(p, k, on ? true : undefined, porder)),
                    })),
                    porder.includes('role') && { label: `${s.keyLabel('role')}…`, summary: p.role || 'Raw, Linear, or Raw(float)', run: () => G.cards.promptText({ x: e.clientX, y: e.clientY, title: s.keyLabel('role'), value: p.role, placeholder: 'Raw', onDone: t => edit(() => set(p, 'role', t || undefined, porder)) }) },
                    porder.includes('default') && !filter && mode !== 'query' && { label: `${s.keyLabel('default')}…`, summary: 'The value it has when a call leaves it out', run: () => app.openValue({ owner: p.id, key: 'default' }, p, 'default', porder, p.type) },
                    { label: `${s.keyLabel('comment')}…`, summary: p.comment || s.keyHelp('comment'), run: () => G.cards.editNote(app, p, porder, { x: e.clientX, y: e.clientY }) },
                    { separator: true },
                    i > 0 && { label: 'Move up', run: () => edit(() => { list.splice(i - 1, 0, list.splice(i, 1)[0]); }) },
                    i < list.length - 1 && { label: 'Move down', run: () => edit(() => { list.splice(i + 1, 0, list.splice(i, 1)[0]); }) },
                    { label: 'Remove', danger: true, run: () => edit(() => { list.splice(list.indexOf(p), 1); }) },
                ], { title: p.name || p.type });
            };
            const words = ['local', 'verified', 'previous'].filter(k => p[k]).map(k => el('span.tg-row-tag', { text: k, title: s.keyLabel(k) }));
            const row = el('div.tg-entry-row' + (filter ? '.tg-entry-filter' : '') + (depth ? '.tg-entry-sub' : ''), { dataset: { id: p.id }, oncontextmenu: more, title: p.comment || undefined },
                routine.kind === 'sample' ? null : button(s.modeLabel(mode), modes, `${s.modeHelp(mode) || 'How it takes it'} · click to change`, 'tg-mode'),
                words,
                p.ownership ? el('span.tg-type.tg-type-fixed', { text: 'ownership' })
                    : mode === 'query' ? null
                        : G.cards.typeChip(app, { type: p.type, at: p.id, own, fk: `${p.id}:type`, onPick: t => edit(d => {
                            const old = p.name, was = p.type;
                            set(p, 'type', t, porder);
                            // A name that was its type's follows the new type, and so do its reads
                            const next = G.pickers.nameFor(t);
                            if (old && !filter && old === G.pickers.nameFor(was) && !list.some(o => o !== p && o.name === next)) {
                                p.name = next;
                                d.renamePin(g, old, next);
                            }
                        }) }),
                p.scene ? el('span.tg-row-tag', { text: 'scene' }) : null,
                filter ? el('span.tg-grow') : field(p.name ?? '', rename(p, p.name), { className: 'tg-input.tg-entry-name', placeholder: 'name', check: G.cards.checkName, fk: `${p.id}:name` }),
                p.default ? el('span.tg-row-tag', { text: '= …', title: s.keyLabel('default'), onclick: () => app.openValue({ owner: p.id, key: 'default' }, p, 'default', porder, p.type) }) : null,
                button('×', () => edit(() => { list.splice(list.indexOf(p), 1); }), 'Remove', 'tg-icon.tg-row-remove'),
                !filter && p.name && depth === 0 ? pinOf(p.name, mode === 'query' ? undefined : p.type) : null);
            if (mode !== 'query') return row;
            // A query's own: what the entities it goes through have
            const inner = Array.isArray(p.query) ? p.query : [];
            return [row, inner.map(q => this.paramRow(routine, inner, q, depth + 1, pinOf, rename, own)),
                button('+ with…', e => G.pickers.typePicker(app, {
                    anchor: e.currentTarget, kinds: ['component'], details: ['component'], ask: () => app.lang.completionInParams(routine), title: 'What the entities it goes through have',
                    onPick: type => edit(d => { if (!Array.isArray(p.query)) set(p, 'query', [], porder); p.query.push(d.newParam(type, G.pickers.nameFor(type))); }),
                }), 'A component the query asks for', 'tg-add-row.tg-entry-sub')];
        }

        // A value's result: where its value is wired
        renderResult(g) {
            const nodes = g ? Doc.nodes(g).filter(n => this.drawn(n)) : [];
            const root = g ? Doc.nodeIn(g, g.root) : null;
            let right = 0;
            for (const n of nodes) right = Math.max(right, this.dx(n) + this.sizes.get(n.id).w);
            const x = nodes.length ? right + 60 : PAD;
            const y = root && this.drawn(root) ? this.dy(root) : PAD;
            const port = { node: RESULT, input: true, exec: false, path: null, label: this.value.type || 'its value' };
            const view = el('div.tg-node.tg-result', { dataset: { id: RESULT }, style: { left: x + 'px', top: y + 'px' } },
                el('div.tg-node-header', null, el('span.tg-node-title', { text: this.schema.keyLabel(this.value.key === 'components' ? 'value' : this.value.key || 'value') })));
            const record = { el: view, node: { id: RESULT }, fixed: true, x, y };
            G.ui.put(view, this.inputRow(port, record));
            G.ui.put(this.nodesLayer, view);
            this.views.set(RESULT, record);
        }

        renderNote(note) {
            const text = el('div.tg-note-text', { text: note.text || '' });
            const view = el('div.tg-note', {
                dataset: { note: note.id },
                style: { left: this.dx(note) + 'px', top: this.dy(note) + 'px', width: (note.w || 200) + 'px', height: (note.h || 120) + 'px' },
            }, el('div.tg-note-bar'), text, el('div.tg-note-size'));
            text.addEventListener('dblclick', e => {
                e.stopPropagation();
                const input = field(note.text || '', value => this.app.edit(() => { note.text = value; }), { multiline: true, className: 'tg-note-edit', rows: 4 });
                text.replaceWith(input);
                input.focus();
                input.addEventListener('blur', () => this.board.render());
            });
            G.ui.put(this.notesLayer, view);
            this.views.set(note.id, { el: view, node: note, note: true, x: this.dx(note), y: this.dy(note) });
        }

        // ---------------------------------------------------------------------------------------------------------
        // Nodes

        // A node. Its title is what it does in the node's own words (a call's name, "ball.Bounce", "Set"), each input
        // is a row (its name, then what's in it), and its outputs are on the right. A value read by one input and
        // simple (a number, a name, a path of fields) is no node: it's shown and changed in that input's row.
        renderNode(n) {
            const s = this.schema;
            const app = this.app;
            const a = this.analysis;
            // A call that one statement runs is drawn as the statement: the call's name and inputs, the statement's
            // place, arrows and value
            const call = a.merged.has(n.id) ? a.byId.get(a.merged.get(n.id)) : null;
            const shown = call || n;
            const spec = s.node(shown.k);
            const own = s.node(n.k);
            const order = s.nodeOrder(shown.k);
            const edit = (key, value) => app.edit(() => set(shown, key, value, order));
            const reached = this.reached.has(n.id);
            const ports = this.portsOf(n, call);
            const inputs = ports.filter(p => p.input && !p.exec), outs = ports.filter(p => !p.input && !p.exec);
            const execIn = ports.find(p => p.input && p.exec), execOuts = ports.filter(p => !p.input && p.exec);
            const record = { el: null, node: n, x: this.dx(n), y: this.dy(n), ids: call ? [n.id, call.id] : [n.id] };
            const dotOf = p => el('span.tg-dot' + (p.exec ? '.tg-dot-exec' : ''));
            const outRow = p => {
                const dot = dotOf(p);
                const row = el('div.tg-port.tg-out' + (p.exec ? '.tg-port-exec' : '') + (p.local !== undefined ? '.tg-pin' : ''),
                    { dataset: { port: portKey(p) }, title: p.local !== undefined ? `Drag from here to use ${p.label}` : undefined },
                    p.label ? el('span.tg-port-label', { text: p.label }) : null, dot);
                this.ports.set(portKey(p), { port: p, dot, row, view: record });
                return row;
            };

            if (!spec) {
                const view = el('div.tg-node.tg-unknown', { dataset: { id: n.id }, style: { left: record.x + 'px', top: record.y + 'px' } },
                    el('div.tg-node-header', null, el('span.tg-node-title', { text: n.k })),
                    el('div.tg-node-unknown', { text: `This editor doesn't know '${n.k}' nodes. It's kept as it is.` }), ports.map(p => (p.input ? this.inputRow(p, record) : outRow(p))));
                record.el = view;
                G.ui.put(this.nodesLayer, view);
                this.views.set(n.id, record);
                return;
            }

            // Its own values, by what each key holds
            const fields = [];
            const primaryKey = ['name', 'member'].find(k => spec.keys.includes(k) && s.keyType(k, shown) === 'string');
            let primary = null, op = null;
            const tags = [];
            for (const key of spec.keys) {
                const type = s.keyType(key, shown);
                if (key === 'ref' && shown.k === 'local') continue;
                if (key === 'op') {
                    const ops = s.ops(shown) || [];
                    const named = shown.k === 'binary' || shown.k === 'unary';
                    // An operator is its node's title, by what it's called: picking another there changes the node
                    // (Adding or taking away one takes no value: what was there goes with the choice)
                    const pick = v => app.edit(doc => {
                        set(shown, 'op', v, order);
                        if (shown.k === 'assign' && ONE.includes(v) && shown.value) {
                            const old = shown.value;
                            delete shown.value;
                            doc.reindex();
                            doc.dropValue(this.graph, old);
                        }
                    });
                    if (shown.k === 'assign') {
                        // How Set changes what it sets: its sign on a button that says it in words, and a menu of
                        // what each does (the schema's `assignInfo`)
                        const words = s.assignLabel(shown.op);
                        op = button(shown.op ?? '=', e => menu(e.clientX, e.clientY, ops.map(o => ({
                            label: s.assignLabel(o) || o, hint: s.assignLabel(o) ? o : undefined, name: 'op:' + o, checked: o === shown.op, run: () => pick(o),
                        })), { anchor: e.currentTarget, title: 'How it changes it' }), words ? `It ${words}: pick another way` : 'How it changes it', 'tg-op.tg-op-button');
                        op.dataset.op = shown.op ?? '';
                        continue;
                    }
                    op = select(shown.op ?? '', ops.map(o => [o, named ? `${s.opTitle(o, shown.k === 'unary')}  ${o}` : o]), pick, named ? 'What it does: pick another' : 'How it changes it', named ? 'tg-select.tg-op.tg-op-title' : 'tg-select.tg-op');
                    continue;
                }
                if (type === 'flag') {
                    if (shown.k === 'bool' && key === 'value') {
                        const box = el('input', { type: 'checkbox', checked: !!shown.value, onchange: e => edit('value', e.target.checked), onpointerdown: e => e.stopPropagation() });
                        fields.push(el('label.tg-value-bool', { onpointerdown: e => e.stopPropagation() }, box, el('span', { text: shown.value ? 'yes' : 'no' })));
                    } else if (shown[key]) {
                        tags.push(el('span.tg-node-tag', { text: s.flagLabel(key).toLowerCase(), title: s.flagHelp(key) }));
                    }
                    continue;
                }
                if (key === 'typeArgs') {
                    for (const [i, t] of (shown.typeArgs || []).entries()) {
                        tags.push(el('span.tg-node-tag', null, t, button('×', () => app.edit(() => { shown.typeArgs.splice(i, 1); if (!shown.typeArgs.length) delete shown.typeArgs; }), 'Remove this type', 'tg-icon')));
                    }
                    continue;
                }
                if (key === 'parts') {
                    fields.push(this.templateField(shown, order));
                    continue;
                }
                if (key === 'params' && shown.k === 'lambda') {
                    fields.push(this.lambdaParams(shown));
                    continue;
                }
                if (type === 'type') {
                    const optional = spec.declares || shown.k === 'lambda';
                    fields.push(el('span.tg-field', null, key === 'type' ? null : el('span.tg-field-label', { text: s.keyLabel(key) }), G.cards.typeChip(app, {
                        type: shown[key], at: shown.id, fk: `${shown.id}:${key}`, placeholder: optional ? 'any type' : 'pick a type',
                        allow: optional ? { none: 'Work it out', noneSummary: 'The type of the value it gets' } : {},
                        onPick: t => edit(key, t),
                    })));
                    continue;
                }
                if (type === 'string' && key !== 'formats' && key !== 'argNames') {
                    // (A local's new name is its reads' too: they name its node, not its name)
                    const input = field(shown[key] ?? '', v => edit(key, v === '' && !['name', 'member'].includes(key) ? undefined : v), {
                        placeholder: key === 'binding' ? 'a name for it' : key, fk: `${shown.id}:${key}`, title: s.keyLabel(key), grow: 4,
                        className: 'tg-input.tg-node-input' + (key === primaryKey ? '.tg-node-name' : ''),
                        check: ['name', 'member', 'binding', 'field'].includes(key) ? v => (v === '' && key === 'binding' ? null : /^[A-Za-z_][A-Za-z0-9_.]*$/.test(v) ? null : 'A name: letters, digits and _') : undefined,
                    });
                    if (key === primaryKey) primary = input; else fields.push(input);
                    continue;
                }
                if (type === 'text') {
                    const wide = shown.k === 'text' || shown.k === 'code';
                    const check = shown.k === 'int' || shown.k === 'float' ? v => (G.values.NUMBER.test(v.trim()) && !v.trim().startsWith('-') ? null : `"${v}" isn't a number`) : undefined;
                    const shownText = shown.k === 'char' ? String(shown.text ?? '').replace(/^'|'$/g, '') : shown.text ?? '';
                    fields.push(field(shownText, v => edit('text', shown.k === 'char' ? `'${v}'` : shown.k === 'int' || shown.k === 'float' ? v.trim() : v), {
                        multiline: wide, rows: wide ? Math.max(2, Math.min(8, String(shown.text ?? '').split('\n').length)) : 1, mono: wide, check, grow: wide ? 0 : 3,
                        className: 'tg-input.tg-node-input' + (wide ? '.tg-node-code' : ''), placeholder: shown.k === 'string' ? 'text' : wide ? 'Tide' : '0', fk: `${shown.id}:text`,
                    }));
                }
            }

            // A value with nothing in it to wire, and one value of its own or none: a pill, with its pin beside it
            const chip = !spec.exec && !call && !inputs.length && !execOuts.length && fields.length + (primary ? 1 : 0) <= 1 && !op && !spec.lists && !spec.items && !spec.map && outs.length === 1;
            const view = el('div.tg-node' + (own?.exec ? '.tg-exec' : '.tg-data') + (chip ? '.tg-chipnode' : '') + (reached ? '' : '.tg-dim'),
                { dataset: { id: n.id }, style: { left: record.x + 'px', top: record.y + 'px', '--tg-cat': s.color(shown.k) } });
            record.el = view;
            const words = shown.k === 'binary' || shown.k === 'unary' ? s.opTitle(shown.op, shown.k === 'unary') : spec.title;
            const moreButton = button('⋯', e => this.nodeMenu(n, e.clientX, e.clientY), 'More', 'tg-more.tg-node-more');
            if (chip) {
                const out = outs[0];
                const dot = dotOf(out);
                const lone = shown.k === 'local' ? el('span.tg-node-name', { text: this.doc.valueText(this.graph, shown.id) }) : null;
                const row = el('div.tg-port.tg-out.tg-chip-row', { dataset: { port: portKey(out) }, title: reached ? s.help(shown.k) : `${s.help(shown.k)}. Nothing uses it yet: drag from its pin into an input.` },
                    fields.length || primary || lone ? null : el('span.tg-node-title', { text: words }),
                    shown.k === 'member' && shown.shorthand ? el('span.tg-node-dot', { text: '.' }) : null,
                    primary, lone, fields, tags, dot);
                this.ports.set(portKey(out), { port: out, dot, row, view: record });
                G.ui.put(view, row);
                G.ui.put(this.nodesLayer, view);
                this.views.set(n.id, record);
                return;
            }

            const header = el('div.tg-node-header', { title: s.help(shown.k) });
            if (execIn) {
                const dot = dotOf(execIn);
                const row = el('span.tg-port.tg-in.tg-port-exec.tg-head-port', { dataset: { port: portKey(execIn) } }, dot);
                this.ports.set(portKey(execIn), { port: execIn, dot, row, view: record });
                G.ui.put(header, row);
            }
            // What a method is called on, or a field is of: in the title when it's a name or a path (ball.Bounce),
            // on the title's row when it's wired from a node, a row of its own when the title's row has an arrow
            const objectPort = shown.k === 'method' || shown.k === 'member' ? inputs.find(p => p.key === 'object') : null;
            const objectWire = objectPort ? this.wireOf(objectPort) : 0;
            const objectNode = !!objectWire && !a.folded.has(objectWire);
            const objectRow = objectNode && (!!execIn || inputs.length > 1);
            const rowInputs = inputs.filter(p => p !== objectPort || objectRow);
            // A value made of one other with nothing else to set: its input on the title's row, one line in all
            const headIn = objectPort && objectNode && !objectRow ? objectPort
                : !objectPort && !execIn && !op && rowInputs.length === 1 && !rowInputs[0].group && !fields.length && !spec.lists && !spec.items && !spec.map && this.wireOf(rowInputs[0]) && !a.folded.has(this.wireOf(rowInputs[0])) ? rowInputs[0] : null;
            if (headIn) {
                const dot = dotOf(headIn);
                const row = el('span.tg-port.tg-in.tg-head-port', { dataset: { port: portKey(headIn) }, title: `What it is ${headIn.label}: a wire from a node` }, dot);
                this.ports.set(portKey(headIn), { port: headIn, dot, row, view: record, label: headIn.label });
                G.ui.put(header, row);
            }
            if (objectPort && !objectNode) {
                // In the title: a chip to change it by, or "pick…"
                const dot = el('span.tg-dot.tg-dot-off');
                const row = el('span.tg-port.tg-in.tg-inline.tg-title-port', { dataset: { port: portKey(objectPort) } }, dot,
                    objectWire ? this.pathChip(objectPort, objectWire) : this.pickButton(objectPort));
                this.ports.set(portKey(objectPort), { port: objectPort, dot, row, view: record, label: objectPort.label });
                G.ui.put(header, row);
            }
            if (objectPort) G.ui.put(header, el('span.tg-node-dot', { text: '.' }));
            // How Set changes what it sets (=, +=) reads between the two: its choice is the name of its value's row.
            // An operator's own choice (Multiply, Less than) is its node's title.
            const titled = op && (shown.k === 'binary' || shown.k === 'unary');
            // One that adds or takes away one (++) has no value: its choice comes after what it changes
            const alone = op && shown.k === 'assign' && !inputs.some(p => p.key === 'value');
            const opRow = op && !titled ? rowInputs.filter(p => !p.group && p !== headIn).at(-1) : null;
            G.ui.put(header, primary && !objectPort && shown.k !== 'call' ? el('span.tg-node-title', { text: words }) : null,
                primary || (titled ? op : el('span.tg-node-title', { text: words })), opRow || titled ? null : op, tags, el('span.tg-grow'), moreButton);
            // The first output on the title's row keeps the node short: next for a statement, the value else
            const headOut = own?.exec ? execOuts.find(p => G.samePath(p.path, ['next'])) : outs.find(p => p.local === undefined);
            if (headOut) {
                const dot = dotOf(headOut);
                const row = el('span.tg-port.tg-out.tg-head-port' + (headOut.exec ? '.tg-port-exec' : ''), { dataset: { port: portKey(headOut) }, title: headOut.exec ? 'What happens next: drag from here' : 'Its value: drag from here into an input' }, dot);
                this.ports.set(portKey(headOut), { port: headOut, dot, row, view: record });
                G.ui.put(header, row);
            }
            G.ui.put(view, header);
            if (shown.comment || n.comment) G.ui.put(view, el('div.tg-node-comment', { text: n.comment || shown.comment, title: n.comment || shown.comment }));
            if (fields.length) G.ui.put(view, el('div.tg-node-fields', null, fields.map(f => el('div.tg-field-row', null, f))));
            const left = el('div.tg-col.tg-col-in'), right = el('div.tg-col.tg-col-out');
            // Another argument, item or field is added by a + at the end of the last one's row
            let group = null, last = null, lastRow = null;
            const addMore = () => {
                if (!last) return;
                const more = this.groupAdd(shown, last, true);
                if (more) G.ui.put(lastRow, more);
                last = null;
            };
            for (const p of rowInputs) {
                if (p === headIn) continue;
                if (p.group !== group) { addMore(); group = p.group; }
                const row = this.inputRow(p, record, p === opRow && !alone ? op : null, p === opRow && alone ? op : null);
                G.ui.put(left, row);
                if (p.group) { last = p; lastRow = row; }
            }
            addMore();
            for (const key of this.emptyGroups(shown, inputs)) G.ui.put(left, this.groupAdd(shown, { key, group: key }));
            for (const p of [...outs, ...execOuts]) {
                if (p === headOut) continue;
                G.ui.put(right, outRow(p));
            }
            if (left.childNodes.length || right.childNodes.length) G.ui.put(view, el('div.tg-node-ports', null, left, right));
            G.ui.put(this.nodesLayer, view);
            this.views.set(n.id, record);
        }

        // An input's row: its name (or `lead` in its place: an operator's choice), then what's in it. A number, a
        // tick or text is edited there; a name or a path is a chip; a wire from a node ends at its pin; nothing yet
        // is "pick…".
        inputRow(p, record, lead, trail) {
            const w = this.wireOf(p);
            const inline = !!w && this.analysis.folded.has(w);
            const dot = el('span.tg-dot' + (w && !inline ? '' : '.tg-dot-off'));
            const row = el('div.tg-port.tg-in' + (w && !inline ? '' : '.tg-inline'), { dataset: { port: portKey(p) } },
                dot, lead || (p.label ? el('span.tg-port-label', { text: p.label }) : null),
                !w ? this.pickButton(p) : inline ? this.inlineValue(p, w) : null, trail || null);
            this.ports.set(portKey(p), { port: p, dot, row, view: record, label: p.label });
            return row;
        }

        pickButton(p) {
            return button('pick…', e => this.pickValue(p, e.currentTarget), 'Nothing is in it yet: pick what goes here', 'tg-pick');
        }

        // Opens the value picker of an input, under `anchor`; `from` is the place in the path shown there to change
        // it from (0, or absent, for all of it)
        pickValue(p, anchor, { from = 0 } = {}) {
            return G.pickers.valuePicker(this.app, this, p, { anchor, from });
        }

        // A simple value where it's read: a number's, a tick's or a text's own control, or a chip with a name or a
        // path
        inlineValue(p, w) {
            const app = this.app;
            const n = this.analysis.byId.get(w);
            const fk = `${w}:in`;
            const other = () => button('▾', e => this.pickValue(p, e.currentTarget.parentElement), 'Pick something else for it', 'tg-row-pick');
            if (n.k === 'int' || n.k === 'float' || n.k === 'unary') {
                const inner = n.k === 'unary' ? this.analysis.byId.get(n.value) : n;
                return el('span.tg-inline-value', null, field((n.k === 'unary' ? '-' : '') + (inner?.text ?? ''), v => this.setNumber(p, w, v.trim()), {
                    className: 'tg-input.tg-inline-number', placeholder: '0', grow: 2, fk, title: 'A number',
                    check: v => (G.values.NUMBER.test(v.trim()) ? null : `"${v}" isn't a number`),
                }), other());
            }
            if (n.k === 'call') {
                // A vector's numbers, one for each of its arguments, or a colour's swatch
                const args = Array.isArray(n.args) ? n.args : [];
                const texts = args.map(a => this.doc.valueText(this.graph, a));
                const setArg = (i, text) => {
                    const body = text.replace(/^-/, '');
                    this.setInput({ node: n.id, input: true, exec: false, path: ['args', i] }, (doc, g, at) => doc.makeLiteral(g, G.values.INT.test(body) ? 'int' : 'float', text, at));
                };
                if (this.schema.control(n.name) === 'colour' && args.length >= 3) {
                    const swatch = el('input.tg-value-color', { type: 'color', value: G.values.toHex(texts), title: `${this.doc.valueText(this.graph, w)}: pick a colour`, onpointerdown: e => e.stopPropagation() });
                    swatch.dataset.fk = fk;
                    swatch.addEventListener('change', () => app.edit(doc => {
                        G.values.fromHex(swatch.value).forEach((t, i) => doc.setInput(this.graph, n.id, ['args', i], (d, g) => d.makeLiteral(g, 'float', t.includes('.') ? t : t + '.0', n)));
                    }));
                    return el('span.tg-inline-value', null, el('span.tg-port-type', { text: n.name }), swatch, other());
                }
                const names = args.length <= 4 && this.schema.control(n.name) === 'vector' ? G.values.AXES : [];
                return el('span.tg-inline-value.tg-inline-vector', { title: n.name }, args.map((a, i) => field(texts[i], v => setArg(i, v.trim()), {
                    className: 'tg-input.tg-inline-number.tg-inline-axis', placeholder: '0', grow: 1, fk: `${fk}.${i}`, title: names[i] ? `${n.name}: ${names[i]}` : n.name,
                    check: v => (G.values.NUMBER.test(v.trim()) ? null : `"${v}" isn't a number`),
                })), other());
            }
            if (n.k === 'bool') {
                const box = el('input', { type: 'checkbox', checked: !!n.value, onchange: e => app.edit(() => { n.value = e.target.checked; }), onpointerdown: e => e.stopPropagation() });
                box.dataset.fk = fk;
                return el('span.tg-inline-value', null, el('label.tg-value-bool', { onpointerdown: e => e.stopPropagation() }, box, el('span', { text: n.value ? 'yes' : 'no' })), other());
            }
            if (n.k === 'string') {
                return el('span.tg-inline-value', null, el('span.tg-inline-quote', { text: '“' }),
                    field(n.text ?? '', v => app.edit(() => { n.text = v; }), { className: 'tg-input.tg-inline-text', placeholder: 'text', grow: 3, fk, title: 'Text' }),
                    el('span.tg-inline-quote', { text: '”' }), other());
            }
            if (n.k === 'char') {
                return el('span.tg-inline-value', null, field(String(n.text ?? '').replace(/^'|'$/g, ''), v => app.edit(() => { n.text = `'${v}'`; }), {
                    className: 'tg-input.tg-inline-number', placeholder: 'a', grow: 1, fk, title: 'One character', check: v => ([...v].length === 1 || /^\\.+$/.test(v) ? null : 'One character'),
                }), other());
            }
            return el('span.tg-inline-value', null, this.pathChip(p, w));
        }

        // A name or a path as a chip in its type's colour: each part is clicked to change the path from there
        pathChip(p, w) {
            const g = this.graph;
            const doc = this.doc;
            const nodes = doc.pathNodes(g, w);
            const type = this.typeOfValue(w);
            const start = nodes[0];
            const read = this.readOf(start);
            const chip = el('span.tg-path' + (typeColor(type) ? '' : '.tg-path-plain'), { style: { '--tg-type': typeColor(type) || undefined }, dataset: read ? { read } : undefined, title: type ? `${doc.valueText(g, w)}: ${type}` : undefined },
                el('span.tg-path-dot', { style: { background: typeColor(type) || undefined } }));
            nodes.forEach((node, i) => {
                const text = i === 0 ? doc.valueText(g, node.id) : node.member;
                if (i > 0) G.ui.put(chip, el('span.tg-path-sep', { text: '.' }));
                G.ui.put(chip, button(text, e => this.pickValue(p, chip, { from: i }), i === 0 ? 'Pick something else for it' : `Pick another of what ${doc.valueText(g, nodes[i - 1].id)} has`, 'tg-path-part'));
            });
            return chip;
        }

        // What a path starts at, when it's something this code has: 'pin:<name>' for what it takes, 'local:<id>' for
        // a local, by the node that declares it
        readOf(start) {
            if (!start) return null;
            if (start.k === 'local') return `local:${start.ref}`;
            if (start.k === 'name' && !start.local && this.pins.some(pin => pin.name === start.name)) return `pin:${start.name}`;
            return null;
        }

        // A number typed into an input: the literal's text changes, or the literal is made again when it becomes
        // another kind (a whole number, one with a fraction, a negative one)
        setNumber(p, w, text) {
            const n = this.analysis.byId.get(w);
            const negative = text.startsWith('-');
            const body = negative ? text.slice(1) : text;
            const kind = G.values.INT.test(body) ? 'int' : 'float';
            const inner = n.k === 'unary' ? this.analysis.byId.get(n.value) : n;
            if ((n.k === 'unary') === negative && inner?.k === kind) this.app.edit(() => { inner.text = body; });
            else this.setInput(p, (doc, g, at) => doc.makeLiteral(g, kind, text, at));
        }

        // Puts a value into an input: `make(doc, graph, at)` makes its nodes where its reader is and gives the id of
        // the one whose value it is; null for nothing. What was there goes if it was simple.
        setInput(p, make) {
            let why = null, made = 0;
            this.app.edit(doc => {
                const g = doc.ensureGraph(this.slot);
                if (!g) return;
                const reader = p.node > 0 ? Doc.nodeIn(g, p.node) : null;
                const at = { x: Number(reader?.x) || 0, y: Number(reader?.y) || 0 };
                why = doc.setInput(g, p.node === RESULT ? 0 : p.node, p.node === RESULT ? null : p.path, make ? (d, graph) => (made = make(d, graph, at)) : null);
            });
            if (why) G.ui.toast(`That can't go there: ${why}.`);
            // A node it made (something worked out) is brought into view, or the node the input is on
            const f = this.value ? null : this.board.frameOf(this.slot);
            if (f) this.board.showNode(f, made && f.views.has(made) ? made : p.node);
            return why;
        }

        // The type an input takes, where it's known: a call's parameter's (tide/graphCalls), what an assignment
        // changes, the other side of an operator, a local's own, the code's result, yes or no for a condition
        expectedType(p) {
            const lang = this.app.lang;
            if (p.node === RESULT) return this.value?.type || null;
            const n = this.analysis.byId.get(p.node);
            if (!n) return null;
            if ((n.k === 'call' || n.k === 'method') && p.group === 'args') {
                const c = lang.callArgs(n.id);
                const t = c?.types[c.at(p.index)];
                if (t) return t;
            }
            if (n.k === 'assign' && p.key === 'value' && n.target) return this.typeOfValue(n.target);
            if (n.k === 'binary' && (p.key === 'lhs' || p.key === 'rhs')) {
                if (n.op === '&&' || n.op === '||') return 'bool';
                const other = p.key === 'lhs' ? n.rhs : n.lhs;
                if (other && this.typeOfValue(other)) return this.typeOfValue(other);
            }
            if (n.k === 'var' && p.key === 'value' && n.type) return n.type;
            if (n.k === 'return' && p.key === 'value') {
                const t = this.item?.accessor ? this.item.property?.type : this.item?.value.returnType;
                if (t && t !== 'void') return t;
            }
            if (p.key === 'cond') return 'bool';
            if (n.k === 'literal' && p.group === 'inits') {
                const name = n.inits?.[p.index]?.name;
                const field = this.doc.membersOf(n.type).find(m => m.name === name);
                if (field?.type) return field.type;
            }
            const w = this.wireOf(p);
            return w ? this.typeOfValue(w) : null;
        }

        // The type of a value by its node: what tidels found, or what the file says of a parameter or a local
        typeOfValue(id) {
            const lang = this.app.lang;
            const known = lang.typeOf(id);
            if (known && known !== 'nothing') return known;
            const n = this.analysis.byId.get(id);
            if (n?.k === 'name') return this.pins.find(pin => pin.name === n.name)?.type || null;
            if (n?.k === 'local') return this.typeOfPort({ local: n.ref });
            if (n?.k === 'int') return 'int';
            if (n?.k === 'float') return 'float';
            if (n?.k === 'bool') return 'bool';
            if (n?.k === 'string') return 'string';
            return null;
        }

        // The locals a node can read: those declared by a statement that runs before its own (or by a value of one)
        localsBefore(id) {
            const g = this.graph;
            const s = this.schema;
            const doc = this.doc;
            const all = g ? doc.locals(g) : [];
            if (!g || id <= 0) return all;
            const readers = new Map();
            for (const n of Doc.nodes(g)) for (const w of s.dataWires(n)) (readers.get(w) || readers.set(w, []).get(w)).push(n.id);
            // The statements a node is part of: itself, or those that read it, through any number of values
            const statements = from => {
                const out = new Set(), seen = new Set(), stack = [from];
                while (stack.length) {
                    const at = stack.pop();
                    if (seen.has(at)) continue;
                    seen.add(at);
                    const n = Doc.nodeIn(g, at);
                    if (n && s.isExec(n.k)) out.add(at);
                    stack.push(...(readers.get(at) || []));
                }
                return out;
            };
            const mine = statements(id);
            if (!mine.size) return all;
            return all.filter(d => {
                if (d.id === id) return false;
                for (const from of statements(d.id)) {
                    for (const to of mine) if (from !== to ? doc.goesTo(g, from, to) : !s.isExec(d.k)) return true;
                }
                return false;
            });
        }

        // The ports of a node as it's drawn: its schema's, named in the schema's and the game's words (`portInfo`, a
        // call's parameters by tide/graphCalls), a pin for the local it declares, and for a statement drawn as its
        // call, the call's inputs with the statement's own arrows and value
        portsOf(n, call) {
            const s = this.schema;
            let ports = s.ports(n);
            if (call) {
                ports = [...ports.filter(p => !(p.input && !p.exec)), ...s.ports(call).filter(p => p.input && !p.exec)];
                // A call holds code only when it's made to
                if (!n.withBlock && !n.block) ports = ports.filter(p => !(p.exec && !p.input && G.samePath(p.path, ['block'])));
                ports = ports.map(p => (!p.input && !p.exec ? { ...p, label: '' } : p));
            } else if (n.k === 'eval' && !n.withBlock && !n.block) {
                ports = ports.filter(p => !(p.exec && !p.input && G.samePath(p.path, ['block'])));
            } else if (n.k === 'assign' && ONE.includes(n.op) && !n.value) {
                // Adding or taking away one takes no value
                ports = ports.filter(p => !(p.input && !p.exec && p.key === 'value'));
            } else if (n.k === 'return' && !n.value && !this.value) {
                // Code that gives nothing back (a system, a function with no result) stops with no value
                const gives = this.item?.accessor ? this.item.value.word === 'get' : this.item?.value.returnType;
                if (!gives || gives === 'void') ports = ports.filter(p => !(p.input && !p.exec && p.key === 'value'));
            }
            for (const p of ports) {
                if (p.exec && !p.input) p.label = G.samePath(p.path, ['next']) ? '' : p.group ? p.label : s.portLabel(p.key);
                else if (p.input && !p.exec) p.label = inputLabel(this.app, call && p.node === call.id ? call : n, p);
            }
            const local = s.declaredLocal(n);
            if (local) ports.push({ node: n.id, input: false, exec: false, local: n.id, label: local });
            return ports;
        }

        // The list, items or named inputs of a node that have nothing in them yet: each still gets its way to add
        emptyGroups(n, inputs) {
            const spec = this.schema.node(n.k);
            const have = new Set(inputs.map(p => p.group).filter(Boolean));
            const out = [];
            for (const l of spec?.lists || []) if (!have.has(l.key)) out.push(l.key);
            for (const i of spec?.items || []) if (!have.has(i.key)) out.push(i.key);
            if (spec?.map && !have.has(spec.map)) out.push(spec.map);
            return out.filter(key => key !== 'values'); // Text with values grows by its text
        }

        // The small "+" after a node's list of inputs: another argument, case, field or named input
        // The way to add to a node's list, items or named inputs: a row of its own ("+ argument") while there are
        // none, `inline` a + at the end of the last one's row
        groupAdd(n, p, inline = false) {
            const s = this.schema;
            const app = this.app;
            const spec = s.node(n.k);
            const order = s.nodeOrder(n.k);
            const text = word => (inline ? '+' : '+ ' + word);
            const cls = inline ? 'tg-port-add.tg-port-add-inline' : 'tg-port-add';
            const list = spec.lists?.find(l => l.key === p.group);
            if (list) {
                if (n.k === 'interp') return null;
                // A call's next argument is called what its function calls it (tide/graphCalls), and there's none to
                // add once each parameter has its own
                const next = list.names ? app.lang.nextParam(n, Array.isArray(n[list.key]) ? n[list.key].length : 0) : null;
                if (next === '') return null;
                return button(text(next || list.label), () => app.edit(() => {
                    if (!Array.isArray(n[list.key])) set(n, list.key, [], order);
                    n[list.key].push(null);
                    if (Array.isArray(n[list.names])) n[list.names].push('');
                }), next ? `Give it its ${next}` : `Another ${list.label}`, cls);
            }
            const item = spec.items?.find(i => i.key === p.group);
            if (item) {
                const iorder = s.itemOrder(n.k, item.key);
                const make = name => app.edit(d => {
                    if (!Array.isArray(n[item.key])) set(n, item.key, [], order);
                    const blank = {};
                    if (name !== undefined) set(blank, item.label, name, iorder);
                    for (const w of item.wires || []) {
                        if (w.endsWith('[]')) set(blank, w.slice(0, -2), [null], iorder);
                        else set(blank, w, null, iorder);
                    }
                    n[item.key].push(blank);
                });
                const named = n.k === 'literal';
                return button(text(named ? 'field' : item.label), e => {
                    if (!named) { make(); return; }
                    // A field of its type, by name: those the file knows, or a name
                    const known = this.doc.membersOf(n.type).filter(m => !m.method && !(n.inits || []).some(i => i.name === m.name));
                    G.ui.picker({
                        anchor: e.currentTarget, placeholder: 'Which field?', entries: known.map(m => ({ title: m.name, detail: m.type, color: typeColor(m.type) })),
                        free: text => (/^[A-Za-z_][A-Za-z0-9_]*$/.test(text) && !known.some(m => m.name === text) ? { title: text, detail: 'a field by this name' } : null),
                        empty: 'Type the field\'s name', onPick: entry => make(entry.title),
                    });
                }, named ? 'Set another of its fields' : `Another ${item.label}`, cls);
            }
            if (spec.map === p.group) {
                return button(text('input'), () => app.edit(() => {
                    if (!n[spec.map] || typeof n[spec.map] !== 'object') set(n, spec.map, {}, order);
                    const names = Object.keys(n[spec.map]);
                    let name = 'a';
                    for (let c = 97; names.includes(name); c++) name = String.fromCharCode(c);
                    n[spec.map][name] = null;
                }), 'Another value the text reads by its name', cls);
            }
            return null;
        }

        // "Text with values": its text with {} where each value goes, as one field
        templateField(n, order) {
            const parts = Array.isArray(n.parts) ? n.parts : [''];
            const formats = Array.isArray(n.formats) ? n.formats : [];
            let text = '';
            parts.forEach((p, i) => {
                text += p;
                if (i < parts.length - 1) text += '{' + (formats[i] ? ':' + formats[i] : '') + '}';
            });
            return field(text, v => this.app.edit(() => {
                const nextParts = [], nextFormats = [];
                let rest = v;
                for (;;) {
                    const m = /\{(?::([^{}]*))?\}/.exec(rest);
                    if (!m) break;
                    nextParts.push(rest.slice(0, m.index));
                    nextFormats.push(m[1] || '');
                    rest = rest.slice(m.index + m[0].length);
                }
                nextParts.push(rest);
                const values = Array.isArray(n.values) ? n.values.slice(0, nextFormats.length) : [];
                while (values.length < nextFormats.length) values.push(null);
                set(n, 'parts', nextParts, order);
                set(n, 'formats', nextFormats.some(f => f) ? nextFormats : undefined, order);
                set(n, 'values', values, order);
            }), { className: 'tg-input.tg-node-input.tg-node-template', placeholder: 'Score: {}', title: 'Its text, with {} where each value goes ({:F2} to format one)', fk: `${n.id}:parts`, grow: 10 });
        }

        lambdaParams(n) {
            const s = this.schema;
            const app = this.app;
            const order = s.element('lambdaParam');
            const list = Array.isArray(n.params) ? n.params : [];
            return el('span.tg-lambda-params', null,
                list.map((p, i) => el('span.tg-lambda-param', null,
                    field(p.name ?? '', v => app.edit(() => set(p, 'name', v, order)), { className: 'tg-input.tg-node-input', placeholder: 'name', check: G.cards.checkName, grow: 2 }),
                    button('×', () => app.edit(() => { list.splice(i, 1); }), 'Remove this parameter', 'tg-icon'))),
                button('+ parameter', () => app.edit(d => {
                    if (!Array.isArray(n.params)) set(n, 'params', [], s.nodeOrder('lambda'));
                    n.params.push({ name: d.uniqueName(n.params, 'x') });
                }), 'Something the lambda takes', 'tg-port-add'));
        }

        // What can be done to a node
        nodeMenu(n, x, y) {
            const app = this.app;
            const s = this.schema;
            const ed = this.editor;
            const call = this.analysis.merged.has(n.id) ? this.analysis.byId.get(this.analysis.merged.get(n.id)) : null;
            const shown = call || n;
            if (!ed.selection.has(n.id)) ed.selectNodes(this.slot, [n.id]);
            const items = [];
            const flags = [];
            for (const target of call ? [call, n] : [n]) {
                const spec = s.node(target.k);
                const order = s.nodeOrder(target.k);
                for (const key of spec?.keys || []) {
                    if (s.keyType(key, target) !== 'flag' || (target.k === 'bool' && key === 'value')) continue;
                    flags.push({ label: s.flagLabel(key), summary: s.flagHelp(key), group: s.flagAdvanced(key) ? 'More' : undefined, checked: !!target[key], stay: true, run: on => app.edit(() => set(target, key, on ? true : undefined, order)) });
                }
                if (spec?.keys.includes('typeArgs')) {
                    items.push({ label: 'Add a type argument…', summary: 'For a generic call: Name<T>', run: () => G.pickers.typePicker(app, { x, y, title: 'Type argument',
                        onPick: t => app.edit(() => { if (!Array.isArray(target.typeArgs)) set(target, 'typeArgs', [], order); target.typeArgs.push(t); }) }) });
                }
            }
            if ((shown.k === 'member' || shown.k === 'method') && shown.object) {
                items.unshift({ label: shown.k === 'method' ? 'Call another of its methods…' : 'Pick another of its fields…', summary: 'Of the same value', run: () => G.pickers.nodeSearch(app, this, x, y, { member: shown }) });
            }
            flags.sort((f1, f2) => (f1.group ? 1 : 0) - (f2.group ? 1 : 0));
            items.push(...flags);
            const order = s.nodeOrder(n.k);
            items.push({ label: `${s.keyLabel('comment')}…`, summary: n.comment || s.keyHelp('comment'), run: () => G.cards.editNote(app, n, order, { x, y }) });
            items.push({ separator: true },
                { label: 'Copy', hint: 'Ctrl+C', run: () => app.command('copy') },
                { label: 'Cut', hint: 'Ctrl+X', run: () => app.command('cut') },
                { label: 'Duplicate', hint: 'Ctrl+D', run: () => app.command('duplicate') },
                { label: 'Delete', hint: 'Del', danger: true, run: () => app.command('delete') });
            menu(x, y, items, { title: s.title(shown.k) });
        }

        // What can be done to a port: take its wire away, name or remove an argument, a case's labels
        portMenu(port, x, y) {
            const app = this.app;
            const s = this.schema;
            const g = this.graph;
            const n = port.node > 0 ? Doc.nodeIn(g, port.node) : null;
            const items = [];
            const wired = port.input && this.wireOf(port);
            if (port.input && !port.exec) items.push({ label: wired ? 'Pick something else…' : 'Pick what goes here…', run: () => this.pickValue(port, this.ports.get(portKey(port))?.row) });
            if (wired) items.push({ label: this.analysis.folded.has(wired) ? 'Empty it' : 'Remove the wire', run: () => this.unwireNow(port) });
            if (n && port.group && port.path) {
                const spec = s.node(n.k);
                const order = s.nodeOrder(n.k);
                const list = spec.lists?.find(l => l.key === port.group);
                if (list) {
                    const i = port.index;
                    if (list.names) {
                        items.push({ label: 'Name this argument…', summary: 'As in crit: 2', run: () => G.cards.promptText({ x, y, title: 'The argument\'s name', value: n[list.names]?.[i] ?? '', placeholder: 'left unnamed',
                            check: v => (v === '' ? null : G.cards.checkName(v)),
                            onDone: text => app.edit(() => {
                                const names = (n[list.key] || []).map((_, j) => (j === i ? text : n[list.names]?.[j] ?? ''));
                                set(n, list.names, names.some(x2 => x2) ? names : undefined, order);
                            }) }) });
                    }
                    if (n.k !== 'interp') {
                        items.push({ label: `Remove this ${list.label}`, danger: true, run: () => this.editWithSweep(() => {
                            n[list.key].splice(i, 1);
                            if (Array.isArray(n[list.names])) { n[list.names].splice(i, 1); if (!n[list.names].some(x2 => x2)) delete n[list.names]; }
                        }, [s.wire(n, port.path)]) });
                    }
                }
                const item = spec.items?.find(it => it.key === port.group);
                if (item) {
                    const o = n[item.key]?.[port.index];
                    for (const w of item.wires || []) {
                        if (!w.endsWith('[]') || !o) continue;
                        const key = w.slice(0, -2);
                        items.push({ label: 'Another value for this case', run: () => app.edit(() => { if (!Array.isArray(o[key])) o[key] = []; o[key].push(null); }) });
                        if ((o[key] || []).length > 1) items.push({ label: 'One value less', run: () => this.editWithSweep(() => { o[key].pop(); }, []) });
                    }
                    items.push({ label: `Remove this ${n.k === 'literal' ? 'field' : item.label}`, danger: true, run: () => this.editWithSweep(() => { n[item.key].splice(port.index, 1); }, s.dataWires(n)) });
                }
                if (spec.map === port.group) {
                    const name = port.path[1];
                    items.push({ label: 'Rename this input…', run: () => G.cards.promptText({ x, y, title: 'The input\'s name', value: name, check: G.cards.checkName, onDone: text => app.edit(() => {
                        const map = n[spec.map];
                        if (!text || text === name || text in map) return;
                        const entries = Object.entries(map).map(([k, v]) => [k === name ? text : k, v]);
                        for (const k of Object.keys(map)) delete map[k];
                        for (const [k, v] of entries) map[k] = v;
                    }) }) });
                    items.push({ label: 'Remove this input', danger: true, run: () => this.editWithSweep(() => { delete n[spec.map][name]; }, [s.wire(n, port.path)]) });
                }
            }
            if (!items.length) return false;
            menu(x, y, items, { title: port.label || undefined });
            return true;
        }

        // An edit that takes inputs away: the simple values that were in them go with them
        editWithSweep(change, sources) {
            const g = this.graph;
            this.app.edit(doc => {
                change(doc);
                doc.reindex();
                for (const id of sources.filter(Boolean)) doc.dropValue(g, id);
            });
        }

        // ---------------------------------------------------------------------------------------------------------
        // Wires

        // Where each port's dot is, from its node's corner, and the body's size around what's drawn
        measure() {
            const z = this.board.zoom || 1;
            const origin = this.body.getBoundingClientRect();
            let right = 0, bottom = 0;
            for (const v of this.views.values()) {
                const r = v.el.getBoundingClientRect();
                v.w = r.width / z;
                v.h = r.height / z;
                v.left = (r.left - origin.left) / z;
                v.top = (r.top - origin.top) / z;
                right = Math.max(right, v.left + v.w);
                bottom = Math.max(bottom, v.top + v.h);
            }
            for (const p of this.ports.values()) {
                const r = p.dot.getBoundingClientRect();
                p.rel = { x: (r.left + r.width / 2 - origin.left) / z - p.view.left, y: (r.top + r.height / 2 - origin.top) / z - p.view.top };
            }
            // It grows around its nodes
            const w = Math.max(this.isValue ? 300 : MIN_W, right + BOTTOM), h = Math.max(this.isValue ? 130 : MIN_H, bottom + BOTTOM);
            if (w !== this.size.w || h !== this.size.h) {
                this.size = { w, h };
                this.el.style.width = w + 'px';
                this.body.style.height = h + 'px';
            }
        }

        // A port's place in the body, or null
        at(key) {
            const p = this.ports.get(key);
            if (!p?.rel) return null;
            const v = p.view;
            const moved = this.places.get(v.node.id);
            const left = moved ? this.nodeX + moved.x - this.origin.x : v.left;
            const top = moved ? PAD + moved.y - this.origin.y : v.top;
            return { x: left + p.rel.x, y: top + p.rel.y };
        }

        // The key of the port a node's value comes from
        sourceKey(id) {
            return portKey({ node: id, input: false, exec: false, path: null });
        }

        // Whether an input has a wire from a node (not nothing, and not a value shown in it)
        hasNodeWire(port) {
            const w = this.wireOf(port);
            return w > 0 && !this.analysis.folded.has(w);
        }

        // The id an input's wire comes from, or 0
        wireOf(port) {
            const g = this.graph;
            if (!g) return 0;
            if (port.node === RESULT) return g.root || 0;
            if (port.exec && port.input) return this.doc.execSourceOf(g, port.node)?.node || (g.entry === port.node ? ENTRY : 0);
            const n = Doc.nodeIn(g, port.node);
            return n && port.path ? this.schema.wire(n, port.path) : 0;
        }

        drawWires() {
            const g = this.graph;
            if (this.mode !== 'near' || !this.wires) return;
            this.wires.replaceChildren();
            if (!g) return;
            const s = this.schema;
            const types = this.app.lang;
            const execIn = id => portKey({ node: this.analysis.mergedInto.get(id) ?? id, input: true, exec: true, path: null });
            // The wires into the selected nodes stand out
            const ed = this.editor;
            this.lit = new Set();
            if (G.slotKey(ed.slot) === this.key) {
                for (const id of ed.selection) {
                    this.lit.add(id);
                    if (this.analysis.merged.has(id)) this.lit.add(this.analysis.merged.get(id));
                }
            }
            this.litKey = [...ed.selection].join();
            const wiredOut = new Set();
            for (const n of Doc.nodes(g)) {
                const merged = this.analysis.merged.get(n.id);
                for (const p of s.ports(n)) {
                    if (!p.path) continue;
                    const from = s.wire(n, p.path);
                    if (!from) continue;
                    if (p.exec) {
                        const a = this.at(portKey(p)), b = this.at(execIn(from));
                        if (a && b) this.path(a, b, 'tg-wire-exec', null, { node: n.id, path: p.path });
                    } else {
                        if (merged === from && G.samePath(p.path, ['value'])) continue; // A statement and its call are one node
                        if (this.analysis.folded.has(from)) continue; // Shown in the input: no wire
                        const key = this.sourceKey(from);
                        wiredOut.add(key);
                        const a = this.at(key), b = this.at(portKey(p));
                        if (a && b) this.path(a, b, 'tg-wire-data', typeColor(this.typeOfValue(from)) || s.color(Doc.nodeIn(g, from)?.k), { node: n.id, path: p.path });
                    }
                }
            }
            if (!this.isValue && g.entry) {
                const a = this.at(portKey({ node: ENTRY, input: false, exec: true, path: null })), b = this.at(execIn(g.entry));
                if (a && b) this.path(a, b, 'tg-wire-exec', null, { node: ENTRY, path: null });
            }
            if (this.isValue && g.root && !this.analysis.folded.has(g.root)) {
                const key = this.sourceKey(g.root);
                wiredOut.add(key);
                const a = this.at(key), b = this.at(portKey({ node: RESULT, input: true, exec: false, path: null }));
                if (a && b) this.path(a, b, 'tg-wire-data', typeColor(types.typeOf(g.root)) || s.color(Doc.nodeIn(g, g.root)?.k), { node: RESULT, path: null });
            }
            // The colours of types on the pins, and which have a wire
            for (const [key, p] of this.ports) {
                if (p.port.exec) continue;
                let type, wired;
                if (p.port.input) {
                    const from = this.wireOf(p.port);
                    type = from ? this.typeOfValue(from) : null;
                    wired = !!from && !this.analysis.folded.has(from);
                } else {
                    type = this.typeOfPort(p.port);
                    wired = wiredOut.has(key);
                }
                p.dot.style.background = typeColor(type) || '';
                p.dot.style.boxShadow = type ? `0 0 0 1px ${typeColor(type)}` : '';
                p.dot.classList.toggle('tg-wired', wired);
                if ((!p.row.title || p.row.dataset.typed) && !p.row.dataset.problem) {
                    const wants = p.port.input && !type ? this.expectedType(p.port) : null;
                    p.row.title = type ? `${p.port.label || 'value'}: ${type}` : wants ? `${p.port.label || 'value'}: takes a ${wants}` : '';
                    p.row.dataset.typed = '1';
                }
            }
            const d = this.board.drag;
            if (d?.kind === 'wire' && d.frame === this && d.to) this.path(d.a, d.to, d.exec ? 'tg-wire-exec.tg-wire-new' : 'tg-wire-data.tg-wire-new', null, null);
        }

        path(a, b, cls, color, wire) {
            const dx = Math.max(40, Math.abs(b.x - a.x) / 2);
            // A long wire (a value worked out far from what reads it) is drawn faint, so the nodes stay what's read first
            const long = Math.abs(b.x - a.x) + Math.abs(b.y - a.y) > 900 && !cls.includes('tg-wire-new');
            const on = wire && wire.node > 0 && this.lit?.has(wire.node);
            const p = svg('path', { d: `M ${a.x} ${a.y} C ${a.x + dx} ${a.y}, ${b.x - dx} ${b.y}, ${b.x} ${b.y}`, class: cls.replace(/\./g, ' ') + (on ? ' tg-wire-on' : long ? ' tg-wire-long' : '') });
            if (color) p.style.stroke = color;
            if (wire) p.dataset.wire = JSON.stringify(wire);
            G.ui.put(this.wires, p);
            return p;
        }

        // The type of what a pin or an output gives, as tidels has it or as the file says
        typeOfPort(port) {
            const lang = this.app.lang;
            if (port.pin !== undefined) {
                const pin = this.pins.find(p => p.name === port.pin);
                if (pin?.type) return pin.type;
                // (A query's rows, a changed field: what tidels found for a read of it)
                for (const n of this.analysis.byId.values()) if (n.k === 'name' && n.name === port.pin && lang.typeOf(n.id)) return lang.typeOf(n.id);
                return null;
            }
            if (port.local !== undefined) {
                const n = this.analysis.byId.get(port.local);
                if (n?.type) return n.type;
                for (const r of this.analysis.byId.values()) if (r.k === 'local' && r.ref === port.local && lang.typeOf(r.id)) return lang.typeOf(r.id);
                return n?.value ? lang.typeOf(n.value) : null;
            }
            const merged = this.analysis.merged.get(port.node);
            return lang.typeOf(port.node) || (merged ? lang.typeOf(merged) : null);
        }

        // The node tidels is asked the members of, for a wire from a node's value: 0 for a pin's, whose members are
        // its type's (tide/graphMembers by type)
        nodeForMembers(port) {
            if (port.pin !== undefined || port.local !== undefined) return 0;
            return this.analysis.merged.get(port.node) ?? (port.node > 0 ? port.node : 0);
        }

        statementOf(id) {
            return this.analysis.mergedInto.get(id) ?? id;
        }

        // Makes the node a wire from a port stands for, when it's a pin's: a read of the parameter or of the local.
        // Returns the id of the node the value comes from.
        sourceNode(doc, port, near) {
            const g = this.graph;
            if (port.pin !== undefined) return doc.addNode(g, 'name', near.x - 160, near.y, { name: port.pin }).id;
            if (port.local !== undefined) return doc.addNode(g, 'local', near.x - 160, near.y, { ref: port.local }).id;
            return port.node;
        }

        // Whether a wire from port `a` can end on port `b`: an output to an input, exec to exec, data to data
        canJoin(a, b) {
            if (!a || !b || a.input === b.input || a.exec !== b.exec) return false;
            const va = this.ports.get(portKey(a))?.view, vb = this.ports.get(portKey(b))?.view;
            return !va || !vb || va !== vb;
        }

        // Wires two ports, whichever end the drag began at. Returns why it can't be, or null.
        join(doc, a, b) {
            const out = a.input ? b : a, inp = a.input ? a : b;
            const g = this.graph;
            if (out.exec) {
                if (out.node === ENTRY) return doc.setEntry(g, inp.node);
                return doc.connect(g, out.node, out.path, inp.node);
            }
            const target = inp.node === RESULT ? null : Doc.nodeIn(g, inp.node);
            const old = target ? this.schema.wire(target, inp.path) : g.root;
            const made = out.pin !== undefined || out.local !== undefined;
            const source = this.sourceNode(doc, out, target ? { x: Number(target.x) || 0, y: Number(target.y) || 0 } : { x: 0, y: 0 });
            const why = inp.node === RESULT ? doc.setRoot(g, source) : doc.connect(g, inp.node, inp.path, source);
            doc.reindex();
            if (why && made) doc.deleteNodes(g, [source]);
            else if (!why && old && old !== source) doc.dropValue(g, old);
            return why;
        }

        // Takes the wire of an input away, now
        unwireNow(port) {
            const g = this.graph;
            const old = this.wireOf(port);
            this.app.edit(doc => {
                this.unwire(doc, port);
                doc.reindex();
                if (old > 0 && !port.exec) doc.dropValue(g, old);
            });
        }

        unwire(doc, port) {
            const g = this.graph;
            if (port.exec && port.input) {
                const src = doc.execSourceOf(g, port.node);
                if (src) doc.disconnect(g, src.node, src.path);
                else if (g.entry === port.node) doc.setEntry(g, null);
            } else if (port.node === RESULT) {
                doc.setRoot(g, null);
            } else if (port.exec && port.node === ENTRY) {
                doc.setEntry(g, null);
            } else {
                doc.disconnect(g, port.node, port.path);
            }
        }

        showSelection() {
            const ed = this.editor;
            const mine = G.slotKey(ed.slot) === this.key;
            for (const [id, v] of this.views) v.el.classList.toggle('tg-selected', mine && ed.selection.has(id));
            if ([...ed.selection].join() !== this.litKey && !this.board.drag) this.drawWires();
        }

        // The element an id of the file is drawn as in this frame: a node's, a parameter's row; for a value shown
        // in an input, that input's row
        elementOf(id) {
            const direct = this.views.get(id)?.el;
            if (direct) return direct;
            const at = this.analysis.shown.get(id);
            if (at) return this.ports.get(portKey({ node: at.node, input: true, exec: false, path: at.path }))?.row || null;
            for (const v of this.views.values()) if (v.ids?.includes(id)) return v.el;
            return this.body.querySelector(`.tg-entry-row[data-id="${id}"]`);
        }
    }

    G.FrameView = FrameView;
    G.frames = { ENTRY, RESULT, HEAD, PAD, BOTTOM, ENTRY_W, NODE_X0, portKey, estimate, inputLabel };
})(globalThis.TideGraph = globalThis.TideGraph || {});
