// Small DOM helpers the editor's views share: elements, menus, a tooltip, and colours of types.
(function (G) {
    'use strict';

    // An element: el('div.a.b', { onclick, title, style: {...}, dataset: {...} }, children...)
    function el(spec, props, ...children) {
        const [tag, ...classes] = spec.split('.');
        const e = document.createElement(tag || 'div');
        if (classes.length) e.className = classes.join(' ');
        if (props) {
            for (const [k, v] of Object.entries(props)) {
                if (v === undefined || v === null || v === false) continue;
                if (k === 'style') {
                    for (const [name, x] of Object.entries(v)) {
                        if (name.startsWith('--')) e.style.setProperty(name, x);
                        else e.style[name] = x;
                    }
                }
                else if (k === 'dataset') Object.assign(e.dataset, v);
                else if (k.startsWith('on')) e.addEventListener(k.slice(2), v);
                else if (k === 'text') e.textContent = v;
                else if (k in e && typeof v !== 'string') e[k] = v;
                else e.setAttribute(k, v === true ? '' : v);
            }
        }
        for (const c of children.flat()) {
            if (c === null || c === undefined || c === false) continue;
            e.append(c instanceof Node ? c : document.createTextNode(String(c)));
        }
        return e;
    }

    function svg(tag, attrs) {
        const e = document.createElementNS('http://www.w3.org/2000/svg', tag);
        for (const [k, v] of Object.entries(attrs || {})) e.setAttribute(k, v);
        return e;
    }

    // A text input that commits on Enter and on leaving it, and goes back on Escape
    function field(value, commit, { placeholder, list, className = 'tg-input', multiline = false, rows = 3, title } = {}) {
        const input = multiline ? el('textarea.' + className, { rows, placeholder, title, spellcheck: 'false' })
            : el('input.' + className, { type: 'text', placeholder, title, spellcheck: 'false' });
        if (list) input.setAttribute('list', list);
        input.value = value ?? '';
        let start = input.value;
        const done = () => {
            if (input.value !== start) {
                start = input.value;
                commit(input.value);
            }
        };
        input.addEventListener('focus', () => { start = input.value; });
        input.addEventListener('change', done);
        input.addEventListener('keydown', e => {
            e.stopPropagation(); // The editor's keys are for the canvas
            if (e.key === 'Enter' && (!multiline || e.ctrlKey || e.metaKey)) {
                e.preventDefault();
                done();
                input.blur();
            } else if (e.key === 'Escape') {
                input.value = start;
                input.blur();
            }
        });
        return input;
    }

    function checkbox(checked, commit, label, title) {
        const box = el('input', { type: 'checkbox', checked: !!checked, onchange: e => commit(e.target.checked) });
        return el('label.tg-check', { title }, box, label ? el('span', { text: label }) : null);
    }

    function select(value, options, commit, title) {
        const s = el('select.tg-select', { title, onchange: e => commit(e.target.value), onkeydown: e => e.stopPropagation() });
        for (const o of options) {
            const [v, text] = Array.isArray(o) ? o : [o, o];
            s.append(el('option', { value: v, text }));
        }
        if (value !== undefined && !options.some(o => (Array.isArray(o) ? o[0] : o) === value)) {
            s.prepend(el('option', { value, text: value }));
        }
        s.value = value ?? '';
        return s;
    }

    function button(text, onclick, title, className = 'tg-button') {
        return el('button.' + className, { type: 'button', title, onclick: e => { e.stopPropagation(); onclick(e); } }, text);
    }

    // A menu of items { label, run, disabled, group } at a screen point; closes on any click elsewhere or Escape
    let openMenu = null;
    function menu(x, y, items) {
        closeMenu();
        const m = el('div.tg-menu', { role: 'menu' });
        let group = null;
        for (const item of items) {
            if (item.group !== undefined && item.group !== group) {
                group = item.group;
                if (group) m.append(el('div.tg-menu-group', { text: group }));
            }
            if (item.separator) {
                m.append(el('div.tg-menu-separator'));
                continue;
            }
            m.append(el('div.tg-menu-item' + (item.disabled ? '.tg-disabled' : ''), {
                role: 'menuitem',
                title: item.title,
                onpointerdown: e => e.stopPropagation(),
                onclick: e => {
                    e.stopPropagation();
                    if (item.disabled) return;
                    closeMenu();
                    item.run();
                },
            }, item.label));
        }
        document.body.append(m);
        const r = m.getBoundingClientRect();
        m.style.left = Math.max(0, Math.min(x, innerWidth - r.width - 4)) + 'px';
        m.style.top = Math.max(0, Math.min(y, innerHeight - r.height - 4)) + 'px';
        const away = e => { if (!m.contains(e.target)) closeMenu(); };
        const key = e => { if (e.key === 'Escape') { e.stopPropagation(); closeMenu(); } };
        setTimeout(() => document.addEventListener('pointerdown', away, true));
        document.addEventListener('keydown', key, true);
        openMenu = { m, away, key };
        return m;
    }

    function closeMenu() {
        if (!openMenu) return;
        openMenu.m.remove();
        document.removeEventListener('pointerdown', openMenu.away, true);
        document.removeEventListener('keydown', openMenu.key, true);
        openMenu = null;
    }

    // One tooltip, shown near a point
    let tip = null;
    function tooltip(x, y, text) {
        if (!tip) {
            tip = el('div.tg-tooltip');
            document.body.append(tip);
        }
        if (!text) {
            tip.style.display = 'none';
            return;
        }
        tip.textContent = text;
        tip.style.display = 'block';
        const r = tip.getBoundingClientRect();
        tip.style.left = Math.max(0, Math.min(x + 12, innerWidth - r.width - 4)) + 'px';
        tip.style.top = Math.max(0, Math.min(y + 16, innerHeight - r.height - 4)) + 'px';
    }

    // The colour of a value's type: a few fixed, others a hue of the name (as Unity's TideGraphTypes)
    const FIXED = {
        bool: '#c0392b', int: '#2aa198', long: '#2aa198', byte: '#2aa198', char: '#5f9ea0', float: '#7cb342', double: '#7cb342',
        string: '#d33682', float2: '#e6b422', float3: '#f0a030', float4: '#d98c1f', int2: '#26a69a', int3: '#26a69a',
        quaternion: '#8e7cc3', Color: '#ec407a', Entity: '#42a5f5', PlayerID: '#29b6f6', void: '#888888',
    };
    function typeColor(type) {
        if (!type) return null;
        if (FIXED[type]) return FIXED[type];
        let h = 0;
        for (let i = 0; i < type.length; i++) h = (h * 31 + type.charCodeAt(i)) >>> 0;
        return `hsl(${h % 360}, 55%, 58%)`;
    }

    G.ui = { el, svg, field, checkbox, select, button, menu, closeMenu, tooltip, typeColor };
})(globalThis.TideGraph = globalThis.TideGraph || {});
