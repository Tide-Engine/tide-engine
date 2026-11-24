// The outline: the file's declarations in order, namespaces' and types' members under them, and what has code or
// a value to show on the canvas. Click to select, double-click to rename, + to add.
(function (G) {
    'use strict';
    const { el, button, field } = G.ui;

    // A short tag for a kind
    const TAGS = {
        component: 'C', singleton: 'S', struct: 'St', event: 'E', input: 'In', scene: 'Sc', enum: 'En', const: 'K', asset: 'A',
        alias: '=', settings: '⚙', function: 'f', system: 'sys', view: 'v', handler: 'on', command: 'cmd', audio: '♪',
        frag: 'fr', vert: 'vt', compute: 'cs', prefab: 'P', namespace: 'N', text: 'T', field: '·', property: 'p',
        method: 'm', operator: 'op', tweak: 't', sample: 's', sanitize: 's', predict: 'p', prepare: 'p',
    };

    class Outline {
        constructor(app) {
            this.app = app;
            this.el = el('div.tg-outline');
            this.collapsed = new Set();
        }

        get editor() { return this.app.editor; }

        render() {
            const ed = this.editor;
            const app = this.app;
            const head = el('div.tg-pane-head', null, el('span', { text: 'Declarations' }),
                button('+', ev => app.declMenu(ev.clientX, ev.clientY, null), 'Add a declaration', 'tg-icon'));
            const tree = el('div.tg-tree', { role: 'tree' });
            if (!ed.error) {
                for (const d of ed.doc.decls) this.item(tree, d, 0);
                if (!ed.doc.decls.length) tree.append(el('div.tg-hint', { text: 'An empty graph. Add a declaration with +: a component, a system, a function...' }));
            }
            this.el.replaceChildren(head, tree);
            tree.querySelector('.tg-current')?.scrollIntoView?.({ block: 'nearest' });
        }

        item(tree, v, depth) {
            const ed = this.editor;
            const doc = ed.doc;
            const e = doc.find(v.id);
            if (!e) return;
            const children = this.childrenOf(v);
            const open = !this.collapsed.has(v.id);
            const errors = this.app.lang.diagnostics.filter(d => d.node && this.contains(v, d.node)).length;
            const current = ed.selected === v.id || (ed.slot && ed.slot.owner === v.id);
            const kind = v.kind ?? '';
            const label = el('span.tg-tree-label', { text: this.label(e) });
            const row = el('div.tg-tree-row' + (current ? '.tg-current' : '') + (ed.selected === v.id ? '.tg-selected' : ''), {
                role: 'treeitem',
                style: { paddingLeft: 6 + depth * 14 + 'px' },
                title: v.comment || undefined,
                onclick: () => ed.select(v.id),
                ondblclick: ev => {
                    ev.stopPropagation();
                    if (!('name' in v)) return;
                    const input = field(v.name, name => this.app.edit(() => { v.name = name; }), { className: 'tg-input.tg-rename' });
                    label.replaceWith(input);
                    input.focus();
                    input.select();
                    input.addEventListener('blur', () => this.render());
                },
            },
            el('span.tg-twisty', { text: children.length ? (open ? '▾' : '▸') : '', onclick: ev => {
                ev.stopPropagation();
                if (open) this.collapsed.add(v.id); else this.collapsed.delete(v.id);
                this.render();
            } }),
            el('span.tg-tag', { text: TAGS[kind] || kind.slice(0, 2), title: kind }),
            label,
            errors ? el('span.tg-tree-errors', { text: String(errors), title: `${errors} problem${errors > 1 ? 's' : ''}` }) : null);
            tree.append(row);
            if (open) for (const c of children) this.item(tree, c, depth + 1);
        }

        label(e) {
            const v = e.value;
            if (e.role === 'decl' && v.kind !== 'text' && v.kind !== 'settings' && v.kind !== 'handler') return v.name ?? v.kind;
            return this.editor.doc.describe(e);
        }

        // What's under an element in the outline: a namespace's declarations, a type's members, a property's accessors
        childrenOf(v) {
            if (v.kind === 'namespace') return v.decls || [];
            if (Array.isArray(v.members) && v.kind !== 'enum') return v.members.filter(m => m && typeof m.id === 'number');
            if (Array.isArray(v.accessors)) return v.accessors;
            return [];
        }

        // Whether an element has the element `id` in it
        contains(v, id) {
            const e = this.editor.doc.find(id);
            for (let x = e; x;) {
                if (x.value === v) return true;
                const owner = x.role === 'node' || x.role === 'note' ? x.slot?.owner : x.owner?.id;
                x = owner ? this.editor.doc.find(owner) : null;
            }
            return false;
        }
    }

    G.Outline = Outline;
})(globalThis.TideGraph = globalThis.TideGraph || {});
