// A card: a declaration of data on the board, edited where it's drawn. Its kind in plain words, its name (renamed by
// a double click), a "+" on its title for another row, a row per field (name, type, starting value), and the one
// menu (menus.js) for what else its kind can have. What a card shows comes from its kind's keys in the schema:
// members, entries, a type and a value, a prefab's entities, text. Its options are in Details (panels.js).
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

    // The tags under a title: its options in words and its attributes, to read (Details changes them)
    function tagsOf(app, item) {
        const s = app.editor.schema;
        const v = item.value;
        const out = [];
        for (const m of v.modifiers || []) out.push(el('span.tg-tag', { text: s.modifierLabel(m), title: s.modifierHelp(m) }));
        if (v.local === true) out.push(el('span.tg-tag', { text: s.keyLabel('local'), title: s.keyHelp('local') }));
        if (v.extern === true) out.push(el('span.tg-tag', { text: s.keyLabel('extern') }));
        if (v.typeParams?.length) out.push(el('span.tg-tag', { text: `${s.keyLabel('typeParams')} ${v.typeParams.join(', ')}`, title: s.keyHelp('typeParams') }));
        if (v.fails) out.push(el('span.tg-tag', { text: `${s.keyLabel('fails')} ${v.fails}`, title: s.keyHelp('fails') }));
        if (v.backing) out.push(el('span.tg-tag', { text: `${s.keyLabel('backing')} ${v.backing}` }));
        for (const a of v.attributes || []) out.push(el('span.tg-tag.tg-tag-attribute', { text: attributeText(a), title: s.keyHelp('attributes') }));
        return out;
    }

    function noteOf(app, item) {
        const v = item.value;
        if (!v.comment) return null;
        return el('div.tg-comment', { text: v.comment, title: v.comment });
    }

    // What a card's rows are: { key, kind: 'member' | 'enum' | 'entry' }, or null for a card with none
    function rowsKind(item) {
        const keys = item.spec?.keys || [];
        if (keys.includes('entries')) return { key: 'entries', kind: 'entry' };
        if (keys.includes('members')) return { key: 'members', kind: item.spec?.members ? 'member' : 'enum' };
        return null;
    }

    // A new row of a card after `after` (at the end for null): a field, a choice, a setting; its name to type
    function addRowTo(app, item, after) {
        const v = item.value;
        const rk = rowsKind(item);
        if (!rk) return null;
        let made;
        app.edit(d => {
            if (!Array.isArray(v[rk.key])) set(v, rk.key, [], item.spec?.keys);
            const list = v[rk.key];
            if (rk.kind === 'entry') made = d.newNamed(d.uniqueName(list, 'name'), 'int', '0');
            else if (rk.kind === 'enum') made = { id: d.newId(), name: d.uniqueName(list, 'Choice') };
            else made = d.newElement('field', true, d.uniqueName(list, 'value'));
            const at = after ? list.indexOf(after) : -1;
            list.splice(at < 0 ? list.length : at + 1, 0, made);
        });
        if (made) {
            app.editor.selectRow(item.id, made.id);
            app.board.focusSoon(`${made.id}:name`, true);
        }
        return made;
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
        const rk = rowsKind(item);
        const listKey = rk?.key;
        const isEnum = rk?.kind === 'enum';
        const rowsOf = () => (listKey && Array.isArray(v[listKey]) ? v[listKey] : []);
        const addRow = after => addRowTo(app, item, after);
        // What the "+" on its title adds: a field, a choice, a setting
        const plus = rk ? (rk.kind === 'member' ? G.menus.addMember(s.memberSpec('field')) : G.menus.addRow(s, v.kind, false)) : null;

        // The head: what it is, its name, a + for another row, its menu. All of it but its buttons moves the card.
        const head = el('div.tg-card-head', { dataset: { handle: item.id } }, el('span.tg-chip.tg-chip-' + (spec?.group || 'More').toLowerCase(), { text: spec?.title || v.kind, title: spec?.summary }));
        if (has('name')) {
            G.ui.put(head, G.ui.nameText(v.name, x => app.renameDecl(item, x), {
                className: 'tg-name', placeholder: 'Name', check: checkName, fk: `${v.id}:name`,
                // A new card's first Enter goes on to its first row
                onEnter: () => { if (rk && !rowsOf().length) addRow(null); },
            }));
        } else {
            G.ui.put(head, el('span.tg-name.tg-name-fixed', { text: has('text') ? '' : spec?.summary || '' }));
        }
        G.ui.put(head, el('span.tg-badge', { dataset: { badge: item.id } }),
            plus ? button('+', () => addRow(rowsOf().at(-1) || null), plus, 'tg-icon.tg-card-add') : null,
            button('⋯', e => G.menus.contextMenu(app, { kind: 'card', item }, e.clientX, e.clientY, { anchor: e.currentTarget }), 'What can be done with it (or right-click it)', 'tg-more'));
        G.ui.put(card, head);
        const tags = tagsOf(app, item);
        if (tags.length) G.ui.put(card, el('div.tg-tags', null, tags));
        const note = noteOf(app, item);
        if (note) G.ui.put(card, note);

        const rows = el('div.tg-rows');
        // Enter in a row goes to the next one, and makes it after the last
        const nextRow = m => () => {
            const list = rowsOf();
            const next = list.slice(list.indexOf(m) + 1).find(x => x.kind === undefined || x.kind === 'field' || x.kind === 'tweak');
            if (next) { ed.selectRow(item.id, next.id); board.focusSoon(`${next.id}:name`, true); }
            else addRow(m);
        };
        const rowOf = (m, ...parts) => el('div.tg-row', { dataset: { id: m.id }, title: m.comment || undefined }, parts);
        const name = (m, placeholder, extra = {}) => G.ui.nameText(m.name, x => edit(() => { m.name = x; }), { className: 'tg-row-name', placeholder, check: checkName, fk: `${m.id}:name`, ...extra });

        if (rk?.kind === 'member') {
            // A type's members: a row for each field, and a line for each member with code, which is a frame
            for (const m of rowsOf()) {
                const mspec = s.memberSpec(m.kind);
                const order = mspec?.keys || [];
                if (m.kind === 'field' || m.kind === 'tweak') {
                    const valueKey = m.kind === 'tweak' ? 'value' : 'default';
                    G.ui.put(rows, rowOf(m,
                        m.kind === 'tweak' ? el('span.tg-row-tag', { text: mspec.title.toLowerCase(), title: mspec.summary }) : null,
                        m.local ? el('span.tg-row-tag', { text: 'local', title: `${s.keyLabel('local')}: ${s.keyHelp('local')}` }) : null,
                        name(m, 'name', { onEnter: nextRow(m) }),
                        typeChip(app, { type: m.type, at: m.id, own, fk: `${m.id}:type`, onPick: t => edit(d => { set(m, 'type', t, order); delete m[valueKey]; if (m.kind === 'tweak') set(m, 'value', defaultValue(app, d, t), order); }) }),
                        G.values.control(app, { holder: m, key: valueKey, type: m.type, slot: { owner: m.id, key: valueKey }, order, fk: `${m.id}:value`, required: m.kind === 'tweak', label: s.keyLabel(valueKey), onEnter: nextRow(m) })));
                } else if (m.kind === 'property') {
                    const accessors = Array.isArray(m.accessors) ? m.accessors : [];
                    G.ui.put(rows, rowOf(m,
                        el('span.tg-row-tag', { text: mspec.title.toLowerCase(), title: mspec.summary }),
                        name(m, 'Name'),
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
                    G.ui.put(rows, rowOf(m,
                        el('span.tg-row-tag', { text: mspec.title.toLowerCase(), title: mspec.summary }),
                        button(title + ' →', () => app.reveal(m.id), 'Its code is a frame on the board: go to it', 'tg-link.tg-row-code')));
                } else {
                    G.ui.put(rows, rowOf(m,
                        el('span.tg-row-tag', { text: (mspec?.title || 'text').toLowerCase(), title: mspec?.summary }),
                        button(firstLine(m.text || '') || '(empty)', e => promptText({ x: e.clientX, y: e.clientY, title: mspec?.title || 'Tide text', help: mspec?.summary, value: m.text, multiline: true, mono: true,
                            onDone: text => edit(() => { m.text = text; }) }), 'Members written as Tide text: click to edit', 'tg-link.tg-row-text.tg-mono')));
                }
            }
        } else if (isEnum) {
            for (const m of rowsOf()) {
                G.ui.put(rows, rowOf(m,
                    name(m, 'Name', { onEnter: nextRow(m) }),
                    G.values.control(app, { holder: m, key: 'value', type: 'int', slot: { owner: m.id, key: 'value' }, order: s.element('enumMember'), fk: `${m.id}:value`, label: 'Its number, when it matters', onEnter: nextRow(m), none: 'auto' })));
            }
        } else if (rk?.kind === 'entry') {
            for (const m of rowsOf()) {
                G.ui.put(rows, rowOf(m,
                    name(m, 'name', { onEnter: nextRow(m) }),
                    G.values.control(app, { holder: m, key: 'value', type: null, slot: { owner: m.id, key: 'value' }, order: s.element('entry'), fk: `${m.id}:value`, required: true, onEnter: nextRow(m) })));
            }
        }
        // An empty card says what goes in it, and how
        if (rk && !rowsOf().length) G.ui.put(rows, el('div.tg-rows-empty', { text: `Nothing yet: + on its title (${plus.replace(/…$/, '').toLowerCase()})` }));
        if (has('type')) {
            // One value: a constant, an asset, another name for a type
            G.ui.put(rows, el('div.tg-row.tg-row-single', { dataset: { id: v.id } },
                typeChip(app, { type: v.type, at: v.id, own, fk: `${v.id}:type`, onPick: t => edit(d => {
                    set(v, 'type', t, keys);
                    // A value its new type's control can't show starts again
                    if (has('value') && G.values.classify(v.value).kind !== 'graph' && !sameControl(app, doc, v.value, t)) set(v, 'value', defaultValue(app, d, t), keys);
                }) }),
                has('value') ? [el('span.tg-row-equals', { text: '=' }), G.values.control(app, { holder: v, key: 'value', type: v.kind === 'asset' ? 'string' : v.type, slot: { owner: v.id, key: 'value' }, order: keys, required: true, fk: `${v.id}:value`, label: s.keyLabel('value') })] : null));
        }
        if (has('root') && v.root && typeof v.root === 'object') {
            G.ui.put(rows, prefabRows(app, v, v.root, null, 0));
        }
        if (has('text') && !rk) {
            const lines = String(v.text ?? '').split('\n').length;
            const text = field(v.text ?? '', x => edit(() => { v.text = x; }), { multiline: true, rows: Math.max(2, Math.min(24, lines)), className: 'tg-input.tg-card-text', mono: true, placeholder: '// Declarations written as Tide', fk: `${v.id}:text` });
            G.ui.put(rows, text);
        }
        G.ui.put(card, rows);
        return card;
    }

    // Whether a literal is what a type's control shows
    function sameControl(app, doc, g, type) {
        const what = G.values.classify(g);
        const tk = G.values.typeKind(type, doc, app.lang);
        if (what.kind === 'none') return false;
        return (tk.kind === 'number' && what.kind === 'number') || (tk.kind === what.kind) || (tk.kind === 'other');
    }

    // What a value that has to be there starts as, by its type
    function defaultValue(app, doc, type) {
        const tk = G.values.typeKind(type, doc, app.lang);
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
            list ? G.ui.nameText(p.name ?? '', x => edit(() => set(p, 'name', x || undefined, order)), { className: 'tg-row-name', placeholder: 'name', check: x => (x === '' ? null : checkName(x)), fk: `${p.id}:name` })
                : el('span.tg-row-name.tg-muted', { text: prefab.name ?? '' }),
            list ? button('×', () => edit(() => { list.splice(list.indexOf(p), 1); }), 'Delete this entity', 'tg-icon.tg-row-remove') : null));
        const chips = el('div.tg-prefab-components');
        components.forEach((c, i) => {
            const root = G.Doc.nodeIn(c, c?.root);
            G.ui.put(chips, el('span.tg-prefab-component', null,
                button(root ? root.type || root.name || s.title(root.k) : 'component', () => app.openValue({ owner: p.id, key: 'components', index: i }, null, null, null, null), 'Its values: open them', 'tg-value-graph'),
                button('×', () => edit(() => { p.components.splice(i, 1); }), 'Delete this component', 'tg-icon')));
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

    G.cards = { render, typeChip, tagsOf, noteOf, promptText, promptRows, editNote, editAttributes, checkName, checkPath, defaultValue, attributeText, addRowTo, rowsKind, firstLine };
})(globalThis.TideGraph = globalThis.TideGraph || {});
