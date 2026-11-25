// A card: a declaration of data on the board, edited where it's drawn. Its kind in plain words, its name, a row per
// field (name, type, starting value), a last row to add one, and a "…" menu with what else its kind can have and
// nothing more. What a card shows comes from its kind's keys in the schema: members, entries, a type and a value,
// a prefab's entities, text.
(function (G) {
    'use strict';
    const { el, field, button, menu, popup, typeColor } = G.ui;
    const { set } = G.json;

    const NAME = /^[A-Za-z_][A-Za-z0-9_]*$/;
    const checkName = v => (NAME.test(v) ? null : v === '' ? 'It needs a name' : 'A name is letters, digits and _, and starts with a letter');
    const checkPath = v => (/^[A-Za-z_][A-Za-z0-9_]*(\.[A-Za-z_][A-Za-z0-9_]*)*$/.test(v) ? null : 'Names joined by dots, like Game.Combat');

    // A type, shown as a chip and changed by the type picker: never typed free. Typing a letter on it opens the
    // picker with that letter.
    function typeChip(app, { type, at = 0, onPick, allow, own, fk, kinds, details, ask, title, placeholder = 'pick a type', plain }) {
        const b = el('button.tg-type' + (type ? '' : '.tg-unset'), { type: 'button', title: title ?? (type ? `${type} · click to change` : 'Pick a type') },
            type ? el('span.tg-type-dot', { style: { background: typeColor(type) } }) : null,
            el('span.tg-type-name', { text: type || placeholder }));
        if (fk) b.dataset.fk = fk;
        const open = typed => G.pickers.typePicker(app, {
            anchor: b, at, current: type, own, allow, kinds, details, ask, typed, plain,
            onPick: t => { if (fk) app.board.focusSoon(fk); onPick(t); },
        });
        b.addEventListener('click', e => { e.stopPropagation(); open(''); });
        b.addEventListener('pointerdown', e => e.stopPropagation());
        b.addEventListener('keydown', e => {
            e.stopPropagation();
            if (e.key.length === 1 && /[A-Za-z]/.test(e.key) && !e.ctrlKey && !e.metaKey && !e.altKey) {
                e.preventDefault();
                open(e.key);
            }
        });
        return b;
    }

    // -----------------------------------------------------------------------------------------------------------
    // Small forms the "…" menus open: a text, a list of names, attributes, a generic's rules

    function form(title, body, { x, y, anchor, done, help }) {
        let p = null;
        const ok = button('Done', () => p.close(), null, 'tg-button.tg-primary');
        p = popup({
            x, y, anchor, className: 'tg-popup tg-form',
            content: [el('div.tg-form-title', { text: title }), help ? el('div.tg-form-help', { text: help }) : null, body, el('div.tg-form-actions', null, ok)],
            onClose: done,
        });
        ((body.matches('input, textarea') ? body : body.querySelector('input, textarea')) || ok).focus();
        return p;
    }

    function promptText({ title, help, value, multiline = false, placeholder, check, mono, onDone, x, y, anchor }) {
        let text = value ?? '';
        const input = field(text, v => { text = v; }, { multiline, rows: multiline ? Math.max(3, Math.min(14, String(text).split('\n').length + 1)) : 1, placeholder, check, mono, className: 'tg-input.tg-form-input', onEnter: i => { i.blur(); p.close(); } });
        input.addEventListener('input', () => { if (!check || !check(input.value)) text = input.value; });
        const p = form(title, input, { x, y, anchor, help, done: () => { if (text !== (value ?? '')) onDone(text); } });
        return p;
    }

    // Rows of small inputs with a way to add and remove one: `columns` [{ key, placeholder, check, mono, width }];
    // `rows` are objects of those keys
    function promptRows({ title, help, rows, columns, addLabel, blank, onDone, x, y, anchor }) {
        const state = rows.map(r => ({ ...r }));
        const before = JSON.stringify(state);
        const list = el('div.tg-form-rows');
        const draw = focus => {
            list.replaceChildren(...state.map((row, i) => el('div.tg-form-row', null,
                columns.map(c => {
                    const input = field(row[c.key] ?? '', v => { row[c.key] = v; }, { placeholder: c.placeholder, check: c.check, mono: c.mono, className: 'tg-input.tg-form-input' });
                    input.addEventListener('input', () => { if (!c.check || !c.check(input.value)) row[c.key] = input.value; });
                    if (c.width) input.style.width = c.width;
                    return input;
                }),
                button('×', () => { state.splice(i, 1); draw(); }, 'Remove', 'tg-icon'))),
                button('+ ' + addLabel, () => { state.push(blank()); draw(state.length - 1); }, null, 'tg-add-row'));
            if (focus !== undefined) list.children[focus]?.querySelector('input')?.focus();
        };
        draw();
        return form(title, list, { x, y, anchor, help, done: () => { if (JSON.stringify(state) !== before) onDone(state); } });
    }

    // -----------------------------------------------------------------------------------------------------------
    // The "…" menu of a card, a frame or a member: what its kind can have, by the schema's keys

    function moreMenu(app, item, x, y, { anchor = null } = {}) {
        const ed = app.editor;
        const s = ed.schema;
        const doc = ed.doc;
        const v = item.value;
        const spec = item.spec;
        const keys = item.accessor ? s.element('accessor') : spec?.keys || [];
        const order = keys;
        const has = key => keys.includes(key);
        const at = { x, y };
        const items = [];
        // What else its kind can have
        if (item.card && spec?.memberKinds) {
            for (const m of s.memberKinds(v.kind)) {
                const there = m.once && (v.members || []).some(x2 => x2.kind === m.kind);
                items.push({ label: m.title, summary: m.summary, group: 'Add', name: 'add-' + m.kind, disabled: there, why: 'It has one already', run: () => app.addMember(item, m.kind) });
            }
        }
        const mods = spec?.modifiers || [];
        if (mods.length && has('modifiers')) {
            const have = new Set(v.modifiers || []);
            for (const m of mods) {
                items.push({
                    label: s.modifierLabel(m), summary: s.modifierHelp(m), group: s.keyLabel('modifiers'), checked: have.has(m), stay: true, name: 'mod-' + m,
                    run: on => app.edit(() => {
                        const next = new Set(v.modifiers || []);
                        if (on) next.add(m); else next.delete(m);
                        const ordered = s.allModifiers.filter(x2 => next.has(x2));
                        set(v, 'modifiers', ordered.length ? ordered : undefined, order);
                    }),
                });
            }
        }
        const flag = key => ({
            label: s.keyLabel(key), summary: s.keyHelp(key), group: s.keyLabel('modifiers'), checked: !!v[key], stay: true, name: 'flag-' + key,
            run: on => app.edit(() => set(v, key, on ? true : undefined, order)),
        });
        if (has('local')) items.push(flag('local'));
        if (has('mut')) items.push(flag('mut'));
        if (has('extern') && (v.extern || !doc.graph({ owner: v.id, key: 'body' })?.nodes?.length)) {
            items.push({
                ...flag('extern'),
                run: on => app.edit(d => {
                    set(v, 'extern', on ? true : undefined, order);
                    if (on) delete v.body; else set(v, 'body', { entry: null, nodes: [] }, order);
                }),
            });
        }
        const more = [];
        if (has('typeParams')) {
            more.push({
                label: `${s.keyLabel('typeParams')}…`, summary: s.keyHelp('typeParams'), name: 'typeParams',
                run: () => promptRows({
                    ...at, title: s.keyLabel('typeParams'), help: s.keyHelp('typeParams'), rows: (v.typeParams || []).map(name => ({ name })),
                    columns: [{ key: 'name', placeholder: 'T', check: checkName }], addLabel: 'type parameter', blank: () => ({ name: 'T' }),
                    onDone: rows => app.edit(() => set(v, 'typeParams', rows.length ? rows.map(r => r.name) : undefined, order)),
                }),
            });
        }
        if (has('where') && v.typeParams?.length) {
            more.push({
                label: `${s.keyLabel('where')}…`, summary: s.keyHelp('where'), name: 'where',
                run: () => promptRows({
                    ...at, title: s.keyLabel('where'), help: s.keyHelp('where'), rows: (v.where || []).map(w => ({ param: w.param, rules: (w.rules || []).join(', ') })),
                    columns: [{ key: 'param', placeholder: v.typeParams[0], width: '5em', check: x2 => (v.typeParams.includes(x2) ? null : `One of: ${v.typeParams.join(', ')}`) },
                        { key: 'rules', placeholder: 'comparable, hashable' }],
                    addLabel: 'rule', blank: () => ({ param: v.typeParams[0], rules: 'comparable' }),
                    onDone: rows => app.edit(() => set(v, 'where', rows.length ? rows.map(r => ({ param: r.param, rules: r.rules.split(',').map(x2 => x2.trim()).filter(Boolean) })) : undefined, order)),
                }),
            });
        }
        if (has('fails')) {
            more.push({
                label: `${s.keyLabel('fails')}…`, summary: v.fails ? v.fails : s.keyHelp('fails'), name: 'fails',
                run: () => G.pickers.typePicker(app, { ...at, at: v.id, current: v.fails, title: s.keyLabel('fails'), allow: { none: 'It can\'t fail', noneSummary: 'It always gives its result' },
                    onPick: t => app.edit(() => set(v, 'fails', t, order)) }),
            });
        }
        if (has('backing')) {
            more.push({
                label: `${s.keyLabel('backing')}…`, summary: v.backing || s.keyHelp('backing'), name: 'backing',
                run: () => G.pickers.typePicker(app, { ...at, current: v.backing, title: s.keyLabel('backing'), plain: true, allow: { none: 'As the compiler picks' },
                    onPick: t => app.edit(() => set(v, 'backing', t, order)) }),
            });
        }
        if (has('overrides') && (v.modifiers || []).includes('override')) {
            more.push({
                label: `${s.keyLabel('overrides')}…`, summary: v.overrides || 'The declaration of a package it takes the place of', name: 'overrides',
                run: () => promptText({ ...at, title: s.keyLabel('overrides'), value: v.overrides, placeholder: 'Package.Name', check: x2 => (x2 === '' ? null : checkPath(x2)),
                    onDone: text => app.edit(() => {
                        set(v, 'overrides', text || undefined, order);
                        if (text) v.name = text.split('.').pop();
                    }) }),
            });
        }
        if (has('attributes')) {
            more.push({
                label: `${s.keyLabel('attributes')}…`, summary: (v.attributes || []).map(attributeText).join(' ') || s.keyHelp('attributes'), name: 'attributes',
                run: () => editAttributes(app, v, order, at),
            });
        }
        if (has('comment')) {
            more.push({
                label: `${s.keyLabel('comment')}…`, summary: v.comment ? firstLine(v.comment) : s.keyHelp('comment'), name: 'comment',
                run: () => editNote(app, v, order, at),
            });
        }
        for (const m of more) items.push({ ...m, group: 'More' });
        if (item.frame && !item.value.extern && v.body) {
            items.push({ label: 'Tidy up its nodes', summary: 'Lay them out again, left to right', group: 'Arrange', name: 'layout', run: () => app.layoutFrame(item.id) });
            items.push({ label: 'Add a note box', summary: 'A box to write in, behind the nodes', group: 'Arrange', name: 'note-box', run: () => app.addNote(item.id) });
        }
        const list = item.list;
        // Where it is on the board says nothing of when its code runs: that's the file's order, changed here
        if (Array.isArray(list) && !item.member && !item.accessor) {
            const i = list.indexOf(v);
            const why = item.frame ? "Code runs in the file's order, unless a stage says otherwise" : "The file's order, as its Tide is written";
            const swap = to => () => app.edit(() => { list.splice(to, 0, list.splice(i, 1)[0]); });
            if (i > 0) items.push({ label: 'Move earlier in the file', summary: `Before ${doc.describe(doc.find(list[i - 1].id)) || 'the one before it'} · ${why}`, group: 'Arrange', name: 'earlier', run: swap(i - 1) });
            if (i >= 0 && i < list.length - 1) items.push({ label: 'Move later in the file', summary: `After ${doc.describe(doc.find(list[i + 1].id)) || 'the one after it'} · ${why}`, group: 'Arrange', name: 'later', run: swap(i + 1) });
        }
        items.push({ separator: true });
        if (Array.isArray(list) && !item.accessor) {
            items.push({ label: 'Duplicate', name: 'duplicate', hint: 'Ctrl+D', run: () => app.duplicateItem(item) });
        }
        items.push({ label: 'Delete', name: 'delete', danger: true, hint: 'Del', run: () => app.deleteItems([item.id]) });
        return menu(x, y, items, { anchor, title: `${item.accessor ? s.memberSpec('property')?.title || '' : spec?.title || ''} ${doc.titleOf(item)}`.trim() });
    }

    function attributeText(a) {
        return `[${a.name ?? ''}${a.args !== undefined ? `(${a.args})` : ''}]`;
    }

    function firstLine(text) {
        const i = String(text).indexOf('\n');
        return i < 0 ? String(text) : String(text).slice(0, i) + ' …';
    }

    function editAttributes(app, v, order, at) {
        const s = app.editor.schema;
        const old = v.attributes || [];
        promptRows({
            ...at, title: s.keyLabel('attributes'), help: s.keyHelp('attributes'),
            rows: old.map(a => ({ id: a.id, name: a.name, args: a.args ?? '' })),
            columns: [{ key: 'name', placeholder: 'Stage', width: '8em', check: checkName }, { key: 'args', placeholder: 'what is in its ( ), if any', mono: true }],
            addLabel: 'attribute', blank: () => ({ name: 'Sparse', args: '' }),
            onDone: rows => app.edit(doc => {
                const next = rows.map(r => {
                    const a = {};
                    set(a, 'id', r.id || doc.newId(), s.element('attribute'));
                    set(a, 'name', r.name, s.element('attribute'));
                    if (r.args !== '') set(a, 'args', r.args, s.element('attribute'));
                    return a;
                });
                set(v, 'attributes', next.length ? next : undefined, order);
            }),
        });
    }

    function editNote(app, v, order, at) {
        const s = app.editor.schema;
        promptText({ ...at, title: s.keyLabel('comment'), help: s.keyHelp('comment'), value: v.comment, multiline: true, placeholder: 'What it is for…',
            onDone: text => app.edit(() => set(v, 'comment', text.trim() ? text : undefined, order)) });
    }

    // The tags under a title: its options in words, its attributes; and its note
    function tagsOf(app, item) {
        const s = app.editor.schema;
        const v = item.value;
        const order = item.spec?.keys || [];
        const out = [];
        for (const m of v.modifiers || []) out.push(el('span.tg-tag', { text: s.modifierLabel(m), title: s.modifierHelp(m) }));
        if (v.local === true) out.push(el('span.tg-tag', { text: s.keyLabel('local'), title: s.keyHelp('local') }));
        if (v.extern === true) out.push(el('span.tg-tag', { text: s.keyLabel('extern') }));
        if (v.typeParams?.length) out.push(el('span.tg-tag', { text: `${s.keyLabel('typeParams')} ${v.typeParams.join(', ')}`, title: s.keyHelp('typeParams') }));
        if (v.fails) out.push(el('span.tg-tag', { text: `${s.keyLabel('fails')} ${v.fails}`, title: s.keyHelp('fails') }));
        if (v.backing) out.push(el('span.tg-tag', { text: `${s.keyLabel('backing')} ${v.backing}` }));
        for (const a of v.attributes || []) {
            out.push(el('button.tg-tag.tg-tag-attribute', { type: 'button', text: attributeText(a), title: s.keyHelp('attributes'), onpointerdown: e => e.stopPropagation(),
                onclick: e => { e.stopPropagation(); editAttributes(app, v, order, { x: e.clientX, y: e.clientY }); } }));
        }
        return out;
    }

    function noteOf(app, item) {
        const v = item.value;
        if (!v.comment) return null;
        const order = item.accessor ? app.editor.schema.element('accessor') : item.spec?.keys || [];
        return el('div.tg-comment', { text: v.comment, title: v.comment, onpointerdown: e => e.stopPropagation(),
            onclick: e => { e.stopPropagation(); editNote(app, v, order, { x: e.clientX, y: e.clientY }); } });
    }

    // -----------------------------------------------------------------------------------------------------------
    // A card

    function render(board, item) {
        const app = board.app;
        const ed = app.editor;
        const s = ed.schema;
        const doc = ed.doc;
        const v = item.value;
        const spec = item.spec;
        const keys = spec?.keys || Object.keys(v);
        const has = key => keys.includes(key);
        const edit = change => app.edit(change);
        const own = v.typeParams || [];
        const card = el('div.tg-card-in');

        // The head: what it is, its name, its menu
        const head = el('div.tg-card-head', { dataset: { handle: item.id } }, el('span.tg-chip.tg-chip-' + (spec?.group || 'More').toLowerCase(), { text: spec?.title || v.kind, title: spec?.summary }));
        if (has('name')) {
            G.ui.put(head, field(v.name, x => edit(() => { v.name = x; }), {
                className: 'tg-input.tg-name', placeholder: 'Name', check: checkName, fk: `${v.id}:name`, title: `${spec?.title || ''} name`,
                onEnter: input => {
                    // A new card's first Enter goes on to its first row
                    if (hasRows && !rowsOf().length) addRow();
                    else input.blur();
                },
            }));
        } else {
            G.ui.put(head, el('span.tg-name.tg-name-fixed', { text: has('text') ? '' : spec?.summary || '' }));
        }
        G.ui.put(head, el('span.tg-badge', { dataset: { badge: item.id } }),
            button('⋯', e => moreMenu(app, item, e.clientX, e.clientY, { anchor: e.currentTarget }), 'More: what else it can have', 'tg-more'));
        G.ui.put(card, head);
        const tags = tagsOf(app, item);
        if (tags.length) G.ui.put(card, el('div.tg-tags', null, tags));
        const note = noteOf(app, item);
        if (note) G.ui.put(card, note);

        const rows = el('div.tg-rows');
        const isEnum = has('members') && !spec?.members;
        const hasRows = has('members') || has('entries');
        const listKey = has('entries') ? 'entries' : 'members';
        const rowsOf = () => (Array.isArray(v[listKey]) ? v[listKey] : []);
        const rowWord = has('entries') ? 'entry' : isEnum ? 'member' : (s.memberSpec('field')?.title || 'field').toLowerCase();

        function addRow() {
            let made;
            edit(d => {
                if (!Array.isArray(v[listKey])) set(v, listKey, [], keys);
                const list = v[listKey];
                if (has('entries')) made = d.newNamed(d.uniqueName(list, 'name'), 'int', '0');
                else if (isEnum) made = { id: d.newId(), name: d.uniqueName(list, 'Choice') };
                else made = d.newElement('field', true, d.uniqueName(list, 'value'));
                list.push(made);
            });
            if (made) board.focusSoon(`${made.id}:name`, true);
        }
        // Enter in a row goes to the next one, and makes it after the last
        const nextRow = m => () => {
            const list = rowsOf();
            const next = list.slice(list.indexOf(m) + 1).find(x => x.kind === undefined || x.kind === 'field' || x.kind === 'tweak');
            if (next) board.focusSoon(`${next.id}:name`, true);
            else addRow();
        };
        const remove = (list, m) => button('×', () => edit(() => { list.splice(list.indexOf(m), 1); }), 'Remove', 'tg-icon.tg-row-remove');
        const rowMenu = (m, list, mspec) => e => {
            e.preventDefault();
            e.stopPropagation();
            const order = mspec?.keys || [];
            const i = list.indexOf(m);
            menu(e.clientX, e.clientY, [
                order.includes('local') && { label: s.keyLabel('local'), summary: s.keyHelp('local'), checked: !!m.local, stay: true, run: on => edit(() => set(m, 'local', on ? true : undefined, order)) },
                order.includes('attributes') && { label: `${s.keyLabel('attributes')}…`, summary: (m.attributes || []).map(attributeText).join(' ') || s.keyHelp('attributes'), run: () => editAttributes(app, m, order, { x: e.clientX, y: e.clientY }) },
                { label: `${s.keyLabel('comment')}…`, summary: m.comment ? firstLine(m.comment) : s.keyHelp('comment'), run: () => editNote(app, m, order.length ? order : s.element(isEnum ? 'enumMember' : 'entry'), { x: e.clientX, y: e.clientY }) },
                { separator: true },
                i > 0 && { label: 'Move up', run: () => edit(() => { list.splice(i - 1, 0, list.splice(i, 1)[0]); }) },
                i < list.length - 1 && { label: 'Move down', run: () => edit(() => { list.splice(i + 1, 0, list.splice(i, 1)[0]); }) },
                { label: 'Duplicate', run: () => edit(d => { const copy = d.duplicate(m); if (copy.name) copy.name = d.uniqueName(list, copy.name); list.splice(i + 1, 0, copy); }) },
                { label: 'Delete', danger: true, run: () => edit(() => { list.splice(i, 1); }) },
            ], { title: m.name || mspec?.title });
        };
        const rowOf = (m, list, mspec, ...parts) => el('div.tg-row', { dataset: { id: m.id }, oncontextmenu: rowMenu(m, list, mspec), title: m.comment || undefined }, parts, remove(list, m));

        if (has('members') && spec?.members) {
            // A type's members: a row for each field, and a line for each member with code, which is a frame
            for (const m of rowsOf()) {
                const mspec = s.memberSpec(m.kind);
                const order = mspec?.keys || [];
                if (m.kind === 'field' || m.kind === 'tweak') {
                    const valueKey = m.kind === 'tweak' ? 'value' : 'default';
                    G.ui.put(rows, rowOf(m, rowsOf(), mspec,
                        m.kind === 'tweak' ? el('span.tg-row-tag', { text: mspec.title.toLowerCase(), title: mspec.summary }) : null,
                        m.local ? el('span.tg-row-tag', { text: 'local', title: `${s.keyLabel('local')}: ${s.keyHelp('local')}` }) : null,
                        field(m.name, x => edit(() => { m.name = x; }), { className: 'tg-input.tg-row-name', placeholder: 'name', check: checkName, fk: `${m.id}:name`, onEnter: nextRow(m) }),
                        typeChip(app, { type: m.type, at: m.id, own, fk: `${m.id}:type`, onPick: t => edit(() => { set(m, 'type', t, order); delete m[valueKey]; if (m.kind === 'tweak') set(m, 'value', defaultValue(doc, t), order); }) }),
                        G.values.control(app, { holder: m, key: valueKey, type: m.type, slot: { owner: m.id, key: valueKey }, order, fk: `${m.id}:value`, required: m.kind === 'tweak', label: s.keyLabel(valueKey), onEnter: nextRow(m) })));
                } else if (m.kind === 'property') {
                    const accessors = Array.isArray(m.accessors) ? m.accessors : [];
                    G.ui.put(rows, rowOf(m, rowsOf(), mspec,
                        el('span.tg-row-tag', { text: mspec.title.toLowerCase(), title: mspec.summary }),
                        field(m.name, x => edit(() => { m.name = x; }), { className: 'tg-input.tg-row-name', placeholder: 'Name', check: checkName, fk: `${m.id}:name` }),
                        typeChip(app, { type: m.type, at: m.id, own, fk: `${m.id}:type`, onPick: t => edit(() => set(m, 'type', t, order)) }),
                        el('span.tg-row-links', null,
                            accessors.map(a => button(a.word, () => app.reveal(a.id), `Go to its ${a.word} code`, 'tg-link')),
                            ['get', 'set'].filter(w => !accessors.some(a => a.word === w)).map(w => button('+ ' + w, () => {
                                let made;
                                edit(d => { if (!Array.isArray(m.accessors)) set(m, 'accessors', [], order); made = d.newAccessor(w); m.accessors.push(made); });
                                if (made) app.reveal(made.id);
                            }, `Give it code for ${w}`, 'tg-link.tg-link-add')))));
                } else if (mspec?.body) {
                    const title = m.kind === 'operator' ? `operator ${m.op ?? ''}` : m.name ?? mspec.title;
                    G.ui.put(rows, rowOf(m, rowsOf(), mspec,
                        el('span.tg-row-tag', { text: mspec.title.toLowerCase(), title: mspec.summary }),
                        button(title + ' →', () => app.reveal(m.id), 'Its code is a frame on the board: go to it', 'tg-link.tg-row-code')));
                } else {
                    G.ui.put(rows, rowOf(m, rowsOf(), mspec,
                        el('span.tg-row-tag', { text: (mspec?.title || 'text').toLowerCase(), title: mspec?.summary }),
                        button(firstLine(m.text || '') || '(empty)', e => promptText({ x: e.clientX, y: e.clientY, title: mspec?.title || 'Tide text', help: mspec?.summary, value: m.text, multiline: true, mono: true,
                            onDone: text => edit(() => { m.text = text; }) }), 'Members written as Tide text: click to edit', 'tg-link.tg-row-text.tg-mono')));
                }
            }
        } else if (isEnum) {
            for (const m of rowsOf()) {
                G.ui.put(rows, rowOf(m, rowsOf(), null,
                    field(m.name, x => edit(() => { m.name = x; }), { className: 'tg-input.tg-row-name', placeholder: 'Name', check: checkName, fk: `${m.id}:name`, onEnter: nextRow(m) }),
                    G.values.control(app, { holder: m, key: 'value', type: 'int', slot: { owner: m.id, key: 'value' }, order: s.element('enumMember'), fk: `${m.id}:value`, label: 'Its number, when it matters', onEnter: nextRow(m), none: 'auto' })));
            }
        } else if (has('entries')) {
            for (const m of rowsOf()) {
                G.ui.put(rows, rowOf(m, rowsOf(), null,
                    field(m.name, x => edit(() => { m.name = x; }), { className: 'tg-input.tg-row-name', placeholder: 'name', check: checkName, fk: `${m.id}:name`, onEnter: nextRow(m) }),
                    G.values.control(app, { holder: m, key: 'value', type: null, slot: { owner: m.id, key: 'value' }, order: s.element('entry'), fk: `${m.id}:value`, required: true, onEnter: nextRow(m) })));
            }
        }
        if (hasRows) {
            G.ui.put(rows, button('+ ' + rowWord, addRow, isEnum ? 'Another choice' : has('entries') ? 'Another setting' : s.memberSpec('field')?.summary, 'tg-add-row'));
        }
        if (has('type')) {
            // One value: a constant, an asset, another name for a type
            G.ui.put(rows, el('div.tg-row.tg-row-single', { dataset: { id: v.id } },
                typeChip(app, { type: v.type, at: v.id, own, fk: `${v.id}:type`, onPick: t => edit(d => {
                    set(v, 'type', t, keys);
                    // A value its new type's control can't show starts again
                    if (has('value') && G.values.classify(v.value).kind !== 'graph' && !sameControl(doc, v.value, t)) set(v, 'value', defaultValue(d, t), keys);
                }) }),
                has('value') ? [el('span.tg-row-equals', { text: '=' }), G.values.control(app, { holder: v, key: 'value', type: v.kind === 'asset' ? 'string' : v.type, slot: { owner: v.id, key: 'value' }, order: keys, required: true, fk: `${v.id}:value`, label: s.keyLabel('value') })] : null));
        }
        if (has('root') && v.root && typeof v.root === 'object') {
            G.ui.put(rows, prefabRows(app, v, v.root, null, 0));
        }
        if (has('text') && !hasRows) {
            const lines = String(v.text ?? '').split('\n').length;
            const text = field(v.text ?? '', x => edit(() => { v.text = x; }), { multiline: true, rows: Math.max(2, Math.min(24, lines)), className: 'tg-input.tg-card-text', mono: true, placeholder: '// Declarations written as Tide', fk: `${v.id}:text` });
            G.ui.put(rows, text);
        }
        G.ui.put(card, rows);
        return card;
    }

    // Whether a literal is what a type's control shows
    function sameControl(doc, g, type) {
        const what = G.values.classify(g);
        const tk = G.values.typeKind(type, doc);
        if (what.kind === 'none') return false;
        return (tk.kind === 'number' && what.kind === 'number') || (tk.kind === what.kind) || (tk.kind === 'other');
    }

    // What a value that has to be there starts as, by its type
    function defaultValue(doc, type) {
        const tk = G.values.typeKind(type, doc);
        switch (tk.kind) {
            case 'number': return doc.valueGraph(tk.whole ? 'int' : 'float', tk.whole ? '0' : '0.0');
            case 'bool': return doc.valueGraph('bool', 'false');
            case 'string': return doc.valueGraph('string', '');
            case 'char': return doc.valueGraph('char', "'a'");
            case 'vector': return doc.callGraph(tk.name, Array(tk.size).fill('0'));
            case 'color': return doc.callGraph('Color', ['1.0', '1.0', '1.0']);
            case 'enum': return tk.members.length ? doc.memberGraph(tk.name, tk.members[0]) : doc.valueGraph('default');
            default: return doc.valueGraph('default');
        }
    }

    // A prefab's entities: each with its components, which are values of their types, and its children
    function prefabRows(app, prefab, p, list, depth) {
        const s = app.editor.schema;
        const order = s.element('prefabNode');
        const edit = change => app.edit(change);
        const components = Array.isArray(p.components) ? p.components : [];
        const out = el('div.tg-prefab', { style: { marginLeft: depth ? '14px' : '0' } });
        G.ui.put(out, el('div.tg-row', { dataset: { id: p.id } },
            el('span.tg-row-tag', { text: list ? 'entity' : 'root' }),
            list ? field(p.name ?? '', x => edit(() => set(p, 'name', x || undefined, order)), { className: 'tg-input.tg-row-name', placeholder: 'name', check: x => (x === '' ? null : checkName(x)), fk: `${p.id}:name` })
                : el('span.tg-row-name.tg-muted', { text: prefab.name ?? '' }),
            list ? button('×', () => edit(() => { list.splice(list.indexOf(p), 1); }), 'Remove this entity', 'tg-icon.tg-row-remove') : null));
        const chips = el('div.tg-prefab-components');
        components.forEach((c, i) => {
            const root = G.Doc.nodeIn(c, c?.root);
            G.ui.put(chips, el('span.tg-prefab-component', null,
                button(root ? root.type || root.name || s.title(root.k) : 'component', () => app.openValue({ owner: p.id, key: 'components', index: i }, null, null, null, null), 'Its values: open them', 'tg-value-graph'),
                button('×', () => edit(() => { p.components.splice(i, 1); }), 'Remove this component', 'tg-icon')));
        });
        G.ui.put(chips, button('+ component', e => G.pickers.typePicker(app, {
            anchor: e.currentTarget, kinds: ['component'], details: ['component'], title: 'A component it has',
            onPick: type => edit(d => {
                if (!Array.isArray(p.components)) set(p, 'components', [], order);
                const g = d.valueGraph('literal');
                g.nodes[0].type = type;
                p.components.push(g);
            }),
        }), 'A component every one of these starts with', 'tg-add-row'));
        G.ui.put(chips, button('+ child', () => {
            let made;
            edit(d => {
                if (!Array.isArray(p.children)) set(p, 'children', [], order);
                made = d.newPrefabNode(d.uniqueName(p.children, 'child'));
                p.children.push(made);
            });
            if (made) app.board.focusSoon(`${made.id}:name`, true);
        }, 'An entity that goes with it', 'tg-add-row'));
        G.ui.put(out, chips);
        for (const child of Array.isArray(p.children) ? p.children : []) {
            if (child && typeof child === 'object') G.ui.put(out, prefabRows(app, prefab, child, p.children, depth + 1));
        }
        return out;
    }

    G.cards = { render, typeChip, moreMenu, tagsOf, noteOf, promptText, promptRows, editNote, editAttributes, checkName, defaultValue, attributeText };
})(globalThis.TideGraph = globalThis.TideGraph || {});
