// The canvas: the shown graph's nodes, with their exec and data ports, the bezier wires between them, note boxes
// behind them, and what's done to them with the pointer: select, move, wire, unwire, pan and zoom.
(function (G) {
    'use strict';
    const { el, svg, field, checkbox, select, menu, tooltip, typeColor } = G.ui;
    const { Doc } = G;

    const ENTRY = -1;  // The body's start, whose exec output is its first statement
    const RESULT = -2; // A value graph's result, whose input is its value

    function portKey(p) {
        return `${p.node}|${p.input ? 'i' : 'o'}|${p.exec ? 'x' : 'd'}|${p.path ? p.path.join('.') : ''}`;
    }

    class Canvas {
        constructor(app) {
            this.app = app;
            this.el = el('div.tg-canvas', { tabIndex: 0 });
            this.world = el('div.tg-world');
            this.wires = svg('svg', { class: 'tg-wires' });
            this.notesLayer = el('div.tg-notes');
            this.nodesLayer = el('div.tg-nodes');
            this.world.append(this.notesLayer, this.wires, this.nodesLayer);
            this.band = el('div.tg-band');
            this.empty = el('div.tg-empty');
            this.el.append(this.world, this.band, this.empty);
            this.pan = { x: 80, y: 80 };
            this.zoom = 1;
            this.views = new Map();   // id -> { el, node, ports: Map key -> { port, dot, rel } }
            this.noteViews = new Map();
            this.places = new Map();  // Where moved nodes are while they move
            this.viewOf = new Map();  // A graph's pan and zoom, by slot, while the editor is open
            this.drag = null;
            this.hoverNode = 0;
            this.lastPointer = { x: 0, y: 0 };
            this.listen();
        }

        get editor() { return this.app.editor; }
        get schema() { return this.app.editor.schema; }
        get graph() { return this.app.editor.graph; }

        // ---------------------------------------------------------------------------------------------------------
        // Drawing

        render() {
            const g = this.graph;
            const key = G.slotKey(this.editor.slot);
            if (key !== this.shownSlot) {
                if (this.shownSlot) this.viewOf.set(this.shownSlot, { pan: { ...this.pan }, zoom: this.zoom });
                this.shownSlot = key;
                const kept = this.viewOf.get(key);
                if (kept) {
                    this.pan = { ...kept.pan };
                    this.zoom = kept.zoom;
                } else if (g) {
                    this.framed = false;
                }
            }
            this.nodesLayer.replaceChildren();
            this.notesLayer.replaceChildren();
            this.views.clear();
            this.noteViews.clear();
            this.empty.replaceChildren();
            if (!g) {
                this.wires.replaceChildren();
                this.empty.append(el('div', { text: this.editor.error ? '' : 'Select a declaration with code on the left, or add one with +.' }));
                this.empty.style.display = 'flex';
                return;
            }
            this.empty.style.display = 'none';
            const reached = this.editor.doc.reached(g);
            for (const note of g.notes || []) this.renderNote(note);
            for (const n of Doc.nodes(g)) this.renderNode(n, reached.has(n.id));
            this.renderPseudo(g);
            this.measure();
            if (!this.framed) {
                this.framed = true;
                this.frame(false);
            }
            this.applyTransform();
            this.drawWires();
            this.showSelection();
        }

        // The node's DOM: a header with its kind's colour and title, its fields, then its ports in two columns
        renderNode(n, reached) {
            const s = this.schema;
            const spec = s.node(n.k);
            const app = this.app;
            const diagnostics = app.lang.diagnosticsOf(n.id);
            const errors = diagnostics.filter(d => d.severity === 1);
            const view = el('div.tg-node' + (reached ? '' : '.tg-dim') + (spec?.exec ? '.tg-exec' : '.tg-value')
                + (errors.length ? '.tg-has-error' : diagnostics.length ? '.tg-has-warning' : '') + (spec ? '' : '.tg-unknown'),
                { dataset: { id: n.id }, style: { left: this.x(n) + 'px', top: this.y(n) + 'px', '--tg-cat': s.color(n.k) } });
            const header = el('div.tg-node-header', null,
                el('span.tg-node-title', { text: this.titleOf(n) }),
                diagnostics.length ? el('span.tg-badge', { text: errors.length ? '!' : '?', title: diagnostics.map(d => d.message).join('\n') }) : null);
            view.append(header);
            if (n.comment) view.append(el('div.tg-node-comment', { text: n.comment, title: n.comment }));
            const fields = this.fieldsOf(n);
            const ports = s.ports(n);
            const left = el('div.tg-col.tg-col-in'), right = el('div.tg-col.tg-col-out');
            const record = { el: view, node: n, ports: new Map() };
            const dotOf = p => el('span.tg-dot' + (p.exec ? '.tg-dot-exec' : ''));
            // A value's output goes on its first field's row, which keeps the node short
            const out = spec?.value && !spec.exec && fields.length ? ports.find(p => !p.input && !p.exec) : null;
            if (fields.length) {
                const box = el('div.tg-node-fields');
                fields.forEach((f, i) => {
                    if (i === 0 && out) {
                        const dot = dotOf(out);
                        const row = el('div.tg-field-row.tg-port.tg-out', { dataset: { port: portKey(out) } }, f, dot);
                        record.ports.set(portKey(out), { port: out, dot, row });
                        box.append(row);
                    } else {
                        box.append(el('div.tg-field-row', null, f));
                    }
                });
                view.append(box);
            }
            for (const p of ports) {
                if (p === out) continue;
                const dot = dotOf(p);
                const row = el('div.tg-port' + (p.input ? '.tg-in' : '.tg-out') + (p.exec ? '.tg-port-exec' : ''),
                    { dataset: { port: portKey(p) } },
                    p.input ? dot : null, p.label ? el('span.tg-port-label', { text: p.label }) : null, p.input ? null : dot);
                (p.input ? left : right).append(row);
                record.ports.set(portKey(p), { port: p, dot, row });
            }
            if (left.childNodes.length || right.childNodes.length) view.append(el('div.tg-node-ports', null, left, right));
            this.nodesLayer.append(view);
            this.views.set(n.id, record);
        }

        // Start and Result: where a body runs from and where a value graph's value goes
        renderPseudo(g) {
            const nodes = Doc.nodes(g);
            const body = Doc.isBody(g);
            const id = body ? ENTRY : RESULT;
            const target = Doc.nodeIn(g, body ? g.entry : g.root);
            let x, y;
            if (target) {
                x = body ? this.x(target) - 170 : this.x(target) + 240;
                y = this.y(target);
            } else if (nodes.length) {
                x = Math.min(...nodes.map(n => this.x(n))) - (body ? 200 : -200);
                y = Math.min(...nodes.map(n => this.y(n)));
            } else {
                x = 0;
                y = 0;
            }
            const owner = this.editor.doc.find(this.editor.slot.owner);
            const p = { node: id, input: !body, exec: body, path: null, label: body ? 'start' : 'value' };
            const dot = el('span.tg-dot' + (body ? '.tg-dot-exec' : ''));
            const view = el('div.tg-node.tg-pseudo', { dataset: { id }, style: { left: x + 'px', top: y + 'px' } },
                el('div.tg-node-header', null, el('span.tg-node-title', { text: body ? 'Start' : 'Result' })),
                el('div.tg-node-fields', null, el('div.tg-pseudo-name', { text: this.editor.doc.describe(owner) + (this.editor.slot.key !== 'body' ? ` (${this.editor.slot.key})` : '') })),
                el('div.tg-node-ports', null,
                    el('div.tg-col.tg-col-in', null, body ? null : el('div.tg-port.tg-in', { dataset: { port: portKey(p) } }, dot, el('span.tg-port-label', { text: 'value' }))),
                    el('div.tg-col.tg-col-out', null, body ? el('div.tg-port.tg-out.tg-port-exec', { dataset: { port: portKey(p) } }, el('span.tg-port-label', { text: 'start' }), dot) : null)));
            this.nodesLayer.append(view);
            const record = { el: view, node: { id, x, y }, ports: new Map([[portKey(p), { port: p, dot }]]), pseudo: true };
            this.views.set(id, record);
        }

        renderNote(note) {
            const text = el('div.tg-note-text', { text: note.text || '' });
            const view = el('div.tg-note', {
                dataset: { note: note.id },
                style: { left: (this.places.get(note.id)?.x ?? note.x ?? 0) + 'px', top: (this.places.get(note.id)?.y ?? note.y ?? 0) + 'px', width: (note.w || 200) + 'px', height: (note.h || 120) + 'px' },
            }, el('div.tg-note-bar'), text, el('div.tg-note-size'));
            text.addEventListener('dblclick', e => {
                e.stopPropagation();
                const input = field(note.text || '', value => this.app.edit(() => { note.text = value; }), { multiline: true, className: 'tg-note-edit', rows: 4 });
                text.replaceWith(input);
                input.focus();
                input.addEventListener('blur', () => this.render());
            });
            this.notesLayer.append(view);
            this.noteViews.set(note.id, { el: view, note });
        }

        // A node's title: its kind's, with what it is when that's not a field of its (a local's read, a call's
        // type arguments)
        titleOf(n) {
            const s = this.schema;
            if (n.k === 'local') {
                const decl = Doc.nodeIn(this.graph, n.ref);
                return 'Local ' + (decl ? s.declaredLocal(decl) ?? '?' : `? (${n.ref})`);
            }
            if ((n.k === 'call' || n.k === 'method') && n.typeArgs?.length) return `${s.title(n.k)} <${n.typeArgs.join(', ')}>`;
            return s.title(n.k);
        }

        // A node's own values, edited where it is: its keys that are text, types and flags. Lists and items are
        // the inspector's.
        fieldsOf(n) {
            const s = this.schema;
            const spec = s.node(n.k);
            if (!spec) return [el('div.tg-node-unknown', { text: `'${n.k}' isn't a kind of node this editor knows` })];
            const out = [];
            const edit = (key, value) => this.app.edit(() => G.json.set(n, key, value, s.nodeOrder(n.k)));
            for (const key of spec.keys) {
                const type = s.keyType(key, n);
                if (n.k === 'local' && key === 'ref') continue; // Its title says which; the inspector changes it
                if (key === 'op') {
                    const ops = s.ops(n) || [];
                    out.push(select(n.op ?? '', ops.map(o => [o, n.k === 'binary' || n.k === 'unary' ? `${o}   ${s.opTitle(o)}` : o]), v => edit('op', v), 'Operator'));
                    continue;
                }
                if (type === 'flag') {
                    if (Object.values(spec.absentWhen || {}).includes(key)) continue; // The inspector's: it takes an input away
                    // A word that's off is the inspector's to turn on; on, it shows (a bool's value always does)
                    if (!n[key] && !(n.k === 'bool' && key === 'value')) continue;
                    out.push(checkbox(n[key], v => edit(key, n.k === 'bool' && key === 'value' ? v : v ? true : undefined), n.k === 'bool' ? 'true' : key, key));
                    continue;
                }
                if (type === 'string' || type === 'type') {
                    if (key === 'text' && (n.k === 'text' || n.k === 'code')) continue;
                    const placeholder = key;
                    out.push(field(n[key], v => edit(key, v === '' && key !== 'text' ? undefined : v), {
                        placeholder, list: type === 'type' ? 'tg-types' : undefined, className: 'tg-input.tg-node-input', title: key,
                    }));
                    continue;
                }
                if (type === 'text') {
                    out.push(field(n.text, v => edit('text', v), { multiline: n.k === 'text' || n.k === 'code', className: 'tg-input.tg-node-input.tg-mono', placeholder: 'text', rows: 2 }));
                }
            }
            return out;
        }

        x(n) { return this.places.get(n.id)?.x ?? Number(n.x) ?? 0; }
        y(n) { return this.places.get(n.id)?.y ?? Number(n.y) ?? 0; }

        // Where each port's dot is, from its node's corner
        measure() {
            for (const v of this.views.values()) {
                const origin = v.el.getBoundingClientRect();
                for (const p of v.ports.values()) {
                    const r = p.dot.getBoundingClientRect();
                    p.rel = { x: (r.left + r.width / 2 - origin.left) / this.zoom, y: (r.top + r.height / 2 - origin.top) / this.zoom };
                }
                v.w = origin.width / this.zoom;
                v.h = origin.height / this.zoom;
            }
        }

        // A port's place in the world, or null
        at(id, key) {
            const v = this.views.get(id);
            const p = v?.ports.get(key);
            if (!p?.rel) return null;
            const x = v.pseudo ? v.node.x : this.x(v.node), y = v.pseudo ? v.node.y : this.y(v.node);
            return { x: x + p.rel.x, y: y + p.rel.y };
        }

        drawWires() {
            const g = this.graph;
            this.wires.replaceChildren();
            if (!g) return;
            const s = this.schema;
            const types = this.app.lang;
            for (const n of Doc.nodes(g)) {
                for (const p of s.ports(n)) {
                    if (!p.path) continue;
                    const from = s.wire(n, p.path);
                    if (!from) continue;
                    if (p.exec) {
                        const a = this.at(n.id, portKey(p));
                        const b = this.at(from, portKey({ node: from, input: true, exec: true, path: null }));
                        if (a && b) this.path(a, b, 'tg-wire-exec', null, { node: n.id, path: p.path });
                    } else {
                        const a = this.at(from, portKey({ node: from, input: false, exec: false, path: null }));
                        const b = this.at(n.id, portKey(p));
                        if (a && b) this.path(a, b, 'tg-wire-data', typeColor(types.typeOf(from)) || s.color(Doc.nodeIn(g, from)?.k), { node: n.id, path: p.path });
                    }
                }
            }
            const body = Doc.isBody(g);
            const target = body ? g.entry : g.root;
            if (target) {
                const pseudo = portKey({ node: body ? ENTRY : RESULT, input: !body, exec: body, path: null });
                if (body) {
                    const a = this.at(ENTRY, pseudo), b = this.at(target, portKey({ node: target, input: true, exec: true, path: null }));
                    if (a && b) this.path(a, b, 'tg-wire-exec', null, { node: ENTRY, path: null });
                } else {
                    const a = this.at(target, portKey({ node: target, input: false, exec: false, path: null })), b = this.at(RESULT, pseudo);
                    if (a && b) this.path(a, b, 'tg-wire-data', typeColor(types.typeOf(target)) || s.color(Doc.nodeIn(g, target)?.k), { node: RESULT, path: null });
                }
            }
            // The port colours of types
            for (const v of this.views.values()) {
                if (v.pseudo) continue;
                const type = types.typeOf(v.node.id);
                for (const p of v.ports.values()) {
                    if (p.port.exec) continue;
                    const from = p.port.input ? s.wire(v.node, p.port.path) : v.node.id;
                    const t = p.port.input ? types.typeOf(from) : type;
                    const c = typeColor(t);
                    p.dot.style.background = c || '';
                    p.row.title = t ? `${p.port.label || 'value'}: ${t}` : '';
                    p.dot.classList.toggle('tg-wired', !!(p.port.input ? from : this.isRead(v.node.id)));
                }
            }
            if (this.drag?.kind === 'wire' && this.drag.to) this.path(this.drag.a, this.drag.to, this.drag.exec ? 'tg-wire-exec.tg-wire-new' : 'tg-wire-data.tg-wire-new', null, null);
        }

        isRead(id) {
            const g = this.graph;
            if (g.root === id) return true;
            return Doc.nodes(g).some(o => this.schema.dataWires(o).includes(id));
        }

        path(a, b, cls, color, wire) {
            const dx = Math.max(40, Math.abs(b.x - a.x) / 2);
            const p = svg('path', { d: `M ${a.x} ${a.y} C ${a.x + dx} ${a.y}, ${b.x - dx} ${b.y}, ${b.x} ${b.y}`, class: cls.replace(/\./g, ' ') });
            if (color) p.style.stroke = color;
            if (wire) p.dataset.wire = JSON.stringify(wire);
            this.wires.append(p);
            return p;
        }

        applyTransform() {
            this.world.style.transform = `translate(${this.pan.x}px, ${this.pan.y}px) scale(${this.zoom})`;
            this.el.style.setProperty('--tg-grid-size', 24 * this.zoom + 'px');
            this.el.style.backgroundPosition = `${this.pan.x}px ${this.pan.y}px`;
        }

        showSelection() {
            const sel = this.editor.selection;
            for (const [id, v] of this.views) v.el.classList.toggle('tg-selected', sel.has(id));
            for (const [id, v] of this.noteViews) v.el.classList.toggle('tg-selected', sel.has(id));
        }

        // Shows every node (or the selection), zoomed to fit
        frame(selection = true) {
            const g = this.graph;
            if (!g) return;
            const sel = this.editor.selection;
            const boxes = [];
            for (const [id, v] of this.views) {
                if (selection && sel.size && !sel.has(id)) continue;
                const x = v.pseudo ? v.node.x : this.x(v.node), y = v.pseudo ? v.node.y : this.y(v.node);
                boxes.push([x, y, x + (v.w || 160), y + (v.h || 60)]);
            }
            if (!boxes.length) {
                this.pan = { x: 80, y: 80 };
                this.zoom = 1;
                this.applyTransform();
                return;
            }
            const x0 = Math.min(...boxes.map(b => b[0])), y0 = Math.min(...boxes.map(b => b[1]));
            const x1 = Math.max(...boxes.map(b => b[2])), y1 = Math.max(...boxes.map(b => b[3]));
            const r = this.el.getBoundingClientRect();
            const w = r.width || 800, h = r.height || 600;
            this.zoom = Math.max(0.2, Math.min(1, Math.min((w - 80) / (x1 - x0 || 1), (h - 80) / (y1 - y0 || 1))));
            this.pan = { x: (w - (x1 - x0) * this.zoom) / 2 - x0 * this.zoom, y: (h - (y1 - y0) * this.zoom) / 2 - y0 * this.zoom };
            this.applyTransform();
        }

        // Brings a node to the middle
        reveal(id) {
            const v = this.views.get(id);
            if (!v) return;
            const r = this.el.getBoundingClientRect();
            this.pan = { x: r.width / 2 - (this.x(v.node) + (v.w || 160) / 2) * this.zoom, y: r.height / 2 - (this.y(v.node) + (v.h || 60) / 2) * this.zoom };
            this.applyTransform();
        }

        // A screen point in the world
        toWorld(clientX, clientY) {
            const r = this.el.getBoundingClientRect();
            return { x: (clientX - r.left - this.pan.x) / this.zoom, y: (clientY - r.top - this.pan.y) / this.zoom };
        }

        // ---------------------------------------------------------------------------------------------------------
        // The pointer

        listen() {
            const c = this.el;
            c.addEventListener('pointerdown', e => this.down(e));
            c.addEventListener('pointermove', e => this.move(e));
            c.addEventListener('pointerup', e => this.up(e));
            c.addEventListener('pointercancel', () => this.cancel());
            c.addEventListener('contextmenu', e => e.preventDefault());
            c.addEventListener('dblclick', e => {
                if (e.target.closest('.tg-node, .tg-note, input, textarea, select')) return;
                this.app.openSearch(e.clientX, e.clientY, null);
            });
            c.addEventListener('wheel', e => this.wheel(e), { passive: false });
            c.addEventListener('pointerover', e => this.over(e));
            c.addEventListener('pointerleave', () => { tooltip(0, 0, null); this.hoverNode = 0; });
        }

        portAt(target) {
            const row = target.closest?.('.tg-port');
            if (!row) return null;
            const id = Number(row.closest('.tg-node').dataset.id);
            return this.views.get(id)?.ports.get(row.dataset.port)?.port ?? null;
        }

        down(e) {
            this.el.focus({ preventScroll: true });
            this.lastPointer = { x: e.clientX, y: e.clientY };
            if (e.target.closest('input, textarea, select, .tg-check')) return;
            const start = { x: e.clientX, y: e.clientY };
            if (e.button === 1 || e.button === 2 || (e.button === 0 && this.app.spaceDown)) {
                this.drag = { kind: 'pan', start, pan: { ...this.pan }, button: e.button, target: e.target };
                c(this.el, e);
                return;
            }
            if (e.button !== 0) return;
            const port = this.portAt(e.target);
            if (port) {
                this.beginWire(port, e);
                c(this.el, e);
                return;
            }
            const noteEl = e.target.closest('.tg-note');
            if (noteEl) {
                const id = Number(noteEl.dataset.note);
                const note = this.noteViews.get(id)?.note;
                if (!note) return;
                if (e.target.classList.contains('tg-note-size')) {
                    this.drag = { kind: 'size', start, note, w: note.w || 200, h: note.h || 120 };
                } else {
                    this.pick(id, e);
                    this.beginMove(start);
                }
                c(this.el, e);
                return;
            }
            const nodeEl = e.target.closest('.tg-node');
            if (nodeEl) {
                const id = Number(nodeEl.dataset.id);
                if (id < 0) return;
                this.pick(id, e);
                this.beginMove(start);
                c(this.el, e);
                return;
            }
            // Nothing: a band to select with
            this.drag = { kind: 'band', start, add: e.shiftKey || e.ctrlKey || e.metaKey, before: new Set(this.editor.selection) };
            c(this.el, e);
        }

        pick(id, e) {
            const ed = this.editor;
            if (e.shiftKey || e.ctrlKey || e.metaKey) {
                if (ed.selection.has(id)) ed.selection.delete(id);
                else ed.selection.add(id);
                ed.selected = id;
                ed.emit('selection');
            } else if (!ed.selection.has(id)) {
                ed.select(id);
            } else {
                ed.selected = id;
                ed.emit('selection');
            }
        }

        beginMove(start) {
            const moving = new Map();
            for (const id of this.editor.selection) {
                const e = this.editor.doc.find(id);
                if (e && (e.role === 'node' || e.role === 'note') && e.graph === this.graph) moving.set(id, { x: Number(e.value.x) || 0, y: Number(e.value.y) || 0 });
            }
            this.drag = { kind: 'move', start, moving, moved: false };
        }

        // Starts a wire from a port. A wired input (or a statement's exec in) gives up its wire to be dragged on
        // from where it comes; an output starts a new one.
        beginWire(port, e) {
            const g = this.graph;
            const s = this.schema;
            let from = port;
            if (port.input) {
                let source = null;
                if (port.exec) {
                    if (port.node > 0) {
                        const src = this.editor.doc.execSourceOf(g, port.node);
                        if (src) source = { node: src.node, input: false, exec: true, path: src.path, label: '' };
                        else if (g.entry === port.node) source = { node: ENTRY, input: false, exec: true, path: null };
                    }
                } else if (port.node === RESULT) {
                    if (g.root) source = { node: g.root, input: false, exec: false, path: null };
                } else {
                    const n = Doc.nodeIn(g, port.node);
                    const w = n && s.wire(n, port.path);
                    if (w) source = { node: w, input: false, exec: false, path: null };
                }
                if (source) {
                    // Take it off; dropping it again on a port wires it there
                    this.unwire(port);
                    from = source;
                }
            }
            const a = this.portPlace(from);
            this.drag = { kind: 'wire', from, exec: from.exec, a, to: a, start: { x: e.clientX, y: e.clientY }, taken: from !== port };
        }

        portPlace(p) {
            return this.at(p.node, portKey(p)) || this.toWorld(this.lastPointer.x, this.lastPointer.y);
        }

        // Takes a wire away from an input (in the model only, until the drag ends)
        unwire(port) {
            const g = this.graph;
            if (!this.pendingUnwire) this.pendingUnwire = [];
            if (port.exec && port.input) {
                const src = this.editor.doc.execSourceOf(g, port.node);
                if (src) this.pendingUnwire.push(() => this.editor.doc.disconnect(g, src.node, src.path));
                else if (g.entry === port.node) this.pendingUnwire.push(() => this.editor.doc.setEntry(g, null));
            } else if (port.node === RESULT) {
                this.pendingUnwire.push(() => this.editor.doc.setRoot(g, null));
            } else {
                this.pendingUnwire.push(() => this.editor.doc.disconnect(g, port.node, port.path));
            }
        }

        move(e) {
            this.lastPointer = { x: e.clientX, y: e.clientY };
            const d = this.drag;
            if (!d) return;
            const dx = e.clientX - d.start.x, dy = e.clientY - d.start.y;
            if (d.kind === 'pan') {
                this.pan = { x: d.pan.x + dx, y: d.pan.y + dy };
                d.moved = d.moved || Math.abs(dx) + Math.abs(dy) > 4;
                this.applyTransform();
            } else if (d.kind === 'move') {
                if (!d.moved && Math.abs(dx) + Math.abs(dy) < 3) return;
                d.moved = true;
                for (const [id, at] of d.moving) {
                    const p = { x: Math.round(at.x + dx / this.zoom), y: Math.round(at.y + dy / this.zoom) };
                    this.places.set(id, p);
                    const v = this.views.get(id) || this.noteViews.get(id);
                    if (v) {
                        v.el.style.left = p.x + 'px';
                        v.el.style.top = p.y + 'px';
                    }
                }
                this.drawWires();
            } else if (d.kind === 'size') {
                const w = Math.max(80, Math.round(d.w + dx / this.zoom)), h = Math.max(40, Math.round(d.h + dy / this.zoom));
                d.size = { w, h };
                const v = this.noteViews.get(d.note.id);
                v.el.style.width = w + 'px';
                v.el.style.height = h + 'px';
            } else if (d.kind === 'wire') {
                d.to = this.toWorld(e.clientX, e.clientY);
                const port = this.portAt(document.elementFromPoint(e.clientX, e.clientY) || e.target);
                for (const r of this.el.querySelectorAll('.tg-port.tg-target')) r.classList.remove('tg-target');
                if (port && this.canJoin(d.from, port)) {
                    this.views.get(port.node)?.ports.get(portKey(port))?.row?.classList.add('tg-target');
                }
                this.drawWires();
            } else if (d.kind === 'band') {
                const r = this.el.getBoundingClientRect();
                const x0 = Math.min(d.start.x, e.clientX), y0 = Math.min(d.start.y, e.clientY);
                const x1 = Math.max(d.start.x, e.clientX), y1 = Math.max(d.start.y, e.clientY);
                Object.assign(this.band.style, { display: 'block', left: x0 - r.left + 'px', top: y0 - r.top + 'px', width: x1 - x0 + 'px', height: y1 - y0 + 'px' });
                const sel = new Set(d.add ? d.before : []);
                for (const [id, v] of [...this.views, ...this.noteViews]) {
                    if (id < 0) continue;
                    const b = v.el.getBoundingClientRect();
                    if (b.right >= x0 && b.left <= x1 && b.bottom >= y0 && b.top <= y1) sel.add(id);
                }
                this.editor.selection = sel;
                this.showSelection();
            }
        }

        // Whether a wire from port `a` can end on port `b`: an output to an input, exec to exec, data to data
        canJoin(a, b) {
            if (!a || !b || a.input === b.input || a.exec !== b.exec) return false;
            return a.node !== b.node;
        }

        up(e) {
            const d = this.drag;
            this.drag = null;
            if (!d) return;
            try { this.el.releasePointerCapture(e.pointerId); } catch {}
            if (d.kind === 'pan') {
                // A right click that didn't move: what to do here
                if (d.button === 2 && !d.moved) this.context(e);
                return;
            }
            if (d.kind === 'band') {
                this.band.style.display = 'none';
                if (Math.abs(e.clientX - d.start.x) + Math.abs(e.clientY - d.start.y) < 3 && !d.add) {
                    this.editor.selection.clear();
                    this.editor.selected = this.editor.slot?.owner ?? null;
                }
                this.editor.emit('selection');
                return;
            }
            if (d.kind === 'move') {
                if (!d.moved) return;
                const places = new Map(this.places);
                this.places.clear();
                this.app.edit(doc => {
                    for (const [id, p] of places) {
                        const v = doc.get(id);
                        if (!v) continue;
                        v.x = p.x;
                        v.y = p.y;
                    }
                });
                return;
            }
            if (d.kind === 'size') {
                if (!d.size) return;
                this.app.edit(() => { d.note.w = d.size.w; d.note.h = d.size.h; });
                return;
            }
            if (d.kind === 'wire') {
                for (const r of this.el.querySelectorAll('.tg-port.tg-target')) r.classList.remove('tg-target');
                const pending = this.pendingUnwire || [];
                this.pendingUnwire = null;
                const port = this.portAt(document.elementFromPoint(e.clientX, e.clientY) || e.target);
                const moved = Math.abs(e.clientX - d.start.x) + Math.abs(e.clientY - d.start.y) > 4;
                if (port && this.canJoin(d.from, port)) {
                    this.app.edit(() => {
                        for (const f of pending) f();
                        const why = this.join(d.from, port);
                        if (why) this.app.say(why);
                    });
                    return;
                }
                if (d.taken) {
                    // Dropped on nothing: the wire is gone
                    if (moved) this.app.edit(() => { for (const f of pending) f(); });
                    else this.drawWires();
                    return;
                }
                this.drawWires();
                if (moved || !port) this.app.openSearch(e.clientX, e.clientY, { from: d.from });
            }
        }

        // Wires two ports, whichever end the drag began at. Returns why it can't be, or null.
        join(a, b) {
            const out = a.input ? b : a, inp = a.input ? a : b;
            const g = this.graph;
            const doc = this.editor.doc;
            if (out.exec) {
                if (out.node === ENTRY) return doc.setEntry(g, inp.node);
                return doc.connect(g, out.node, out.path, inp.node);
            }
            if (inp.node === RESULT) return doc.setRoot(g, out.node);
            if (out.node < 0) return 'a value comes from a node';
            return doc.connect(g, inp.node, inp.path, out.node);
        }

        cancel() {
            if (this.drag?.kind === 'move') {
                this.places.clear();
                this.render();
            }
            this.pendingUnwire = null;
            this.drag = null;
            this.band.style.display = 'none';
        }

        wheel(e) {
            e.preventDefault();
            if (e.ctrlKey || e.metaKey) {
                const r = this.el.getBoundingClientRect();
                const mx = e.clientX - r.left, my = e.clientY - r.top;
                const before = this.zoom;
                const zoom = Math.max(0.15, Math.min(2.5, before * Math.exp(-e.deltaY * 0.0025)));
                this.pan = { x: mx - (mx - this.pan.x) * zoom / before, y: my - (my - this.pan.y) * zoom / before };
                this.zoom = zoom;
            } else {
                this.pan = { x: this.pan.x - (e.shiftKey ? e.deltaY : e.deltaX), y: this.pan.y - (e.shiftKey ? 0 : e.deltaY) };
            }
            this.applyTransform();
        }

        zoomBy(f) {
            const r = this.el.getBoundingClientRect();
            const mx = r.width / 2, my = r.height / 2, before = this.zoom;
            this.zoom = Math.max(0.15, Math.min(2.5, before * f));
            this.pan = { x: mx - (mx - this.pan.x) * this.zoom / before, y: my - (my - this.pan.y) * this.zoom / before };
            this.applyTransform();
        }

        // A right click on nothing opens the search; on a node, what can be done to it
        context(e) {
            const nodeEl = e.target.closest?.('.tg-node');
            const id = nodeEl ? Number(nodeEl.dataset.id) : 0;
            if (id > 0) {
                if (!this.editor.selection.has(id)) this.editor.select(id);
                const n = this.editor.doc.get(id);
                const app = this.app;
                menu(e.clientX, e.clientY, [
                    (n.k === 'member' || n.k === 'method') && { label: 'Pick member…', run: () => app.pickMember(n, e.clientX, e.clientY) },
                    { label: 'Copy', run: () => app.command('copy') },
                    { label: 'Cut', run: () => app.command('cut') },
                    { label: 'Duplicate', run: () => app.command('duplicate') },
                    { label: 'Delete', run: () => app.command('delete') },
                    { separator: true },
                    { label: n.comment ? 'Edit comment' : 'Add comment', run: () => app.focusInspector('comment') },
                    { label: 'Add note here', run: () => app.addNote(this.toWorld(e.clientX, e.clientY)) },
                ].filter(Boolean));
                return;
            }
            const noteEl = e.target.closest?.('.tg-note');
            if (noteEl) {
                const nid = Number(noteEl.dataset.note);
                this.editor.select(nid);
                menu(e.clientX, e.clientY, [{ label: 'Delete note', run: () => this.app.command('delete') }]);
                return;
            }
            this.app.openSearch(e.clientX, e.clientY, null);
        }

        // The tooltip of a node: its type and what tidels says of it
        over(e) {
            const nodeEl = e.target.closest?.('.tg-node');
            const id = nodeEl ? Number(nodeEl.dataset.id) : 0;
            if (id === this.hoverNode) return;
            this.hoverNode = id;
            tooltip(0, 0, null);
            if (id <= 0 || this.drag) return;
            const n = this.editor.doc.get(id);
            if (!n) return;
            const lang = this.app.lang;
            const parts = [];
            const type = lang.typeOf(id);
            if (type) parts.push(type);
            for (const d of lang.diagnosticsOf(id)) parts.push((d.severity === 1 ? 'error: ' : 'warning: ') + d.message);
            const x = e.clientX, y = e.clientY;
            if (parts.length) tooltip(x, y, parts.join('\n'));
            if (lang.status !== 'checked') return;
            lang.hover(n).then(text => {
                if (this.hoverNode !== id || !text) return;
                tooltip(x, y, [...parts.filter(p => p !== type), text.replace(/```\w*\n?/g, '').trim()].join('\n\n'));
            });
        }
    }

    // Keeps the pointer's events on the canvas while a drag goes on
    function c(target, e) {
        try { target.setPointerCapture(e.pointerId); } catch {}
        e.preventDefault();
    }

    G.Canvas = Canvas;
    G.Canvas.ENTRY = ENTRY;
    G.Canvas.RESULT = RESULT;
    G.Canvas.portKey = portKey;
})(globalThis.TideGraph = globalThis.TideGraph || {});
