// The board's two panels (notes/visual-scripting.md, Round three), as Blueprint has them:
//
//   My game, on the left: the Variables of the code in hand (dragged onto it, a Get or a Set of one), then the file's
//   declarations by the schema's groups, a "+" on each group's heading that adds one
//   of its kinds, a click goes to one, a double click renames it, and one dragged out goes where it's dropped (a
//   component or a singleton onto a frame: what it needs; a function into a frame: its call; a constant onto an
//   input: its value).
//
//   Details, on the right: what's selected, its name, type and starting value; its kind's options as ticks with
//   their help under them; "Advanced", closed, for the rest; its note. With nothing selected, the file's own.
//
// Each can be closed, and stays as it was left. On a phone each is a sheet over the board.
(function (G) {
    'use strict';
    const { el, field, button, typeColor, toast } = G.ui;
    const w = (key, vars) => G.w(key, vars);
    const { set } = G.json;

    // Options that are for packages and the like: the schema's modifierInfo doesn't say which (yet), so these go
    // under Advanced by name

    // ---------------------------------------------------------------------------------------------------------------
    // My game

    class MyGame {
        constructor(app) {
            this.app = app;
            this.el = el('div.tg-panel.tg-mygame', { dataset: { panel: 'mygame' } });
            this.filter = '';
            this.drag = null;
            this.openVars = new Set(); // Variables opened to their fields, by frame and path
            this.fields = new Map();   // What a type has, as tidels said: type -> [{ name, type }]
            this.varsOf = 0;           // The frame whose variables are listed
            // Drawn again once what had the cursor in it lets go, when something changed meanwhile (refresh)
            this.el.addEventListener('focusout', () => requestAnimationFrame(() => {
                if (this.stale && this.app.open.mygame && !this.busy()) this.render();
            }));
        }

        // Whether something in it other than its search has the cursor (its search is drawn again with it)
        busy() {
            const a = document.activeElement;
            return !!a && a !== this.input && this.el.contains(a);
        }

        // Draws it again for what changed (its problems, its variables), or once what has the cursor lets go
        refresh() {
            if (this.busy()) { this.stale = true; return; }
            this.render();
        }

        // The Variables of the code in hand (Blueprint's My Blueprint): what the frame takes and its locals, each
        // opening to its fields. Dragged onto the frame, one asks "Get <name>" or "Set <name>" (Ctrl makes a Get at
        // once, Alt a Set); dropped on an input, it's a Get wired to it.
        variables(list, filter) {
            const app = this.app;
            const f = app.variablesFrame();
            this.varsOf = f?.item?.id || 0;
            const vars = f ? this.variablesOf(f) : [];
            const shown = vars.filter(v => !filter || v.text.toLowerCase().includes(filter));
            G.ui.put(list, el('div.tg-mygame-group.tg-mygame-vars', { dataset: { group: 'variables' }, title: w('variables.tip') },
                el('span.tg-mygame-group-title', { text: w('variables.title') }),
                el('span.tg-mygame-count', { text: shown.length ? String(shown.length) : '' }),
                el('span.tg-grow'),
                f ? el('span.tg-mygame-of', { text: app.editor.doc.titleOf(f.item) }) : null));
            if (!f) {
                G.ui.put(list, el('div.tg-mygame-none', { text: w('variables.noFrame') }));
                return;
            }
            if (!shown.length) {
                G.ui.put(list, el('div.tg-mygame-none', { text: filter ? w('mygame.noMatch') : w('variables.none') }));
                return;
            }
            for (const v of shown) this.varRows(list, f, v, 0);
        }

        // What a frame's code has: { text, start, members, type }
        variablesOf(f) {
            const doc = this.app.editor.doc;
            const s = this.app.editor.schema;
            const out = f.pins.map(pin => ({ text: pin.name, start: { name: pin.name }, members: [], type: f.typeOfPort({ pin: pin.name }) }));
            for (const d of f.graph ? doc.locals(f.graph) : []) {
                const name = s.declaredLocal(d);
                if (name) out.push({ text: name, start: { local: d.id }, members: [], type: f.typeOfPort({ local: d.id }) });
            }
            return out;
        }

        // A variable's row, and its fields' rows when it's open
        varRows(list, f, v, depth) {
            const app = this.app;
            const ed = app.editor;
            const s = ed.schema;
            const key = `${f.item.id}|${v.text}`;
            const control = s.control(v.type);
            const opens = !!v.type && depth < 6 && (!control || control === 'vector' || control === 'colour') && !ed.doc.enumMembers(v.type) && app.lang.declares(v.type) !== 'enum';
            const open = opens && this.openVars.has(key);
            const toggle = opens
                ? button(open ? '▾' : '▸', () => { if (open) this.openVars.delete(key); else this.openVars.add(key); this.render(); }, open ? w('variables.close') : w('variables.open'), 'tg-icon.tg-var-toggle')
                : el('span.tg-var-toggle');
            // (A field's row is set in in its name's column, so the column is as wide as that needs)
            const name = G.ui.cell(1, toggle, el('span.tg-swatch', { style: { background: typeColor(v.type) || 'var(--tg-muted)' } }),
                el('span.tg-mygame-name', { text: depth ? v.members.at(-1) : v.text }));
            name.style.paddingLeft = 6 + 14 * depth + 'px';
            const row = el('div.tg-mygame-item.tg-mygame-var.tg-colrow', { dataset: { var: v.text }, title: w(v.type ? 'variables.row.typed' : 'variables.row', { name: v.text, type: v.type }) },
                name, G.ui.cell(2, el('span.tg-mygame-kind', { text: v.type || '' })));
            row.addEventListener('pointerdown', e => this.downVar(e, row, f, v));
            // Over it, its Gets on the board light, and where it comes from
            const read = v.start.local !== undefined ? `local:${v.start.local}` : `pin:${v.start.name}`;
            row.addEventListener('pointerenter', () => app.board.lightRead(f, read));
            row.addEventListener('pointerleave', () => app.board.lightRead(null, null));
            row.addEventListener('pointermove', e => this.move(e));
            row.addEventListener('pointerup', e => this.up(e));
            row.addEventListener('pointercancel', () => this.end());
            G.ui.put(list, row);
            if (!open) return;
            const fields = this.fieldsOf(v.type);
            if (!fields) {
                G.ui.put(list, el('div.tg-mygame-none', { text: w('variables.asking'), style: { paddingLeft: 20 + 14 * depth + 'px' } }));
                return;
            }
            for (const m of fields) this.varRows(list, f, { text: `${v.text}.${m.name}`, start: v.start, members: [...v.members, m.name], type: m.type }, depth + 1);
        }

        // A type's fields: the file's, else tidels's (asked once, the list drawn again when it answers); null while
        // it's asked
        fieldsOf(type) {
            const app = this.app;
            const own = app.editor.doc.membersOf(type).filter(m => !m.method);
            if (own.length) return own;
            if (this.fields.has(type)) return this.fields.get(type);
            if (app.lang.status === 'unavailable') return [];
            this.fields.set(type, null);
            app.lang.membersOfType(type).then(items => {
                const K = G.lang.KIND;
                this.fields.set(type, items.filter(i => [K.Field, K.Property, K.Constant, K.Variable, K.Value].includes(i.kind) && typeof i.label === 'string')
                    .map(i => ({ name: i.label.replace(/\(.*$/, ''), type: G.pickers.typeIn(i.detail) })));
                if (this.app.open.mygame) this.render();
            });
            return null;
        }

        downVar(e, row, f, v) {
            if (e.button !== 0 || e.target.closest('input, button')) return;
            this.drag = { start: { x: e.clientX, y: e.clientY }, row, variable: { frame: f, ...v }, moved: false, ghost: null, id: e.pointerId };
            try { row.setPointerCapture(e.pointerId); } catch {}
        }

        render() {
            const app = this.app;
            const ed = app.editor;
            const box = this.el;
            if (!ed || ed.error) { box.replaceChildren(); return; }
            // (not while one of its rows is dragged out: the row holds the pointer until it's let go)
            if (this.drag) { this.stale = true; return; }
            this.stale = false;
            const s = ed.schema;
            const focused = document.activeElement === this.input;
            const input = el('input.tg-input.tg-panel-filter', { type: 'text', placeholder: w('mygame.filter'), spellcheck: 'false', value: this.filter });
            input.addEventListener('input', () => { this.filter = input.value; this.fill(list); });
            input.addEventListener('keydown', e => {
                e.stopPropagation();
                // Enter goes to the first that's found; Escape empties the search and goes back to the board
                if (e.key === 'Enter') {
                    const first = list.querySelector('.tg-mygame-item[data-id]');
                    if (first) app.reveal(Number(first.dataset.id));
                } else if (e.key === 'Escape') {
                    this.filter = '';
                    input.value = '';
                    this.fill(list);
                    app.board.el.focus({ preventScroll: true });
                }
            });
            this.input = input;
            // Its rows line up: name | what it is (a variable's type)
            const list = el('div.tg-mygame-list.tg-cols');
            this.fill(list);
            box.replaceChildren(
                el('div.tg-panel-head', null, el('span.tg-panel-title', { text: w('mygame.title') }), el('span.tg-grow'),
                    button(w('close'), () => app.togglePanel('mygame', false), w('mygame.close'), 'tg-icon')),
                input, list);
            if (focused) input.focus();
            void s;
        }

        fill(list) {
            const app = this.app;
            const ed = app.editor;
            const s = ed.schema;
            const entries = G.pickers.contents(app);
            const text = this.filter.trim().toLowerCase();
            list.replaceChildren();
            this.variables(list, text);
            for (const group of s.groups) {
                // (found by its name or by what it is: "method")
                const found = e => !text || e.title.toLowerCase().includes(text) || String(e.detail || '').toLowerCase().includes(text);
                const mine = entries.filter(e => e.group === group.title && !e.of && found(e));
                G.ui.put(list, el('div.tg-mygame-group', { dataset: { group: group.name }, title: group.summary },
                    el('span.tg-mygame-group-title', { text: group.title }),
                    el('span.tg-mygame-count', { text: mine.length ? String(mine.length) : '' }),
                    el('span.tg-grow'),
                    button('+', e => G.pickers.addMenu(app, 0, 0, { anchor: e.currentTarget, group: group.name }), w('mygame.addTo', { group: group.title, summary: group.summary }), 'tg-icon.tg-mygame-add')));
                if (!mine.length) {
                    G.ui.put(list, el('div.tg-mygame-none', { text: text ? w('mygame.noMatch') : w('mygame.empty', { summary: group.summary.toLowerCase() }) }));
                    continue;
                }
                // A type's members with code (a method, a property's get) under it
                for (const e of mine) {
                    G.ui.put(list, this.itemOf(e));
                    for (const m of entries.filter(x => x.of === e.id && found(x))) G.ui.put(list, this.itemOf(m, 1));
                }
            }
        }

        itemOf(entry, depth = 0) {
            const app = this.app;
            const ed = app.editor;
            const item = ed.doc.items().find(i => i.id === entry.id);
            const v = item?.value;
            const renamable = item && !item.member && !item.accessor && item.spec?.keys.includes('name') && v.kind !== 'operator';
            const name = renamable
                ? G.ui.nameText(v.name ?? '', x => app.renameDecl(item, x), { className: 'tg-mygame-name', placeholder: w('placeholder.name'), check: G.cards.checkName, fk: `mygame:${v.id}:name`, title: w('mygame.item.tip') })
                : el('span.tg-mygame-name', { text: entry.title, title: w('mygame.item.go') });
            const lead = G.ui.cell(1, el('span.tg-swatch', { style: { background: item?.card ? typeColor(v?.name) || 'var(--tg-data)' : 'var(--tg-behaviour)' } }), name);
            lead.style.paddingLeft = 14 + 16 * depth + 'px';
            const row = el('div.tg-mygame-item.tg-colrow' + (ed.picked.has(entry.id) ? '.tg-on' : ''), { dataset: { id: entry.id, kind: v?.kind || '' } },
                lead,
                G.ui.cell(2, el('span.tg-mygame-kind', { text: entry.detail })),
                G.ui.cell(3, entry.problems ? el('span.tg-badge.tg-badge-error', { text: String(entry.problems) }) : null));
            row.addEventListener('pointerdown', e => this.down(e, row, item));
            row.addEventListener('pointermove', e => this.move(e));
            row.addEventListener('pointerup', e => this.up(e));
            row.addEventListener('pointercancel', () => this.end());
            row.addEventListener('dblclick', e => {
                if (e.target.closest('input')) return;
                name.rename?.(true);
            });
            return row;
        }

        down(e, row, item) {
            if (e.button !== 0 || e.target.closest('input, button') || !item) return;
            this.drag = { start: { x: e.clientX, y: e.clientY }, row, item, moved: false, ghost: null, id: e.pointerId };
            try { row.setPointerCapture(e.pointerId); } catch {}
        }

        move(e) {
            const d = this.drag;
            if (!d) return;
            if (!d.moved && Math.abs(e.clientX - d.start.x) + Math.abs(e.clientY - d.start.y) < 6) return;
            if (!d.moved) {
                d.moved = true;
                d.ghost = el('div.tg-drag-ghost', { text: d.variable ? d.variable.text : this.app.editor.doc.titleOf(d.item) });
                document.body.append(d.ghost);
            }
            // A variable says what it will be: Ctrl a Get, Alt a Set
            if (d.variable) d.ghost.textContent = e.ctrlKey || e.metaKey ? w('menu.get', { name: d.variable.text }) : e.altKey ? w('menu.set', { name: d.variable.text }) : d.variable.text;
            d.ghost.style.left = e.clientX + 12 + 'px';
            d.ghost.style.top = e.clientY + 8 + 'px';
            // What it would go into lights
            for (const x of document.querySelectorAll('.tg-drop-ok')) x.classList.remove('tg-drop-ok');
            const under = document.elementFromPoint(e.clientX, e.clientY);
            const target = d.variable ? this.app.variableTarget(d.variable, under) : this.app.dropTarget(d.item, under);
            target?.el?.classList.add('tg-drop-ok');
            d.ghost.classList.toggle('tg-drag-ghost-ok', !!target);
        }

        up(e) {
            const d = this.drag;
            this.end();
            if (!d) return;
            if (d.variable) {
                if (d.moved) this.app.dropVariable(d.variable, e.clientX, e.clientY, { get: e.ctrlKey || e.metaKey, set: e.altKey });
                else this.app.revealVariable(d.variable);
                return;
            }
            if (!d.moved) {
                // A click: go to it
                this.app.reveal(d.item.id);
                if (innerWidth < 640) this.app.togglePanel('mygame', false);
                return;
            }
            this.app.dropFromGame(d.item, e.clientX, e.clientY);
        }

        end() {
            const d = this.drag;
            this.drag = null;
            if (d?.ghost) d.ghost.remove();
            for (const x of document.querySelectorAll('.tg-drop-ok')) x.classList.remove('tg-drop-ok');
            if (this.stale) requestAnimationFrame(() => { if (this.stale && this.app.open.mygame) this.render(); });
        }
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Details

    class Details {
        constructor(app) {
            this.app = app;
            this.el = el('div.tg-panel.tg-details', { dataset: { panel: 'details' } });
            this.advancedOpen = false;
        }

        render() {
            const app = this.app;
            const ed = app.editor;
            const box = this.el;
            if (!ed || ed.error) { box.replaceChildren(); return; }
            const head = el('div.tg-panel-head', null, el('span.tg-panel-title', { text: w('details.title') }), el('span.tg-grow'),
                button(w('close'), () => app.togglePanel('details', false), w('details.close'), 'tg-icon'));
            const body = el('div.tg-details-body');
            let what = 'file';
            if (ed.selection.size === 1) what = 'node';
            else if (ed.selection.size > 1 || ed.picked.size > 1) what = 'many';
            else if (ed.row) what = 'row';
            else if (ed.picked.size === 1) what = 'item';
            try {
                if (what === 'node') this.node(body, [...ed.selection][0]);
                else if (what === 'row') this.row(body, ed.row);
                else if (what === 'item') this.item(body, [...ed.picked][0]);
                else if (what === 'many') G.ui.put(body, el('div.tg-details-hint', { text: w('details.many', { count: ed.selection.size || ed.picked.size }) }));
                else this.file(body);
            } catch (e) {
                console.error(e);
            }
            box.dataset.what = what;
            box.replaceChildren(head, body);
            this.lineUp(body);
        }

        // Its lines line up: label | control, the labels' column as wide as the widest label in Details, those in a
        // closed Advanced too (measured as text, since they aren't drawn), so opening it moves nothing
        lineUp(body) {
            const labels = [...body.querySelectorAll('.tg-details-label')];
            if (!labels.length) return;
            const ctx = (Details.measure ||= document.createElement('canvas').getContext('2d'));
            let widest = 0;
            for (const label of labels) {
                const style = getComputedStyle(label);
                ctx.font = `${style.fontStyle} ${style.fontWeight} ${style.fontSize} ${style.fontFamily}`;
                widest = Math.max(widest, ctx.measureText(label.textContent).width);
            }
            body.style.setProperty('--tg-details-label', Math.ceil(widest) + 'px');
        }

        // -- What Details is made of

        section(body, title, ...children) {
            const s = el('div.tg-details-section', null, title ? el('div.tg-details-heading', { text: title }) : null, children);
            G.ui.put(body, s);
            return s;
        }

        line(label, control, help) {
            return el('div.tg-details-line', null, el('span.tg-details-label', { text: label }), el('span.tg-details-control', null, control), help ? el('div.tg-details-help', { text: help }) : null);
        }

        // An option as a tick, its help written under it
        tick(label, help, checked, commit, name) {
            const box = el('input', { type: 'checkbox', checked: !!checked, onchange: e => commit(e.target.checked) });
            return el('label.tg-details-tick', { dataset: { option: name || label } }, box,
                el('span.tg-details-tick-text', null, el('span.tg-details-tick-label', { text: label }), help ? el('span.tg-details-help', { text: help }) : null));
        }

        advanced(body, children) {
            const shown = children.flat(Infinity).filter(Boolean);
            if (!shown.length) return;
            const d = el('details.tg-details-advanced', { open: this.advancedOpen }, el('summary', { text: w('details.advanced') }), shown);
            d.addEventListener('toggle', () => { this.advancedOpen = d.open; });
            G.ui.put(body, d);
        }

        nameField(holder, order, { fk, rename, check = G.cards.checkName, placeholder = w('placeholder.name') } = {}) {
            const app = this.app;
            return field(holder.name ?? '', x => app.edit(d => {
                const old = holder.name;
                if (holder.name === undefined && order) set(holder, 'name', x, order); else holder.name = x;
                rename?.(d, old, x);
            }), { className: 'tg-input.tg-details-input', placeholder, check, fk });
        }

        note(body, holder, order) {
            const app = this.app;
            const s = app.editor.schema;
            if (!order?.includes('comment')) return;
            const area = field(holder.comment ?? '', x => app.edit(() => set(holder, 'comment', x.trim() ? x : undefined, order)), {
                multiline: true, rows: 3, className: 'tg-input.tg-details-note', placeholder: s.keyHelp('comment') || w('details.note'), fk: `details:${holder.id}:comment`,
            });
            this.section(body, s.keyLabel('comment'), area);
        }

        order(list, v, why) {
            const app = this.app;
            const i = list.indexOf(v);
            if (i < 0 || list.length < 2) return null;
            const swap = to => () => app.edit(() => { list.splice(to, 0, list.splice(i, 1)[0]); });
            return el('div.tg-details-line', null, el('span.tg-details-label', { text: w('details.order') }),
                el('span.tg-details-control', null,
                    button(w('details.earlier'), swap(i - 1), w('details.earlier.tip'), 'tg-button.tg-small' + (i > 0 ? '' : '.tg-hidden')),
                    button(w('details.later'), swap(i + 1), w('details.later.tip'), 'tg-button.tg-small' + (i < list.length - 1 ? '' : '.tg-hidden'))),
                why ? el('div.tg-details-help', { text: why }) : null);
        }

        title(body, chip, group, text, summary) {
            G.ui.put(body, el('div.tg-details-title', null, el('span.tg-chip.tg-chip-' + group, { text: chip }), el('span.tg-details-name', { text }),
                summary ? el('div.tg-details-help', { text: summary }) : null));
        }

        attributes(v, order) {
            const app = this.app;
            const s = app.editor.schema;
            if (!order.includes('attributes')) return null;
            const list = v.attributes || [];
            return this.line(s.keyLabel('attributes'), el('span.tg-details-attributes', null,
                list.length ? list.map(a => el('span.tg-tag.tg-tag-attribute', { text: G.cards.attributeText(a) })) : el('span.tg-muted', { text: w('details.none') }),
                button(w('details.change'), e => G.cards.editAttributes(app, v, order, { x: e.clientX, y: e.clientY }), s.keyHelp('attributes'), 'tg-button.tg-small')), s.keyHelp('attributes'));
        }

        // -- A card or a frame

        item(body, id) {
            const app = this.app;
            const ed = app.editor;
            const s = ed.schema;
            const doc = ed.doc;
            const item = doc.items().find(i => i.id === id);
            if (!item) return this.file(body);
            const v = item.value;
            const spec = item.accessor ? null : item.spec;
            const keys = item.accessor ? s.element('accessor') : spec?.keys || [];
            const has = k => keys.includes(k);
            const edit = change => app.edit(change);
            const group = item.member || item.accessor ? 'behaviour' : (spec?.group || 'More').toLowerCase();
            this.title(body, item.accessor ? s.memberSpec('property')?.title || w('details.property') : spec?.title || v.kind, group, doc.titleOf(item), spec?.summary);
            const main = [];
            if (has('name') && v.kind !== 'operator') main.push(this.line(s.keyLabel('name'), field(v.name ?? '', x => app.renameDecl(item, x), { className: 'tg-input.tg-details-input', placeholder: w('placeholder.name'), check: G.cards.checkName, fk: `details:${v.id}:name` })));
            if (has('type')) main.push(this.line(s.keyLabel('type'), G.cards.typeChip(app, { type: v.type, at: v.id, own: v.typeParams || [], fk: `details:${v.id}:type`, onPick: t => edit(d => {
                set(v, 'type', t, keys);
                if (has('value') && G.values.classify(v.value).kind !== 'graph') set(v, 'value', G.cards.defaultValue(app, d, t), keys);
            }) })));
            if (has('value')) main.push(this.line(s.keyLabel('value'), G.values.control(app, { holder: v, key: 'value', type: v.kind === 'asset' ? 'string' : v.type, slot: { owner: v.id, key: 'value' }, order: keys, required: true, fk: `details:${v.id}:value` })));
            if (has('returnType')) main.push(this.line(s.keyLabel('returnType'), G.cards.typeChip(app, { type: v.returnType, at: v.id, own: v.typeParams || [], allow: { void: true }, fk: `details:${v.id}:returnType`, onPick: t => edit(() => set(v, 'returnType', t, keys)) })));
            if (v.trigger && typeof v.trigger === 'object') {
                const t = v.trigger;
                const torder = s.element('trigger');
                main.push(this.line(s.keyLabel('trigger'), el('span.tg-mono', { text: t.changed ? `Changed<${t.type ?? ''}>` : t.type ?? '' })));
                main.push(this.tick(s.keyLabel('verified'), s.keyHelp('verified'), t.verified, on => edit(() => set(t, 'verified', on ? true : undefined, torder)), 'verified'));
                main.push(this.tick(w('details.changed'), w('details.changed.help'), t.changed, on => edit(() => set(t, 'changed', on ? true : undefined, torder)), 'changed'));
            }
            // A scene that's a name alone (scene Arena;): no members of its own; while it has some, they say why not
            if (v.kind === 'scene') {
                const empty = v.members === null;
                const box = this.tick(w('details.sceneEmpty'), (v.members || []).length ? w('details.sceneEmpty.has') : w('details.sceneEmpty.help'), empty, on => edit(() => set(v, 'members', on ? null : [], keys)), 'scene-empty');
                if ((v.members || []).length) box.querySelector('input').disabled = true;
                main.push(box);
            }
            if (main.length) this.section(body, null, main);

            // Its options: ticks with their help
            const options = [];
            const later = [];
            const mods = has('modifiers') ? spec?.modifiers || [] : [];
            const modTick = m => this.tick(s.modifierLabel(m), s.modifierHelp(m), (v.modifiers || []).includes(m), on => edit(() => {
                const next = new Set(v.modifiers || []);
                if (on) next.add(m); else next.delete(m);
                const ordered = s.allModifiers.filter(x => next.has(x));
                set(v, 'modifiers', ordered.length ? ordered : undefined, keys);
            }), 'mod-' + m);
            for (const m of mods) (s.modifierAdvanced(m) ? later : options).push(modTick(m));
            const flag = k => this.tick(s.keyLabel(k), s.keyHelp(k), v[k], on => edit(() => set(v, k, on ? true : undefined, keys)), k);
            for (const k of ['mut', 'local']) if (has(k)) (s.keyAdvanced(k) ? later : options).push(flag(k));
            if (options.length) this.section(body, s.keyLabel('modifiers'), options);

            // What a frame does with its nodes
            if (item.frame && v.body && !v.extern) {
                this.section(body, null, el('div.tg-details-buttons', null,
                    button(w('details.tidy'), () => app.layoutFrame(item.id), w('details.tidy.tip'), 'tg-button.tg-small'),
                    button(w('menu.addNote'), () => app.addNote(item.id), w('details.note.tip'), 'tg-button.tg-small')));
            }

            // The rest
            if (has('overrides') && (v.modifiers || []).includes('override')) {
                later.push(this.line(s.keyLabel('overrides'), field(v.overrides ?? '', x => edit(() => {
                    set(v, 'overrides', x || undefined, keys);
                    if (x) v.name = x.split('.').pop();
                }), { className: 'tg-input.tg-details-input', placeholder: 'Package.Name', check: x => (x === '' ? null : G.cards.checkPath(x)), fk: `details:${v.id}:overrides` }), w('details.overrides')));
            }
            if (has('extern') && (v.extern || !doc.graph({ owner: v.id, key: 'body' })?.nodes?.length)) {
                later.push(this.tick(s.keyLabel('extern'), s.keyHelp('extern'), v.extern, on => edit(() => {
                    set(v, 'extern', on ? true : undefined, keys);
                    if (on) delete v.body; else set(v, 'body', { entry: null, nodes: [] }, keys);
                }), 'extern'));
            }
            later.push(this.attributes(v, keys));
            if (has('typeParams')) {
                later.push(this.line(s.keyLabel('typeParams'), field((v.typeParams || []).join(', '), x => edit(() => {
                    const names = x.split(',').map(n => n.trim()).filter(Boolean);
                    set(v, 'typeParams', names.length ? names : undefined, keys);
                }), { className: 'tg-input.tg-details-input', placeholder: 'T, U', check: x => (x.split(',').map(n => n.trim()).filter(Boolean).every(n => !G.cards.checkName(n)) ? null : w('check.typeParams')), fk: `details:${v.id}:typeParams` }), s.keyHelp('typeParams')));
            }
            if (has('where') && v.typeParams?.length) {
                later.push(this.line(s.keyLabel('where'), button((v.where || []).map(w => `${w.param}: ${(w.rules || []).join(', ')}`).join('; ') || w('details.none'), e => G.cards.promptRows({
                    x: e.clientX, y: e.clientY, title: s.keyLabel('where'), help: s.keyHelp('where'), rows: (v.where || []).map(w => ({ param: w.param, rules: (w.rules || []).join(', ') })),
                    columns: [{ key: 'param', placeholder: v.typeParams[0], width: '5em', check: x => (v.typeParams.includes(x) ? null : w('check.oneOf', { list: v.typeParams.join(', ') })) }, { key: 'rules', placeholder: 'comparable, hashable' }],
                    addLabel: w('details.rule'), blank: () => ({ param: v.typeParams[0], rules: 'comparable' }),
                    onDone: rows => edit(() => set(v, 'where', rows.length ? rows.map(r => ({ param: r.param, rules: r.rules.split(',').map(x => x.trim()).filter(Boolean) })) : undefined, keys)),
                }), s.keyHelp('where'), 'tg-button.tg-small'), s.keyHelp('where')));
            }
            if (has('fails')) {
                later.push(this.line(s.keyLabel('fails'), G.cards.typeChip(app, { type: v.fails, at: v.id, placeholder: w('details.fails.none'), allow: { none: w('details.fails.none'), noneSummary: w('details.fails.summary') }, fk: `details:${v.id}:fails`, onPick: t => edit(() => set(v, 'fails', t, keys)) }), s.keyHelp('fails')));
            }
            if (has('backing')) {
                later.push(this.line(s.keyLabel('backing'), G.cards.typeChip(app, { type: v.backing, placeholder: w('details.backing.none'), plain: true, allow: { none: w('details.backing.none') }, fk: `details:${v.id}:backing`, onPick: t => edit(() => set(v, 'backing', t, keys)) }), s.keyHelp('backing')));
            }
            // Where it is on the board says nothing of when its code runs: that's the file's order
            if (Array.isArray(item.list) && !item.member && !item.accessor) later.push(this.order(item.list, v, item.frame ? w('details.order.code') : w('details.order.data')));
            this.advanced(body, later);
            this.note(body, v, keys);
        }

        // -- A row of a card or of a frame's entry

        row(body, id) {
            const app = this.app;
            const ed = app.editor;
            const s = ed.schema;
            const doc = ed.doc;
            const e = doc.find(id);
            if (!e) return this.file(body);
            const v = e.value;
            const edit = change => app.edit(change);
            const item = doc.items().find(i => i.id === ed.itemOf(id));
            const later = [];
            if (e.role === 'member') {
                const spec = s.memberSpec(v.kind);
                const keys = spec?.keys || [];
                this.title(body, spec?.title || v.kind, 'data', `${e.owner?.name ?? ''}.${v.name ?? ''}`, spec?.summary);
                const main = [];
                if (keys.includes('name')) main.push(this.line(s.keyLabel('name'), this.nameField(v, keys, { fk: `details:${v.id}:name` })));
                if (keys.includes('type')) main.push(this.line(s.keyLabel('type'), G.cards.typeChip(app, { type: v.type, at: v.id, own: e.owner?.typeParams || [], fk: `details:${v.id}:type`, onPick: t => edit(d => {
                    set(v, 'type', t, keys);
                    if (v.kind === 'field') delete v.default;
                    if (v.kind === 'tweak') set(v, 'value', G.cards.defaultValue(app, d, t), keys);
                }) })));
                const valueKey = keys.includes('default') ? 'default' : keys.includes('value') ? 'value' : null;
                if (valueKey) {
                    main.push(this.line(s.keyLabel(valueKey), el('span.tg-details-value', null,
                        G.values.control(app, { holder: v, key: valueKey, type: v.type, slot: { owner: v.id, key: valueKey }, order: keys, fk: `details:${v.id}:value`, required: v.kind === 'tweak' }),
                        button(w('menu.work'), () => app.openValue({ owner: v.id, key: valueKey }, v, valueKey, keys, v.type), w('details.work.tip'), 'tg-button.tg-small'))));
                }
                if (spec?.body) main.push(button(w('details.goToCode'), () => app.reveal(v.id), w('details.goToCode.tip'), 'tg-button.tg-small'));
                if (main.length) this.section(body, null, main);
                if (keys.includes('local')) later.push(this.tick(s.keyLabel('local'), s.keyHelp('local'), v.local, on => edit(() => set(v, 'local', on ? true : undefined, keys)), 'local'));
                later.push(this.attributes(v, keys));
                later.push(this.order(e.list, v));
                this.advanced(body, later);
                this.note(body, v, keys);
                return;
            }
            if (e.role === 'enumMember' || e.role === 'entry') {
                const order = s.element(e.role);
                this.title(body, e.role === 'entry' ? w('details.setting') : w('details.choice'), 'data', v.name ?? '', null);
                this.section(body, null,
                    this.line(s.keyLabel('name'), this.nameField(v, order, { fk: `details:${v.id}:name` })),
                    this.line(s.keyLabel('value'), G.values.control(app, { holder: v, key: 'value', type: e.role === 'enumMember' ? 'int' : null, slot: { owner: v.id, key: 'value' }, order, fk: `details:${v.id}:value`, required: e.role === 'entry', none: 'auto' })));
                this.advanced(body, [this.order(e.list, v)]);
                this.note(body, v, order);
                return;
            }
            if (e.role === 'param' || e.role === 'sampleParam' || e.role === 'payload') {
                const order = s.element(e.role);
                const routine = e.owner;
                const filter = s.modeIsFilter(v.mode);
                const g = item?.frame ? doc.graph({ owner: item.id, key: 'body' }) : null;
                const rename = (d, old, x) => d.renamePin(g, old, x);
                this.title(body, e.role === 'payload' ? s.keyLabel('payload') : s.keyLabel('params'), 'behaviour', v.name ?? v.type ?? '', null);
                const main = [];
                if (!filter && !v.ownership) main.push(this.line(s.keyLabel('name'), this.nameField(v, order, { fk: `details:${v.id}:name`, rename })));
                if (e.role === 'param') {
                    const modes = s.paramModes.filter(m => !s.modeAdvanced(m) || m === v.mode || m === 'out');
                    main.push(this.line(w('details.mode'), G.ui.select(v.mode ?? 'read', modes.map(m => [m, s.modeLabel(m)]), m => edit(() => {
                        set(v, 'mode', m, order);
                        if (s.modeIsFilter(m)) { delete v.name; delete v.default; }
                        else if (!v.name) set(v, 'name', G.pickers.nameFor(v.type), order);
                    }), s.modeHelp(v.mode ?? 'read'), 'tg-select.tg-details-select'), s.modeHelp(v.mode ?? 'read')));
                }
                // A filter of owning (with ownership): no component of its own; turned off, it asks for the component
                if (filter && e.role === 'param') {
                    main.push(this.tick(w('details.ownership'), w('details.ownership.help'), v.ownership, on => {
                        if (on) { edit(() => { set(v, 'ownership', true, order); delete v.type; }); return; }
                        G.pickers.typePicker(app, { kinds: ['component'], details: ['component'], ask: () => app.lang.completionInParams(routine), title: w('details.ownership.off'),
                            anchor: this.el.querySelector('[data-option="ownership"]'),
                            onPick: t => edit(() => { delete v.ownership; set(v, 'type', t, order); }) });
                        this.render();
                    }, 'ownership'));
                }
                if (!v.ownership && (v.type !== undefined || order.includes('type'))) {
                    main.push(this.line(s.keyLabel('type'), G.cards.typeChip(app, { type: v.type, at: v.id, fk: `details:${v.id}:type`, onPick: t => edit(() => set(v, 'type', t, order)) })));
                }
                if (order.includes('default') && !filter && v.mode !== 'query') {
                    main.push(this.line(s.keyLabel('default'), button(v.default ? w('details.change') : w('details.giveOne'), () => app.openValue({ owner: v.id, key: 'default' }, v, 'default', order, v.type), w('details.default.help'), 'tg-button.tg-small'), w('details.default.help')));
                }
                if (main.length) this.section(body, null, main);
                for (const k of ['local', 'verified', 'previous']) if (order.includes(k)) later.push(this.tick(s.keyLabel(k), s.keyHelp(k), v[k], on => edit(() => set(v, k, on ? true : undefined, order)), k));
                if (order.includes('role')) later.push(this.line(s.keyLabel('role'), field(v.role ?? '', x => edit(() => set(v, 'role', x || undefined, order)), { className: 'tg-input.tg-details-input', placeholder: 'Raw', fk: `details:${v.id}:role` })));
                later.push(this.attributes(v, order));
                later.push(this.order(e.list, v));
                this.advanced(body, later);
                this.note(body, v, order);
                void routine;
                return;
            }
            if (item) this.item(body, item.id);
        }

        // -- A node

        node(body, id) {
            const app = this.app;
            const ed = app.editor;
            const s = ed.schema;
            const e = ed.doc.find(id);
            if (!e) return this.file(body);
            if (e.role === 'note') {
                this.title(body, w('title.note'), 'more', '', w('details.note.tip'));
                const n = e.value;
                this.section(body, null, field(n.text ?? '', x => app.edit(() => { n.text = x; }), { multiline: true, rows: 5, className: 'tg-input.tg-details-note', fk: `details:${n.id}:text` }));
                return;
            }
            const frame = app.board.frameOf(e.slot);
            const a = frame?.analysis;
            const n = e.value;
            const shown = a?.merged.has(n.id) ? a.byId.get(a.merged.get(n.id)) : n;
            const spec = s.node(shown.k);
            this.title(body, s.category(shown.k)?.title || w('details.node'), 'behaviour', s.title(shown.k), s.help(shown.k));
            const edit = change => app.edit(change);
            const main = [], later = [];
            const local = s.declaredLocal(shown);
            if (local && typeof shown.name === 'string') {
                main.push(this.line(s.keyLabel('name'), field(shown.name, x => edit(() => { shown.name = x; }), { className: 'tg-input.tg-details-input', check: G.cards.checkName, fk: `details:${shown.id}:name` })));
            }
            for (const target of shown === n ? [n] : [shown, n]) {
                const order = s.nodeOrder(target.k);
                for (const key of s.node(target.k)?.keys || []) {
                    if (s.keyType(key, target) !== 'flag' || (target.k === 'bool' && key === 'value')) continue;
                    const t = this.tick(s.flagLabel(key), s.flagHelp(key), target[key], on => edit(() => set(target, key, on ? true : undefined, order)), key);
                    (s.flagAdvanced(key) ? later : main).push(t);
                }
                if (s.node(target.k)?.keys.includes('typeArgs')) {
                    later.push(this.line(w('details.typeArgs'), el('span.tg-details-attributes', null,
                        (target.typeArgs || []).map((t, i) => el('span.tg-tag', null, t, button('×', () => edit(() => { target.typeArgs.splice(i, 1); if (!target.typeArgs.length) delete target.typeArgs; }), w('details.typeArg.delete'), 'tg-icon'))),
                        button(w('details.add'), ev => G.pickers.typePicker(app, { x: ev.clientX, y: ev.clientY, title: w('details.typeArg'), onPick: t => edit(() => { if (!Array.isArray(target.typeArgs)) set(target, 'typeArgs', [], order); target.typeArgs.push(t); }) }), 'For a generic call: Name<T>', 'tg-button.tg-small')), 'For a generic call: Name<T>'));
                }
            }
            if ((shown.k === 'member' || shown.k === 'method') && shown.object && frame) {
                main.push(button(shown.k === 'method' ? w('details.otherMethod') : w('details.otherField'), ev => G.pickers.nodeSearch(app, frame, ev.clientX, ev.clientY, { member: shown }), w('details.sameValue'), 'tg-button.tg-small'));
            }
            if (main.length) this.section(body, null, main);
            this.advanced(body, later);
            this.note(body, n, s.nodeOrder(n.k));
            void spec;
        }

        // -- The file

        file(body) {
            const app = this.app;
            const ed = app.editor;
            const root = ed.doc.root;
            const order = ed.schema.root;
            G.ui.put(body, el('div.tg-details-hint', { text: w('details.file.hint') }));
            const check = v => (v === '' || /^[A-Za-z_][A-Za-z0-9_]*(\.[A-Za-z_][A-Za-z0-9_]*)*$/.test(v) ? null : w('check.namespace'));
            this.section(body, w('details.file'),
                this.line(w('details.namespace'), field(root.namespace ?? '', x => app.edit(() => G.json.set(root, 'namespace', x || undefined, order)), { className: 'tg-input.tg-details-input', placeholder: w('details.none'), check, fk: 'details:file:namespace' }),
                    w('details.namespace.help')),
                this.line(w('details.uses'), field((root.using || []).join(', '), x => app.edit(() => {
                    const names = x.split(',').map(n => n.trim()).filter(Boolean);
                    G.json.set(root, 'using', names.length ? names : undefined, order);
                }), { className: 'tg-input.tg-details-input', placeholder: 'Physics, Combat', check: x => (x.split(',').map(n => n.trim()).filter(Boolean).every(n => !check(n)) ? null : w('check.uses')), fk: 'details:file:using' }),
                    w('menu.uses.summary')));
            this.section(body, null, el('div.tg-details-buttons', null,
                button(w('menu.viewTide'), () => app.post({ type: 'showTide', text: app.lang.text ?? undefined }), w('menu.viewTide.summary'), 'tg-button.tg-small'),
                button(w('menu.tidyBoard'), () => app.layoutBoard(), w('menu.tidyBoard.summary'), 'tg-button.tg-small')));
        }
    }

    G.panels = { MyGame, Details };
})(globalThis.TideGraph = globalThis.TideGraph || {});
