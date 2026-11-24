// The search for a node to add: the schema's kinds of node, the graph's locals and the routine's parameters, and
// what tidels offers at the place the node would go (or, for a wire from a value, what that value has).
(function (G) {
    'use strict';
    const { el } = G.ui;
    const { Doc } = G;

    // How well a title matches what's typed: whole, start, a word's start, inside; 0 for not at all
    function rank(title, typed) {
        if (!typed) return 1;
        const t = title.toLowerCase(), q = typed.toLowerCase();
        if (t === q) return 5;
        if (t.startsWith(q)) return 4;
        if (new RegExp('(^|[^a-z0-9])' + q.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')).test(t)) return 3;
        if (t.includes(q)) return 2;
        return 0;
    }

    class Search {
        constructor(app) {
            this.app = app;
            this.el = null;
        }

        get open() { return !!this.el; }

        close() {
            if (!this.el) return;
            this.el.remove();
            this.el = null;
            document.removeEventListener('pointerdown', this.away, true);
            this.app.canvas.el.focus({ preventScroll: true });
        }

        // Opens at a screen point. `context`: { from: a port a wire was dragged from } or { member: a node whose
        // object's members to pick }, or null
        show(clientX, clientY, context) {
            this.close();
            const app = this.app;
            const editor = app.editor;
            const g = editor.graph;
            if (!g) return;
            const at = app.canvas.toWorld(clientX, clientY);
            this.context = context;
            this.at = at;
            this.entries = this.catalog(g, context);
            this.live = [];
            this.asking = false;
            this.chosen = 0;
            const input = el('input.tg-input.tg-search-input', { type: 'text', placeholder: this.placeholder(context), spellcheck: 'false' });
            this.list = el('div.tg-search-list');
            this.status = el('div.tg-search-status');
            this.el = el('div.tg-search', null, input, this.status, this.list);
            document.body.append(this.el);
            const r = this.el.getBoundingClientRect();
            this.el.style.left = Math.max(4, Math.min(clientX, innerWidth - r.width - 8)) + 'px';
            this.el.style.top = Math.max(4, Math.min(clientY, innerHeight - 360)) + 'px';
            this.input = input;
            input.addEventListener('input', () => { this.chosen = 0; this.fill(); });
            input.addEventListener('keydown', e => {
                e.stopPropagation();
                if (e.key === 'Escape') this.close();
                else if (e.key === 'ArrowDown') { e.preventDefault(); this.chosen++; this.fill(); }
                else if (e.key === 'ArrowUp') { e.preventDefault(); this.chosen = Math.max(0, this.chosen - 1); this.fill(); }
                else if (e.key === 'Enter') { e.preventDefault(); const pick = this.shown[this.chosen]; if (pick) this.pick(pick); }
            });
            this.away = e => { if (this.el && !this.el.contains(e.target)) this.close(); };
            setTimeout(() => document.addEventListener('pointerdown', this.away, true));
            input.focus();
            this.fill();
            this.ask(g, context);
        }

        placeholder(context) {
            if (context?.member) return 'A member of its object…';
            const from = context?.from;
            if (!from) return 'A node to add…';
            if (from.exec) return from.input ? 'A statement before it…' : 'A statement after it…';
            return from.input ? 'A value for it…' : 'What to do with the value…';
        }

        // What the editor knows without tidels: every kind of node, the graph's locals, the routine's parameters
        catalog(g, context) {
            const s = this.app.editor.schema;
            const doc = this.app.editor.doc;
            const out = [];
            const from = context?.from;
            const wantExec = from?.exec;
            const wantValue = from && !from.exec && from.input;
            const takesValue = from && !from.exec && !from.input;
            if (context?.member) return out;
            for (const spec of s.nodes) {
                if (wantExec && !spec.exec) continue;
                if (wantValue && !spec.value) continue;
                if (takesValue && !(spec.inputs?.length || spec.lists?.length || spec.items?.length || spec.map)) continue;
                const category = s.categoryByName.get(spec.category);
                out.push({ title: spec.title, detail: spec.k, group: category?.title || spec.category, color: category?.color, node: { k: spec.k, fields: {} } });
            }
            if (!wantExec) {
                for (const d of doc.locals(g)) {
                    out.push({ title: s.declaredLocal(d), detail: 'local', group: 'Locals', color: s.categoryByName.get('locals')?.color, node: { k: 'local', fields: { ref: d.id } } });
                }
                const routine = doc.routineOf(this.app.editor.slot);
                const params = [...(routine?.params || []), ...(routine?.params || []).flatMap(p => p.query || [])];
                for (const p of params) {
                    if (p.name) out.push({ title: p.name, detail: `${p.mode && p.mode !== 'read' ? p.mode + ' ' : ''}${p.type ?? ''}`, group: 'Parameters', node: { k: 'name', fields: { name: p.name } } });
                }
                if (this.app.editor.doc.find(this.app.editor.slot.owner)?.role === 'member') out.push({ title: 'this', detail: 'this', group: 'Values', node: { k: 'this', fields: {} } });
            }
            return out;
        }

        // What tidels offers, which comes in when it answers
        ask(g, context) {
            const app = this.app;
            const lang = app.lang;
            if (lang.status === 'unavailable' && !lang.text) return;
            const from = context?.from;
            const memberOf = context?.member ? context.member.object : from && !from.exec && !from.input && from.node > 0 ? from.node : 0;
            this.asking = true;
            this.fill();
            const done = items => {
                if (!this.el || this.context !== context) return;
                this.asking = false;
                const known = new Set(this.entries.map(e => e.title));
                this.live = items.filter(e => e && !known.has(e.title));
                this.fill();
            };
            if (memberOf) {
                lang.members(memberOf).then(({ items, type }) => {
                    this.type = type;
                    done(items.map(i => G.lang.entryOf(i, { member: true })));
                });
                return;
            }
            const flow = !!from?.exec;
            const place = lang.placeFor(from?.exec ? (from.input ? { before: from.node } : { after: from.node }) : from?.input ? { valueFor: from.node } : null, app.editor.slot.owner);
            lang.completion(place).then(items => {
                const params = new Set(this.entries.filter(e => e.group === 'Parameters' || e.group === 'Locals').map(e => e.title));
                done(items.map(i => G.lang.entryOf(i, { flow })).filter(e => e && !params.has(e.title) && (!flow || e.node.k === 'call')));
            });
        }

        fill() {
            if (!this.el) return;
            const typed = this.input.value.trim();
            const all = [...this.live.map(e => ({ ...e, live: true })), ...this.entries];
            const scored = all.map(e => ({ e, r: rank(e.title, typed) || (typed && rank(e.group || '', typed) ? 0.5 : 0) }))
                .filter(x => x.r > 0)
                .sort((a, b) => b.r - a.r || (b.e.live ? 1 : 0) - (a.e.live ? 1 : 0));
            this.shown = scored.map(x => x.e).slice(0, 200);
            this.chosen = Math.min(this.chosen, Math.max(0, this.shown.length - 1));
            this.status.textContent = this.asking ? 'Asking tidels…' : this.type ? `Members of ${this.type}` : '';
            this.status.style.display = this.status.textContent ? 'block' : 'none';
            this.list.replaceChildren();
            let group = null;
            this.shown.forEach((e, i) => {
                const g = typed ? null : e.group;
                if (g && g !== group) {
                    group = g;
                    this.list.append(el('div.tg-search-group', { text: g }));
                }
                const row = el('div.tg-search-item' + (i === this.chosen ? '.tg-chosen' : ''), {
                    onpointerdown: ev => ev.stopPropagation(),
                    onclick: () => this.pick(e),
                    onpointerenter: () => { this.chosen = i; for (const r of this.list.querySelectorAll('.tg-chosen')) r.classList.remove('tg-chosen'); row.classList.add('tg-chosen'); },
                }, el('span.tg-swatch', { style: { background: e.color || this.app.editor.schema.color(e.node.k) } }),
                el('span.tg-search-title', { text: e.title }), el('span.tg-search-detail', { text: e.detail || '' }));
                this.list.append(row);
                if (i === this.chosen) requestAnimationFrame(() => row.scrollIntoView?.({ block: 'nearest' }));
            });
            if (!this.shown.length && !this.asking) this.list.append(el('div.tg-search-none', { text: 'Nothing by that name' }));
        }

        pick(entry) {
            const context = this.context, at = this.at;
            this.close();
            if (context?.member) this.app.retarget(context.member, entry);
            else this.app.addFromSearch(entry, at, context);
        }
    }

    G.Search = Search;
    G.search = { rank };
})(globalThis.TideGraph = globalThis.TideGraph || {});
