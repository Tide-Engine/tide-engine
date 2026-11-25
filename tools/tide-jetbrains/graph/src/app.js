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
            this.contentsOpen = false;
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
            this.contentsButton = button('Contents', () => this.toggleContents(), 'What this file has, to jump to', 'tg-button');
            this.findButton = button('Find…', e => G.pickers.jumpTo(this, { anchor: e.currentTarget }), 'Jump to a declaration by its name (Ctrl+F)', 'tg-button.tg-find');
            const bar = el('div.tg-toolbar', null,
                this.addButton, this.contentsButton, this.findButton,
                el('span.tg-grow'), this.statusChip, el('span.tg-sep'), this.undoButton, this.redoButton,
                button('⋯', e => this.boardMenu(e.currentTarget), 'More: view as Tide, tidy up, this file\'s namespace', 'tg-button.tg-tool.tg-board-more'));
            this.bannerEl = el('div.tg-banner');
            this.board = new Board(this);
            this.contents = el('div.tg-contents');
            this.zoomLabel = button('100%', () => { this.board.zoomAt(1 / this.board.zoom); }, 'Back to full size', 'tg-button.tg-zoom-label');
            const zoom = el('div.tg-zoom', { onpointerdown: e => e.stopPropagation() },
                button('−', () => this.board.zoomAt(1 / 1.25), 'Zoom out (Ctrl and the wheel)', 'tg-button'),
                this.zoomLabel,
                button('+', () => this.board.zoomAt(1.25), 'Zoom in', 'tg-button'),
                button('Fit', () => this.board.fit(false), 'See the whole file (F)', 'tg-button'));
            this.main = el('div.tg-main', null, this.contents, el('div.tg-center', null, this.board.el, zoom));
            G.ui.put(root, bar, this.bannerEl, this.main);
            this.keys();
            addEventListener('resize', () => { if (this.editor) this.board.scheduleCull(); });
            this.banner(null);
            this.renderStatus();
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
                this.renderContents();
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
                this.renderContents();
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

        // Contents: the file's declarations by group, closed until asked for
        toggleContents(open = !this.contentsOpen) {
            this.contentsOpen = open;
            this.root.classList.toggle('tg-contents-open', open);
            this.contentsButton.classList.toggle('tg-on', open);
            this.renderContents();
            requestAnimationFrame(() => this.board.scheduleCull());
        }

        renderContents() {
            const box = this.contents;
            if (!this.contentsOpen || !this.editor || this.editor.error) {
                box.replaceChildren();
                return;
            }
            const entries = G.pickers.contents(this);
            const typed = this.contentsFilter?.value ?? '';
            const filter = el('input.tg-input.tg-contents-filter', { type: 'text', placeholder: 'Filter…', spellcheck: 'false', value: typed });
            const list = el('div.tg-contents-list');
            const fill = () => {
                const text = filter.value.trim().toLowerCase();
                list.replaceChildren();
                let group = null;
                for (const e of entries) {
                    if (text && !e.title.toLowerCase().includes(text)) continue;
                    if (e.group !== group) {
                        group = e.group;
                        G.ui.put(list, el('div.tg-contents-group', { text: group }));
                    }
                    G.ui.put(list, el('div.tg-contents-item', { dataset: { id: e.id }, onclick: () => { this.reveal(e.id); if (innerWidth < 640) this.toggleContents(false); }, title: e.detail },
                        el('span.tg-contents-name', { text: e.title }), el('span.tg-contents-kind', { text: e.detail }),
                        e.problems ? el('span.tg-badge.tg-badge-error', { text: String(e.problems) }) : null));
                }
                if (!list.childNodes.length) G.ui.put(list, el('div.tg-contents-none', { text: entries.length ? 'Nothing by that name' : 'Nothing here yet: + Add makes the first thing' }));
            };
            filter.addEventListener('input', fill);
            filter.addEventListener('keydown', e => e.stopPropagation());
            const focused = document.activeElement === this.contentsFilter;
            this.contentsFilter = filter;
            fill();
            box.replaceChildren(el('div.tg-contents-head', null, el('span', { text: 'Contents' }), button('×', () => this.toggleContents(false), 'Close', 'tg-icon')), filter, list);
            if (focused) filter.focus();
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
            this.board.ensureVisible(made.id);
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
                        const source = frame.sourceNode(doc, from, { x: at.x, y: at.y });
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
                    else if (ed.picked.size) this.deleteItems([...ed.picked]);
                    break;
                case 'duplicate':
                    if (ed.selection.size) { ed.duplicateSelection(); this.checkSoon(); }
                    else for (const item of ed.doc.items().filter(i => ed.picked.has(i.id))) this.duplicateItem(item);
                    break;
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
            if (xs.length && p.x >= r.left && p.x <= r.right && p.y >= r.top && p.y <= r.bottom) {
                const at = f.toLocal(p.x, p.y);
                dx = at.x - Math.min(...xs.map(n => Number(n.x) || 0));
                dy = at.y - Math.min(...xs.map(n => Number(n.y) || 0));
            }
            const made = ed.paste(text, dx, dy, f.slot);
            // What's selected is what's drawn: not the values shown in inputs that came with it
            f.refresh();
            ed.selectNodes(f.slot, made.filter(id => { const n = ed.doc.get(id); return !n || n.k === undefined || f.drawn(n); }));
            this.checkSoon();
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
                else if (!mod && (key === 'delete' || key === 'backspace')) this.command('delete');
                else if (!mod && !e.altKey && key === 'f') this.command('frame');
                else if (!mod && key === '/') this.command('find');
                else if (!mod && key === ' ' && !onButton) this.command('search');
                else if (key === 'escape') {
                    tooltip(0, 0, null);
                    if (this.board.drag) this.board.cancel();
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
