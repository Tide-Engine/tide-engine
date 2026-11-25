// What's picked from a list with a search box: a kind of declaration to add (the Add menu), a type (the type
// picker: nothing is typed free, a type is always one that was picked), what a frame's code needs, what goes in an
// input of a node (the value picker), a node to add inside a frame, and a declaration to jump to. Each offers only what's valid where it was opened: the schema's
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

    function addEntries(schema, only = null) {
        const out = [];
        for (const group of schema.groups) {
            if (only && group.name !== only) continue;
            for (const d of schema.declsOf(group.name)) {
                out.push({ title: d.title, summary: d.summary, group: group.title, groupSummary: group.summary, kind: d.kind, name: d.kind, keywords: d.kind });
            }
        }
        return out;
    }

    function addMenu(app, x, y, { namespace = null, at = null, anchor = null, group = null } = {}) {
        const schema = app.editor?.schema;
        if (!schema || app.editor.error) return null;
        const g = group ? schema.groups.find(x2 => x2.name === group) : null;
        return picker({
            x, y, anchor, className: 'tg-add-menu', placeholder: 'What do you want to make?',
            title: namespace ? `Add to ${namespace.name}` : g ? `Add to ${g.title}: ${g.summary}` : null,
            entries: addEntries(schema, group),
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
        const lang = app.lang;
        const build = offered => {
            // `offered`: the names tidels offers there (label -> item), or null before it answered
            const out = [];
            const seen = new Set();
            const add = (name, entry) => {
                if (seen.has(name)) return;
                seen.add(name);
                out.push({ title: name, key: name, type: name, ...entry });
            };
            const group = name => schema.typeGroup(name);
            if (allow.none) out.push({ title: allow.none, key: '\0none', none: true, summary: allow.noneSummary, group: group('common') });
            if (!kinds) {
                for (const t of schema.typePicker.common || []) {
                    if (!offered || offered.has(t)) add(t, { group: group('common'), color: G.ui.typeColor(t), help: docOf(offered?.get(t)) });
                }
            }
            for (const t of own) add(t, { group: group('own'), detail: 'type parameter', summary: 'Whatever type it is used with' });
            for (const d of local) {
                // What the file declares, where tidels says it goes (or wherever, before it answers)
                if (offered && !offered.has(d.name)) continue;
                add(d.name, { group: group('file'), detail: schema.declSpec(d.kind)?.title, color: G.ui.typeColor(d.name) });
            }
            // What the game itself declares elsewhere (tide/graphNames); what the engine gives goes with the rest
            const engine = [];
            if (offered) {
                const known = Object.keys(lang.names.game).length + Object.keys(lang.names.packages).length > 0;
                for (const [name, item] of offered) {
                    if (item.detail === 'built-in type' || schema.types.includes(name)) continue;
                    if (details && !details.includes(item.detail)) continue;
                    if (known && !lang.declares(name)) { engine.push([name, item]); continue; }
                    add(name, { group: group('game'), detail: item.detail, color: G.ui.typeColor(name), help: docOf(item) });
                }
            }
            if (!kinds && !plain) {
                for (const b of schema.typePicker.builders || []) {
                    out.push({ title: b.title, key: '\0' + b.type, builder: b, group: group('builders'), summary: b.type.replace(/\{(\d)\}/g, (_, i) => b.slots[i].replace(/ \(.*\)$/, '')), stay: true });
                }
            }
            if (!kinds) {
                const rest = offered ? [...offered.keys()].filter(n => offered.get(n).detail === 'built-in type' || schema.types.includes(n)) : schema.types;
                for (const t of rest) {
                    if (t.includes('<') || (t === 'void' && !allow.void)) continue;
                    // A type that takes others is made with its builder, or said whole below
                    if (schema.typePicker.builders?.some(b => b.type.startsWith(t + '<'))) continue;
                    add(t, { group: group('rest'), color: G.ui.typeColor(t), help: docOf(offered?.get(t)) });
                }
                if (allow.void) add('void', { group: group('rest'), summary: 'Nothing' });
            }
            for (const [name, item] of engine) add(name, { group: group('rest'), detail: item.detail, color: G.ui.typeColor(name), help: docOf(item) });
            return out;
        };
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
        // A slot the schema says is a number (`counts`)
        if ((builder.counts || []).includes(i)) {
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
        const FILE = schema.typeGroup('file'), COMMON = schema.typeGroup('common'), GAME = schema.typeGroup('game'), REST = schema.typeGroup('rest');
        for (const d of fromFile) {
            entries.push({ title: d.name, key: d.name, type: d.name, group: FILE, detail: schema.declSpec(d.kind)?.title, color: G.ui.typeColor(d.name) });
        }
        if (called) {
            for (const t of schema.typePicker.common || []) entries.push({ title: t, key: t, type: t, group: COMMON, color: G.ui.typeColor(t) });
            for (const d of doc.declared(['struct', 'enum', 'alias'])) entries.push({ title: d.name, key: d.name, type: d.name, group: FILE, detail: schema.declSpec(d.kind)?.title, color: G.ui.typeColor(d.name) });
        } else {
            for (const mode of schema.paramModes.filter(m => schema.modeIsFilter(m))) {
                entries.push({ title: `${schema.modeLabel(mode)}…`, key: '\0' + mode, mode, group: 'Filters', summary: schema.modeHelp(mode), stay: true });
            }
        }
        entries.push({ title: 'Another type…', key: '\0more', other: true, group: REST, summary: 'Anything else it takes: pick its type', stay: true });
        const lang = app.lang;
        // What the rest of the game has that it can take, once tidels says: after the file's own, before the rest
        const asked = lang.status !== 'unavailable'
            ? app.checked().then(() => lang.completionInParams(routine)).then(items => {
                const have = new Set(entries.map(e => e.key));
                // The game's own (tide/graphNames) after the file's; what the engine gives (Time, Devices) with the rest
                const known = Object.keys(lang.names.game).length + Object.keys(lang.names.packages).length > 0;
                const offered = items.filter(i => TYPE_KINDS.has(i.kind) && typeof i.label === 'string' && !called && !have.has(i.label))
                    .map(i => ({ title: i.label, key: i.label, type: i.label, group: !known || lang.declares(i.label) ? GAME : REST, detail: i.detail && i.detail !== 'built-in type' ? i.detail : undefined, help: docOf(i), color: G.ui.typeColor(i.label) }));
                const own = entries.filter(e => e.group === FILE || e.group === COMMON);
                const game = offered.filter(e => e.group === GAME), engine = offered.filter(e => e.group === REST);
                if (offered.length) p.refill([...own, ...game, ...entries.filter(e => !own.includes(e)), ...engine]);
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
        for (const spec of s.nodes) {
            // A number, a name or a field is picked in the input that reads it, not added as a node
            if (s.inlineKind(spec.k) && !(takesValue && spec.k === 'member')) continue;
            if (wantExec && !spec.exec) continue;
            if (wantValue && !spec.value) continue;
            if (takesValue && !(spec.inputs?.length || spec.lists?.length || spec.items?.length || spec.map)) continue;
            if (spec.k === 'local' || spec.k === 'outVar' || spec.k === 'out') continue; // Wires from a local's pin, and a call's own
            const category = s.categoryByName.get(spec.category);
            out.push({ title: spec.title, key: 'k:' + spec.k, summary: spec.help, group: category?.title || spec.category, color: category?.color, node: { k: spec.k, fields: {} }, keywords: spec.k });
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
        const memberType = context?.member ? frame.typeOfValue(context.member.object) : valueOut ? frame.typeOfPort(from) : null;
        const memberNode = context?.member ? context.member.object : valueOut ? frame.nodeForMembers(from) : 0;
        const entries = [...(memberType ? localMembers(app, memberType) : []), ...catalog(app, frame, context)];
        let asked = null;
        if (lang.status !== 'unavailable') {
            if (memberNode || (valueOut && memberType)) {
                // What the value has: by its node, or for a pin's by its type
                const members = () => (memberNode ? lang.members(memberNode) : lang.membersOfType(memberType).then(items => ({ items, type: memberType })));
                asked = app.checked().then(members).then(({ items, type }) => items.map(i => G.lang.entryOf(i, { member: true })).filter(Boolean)
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
        // What fits where a wire was dropped comes first; everything else when asked (as Blueprint's "Context
        // Sensitive" off)
        const EVERYTHING = { title: 'Show everything', summary: 'What goes anywhere in this code, not only what fits here', key: '\0all', group: 'Or', all: true, stay: true, untyped: true };
        const p = picker({
            x: clientX, y: clientY, className: 'tg-node-search', placeholder: placeholderOf(context), title,
            entries: from && !context?.member ? [...entries, EVERYTHING] : entries, more: asked,
            empty: text => `Nothing called "${text}" here.`,
            onPick: entry => {
                if (entry.all) {
                    const place = () => lang.placeFor(null, frame.slot.owner);
                    p.reset({
                        entries: catalog(app, frame, null), title: 'Everything', placeholder: 'What should happen here?',
                        more: lang.status === 'unavailable' ? null : app.checked().then(() => lang.completion(place())).then(items => items.map(i => G.lang.entryOf(i, {})).filter(Boolean).map(e => ({ ...e, key: 'c:' + e.title, summary: undefined }))),
                    });
                    return;
                }
                if (context?.member) app.retarget(frame, context.member, entry);
                else app.addFromSearch(frame, entry, at, context);
            },
            onClose: () => app.board.el.focus({ preventScroll: true }),
        });
        return p;
    }

    // -----------------------------------------------------------------------------------------------------------
    // The value picker of an input: what can go in it, picked from a list with a search box. A value of its type
    // to type in; what this code has (what it takes, its locals), each opening to its fields; the game's constants
    // and functions; and "Work it out…", an operator or a call, which is a node wired in.

    // Which controls edit the same kind of value: a tick doesn't go where a number does
    const FAMILY = { whole: 'number', decimal: 'number', tick: 'tick', text: 'text', char: 'char', vector: 'vector', colour: 'colour' };

    // Whether a value of type `have` can go where `want` is taken, as far as the page can tell: a number goes where
    // a number does, not where a tick or a vector does; something with fields (a component, a struct) isn't a number;
    // and a number isn't one of the game's own types (`declared(type)`). What the page can't tell (a generic's T,
    // an alias) fits.
    function fits(schema, have, want, declared) {
        if (!have || !want || have === want) return true;
        const a = schema.control(have), b = schema.control(want);
        if (!a && !b) return true;
        if (!b) return !declared(want);
        if (!a) return false;
        return FAMILY[a] === FAMILY[b] && a !== 'vector' && a !== 'colour';
    }

    // Whether a type opens to fields: one with no control of its own (a component, a struct), a vector's x y z, a
    // colour's r g b
    function opens(schema, type) {
        if (!type) return false;
        const c = schema.control(type);
        return !c || c === 'vector' || c === 'colour';
    }

    // What a function gives, by its completion item's detail (the game's "int Twice(int amount)", the engine's
    // "Spawn(components...) -> Entity"): its type, '' for one that gives nothing (it's no value), null when the
    // detail doesn't say
    function resultIn(detail) {
        const text = String(detail || '').trim();
        const gives = /->\s*(.+)$/.exec(text);
        if (gives) return gives[1].trim();
        const typed = /^(.+?)\s+[A-Za-z_][\w.]*\s*\(.*\)$/.exec(text);
        if (typed) return typed[1].split(/\s+/).pop() === 'void' ? '' : typed[1].split(/\s+/).pop();
        return /^[A-Za-z_][\w.]*\s*\(.*\)$/.test(text) ? '' : null;
    }

    // The type a completion item's detail says: "const float" -> float, "float" -> float
    function typeIn(detail) {
        const words = String(detail || '').trim().split(/\s+/);
        const last = words[words.length - 1];
        return /^[A-Za-z_][\w.<>?,\[\] ]*$/.test(last) && !['enum', 'struct', 'component', 'singleton', 'event', 'scene', 'input', 'function', 'const'].includes(last) ? last : null;
    }

    function valuePicker(app, frame, port, { anchor = null, from = 0, work = false, x = 0, y = 0 } = {}) {
        const ed = app.editor;
        const s = ed.schema;
        const doc = ed.doc;
        const lang = app.lang;
        const g = frame.graph;
        const readerNode = port.node > 0 ? frame.analysis.byId.get(port.node) : null;
        // What an operator works on says what's typed into it, not what fits: a vector takes a number times it
        const hint = readerNode?.k === 'binary' && !['&&', '||'].includes(readerNode.op);
        const want = frame.expectedType(port);
        // What Set changes is a place: something this code has, or a field of it. Nothing typed, nothing worked out.
        const place = readerNode?.k === 'assign' && port.key === 'target';
        const declared = t => !!lang.declares(t) || doc.declared(['struct', 'enum', 'alias', 'component', 'singleton', 'event', 'input', 'scene']).some(d => d.name === t);
        // What doesn't fit is left out, and counted (`skip()`): the list then ends with a way to see it too
        let loose = false, left = 0;
        const fit = type => loose || hint || fits(s, type, want, declared);
        const skip = () => { left++; return true; };
        // (An enum has choices, not fields: it opens to nothing)
        const open = type => opens(s, type) && lang.declares(type) !== 'enum' && !doc.enumMembers(type);
        const current = frame.wireOf(port);
        const path = current && frame.analysis.folded.has(current) ? doc.pathNodes(g, current) : [];
        const key = G.frames.portKey(port);
        const label = frame.ports.get(key)?.label || 'value';
        const reader = port.node > 0 ? port.node : 0;
        const HAS = 'What this code has', GAME = s.typeGroup('game'), REST = s.typeGroup('rest'), WORK = 'Work it out', READ = 'Only read here';
        const ALL = { title: "Show what doesn't fit too", key: '\0all', group: 'Or', all: true, stay: true, untyped: true };
        const BACK = { title: '‹ Back', key: '\0back', group: 'Something else', back: true, stay: true, untyped: true };
        const startOf = n => (n.k === 'local' ? { local: n.ref } : n.k === 'member' ? { shorthand: n.member } : n.k === 'name' ? { name: n.name } : { word: n.k });
        const textOf = b => [b.start.name ?? b.start.word ?? (b.start.shorthand ? '.' + b.start.shorthand : s.declaredLocal(Doc.nodeIn(g, b.start.local) || {}) || '?'), ...b.members].join('.');
        const colour = type => G.ui.typeColor(type) || undefined;
        let base = null; // What the list is inside: { start, members, type, node } (a path), or { work: true }
        let p = null;

        // What a pick puts in the input
        const put = make => frame.setInput(port, make);
        const pathOf = (b, more) => (d, graph, at) => d.makePath(graph, b.start, [...b.members, ...(more ? [more] : [])], at);
        // A node wired in goes under its reader, to the left, where nothing else is
        const under = at => {
            const wired = reader ? s.dataWires(Doc.nodeIn(g, reader) || {}).filter(w => !frame.analysis.folded.has(w)).length : 0;
            return frame.freeSpot({ x: at.x - 240, y: at.y + (frame.sizes.get(frame.statementOf(reader))?.h || 96) + 16 + 96 * wired });
        };
        const nodeOf = (k, fields, wrap) => (d, graph, at) => {
            const where = under(at);
            const n = d.newNode(k, where.x, where.y, fields);
            Doc.nodes(graph).push(n);
            // What was there goes on as the first thing the new node works on: frequency -> frequency × …
            if (wrap && current) s.setWire(n, [wrap], current);
            return n.id;
        };
        const methodOf = (b, name, count) => (d, graph, at) => {
            const where = under(at);
            const n = d.newNode('method', where.x, where.y, { name, args: Array(count).fill(null) });
            Doc.nodes(graph).push(n);
            s.setWire(n, ['object'], d.makePath(graph, b.start, b.members, where));
            return n.id;
        };

        // "Work it out…": the operators by what they're called (only those Tide takes for what's in the input, or
        // for what it takes, once tidels has said), then the other nodes that give a value
        let validOps = null;
        const opType = (current ? frame.typeOfValue(current) : null) || want;
        const workEntries = () => {
            const out = [];
            for (const o of s.operators.binary) if (!validOps || validOps.binary.has(o.op)) out.push({ title: o.title, detail: o.op, key: 'op:' + o.op, group: WORK, color: s.color('binary'), make: nodeOf('binary', { op: o.op }, 'lhs') });
            for (const o of s.operators.unary) if (!validOps || validOps.unary.has(o.op)) out.push({ title: o.title, detail: o.op, key: 'un:' + o.op, group: WORK, color: s.color('unary'), make: nodeOf('unary', { op: o.op }, 'value') });
            for (const spec of s.nodes) {
                if (spec.exec || !spec.value || s.inlineKind(spec.k) || ['binary', 'unary', 'method'].includes(spec.k)) continue;
                const wrap = spec.category !== 'math' ? null : spec.k === 'conditional' ? 'then' : spec.inputs?.[0];
                out.push({ title: spec.title, summary: spec.help, key: 'k:' + spec.k, group: WORK, color: s.color(spec.k), keywords: spec.k, make: nodeOf(spec.k, {}, wrap) });
            }
            return out;
        };

        // The first list: its type's own values, what this code has, the game's, then "Work it out…"
        const top = game => {
            const out = [];
            const control = s.control(want);
            const name = String(want || '').split('.').pop();
            const choices = want ? doc.enumMembers(want) || lang.choices[name] : null;
            for (const c of choices || []) out.push({ title: `${name}.${c}`, key: 'e:' + c, group: name, color: colour(want), make: pathOf({ start: { name }, members: [c] }) });
            if (control === 'tick') {
                out.push({ title: 'yes', detail: 'true', key: '\0yes', group: want, make: (d, graph, at) => d.makeLiteral(graph, 'bool', true, at) },
                    { title: 'no', detail: 'false', key: '\0no', group: want, make: (d, graph, at) => d.makeLiteral(graph, 'bool', false, at) });
            }
            if (control === 'vector' || control === 'colour') {
                const size = control === 'colour' ? 3 : G.values.vectorSize(want, s);
                out.push({ title: `${want}(…)`, summary: control === 'colour' ? 'Its red, green and blue, to fill in' : 'A number for each axis, to fill in', key: '\0new', group: want, color: colour(want),
                    make: (d, graph, at) => {
                        const call = d.newNode('call', at.x, at.y, { name: want, args: [] });
                        Doc.nodes(graph).push(call);
                        for (let i = 0; i < size; i++) call.args.push(d.makeLiteral(graph, control === 'colour' ? 'float' : /^int/.test(want) ? 'int' : 'float', control === 'colour' ? '1.0' : '0', at));
                        return call.id;
                    } });
            }
            const has = (title, type, start) => {
                if (!place && !fit(type) && !open(type)) { left++; return; }
                // Something with fields, where its whole isn't what's wanted, opens to them when it's picked
                const into = open(type) && !!type && type !== want;
                out.push({ title, key: 'h:' + title, detail: type || undefined, group: HAS, color: colour(type), start, type, drill: open(type), stay: into, into });
            };
            for (const pin of frame.pins) has(pin.name, pin.type, { name: pin.name });
            for (const d of frame.localsBefore(reader)) has(s.declaredLocal(d), frame.typeOfPort({ local: d.id }), { local: d.id });
            if (place) {
                out.length = 0;
                const readOnly = [];
                const offer = (title, type, start) => {
                    const can = frame.changeable(start);
                    if (can.ok) has(title, type, start);
                    else readOnly.push({ title, key: 'r:' + title, detail: type || undefined, summary: can.why, group: READ, color: colour(type), disabled: true, why: can.why });
                };
                for (const pin of frame.pins) offer(pin.name, pin.type, { name: pin.name });
                for (const d of frame.localsBefore(reader)) offer(s.declaredLocal(d), frame.typeOfPort({ local: d.id }), { local: d.id });
                if (!out.length) out.push({ title: 'Nothing here can be changed yet', summary: readOnly.length ? 'What this code only reads is below: make one of them "' + s.modeLabel('mut') + '" on the entry' : 'Add what it changes on the entry: + needs…', key: '\0none', group: HAS, disabled: true, why: 'Nothing to change' });
                out.push(...readOnly);
                if (current) out.push({ title: 'Nothing', summary: 'Empty it', key: '\0clear', group: 'Or', clear: true, untyped: true });
                return out;
            }
            // The game's own, then "Work it out…", then what the engine has (its namespaces' functions are calls too)
            out.push(...(game || []).filter(e => e.group !== REST));
            out.push({ title: 'Work it out…', summary: 'An operator or a call: a node, wired in', key: '\0work', group: WORK, work: true, drill: true, stay: true, untyped: true });
            out.push(...(game || []).filter(e => e.group === REST));
            out.push(...workEntries().map(e => ({ ...e, typed: true })));
            if (current) out.push({ title: 'Nothing', summary: 'Empty it', key: '\0clear', group: 'Or', clear: true, untyped: true });
            if (left) out.push(ALL);
            return out;
        };

        // What tidels offers where the value is written: the game's constants, enums and functions first, then the
        // engine's
        const gameEntries = async () => {
            if (lang.status === 'unavailable') return [];
            await app.checked();
            const place = lang.placeFor(reader ? { valueFor: frame.statementOf(reader) } : null, frame.slot.owner);
            const items = await lang.completion(place);
            const known = Object.keys(lang.names.game).length + Object.keys(lang.names.packages).length > 0;
            const mine = [], rest = [];
            const taken = new Set([...frame.pins.map(pin => pin.name), ...doc.locals(g).map(d => s.declaredLocal(d))]);
            for (const item of items) {
                const name = String(item.label || '').replace(/\(.*$/, '');
                if (!name || taken.has(name)) continue;
                const own = known ? !!lang.declares(name) : item.detail !== 'built-in type';
                const group = own ? GAME : REST;
                const to = own ? mine : rest;
                const help = docOf(item);
                if (item.kind === KIND.Constant || item.kind === KIND.Value || (item.kind === KIND.Keyword && name === 'this')) {
                    const type = item.kind === KIND.Keyword ? typeIn(item.detail) : typeIn(item.detail);
                    if (!fit(type) && !open(type) && skip()) continue;
                    (item.kind === KIND.Keyword ? mine : to).push({ title: name, key: 'n:' + name, detail: type || item.detail, group: item.kind === KIND.Keyword ? HAS : group, color: colour(type), help,
                        start: item.kind === KIND.Keyword ? { word: 'this' } : { name }, type, drill: open(type), stay: open(type) && type !== want, into: open(type) && type !== want });
                } else if (item.kind === KIND.Enum && item.detail === 'enum') {
                    if (!fit(name) && skip()) continue;
                    to.push({ title: name, key: 'n:' + name, detail: 'its choices', group, color: colour(name), help, start: { name }, named: true, drill: true, stay: true, into: true });
                } else if (item.kind === KIND.Module) {
                    to.push({ title: name, key: 'n:' + name, detail: 'its functions', summary: item.detail, group, help, start: { name }, named: true, drill: true, stay: true, into: true });
                } else if (item.kind === KIND.Function || item.kind === KIND.Method) {
                    // A call is offered where what it gives can go: not one that gives nothing
                    const gives = resultIn(item.detail);
                    if (gives === '' || (gives && !fit(gives) && skip())) continue;
                    const params = G.lang.paramNames(item);
                    to.push({ title: `${name}(${params.join(', ')})`, key: 'f:' + name, detail: gives || 'a call', group, color: colour(gives) || s.color('call'), help, keywords: name, make: nodeOf('call', { name, args: params.map(() => null) }) });
                } else if ((item.kind === KIND.Struct || item.kind === KIND.Class) && own && ['struct', 'component', 'event'].includes(item.detail)) {
                    if (!fit(name) && skip()) continue;
                    to.push({ title: `${name} { … }`, key: 't:' + name, detail: `a new ${item.detail}`, group, color: colour(name), help, keywords: name, make: nodeOf('literal', { type: name, inits: [] }) });
                }
            }
            return [...mine, ...rest];
        };

        // What something has: its fields to go on to, its methods to call (a node); an enum's choices, a
        // namespace's functions
        const memberEntries = async b => {
            let items = [];
            if (lang.status !== 'unavailable') {
                await app.checked();
                if (b.named) items = await lang.membersOfName(b.start.name);
                else if (b.node && lang.span(b.node)) items = (await lang.members(b.node)).items;
                if (!items.length && b.type) items = await lang.membersOfType(b.type);
            }
            const out = [];
            for (const item of items) {
                const name = String(item.label || '').replace(/\(.*$/, '');
                if (!name) continue;
                if (item.kind === KIND.Method || item.kind === KIND.Function) {
                    const gives = resultIn(item.detail);
                    if (place || gives === '' || (gives && !fit(gives) && skip())) continue;
                    const params = G.lang.paramNames(item);
                    out.push({ title: `${name}(${params.join(', ')})`, key: 'm:' + name, detail: gives || 'a call', color: colour(gives) || s.color('method'), help: docOf(item), keywords: name, make: methodOf(b, name, params.length) });
                } else {
                    const type = typeIn(item.detail);
                    if (!fit(type) && !open(type) && skip()) continue;
                    const into = !fit(type);
                    out.push({ title: name, key: 'm:' + name, detail: type || undefined, color: colour(type), help: docOf(item), field: name, type, drill: open(type) && !!type, into, stay: into });
                }
            }
            return out;
        };
        const localMembers = b => doc.membersOf(b.type).filter(m => (m.method ? !place : fit(m.type) || open(m.type) || !skip())).map(m => (m.method
            ? { title: `${m.name}(${m.method.join(', ')})`, key: 'm:' + m.name, detail: 'a call', color: s.color('method'), keywords: m.name, make: methodOf(b, m.name, m.method.length) }
            : { title: m.name, key: 'm:' + m.name, detail: m.type, color: colour(m.type), field: m.name, type: m.type, drill: open(m.type), into: !fit(m.type), stay: !fit(m.type) }));

        let lastGame = null;
        const show = () => {
            left = 0;
            if (!base) {
                const game = gameEntries();
                p.reset({
                    entries: top(lastGame), title: place ? `What does it change? Something this code can change.` : `What goes in '${label}'?${want ? ` A ${want}.`.replace(/^ A ([aeiou])/i, ' An $1') : ''}`,
                    placeholder: place ? 'Search…' : s.control(want) === 'whole' || s.control(want) === 'decimal' ? 'Type a number, or search…' : s.control(want) === 'text' ? 'Type the text, or search…' : want ? 'Search…' : 'Search, or type a number…',
                    more: game.then(entries => { lastGame = entries; if (!base) p.refill(top(entries)); return []; }),
                });
            } else if (base.work) {
                p.reset({ entries: [...workEntries().map(e => ({ ...e, group: undefined })), BACK], title: `'${label}': work it out`, placeholder: 'An operator or a call…' });
            } else {
                const text = textOf(base);
                const whole = base.named || !fit(base.type) ? [] : [{ title: text, key: '\0itself', detail: base.type || undefined, summary: 'All of it', color: colour(base.type), itself: true }];
                const local = base.type ? localMembers(base) : [];
                const choices = base.named ? (doc.enumMembers(base.start.name) || []).map(c => ({ title: c, key: 'm:' + c, field: c, color: colour(base.start.name) })) : [];
                // What something has comes before all of it where a field is what's likely meant: something to change
                const first = place || (want && base.type !== want) ? [...choices, ...local] : [...whole, ...choices, ...local];
                const after = first.length === choices.length + local.length ? whole : [];
                p.reset({
                    entries: first, title: `${text}: which of it?`, placeholder: `Of ${text}…`,
                    more: memberEntries(base).then(found => { const have = new Set(first.map(e => e.key)); return [...found.filter(e => !have.has(e.key)), ...after, ...(left ? [ALL] : []), BACK]; }),
                });
            }
        };

        // Goes into what an entry has
        const drill = entry => {
            if (entry.work) base = { work: true };
            else if (entry.field) base = { ...base, members: [...base.members, entry.field], type: entry.type, node: 0, named: false };
            else if (entry.start) base = { start: entry.start, members: [], type: entry.type, node: 0, named: !!entry.named };
            else return;
            show();
        };

        // Out of what the list is inside, to what it was in
        const back = () => {
            if (!base) return;
            if (base.work || !base.members.length) base = null;
            else base = { ...base, members: base.members.slice(0, -1), type: null, node: 0 };
            show();
        };

        p = picker({
            anchor, x, y, className: 'tg-value-picker', entries: [],
            empty: text => (base ? `It has nothing called "${text}".` : `Nothing called "${text}" that goes here.`),
            status: 'Asking what the game has…',
            onDrill: drill,
            onBack: () => back(),
            free: (text, shown) => {
                if (base || !text || place) return null;
                const control = s.control(want);
                if (G.values.NUMBER.test(text) && (!control || control === 'whole' || control === 'decimal')) {
                    const kind = G.values.INT.test(text.replace(/^-/, '')) ? 'int' : 'float';
                    return { title: text, key: '\0typed', detail: 'a number', make: (d, graph, at) => d.makeLiteral(graph, kind, text, at) };
                }
                if (control === 'text' || (!control && /^["“]/.test(text))) {
                    const body = text.replace(/^["“]|["”]$/g, '');
                    // Text that is also the start of a name comes after the names
                    return { title: `“${body}”`, key: '\0typed', detail: 'text', last: shown.length > 0 && control === 'text', make: (d, graph, at) => d.makeLiteral(graph, 'string', body, at) };
                }
                if (control === 'char' && [...text].length === 1) return { title: `'${text}'`, key: '\0typed', detail: 'a character', make: (d, graph, at) => d.makeLiteral(graph, 'char', `'${text}'`, at) };
                return null;
            },
            onPick: entry => {
                if (entry.all) { loose = true; show(); return; }
                if (entry.back) { back(); return; }
                if (entry.work || entry.into) { drill(entry); return; }
                if (entry.clear) put(null);
                else if (entry.itself) put(pathOf(base));
                else if (entry.field) put(pathOf(base, entry.field));
                else if (entry.start) put(pathOf({ start: entry.start, members: [] }));
                else if (entry.make) put(entry.make);
                else return;
                // A value Set can't take the way it changes now, but can another way (a float into a float3 with
                // "becomes"): how it changes it is asked next, with only the ways it can (Blueprint offers no wrong pin)
                if (readerNode?.k !== 'assign' || port.key !== 'value' || !entry.type) return;
                // (once a check has the value, not one of the file before it: the menu then offers the ways for it)
                let checks = 0;
                const off = lang.on(async what => {
                    if (what !== 'check') return;
                    const n = app.editor.doc.find(reader)?.value;
                    if (lang.status === 'checked' && n?.value && !lang.typeOf(n.value) && ++checks < 3) return;
                    off();
                    const target = n?.target ? lang.typeOf(n.target) : null;
                    if (lang.status !== 'checked' || !n?.value || !target) return;
                    const value = lang.typeOf(n.value);
                    if (value === target) return;
                    const ways = (await lang.operators(target, value))?.assign || [];
                    if (!ways.length || ways.includes(n.op ?? '=') || G.ui.popupOpen()) return;
                    app.board.elementOf(reader)?.querySelector('[data-op]')?.click();
                });
            },
            onClose: () => app.board.el.focus({ preventScroll: true }),
        });
        // Opened on a part of a path: what the part before it has, to change the path from there
        if (from > 0 && path.length >= from) {
            base = { start: startOf(path[0]), members: path.slice(1, from).map(n => n.member), node: path[from - 1].id, type: frame.typeOfValue(path[from - 1].id), named: false };
            if (path[0].k === 'name' && !base.type && from === 1) base.named = !!doc.enumMembers(path[0].name) || !frame.pins.some(pin => pin.name === path[0].name);
        }
        if (work && !place) base = { work: true };
        show();
        // The operators that fit, once tidels says: the lists showing them are made again
        if (opType && !place) {
            lang.operators(opType).then(r => {
                if (!r || !G.ui.popupOpen()) return;
                validOps = { binary: new Set(r.binary || []), unary: new Set(r.unary || []) };
                if (base?.work) p.refill([...workEntries().map(e => ({ ...e, group: undefined })), BACK]);
                else if (!base) p.refill(top(lastGame));
            });
        }
        return p;
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

    G.pickers = { addMenu, addEntries, typePicker, needsPicker, valuePicker, nodeSearch, jumpTo, contents, nameFor, lowerFirst, fits };
})(globalThis.TideGraph = globalThis.TideGraph || {});
