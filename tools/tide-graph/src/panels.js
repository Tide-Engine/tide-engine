// The board's two panels (notes/visual-scripting.md, Round three), as Blueprint has them:
//
//   My game, on the left: the file's declarations by the schema's groups, a "+" on each group's heading that adds one
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
        }

        render() {
            const app = this.app;
            const ed = app.editor;
            const box = this.el;
            if (!ed || ed.error) { box.replaceChildren(); return; }
            const s = ed.schema;
            const focused = document.activeElement === this.input;
            const input = el('input.tg-input.tg-panel-filter', { type: 'text', placeholder: 'Find in this file…', spellcheck: 'false', value: this.filter });
            input.addEventListener('input', () => { this.filter = input.value; this.fill(list); });
            input.addEventListener('keydown', e => e.stopPropagation());
            this.input = input;
            const list = el('div.tg-mygame-list');
            this.fill(list);
            box.replaceChildren(
                el('div.tg-panel-head', null, el('span.tg-panel-title', { text: 'My game' }), el('span.tg-grow'),
                    button('×', () => app.togglePanel('mygame', false), 'Close My game', 'tg-icon')),
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
            for (const group of s.groups) {
                const mine = entries.filter(e => e.group === group.title && (!text || e.title.toLowerCase().includes(text)));
                G.ui.put(list, el('div.tg-mygame-group', { dataset: { group: group.name }, title: group.summary },
                    el('span.tg-mygame-group-title', { text: group.title }),
                    el('span.tg-mygame-count', { text: mine.length ? String(mine.length) : '' }),
                    el('span.tg-grow'),
                    button('+', e => G.pickers.addMenu(app, 0, 0, { anchor: e.currentTarget, group: group.name }), `Add to ${group.title}: ${group.summary}`, 'tg-icon.tg-mygame-add')));
                if (!mine.length) {
                    G.ui.put(list, el('div.tg-mygame-none', { text: text ? 'Nothing by that name' : `Nothing yet: + adds ${group.summary.toLowerCase()}` }));
                    continue;
                }
                for (const e of mine) G.ui.put(list, this.itemOf(e));
            }
        }

        itemOf(entry) {
            const app = this.app;
            const ed = app.editor;
            const item = ed.doc.items().find(i => i.id === entry.id);
            const v = item?.value;
            const renamable = item && !item.member && !item.accessor && item.spec?.keys.includes('name') && v.kind !== 'operator';
            const name = renamable
                ? G.ui.nameText(v.name ?? '', x => app.renameDecl(item, x), { className: 'tg-mygame-name', placeholder: 'Name', check: G.cards.checkName, fk: `mygame:${v.id}:name`, title: 'Click to go to it · double-click to rename · drag it out to use it' })
                : el('span.tg-mygame-name', { text: entry.title, title: 'Click to go to it' });
            const row = el('div.tg-mygame-item' + (ed.picked.has(entry.id) ? '.tg-on' : ''), { dataset: { id: entry.id, kind: v?.kind || '' } },
                el('span.tg-swatch', { style: { background: item?.card ? typeColor(v?.name) || 'var(--tg-data)' : 'var(--tg-behaviour)' } }),
                name, el('span.tg-mygame-kind', { text: entry.detail }),
                entry.problems ? el('span.tg-badge.tg-badge-error', { text: String(entry.problems) }) : null);
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
                d.ghost = el('div.tg-drag-ghost', { text: this.app.editor.doc.titleOf(d.item) });
                document.body.append(d.ghost);
            }
            d.ghost.style.left = e.clientX + 12 + 'px';
            d.ghost.style.top = e.clientY + 8 + 'px';
            // What it would go into lights
            for (const x of document.querySelectorAll('.tg-drop-ok')) x.classList.remove('tg-drop-ok');
            const target = this.app.dropTarget(d.item, document.elementFromPoint(e.clientX, e.clientY));
            target?.el?.classList.add('tg-drop-ok');
            d.ghost.classList.toggle('tg-drag-ghost-ok', !!target);
        }

        up(e) {
            const d = this.drag;
            this.end();
            if (!d) return;
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
            const head = el('div.tg-panel-head', null, el('span.tg-panel-title', { text: 'Details' }), el('span.tg-grow'),
                button('×', () => app.togglePanel('details', false), 'Close Details', 'tg-icon'));
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
                else if (what === 'many') G.ui.put(body, el('div.tg-details-hint', { text: `${ed.selection.size || ed.picked.size} selected: they move, copy and delete together. Select one to see its details.` }));
                else this.file(body);
            } catch (e) {
                console.error(e);
            }
            box.dataset.what = what;
            box.replaceChildren(head, body);
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
            const d = el('details.tg-details-advanced', { open: this.advancedOpen }, el('summary', { text: 'Advanced' }), shown);
            d.addEventListener('toggle', () => { this.advancedOpen = d.open; });
            G.ui.put(body, d);
        }

        nameField(holder, order, { fk, rename, check = G.cards.checkName, placeholder = 'Name' } = {}) {
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
                multiline: true, rows: 3, className: 'tg-input.tg-details-note', placeholder: s.keyHelp('comment') || 'A note for people reading this', fk: `details:${holder.id}:comment`,
            });
            this.section(body, s.keyLabel('comment'), area);
        }

        order(list, v, why) {
            const app = this.app;
            const i = list.indexOf(v);
            if (i < 0 || list.length < 2) return null;
            const swap = to => () => app.edit(() => { list.splice(to, 0, list.splice(i, 1)[0]); });
            return el('div.tg-details-line', null, el('span.tg-details-label', { text: 'Order' }),
                el('span.tg-details-control', null,
                    button('↑ Earlier', swap(i - 1), 'Move it up', 'tg-button.tg-small' + (i > 0 ? '' : '.tg-hidden')),
                    button('↓ Later', swap(i + 1), 'Move it down', 'tg-button.tg-small' + (i < list.length - 1 ? '' : '.tg-hidden'))),
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
                list.length ? list.map(a => el('span.tg-tag.tg-tag-attribute', { text: G.cards.attributeText(a) })) : el('span.tg-muted', { text: 'None' }),
                button('Change…', e => G.cards.editAttributes(app, v, order, { x: e.clientX, y: e.clientY }), s.keyHelp('attributes'), 'tg-button.tg-small')), s.keyHelp('attributes'));
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
            this.title(body, item.accessor ? s.memberSpec('property')?.title || 'Property' : spec?.title || v.kind, group, doc.titleOf(item), spec?.summary);
            const main = [];
            if (has('name') && v.kind !== 'operator') main.push(this.line(s.keyLabel('name'), field(v.name ?? '', x => app.renameDecl(item, x), { className: 'tg-input.tg-details-input', placeholder: 'Name', check: G.cards.checkName, fk: `details:${v.id}:name` })));
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
                main.push(this.tick('When a field of it changes', 'Changed<…>: runs when the field changes, not when an event is sent', t.changed, on => edit(() => set(t, 'changed', on ? true : undefined, torder)), 'changed'));
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
                    button('Tidy up its nodes', () => app.layoutFrame(item.id), 'Lays its nodes out again, left to right', 'tg-button.tg-small'),
                    button('Add a note box', () => app.addNote(item.id), 'A box to write in, behind the nodes', 'tg-button.tg-small')));
            }

            // The rest
            if (has('overrides') && (v.modifiers || []).includes('override')) {
                later.push(this.line(s.keyLabel('overrides'), field(v.overrides ?? '', x => edit(() => {
                    set(v, 'overrides', x || undefined, keys);
                    if (x) v.name = x.split('.').pop();
                }), { className: 'tg-input.tg-details-input', placeholder: 'Package.Name', check: x => (x === '' ? null : G.cards.checkPath(x)), fk: `details:${v.id}:overrides` }), 'The declaration of a package it takes the place of'));
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
                }), { className: 'tg-input.tg-details-input', placeholder: 'T, U', check: x => (x.split(',').map(n => n.trim()).filter(Boolean).every(n => !G.cards.checkName(n)) ? null : 'Names, like T, U'), fk: `details:${v.id}:typeParams` }), s.keyHelp('typeParams')));
            }
            if (has('where') && v.typeParams?.length) {
                later.push(this.line(s.keyLabel('where'), button((v.where || []).map(w => `${w.param}: ${(w.rules || []).join(', ')}`).join('; ') || 'None', e => G.cards.promptRows({
                    x: e.clientX, y: e.clientY, title: s.keyLabel('where'), help: s.keyHelp('where'), rows: (v.where || []).map(w => ({ param: w.param, rules: (w.rules || []).join(', ') })),
                    columns: [{ key: 'param', placeholder: v.typeParams[0], width: '5em', check: x => (v.typeParams.includes(x) ? null : `One of: ${v.typeParams.join(', ')}`) }, { key: 'rules', placeholder: 'comparable, hashable' }],
                    addLabel: 'rule', blank: () => ({ param: v.typeParams[0], rules: 'comparable' }),
                    onDone: rows => edit(() => set(v, 'where', rows.length ? rows.map(r => ({ param: r.param, rules: r.rules.split(',').map(x => x.trim()).filter(Boolean) })) : undefined, keys)),
                }), s.keyHelp('where'), 'tg-button.tg-small'), s.keyHelp('where')));
            }
            if (has('fails')) {
                later.push(this.line(s.keyLabel('fails'), G.cards.typeChip(app, { type: v.fails, at: v.id, placeholder: 'It can\'t fail', allow: { none: 'It can\'t fail', noneSummary: 'It always gives its result' }, fk: `details:${v.id}:fails`, onPick: t => edit(() => set(v, 'fails', t, keys)) }), s.keyHelp('fails')));
            }
            if (has('backing')) {
                later.push(this.line(s.keyLabel('backing'), G.cards.typeChip(app, { type: v.backing, placeholder: 'As the compiler picks', plain: true, allow: { none: 'As the compiler picks' }, fk: `details:${v.id}:backing`, onPick: t => edit(() => set(v, 'backing', t, keys)) }), s.keyHelp('backing')));
            }
            // Where it is on the board says nothing of when its code runs: that's the file's order
            if (Array.isArray(item.list) && !item.member && !item.accessor) later.push(this.order(item.list, v, item.frame ? "Code runs in the file's order, unless a stage says otherwise" : "The file's order, as its Tide is written"));
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
                        button('Work it out…', () => app.openValue({ owner: v.id, key: valueKey }, v, valueKey, keys, v.type), 'A small graph of nodes, for more than a number or a text', 'tg-button.tg-small'))));
                }
                if (spec?.body) main.push(button('Go to its code', () => app.reveal(v.id), 'Its code is a frame on the board', 'tg-button.tg-small'));
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
                this.title(body, e.role === 'entry' ? 'Setting' : 'Choice', 'data', v.name ?? '', null);
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
                    main.push(this.line('How it takes it', G.ui.select(v.mode ?? 'read', modes.map(m => [m, s.modeLabel(m)]), m => edit(() => {
                        set(v, 'mode', m, order);
                        if (s.modeIsFilter(m)) { delete v.name; delete v.default; }
                        else if (!v.name) set(v, 'name', G.pickers.nameFor(v.type), order);
                    }), s.modeHelp(v.mode ?? 'read'), 'tg-select.tg-details-select'), s.modeHelp(v.mode ?? 'read')));
                }
                if (v.type !== undefined || order.includes('type')) {
                    main.push(this.line(s.keyLabel('type'), G.cards.typeChip(app, { type: v.type, at: v.id, fk: `details:${v.id}:type`, onPick: t => edit(() => set(v, 'type', t, order)) })));
                }
                if (order.includes('default') && !filter && v.mode !== 'query') {
                    main.push(this.line(s.keyLabel('default'), button(v.default ? 'Change…' : 'Give it one…', () => app.openValue({ owner: v.id, key: 'default' }, v, 'default', order, v.type), 'The value it has when a call leaves it out', 'tg-button.tg-small'), 'The value it has when a call leaves it out'));
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
                this.title(body, 'Note box', 'more', '', 'A box to write in, behind the nodes');
                const n = e.value;
                this.section(body, null, field(n.text ?? '', x => app.edit(() => { n.text = x; }), { multiline: true, rows: 5, className: 'tg-input.tg-details-note', fk: `details:${n.id}:text` }));
                return;
            }
            const frame = app.board.frameOf(e.slot);
            const a = frame?.analysis;
            const n = e.value;
            const shown = a?.merged.has(n.id) ? a.byId.get(a.merged.get(n.id)) : n;
            const spec = s.node(shown.k);
            this.title(body, s.category(shown.k)?.title || 'Node', 'behaviour', s.title(shown.k), s.help(shown.k));
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
                    later.push(this.line('Type arguments', el('span.tg-details-attributes', null,
                        (target.typeArgs || []).map((t, i) => el('span.tg-tag', null, t, button('×', () => edit(() => { target.typeArgs.splice(i, 1); if (!target.typeArgs.length) delete target.typeArgs; }), 'Delete this type', 'tg-icon'))),
                        button('Add…', ev => G.pickers.typePicker(app, { x: ev.clientX, y: ev.clientY, title: 'Type argument', onPick: t => edit(() => { if (!Array.isArray(target.typeArgs)) set(target, 'typeArgs', [], order); target.typeArgs.push(t); }) }), 'For a generic call: Name<T>', 'tg-button.tg-small')), 'For a generic call: Name<T>'));
                }
            }
            if ((shown.k === 'member' || shown.k === 'method') && shown.object && frame) {
                main.push(button(shown.k === 'method' ? 'Call another of its methods…' : 'Pick another of its fields…', ev => G.pickers.nodeSearch(app, frame, ev.clientX, ev.clientY, { member: shown }), 'Of the same value', 'tg-button.tg-small'));
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
            G.ui.put(body, el('div.tg-details-hint', { text: 'Select a card, a frame, a node or a row to see its details here. This is the file’s own.' }));
            const check = v => (v === '' || /^[A-Za-z_][A-Za-z0-9_]*(\.[A-Za-z_][A-Za-z0-9_]*)*$/.test(v) ? null : 'Names joined by dots, like Game.Combat');
            this.section(body, 'This file',
                this.line('Namespace', field(root.namespace ?? '', x => app.edit(() => G.json.set(root, 'namespace', x || undefined, order)), { className: 'tg-input.tg-details-input', placeholder: 'None', check, fk: 'details:file:namespace' }),
                    'A name its declarations go under, like Game.Combat. Empty for none: what it declares is seen everywhere.'),
                this.line('Uses', field((root.using || []).join(', '), x => app.edit(() => {
                    const names = x.split(',').map(n => n.trim()).filter(Boolean);
                    G.json.set(root, 'using', names.length ? names : undefined, order);
                }), { className: 'tg-input.tg-details-input', placeholder: 'Physics, Combat', check: x => (x.split(',').map(n => n.trim()).filter(Boolean).every(n => !check(n)) ? null : 'Namespaces, like Physics'), fk: 'details:file:using' }),
                    'The namespaces whose names it uses without writing them'));
            this.section(body, null, el('div.tg-details-buttons', null,
                button('View as Tide', () => app.post({ type: 'showTide', text: app.lang.text ?? undefined }), 'The Tide code this file stands for', 'tg-button.tg-small'),
                button('Tidy up the board', () => app.layoutBoard(), 'Data in a column, code beside it', 'tg-button.tg-small')));
        }
    }

    G.panels = { MyGame, Details };
})(globalThis.TideGraph = globalThis.TideGraph || {});
