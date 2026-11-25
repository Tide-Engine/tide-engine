// The editor's state without its UI: the document, the texts it has had (for undo and redo) and what's selected:
// nodes and notes of one frame, or cards and frames of the board. Every edit writes the whole file and sends it to
// the host, whose document it is; a change the host makes (its own undo, a git checkout, the text editor) comes back
// as text.
(function (G) {
    'use strict';
    const { Doc } = G;

    const HISTORY = 500; // Texts kept for undo

    class Editor {
        constructor(schema, post) {
            this.schema = schema;
            this.post = post || (() => {});
            this.listeners = new Set();
            this.doc = Doc.empty(schema);
            this.text = '';
            this.history = [];
            this.at = -1;
            this.error = null;     // Why the text isn't a graph the editor can read, or null
            this.slot = null;      // The graph the selected nodes are in: { owner, key, index }
            this.selection = new Set(); // Nodes and notes selected in it
            this.picked = new Set();    // Cards and frames selected on the board
        }

        on(listener) {
            this.listeners.add(listener);
            return () => this.listeners.delete(listener);
        }

        emit(what) {
            for (const l of this.listeners) l(what, this);
        }

        // The file as it is in the host. `fresh` forgets the history (a file opened again)
        load(text, fresh) {
            this.text = text;
            if (fresh || this.at < 0) {
                this.history = [text];
                this.at = 0;
            }
            this.read(text);
        }

        read(text) {
            try {
                this.doc = text.trim() ? Doc.parse(text, this.schema) : Doc.empty(this.schema);
                this.error = null;
            } catch (e) {
                this.error = e.message;
            }
            this.keepValid();
            this.emit('document');
        }

        // A new schema (tidels's, after the host's copy): the document is read again
        setSchema(schema) {
            this.schema = schema;
            this.read(this.text);
        }

        // Drops what's selected that the text no longer has
        keepValid() {
            if (this.error) return;
            if (this.slot && !this.doc.graph(this.slot)) this.slot = null;
            for (const id of [...this.selection]) if (!this.doc.find(id)) this.selection.delete(id);
            for (const id of [...this.picked]) if (!this.doc.find(id)) this.picked.delete(id);
        }

        // The graph the selection is in, or undefined
        get graph() {
            return this.error ? undefined : this.doc.graph(this.slot);
        }

        // Changes the document with `change(doc)` and writes it; one step of undo. Returns what change returned.
        edit(change) {
            if (this.error) return undefined;
            const result = change(this.doc);
            this.doc.reindex();
            this.commit();
            return result;
        }

        commit() {
            const text = this.doc.text;
            if (text === this.text) {
                this.emit('selection');
                return;
            }
            this.history.length = this.at + 1;
            this.history.push(text);
            if (this.history.length > HISTORY) this.history.shift();
            this.at = this.history.length - 1;
            this.text = text;
            this.keepValid();
            this.post({ type: 'edit', text });
            this.emit('document');
        }

        get canUndo() { return this.at > 0; }
        get canRedo() { return this.at < this.history.length - 1; }

        undo() {
            if (!this.canUndo) return;
            this.go(this.at - 1);
        }

        redo() {
            if (!this.canRedo) return;
            this.go(this.at + 1);
        }

        go(at) {
            this.at = at;
            this.text = this.history[at];
            this.post({ type: 'edit', text: this.text });
            this.read(this.text);
        }

        // The host's document changed: by this editor's own edit (nothing to do), by the host's undo or redo of
        // one (a step of the history), or otherwise (a new step)
        external(text) {
            if (text === this.text) return;
            if (this.at > 0 && this.history[this.at - 1] === text) this.at--;
            else if (this.at < this.history.length - 1 && this.history[this.at + 1] === text) this.at++;
            else {
                this.history.length = this.at + 1;
                this.history.push(text);
                this.at = this.history.length - 1;
            }
            this.text = text;
            this.read(text);
        }

        // Selects nodes and notes of one graph (a frame's, or a value's), or adds to those selected in it
        selectNodes(slot, ids, { add = false } = {}) {
            const same = G.slotKey(slot) === G.slotKey(this.slot);
            if (!add || !same) this.selection.clear();
            this.picked.clear();
            this.slot = slot;
            for (const id of ids) this.selection.add(id);
            this.emit('selection');
        }

        // Selects cards and frames
        pick(ids, { add = false } = {}) {
            if (!add) this.picked.clear();
            this.selection.clear();
            for (const id of ids) this.picked.add(id);
            this.emit('selection');
        }

        clearSelection() {
            if (!this.selection.size && !this.picked.size) return;
            this.selection.clear();
            this.picked.clear();
            this.emit('selection');
        }

        // Selects an element wherever it is: a node or a note in its graph, a card or a frame on the board; for
        // anything else (a field, a parameter), what it's on
        select(id) {
            let e = this.doc.find(id);
            if (!e) {
                this.clearSelection();
                return;
            }
            if (e.role === 'node' || e.role === 'note') {
                this.selectNodes(e.slot, [id]);
                return;
            }
            const item = this.itemOf(id);
            if (item) this.pick([item]);
            else this.clearSelection();
        }

        // The card or frame an element is on (or is): its id, or 0
        itemOf(id) {
            const items = new Set(this.doc.items().filter(i => !i.region).map(i => i.id));
            let e = this.doc.find(id);
            for (let depth = 0; e && depth < 8; depth++) {
                if (items.has(e.id)) return e.id;
                if (e.role === 'node' || e.role === 'note') e = this.doc.find(e.slot?.owner);
                else if (e.owner && typeof e.owner.id === 'number') e = this.doc.find(e.owner.id);
                else break;
            }
            return 0;
        }

        // The selection as nodes' and notes' ids of a graph
        selectedIn(g) {
            const nodes = new Set(), notes = new Set();
            for (const id of this.selection) {
                const e = this.doc.find(id);
                if (!e || e.graph !== g) continue;
                (e.role === 'note' ? notes : nodes).add(id);
            }
            return { nodes, notes };
        }

        // The selected nodes with what goes with them: a statement's call, the values shown in their inputs
        selectedWithHidden(g) {
            const { nodes, notes } = this.selectedIn(g);
            return { nodes: this.doc.withFolded(g, nodes), notes };
        }

        // Copy, cut, paste, duplicate and delete of the selected nodes
        copySelection() {
            const g = this.graph;
            if (!g) return null;
            const { nodes, notes } = this.selectedWithHidden(g);
            if (!nodes.size && !notes.size) return null;
            return Doc.copy(g, nodes, notes);
        }

        deleteSelection() {
            const g = this.graph;
            if (!g) return false;
            const { nodes, notes } = this.selectedWithHidden(g);
            if (!nodes.size && !notes.size) return false;
            this.edit(doc => doc.deleteNodes(g, [...nodes, ...notes]));
            this.selection.clear();
            this.emit('selection');
            return true;
        }

        // Pastes a clip into a graph, `dx, dy` from where it was copied, and selects what came in
        paste(text, dx = 40, dy = 40, slot = this.slot) {
            const g = this.doc.graph(slot);
            if (!g || !Doc.isClip(text)) return [];
            const made = this.edit(doc => doc.paste(g, text, dx, dy));
            this.slot = slot;
            this.picked.clear();
            this.selection = new Set(made);
            this.emit('selection');
            return made;
        }

        duplicateSelection() {
            const clip = this.copySelection();
            return clip ? this.paste(clip, 40, 40) : [];
        }
    }

    G.Editor = Editor;
})(globalThis.TideGraph = globalThis.TideGraph || {});
