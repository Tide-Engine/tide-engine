// What's picked from a list with a search box: a kind of declaration to add (the Add menu), a type (the type
// picker: nothing is typed free, a type is always one that was picked), what a frame's code needs, a node to add
// inside a frame, and a declaration to jump to. Each offers only what's valid where it was opened: the schema's
// rules, and what tidels offers at that place of the graph's Tide.
(function (G) {
    'use strict';
    const { picker } = G.ui;
    const { Doc } = G;
    const KIND = G.lang.KIND;

    const TYPE_KINDS = new Set([KIND.Struct, KIND.Class, KIND.Enum, KIND.Interface, KIND.TypeParameter, KIND.Event]);

    function lowerFirst(s) {
        return s ? s[0].toLowerCase() + s.slice(1) : s;
    }

    // A name for a value of a type: `wobble` for Wobble, `values` for List<int>
    function nameFor(type) {
        const plain = String(type || '').split('.').pop();
        if (/^[A-Z][A-Za-z0-9_]*$/.test(plain)) return lowerFirst(plain);
        if (/^(List|Set)</.test(plain)) return 'items';
        if (/^Map</.test(plain)) return 'map';
        if (/^Grid/.test(plain)) return 'cells';
        return 'value';
    }

    // -----------------------------------------------------------------------------------------------------------
    // The Add menu: every kind of declaration by the schema's groups, with what each is for

    function addEntries(schema) {
        const out = [];
        for (const group of schema.groups) {
            for (const d of schema.declsOf(group.name)) {
                out.push({ title: d.title, summary: d.summary, group: group.title, groupSummary: group.summary, kind: d.kind, name: d.kind, keywords: d.kind });
            }
        }
        return out;
    }

    function addMenu(app, x, y, { namespace = null, at = null, anchor = null } = {}) {
        const schema = app.editor?.schema;
        if (!schema || app.editor.error) return null;
        return picker({
            x, y, anchor, className: 'tg-add-menu', placeholder: 'What do you want to make?',
            title: namespace ? `Add to ${namespace.name}` : null,
            entries: addEntries(schema),
            onPick: entry => app.addDecl(entry.kind, { namespace, at }),
        });
    }

    // -----------------------------------------------------------------------------------------------------------
    // The type picker

    // Where a type is written in the graph's Tide: in its element's stretch, at the type's own text
    function typeOffset(lang, id, current) {
        const s = lang.span(id);
        if (!s || lang.text === null) return -1;
        if (current) {
            const text = lang.text.slice(s[0], s[1]);
            const found = new RegExp('(^|[^A-Za-z0-9_.])' + current.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')).exec(text);
            if (found) return s[0] + found.index + found[1].length;
        }
        return s[0];
    }

    // Picks a type. `at`: the element whose type it is (tidels is asked what's valid where it's written, or `ask()`
    // asks it elsewhere); `current` the type it has; `own` the type parameters of what it's in; `allow.none` offers
    // no type at all under that label (a local's `var`); `allow.void` offers void; `kinds` narrows the file's own to
    // those kinds of declaration and `details` tidels's to those it says are that (an event for a handler).
    // `onPick(type)` gets the picked type, undefined for none.
    function typePicker(app, { x = 0, y = 0, anchor = null, at = 0, current = null, own = [], allow = {}, kinds = null, details = null, ask = null, title = null, onPick, keep = false, typed = '', plain = false }) {
        const ed = app.editor;
        const schema = ed.schema;
        const doc = ed.doc;
        const local = doc.declared(kinds || ['struct', 'enum', 'alias', 'component', 'singleton', 'event', 'input', 'scene']);
        const build = offered => {
            // `offered`: the names tidels offers there (label -> item), or null before it answered
            const out = [];
            const seen = new Set();
            const add = (name, entry) => {
                if (seen.has(name)) return;
                seen.add(name);
                out.push({ title: name, key: name, type: name, ...entry });
            };
            if (allow.none) out.push({ title: allow.none, key: '\0none', none: true, summary: allow.noneSummary, group: 'Common' });
            if (!kinds) {
                for (const t of schema.typePicker.common || []) {
                    if (!offered || offered.has(t)) add(t, { group: 'Common', color: G.ui.typeColor(t), help: docOf(offered?.get(t)) });
                }
            }
            for (const t of own) add(t, { group: 'Its own', detail: 'type parameter', summary: 'Whatever type it is used with' });
            for (const d of local) {
                // What the file declares, where tidels says it goes (or wherever, before it answers)
                if (offered && !offered.has(d.name)) continue;
                add(d.name, { group: 'In this file', detail: schema.declSpec(d.kind)?.title, color: G.ui.typeColor(d.name) });
            }
            if (offered) {
                for (const [name, item] of offered) {
                    if (item.detail === 'built-in type' || schema.types.includes(name)) continue;
                    if (details && !details.includes(item.detail)) continue;
                    add(name, { group: 'From the game and the engine', detail: item.detail, color: G.ui.typeColor(name), help: docOf(item) });
                }
            }
            if (!kinds && !plain) {
                for (const b of schema.typePicker.builders || []) {
                    out.push({ title: b.title, key: '\0' + b.type, builder: b, group: 'Made of others', summary: b.type.replace(/\{(\d)\}/g, (_, i) => b.slots[i].replace(/ \(.*\)$/, '')), stay: true });
                }
            }
            if (!kinds) {
                const rest = offered ? [...offered.keys()].filter(n => offered.get(n).detail === 'built-in type' || schema.types.includes(n)) : schema.types;
                for (const t of rest) {
                    if (t.includes('<') || (t === 'void' && !allow.void)) continue;
                    // A type that takes others is made with its builder, or said whole below
                    if (schema.typePicker.builders?.some(b => b.type.startsWith(t + '<'))) continue;
                    add(t, { group: 'More', color: G.ui.typeColor(t), help: docOf(offered?.get(t)) });
                }
                if (allow.void) add('void', { group: 'More', summary: 'Nothing' });
            }
            return out;
        };
        const lang = app.lang;
        // Asked of the document as it is now: a check that's waiting runs first
        const asked = (ask || at) && lang.status !== 'unavailable'
            ? app.checked().then(() => (ask ? ask() : lang.completion(typeOffset(lang, at, current)))).then(items => {
                const offered = new Map();
                for (const item of items) {
                    if (TYPE_KINDS.has(item.kind) && typeof item.label === 'string') offered.set(item.label.replace(/[<(].*$/, ''), item);
                }
                // Nothing offered is no answer (a place tidels can't complete at): the lists stay as they are
                if (offered.size && p) p.refill(build(offered));
                return [];
            }) : null;
        let p = null;
        p = picker({
            x, y, anchor, keep, typed, className: 'tg-type-picker', title,
            placeholder: current ? `${current}  ·  search for a type` : 'Search for a type…',
            entries: build(null), more: asked, status: 'Asking what fits here…',
            empty: text => `No type called "${text}". Declare it first: a struct or an enum from + Add.`,
            onPick: entry => {
                if (entry.none) { onPick(undefined); return; }
                if (entry.builder) {
                    const r = p.el.getBoundingClientRect();
                    fillBuilder(app, entry.builder, [], { x: r.left, y: r.top, at, own, onPick: type => { p.close(); onPick(type); } });
                    return;
                }
                onPick(entry.type);
            },
        });
        return p;
    }

    function docOf(item) {
        const d = item?.documentation;
        return typeof d === 'string' ? d : d?.value || undefined;
    }

    // A builder's slots, one picker after the other: "List of…" asks what it holds, with the same picker
    function fillBuilder(app, builder, got, { x, y, at, own, onPick }) {
        const i = got.length;
        if (i >= builder.slots.length) {
            onPick(builder.type.replace(/\{(\d)\}/g, (_, n) => got[Number(n)]));
            return;
        }
        const next = value => fillBuilder(app, builder, [...got, value], { x, y, at, own, onPick });
        const title = `${builder.title.replace(/…/g, '')}: ${builder.slots[i]}`;
        // A slot between [ ] is a count
        const count = new RegExp('\\[\\{' + i + '\\}\\]').test(builder.type);
        if (count) {
            picker({
                x, y, keep: true, title, placeholder: 'A number…', entries: [2, 3, 4, 8, 16, 32, 64].map(n => ({ title: String(n), n: String(n) })),
                free: text => (/^\d+$/.test(text) && Number(text) > 0 ? { title: text, n: text, detail: 'this many' } : null),
                empty: 'A whole number, like 8',
                onPick: entry => next(entry.n),
            });
            return;
        }
        typePicker(app, { x, y, at: 0, own, title, keep: true, onPick: type => next(type) });
    }

    // -----------------------------------------------------------------------------------------------------------
    // What a frame's code needs: a parameter, picked by what it takes

    function needsPicker(app, item, { x = 0, y = 0, anchor = null } = {}) {
        const ed = app.editor;
        const schema = ed.schema;
        const doc = ed.doc;
        const routine = item.value;
        const list = Array.isArray(routine.params) ? routine.params : [];
        const taken = new Set(list.map(p => p.name));
        // A routine with a result is called with values of any type; the rest run for entities, and take what
        // those have
        const called = !!(item.spec?.keys.includes('returnType') || item.value.kind === 'sample' || item.value.kind === 'audio');
        const add = (type, mode, name) => {
            let made;
            app.edit(d => {
                if (!Array.isArray(routine.params)) G.json.set(routine, 'params', [], item.spec?.keys);
                let own = name ?? nameFor(type);
                if (schema.modeIsFilter(mode)) own = undefined;
                else for (let i = 2; taken.has(own); i++) own = (name ?? nameFor(type)) + i;
                made = item.value.kind === 'sample' ? { id: d.newId(), type, name: own } : d.newParam(type, own, mode);
                routine.params.push(made);
            });
            if (made) app.board.focusSoon(`${made.id}:name`);
        };
        const entries = [];
        const fromFile = called ? [] : doc.declared(['component', 'singleton']);
            for (const d of fromFile) {
            entries.push({ title: d.name, key: d.name, type: d.name, group: 'In this file', detail: schema.declSpec(d.kind)?.title, color: G.ui.typeColor(d.name) });
        }
        if (called) {
            for (const t of schema.typePicker.common || []) entries.push({ title: t, key: t, type: t, group: 'Common', color: G.ui.typeColor(t) });
            for (const d of doc.declared(['struct', 'enum', 'alias'])) entries.push({ title: d.name, key: d.name, type: d.name, group: 'In this file', detail: schema.declSpec(d.kind)?.title, color: G.ui.typeColor(d.name) });
        } else {
            for (const mode of schema.paramModes.filter(m => schema.modeIsFilter(m))) {
                entries.push({ title: `${schema.modeLabel(mode)}…`, key: '\0' + mode, mode, group: 'Filters', summary: schema.modeHelp(mode), stay: true });
            }
        }
        entries.push({ title: 'Another type…', key: '\0more', other: true, group: 'More', summary: 'Anything else it takes: pick its type', stay: true });
        const lang = app.lang;
        // What the rest of the game has that it can take, once tidels says: after the file's own, before the rest
        const asked = lang.status !== 'unavailable'
            ? app.checked().then(() => lang.completionInParams(routine)).then(items => {
                const have = new Set(entries.map(e => e.key));
                const game = items.filter(i => TYPE_KINDS.has(i.kind) && typeof i.label === 'string' && !called && !have.has(i.label))
                    .map(i => ({ title: i.label, key: i.label, type: i.label, group: 'In this game', detail: i.detail && i.detail !== 'built-in type' ? i.detail : undefined, help: docOf(i), color: G.ui.typeColor(i.label) }));
                const own = entries.filter(e => e.group === 'In this file' || e.group === 'Common');
                if (game.length) p.refill([...own, ...game, ...entries.filter(e => !own.includes(e))]);
                return [];
            })
            : null;
        const p = picker({
            x, y, anchor, className: 'tg-needs-picker', placeholder: called ? 'What does it take?' : 'What does it need?',
            title: called ? `What ${doc.titleOf(item)} takes` : `What ${doc.titleOf(item)} needs`,
            entries, more: asked, status: 'Asking what the game has…',
            empty: text => `Nothing called "${text}" to take.`,
            onPick: entry => {
                const r = p.el.getBoundingClientRect();
                if (entry.other) {
                    typePicker(app, { x: r.left, y: r.top, onPick: type => add(type, 'read') });
                } else if (entry.mode) {
                    typePicker(app, { x: r.left, y: r.top, kinds: ['component'], details: ['component'], ask: () => lang.completionInParams(routine),
                        title: `${schema.modeLabel(entry.mode)}…`, onPick: type => add(type, entry.mode) });
                } else {
                    add(entry.type, 'read');
                }
            },
        });
        return p;
    }

    // -----------------------------------------------------------------------------------------------------------
    // The node search of a frame: what can go where it was opened

    function placeholderOf(context) {
        if (context?.member) return 'A member of its value…';
        const from = context?.from;
        if (!from) return 'What should happen here?';
        if (from.exec) return from.input ? 'What happens before it?' : 'What happens next?';
        return from.input ? 'A value for it…' : 'What to do with this value?';
    }

    // What the editor knows without tidels: the schema's kinds of node, the frame's locals and pins
    function catalog(app, frame, context) {
        const s = app.editor.schema;
        const doc = app.editor.doc;
        const g = frame.graph;
        const out = [];
        const from = context?.from;
        const wantExec = from?.exec;
        const wantValue = from && !from.exec && from.input;
        const takesValue = from && !from.exec && !from.input;
        if (context?.member) return out;
        if (!wantExec && !takesValue) {
            for (const pin of frame.pins) {
                out.push({ title: pin.name, key: 'pin:' + pin.name, detail: pin.type, group: 'What it takes', color: G.ui.typeColor(pin.type), node: { k: 'name', fields: { name: pin.name } }, hidden: true });
            }
            for (const d of doc.locals(g)) {
                out.push({ title: s.declaredLocal(d), key: 'local:' + d.id, detail: 'local', group: 'Locals', color: s.categoryByName.get('locals')?.color, node: { k: 'local', fields: { ref: d.id } }, hidden: true });
            }
        }
        for (const spec of s.nodes) {
            if (wantExec && !spec.exec) continue;
            if (wantValue && !spec.value) continue;
            if (takesValue && !(spec.inputs?.length || spec.lists?.length || spec.items?.length || spec.map)) continue;
            if (spec.k === 'local' || spec.k === 'outVar' || spec.k === 'out') continue; // Wires from a local's pin, and a call's own
            const category = s.categoryByName.get(spec.category);
            out.push({ title: spec.title, key: 'k:' + spec.k, summary: spec.help, group: category?.title || spec.category, color: category?.color, node: { k: spec.k, fields: {} }, keywords: spec.k });
        }
        if (!wantExec && frame.item?.member && !takesValue) {
            const spec = s.node('this');
            if (spec) out.push({ title: spec.title, key: 'k:this:own', summary: spec.help, group: 'What it takes', node: { k: 'this', fields: {} } });
        }
        return out;
    }

    // The members of a type the file declares, as entries, before (or without) tidels
    function localMembers(app, type) {
        const s = app.editor.schema;
        return app.editor.doc.membersOf(type).map(m => (m.method
            ? { title: `${m.name}(${m.method.join(', ')})`, key: 'm:' + m.name, detail: m.type, group: `Of ${type}`, color: s.color('method'), node: { k: 'method', fields: { name: m.name, args: m.method.map(() => null) } }, first: true }
            : { title: m.name, key: 'm:' + m.name, detail: m.type, group: `Of ${type}`, color: G.ui.typeColor(m.type) || s.color('member'), node: { k: 'member', fields: { member: m.name } }, first: true }));
    }

    function nodeSearch(app, frame, clientX, clientY, context) {
        const ed = app.editor;
        const g = frame.graph;
        if (!g) return null;
        const lang = app.lang;
        const from = context?.from;
        const at = frame.toLocal(clientX, clientY);
        // The value whose members to offer: what a wire was dragged from, or a member node's object
        const valueOut = from && !from.exec && !from.input;
        const memberType = context?.member ? lang.typeOf(context.member.object) || frame.typeOfSource(context.member.object) : valueOut ? frame.typeOfPort(from) : null;
        const memberNode = context?.member ? context.member.object : valueOut ? frame.nodeForMembers(from) : 0;
        const entries = [...(memberType ? localMembers(app, memberType) : []), ...catalog(app, frame, context)];
        let asked = null;
        if (lang.status !== 'unavailable') {
            if (memberNode) {
                asked = app.checked().then(() => lang.members(memberNode)).then(({ items, type }) => items.map(i => G.lang.entryOf(i, { member: true })).filter(Boolean)
                    .map(e => ({ ...e, key: 'm:' + (e.node.fields.member ?? e.node.fields.name), group: `Of ${type || memberType || 'the value'}`, first: true })));
            } else if (!valueOut && !context?.member) {
                const flow = !!from?.exec;
                const place = () => lang.placeFor(from?.exec ? (from.input ? { before: from.node } : { after: from.node > 0 ? from.node : 0 }) : from?.input && from.node > 0 ? { valueFor: frame.statementOf(from.node) } : null, frame.slot.owner);
                asked = app.checked().then(() => lang.completion(place())).then(items => {
                    const have = new Set(entries.map(e => e.title));
                    return items.map(i => G.lang.entryOf(i, { flow })).filter(e => e && !have.has(e.title) && (!flow || e.node.k === 'call'))
                        .map(e => ({ ...e, key: 'c:' + e.title, summary: undefined }));
                });
            }
        }
        const title = context?.member || (valueOut && memberType) ? `Of ${memberType || 'its value'}` : null;
        return picker({
            x: clientX, y: clientY, className: 'tg-node-search', placeholder: placeholderOf(context), title,
            entries, more: asked,
            empty: text => `Nothing called "${text}" here.`,
            onPick: entry => {
                if (context?.member) app.retarget(frame, context.member, entry);
                else app.addFromSearch(frame, entry, at, context);
            },
            onClose: () => app.board.el.focus({ preventScroll: true }),
        });
    }

    // -----------------------------------------------------------------------------------------------------------
    // Jump to: every declaration of the file, by group

    function contents(app) {
        const ed = app.editor;
        const schema = ed.schema;
        const out = [];
        const items = ed.doc.items().filter(i => !i.region);
        for (const group of schema.groups) {
            for (const item of items) {
                const kind = item.accessor ? 'accessor' : item.value.kind;
                const spec = item.accessor ? null : item.spec;
                const top = item.member || item.accessor ? schema.declSpec(item.type?.kind) : spec;
                // A member's frame goes with its type's kind of thing, as code
                const in_ = item.frame ? (top?.group === 'More' && !item.member && !item.accessor ? 'More' : 'Behaviour') : top?.group;
                if (in_ !== group.name) continue;
                out.push({
                    title: ed.doc.titleOf(item), key: String(item.id), id: item.id, detail: item.accessor ? schema.memberSpec('property')?.title || 'Property' : spec?.title || kind,
                    group: group.title, groupSummary: group.summary, problems: app.problemsIn(item.id),
                    tag: app.problemsIn(item.id) ? '!' : undefined,
                });
            }
        }
        return out;
    }

    function jumpTo(app, { x, y, anchor } = {}) {
        if (!app.editor || app.editor.error) return null;
        return picker({
            x: x ?? innerWidth / 2 - 180, y: y ?? 60, anchor, className: 'tg-jump', placeholder: 'Jump to…',
            entries: contents(app), empty: text => `Nothing called "${text}" in this file.`,
            onPick: entry => app.reveal(entry.id),
        });
    }

    G.pickers = { addMenu, addEntries, typePicker, needsPicker, nodeSearch, jumpTo, contents, nameFor, lowerFirst };
})(globalThis.TideGraph = globalThis.TideGraph || {});
