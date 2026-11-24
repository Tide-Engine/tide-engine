// The graph editor's page: the outline, the canvas, the inspector and the Tide panel, and its one connection to
// the host (VS Code's custom editor, JetBrains' JCEF editor, a test). The host's side is a few messages:
//
//   page -> host  {type: 'ready'}                          the page is there: send 'load'
//                 {type: 'edit', text}                     the document's new text (each edit, undo and redo)
//                 {type: 'request', id, method, params}    to tidels (tide/graph*), or the host's own
//                                                          (host/clipboardRead); answered with 'response'
//                 {type: 'clipboardWrite', text}           copied nodes
//                 {type: 'save'} {type: 'showTide', text}  {type: 'error', message}
//   host -> page  {type: 'load', text, uri, name, schema}  the document; `schema` the host's copy, which
//                                                          tidels's (tide/graphSchema) replaces when it answers
//                 {type: 'text', text}                     the document changed (by the host's undo, a checkout...)
//                 {type: 'diagnostics', diagnostics}       tidels's for the document, as published (data.node)
//                 {type: 'response', id, result | error}   {type: 'command', name}  {type: 'theme', ...}
//
// TideGraph.start(host) makes the page, where host.post(message) sends to the host; the host calls
// TideGraph.receive(message).
(function (G) {
    'use strict';
    const { el, button, menu, closeMenu, tooltip } = G.ui;
    const { Doc, Schema, Editor, Lang, Canvas, Inspector, Outline, Search } = G;

    class App {
        constructor(host, root) {
            this.host = host;
            this.root = root || document.body;
            this.pending = new Map();
            this.nextRequest = 1;
            this.clip = null;
            this.spaceDown = false;
            this.showPanel = true;
            this.lang = new Lang((method, params) => this.request(method, params));
            this.editor = null;
            this.build();
            this.lang.on(what => this.langChanged(what));
            this.post({ type: 'ready' });
        }

        post(message) {
            try {
                this.host.post(message);
            } catch (e) {
                console.error('Tide graph: the host refused a message', e);
            }
        }

        // Asks the host; resolves with the answer, rejects with an error's message
        request(method, params) {
            const id = this.nextRequest++;
            return new Promise((resolve, reject) => {
                this.pending.set(id, { resolve, reject });
                this.post({ type: 'request', id, method, params });
            });
        }

        receive(m) {
            if (!m || typeof m !== 'object') return;
            switch (m.type) {
                case 'load': return this.load(m);
                case 'text': if (this.editor) { this.editor.external(m.text); this.checkSoon(); } return;
                case 'diagnostics': return this.lang.setDiagnostics(m.diagnostics);
                case 'response': {
                    const p = this.pending.get(m.id);
                    if (!p) return;
                    this.pending.delete(m.id);
                    if (m.error !== undefined && m.error !== null) p.reject(new Error(typeof m.error === 'string' ? m.error : m.error.message || 'failed'));
                    else p.resolve(m.result);
                    return;
                }
                case 'command': return this.command(m.name, m);
                case 'theme': return this.theme(m);
                case 'reveal': return this.reveal(m.node);
                case 'server':
                    if (!m.available) {
                        this.lang.status = 'unavailable';
                        this.lang.why = m.message || null;
                        this.langChanged('status');
                    } else this.checkSoon(0);
                    return;
                default: return;
            }
        }

        load(m) {
            let schemaJson = m.schema;
            if (typeof schemaJson === 'string') {
                try { schemaJson = JSON.parse(schemaJson); } catch { schemaJson = null; }
            }
            this.name = m.name || 'graph';
            this.lang.uri = m.uri || null;
            if (!schemaJson && !this.editor) {
                // No copy of the host's: tidels's, or nothing to edit with
                this.banner('Asking tidels for the graph schema…');
                this.request('tide/graphSchema', null).then(s => {
                    if (s && s.nodes) this.load({ ...m, schema: s });
                    else this.banner('There is no graph schema: tidels gave none.');
                }, e => this.banner(`There is no graph schema: ${e.message}. Tidels, the language server that comes with tide, gives it: install tide, then open the file again.`));
                return;
            }
            if (schemaJson && (!this.editor || m.schema)) {
                const fresh = !this.editor;
                if (fresh) {
                    this.editor = new Editor(new Schema(schemaJson), msg => this.post(msg));
                    this.editor.on(what => this.changed(what));
                } else {
                    this.editor.setSchema(new Schema(schemaJson));
                }
            }
            if (!this.editor) return;
            this.editor.load(m.text ?? '', true);
            this.fillTypes();
            // tidels's own schema, when it's there, is the one its tidec reads
            this.request('tide/graphSchema', null).then(s => {
                if (s && s.nodes && JSON.stringify(s) !== JSON.stringify(this.editor.schema.json)) this.editor.setSchema(new Schema(s));
            }, () => {});
            this.checkSoon(0);
        }

        // ---------------------------------------------------------------------------------------------------------
        // The page

        build() {
            const root = this.root;
            root.classList.add('tg-root');
            this.types = el('datalist', { id: 'tg-types' });
            this.title = el('span.tg-crumb');
            this.statusText = el('span.tg-status');
            this.undoButton = button('Undo', () => this.command('undo'), 'Undo (Ctrl+Z)');
            this.redoButton = button('Redo', () => this.command('redo'), 'Redo (Ctrl+Y)');
            this.panelButton = button('Tide', () => { this.showPanel = !this.showPanel; this.layoutPanes(); this.renderPanel(); }, 'Show the Tide the graph stands for, and its problems');
            const bar = el('div.tg-toolbar', null,
                button('+ Node', ev => this.openSearch(ev.clientX, ev.clientY + 16, null), 'Add a node (Space, or a right click on the canvas)'),
                button('+ Note', () => this.addNote(null), 'A box to write in, behind the nodes'),
                button('Layout', () => this.autoLayout(), 'Lay out the graph shown as tidec does'),
                button('Frame', () => this.command('frame'), 'Frame the selection, or the whole graph (F)'),
                el('span.tg-sep'), this.undoButton, this.redoButton, el('span.tg-sep'),
                this.panelButton,
                button('Show as Tide', () => this.post({ type: 'showTide', text: this.lang.text }), 'Open the Tide the graph stands for'),
                el('span.tg-sep'), this.title, el('span.tg-grow'), this.statusText);
            this.bannerEl = el('div.tg-banner');
            this.outline = new Outline(this);
            this.canvas = new Canvas(this);
            this.inspector = new Inspector(this);
            this.search = new Search(this);
            this.panel = el('div.tg-panel');
            const split = (cls, onMove) => {
                const s = el('div.' + cls);
                s.addEventListener('pointerdown', e => {
                    e.preventDefault();
                    s.setPointerCapture(e.pointerId);
                    const move = ev => onMove(ev);
                    const up = () => { s.removeEventListener('pointermove', move); s.removeEventListener('pointerup', up); };
                    s.addEventListener('pointermove', move);
                    s.addEventListener('pointerup', up);
                });
                return s;
            };
            this.sizes = { left: 230, right: 290, bottom: 200 };
            const left = split('tg-split.tg-split-v', e => { this.sizes.left = Math.max(120, Math.min(600, e.clientX)); this.layoutPanes(); });
            const right = split('tg-split.tg-split-v', e => { this.sizes.right = Math.max(160, Math.min(700, innerWidth - e.clientX)); this.layoutPanes(); });
            this.bottomSplit = split('tg-split.tg-split-h', e => { this.sizes.bottom = Math.max(60, Math.min(innerHeight - 160, innerHeight - e.clientY)); this.layoutPanes(); });
            this.main = el('div.tg-main', null, this.outline.el, left, el('div.tg-center', null, this.canvas.el), right, this.inspector.el);
            root.append(this.types, bar, this.bannerEl, this.main, this.bottomSplit, this.panel);
            this.layoutPanes();
            this.keys();
            addEventListener('resize', () => this.canvas.applyTransform());
            this.banner(null);
        }

        layoutPanes() {
            this.root.style.setProperty('--tg-left', this.sizes.left + 'px');
            this.root.style.setProperty('--tg-right', this.sizes.right + 'px');
            this.root.style.setProperty('--tg-bottom', (this.showPanel ? this.sizes.bottom : 0) + 'px');
            this.panel.style.display = this.showPanel ? '' : 'none';
            this.bottomSplit.style.display = this.showPanel ? '' : 'none';
            this.panelButton.classList.toggle('tg-on', this.showPanel);
        }

        banner(text) {
            this.bannerEl.textContent = text || '';
            this.bannerEl.style.display = text ? 'block' : 'none';
        }

        fillTypes() {
            const types = new Set([...this.editor.schema.types, ...this.editor.doc.declaredTypes()]);
            this.types.replaceChildren(...[...types].map(t => el('option', { value: t })));
        }

        // The editor's changes: what to draw again
        changed(what) {
            const ed = this.editor;
            if (what === 'document') {
                this.banner(ed.error ? `This file isn't a graph the editor can read (${ed.error}). Fix it in the text editor; the graph comes back when it reads again.` : null);
                this.fillTypes();
                this.renderAll();
            } else if (what === 'graph') {
                this.canvas.render();
                this.inspector.render();
                this.outline.render();
                this.renderTitle();
                this.renderPanel();
            } else if (what === 'selection') {
                this.canvas.showSelection();
                this.inspector.render();
                this.outline.render();
                this.renderPanel();
            }
            this.undoButton.disabled = !ed.canUndo;
            this.redoButton.disabled = !ed.canRedo;
        }

        renderAll() {
            this.outline.render();
            this.canvas.render();
            this.inspector.render();
            this.renderTitle();
            this.renderPanel();
            this.renderStatus();
        }

        renderTitle() {
            const ed = this.editor;
            const where = ed.slot ? ed.doc.find(ed.slot.owner) : null;
            this.title.textContent = where ? `${ed.doc.describe(where)} › ${ed.slot.key === 'body' ? 'code' : ed.slot.key}${ed.slot.index >= 0 ? ' ' + (ed.slot.index + 1) : ''}` : '';
        }

        langChanged(what) {
            if (!this.editor) return;
            if (what === 'check' || what === 'diagnostics') {
                this.canvas.render();
                this.outline.render();
                if (this.editor.selected && this.editor.doc.find(this.editor.selected)?.role === 'node') this.inspector.render();
                this.renderPanel();
            }
            this.renderStatus();
        }

        renderStatus() {
            const l = this.lang;
            const errors = l.diagnostics.filter(d => d.severity === 1).length;
            const warnings = l.diagnostics.length - errors;
            const said = errors || warnings ? ` · ${errors} error${errors === 1 ? '' : 's'}${warnings ? `, ${warnings} warning${warnings === 1 ? '' : 's'}` : ''}` : '';
            const text = l.status === 'checked' ? 'Checked by tidels' + said
                : l.status === 'checking' ? 'Checking…' + said
                : l.status === 'unavailable' ? 'Not checked: ' + (l.why || 'no tidels')
                : '';
            this.statusText.textContent = text;
            this.statusText.className = 'tg-status' + (errors ? ' tg-status-error' : l.status === 'unavailable' ? ' tg-status-off' : '');
        }

        // The Tide the graph stands for, the selected element's part marked, and the problems on top
        renderPanel() {
            if (!this.showPanel) return;
            const l = this.lang;
            const ed = this.editor;
            const list = el('div.tg-problems');
            for (const d of l.diagnostics) {
                const n = d.node ? ed?.doc.find(d.node) : null;
                list.append(el('div.tg-problem' + (d.severity === 1 ? '.tg-error' : '.tg-warning'), {
                    onclick: () => d.node && this.reveal(d.node),
                    title: d.node ? 'Go to the node' : '',
                }, el('span.tg-problem-mark', { text: d.severity === 1 ? '●' : '▲' }), el('span', { text: d.message }),
                n ? el('span.tg-problem-where', { text: ed.doc.describe(n) }) : null));
            }
            const pre = el('pre.tg-tide');
            if (l.text !== null) {
                const mark = ed?.selected ? l.span(ed.selected) : null;
                const text = l.text;
                const errorSpans = l.diagnostics.filter(d => d.node).map(d => l.span(d.node)).filter(Boolean);
                const cuts = new Set([0, text.length]);
                if (mark) { cuts.add(mark[0]); cuts.add(mark[1]); }
                for (let i = text.indexOf('\n'); i >= 0; i = text.indexOf('\n', i + 1)) cuts.add(i + 1);
                const sorted = [...cuts].sort((a, b) => a - b);
                for (let i = 0; i < sorted.length - 1; i++) {
                    const a = sorted[i], b = sorted[i + 1];
                    if (a === b) continue;
                    const marked = mark && a >= mark[0] && b <= mark[1];
                    const wrong = errorSpans.some(s => a >= s[0] && b <= s[1]);
                    pre.append(el('span' + (marked ? '.tg-mark' : '') + (wrong ? '.tg-wrong' : ''), { dataset: { at: a }, text: text.slice(a, b) }));
                }
                pre.addEventListener('click', () => {
                    const sel = getSelection();
                    const node = sel?.focusNode;
                    const part = node?.nodeType === 3 ? node.parentElement : node;
                    if (!part?.dataset?.at) return;
                    const id = l.idAt(Number(part.dataset.at) + (sel.focusOffset || 0));
                    if (id) this.reveal(id);
                });
                requestAnimationFrame(() => pre.querySelector('.tg-mark')?.scrollIntoView?.({ block: 'nearest' }));
            } else {
                pre.append(el('span.tg-muted', { text: l.status === 'checking' ? 'Asking tidels…' : `The Tide isn't shown: ${l.why || 'tidels has not answered'}.` }));
            }
            this.panel.replaceChildren(list, pre);
        }

        // Selects an element and shows it: a node in the middle of its graph
        reveal(id) {
            const ed = this.editor;
            if (!ed?.doc.find(id)) return;
            ed.select(id);
            requestAnimationFrame(() => this.canvas.reveal(id));
        }

        say(message) {
            if (!message) return;
            this.statusText.textContent = message;
            this.statusText.className = 'tg-status tg-status-said';
            clearTimeout(this.sayTimer);
            this.sayTimer = setTimeout(() => this.renderStatus(), 4000);
        }

        // ---------------------------------------------------------------------------------------------------------
        // Edits

        edit(change) {
            if (!this.editor) return undefined;
            const result = this.editor.edit(change);
            this.checkSoon();
            return result;
        }

        checkSoon(delay = 300) {
            clearTimeout(this.checkTimer);
            this.checkTimer = setTimeout(() => this.lang.check(), delay);
        }

        openSearch(clientX, clientY, context) {
            if (!this.editor?.graph) {
                this.say('Show a graph first: select a declaration with code, or give one code in the inspector');
                return;
            }
            this.search.show(clientX, clientY, context);
        }

        // Adds what the search picked at a place, wired to where the search came from
        addFromSearch(entry, at, context) {
            const ed = this.editor;
            const s = ed.schema;
            const g = ed.graph;
            const from = context?.from;
            const { ENTRY, RESULT } = Canvas;
            let made = null;
            this.edit(doc => {
                const node = doc.newNode(entry.node.k, at.x, at.y, entry.node.fields);
                Doc.nodes(g).push(node);
                let statement = s.isExec(node.k) ? node : null;
                // A call where a statement goes: the statement that does it
                if (!statement && (from?.exec || (entry.statement && !from))) {
                    statement = doc.newNode('eval', at.x + 240, at.y);
                    Doc.nodes(g).push(statement);
                    node.y = at.y + 100;
                    doc.connect(g, statement.id, ['value'], node.id);
                }
                doc.reindex();
                if (from && !from.exec && !from.input) {
                    // A value dragged out: into the new node's first input
                    const input = s.ports(node).find(p => p.input && !p.exec);
                    if (input) say(doc.connect(g, node.id, input.path, from.node));
                } else if (from && !from.exec && from.input) {
                    if (from.node === RESULT) say(doc.setRoot(g, node.id));
                    else if (s.hasValue(node.k)) say(doc.connect(g, from.node, from.path, node.id));
                } else if (from?.exec && statement) {
                    if (!from.input) {
                        // After an exec output: in between it and what it went to
                        const was = from.node === ENTRY ? g.entry : s.wire(Doc.nodeIn(g, from.node), from.path);
                        say(from.node === ENTRY ? doc.setEntry(g, statement.id) : doc.connect(g, from.node, from.path, statement.id));
                        if (was && s.isExecPath(statement, ['next'])) say(doc.connect(g, statement.id, ['next'], was));
                    } else if (from.node > 0) {
                        // Before a statement: what ran it runs this, and this runs it
                        const source = doc.execSourceOf(g, from.node);
                        if (g.entry === from.node) doc.setEntry(g, statement.id);
                        else if (source) doc.connect(g, source.node, source.path, statement.id);
                        say(doc.connect(g, statement.id, ['next'], from.node));
                    }
                } else if (!from && statement && Doc.isBody(g) && !g.entry) {
                    doc.setEntry(g, statement.id);
                } else if (!from && !Doc.isBody(g) && !g.root && s.hasValue(node.k)) {
                    doc.setRoot(g, node.id);
                }
                made = node;
            });
            const that = this;
            function say(why) { if (why) that.say(why); }
            if (made) ed.select(made.id);
        }

        // Pick member…: the node becomes a member or method of its object's, by what tidels offers
        pickMember(n, clientX, clientY) {
            if (!n.object) {
                this.say('It has no object whose members to offer: wire one into it');
                return;
            }
            this.search.show(clientX, clientY, { member: n });
        }

        retarget(n, entry) {
            const s = this.editor.schema;
            const g = this.editor.graph;
            this.edit(() => {
                const k = entry.node.k === 'method' ? 'method' : 'member';
                const order = s.nodeOrder(k);
                const next = {};
                for (const key of ['id', 'k', 'x', 'y']) G.json.set(next, key, key === 'k' ? k : n[key], order);
                if (n.comment) G.json.set(next, 'comment', n.comment, order);
                G.json.set(next, 'object', n.object, order);
                if (k === 'method') {
                    G.json.set(next, 'name', entry.node.fields.name, order);
                    const keep = n.k === 'method' ? n.args || [] : [];
                    G.json.set(next, 'args', entry.node.fields.args.map((_, i) => keep[i] ?? null), order);
                } else {
                    G.json.set(next, 'member', entry.node.fields.member, order);
                }
                const nodes = Doc.nodes(g);
                nodes[nodes.indexOf(n)] = next;
            });
        }

        addNote(at) {
            const g = this.editor?.graph;
            if (!g) return;
            const r = this.canvas.el.getBoundingClientRect();
            const place = at || this.canvas.toWorld(r.left + r.width / 2 - 120, r.top + r.height / 2 - 70);
            let id;
            this.edit(doc => {
                const order = Doc.isBody(g) ? doc.schema.element('body') : doc.schema.element('value');
                if (!Array.isArray(g.notes)) G.json.set(g, 'notes', [], order);
                id = doc.newId();
                g.notes.push({ id, x: Math.round(place.x), y: Math.round(place.y), w: 240, h: 140, text: 'Note' });
            });
            this.editor.select(id);
        }

        addMember(e, kind) {
            let made;
            this.edit(doc => {
                const v = e.value;
                if (!Array.isArray(v.members)) v.members = [];
                const base = { field: 'value', property: 'Value', method: 'Method', tweak: 'tweak', operator: '', sample: 'Sample' }[kind] ?? 'member';
                made = doc.newElement(kind, true, doc.uniqueName(v.members, base));
                v.members.push(made);
            });
            if (made) this.editor.select(made.id);
        }

        // The menu of declarations to add, grouped as the schema groups them; into a namespace when one is given
        declMenu(x, y, namespace) {
            const s = this.editor?.schema;
            if (!s) return;
            const items = s.declarations.map(d => ({ label: d.title, group: d.group, run: () => this.addDecl(d.kind, namespace) }));
            menu(x, y, items);
        }

        addDecl(kind, namespace) {
            let made;
            this.edit(doc => {
                const list = namespace ? (namespace.decls ||= []) : doc.decls;
                const spec = doc.schema.declSpec(kind);
                const base = kind === 'scene' ? 'Main' : kind === 'settings' || kind === 'text' ? undefined : (spec?.title || kind).replace(/[^A-Za-z0-9]/g, '');
                made = doc.newElement(kind, false, base ? doc.uniqueName(list, base) : undefined);
                list.push(made);
            });
            if (made) this.editor.select(made.id);
        }

        async autoLayout() {
            const g = this.editor?.graph;
            if (!g) return;
            let places;
            try {
                places = await this.request('tide/graphLayout', { graph: g });
            } catch (e) {
                this.say(`No layout: ${e.message}`);
                return;
            }
            if (!places || typeof places !== 'object') {
                this.say('No layout: tidels gave none');
                return;
            }
            this.edit(() => {
                for (const n of Doc.nodes(g)) {
                    const p = places[n.id] ?? places[String(n.id)];
                    if (Array.isArray(p)) { n.x = p[0]; n.y = p[1]; }
                }
            });
            this.canvas.frame(false);
        }

        // ---------------------------------------------------------------------------------------------------------
        // Commands: from the keyboard, the toolbar, menus, and the host (its own Undo, Copy...)

        async command(name, m) {
            const ed = this.editor;
            if (!ed) return;
            switch (name) {
                case 'undo': ed.undo(); this.checkSoon(); break;
                case 'redo': ed.redo(); this.checkSoon(); break;
                case 'delete': ed.deleteSelection(); this.checkSoon(); break;
                case 'duplicate': ed.duplicateSelection(); this.checkSoon(); break;
                case 'selectAll': {
                    const g = ed.graph;
                    if (!g) break;
                    ed.selection = new Set([...Doc.nodes(g).map(n => n.id), ...(g.notes || []).map(n => n.id)]);
                    ed.emit('selection');
                    break;
                }
                case 'copy':
                case 'cut': {
                    const text = ed.copySelection();
                    if (!text) break;
                    this.clip = text;
                    this.post({ type: 'clipboardWrite', text });
                    if (name === 'cut') { ed.deleteSelection(); this.checkSoon(); }
                    break;
                }
                case 'paste': {
                    let text = m?.text;
                    if (typeof text !== 'string') {
                        try { text = await this.request('host/clipboardRead', null); } catch { text = null; }
                    }
                    if (!Doc.isClip(text)) text = this.clip;
                    if (!Doc.isClip(text)) { this.say('Nothing to paste: copy nodes first'); break; }
                    this.pasteAt(text);
                    break;
                }
                case 'frame': this.canvas.frame(ed.selection.size > 0); break;
                case 'layout': this.autoLayout(); break;
                case 'search': {
                    const p = this.canvas.lastPointer;
                    const r = this.canvas.el.getBoundingClientRect();
                    const inside = p.x >= r.left && p.x <= r.right && p.y >= r.top && p.y <= r.bottom;
                    this.openSearch(inside ? p.x : r.left + r.width / 2, inside ? p.y : r.top + r.height / 3, null);
                    break;
                }
                default: break;
            }
        }

        // Pastes nodes where the pointer is (or beside where they were)
        pasteAt(text) {
            let dx = 40, dy = 40;
            try {
                const clip = JSON.parse(text);
                const xs = [...clip.nodes, ...(clip.notes || [])];
                const p = this.canvas.lastPointer;
                const r = this.canvas.el.getBoundingClientRect();
                if (xs.length && p.x >= r.left && p.x <= r.right && p.y >= r.top && p.y <= r.bottom) {
                    const at = this.canvas.toWorld(p.x, p.y);
                    dx = at.x - Math.min(...xs.map(n => Number(n.x) || 0));
                    dy = at.y - Math.min(...xs.map(n => Number(n.y) || 0));
                }
            } catch {}
            this.editor.paste(text, dx, dy);
            this.checkSoon();
        }

        focusInspector(key) {
            requestAnimationFrame(() => this.inspector.focus(key));
        }

        keys() {
            document.addEventListener('keydown', e => {
                if (!this.editor) return;
                const t = e.target;
                const typing = t && (t.tagName === 'INPUT' || t.tagName === 'TEXTAREA' || t.tagName === 'SELECT' || t.isContentEditable);
                const mod = e.ctrlKey || e.metaKey;
                const key = e.key.toLowerCase();
                let handled = true;
                if (mod && key === 's') this.post({ type: 'save' });
                else if (typing) handled = false;
                else if (mod && key === 'z' && !e.shiftKey) this.command('undo');
                else if (mod && (key === 'y' || (key === 'z' && e.shiftKey))) this.command('redo');
                else if (mod && key === 'c') this.command('copy');
                else if (mod && key === 'x') this.command('cut');
                else if (mod && key === 'v') this.command('paste');
                else if (mod && key === 'd') this.command('duplicate');
                else if (mod && key === 'a') this.command('selectAll');
                else if (!mod && (key === 'delete' || key === 'backspace')) this.command('delete');
                else if (!mod && !e.altKey && key === 'f') this.command('frame');
                else if (!mod && key === ' ') this.command('search');
                else if (key === 'escape') { closeMenu(); tooltip(0, 0, null); this.editor.selection.clear(); this.editor.emit('selection'); }
                else if (!mod && (key === '+' || key === '=')) this.canvas.zoomBy(1.2);
                else if (!mod && key === '-') this.canvas.zoomBy(1 / 1.2);
                else handled = false;
                if (handled) {
                    // Not the host's too: VS Code's webview would undo, copy or save the document again
                    e.preventDefault();
                    e.stopPropagation();
                }
            });
        }

        // The host's colours, for hosts without CSS variables of their own (JetBrains): { dark, colors: { name: css } }
        theme(m) {
            const r = document.documentElement;
            r.classList.toggle('tg-dark', !!m.dark);
            r.classList.toggle('tg-light', m.dark === false);
            for (const [k, v] of Object.entries(m.colors || {})) r.style.setProperty('--tg-' + k, v);
            if (m.font) r.style.setProperty('--tg-font', m.font);
            if (m.fontSize) r.style.setProperty('--tg-font-size', m.fontSize + 'px');
            if (this.editor) this.renderAll();
        }
    }

    let app = null;
    G.start = (host, root) => {
        app = new App(host, root);
        G.app = app;
        return app;
    };
    G.receive = message => app?.receive(message);
    G.App = App;
})(globalThis.TideGraph = globalThis.TideGraph || {});
