// Small DOM helpers the editor's views share: elements, inputs edited in place, popups (menus and pickers with a
// search box), a tooltip, toasts, and colours of types.
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
                        if (x === undefined || x === null) continue;
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
        for (const c of children.flat(Infinity)) {
            if (c === null || c === undefined || c === false) continue;
            e.append(c instanceof Node ? c : document.createTextNode(String(c)));
        }
        return e;
    }

    // Appends children as el() takes them: arrays flattened, nothing for null, undefined and false
    function put(parent, ...children) {
        for (const c of children.flat(Infinity)) {
            if (c === null || c === undefined || c === false) continue;
            parent.append(c instanceof Node ? c : document.createTextNode(String(c)));
        }
        return parent;
    }

    function svg(tag, attrs) {
        const e = document.createElementNS('http://www.w3.org/2000/svg', tag);
        for (const [k, v] of Object.entries(attrs || {})) e.setAttribute(k, v);
        return e;
    }

    // A text input edited where it's drawn: it looks like text until it's hovered or has the focus, commits on Enter,
    // Tab and on leaving it, and goes back on Escape. `check(value)` gives why a value can't be (it's then shown red
    // with the reason, and not committed); `fk` names it, so the focus comes back to it after the page draws again.
    function field(value, commit, { placeholder, className = 'tg-input', multiline = false, rows = 3, title, check, fk, onEnter, size, grow = false, mono = false } = {}) {
        const input = multiline ? el('textarea.' + className, { rows, placeholder, title, spellcheck: 'false' })
            : el('input.' + className, { type: 'text', placeholder, title, spellcheck: 'false', autocomplete: 'off' });
        if (mono) input.classList.add('tg-mono');
        if (fk) input.dataset.fk = fk;
        input.value = value ?? '';
        if (size) input.size = size;
        let start = input.value;
        const fit = () => {
            if (!grow || multiline) return;
            // As wide as its text (and a letter more: bold letters are wider than the digit `ch` measures), with
            // its padding and border, which the width includes
            input.style.width = `calc(${Math.max(grow, (input.value || placeholder || '').length + 1)}ch + 12px)`;
        };
        fit();
        const why = () => (check ? check(input.value) : null);
        const show = () => {
            const reason = why();
            input.classList.toggle('tg-invalid', !!reason);
            if (reason) input.dataset.problem = reason; else delete input.dataset.problem;
            return reason;
        };
        const done = () => {
            if (input.value === start) return true;
            if (show()) return false;
            start = input.value;
            commit(input.value);
            return true;
        };
        input.addEventListener('focus', () => { start = input.value; });
        input.addEventListener('input', () => { fit(); show(); });
        input.addEventListener('blur', e => {
            // Where the focus goes on to (a Tab, a click on another field): the page draws again when this one
            // commits, and the board puts the focus back there (Board.applyFocus)
            const next = e.relatedTarget?.closest?.('[data-fk]')?.dataset.fk;
            if (next) {
                G.ui.focusAfter = next;
                setTimeout(() => { if (G.ui.focusAfter === next) G.ui.focusAfter = null; }, 0);
            }
            if (!done()) {
                // What can't be isn't kept: back to what it was
                const reason = why();
                input.value = start;
                input.classList.remove('tg-invalid');
                delete input.dataset.problem;
                fit();
                if (reason) toast(reason);
            }
        });
        input.addEventListener('keydown', e => {
            e.stopPropagation(); // The board's keys aren't for a field being typed in
            if (e.key === 'Enter' && (!multiline || e.ctrlKey || e.metaKey)) {
                e.preventDefault();
                if (!done()) return;
                if (onEnter) onEnter(input);
                else input.blur();
            } else if (e.key === 'Escape') {
                input.value = start;
                input.classList.remove('tg-invalid');
                fit();
                input.blur();
            }
        });
        // A drag that starts in a field selects its text, and moves nothing
        input.addEventListener('pointerdown', e => e.stopPropagation());
        return input;
    }

    function checkbox(checked, commit, label, title) {
        const box = el('input', { type: 'checkbox', checked: !!checked, onchange: e => commit(e.target.checked), onpointerdown: e => e.stopPropagation() });
        return el('label.tg-check', { title, onpointerdown: e => e.stopPropagation() }, box, label ? el('span', { text: label }) : null);
    }

    function select(value, options, commit, title, className = 'tg-select') {
        const s = el('select.' + className, { title, onchange: e => commit(e.target.value), onkeydown: e => e.stopPropagation(), onpointerdown: e => e.stopPropagation() });
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
        return el('button.' + className, {
            type: 'button', title,
            onclick: e => { e.stopPropagation(); onclick(e); },
            onpointerdown: e => e.stopPropagation(),
        }, text);
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Popups: one on another (a type picker from a menu's form), each closed by a press elsewhere or Escape

    const popups = [];

    function place(box, x, y, anchor) {
        const r = box.getBoundingClientRect();
        const W = innerWidth, H = innerHeight;
        let left = x, top = y;
        if (anchor) {
            const a = anchor.getBoundingClientRect();
            left = a.left;
            top = a.bottom + 4;
            if (top + r.height > H - 4 && a.top - r.height - 4 >= 4) top = a.top - r.height - 4;
        }
        box.style.left = Math.max(4, Math.min(left, W - r.width - 4)) + 'px';
        box.style.top = Math.max(4, Math.min(top, H - r.height - 4)) + 'px';
    }

    // Shows `content` at a screen point, or under `anchor`. Returns { el, close }.
    function popup({ x = 0, y = 0, anchor = null, className = 'tg-popup', content, onClose, keep = false }) {
        if (!keep) closePopups();
        tooltip(0, 0, null);
        const box = el('div.' + className.split(' ').join('.'), { onpointerdown: e => e.stopPropagation(), oncontextmenu: e => { e.preventDefault(); e.stopPropagation(); } }, content);
        document.body.append(box);
        const entry = {
            el: box,
            close() {
                const at = popups.indexOf(entry);
                if (at < 0) return;
                for (const later of popups.splice(at)) {
                    later.el.remove();
                    later.onClose?.();
                }
            },
            onClose,
            replace() { place(box, x, y, anchor); },
        };
        popups.push(entry);
        place(box, x, y, anchor);
        return entry;
    }

    function closePopups() {
        popups[0]?.close();
    }

    function popupOpen() {
        return popups.length > 0;
    }

    document.addEventListener('pointerdown', e => {
        // A press outside every popup closes them all; inside one, those over it
        for (let i = popups.length - 1; i >= 0; i--) {
            if (popups[i].el.contains(e.target)) {
                popups[i + 1]?.close();
                return;
            }
        }
        closePopups();
    }, true);
    document.addEventListener('keydown', e => {
        if (e.key === 'Escape' && popups.length) {
            e.stopPropagation();
            e.preventDefault();
            popups[popups.length - 1].close();
        }
    }, true);
    addEventListener('blur', () => tooltip(0, 0, null));

    // A menu of items at a screen point: { label, summary, run, checked, disabled, why, group, separator, danger,
    // stay (the menu stays open: a tick box) }
    function menu(x, y, items, { anchor = null, keep = false, title = null } = {}) {
        const list = el('div.tg-menu-list', { role: 'menu' });
        let group;
        if (title) list.append(el('div.tg-menu-title', { text: title }));
        const shown = items.filter(Boolean);
        let p = null;
        for (const item of shown) {
            if (item.group !== undefined && item.group !== group) {
                group = item.group;
                if (group) list.append(el('div.tg-menu-group', { text: group }));
            }
            if (item.separator) {
                list.append(el('div.tg-menu-separator'));
                continue;
            }
            const row = el('div.tg-menu-item' + (item.disabled ? '.tg-disabled' : '') + (item.danger ? '.tg-danger' : ''), {
                role: item.checked !== undefined ? 'menuitemcheckbox' : 'menuitem',
                title: item.disabled ? item.why : item.title,
                dataset: item.name ? { name: item.name } : undefined,
                onclick: e => {
                    e.stopPropagation();
                    if (item.disabled) return;
                    if (item.stay) {
                        item.checked = !item.checked;
                        row.querySelector('.tg-menu-check').textContent = item.checked ? '✓' : '';
                        item.run(item.checked);
                        return;
                    }
                    p?.close();
                    item.run(e);
                },
            },
            item.checked !== undefined ? el('span.tg-menu-check', { text: item.checked ? '✓' : '' }) : null,
            el('span.tg-menu-text', null, el('span.tg-menu-label', { text: item.label }), item.summary ? el('span.tg-menu-summary', { text: item.summary }) : null),
            item.hint ? el('span.tg-menu-hint', { text: item.hint }) : null);
            list.append(row);
        }
        p = popup({ x, y, anchor, className: 'tg-popup tg-menu', content: list, keep });
        return p;
    }

    // How well a title matches what's typed: whole, start, a word's start, inside; 0 for not at all
    function rank(title, typed) {
        if (!typed) return 1;
        const t = String(title).toLowerCase(), q = typed.toLowerCase();
        if (t === q) return 5;
        if (t.startsWith(q)) return 4;
        if (new RegExp('(^|[^a-z0-9])' + q.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')).test(t)) return 3;
        if (t.includes(q)) return 2;
        return 0;
    }

    // A list to pick from with a search box that narrows it as it's typed: the Add menu, the node search, the type
    // picker, jump-to. Entries are { title, detail, summary, group, color, tag, keywords, disabled, why }; `more` adds
    // entries later (what tidels answers), under `status` meanwhile. Enter takes the first match. An entry with
    // `drill` opens to more (a value's fields): its arrow, the right arrow key or a typed '.' calls `onDrill(entry)`,
    // and Backspace in an empty box `onBack()`; `reset({ entries, more, title, placeholder })` shows another list in
    // the same popup. An entry with `typed` is shown only once something is typed, one with `untyped` only before.
    function picker({ x = 0, y = 0, anchor = null, placeholder = 'Search…', title = null, entries = [], more = null, status = null, onPick, onClose,
        empty = 'Nothing by that name', typed = '', keep = false, width = null, className = '', footer = null, free = null, onDrill = null, onBack = null }) {
        const input = el('input.tg-input.tg-picker-input', { type: 'text', placeholder, spellcheck: 'false', autocomplete: 'off' });
        input.value = typed;
        const list = el('div.tg-picker-list', { role: 'listbox' });
        const statusEl = el('div.tg-picker-status');
        const titleEl = el('div.tg-picker-title', { text: title || '' });
        titleEl.style.display = title ? '' : 'none';
        const box = el('div.tg-picker-box', null, titleEl, input, statusEl, list, footer ? el('div.tg-picker-footer', null, footer) : null);
        let asked = 0; // Which list's later entries are awaited
        let all = entries.slice();
        let shown = [];
        let chosen = 0;
        let asking = !!more;
        let p = null;
        const pick = entry => {
            if (!entry || entry.disabled) return;
            const text = input.value.trim();
            if (!entry.stay) p.close();
            onPick(entry, text, p);
        };
        const fill = () => {
            const text = input.value.trim();
            let scored = all.filter(e => (text ? !e.untyped : !e.typed))
                .map((e, i) => ({ e, i, r: Math.max(rank(e.title, text), text && e.keywords ? Math.min(2, rank(e.keywords, text)) : 0) || (text && e.group && rank(e.group, text) >= 4 ? 0.5 : 0) }))
                .filter(s => s.r > 0);
            if (text) scored.sort((a, b) => b.r - a.r || a.i - b.i);
            shown = scored.map(s => s.e).slice(0, 300);
            // What's typed as a value of its own (a number, text): first, or after the names it is the start of
            const made = free ? free(text, shown) : null;
            if (made && made.last) shown.push(made);
            else if (made) shown.unshift(made);
            chosen = Math.min(chosen, Math.max(0, shown.length - 1));
            statusEl.textContent = asking ? (status || 'Asking tidels…') : '';
            statusEl.style.display = statusEl.textContent ? 'block' : 'none';
            list.replaceChildren();
            let group = null;
            shown.forEach((e, i) => {
                if (!text && e.group && e.group !== group) {
                    group = e.group;
                    list.append(el('div.tg-picker-group', null, el('span', { text: group }), e.groupSummary ? el('span.tg-picker-group-summary', { text: e.groupSummary }) : null));
                }
                const row = el('div.tg-picker-item' + (i === chosen ? '.tg-chosen' : '') + (e.disabled ? '.tg-disabled' : ''), {
                    role: 'option',
                    title: e.disabled ? e.why : e.help,
                    dataset: e.name ? { name: e.name } : undefined,
                    onclick: () => pick(e),
                    onpointermove: () => {
                        if (chosen === i) return;
                        chosen = i;
                        for (const r of list.querySelectorAll('.tg-chosen')) r.classList.remove('tg-chosen');
                        row.classList.add('tg-chosen');
                    },
                },
                e.color ? el('span.tg-swatch', { style: { background: e.color } }) : null,
                el('span.tg-picker-text', null,
                    el('span.tg-picker-line', null, el('span.tg-picker-name', { text: e.title }), e.detail ? el('span.tg-picker-detail', { text: e.detail }) : null),
                    e.summary ? el('span.tg-picker-summary', { text: e.summary }) : null),
                e.tag ? el('span.tg-picker-tag', { text: e.tag }) : null,
                e.note ? el('span.tg-picker-note', { text: e.note }) : null,
                e.drill && onDrill ? el('span.tg-picker-drill', { text: '›', title: 'What it has', onclick: ev => { ev.stopPropagation(); onDrill(e); } }) : null);
                list.append(row);
            });
            if (!shown.length && !asking) list.append(el('div.tg-picker-none', { text: typeof empty === 'function' ? empty(text) : empty }));
            list.querySelector('.tg-chosen')?.scrollIntoView?.({ block: 'nearest' });
        };
        input.addEventListener('input', () => {
            // A '.' after a name goes into what it has, as it's written: wobble.
            if (onDrill && input.value.endsWith('.') && input.value.length > 1) {
                const name = input.value.slice(0, -1).trim().toLowerCase();
                const into = shown.find(e => e.drill && String(e.title).toLowerCase() === name) || (shown[chosen]?.drill ? shown[chosen] : null);
                if (into) { onDrill(into); return; }
            }
            chosen = 0;
            fill();
        });
        input.addEventListener('keydown', e => {
            e.stopPropagation();
            if (e.key === 'ArrowRight' && onDrill && shown[chosen]?.drill && input.selectionStart === input.value.length) { e.preventDefault(); onDrill(shown[chosen]); }
            else if (e.key === 'Backspace' && onBack && input.value === '') { e.preventDefault(); onBack(); }
            else if (e.key === 'ArrowDown') { e.preventDefault(); chosen = Math.min(shown.length - 1, chosen + 1); fill(); }
            else if (e.key === 'ArrowUp') { e.preventDefault(); chosen = Math.max(0, chosen - 1); fill(); }
            else if (e.key === 'Enter') { e.preventDefault(); pick(shown[chosen]); }
            else if (e.key === 'Escape') { e.preventDefault(); p.close(); }
            else if (e.key === 'Tab') { e.preventDefault(); pick(shown[chosen]); }
        });
        p = popup({ x, y, anchor, className: 'tg-popup tg-picker ' + className, content: box, onClose, keep });
        if (width) p.el.style.width = width + 'px';
        fill();
        p.replace();
        input.focus({ preventScroll: true });
        // What was typed to open it stays, to be typed on from
        if (typed) input.setSelectionRange(typed.length, typed.length); else input.select();
        const later = promise => {
            const mine = ++asked;
            asking = !!promise;
            if (!promise) return;
            Promise.resolve(promise).then(extra => {
                if (mine !== asked || !popups.includes(p)) return;
                asking = false;
                const known = new Set(all.map(e => e.key ?? e.title));
                const add = (extra || []).filter(e => e && !known.has(e.key ?? e.title));
                // What tidels knows goes where its entries ask to: before the rest, or after
                all = [...add.filter(e => e.first), ...all, ...add.filter(e => !e.first)];
                fill();
            }, () => { if (mine === asked && popups.includes(p)) { asking = false; fill(); } });
        };
        later(more);
        p.input = input;
        p.refill = next => { if (next) all = next; fill(); };
        // Another list in the same popup: what an entry opened to, or the list before it
        p.reset = ({ entries: next = [], more: nextMore = null, title: nextTitle = null, placeholder: nextHint }) => {
            all = next.slice();
            chosen = 0;
            input.value = '';
            if (nextHint !== undefined) input.placeholder = nextHint;
            titleEl.textContent = nextTitle || '';
            titleEl.style.display = nextTitle ? '' : 'none';
            later(nextMore);
            fill();
            p.replace();
            input.focus({ preventScroll: true });
        };
        return p;
    }

    // One tooltip, shown near a point
    let tip = null;
    function tooltip(x, y, text, kind) {
        if (!tip) {
            tip = el('div.tg-tooltip');
            document.body.append(tip);
        }
        if (!text) {
            tip.style.display = 'none';
            return;
        }
        tip.className = 'tg-tooltip' + (kind ? ' tg-tooltip-' + kind : '');
        tip.replaceChildren();
        const parts = Array.isArray(text) ? text : [text];
        for (const part of parts) {
            if (!part) continue;
            if (typeof part === 'string') tip.append(el('div', { text: part }));
            else tip.append(el('div.tg-tip-' + part.kind, { text: part.text }));
        }
        tip.style.display = 'block';
        const r = tip.getBoundingClientRect();
        tip.style.left = Math.max(4, Math.min(x + 12, innerWidth - r.width - 4)) + 'px';
        tip.style.top = Math.max(4, y + 18 + r.height > innerHeight - 4 ? y - r.height - 10 : y + 18) + 'px';
    }

    // A line said for a few seconds at the bottom of the page: why something can't be, what was done (with a way
    // back). One at a time.
    let toastEl = null, toastTimer = 0;
    function toast(text, { action = null, run = null, kind = '' } = {}) {
        if (!toastEl) {
            toastEl = el('div.tg-toast', { role: 'status' });
            document.body.append(toastEl);
        }
        clearTimeout(toastTimer);
        if (!text) {
            toastEl.classList.remove('tg-toast-shown');
            return;
        }
        toastEl.className = 'tg-toast tg-toast-shown' + (kind ? ' tg-toast-' + kind : '');
        toastEl.replaceChildren();
        put(toastEl, el('span', { text }), action ? button(action, () => { toast(null); run(); }, null, 'tg-toast-action') : null);
        toastTimer = setTimeout(() => toastEl.classList.remove('tg-toast-shown'), action ? 7000 : 4500);
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

    G.ui = { el, put, svg, field, checkbox, select, button, popup, closePopups, popupOpen, menu, picker, rank, tooltip, toast, typeColor };
})(globalThis.TideGraph = globalThis.TideGraph || {});
