// The board: a file as one canvas. Every declaration is on it where its x, y say: a card for data, a frame for code;
// a namespace block is a tinted region around its declarations. The board pans and zooms, draws only what's on
// screen (frames far away are blocks, frames off screen are empty), and turns the pointer into what's done: select,
// move, wire, and a menu or a search for every place, so no click does nothing.
(function (G) {
    'use strict';
    const { el, button, menu, tooltip, toast } = G.ui;
    const { Doc, FrameView } = G;
    const { ENTRY, RESULT, HEAD, portKey } = G.frames;

    // tidec's board (graph_layout.c): cards in a column on the left, frames to their right
    const CARD_W = 300, CARD_ROW = 26, CARD_HEAD = 48, CARD_GAP = 40, LANE = 380, FRAME_GAP = 80;
    const FAR = 0.32;      // Below this zoom a frame's nodes are blocks
    const MARGIN = 400;    // What's this near the screen is drawn too
    const MIN_ZOOM = 0.02, MAX_ZOOM = 2;

    class Board {
        constructor(app) {
            this.app = app;
            this.el = el('div.tg-board', { tabIndex: 0 });
            this.world = el('div.tg-world');
            this.regionsLayer = el('div.tg-regions');
            this.itemsLayer = el('div.tg-items');
            this.valueLayer = el('div.tg-values');
            G.ui.put(this.world, this.regionsLayer, this.itemsLayer, this.valueLayer);
            this.band = el('div.tg-band');
            this.start = el('div.tg-start');
            G.ui.put(this.el, this.world, this.band, this.start);
            this.pan = { x: 40, y: 40 };
            this.zoom = 1;
            this.cards = new Map();   // id -> { el, item, x, y, w, h, drawn }
            this.frames = new Map();  // id -> FrameView
            this.regions = new Map(); // id -> { el, item }
            this.auto = new Map();    // Where declarations with no place of their own were put: id -> { x, y }
            this.valueFrame = null;
            this.items = [];
            this.drag = null;
            this.pointers = new Map();
            this.lastPointer = { x: 0, y: 0 };
            this.framed = false;
            this.listen();
        }

        get editor() { return this.app.editor; }
        get schema() { return this.app.editor.schema; }

        // ---------------------------------------------------------------------------------------------------------
        // Drawing

        // Everything again, after the document changed
        render() {
            const ed = this.editor;
            tooltip(0, 0, null);
            if (!ed || ed.error) {
                this.itemsLayer.replaceChildren();
                this.regionsLayer.replaceChildren();
                this.valueLayer.replaceChildren();
                this.cards.clear();
                this.frames.clear();
                this.regions.clear();
                this.valueFrame = null;
                this.start.style.display = 'none';
                return;
            }
            const items = ed.doc.items();
            this.items = items;
            this.placeAll(items);
            const seen = new Set();
            for (const item of items) {
                seen.add(item.id);
                if (item.card) {
                    let c = this.cards.get(item.id);
                    if (!c) {
                        c = { el: el('div.tg-card', { dataset: { id: item.id, card: item.id } }), w: CARD_W, h: 0, drawn: false };
                        this.cards.set(item.id, c);
                        G.ui.put(this.itemsLayer, c.el);
                    }
                    c.item = item;
                    c.x = item.x; c.y = item.y;
                    c.h = c.drawn ? c.h : this.cardHeight(item);
                    c.dirty = true;
                    c.el.style.left = c.x + 'px';
                    c.el.style.top = c.y + 'px';
                } else if (item.frame) {
                    let f = this.frames.get(item.id);
                    if (!f) {
                        f = new FrameView(this, item);
                        this.frames.set(item.id, f);
                        G.ui.put(this.itemsLayer, f.el);
                    }
                    f.refresh(item);
                    f.x = item.x; f.y = item.y;
                    f.dirty = true;
                    f.el.style.left = f.x + 'px';
                    f.el.style.top = f.y + 'px';
                    f.el.style.width = f.size.w + 'px';
                    f.body.style.height = f.size.h + 'px';
                    f.renderHead();
                    f.label?.remove();
                    f.label = el('div.tg-far-label', { text: ed.doc.titleOf(item) });
                    G.ui.put(f.el, f.label);
                }
            }
            for (const [id, c] of this.cards) if (!seen.has(id)) { c.el.remove(); this.cards.delete(id); }
            for (const [id, f] of this.frames) if (!seen.has(id)) { f.el.remove(); this.frames.delete(id); }
            // A value's frame stays while its place in the file does
            if (this.valueFrame) {
                const v = this.valueFrame;
                if (!ed.doc.find(v.slot.owner)) this.closeValue(false);
                else { v.refresh(); v.dirty = true; v.renderHead(); }
            }
            this.stale = false;
            // What the pointer was over may be gone: what was said of it goes too
            tooltip(0, 0, null);
            this.hover = null;
            this.lit = null;
            this.renderStart(items);
            if (!this.framed) {
                // A file opens on its start; one that opens empty stays where its first thing is made
                this.framed = true;
                if (items.length) this.home();
            }
            this.applyTransform();
            this.cull();
            this.renderRegions();
            this.showSelection();
            this.markProblems();
            this.applyFocus();
        }

        // How tall tidec takes a card to be, which is how tall it's drawn
        cardHeight(item) {
            const v = item.value;
            let rows = (Array.isArray(v.members) ? v.members.length : 0) + (Array.isArray(v.entries) ? v.entries.length : 0);
            if (typeof v.text === 'string') rows += Math.min(24, v.text.split('\n').length);
            return CARD_HEAD + CARD_ROW * (rows + 2);
        }

        // Where each item is: its own x, y, or for one that has none, a place beside what's there (cards under the
        // cards, frames under the frames), kept while the file is open
        placeAll(items) {
            let cardX = Infinity, cardBottom = -Infinity, frameX = Infinity, frameBottom = -Infinity;
            const sized = item => {
                if (item.card) return { w: CARD_W, h: this.cards.get(item.id)?.drawn ? this.cards.get(item.id).h : this.cardHeight(item) };
                const f = this.frames.get(item.id);
                if (f && f.item === item) return { w: f.size.w, h: f.size.h + HEAD };
                const probe = f || new FrameView(this, item);
                probe.refresh(item);
                return { w: probe.size.w, h: probe.size.h + HEAD };
            };
            const placed = [];
            for (const item of items) {
                if (item.region) continue;
                const v = item.value;
                const at = typeof v.x === 'number' && typeof v.y === 'number' ? { x: v.x, y: v.y } : this.auto.get(item.id);
                if (!at) continue;
                item.x = at.x; item.y = at.y;
                item.own = typeof v.x === 'number' && typeof v.y === 'number';
                placed.push(item);
                const size = sized(item);
                if (item.card) { cardX = Math.min(cardX, at.x); cardBottom = Math.max(cardBottom, at.y + size.h); }
                else { frameX = Math.min(frameX, at.x); frameBottom = Math.max(frameBottom, at.y + size.h); }
            }
            if (cardX === Infinity) cardX = frameX === Infinity ? 0 : frameX - LANE;
            if (frameX === Infinity) frameX = cardX + LANE;
            let cy = cardBottom === -Infinity ? 0 : cardBottom + CARD_GAP;
            let fy = frameBottom === -Infinity ? 0 : frameBottom + FRAME_GAP;
            for (const item of items) {
                if (item.region || item.x !== undefined) continue;
                const size = sized(item);
                if (item.card) { item.x = cardX; item.y = cy; cy += size.h + CARD_GAP; }
                else { item.x = frameX; item.y = fy; fy += size.h + FRAME_GAP; }
                this.auto.set(item.id, { x: item.x, y: item.y });
            }
        }

        // Writes the places the board gave to declarations that had none: once one is moved, all stay where they
        // are seen
        persistPlaces(doc) {
            for (const item of doc.items()) {
                if (item.region) continue;
                const v = item.value;
                if (typeof v.x === 'number' && typeof v.y === 'number') continue;
                const at = this.auto.get(item.id);
                if (!at) continue;
                this.setPlace(doc, item, at.x, at.y);
            }
        }

        setPlace(doc, item, x, y) {
            const order = item.accessor ? doc.schema.element('accessor') : item.spec?.keys || [];
            if (!order.includes('x')) return;
            G.json.set(item.value, 'x', Math.round(x), order);
            G.json.set(item.value, 'y', Math.round(y), order);
        }

        // The start: an empty board says what to do
        renderStart(items) {
            const s = this.schema;
            if (items.length) {
                this.start.style.display = 'none';
                this.start.replaceChildren();
                return;
            }
            const choices = s.declarations.filter(d => d.common);
            this.start.replaceChildren(el('div.tg-start-in', null,
                el('div.tg-start-title', { text: 'What do you want to make?' }),
                el('div.tg-start-sub', { text: 'A game is made of data, and of code that runs on it. Start with either.' }),
                el('div.tg-start-choices', null, choices.map(d => el('button.tg-start-choice', {
                    type: 'button', dataset: { kind: d.kind }, onpointerdown: e => e.stopPropagation(),
                    onclick: () => this.app.addDecl(d.kind),
                }, el('span.tg-chip.tg-chip-' + (d.group || 'more').toLowerCase(), { text: s.groups.find(g => g.name === d.group)?.title || d.group }),
                el('span.tg-start-name', { text: d.title }), el('span.tg-start-summary', { text: d.summary })))),
                button('More…', e => G.pickers.addMenu(this.app, 0, 0, { anchor: e.currentTarget }), 'Everything a file can have', 'tg-button.tg-start-more'),
                el('div.tg-start-hint', { text: 'Later: + Add, or a right click (a long press, on a touch screen) anywhere on the board.' })));
            this.start.style.display = 'flex';
        }

        // Draws what's on screen (or near it) and empties what isn't
        cull() {
            const r = this.el.getBoundingClientRect();
            const w = r.width || 800, h = r.height || 600;
            const z = this.zoom;
            const view = { x0: (-this.pan.x - MARGIN) / z, y0: (-this.pan.y - MARGIN) / z, x1: (w - this.pan.x + MARGIN) / z, y1: (h - this.pan.y + MARGIN) / z };
            const far = z < FAR;
            this.el.classList.toggle('tg-far-view', far);
            this.el.style.setProperty('--tg-inv', String(Math.min(60, 1 / z)));
            const seen = (x, y, bw, bh) => x < view.x1 && x + bw > view.x0 && y < view.y1 && y + bh > view.y0;
            let changed = false;
            for (const c of this.cards.values()) {
                const on = seen(c.x, c.y, c.w, Math.max(c.h, 60));
                if (on && (!c.drawn || c.dirty)) {
                    c.el.replaceChildren(G.cards.render(this, c.item), el('div.tg-far-label', { text: this.editor.doc.titleOf(c.item) }));
                    c.drawn = true;
                    c.dirty = false;
                    c.h = c.el.offsetHeight || c.h;
                    changed = true;
                } else if (!on && c.drawn && !c.el.contains(document.activeElement)) {
                    c.el.replaceChildren();
                    c.el.style.height = c.h + 'px';
                    c.drawn = false;
                } else if (on) {
                    c.el.style.height = '';
                }
                if (on) c.el.style.height = '';
            }
            for (const f of this.frames.values()) {
                const on = seen(f.x, f.y, f.size.w, f.size.h + HEAD);
                const mode = !on ? 'none' : far ? 'far' : 'near';
                if (mode !== f.mode || (f.dirty && mode !== 'none')) {
                    if (mode === 'none' && (f.el.contains(document.activeElement) && document.activeElement !== document.body || this.drag?.frame === f)) continue;
                    f.render(mode);
                    f.dirty = false;
                    changed = true;
                }
            }
            const v = this.valueFrame;
            if (v && (v.dirty || v.mode !== 'near')) {
                v.render('near');
                v.dirty = false;
                v.el.style.width = v.size.w + 'px';
                if (!this.drag) this.keepValueSeen();
            }
            return changed;
        }

        scheduleCull() {
            if (this.culling) return;
            this.culling = requestAnimationFrame(() => {
                this.culling = 0;
                if (!this.editor || this.editor.error) return;
                if (this.cull()) {
                    this.showSelection();
                    this.markProblems();
                    this.renderRegions();
                }
            });
        }

        // A namespace block: a tinted region around its declarations, named
        renderRegions() {
            const ed = this.editor;
            const boxes = new Map();
            const grow = (ns, x, y, w, h) => {
                for (let n = ns; n; n = this.items.find(i => i.region && i.value === n)?.namespace) {
                    const b = boxes.get(n.id) || { x0: Infinity, y0: Infinity, x1: -Infinity, y1: -Infinity };
                    b.x0 = Math.min(b.x0, x); b.y0 = Math.min(b.y0, y); b.x1 = Math.max(b.x1, x + w); b.y1 = Math.max(b.y1, y + h);
                    boxes.set(n.id, b);
                }
            };
            for (const item of this.items) {
                if (item.region || !item.namespace) continue;
                const c = this.cards.get(item.id), f = this.frames.get(item.id);
                if (c) grow(item.namespace, c.x, c.y, c.w, c.h);
                else if (f) grow(item.namespace, f.x, f.y, f.size.w, f.size.h + HEAD);
            }
            const seen = new Set();
            let fallback = 0;
            for (const item of this.items) {
                if (!item.region) continue;
                seen.add(item.id);
                let r = this.regions.get(item.id);
                if (!r) {
                    r = { el: el('div.tg-region', { dataset: { id: item.id, region: item.id } }) };
                    this.regions.set(item.id, r);
                    G.ui.put(this.regionsLayer, r.el);
                }
                r.item = item;
                // An empty block waits above the board's corner for its first declaration
                const b = boxes.get(item.id) || { x0: -LANE, y0: -160 - 90 * fallback, x1: -LANE + 260, y1: -160 - 90 * fallback++ + 40 };
                const pad = 28;
                Object.assign(r.el.style, { left: b.x0 - pad + 'px', top: b.y0 - pad - 30 + 'px', width: b.x1 - b.x0 + 2 * pad + 'px', height: b.y1 - b.y0 + 2 * pad + 30 + 'px' });
                const v = item.value;
                r.el.replaceChildren(el('div.tg-region-head', null,
                    el('span.tg-chip.tg-chip-more', { text: item.spec?.title || 'Namespace', title: item.spec?.summary }),
                    G.ui.field(v.name ?? '', x => this.app.edit(() => { v.name = x; }), { className: 'tg-input.tg-name', placeholder: 'Name', fk: `${v.id}:name`,
                        check: x => (/^[A-Za-z_][A-Za-z0-9_]*(\.[A-Za-z_][A-Za-z0-9_]*)*$/.test(x) ? null : 'Names joined by dots, like Game.Combat') }),
                    button('+ Add', e => G.pickers.addMenu(this.app, 0, 0, { anchor: e.currentTarget, namespace: v }), 'Add a declaration to this namespace', 'tg-button'),
                    button('⋯', e => menu(e.clientX, e.clientY, [
                        { label: 'Take the block away', summary: 'Its declarations stay, outside it', run: () => this.app.edit(() => { const list = item.list; list.splice(list.indexOf(v), 1, ...(v.decls || [])); }) },
                        { label: 'Delete it and what is in it', danger: true, run: () => this.app.edit(() => { item.list.splice(item.list.indexOf(v), 1); }) },
                    ], { anchor: e.currentTarget }), 'More', 'tg-more')));
            }
            for (const [id, r] of this.regions) if (!seen.has(id)) { r.el.remove(); this.regions.delete(id); }
        }

        applyTransform() {
            this.world.style.transform = `translate(${this.pan.x}px, ${this.pan.y}px) scale(${this.zoom})`;
            // The grid's dots stay apart as the board gets small
            let grid = 24 * this.zoom;
            while (grid < 18) grid *= 2;
            this.el.style.setProperty('--tg-grid-size', grid + 'px');
            this.el.style.backgroundPosition = `${this.pan.x}px ${this.pan.y}px`;
            this.app.zoomChanged?.();
        }

        showSelection() {
            const ed = this.editor;
            for (const [id, c] of this.cards) c.el.classList.toggle('tg-picked', ed.picked.has(id));
            for (const [id, f] of this.frames) {
                f.el.classList.toggle('tg-picked', ed.picked.has(id));
                if (f.mode === 'near') f.showSelection();
            }
            if (this.valueFrame?.mode === 'near') this.valueFrame.showSelection();
        }

        // The frame a graph's slot is drawn as, or undefined
        frameOf(slot) {
            if (!slot) return undefined;
            if (this.valueFrame && G.slotKey(this.valueFrame.slot) === G.slotKey(slot)) return this.valueFrame;
            return slot.key === 'body' ? this.frames.get(slot.owner) : undefined;
        }

        // The frame under a target of the pointer, or null
        frameAt(target) {
            const f = target?.closest?.('.tg-frame');
            if (!f) return null;
            if (this.valueFrame?.el === f) return this.valueFrame;
            return this.frames.get(Number(f.dataset.id)) || null;
        }

        // The element an id of the file is drawn as, or null: a node, a row, a card, a frame
        elementOf(id) {
            const e = this.editor.doc.find(id);
            if (!e) return null;
            if (e.role === 'node' || e.role === 'note') {
                const f = this.frameOf(e.slot);
                const drawn = f?.mode === 'near' ? f.elementOf(id) : null;
                if (drawn) return drawn;
                // (A value shown in an input is that input's row: FrameView.elementOf)
            }
            // A card or a frame by its head: what's said of it is said there, not over all that's in it
            const c = this.cards.get(id), fr = this.frames.get(id);
            if (c) return c.el.querySelector('.tg-card-head') || c.el;
            if (fr) return fr.head;
            return this.world.querySelector(`[data-id="${id}"]`);
        }

        // Draws the frames again for what tidels found (an argument's name, a value's colour). One that's being
        // typed in waits until that's done: what's typed isn't lost to it.
        redrawFrames() {
            const active = document.activeElement;
            if (this.drag || (active && /^(INPUT|TEXTAREA)$/.test(active.tagName) && active.closest('.tg-frame'))) {
                this.stale = true;
                return;
            }
            this.stale = false;
            for (const f of this.frames.values()) f.dirty = true;
            if (this.valueFrame) this.valueFrame.dirty = true;
            this.cull();
            this.showSelection();
            this.markProblems();
        }

        // The card or frame an id is, as it's drawn whole
        shellOf(id) {
            return this.cards.get(id)?.el || this.frames.get(id)?.el || null;
        }

        // Brings a card or a frame into view when it isn't, moving the board no more than that takes
        ensureVisible(id) {
            const c = this.cards.get(id), f = this.frames.get(id);
            const r = this.el.getBoundingClientRect();
            const w = r.width || 800, z = this.zoom, m = 24;
            const box = c ? { x: c.x, y: c.y, w: c.w, h: Math.max(c.h, 100) } : f ? { x: f.x, y: f.y, w: Math.min(f.size.w, (w - 2 * m) / z), h: Math.min(f.size.h + HEAD, 320) } : null;
            if (box) this.bringIn(box, m);
        }

        // Brings a node of a frame into view when it isn't whole: one just made, or one whose input was just filled
        showNode(frame, id) {
            const v = frame?.mode === 'near' ? frame.views.get(frame.statementOf(id)) || frame.views.get(id) : null;
            if (!v?.el) return;
            this.bringIn({ x: frame.x + v.x, y: frame.y + HEAD + v.y, w: (v.el.offsetWidth || 160) + 24, h: (v.el.offsetHeight || 80) + 24 }, 40);
        }

        // Moves the board no more than it takes for a box of it to be in view, `m` from the edges
        bringIn(box, m) {
            const r = this.el.getBoundingClientRect();
            const w = r.width || 800, h = r.height || 600, z = this.zoom;
            const x0 = box.x * z + this.pan.x, y0 = box.y * z + this.pan.y, x1 = x0 + box.w * z, y1 = y0 + box.h * z;
            let dx = 0, dy = 0;
            if (x1 > w - m) dx = w - m - x1;
            if (x0 + dx < m) dx = m - x0;
            if (y1 > h - m) dy = h - m - y1;
            if (y0 + dy < m) dy = m - y0;
            if (!dx && !dy) return;
            this.pan = { x: Math.round(this.pan.x + dx), y: Math.round(this.pan.y + dy) };
            this.applyTransform();
            this.cull();
            this.showSelection();
            this.markProblems();
        }

        // Problems on what they're about: the row, input or node (red, with the problem's words on hover), and a
        // count on the card or frame they're in, which shows when that isn't drawn
        markProblems() {
            const lang = this.app.lang;
            for (const x of this.world.querySelectorAll('.tg-has-error, .tg-has-warning')) {
                x.classList.remove('tg-has-error', 'tg-has-warning');
                delete x.dataset.problem;
            }
            const counts = new Map();
            for (const d of lang.diagnostics) {
                if (!d.node) continue;
                const item = this.editor.itemOf(d.node);
                if (item) {
                    const c = counts.get(item) || { errors: 0, warnings: 0 };
                    if (d.severity === 1) c.errors++; else c.warnings++;
                    counts.set(item, c);
                }
                const hole = this.holeOf(d);
                const target = (hole?.frame?.mode === 'near' ? hole.frame.ports.get(G.frames.portKey(hole.port))?.row : null) || this.elementOf(d.node);
                if (!target) continue;
                const error = d.severity === 1;
                if (error) { target.classList.remove('tg-has-warning'); target.classList.add('tg-has-error'); }
                else if (!target.classList.contains('tg-has-error')) target.classList.add('tg-has-warning');
                // Each problem with its note, one after the other (see over())
                const text = (error ? 'E' : 'W') + this.wordsOf(d, hole) + (d.note && !hole ? '\n' + d.note : '');
                target.dataset.problem = target.dataset.problem ? target.dataset.problem + '\u0001' + text : text;
            }
            for (const badge of this.world.querySelectorAll('[data-badge]')) {
                const c = counts.get(Number(badge.dataset.badge));
                badge.textContent = c ? String(c.errors || c.warnings) : '';
                badge.className = 'tg-badge' + (c ? (c.errors ? ' tg-badge-error' : ' tg-badge-warning') : '');
                badge.title = c ? `${c.errors ? c.errors + (c.errors === 1 ? ' problem' : ' problems') : c.warnings + (c.warnings === 1 ? ' warning' : ' warnings')} here` : '';
            }
            for (const [id, f] of this.frames) f.el.classList.toggle('tg-frame-wrong', !!counts.get(id)?.errors);
            for (const [id, c] of this.cards) c.el.classList.toggle('tg-card-wrong', !!counts.get(id)?.errors);
        }

        // The input a problem is about, when the compiler says one (an input with nothing in it: the diagnostic's
        // data.port, and data.index among a list's): its port, what it's called on its node and the frame it's in
        holeOf(d) {
            if (!d.port || !d.node) return null;
            const e = this.editor.doc.find(d.node);
            if (e?.role !== 'node') return null;
            const port = this.schema.ports(e.value).find(p => p.input && !p.exec && p.key === d.port && (d.index < 0 || p.index === undefined || p.index === d.index));
            if (!port) return null;
            return { port, label: G.frames.inputLabel(this.app, e.value, port, true), frame: this.frameOf(e.slot) };
        }

        // A problem's words: the compiler's, or the editor's own for an input with nothing in it
        wordsOf(d, hole = this.holeOf(d)) {
            return hole ? `Pick what goes in '${hole.label}'` : d.message;
        }

        // What reads the same thing lights with it: over a name shown in an input, the entry's row of that name (or
        // the node that declares the local) and its other reads; over the entry's row or the local's pin, its reads
        light(target) {
            const frameEl = target.closest?.('.tg-frame');
            let read = null;
            if (frameEl) {
                const chip = target.closest('[data-read]');
                const row = chip ? null : target.closest('.tg-entry-row');
                const pin = row ? row.querySelector('[data-pin]') : null;
                const local = chip || row ? null : target.closest('.tg-pin[data-port$="|o|d|local"]');
                read = chip ? chip.dataset.read : pin ? 'pin:' + pin.dataset.pin : local ? 'local:' + local.dataset.port.split('|')[0] : null;
            }
            const key = read ? frameEl.dataset.frame + '\n' + read : null;
            if (key === this.lit) return;
            this.lit = key;
            for (const x of this.world.querySelectorAll('.tg-lit')) x.classList.remove('tg-lit');
            if (!key) return;
            for (const x of frameEl.querySelectorAll('[data-read]')) if (x.dataset.read === read) x.classList.add('tg-lit');
            if (read.startsWith('pin:')) {
                for (const x of frameEl.querySelectorAll('[data-pin]')) if (x.dataset.pin === read.slice(4)) x.closest('.tg-entry-row')?.classList.add('tg-lit');
            } else {
                frameEl.querySelector(`.tg-pin[data-port="${read.slice(6)}|o|d|local"]`)?.classList.add('tg-lit');
            }
        }

        // Puts the cursor in a field by its name once the page has drawn again (`select` its text too)
        focusSoon(fk, select = false) {
            if (!fk) return;
            this.pendingFocus = { fk, select };
            requestAnimationFrame(() => this.applyFocus());
        }

        applyFocus() {
            const want = this.pendingFocus || (G.ui.focusAfter ? { fk: G.ui.focusAfter } : null);
            if (!want) return;
            const target = document.querySelector(`[data-fk="${want.fk}"]`);
            if (!target) return;
            this.pendingFocus = null;
            G.ui.focusAfter = null;
            if (document.activeElement !== target) target.focus({ preventScroll: true });
            if (want.select && target.select) target.select();
            // A focus that was on its way to what was drawn before lands on nothing: back to where it was meant
            requestAnimationFrame(() => {
                const again = document.querySelector(`[data-fk="${want.fk}"]`);
                if (again && (!document.activeElement || document.activeElement === document.body)) again.focus({ preventScroll: true });
            });
        }

        // ---------------------------------------------------------------------------------------------------------
        // The view

        toWorld(clientX, clientY) {
            const r = this.el.getBoundingClientRect();
            return { x: (clientX - r.left - this.pan.x) / this.zoom, y: (clientY - r.top - this.pan.y) / this.zoom };
        }

        // The whole board's box, or the selection's
        bounds(ids) {
            let x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity;
            const add = (x, y, w, h) => { x0 = Math.min(x0, x); y0 = Math.min(y0, y); x1 = Math.max(x1, x + w); y1 = Math.max(y1, y + h); };
            for (const [id, c] of this.cards) if (!ids || ids.has(id)) add(c.x, c.y, c.w, c.h);
            for (const [id, f] of this.frames) if (!ids || ids.has(id)) add(f.x, f.y, f.size.w, f.size.h + HEAD);
            return x0 === Infinity ? null : { x0, y0, x1, y1 };
        }

        // Where a file opens: its start, at a size to read
        home() {
            const b = this.bounds();
            this.zoom = 1;
            this.pan = b ? { x: 40 - b.x0, y: 40 - b.y0 } : { x: 40, y: 40 };
            this.applyTransform();
        }

        // Shows everything (or what's selected), zoomed to fit
        fit(selection = false) {
            const ed = this.editor;
            let b = null;
            if (selection && ed.selection.size) {
                const f = this.frameOf(ed.slot);
                if (f) {
                    let x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity;
                    for (const id of ed.selection) {
                        const v = f.views.get(id);
                        if (!v) continue;
                        x0 = Math.min(x0, f.x + v.left); y0 = Math.min(y0, f.y + HEAD + v.top);
                        x1 = Math.max(x1, f.x + v.left + v.w); y1 = Math.max(y1, f.y + HEAD + v.top + v.h);
                    }
                    if (x0 !== Infinity) b = { x0, y0, x1, y1 };
                }
            } else if (selection && ed.picked.size) {
                b = this.bounds(ed.picked);
            }
            b = b || this.bounds();
            const r = this.el.getBoundingClientRect();
            const w = r.width || 800, h = r.height || 600;
            if (!b) {
                this.zoom = 1;
                this.pan = { x: 40, y: 40 };
            } else {
                this.zoom = Math.max(MIN_ZOOM, Math.min(1, Math.min((w - 80) / (b.x1 - b.x0 || 1), (h - 80) / (b.y1 - b.y0 || 1))));
                this.pan = { x: (w - (b.x1 - b.x0) * this.zoom) / 2 - b.x0 * this.zoom, y: (h - (b.y1 - b.y0) * this.zoom) / 2 - b.y0 * this.zoom };
            }
            this.applyTransform();
            this.scheduleCull();
        }

        // Brings an element of the file to where it can be read, and marks it for a moment
        reveal(id) {
            const ed = this.editor;
            const e = ed.doc.find(id);
            if (!e) return;
            const itemId = ed.itemOf(id);
            const c = this.cards.get(itemId), f = this.frames.get(itemId);
            const r = this.el.getBoundingClientRect();
            const w = r.width || 800, h = r.height || 600;
            if (this.zoom < 0.6) this.zoom = 1;
            let target = null;
            if (f && e.role === 'node') {
                const n = e.value;
                // A value shown in an input is where its reader is
                const reader = f.analysis.shown.get(id)?.node;
                const at = f.analysis.byId.get(f.analysis.mergedInto.get(reader ?? id) ?? reader ?? id) || n;
                target = { x: f.x + f.dx(at) + 80, y: f.y + HEAD + f.dy(at) + 40 };
            } else if (f) {
                // A frame by its start: its title and its entry at the top left
                this.pan = { x: Math.round(Math.min(60, w * 0.08) - f.x * this.zoom), y: Math.round(Math.min(60, h * 0.1) - f.y * this.zoom) };
            } else if (c) {
                target = { x: c.x + c.w / 2, y: c.y + Math.min(c.h, h * 0.6) / 2 };
            }
            if (target) this.pan = { x: Math.round(w / 2 - target.x * this.zoom), y: Math.round(h / 2 - target.y * this.zoom) };
            this.applyTransform();
            this.cull();
            this.showSelection();
            this.markProblems();
            this.renderRegions();
            const shown = this.shellOf(id) || this.elementOf(id);
            if (shown) {
                shown.classList.add('tg-flash');
                setTimeout(() => shown.classList.remove('tg-flash'), 1300);
            }
        }

        zoomAt(factor, clientX, clientY) {
            const r = this.el.getBoundingClientRect();
            const mx = (clientX ?? r.left + r.width / 2) - r.left, my = (clientY ?? r.top + r.height / 2) - r.top;
            const before = this.zoom;
            const zoom = Math.max(MIN_ZOOM, Math.min(MAX_ZOOM, before * factor));
            this.pan = { x: mx - (mx - this.pan.x) * zoom / before, y: my - (my - this.pan.y) * zoom / before };
            this.zoom = zoom;
            this.applyTransform();
            this.scheduleCull();
        }

        // A free place for a new card or frame: where the pointer asked, else in the middle of what's seen when
        // nothing is there, else under the last of its kind
        freePlace(isFrame, at) {
            if (at) return { x: Math.round(at.x), y: Math.round(at.y) };
            const r = this.el.getBoundingClientRect();
            const w = isFrame ? 620 : CARD_W, h = isFrame ? 220 : 130;
            const c = this.toWorld(r.left + r.width / 2, r.top + r.height / 2);
            const want = { x: Math.round(c.x - w / 2), y: Math.round(c.y - h / 2 - 40) };
            const hits = (x, y) => {
                for (const card of this.cards.values()) if (x < card.x + card.w + 20 && x + w > card.x - 20 && y < card.y + card.h + 20 && y + h > card.y - 20) return true;
                for (const f of this.frames.values()) if (x < f.x + f.size.w + 20 && x + w > f.x - 20 && y < f.y + f.size.h + HEAD + 20 && y + h > f.y - 20) return true;
                return false;
            };
            if (!hits(want.x, want.y)) return want;
            let x = Infinity, bottom = -Infinity;
            for (const card of this.cards.values()) if (!isFrame) { x = Math.min(x, card.x); bottom = Math.max(bottom, card.y + card.h); }
            for (const f of this.frames.values()) if (isFrame) { x = Math.min(x, f.x); bottom = Math.max(bottom, f.y + f.size.h + HEAD); }
            if (x === Infinity) {
                // The first of its kind: beside the other kind's column
                const b = this.bounds();
                return b ? { x: isFrame ? b.x0 + LANE : b.x0 - LANE, y: b.y0 } : want;
            }
            return { x, y: bottom + (isFrame ? FRAME_GAP : CARD_GAP) };
        }

        // ---------------------------------------------------------------------------------------------------------
        // A value's small graph, beside what it's the value of

        openValue(value, anchor) {
            this.closeValue(false);
            const f = new FrameView(this, null, value);
            f.refresh();
            let at;
            const a = anchor?.getBoundingClientRect?.();
            if (a && a.width) at = this.toWorld(a.right + 28 * this.zoom, a.top - 60 * this.zoom);
            else {
                const r = this.el.getBoundingClientRect();
                at = this.toWorld(r.left + r.width / 2 - 200, r.top + r.height / 3);
            }
            f.x = Math.round(at.x); f.y = Math.round(at.y);
            f.el.style.left = f.x + 'px';
            f.el.style.top = f.y + 'px';
            f.renderHead();
            this.valueLayer.replaceChildren(f.el);
            this.valueFrame = f;
            f.render('near');
            f.el.style.width = f.size.w + 'px';
            this.keepValueSeen();
            this.markProblems();
        }

        // A value's graph stays all on screen as it grows, where the screen has the room
        keepValueSeen() {
            const f = this.valueFrame;
            if (!f) return;
            const r = this.el.getBoundingClientRect();
            const box = f.el.getBoundingClientRect();
            let dx = box.right > r.right - 12 ? r.right - 12 - box.right : 0;
            if (box.left + dx < r.left + 8) dx = r.left + 8 - box.left;
            let dy = box.bottom > r.bottom - 12 ? r.bottom - 12 - box.bottom : 0;
            if (box.top + dy < r.top + 8) dy = r.top + 8 - box.top;
            if (Math.abs(dx) < 1 && Math.abs(dy) < 1) return;
            this.pan = { x: Math.round(this.pan.x + dx), y: Math.round(this.pan.y + dy) };
            this.applyTransform();
            this.scheduleCull();
        }

        // Closes it; a value left with nothing in it isn't kept
        closeValue(tidy = true) {
            const f = this.valueFrame;
            if (!f) return;
            this.valueFrame = null;
            this.valueLayer.replaceChildren();
            const ed = this.editor;
            if (G.slotKey(ed.slot) === f.key) ed.clearSelection();
            if (!tidy) return;
            const g = f.graph;
            const v = f.value;
            if (g && !g.root && !Doc.nodes(g).length && v.holder && v.key && !v.required) {
                this.app.edit(() => G.json.set(v.holder, v.key, undefined, v.order));
            }
        }

        // ---------------------------------------------------------------------------------------------------------
        // The pointer

        listen() {
            const c = this.el;
            c.addEventListener('pointerdown', e => this.down(e));
            c.addEventListener('pointermove', e => this.move(e));
            c.addEventListener('pointerup', e => this.up(e));
            c.addEventListener('pointercancel', e => { this.pointers.delete(e.pointerId); this.cancel(); });
            c.addEventListener('contextmenu', e => {
                e.preventDefault();
                if (this.panned) { this.panned = false; return; }
                this.context(e);
            });
            c.addEventListener('dblclick', e => {
                if (e.target.closest('.tg-node, .tg-note, .tg-card, .tg-frame-head, .tg-region-head, .tg-start, input, textarea, select, button')) return;
                this.openAdd(e.clientX, e.clientY, e.target);
            });
            c.addEventListener('wheel', e => this.wheel(e), { passive: false });
            c.addEventListener('pointerover', e => this.over(e));
            c.addEventListener('focusout', () => { if (this.stale) requestAnimationFrame(() => { if (this.stale) this.redrawFrames(); }); });
            c.addEventListener('pointerleave', () => { tooltip(0, 0, null); this.light(c); });
        }

        // What a press on nothing in particular opens: the node search inside a frame, the Add menu on the board
        openAdd(clientX, clientY, target) {
            if (!this.editor || this.editor.error) return;
            const f = this.frameAt(target);
            if (f) {
                if (!f.graph && !f.isValue) { toast(`${this.schema.keyLabel('extern')}: this one has no nodes`); return; }
                if (f.mode !== 'near') { this.reveal(f.item.id); return; }
                G.pickers.nodeSearch(this.app, f, clientX, clientY, null);
                return;
            }
            const region = target?.closest?.('.tg-region');
            const namespace = region ? this.regions.get(Number(region.dataset.id))?.item.value : null;
            G.pickers.addMenu(this.app, clientX, clientY, { at: this.toWorld(clientX, clientY), namespace });
        }

        portAt(frame, target) {
            const row = target?.closest?.('.tg-port');
            if (!row || !frame || !frame.el.contains(row)) return null;
            return frame.ports.get(row.dataset.port)?.port ?? null;
        }

        down(e) {
            this.lastPointer = { x: e.clientX, y: e.clientY };
            if (!this.editor || this.editor.error) return;
            this.pointers.set(e.pointerId, { x: e.clientX, y: e.clientY });
            if (this.pointers.size === 2) {
                // Two fingers: pinch to zoom, and move together to pan
                this.cancel();
                const [a, b] = [...this.pointers.values()];
                this.drag = { kind: 'pinch', distance: Math.hypot(a.x - b.x, a.y - b.y) || 1, zoom: this.zoom, mid: { x: (a.x + b.x) / 2, y: (a.y + b.y) / 2 }, pan: { ...this.pan } };
                return;
            }
            if (e.target.closest('input, textarea, select, button, .tg-check, .tg-start')) return;
            this.el.focus({ preventScroll: true });
            const start = { x: e.clientX, y: e.clientY };
            this.panned = false;
            if (e.button === 1 || e.button === 2 || (e.button === 0 && this.app.spaceDown)) {
                this.drag = { kind: 'pan', start, pan: { ...this.pan }, button: e.button };
                capture(this.el, e);
                return;
            }
            if (e.button !== 0) return;
            const frame = this.frameAt(e.target);
            if (frame && frame.mode === 'near') {
                // A wire starts at an output or an arrow, or is taken off an input that has one: an input with a value
                // shown in it, or nothing yet, is part of its node (it's pressed to move the node, and clicked to pick)
                let port = this.portAt(frame, e.target);
                if (port && port.input && !port.exec && !frame.hasNodeWire(port)) port = null;
                if (port) {
                    if (e.altKey && port.input && frame.wireOf(port)) { frame.unwireNow(port); return; }
                    this.beginWire(frame, port, e);
                    capture(this.el, e);
                    return;
                }
                const noteEl = e.target.closest('.tg-note');
                if (noteEl) {
                    const id = Number(noteEl.dataset.note);
                    const note = frame.views.get(id)?.node;
                    if (!note) return;
                    if (e.target.classList.contains('tg-note-size')) {
                        this.drag = { kind: 'size', start, frame, note, w: note.w || 200, h: note.h || 120 };
                    } else {
                        this.pickNode(frame, id, e);
                        this.beginMove(frame, start);
                    }
                    capture(this.el, e);
                    return;
                }
                const nodeEl = e.target.closest('.tg-node');
                if (nodeEl) {
                    const id = Number(nodeEl.dataset.id);
                    if (id < 0) {
                        // The entry and the result stay where they are: a press on them is a press on the frame
                        if (!frame.isValue) { this.pickItem(frame.item.id, e); this.beginMoveItems(start); capture(this.el, e); }
                        return;
                    }
                    this.pickNode(frame, id, e);
                    this.beginMove(frame, start);
                    capture(this.el, e);
                    return;
                }
            }
            const handle = e.target.closest('[data-handle]');
            const cardEl = e.target.closest('.tg-card');
            if ((frame && !frame.isValue && (handle || frame.mode === 'far')) || cardEl) {
                const id = cardEl ? Number(cardEl.dataset.id) : frame.item.id;
                this.pickItem(id, e);
                this.beginMoveItems(start);
                capture(this.el, e);
                return;
            }
            if (frame?.isValue && handle) {
                this.drag = { kind: 'move-value', start, frame, x: frame.x, y: frame.y };
                capture(this.el, e);
                return;
            }
            if (e.shiftKey) {
                // A box to select with: a frame's nodes inside it, the board's cards and frames outside
                this.drag = { kind: 'band', start, frame: frame && frame.mode === 'near' ? frame : null, before: new Set(frame ? this.editor.selection : this.editor.picked) };
                capture(this.el, e);
                return;
            }
            // Nothing: the board moves with the pointer
            this.drag = { kind: 'pan', start, pan: { ...this.pan }, button: 0, frame };
            capture(this.el, e);
        }

        pickNode(frame, id, e) {
            const ed = this.editor;
            const same = G.slotKey(ed.slot) === frame.key;
            if (e.shiftKey || e.ctrlKey || e.metaKey) {
                if (same && ed.selection.has(id)) { ed.selection.delete(id); ed.emit('selection'); }
                else ed.selectNodes(frame.slot, [id], { add: true });
            } else if (!same || !ed.selection.has(id)) {
                ed.selectNodes(frame.slot, [id]);
            }
        }

        pickItem(id, e) {
            const ed = this.editor;
            if (e.shiftKey || e.ctrlKey || e.metaKey) {
                if (ed.picked.has(id)) { ed.picked.delete(id); ed.emit('selection'); }
                else ed.pick([id], { add: true });
            } else if (!ed.picked.has(id)) {
                ed.pick([id]);
            }
        }

        beginMove(frame, start) {
            const ed = this.editor;
            const g = frame.graph;
            const moving = new Map();
            const { nodes, notes } = ed.selectedWithHidden(g);
            for (const id of [...nodes, ...notes]) {
                const v = ed.doc.find(id)?.value;
                if (v) moving.set(id, { x: Number(v.x) || 0, y: Number(v.y) || 0 });
            }
            this.drag = { kind: 'move', start, frame, moving, moved: false };
        }

        beginMoveItems(start) {
            const moving = new Map();
            for (const id of this.editor.picked) {
                const c = this.cards.get(id), f = this.frames.get(id);
                if (c) moving.set(id, { x: c.x, y: c.y });
                else if (f) moving.set(id, { x: f.x, y: f.y });
            }
            this.drag = { kind: 'move-items', start, moving, moved: false };
        }

        // Starts a wire from a port. A wired input (or a statement's exec in) gives up its wire to be dragged on
        // from where it comes; an output starts a new one.
        beginWire(frame, port, e) {
            const g = frame.graph;
            const s = this.schema;
            let from = port, fromKey = portKey(port), taken = false;
            if (port.input && g) {
                let source = null, key = null;
                if (port.exec) {
                    if (port.node > 0) {
                        const src = this.editor.doc.execSourceOf(g, port.node);
                        if (src) source = { node: src.node, input: false, exec: true, path: src.path, label: '' };
                        else if (g.entry === port.node) source = { node: ENTRY, input: false, exec: true, path: null };
                    }
                } else {
                    const w = frame.wireOf(port);
                    if (w) {
                        source = { node: w, input: false, exec: false, path: null };
                        key = frame.sourceKey(w);
                    }
                }
                if (source) {
                    // Take it off; dropping it again on a port wires it there
                    this.pendingUnwire = { port, old: port.exec ? 0 : frame.wireOf(port) };
                    from = source;
                    fromKey = key || portKey(source);
                    taken = true;
                }
            }
            const a = frame.at(fromKey) || frame.at(portKey(port)) || { x: 0, y: 0 };
            this.drag = { kind: 'wire', frame, from, exec: from.exec, a, to: a, start: { x: e.clientX, y: e.clientY }, taken };
            frame.el.classList.add('tg-wiring');
        }

        move(e) {
            this.lastPointer = { x: e.clientX, y: e.clientY };
            if (this.pointers.has(e.pointerId)) this.pointers.set(e.pointerId, { x: e.clientX, y: e.clientY });
            const d = this.drag;
            if (!d) return;
            if (d.kind === 'pinch') {
                if (this.pointers.size < 2) return;
                const [a, b] = [...this.pointers.values()];
                const distance = Math.hypot(a.x - b.x, a.y - b.y) || 1;
                const mid = { x: (a.x + b.x) / 2, y: (a.y + b.y) / 2 };
                const r = this.el.getBoundingClientRect();
                const zoom = Math.max(MIN_ZOOM, Math.min(MAX_ZOOM, d.zoom * distance / d.distance));
                const mx = d.mid.x - r.left, my = d.mid.y - r.top;
                this.pan = { x: mid.x - r.left - (mx - d.pan.x) * zoom / d.zoom, y: mid.y - r.top - (my - d.pan.y) * zoom / d.zoom };
                this.zoom = zoom;
                this.applyTransform();
                this.scheduleCull();
                return;
            }
            const dx = e.clientX - d.start.x, dy = e.clientY - d.start.y;
            const z = this.zoom;
            if (d.kind === 'pan') {
                if (!d.moved && Math.abs(dx) + Math.abs(dy) < 4) return;
                d.moved = true;
                this.pan = { x: d.pan.x + dx, y: d.pan.y + dy };
                this.el.classList.add('tg-panning');
                this.applyTransform();
                this.scheduleCull();
            } else if (d.kind === 'move') {
                if (!d.moved && Math.abs(dx) + Math.abs(dy) < 3) return;
                d.moved = true;
                const f = d.frame;
                for (const [id, at] of d.moving) {
                    const p = { x: Math.round(at.x + dx / z), y: Math.round(at.y + dy / z) };
                    f.places.set(id, p);
                    const v = f.views.get(id);
                    if (v) {
                        v.el.style.left = f.nodeX + p.x - f.origin.x + 'px';
                        v.el.style.top = G.frames.PAD + p.y - f.origin.y + 'px';
                    }
                }
                f.drawWires();
            } else if (d.kind === 'move-items') {
                if (!d.moved && Math.abs(dx) + Math.abs(dy) < 3) return;
                d.moved = true;
                for (const [id, at] of d.moving) {
                    const target = this.cards.get(id) || this.frames.get(id);
                    target.x = Math.round(at.x + dx / z);
                    target.y = Math.round(at.y + dy / z);
                    target.el.style.left = target.x + 'px';
                    target.el.style.top = target.y + 'px';
                }
            } else if (d.kind === 'move-value') {
                d.frame.x = Math.round(d.x + dx / z);
                d.frame.y = Math.round(d.y + dy / z);
                d.frame.el.style.left = d.frame.x + 'px';
                d.frame.el.style.top = d.frame.y + 'px';
            } else if (d.kind === 'size') {
                const w = Math.max(80, Math.round(d.w + dx / z)), h = Math.max(40, Math.round(d.h + dy / z));
                d.size = { w, h };
                const v = d.frame.views.get(d.note.id);
                v.el.style.width = w + 'px';
                v.el.style.height = h + 'px';
            } else if (d.kind === 'wire') {
                const f = d.frame;
                const r = f.body.getBoundingClientRect();
                d.to = { x: (e.clientX - r.left) / z, y: (e.clientY - r.top) / z };
                const over = document.elementFromPoint(e.clientX, e.clientY) || e.target;
                const port = this.portAt(f, over);
                for (const row of this.el.querySelectorAll('.tg-port.tg-target')) row.classList.remove('tg-target');
                if (port && f.canJoin(d.from, port)) f.ports.get(portKey(port))?.row?.classList.add('tg-target');
                // Outside its frame a wire has nowhere to go
                const outside = !f.el.contains(over);
                f.el.classList.toggle('tg-wire-out', outside);
                f.drawWires();
            } else if (d.kind === 'band') {
                const r = this.el.getBoundingClientRect();
                const x0 = Math.min(d.start.x, e.clientX), y0 = Math.min(d.start.y, e.clientY);
                const x1 = Math.max(d.start.x, e.clientX), y1 = Math.max(d.start.y, e.clientY);
                Object.assign(this.band.style, { display: 'block', left: x0 - r.left + 'px', top: y0 - r.top + 'px', width: x1 - x0 + 'px', height: y1 - y0 + 'px' });
                const inside = b => b.right >= x0 && b.left <= x1 && b.bottom >= y0 && b.top <= y1;
                const ed = this.editor;
                if (d.frame) {
                    const sel = new Set(G.slotKey(ed.slot) === d.frame.key ? d.before : []);
                    for (const [id, v] of d.frame.views) if (id > 0 && inside(v.el.getBoundingClientRect())) sel.add(id);
                    ed.slot = d.frame.slot;
                    ed.picked.clear();
                    ed.selection = sel;
                } else {
                    const sel = new Set(d.before);
                    for (const [id, c] of this.cards) if (inside(c.el.getBoundingClientRect())) sel.add(id);
                    for (const [id, f] of this.frames) if (inside(f.el.getBoundingClientRect())) sel.add(id);
                    ed.selection.clear();
                    ed.picked = sel;
                }
                this.showSelection();
            }
        }

        up(e) {
            this.pointers.delete(e.pointerId);
            const d = this.drag;
            this.drag = null;
            if (!d) return;
            try { this.el.releasePointerCapture(e.pointerId); } catch {}
            this.el.classList.remove('tg-panning');
            const ed = this.editor;
            if (d.kind === 'pinch') return;
            if (d.kind === 'pan') {
                if (d.moved) {
                    // A right drag is a pan, not a menu
                    this.panned = d.button === 2;
                    return;
                }
                if (d.button === 0) {
                    // A click on nothing: nothing is selected, and a value's graph closes
                    if (this.valueFrame && !this.valueFrame.el.contains(e.target)) this.app.closeValue();
                    ed.clearSelection();
                }
                return;
            }
            if (d.kind === 'band') {
                this.band.style.display = 'none';
                ed.emit('selection');
                return;
            }
            if (d.kind === 'move') {
                if (!d.moved) return;
                const f = d.frame;
                const places = new Map(f.places);
                f.places.clear();
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
            if (d.kind === 'move-items') {
                if (!d.moved) return;
                const moved = new Map([...d.moving.keys()].map(id => [id, this.cards.get(id) || this.frames.get(id)]));
                this.app.edit(doc => {
                    this.persistPlaces(doc);
                    for (const item of doc.items()) {
                        const at = moved.get(item.id);
                        if (at && !item.region) {
                            this.auto.set(item.id, { x: at.x, y: at.y });
                            this.setPlace(doc, item, at.x, at.y);
                        }
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
                const f = d.frame;
                f.el.classList.remove('tg-wiring', 'tg-wire-out');
                for (const row of this.el.querySelectorAll('.tg-port.tg-target')) row.classList.remove('tg-target');
                const pending = this.pendingUnwire;
                this.pendingUnwire = null;
                const over = document.elementFromPoint(e.clientX, e.clientY) || e.target;
                const port = this.portAt(f, over);
                const moved = Math.abs(e.clientX - d.start.x) + Math.abs(e.clientY - d.start.y) > 4;
                const inFrame = f.el.contains(over);
                if (port && f.canJoin(d.from, port)) {
                    let why = null;
                    this.app.edit(doc => {
                        if (pending) {
                            // Dropped back where it was: nothing changes
                            if (portKey(pending.port) === portKey(port)) return;
                            f.unwire(doc, pending.port);
                        }
                        why = f.join(doc, d.from, port);
                        doc.reindex();
                        if (pending?.old && !why) doc.dropValue(f.graph, pending.old);
                    });
                    if (why) toast(`That wire can't be: ${why}.`);
                    return;
                }
                if (d.taken) {
                    // Dropped on nothing: the wire is gone
                    if (moved && pending) f.unwireNow(pending.port);
                    else f.drawWires();
                    return;
                }
                f.drawWires();
                if (!moved) {
                    // A click on a pin: what to do with it
                    G.pickers.nodeSearch(this.app, f, e.clientX, e.clientY, { from: d.from });
                    return;
                }
                if (!inFrame) {
                    const other = this.frameAt(over);
                    toast(other ? "A wire can't leave its frame. To use a value in other code, keep it in a component or send it with an event."
                        : "A wire stays inside its frame: drop it on a pin, or inside the frame to add what it goes to.");
                    return;
                }
                G.pickers.nodeSearch(this.app, f, e.clientX, e.clientY, { from: d.from });
            }
        }

        cancel() {
            const d = this.drag;
            this.drag = null;
            this.pendingUnwire = null;
            this.band.style.display = 'none';
            this.el.classList.remove('tg-panning');
            if (!d) return;
            if (d.kind === 'move') {
                d.frame.places.clear();
                this.render();
            } else if (d.kind === 'move-items') {
                this.render();
            } else if (d.kind === 'wire') {
                d.frame.el.classList.remove('tg-wiring', 'tg-wire-out');
                d.frame.drawWires();
            }
        }

        wheel(e) {
            e.preventDefault();
            if (e.ctrlKey || e.metaKey) {
                this.zoomAt(Math.exp(-e.deltaY * 0.0025), e.clientX, e.clientY);
                return;
            }
            const unit = e.deltaMode === 1 ? 32 : 1;
            this.pan = { x: this.pan.x - (e.shiftKey ? e.deltaY : e.deltaX) * unit, y: this.pan.y - (e.shiftKey ? 0 : e.deltaY) * unit };
            this.applyTransform();
            this.scheduleCull();
        }

        // A right click (or a long press): what can be done there
        context(e) {
            if (!this.editor || this.editor.error) return;
            const target = e.target;
            if (target.closest('.tg-start')) return;
            const frame = this.frameAt(target);
            const x = e.clientX, y = e.clientY;
            if (frame && frame.mode === 'near') {
                const port = this.portAt(frame, target);
                if (port && frame.portMenu(port, x, y)) return;
                const nodeEl = target.closest('.tg-node');
                const id = nodeEl ? Number(nodeEl.dataset.id) : 0;
                if (id > 0) {
                    frame.nodeMenu(frame.views.get(id).node, x, y);
                    return;
                }
                const noteEl = target.closest('.tg-note');
                if (noteEl) {
                    const nid = Number(noteEl.dataset.note);
                    this.editor.selectNodes(frame.slot, [nid]);
                    menu(x, y, [{ label: 'Delete the note', danger: true, run: () => this.app.command('delete') }]);
                    return;
                }
                if (!frame.isValue && target.closest('.tg-frame-head')) {
                    G.cards.moreMenu(this.app, frame.item, x, y);
                    return;
                }
                if (id === ENTRY || id === RESULT) return;
            } else if (frame && !frame.isValue) {
                G.cards.moreMenu(this.app, frame.item, x, y);
                return;
            }
            const cardEl = target.closest('.tg-card');
            if (cardEl) {
                const c = this.cards.get(Number(cardEl.dataset.id));
                if (c) G.cards.moreMenu(this.app, c.item, x, y);
                return;
            }
            this.openAdd(x, y, target);
        }

        // What the pointer is over says what it is: a problem's words, a node's type and what tidels says of it
        over(e) {
            if (this.drag) return;
            if (this.stale) this.redrawFrames();
            this.light(e.target);
            // A list is open over the board: nothing else is said meanwhile
            if (G.ui.popupOpen()) { this.hover = null; return; }
            const wrong = e.target.closest?.('[data-problem]');
            if (wrong) {
                const parts = [];
                let errors = false;
                for (const one of wrong.dataset.problem.split('\u0001')) {
                    const [first, ...rest] = one.slice(1).split('\n');
                    if (one[0] === 'E') errors = true;
                    parts.push({ kind: 'problem', text: first });
                    if (rest.length) parts.push({ kind: 'note', text: rest.join('\n') });
                }
                tooltip(e.clientX, e.clientY, parts, errors ? 'problem' : 'warning');
                this.hover = wrong;
                return;
            }
            const nodeEl = e.target.closest?.('.tg-node');
            if (nodeEl === this.hover) return;
            this.hover = nodeEl;
            tooltip(0, 0, null);
            const id = nodeEl ? Number(nodeEl.dataset.id) : 0;
            if (id <= 0 || e.target.closest('input, textarea, select, button')) return;
            const frame = this.frameAt(nodeEl);
            const lang = this.app.lang;
            if (!frame || lang.status !== 'checked') return;
            const n = frame.analysis.byId.get(frame.analysis.merged.get(id) ?? id);
            if (!n) return;
            const x = e.clientX, y = e.clientY;
            clearTimeout(this.hoverTimer);
            this.hoverTimer = setTimeout(() => {
                if (this.hover !== nodeEl) return;
                lang.hover(n).then(text => {
                    if (this.hover !== nodeEl || !text || G.ui.popupOpen()) return;
                    tooltip(x, y, text.replace(/```\w*\n?/g, '').trim());
                });
            }, 450);
        }
    }

    // Keeps the pointer's events on the board while a drag goes on
    function capture(target, e) {
        try { target.setPointerCapture(e.pointerId); } catch {}
        if (e.pointerType !== 'touch') e.preventDefault();
    }

    G.Board = Board;
})(globalThis.TideGraph = globalThis.TideGraph || {});
