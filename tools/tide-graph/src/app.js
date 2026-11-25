// The graph editor's page: the board (a file as one canvas of cards and frames), a small toolbar over it, and its one
// connection to the host (VS Code's custom editor, JetBrains' JCEF editor, the docs' playground, a test). The host's
// side is a few messages:
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
//                 {type: 'reveal', node}                   show an element of the file (a declaration, a node)
//
// TideGraph.start(host) makes the page, where host.post(message) sends to the host; the host calls
// TideGraph.receive(message).
(function (G) {
    'use strict';
    const { el, button, menu, popup, closePopups, popupOpen, tooltip, toast } = G.ui;
    const { Doc, Schema, Editor, Lang, Board } = G;
    const { ENTRY, RESULT } = G.frames;

    class App {
        constructor(host, root) {
            this.host = host;
            this.root = root || document.body;
            this.pending = new Map();
            this.nextRequest = 1;
            this.clip = null;
            this.spaceDown = false;
            // The panels, as they were left (a phone's are closed until asked for)
            const narrow = typeof innerWidth === 'number' && innerWidth < 900;
            this.open = { mygame: G.ui.remembered('mygame', !narrow) && !narrow, details: G.ui.remembered('details', !narrow) && !narrow };
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
                    if (!m.available) this.lang.unavailable(m.message || null);
                    else this.checkSoon(0);
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
                this.banner('Getting ready…');
                this.request('tide/graphSchema', null).then(s => {
                    if (s && s.nodes) this.load({ ...m, schema: s });
                    else this.banner("The graph editor can't start: Tide's language server gave it nothing to work with. Install tide, then open the file again.");
                }, e => this.banner(`The graph editor can't start: ${e.message}. It needs tidels, the language server that comes with tide: install tide, then open the file again.`));
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
            this.board.auto.clear();
            this.board.framed = false;
            this.editor.load(m.text ?? '', true);
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
            this.statusChip = el('button.tg-status', { type: 'button', onclick: e => { e.stopPropagation(); this.showProblems(); }, onpointerdown: e => e.stopPropagation() });
            this.undoButton = button('↶', () => this.command('undo'), 'Undo (Ctrl+Z)', 'tg-button.tg-tool');
            this.redoButton = button('↷', () => this.command('redo'), 'Redo (Ctrl+Y)', 'tg-button.tg-tool');
            this.addButton = button('+ Add', e => G.pickers.addMenu(this, 0, 0, { anchor: e.currentTarget }), 'Add something to the file (or right-click the board)', 'tg-button.tg-primary.tg-add');
            this.gameButton = button('My game', () => this.togglePanel('mygame'), 'What this file has, by group: click one to go to it, drag one out to use it', 'tg-button.tg-mygame-button');
            this.findButton = button('Find…', e => G.pickers.jumpTo(this, { anchor: e.currentTarget }), 'Jump to a declaration by its name (Ctrl+F)', 'tg-button.tg-find');
            this.detailsButton = button('Details', () => this.togglePanel('details'), "What's selected: its options, its note", 'tg-button.tg-details-button');
            const bar = el('div.tg-toolbar', null,
                this.addButton, this.gameButton, this.findButton,
                el('span.tg-grow'), this.statusChip, el('span.tg-sep'), this.undoButton, this.redoButton, el('span.tg-sep'), this.detailsButton,
                button('⋯', e => this.boardMenu(e.currentTarget), 'More: view as Tide, tidy up, this file\'s namespace', 'tg-button.tg-tool.tg-board-more'));
            this.bannerEl = el('div.tg-banner');
            this.board = new Board(this);
            this.myGame = new G.panels.MyGame(this);
            this.details = new G.panels.Details(this);
            this.zoomLabel = button('100%', () => { this.board.zoomAt(1 / this.board.zoom); }, 'Back to full size', 'tg-button.tg-zoom-label');
            const zoom = el('div.tg-zoom', { onpointerdown: e => e.stopPropagation() },
                button('−', () => this.board.zoomAt(1 / 1.25), 'Zoom out (Ctrl and the wheel)', 'tg-button'),
                this.zoomLabel,
                button('+', () => this.board.zoomAt(1.25), 'Zoom in', 'tg-button'),
                button('Fit', () => this.board.fit(false), 'See the whole file (F)', 'tg-button'));
            this.main = el('div.tg-main', null, this.myGame.el, el('div.tg-center', null, this.board.el, zoom), this.details.el);
            G.ui.put(root, bar, this.bannerEl, this.main);
            this.keys();
            addEventListener('resize', () => { if (this.editor) this.board.scheduleCull(); });
            this.banner(null);
            this.renderStatus();
            this.showPanels();
        }

        // Opens or closes a panel, and remembers it
        togglePanel(name, open = !this.open[name]) {
            this.open[name] = open;
            if (innerWidth >= 900) G.ui.remember(name, open);
            // A phone has one sheet at a time
            if (open && innerWidth < 640) for (const other of Object.keys(this.open)) if (other !== name) this.open[other] = false;
            this.showPanels();
            this.renderPanels();
            requestAnimationFrame(() => this.board.scheduleCull());
        }

        showPanels() {
            this.root.classList.toggle('tg-mygame-open', !!this.open.mygame);
            this.root.classList.toggle('tg-details-open', !!this.open.details);
            this.gameButton?.classList.toggle('tg-on', !!this.open.mygame);
            this.detailsButton?.classList.toggle('tg-on', !!this.open.details);
        }

        renderPanels() {
            if (!this.editor) return;
            if (this.open.mygame) this.myGame.render(); else this.myGame.el.replaceChildren();
            if (this.open.details) this.details.render(); else this.details.el.replaceChildren();
        }

        // What's selected changed: Details shows it, and My game marks it
        selectionChanged() {
            const ed = this.editor;
            if (!ed) return;
            const key = [G.slotKey(ed.slot), [...ed.selection].join(), [...ed.picked].join(), ed.row].join('|');
            if (key === this.selectionKey) return;
            this.selectionKey = key;
            if (this.open.details && !this.details.el.contains(document.activeElement)) this.details.render();
            for (const x of this.myGame.el.querySelectorAll('.tg-mygame-item')) x.classList.toggle('tg-on', ed.picked.has(Number(x.dataset.id)));
            // My game's Variables are the code in hand's
            const f = this.variablesFrame();
            if (f) this.lastFrame = f.item.id;
            if ((f?.item.id || 0) !== this.myGame.varsOf && this.open.mygame && !this.myGame.el.contains(document.activeElement)) this.myGame.render();
        }

        // The frame whose variables My game lists: the one the selection is in, or the one picked, or the last of
        // those, or the file's only one
        variablesFrame() {
            const ed = this.editor;
            const b = this.board;
            if (!ed || ed.error) return null;
            if (ed.selection.size && ed.slot?.key === 'body' && b.frames.get(ed.slot.owner)) return b.frames.get(ed.slot.owner);
            for (const id of ed.picked) if (b.frames.get(id)) return b.frames.get(id);
            if (this.lastFrame && b.frames.get(this.lastFrame)) return b.frames.get(this.lastFrame);
            return b.frames.size === 1 ? [...b.frames.values()][0] : null;
        }

        // Where a variable of My game would go if dropped on `under`: { el } of the input or the frame it goes in
        variableTarget(v, under) {
            const f = this.board.frameAt(under);
            if (!f || f !== v.frame || f.mode !== 'near' || !f.graph) return null;
            const port = this.board.portAt(f, under);
            if (port && port.input && !port.exec) return { el: f.ports.get(G.frames.portKey(port))?.row || f.el };
            return { el: f.el };
        }

        // A variable of My game dropped: on an input, a Get of it wired there; on its code, "Get <name>" or
        // "Set <name>" asked (Ctrl: a Get at once; Alt: a Set). A Set is only of what the code can change.
        dropVariable(v, x, y, { get = false, set = false } = {}) {
            const f = v.frame;
            const under = document.elementFromPoint(x, y);
            if (!this.variableTarget(v, under)) {
                toast(`Drop ${v.text} inside ${this.editor.doc.titleOf(f.item)}: it's that code's to use.`);
                return;
            }
            const port = this.board.portAt(f, under);
            const can = f.changeable(v.start);
            if (port && port.input && !port.exec) {
                if (f.isPlace(port) && !can.ok) { toast(`That can't go there: ${can.why}.`); return; }
                f.setInput(port, (d, g, at) => d.makePath(g, v.start, v.members, at));
                return;
            }
            const at = f.toLocal(x, y);
            const made = id => {
                if (!id) return;
                this.editor.selectNodes(f.slot, [id]);
                this.board.showNode(this.board.frameOf(f.slot), id);
            };
            const doGet = () => {
                let last = 0;
                this.edit(doc => {
                    const g = doc.ensureGraph(f.slot);
                    if (!g) return;
                    const w = 44 + 7.5 * String(v.members.at(-1) ?? v.text).length + (v.members.length ? 62 : 0);
                    last = f.makeGets(doc, g, v.start, v.members, f.freeSpot({ x: at.x - w / 2, y: at.y - 14 }, { w, h: 28 }));
                    f.holdPlace(doc, g);
                });
                made(last);
            };
            const doSet = () => {
                if (!can.ok) { toast(`It can't be set here: ${can.why}.`); return; }
                let node = null;
                this.edit(doc => {
                    const g = doc.ensureGraph(f.slot);
                    if (!g) return;
                    const before = new Set(Doc.nodes(g).map(n => n.id));
                    const spot = f.freeSpot({ x: at.x - 60, y: at.y - 20 }, { w: 240, h: 80 });
                    node = doc.newNode('assign', spot.x, spot.y, { op: '=' });
                    Doc.nodes(g).push(node);
                    doc.schema.setWire(node, ['target'], doc.makePath(g, v.start, v.members, spot));
                    doc.reindex();
                    f.placeGets(doc, g, new Set(Doc.nodes(g).filter(n => !before.has(n.id)).map(n => n.id)));
                    f.holdPlace(doc, g);
                });
                made(node?.id);
            };
            if (get) { doGet(); return; }
            if (set) { doSet(); return; }
            menu(x, y, [
                { label: `Get ${v.text}`, summary: 'Its value, to wire into any number of inputs', name: 'get', hint: 'Ctrl', run: doGet },
                { label: `Set ${v.text}`, summary: 'Changes it, when the code runs this', name: 'set', hint: 'Alt', run: doSet, disabled: !can.ok, why: can.why },
            ], { title: v.text, className: 'tg-context' });
        }

        zoomChanged() {
            if (this.zoomLabel) this.zoomLabel.textContent = Math.round(this.board.zoom * 100) + '%';
        }

        banner(text) {
            this.bannerEl.textContent = text || '';
            this.bannerEl.style.display = text ? 'block' : 'none';
        }

        // The editor's changes: what to draw again
        changed(what) {
            const ed = this.editor;
            if (what === 'document') {
                this.banner(ed.error ? `This file isn't a graph the editor can read (${ed.error}). Fix it as text; the board comes back when it reads again.` : null);
                this.problemCounts = null;
                this.board.render();
                this.selectionKey = null;
                this.renderPanels();
                this.renderStatus();
            } else if (what === 'selection') {
                this.board.showSelection();
            }
            this.undoButton.disabled = !ed.canUndo;
            this.redoButton.disabled = !ed.canRedo;
        }

        langChanged(what) {
            if (!this.editor) return;
            this.problemCounts = null;
            if (what === 'check') {
                // What tidels found: the colours of pins, wires and names by their types, and what a call's
                // arguments are called. Frames say those, so they're drawn again when any changed.
                const found = JSON.stringify([this.lang.callsKnown, this.lang.calls, this.lang.types]);
                if (found !== this.found) {
                    this.found = found;
                    this.board.redrawFrames();
                } else {
                    for (const f of this.board.frames.values()) if (f.mode === 'near') f.drawWires();
                    if (this.board.valueFrame) this.board.valueFrame.drawWires();
                }
            }
            if (what === 'check' || what === 'diagnostics') {
                this.board.markProblems();
                if (this.open.mygame && !this.myGame.el.contains(document.activeElement)) this.myGame.render();
            }
            this.renderStatus();
        }

        // The problems of the last check that are about something the file still has (or about the file)
        problems() {
            const ed = this.editor;
            if (!ed || ed.error) return [];
            return this.lang.diagnostics.filter(d => !d.node || ed.doc.find(d.node));
        }

        // How many problems a card or a frame has in it
        problemsIn(item) {
            if (!this.problemCounts) {
                this.problemCounts = new Map();
                for (const d of this.problems()) {
                    const id = d.node ? this.editor.itemOf(d.node) : 0;
                    if (id) this.problemCounts.set(id, (this.problemCounts.get(id) || 0) + 1);
                }
            }
            return this.problemCounts.get(item) || 0;
        }

        // The chip that says whether the board is checked, and how many problems it has
        renderStatus() {
            const l = this.lang;
            const chip = this.statusChip;
            const list = this.problems();
            const errors = list.filter(d => d.severity === 1).length;
            const warnings = list.length - errors;
            let text, cls, title;
            if (!this.editor) { text = ''; cls = 'tg-status-quiet'; title = ''; }
            else if (l.status === 'unavailable') { text = 'Not checked'; cls = 'tg-status-off'; title = 'Nothing checks this file: click to see why'; }
            else if (errors) { text = `${errors} ${errors === 1 ? 'problem' : 'problems'}`; cls = 'tg-status-error'; title = 'Click to see them'; }
            else if (warnings) { text = `${warnings} ${warnings === 1 ? 'warning' : 'warnings'}`; cls = 'tg-status-warning'; title = 'Click to see them'; }
            else if (l.status === 'checked') { text = 'Checked'; cls = 'tg-status-ok'; title = 'Tide checked this file and found nothing wrong'; }
            else { text = 'Checking…'; cls = 'tg-status-quiet'; title = ''; }
            chip.replaceChildren(el('span.tg-status-mark'), text);
            chip.className = 'tg-status ' + cls;
            chip.title = title;
            chip.style.display = text ? '' : 'none';
        }

        // The list of problems: each with its words (the compiler's; the editor's own for an input with nothing in
        // it), what it's on, and a click to go there
        showProblems() {
            const ed = this.editor;
            if (!ed) return;
            const l = this.lang;
            const list = this.problems();
            const body = el('div.tg-problems');
            if (l.status === 'unavailable') {
                G.ui.put(body, el('div.tg-problems-none', null, el('strong', { text: 'This file isn\'t being checked.' }),
                    el('div', { text: `Tide's language server (tidels, which comes with tide) checks a graph as you edit it. ${l.why ? 'It says: ' + l.why + '.' : 'It isn\'t running.'} You can still edit; mistakes show when the game is built.` })));
            } else if (!list.length) {
                G.ui.put(body, el('div.tg-problems-none', { text: l.status === 'checked' ? 'Nothing wrong: Tide checked this file.' : 'Checking…' }));
            }
            for (const d of list) {
                const e = d.node ? ed.doc.find(d.node) : null;
                const item = d.node ? ed.doc.items().find(i => i.id === ed.itemOf(d.node)) : null;
                G.ui.put(body, el('div.tg-problem' + (d.severity === 1 ? '.tg-error' : '.tg-warning'), {
                    onclick: () => { closePopups(); if (d.node) this.reveal(d.node); },
                    title: d.node ? 'Go to it' : '',
                },
                el('span.tg-problem-mark', { text: d.severity === 1 ? '●' : '▲' }),
                el('span.tg-problem-text', null, el('span.tg-problem-message', { text: this.board.wordsOf(d) }), d.note && !d.port ? el('span.tg-problem-note', { text: d.note }) : null),
                item ? el('span.tg-problem-where', { text: `${ed.doc.titleOf(item)}${e && e.id !== item.id && e.role !== 'node' ? ' · ' + ed.doc.describe(e) : ''}` }) : null));
            }
            popup({ anchor: this.statusChip, className: 'tg-popup tg-problems-popup', content: body });
        }

        // The board's own menu: what isn't on it
        boardMenu(anchor) {
            const ed = this.editor;
            if (!ed || ed.error) return;
            const root = ed.doc.root;
            const order = ed.schema.root;
            const r = anchor.getBoundingClientRect();
            const at = { x: r.left - 200, y: r.bottom + 4 };
            menu(0, 0, [
                { label: 'View as Tide', summary: 'The Tide code this file stands for', name: 'view-tide', run: () => this.post({ type: 'showTide', text: this.lang.text ?? undefined }) },
                { label: 'Tidy up the board', summary: 'Data in a column, code beside it', name: 'tidy', run: () => this.layoutBoard() },
                { label: 'See the whole file', hint: 'F', name: 'fit', run: () => this.board.fit(false) },
                { separator: true },
                { label: 'Namespace…', summary: root.namespace ? root.namespace : 'None: what it declares is seen everywhere', name: 'namespace',
                    run: () => G.cards.promptText({ ...at, title: 'This file\'s namespace', help: 'A name its declarations go under, like Game.Combat. Empty for none.', value: root.namespace, placeholder: 'Game.Combat',
                        check: v => (v === '' || /^[A-Za-z_][A-Za-z0-9_]*(\.[A-Za-z_][A-Za-z0-9_]*)*$/.test(v) ? null : 'Names joined by dots, like Game.Combat'),
                        onDone: text => this.edit(() => G.json.set(root, 'namespace', text || undefined, order)) }) },
                { label: 'Uses…', summary: (root.using || []).join(', ') || 'The namespaces whose names it uses without writing them', name: 'using',
                    run: () => G.cards.promptRows({ ...at, title: 'Namespaces this file uses', help: 'Their names are used here without the namespace before them.', rows: (root.using || []).map(name => ({ name })),
                        columns: [{ key: 'name', placeholder: 'Physics', check: v => (/^[A-Za-z_][A-Za-z0-9_]*(\.[A-Za-z_][A-Za-z0-9_]*)*$/.test(v) ? null : 'A namespace, like Physics') }],
                        addLabel: 'namespace', blank: () => ({ name: '' }),
                        onDone: rows => this.edit(() => G.json.set(root, 'using', rows.filter(x => x.name).length ? rows.filter(x => x.name).map(x => x.name) : undefined, order)) }) },
            ], { anchor });
        }

        // Selects an element and shows it: its card or frame brought to where it can be read
        reveal(id) {
            const ed = this.editor;
            if (!ed || ed.error || !ed.doc.find(id)) return;
            const e = ed.doc.find(id);
            // A node of a value's graph: the value opens
            if ((e.role === 'node' || e.role === 'note') && e.slot && e.slot.key !== 'body') {
                const owner = ed.doc.find(e.slot.owner);
                if (owner) this.openValue(e.slot, owner.value, e.slot.key === 'components' ? null : e.slot.key, ed.doc.orderOf(owner), owner.value.type);
            }
            ed.select(id);
            this.board.reveal(id);
        }

        // A card or a frame renamed: what the file says of it by that name follows (Doc.renameUses), and a
        // replacement keeps what it replaces only if that was its own name
        renameDecl(item, name) {
            const v = item.value;
            this.edit(doc => {
                const old = v.name;
                v.name = name;
                if (v.overrides && !v.overrides.endsWith('.' + name)) delete v.overrides;
                if (!item.member && !item.accessor) doc.renameUses(old, name);
            });
        }

        // Starts renaming what a name's field key names (`${id}:name`): on the board, drawn first if it isn't
        rename(fk, target = null) {
            const find = () => document.querySelector(`.tg-world [data-fk="${fk}"]`);
            let named = find();
            if (!named?.rename) {
                const id = Number(fk.split(':')[0]);
                const e = this.editor.doc.find(id);
                if (!e) return;
                this.board.reveal(target?.kind === 'row' ? target.item.id : e.role === 'node' ? id : this.editor.itemOf(id) || id);
                named = find();
            }
            named?.rename?.(true);
        }

        // Renames what's selected (F2): a row, a node that declares a local, a card or a frame
        renameSelection() {
            const ed = this.editor;
            const b = this.board;
            let t = null;
            if (ed.selection.size === 1) {
                const f = b.frameOf(ed.slot);
                const node = f?.views.get([...ed.selection][0])?.node;
                if (node) t = { kind: 'node', frame: f, node };
            } else if (ed.row) {
                const item = ed.doc.items().find(i => i.id === [...ed.picked][0]);
                const row = ed.doc.find(ed.row);
                if (item && row) t = { kind: 'row', item, row };
            } else if (ed.picked.size === 1) {
                const item = ed.doc.items().find(i => i.id === [...ed.picked][0]);
                if (item) t = { kind: item.card ? 'card' : item.region ? 'region' : 'frame', item };
            }
            const fk = t ? G.menus.nameOf(this, t) : null;
            if (fk) this.rename(fk, t);
            else if (t) toast('It has no name to change');
        }

        // What a frame takes, picked: a component, a singleton, a filter...
        openNeeds(item, x, y) {
            const f = this.board.frames.get(item.id);
            const add = f?.mode === 'near' ? f.body.querySelector('.tg-entry .tg-add-row') : null;
            G.pickers.needsPicker(this, item, add ? { anchor: add } : { x, y });
        }

        // ---------------------------------------------------------------------------------------------------------
        // Dragged out of My game

        // Where something of My game would go if dropped on `under`: { el (lit while it's over it), run() }, or null
        dropTarget(item, under) {
            const ed = this.editor;
            if (!under?.closest || !item) return null;
            const kind = item.value.kind;
            const frame = this.board.frameAt(under);
            if ((kind === 'component' || kind === 'singleton') && frame && !frame.isValue && (frame.item.spec?.keys || []).includes('params')) {
                const routine = frame.item.value;
                return { el: frame.el, run: () => {
                    const taken = new Set((routine.params || []).map(p => p.name));
                    let name = G.pickers.nameFor(item.value.name);
                    for (let i = 2; taken.has(name); i++) name = G.pickers.nameFor(item.value.name) + i;
                    let made;
                    this.edit(d => {
                        if (!Array.isArray(routine.params)) G.json.set(routine, 'params', [], frame.item.spec?.keys);
                        made = d.newParam(item.value.name, name);
                        routine.params.push(made);
                    });
                    if (made) ed.selectRow(frame.item.id, made.id);
                } };
            }
            if (kind === 'function' && frame?.mode === 'near' && frame.graph) {
                return { el: frame.el, run: (x, y) => {
                    const fields = { name: item.value.name, args: (item.value.params || []).filter(p => !this.editor.schema.modeIsFilter(p.mode)).map(() => null) };
                    const gives = item.value.returnType && item.value.returnType !== 'void';
                    this.addFromSearch(frame, { node: { k: 'call', fields }, statement: !gives, detail: 'from My game' }, frame.toLocal(x, y), null);
                } };
            }
            if (kind === 'const' && frame?.mode === 'near') {
                const port = this.board.portAt(frame, under);
                if (port && port.input && !port.exec) {
                    return { el: frame.ports.get(G.frames.portKey(port))?.row, run: () => frame.setInput(port, (d, g, at) => d.makePath(g, { name: item.value.name }, [], at)) };
                }
            }
            return null;
        }

        dropFromGame(item, x, y) {
            const target = this.dropTarget(item, document.elementFromPoint(x, y));
            if (target) { target.run(x, y); return; }
            const kind = item.value.kind;
            if (kind === 'component' || kind === 'singleton') toast(`Drop ${item.value.name} on a frame of code to add it to what the code needs.`);
            else if (kind === 'function') toast(`Drop ${item.value.name} inside a frame of code to call it there.`);
            else if (kind === 'const') toast(`Drop ${item.value.name} on an input of a node to put it there.`);
            else toast(`Click ${this.editor.doc.titleOf(item)} to go to it.`);
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
            if (this.lang.status === 'checked') { this.lang.status = 'checking'; this.renderStatus(); }
            this.checkTimer = setTimeout(() => { this.checkTimer = 0; this.checking = this.lang.check(); }, delay);
        }

        // Resolves once tidels has checked the document as it is now: a check that's waiting its turn runs at once.
        // What's asked of tidels after it is about what's on the board.
        checked() {
            if (this.checkTimer) {
                clearTimeout(this.checkTimer);
                this.checkTimer = 0;
                this.checking = this.lang.check();
            }
            return (this.checking || Promise.resolve()).catch(() => {});
        }

        // A new declaration of a kind: a card or a frame where the pointer asked (or where there's room), its name
        // selected to type over
        addDecl(kind, { namespace = null, at = null } = {}) {
            const ed = this.editor;
            if (!ed || ed.error) return null;
            const spec = ed.schema.declSpec(kind);
            const place = this.board.freePlace(!!spec?.body, at);
            let made;
            this.edit(doc => {
                this.board.persistPlaces(doc);
                const list = namespace ? (namespace.decls ||= []) : doc.decls;
                const names = doc.declared().map(d => ({ name: d.name }));
                const has = key => spec?.keys.includes(key);
                const base = !has('name') ? undefined : kind === 'scene' ? 'Main' : kind === 'namespace' ? 'Game' : (spec?.title || kind).replace(/[^A-Za-z0-9]/g, '');
                made = doc.newElement(kind, false, base ? doc.uniqueName(names, base) : undefined, place);
                list.push(made);
            });
            if (!made) return null;
            ed.pick([made.id]);
            this.board.showWith(made.id);
            const fk = spec?.keys.includes('name') ? `${made.id}:name` : spec?.keys.includes('text') ? `${made.id}:text` : null;
            if (fk) this.board.focusSoon(fk, true);
            return made;
        }

        // A new member of a kind on a card: a row for a field, a frame for a member with code
        addMember(item, kind) {
            const v = item.value;
            const s = this.editor.schema;
            const spec = s.memberSpec(kind);
            let made;
            this.edit(doc => {
                if (!Array.isArray(v.members)) G.json.set(v, 'members', [], item.spec?.keys);
                const base = !spec?.keys.includes('name') ? undefined : kind === 'field' ? 'value' : kind === 'tweak' ? 'tweak' : kind === 'sample' ? 'Sample' : (spec.title || kind).replace(/[^A-Za-z0-9]/g, '');
                const place = spec?.body ? this.board.freePlace(true, null) : null;
                if (spec?.body || kind === 'property') this.board.persistPlaces(doc);
                made = doc.newElement(kind, true, base && kind !== 'sample' ? doc.uniqueName(v.members, base) : base, place);
                v.members.push(made);
            });
            if (!made) return;
            if (spec?.body) {
                this.editor.pick([made.id]);
                this.board.ensureVisible(made.id);
            }
            if (kind === 'text') return;
            this.board.focusSoon(`${made.id}:name`, true);
        }

        duplicateItem(item) {
            if (item.accessor || !Array.isArray(item.list)) return;
            let copy;
            this.edit(doc => {
                this.board.persistPlaces(doc);
                copy = doc.duplicate(item.value);
                if (copy.name) copy.name = doc.uniqueName(item.member ? item.list : doc.declared().map(d => ({ name: d.name })), copy.name);
                if (typeof copy.x === 'number') { copy.x += 40; copy.y += 40; }
                item.list.splice(item.list.indexOf(item.value) + 1, 0, copy);
            });
            if (copy) {
                this.editor.pick([copy.id]);
                this.board.focusSoon(`${copy.id}:name`, true);
            }
        }

        // Deletes cards and frames, with a way back said for a moment
        deleteItems(ids) {
            const ed = this.editor;
            const items = ed.doc.items().filter(i => ids.includes(i.id) && !i.region);
            if (!items.length) return;
            const names = items.map(i => ed.doc.titleOf(i));
            this.edit(() => {
                for (const item of items) {
                    const at = item.list.indexOf(item.value);
                    if (at >= 0) item.list.splice(at, 1);
                }
            });
            ed.clearSelection();
            toast(`Deleted ${names.length === 1 ? names[0] : names.length + ' things'}`, { action: 'Undo', run: () => this.command('undo') });
        }

        // Lays a frame's nodes out as tidec does
        async layoutFrame(id) {
            const f = this.board.frames.get(id);
            const g = f?.graph;
            if (!g) return;
            let places;
            try {
                places = await this.request('tide/graphLayout', { graph: g });
            } catch (e) {
                toast(`It can't be tidied now: ${e.message}`);
                return;
            }
            if (!places || typeof places !== 'object') {
                toast("It can't be tidied now: Tide's language server didn't answer");
                return;
            }
            this.edit(() => {
                for (const n of Doc.nodes(g)) {
                    const p = places[n.id] ?? places[String(n.id)];
                    if (Array.isArray(p)) { n.x = p[0]; n.y = p[1]; }
                }
            });
        }

        // Lays the whole board out: data in a column on the left, code to its right
        async layoutBoard() {
            const ed = this.editor;
            if (!ed || ed.error) return;
            let places;
            try {
                places = await this.request('tide/graphLayout', { file: ed.doc.root });
            } catch (e) {
                toast(`The board can't be tidied now: ${e.message}`);
                return;
            }
            if (!places || typeof places !== 'object') {
                toast("The board can't be tidied now: Tide's language server didn't answer");
                return;
            }
            this.board.auto.clear();
            this.edit(doc => {
                for (const item of doc.items()) {
                    const p = places[item.id] ?? places[String(item.id)];
                    if (Array.isArray(p) && !item.region) this.board.setPlace(doc, item, p[0], p[1]);
                }
            });
            this.board.home();
            this.board.scheduleCull();
        }

        addNote(id) {
            const f = this.board.frames.get(id);
            const g = f?.graph;
            if (!g) return;
            let made;
            this.edit(doc => {
                const order = doc.schema.element('body');
                if (!Array.isArray(g.notes)) G.json.set(g, 'notes', [], order);
                made = doc.newId();
                const nodes = Doc.nodes(g);
                const y = nodes.length ? Math.max(...nodes.map(n => Number(n.y) || 0)) + 140 : 120;
                g.notes.push({ id: made, x: 0, y, w: 260, h: 120, text: 'A note: double-click to write' });
            });
            if (made) this.editor.selectNodes(f.slot, [made]);
        }

        // Opens a value as a small graph beside what it's the value of
        openValue(slot, holder, key, order, type) {
            const ed = this.editor;
            const e = ed.doc.find(slot.owner);
            if (!e) return;
            const v = e.value;
            const parent = e.owner && typeof e.owner.id === 'number' ? e.owner : null;
            let title = v.name ?? '';
            if (e.role === 'member' || e.role === 'enumMember') title = `${parent?.name ?? ''}.${v.name ?? ''}`;
            else if (e.role === 'prefabNode') {
                const root = Doc.nodeIn(ed.doc.graph(slot), ed.doc.graph(slot)?.root);
                title = `${v.name ?? parent?.name ?? ''} · ${root?.type ?? ''}`;
            }
            const required = e.role === 'decl' || e.role === 'entry' || v.kind === 'tweak' || slot.key === 'components';
            const anchor = document.querySelector(`[data-value="${slot.owner}:${slot.key}"]`) || this.board.elementOf(slot.owner);
            this.board.openValue({ slot, title, holder, key, order, type, required }, anchor);
        }

        closeValue() {
            this.board.closeValue(true);
        }

        // Adds what the node search picked at a place of a frame, wired to where the search came from
        addFromSearch(frame, entry, at, context) {
            const ed = this.editor;
            const s = ed.schema;
            const from = context?.from;
            let made = null;
            const whys = [];
            const say = why => { if (why) whys.push(why); };
            // Where it's asked for when that's free, else as near it as it's free: nothing goes on top of anything
            at = frame.freeSpot(at);
            this.edit(doc => {
                const g = doc.ensureGraph(frame.slot);
                if (!g) return;
                const node = doc.newNode(entry.node.k, at.x, at.y, entry.node.fields);
                Doc.nodes(g).push(node);
                let statement = s.isExec(node.k) ? node : null;
                // A call where a statement goes: the statement that does it, drawn as one node with it
                if (!statement && (from?.exec || (entry.statement && !from))) {
                    statement = doc.newNode('eval', at.x, at.y);
                    Doc.nodes(g).push(statement);
                    node.x = at.x - 200;
                    doc.reindex();
                    doc.connect(g, statement.id, ['value'], node.id);
                }
                doc.reindex();
                if (from && !from.exec && !from.input) {
                    // A value dragged out: into the new node's first input
                    const input = s.ports(node).find(p => p.input && !p.exec);
                    if (input) {
                        // (a pin of the entry or of a local: a Get of it, left of the new node)
                        const w = 44 + 7.5 * frame.pinText(from).length;
                        const spot = frame.freeSpot({ x: Math.round(at.x - w - 46), y: at.y + 30 }, { w, h: 28 }, new Set(), [{ x: at.x, y: at.y, w: 220, h: 96 }]);
                        const source = frame.sourceNode(doc, from, spot);
                        doc.reindex();
                        say(doc.connect(g, node.id, input.path, source));
                    }
                } else if (from && !from.exec && from.input) {
                    if (!s.hasValue(node.k)) say(`${s.title(node.k)} has no value to give`);
                    else if (from.node === RESULT) say(doc.setRoot(g, node.id));
                    else {
                        const target = Doc.nodeIn(g, from.node);
                        const old = target ? s.wire(target, from.path) : 0;
                        say(doc.connect(g, from.node, from.path, node.id));
                        if (old) { doc.reindex(); doc.dropValue(g, old); }
                    }
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
                made = statement && statement !== node ? statement : node;
            });
            if (whys.length) toast(`That wire can't be: ${whys[0]}.`);
            if (!made) return;
            ed.selectNodes(frame.slot, [made.id]);
            this.board.showNode(this.board.frameOf(frame.slot), made.id);
            // A node that starts with a name to give has it selected to type over
            const named = ed.doc.get(made.id);
            const own = named?.k === 'eval' ? ed.doc.get(named.value) : named;
            if (own && !entry.detail && !entry.hidden) {
                const key = ['name', 'member', 'text'].find(k => typeof own[k] === 'string');
                if (key && !entry.node.fields?.[key]) this.board.focusSoon(`${own.id}:${key}`, true);
            }
        }

        // Pick another member…: the node becomes a member or method of its value's, by what tidels offers
        retarget(frame, n, entry) {
            const s = this.editor.schema;
            const g = frame.graph;
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

        // ---------------------------------------------------------------------------------------------------------
        // Commands: from the keyboard, the toolbar, menus, and the host (its own Undo, Copy...)

        // The frame commands on nodes are about: the selection's, else the one under the pointer, else the file's
        // only one
        activeFrame() {
            const ed = this.editor;
            const b = this.board;
            if (ed.selection.size && b.frameOf(ed.slot)) return b.frameOf(ed.slot);
            if (b.valueFrame) return b.valueFrame;
            const under = b.frameAt(document.elementFromPoint(b.lastPointer.x, b.lastPointer.y));
            if (under && under.mode === 'near') return under;
            return b.frames.size === 1 && !ed.picked.size ? [...b.frames.values()][0] : null;
        }

        async command(name, m) {
            const ed = this.editor;
            if (!ed || ed.error) return;
            const b = this.board;
            switch (name) {
                case 'undo': ed.undo(); this.checkSoon(); break;
                case 'redo': ed.redo(); this.checkSoon(); break;
                case 'delete':
                    if (ed.selection.size) { ed.deleteSelection(); this.checkSoon(); }
                    else if (ed.row) this.deleteRow(ed.row);
                    else if (ed.picked.size) this.deleteItems([...ed.picked]);
                    break;
                case 'duplicate':
                    if (ed.selection.size) { ed.duplicateSelection(); this.checkSoon(); }
                    else if (ed.row) this.duplicateRow(ed.row);
                    else for (const item of ed.doc.items().filter(i => ed.picked.has(i.id))) this.duplicateItem(item);
                    break;
                case 'rename': this.renameSelection(); break;
                case 'selectAll': {
                    const f = this.activeFrame();
                    const g = f?.graph;
                    if (g) {
                        const ids = [...Doc.nodes(g).filter(n => f.drawn(n)).map(n => n.id), ...(g.notes || []).map(n => n.id)];
                        ed.selectNodes(f.slot, ids);
                    } else {
                        ed.pick(ed.doc.items().filter(i => !i.region).map(i => i.id));
                    }
                    break;
                }
                case 'copy':
                case 'cut': {
                    let text = ed.selection.size ? ed.copySelection() : null;
                    if (!text && ed.row) {
                        // A row: a field, a choice, a parameter, which pastes onto a card or a frame that has those
                        const e = ed.doc.find(ed.row);
                        if (e) text = G.json.write({ tidegraphClip: 1, nodes: [], rows: [{ role: e.role, value: G.json.clone(e.value) }] });
                    }
                    if (!text && ed.picked.size) {
                        // Cards and frames: as declarations, which paste on a board
                        const decls = ed.doc.items().filter(i => ed.picked.has(i.id) && !i.member && !i.accessor && !i.region).map(i => G.json.clone(i.value));
                        if (decls.length) text = G.json.write({ tidegraphClip: 1, nodes: [], decls });
                    }
                    if (!text) break;
                    this.clip = text;
                    this.post({ type: 'clipboardWrite', text });
                    if (name === 'cut') this.command('delete');
                    break;
                }
                case 'paste': {
                    let text = m?.text;
                    if (typeof text !== 'string') {
                        try { text = await this.request('host/clipboardRead', null); } catch { text = null; }
                    }
                    if (!Doc.isClip(text)) text = this.clip;
                    if (!Doc.isClip(text)) { toast('Nothing to paste: copy nodes, a card or a frame first'); break; }
                    this.pasteAt(text);
                    break;
                }
                case 'frame': b.fit(ed.selection.size > 0 || ed.picked.size > 0); break;
                case 'layout': {
                    const f = ed.selection.size ? b.frameOf(ed.slot) : ed.picked.size === 1 ? b.frames.get([...ed.picked][0]) : null;
                    if (f && !f.isValue) this.layoutFrame(f.item.id); else this.layoutBoard();
                    break;
                }
                case 'search': {
                    const p = b.lastPointer;
                    const r = b.el.getBoundingClientRect();
                    const inside = p.x >= r.left && p.x <= r.right && p.y >= r.top && p.y <= r.bottom;
                    const x = inside ? p.x : r.left + r.width / 2, y = inside ? p.y : r.top + r.height / 3;
                    b.openAdd(x, y, document.elementFromPoint(x, y));
                    break;
                }
                case 'find': G.pickers.jumpTo(this, { anchor: this.findButton }); break;
                case 'myGame': this.togglePanel('mygame'); break;
                case 'details': this.togglePanel('details'); break;
                default: break;
            }
        }

        // Pastes nodes into a frame where the pointer is (or beside where they were), declarations onto the board
        pasteAt(text) {
            const ed = this.editor;
            const b = this.board;
            let clip;
            try { clip = JSON.parse(text); } catch { return; }
            const p = b.lastPointer;
            if (Array.isArray(clip.rows) && clip.rows.length) {
                this.pasteRows(clip.rows);
                return;
            }
            if (Array.isArray(clip.decls) && clip.decls.length && !(clip.nodes || []).length) {
                const r = b.el.getBoundingClientRect();
                const inside = p.x >= r.left && p.x <= r.right && p.y >= r.top && p.y <= r.bottom;
                const at = inside ? b.toWorld(p.x, p.y) : null;
                const made = [];
                this.edit(doc => {
                    b.persistPlaces(doc);
                    const x0 = Math.min(...clip.decls.map(d => Number(d.x) || 0)), y0 = Math.min(...clip.decls.map(d => Number(d.y) || 0));
                    for (const d of clip.decls) {
                        const copy = doc.duplicate(d);
                        if (copy.name) copy.name = doc.uniqueName([...doc.declared(), ...made].map(x => ({ name: x.name })), copy.name);
                        if (typeof copy.x === 'number') {
                            copy.x = Math.round(at ? at.x + (copy.x - x0) : copy.x + 40);
                            copy.y = Math.round(at ? at.y + (copy.y - y0) : copy.y + 40);
                        }
                        doc.decls.push(copy);
                        made.push(copy);
                    }
                });
                ed.pick(made.map(d => d.id));
                return;
            }
            const f = this.activeFrame();
            if (!f?.graph) {
                toast('Nodes paste into a frame: put the pointer over the one they go in');
                return;
            }
            let dx = 40, dy = 40;
            const xs = [...(clip.nodes || []), ...(clip.notes || [])];
            const r = f.body.getBoundingClientRect();
            const x0 = xs.length ? Math.min(...xs.map(n => Number(n.x) || 0)) : 0, y0 = xs.length ? Math.min(...xs.map(n => Number(n.y) || 0)) : 0;
            if (xs.length && p.x >= r.left && p.x <= r.right && p.y >= r.top && p.y <= r.bottom) {
                const at = f.toLocal(p.x, p.y);
                dx = at.x - x0;
                dy = at.y - y0;
            }
            // Pasted where nothing is: the whole of it moved as one, as near as it's free
            if (xs.length) {
                const w = Math.max(...xs.map(n => (Number(n.x) || 0) + 220)) - x0, h = Math.max(...xs.map(n => (Number(n.y) || 0) + 96)) - y0;
                const spot = f.freeSpot({ x: x0 + dx, y: y0 + dy }, { w, h });
                dx = spot.x - x0;
                dy = spot.y - y0;
            }
            const made = ed.paste(text, dx, dy, f.slot);
            // What's selected is what's drawn: not the values shown in inputs that came with it
            f.refresh();
            ed.selectNodes(f.slot, made.filter(id => { const n = ed.doc.get(id); return !n || n.k === undefined || f.drawn(n); }));
            this.checkSoon();
        }

        // A row deleted, duplicated, pasted: a card's fields and choices, a frame's parameters
        deleteRow(id) {
            const ed = this.editor;
            const e = ed.doc.find(id);
            if (!e || !Array.isArray(e.list)) return;
            const name = ed.doc.describe(e);
            this.edit(() => { const i = e.list.indexOf(e.value); if (i >= 0) e.list.splice(i, 1); });
            ed.row = 0;
            ed.emit('selection');
            toast(`Deleted ${name}`, { action: 'Undo', run: () => this.command('undo') });
        }

        duplicateRow(id) {
            const ed = this.editor;
            const e = ed.doc.find(id);
            if (!e || !Array.isArray(e.list)) return;
            let copy;
            this.edit(d => {
                copy = d.duplicate(e.value);
                if (copy.name) copy.name = d.uniqueName(e.list, copy.name);
                e.list.splice(e.list.indexOf(e.value) + 1, 0, copy);
            });
            if (copy) ed.selectRow(ed.itemOf(copy.id), copy.id);
        }

        pasteRows(rows) {
            const ed = this.editor;
            const item = ed.picked.size === 1 ? ed.doc.items().find(i => i.id === [...ed.picked][0]) : null;
            if (!item) { toast('Rows paste into a card or a frame: select the one they go in'); return; }
            const v = item.value;
            const keys = item.spec?.keys || [];
            const at = ed.row ? ed.doc.find(ed.row) : null;
            const listOf = role => (role === 'member' && keys.includes('members') && item.spec?.members ? 'members'
                : role === 'enumMember' && keys.includes('members') && !item.spec?.members ? 'members'
                : role === 'entry' && keys.includes('entries') ? 'entries'
                : (role === 'param' || role === 'sampleParam') && keys.includes('params') ? 'params'
                : role === 'payload' && keys.includes('payload') ? 'payload' : null);
            const fit = rows.filter(r => listOf(r.role) && (r.role !== 'member' || item.spec?.memberKinds?.includes(r.value.kind)));
            if (!fit.length) { toast(`${ed.doc.titleOf(item)} can't have what was copied`); return; }
            let last = null;
            this.edit(d => {
                for (const r of fit) {
                    const key = listOf(r.role);
                    if (!Array.isArray(v[key])) G.json.set(v, key, [], keys);
                    const copy = d.duplicate(r.value);
                    if (copy.name) copy.name = d.uniqueName(v[key], copy.name);
                    const i = at && at.list === v[key] ? v[key].indexOf(at.value) : -1;
                    v[key].splice(i < 0 ? v[key].length : i + 1, 0, copy);
                    last = copy;
                }
            });
            if (last) ed.selectRow(item.id, last.id);
        }

        keys() {
            document.addEventListener('keydown', e => {
                if (!this.editor) return;
                const t = e.target;
                const typing = t && (t.tagName === 'INPUT' || t.tagName === 'TEXTAREA' || t.tagName === 'SELECT' || t.isContentEditable);
                const onButton = t && t.tagName === 'BUTTON';
                const mod = e.ctrlKey || e.metaKey;
                const key = e.key.toLowerCase();
                let handled = true;
                if (mod && key === 's') this.post({ type: 'save' });
                else if (typing || popupOpen()) handled = false;
                else if (mod && key === 'z' && !e.shiftKey) this.command('undo');
                else if (mod && (key === 'y' || (key === 'z' && e.shiftKey))) this.command('redo');
                else if (mod && key === 'c') this.command('copy');
                else if (mod && key === 'x') this.command('cut');
                else if (mod && key === 'v') this.command('paste');
                else if (mod && key === 'd') this.command('duplicate');
                else if (mod && key === 'a') this.command('selectAll');
                else if (mod && key === 'f') this.command('find');
                else if (!mod && e.key === 'F2') this.command('rename');
                else if (!mod && (key === 'delete' || key === 'backspace')) { if (!this.board.emptyInputUnder()) this.command('delete'); }
                else if (!mod && !e.altKey && key === 'f') this.command('frame');
                else if (!mod && key === '/') this.command('find');
                else if (!mod && key === ' ' && !onButton) this.command('search');
                else if (key === 'escape') {
                    tooltip(0, 0, null);
                    if (this.board.drag) this.board.cancel();
                    else if (this.open.mygame && innerWidth < 640) this.togglePanel('mygame', false);
                    else if (this.open.details && innerWidth < 640) this.togglePanel('details', false);
                    else if (this.editor.selection.size || this.editor.picked.size) this.editor.clearSelection();
                    else if (this.board.valueFrame) this.closeValue();
                }
                else if (!mod && (key === '+' || key === '=')) this.board.zoomAt(1.2);
                else if (!mod && key === '-') this.board.zoomAt(1 / 1.2);
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
            if (this.editor) this.board.render();
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
