// A value edited by its type's own control, where it's drawn: a number, a tick box, text, a vector's numbers, a
// colour, an enum's choices. A value's graph (a constant's value, a field's starting value, a setting) that is one
// literal is shown and edited that way; one that's more opens as a small graph beside its card (board.js).
(function (G) {
    'use strict';
    const { el, field, button } = G.ui || {}; // No page (a test of the model): what a value is, without its control
    const { set } = G.json;
    const { Doc } = G;

    // Types with a control of their own
    const NUMBERS = new Set(['int', 'long', 'byte', 'float', 'double']);
    const WHOLE = new Set(['int', 'long', 'byte']);
    const VECTORS = { float2: 2, float3: 3, float4: 4, int2: 2, int3: 3, int4: 4, double2: 2, double3: 3, double4: 4 };
    const AXES = ['x', 'y', 'z', 'w'];
    // A number as Tide writes one: 12, 0xFF, 1.5, .5, 1e3, 2.5d, 90deg
    const NUMBER = /^-?(0[xX][0-9a-fA-F_]+L?|0[bB][01_]+L?|(\d[\d_]*\.?\d*|\.\d+)([eE][+-]?\d+)?(L|f|d|deg)?)$/;
    const INT = /^-?(0[xX][0-9a-fA-F_]+L?|0[bB][01_]+L?|\d[\d_]*L?)$/;

    // What a type is edited as: 'number', 'bool', 'string', 'char', 'vector' (with `size`), 'color', 'enum' (with
    // `members`), or 'other'
    function typeKind(type, doc) {
        if (!type) return { kind: 'other' };
        const t = String(type).replace(/\?$/, '');
        if (NUMBERS.has(t)) return { kind: 'number', whole: WHOLE.has(t) };
        if (t === 'bool') return { kind: 'bool' };
        if (t === 'string') return { kind: 'string' };
        if (t === 'char') return { kind: 'char' };
        if (VECTORS[t]) return { kind: 'vector', size: VECTORS[t], name: t, whole: t.startsWith('int') };
        if (t === 'Color') return { kind: 'color' };
        const members = doc?.enumMembers(t);
        if (members) return { kind: 'enum', members, name: t.split('.').pop() };
        return { kind: 'other' };
    }

    // A number literal's text, of a node or of a `-` over one; null for anything else
    function numberText(g, id) {
        const n = Doc.nodeIn(g, id);
        if (!n) return null;
        if (n.k === 'int' || n.k === 'float') return typeof n.text === 'string' ? n.text : null;
        if (n.k === 'unary' && n.op === '-') {
            const inner = Doc.nodeIn(g, n.value);
            return inner && (inner.k === 'int' || inner.k === 'float') && typeof inner.text === 'string' ? '-' + inner.text : null;
        }
        return null;
    }

    // What a value's graph is, when it's one literal: { kind: 'none' } for no value, 'number' { text }, 'bool'
    // { value }, 'string' / 'char' { text }, 'call' { name, texts } (a vector's or a colour's numbers), 'member'
    // { type, member } (an enum's), 'word' { word }; { kind: 'graph' } for anything more
    function classify(g) {
        if (!g || typeof g !== 'object' || !g.root) return { kind: 'none' };
        const root = Doc.nodeIn(g, g.root);
        if (!root) return { kind: 'graph' };
        const number = numberText(g, root.id);
        if (number !== null) return { kind: 'number', text: number };
        switch (root.k) {
            case 'bool': return { kind: 'bool', value: !!root.value };
            case 'string': return { kind: 'string', text: root.text ?? '' };
            case 'char': return { kind: 'char', text: root.text ?? '' };
            case 'default':
            case 'null': return { kind: 'word', word: root.k };
            case 'call': {
                if (root.typeArgs?.length || root.argNames || root.local || !Array.isArray(root.args)) return { kind: 'graph' };
                const texts = root.args.map(a => numberText(g, a));
                return texts.every(t => t !== null) ? { kind: 'call', name: root.name, texts } : { kind: 'graph' };
            }
            case 'member': {
                if (root.shorthand) return { kind: 'member', type: null, member: root.member };
                const object = Doc.nodeIn(g, root.object);
                return object?.k === 'name' && !object.local ? { kind: 'member', type: object.name, member: root.member } : { kind: 'graph' };
            }
            default: return { kind: 'graph' };
        }
    }

    // A short reading of a value that's more than a literal, for its chip
    function summary(g, schema) {
        const root = Doc.nodeIn(g, g?.root);
        if (!root) return 'nothing yet';
        if (root.k === 'call') return `${root.name}(…)`;
        if (root.k === 'method') return `.${root.name}(…)`;
        if (root.k === 'member') return `.${root.member}`;
        if (root.k === 'name') return root.name;
        if (root.k === 'literal') return `${root.type} { … }`;
        if (root.k === 'binary') return schema.opTitle(root.op);
        return schema.title(root.k);
    }

    // A literal's graph: one node, or a `-` over one for a negative number
    function numberGraph(doc, text) {
        const negative = text.startsWith('-');
        const body = negative ? text.slice(1) : text;
        const g = doc.valueGraph(INT.test(body) ? 'int' : 'float', body);
        if (!negative) return g;
        const minus = doc.newNode('unary', -180, 0, { op: '-' });
        g.nodes[0].x = -400;
        doc.schema.setWire(minus, ['value'], g.root);
        return { root: minus.id, nodes: [minus, g.nodes[0]] };
    }

    function toHex(texts) {
        const part = t => Math.max(0, Math.min(255, Math.round((parseFloat(t) || 0) * 255))).toString(16).padStart(2, '0');
        return '#' + part(texts[0]) + part(texts[1]) + part(texts[2]);
    }

    function fromHex(hex) {
        const n = parseInt(hex.slice(1), 16);
        const part = v => String(Math.round(v / 255 * 1000) / 1000);
        return [part((n >> 16) & 255), part((n >> 8) & 255), part(n & 255)];
    }

    // The control of a value: `holder[key]` is its graph (or absent), `type` what it has to be, `slot` the graph's
    // place in the file (for the small graph it opens as). `required` keeps a value there (a constant's); else an
    // emptied control takes the value away, and shows the type's default greyed.
    function control(app, { holder, key, type, slot, order, required = false, fk, label, onEnter, none = '0' }) {
        const doc = app.editor.doc;
        const g = holder[key];
        const what = classify(g);
        const tk = typeKind(type, doc);
        const put = make => app.edit(d => set(holder, key, make ? make(d) : undefined, order));
        const openGraph = () => app.openValue(slot, holder, key, order, type);
        const wrap = (...parts) => el('span.tg-value', { dataset: { value: `${slot.owner}:${key}` } }, parts,
            button('ƒ', openGraph, 'Work it out with nodes instead', 'tg-value-more'));
        const title = label ? `${label}` : undefined;

        if (what.kind === 'graph') {
            return el('span.tg-value', { dataset: { value: `${slot.owner}:${key}` } },
                button('ƒ ' + summary(g, app.editor.schema), openGraph, 'A value worked out with nodes: open it', 'tg-value-graph'));
        }
        // What kind of control: the type's, or the literal's own where the type doesn't say
        let kind = tk.kind;
        if (kind === 'other' || (what.kind !== 'none' && !fits(what, tk))) {
            kind = what.kind === 'none' ? 'other' : what.kind === 'call' ? (what.name === 'Color' ? 'color' : VECTORS[what.name] ? 'vector' : 'graph')
                : what.kind === 'member' ? 'graph' : what.kind;
        }
        if (kind === 'number') {
            return wrap(field(what.kind === 'number' ? what.text : '', v => {
                if (v.trim() === '') put(required ? d => numberGraph(d, '0') : null);
                else put(d => numberGraph(d, v.trim()));
            }, {
                placeholder: none, className: 'tg-input.tg-value-number', fk, title, onEnter,
                check: v => (v.trim() === '' || NUMBER.test(v.trim()) ? null : `"${v}" isn't a number`),
            }));
        }
        if (kind === 'bool') {
            const on = what.kind === 'bool' && what.value;
            const box = el('input', { type: 'checkbox', checked: on, title, onpointerdown: e => e.stopPropagation(), onchange: e => {
                const v = e.target.checked;
                put(!v && !required ? null : d => d.valueGraph('bool', String(v)));
            } });
            if (fk) box.dataset.fk = fk;
            return wrap(el('label.tg-value-bool', { onpointerdown: e => e.stopPropagation() }, box, el('span', { text: on ? 'yes' : 'no' })));
        }
        if (kind === 'string') {
            return wrap(field(what.kind === 'string' ? what.text : '', v => put(v === '' && !required ? null : d => d.valueGraph('string', v)),
                { placeholder: 'text', className: 'tg-input.tg-value-text', fk, title, onEnter }));
        }
        if (kind === 'char') {
            const shown = what.kind === 'char' ? what.text.replace(/^'|'$/g, '') : '';
            return wrap(field(shown, v => put(v === '' && !required ? null : d => d.valueGraph('char', `'${v}'`)),
                { placeholder: 'a', className: 'tg-input.tg-value-char', fk, title, check: v => ([...v].length <= 1 || /^\\.+$/.test(v) ? null : 'One character') }));
        }
        if (kind === 'vector') {
            const name = what.kind === 'call' ? what.name : tk.name;
            const size = VECTORS[name] || tk.size || 3;
            const texts = what.kind === 'call' ? what.texts.slice() : [];
            // One number for every axis is written as it is: float3(1)
            const spread = texts.length === 1 && size > 1;
            const inputs = [];
            const commit = () => {
                const vs = inputs.map(i => i.value.trim());
                if (vs.every(v => v === '')) { put(required ? d => d.callGraph(name, Array(size).fill('0')) : null); return; }
                if (vs.some(v => v !== '' && !NUMBER.test(v))) return;
                put(d => d.callGraph(name, vs.map(v => v || '0')));
            };
            for (let i = 0; i < size; i++) {
                inputs.push(field(spread ? texts[0] : texts[i] ?? '', commit, {
                    placeholder: '0', className: 'tg-input.tg-value-axis', fk: fk ? `${fk}.${i}` : undefined, title: AXES[i], onEnter: i === size - 1 ? onEnter : undefined,
                    check: v => (v.trim() === '' || NUMBER.test(v.trim()) ? null : `"${v}" isn't a number`),
                }));
            }
            return wrap(el('span.tg-value-vector', null, inputs));
        }
        if (kind === 'color') {
            const texts = what.kind === 'call' ? what.texts : null;
            const swatch = el('input.tg-value-color', { type: 'color', value: texts ? toHex(texts) : '#ffffff', title: title || 'Pick a colour', onpointerdown: e => e.stopPropagation() });
            if (fk) swatch.dataset.fk = fk;
            swatch.addEventListener('change', () => put(d => d.callGraph('Color', [...fromHex(swatch.value).map(t => (t.includes('.') ? t : t + '.0')), ...(texts && texts.length > 3 ? [texts[3]] : [])])));
            return wrap(el('span.tg-value-colour' + (texts ? '' : '.tg-unset'), null, swatch,
                texts ? null : el('span.tg-value-hint', { text: 'white' })));
        }
        if (kind === 'enum') {
            const current = what.kind === 'member' ? what.member : '';
            const options = [...(required ? [] : [['', `(${tk.members[0] ?? 'first'})`]]), ...tk.members.map(m => [m, m])];
            const s = G.ui.select(current, options, v => put(v === '' ? null : d => d.memberGraph(tk.name, v)), title, 'tg-select.tg-value-enum');
            if (fk) s.dataset.fk = fk;
            return wrap(s);
        }
        if (kind === 'word') {
            return el('span.tg-value', { dataset: { value: `${slot.owner}:${key}` } }, button(what.word, openGraph, 'Open it as a graph to change it', 'tg-value-graph'));
        }
        // A type with no control of its own: its value is made with nodes
        return el('span.tg-value', { dataset: { value: `${slot.owner}:${key}` } },
            button(what.kind === 'none' ? '+ value' : 'ƒ ' + summary(g, app.editor.schema), openGraph,
                what.kind === 'none' ? 'Give it a value, made with nodes' : 'Open its value', 'tg-value-graph' + (what.kind === 'none' ? '.tg-unset' : '')));
    }

    // Whether a literal is one its type's control can show
    function fits(what, tk) {
        switch (tk.kind) {
            case 'number': return what.kind === 'number';
            case 'bool': return what.kind === 'bool';
            case 'string': return what.kind === 'string';
            case 'char': return what.kind === 'char';
            case 'vector': return what.kind === 'call' && !!VECTORS[what.name] && (what.texts.length === VECTORS[what.name] || what.texts.length === 1);
            case 'color': return what.kind === 'call' && what.name === 'Color' && what.texts.length >= 3;
            case 'enum': return what.kind === 'member' && tk.members.includes(what.member) && (!what.type || what.type.split('.').pop() === tk.name);
            default: return false;
        }
    }

    G.values = { control, classify, typeKind, summary, NUMBER, numberGraph };
})(globalThis.TideGraph = globalThis.TideGraph || {});
