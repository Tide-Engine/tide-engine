// The editor's state without its UI: the document, the texts it has had (for undo and redo), what's selected and
// which graph is shown. Every edit writes the whole file and sends it to the host, whose document it is; a change
// the host makes (its own undo, a git checkout, the text editor) comes back as text.
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
            this.slot = null;      // The graph shown: { owner, key, index }
            this.selected = null;  // The element the inspector shows: an id
            this.selection = new Set(); // Nodes and notes selected on the canvas
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

        // Drops what's selected and shown that the text no longer has
        keepValid() {
            if (this.error) return;
            if (this.slot && !this.doc.graph(this.slot)) this.slot = null;
            if (this.selected && !this.doc.find(this.selected)) this.selected = null;
            for (const id of [...this.selection]) if (!this.doc.find(id)) this.selection.delete(id);
            if (!this.slot) this.slot = this.firstSlot();
        }

        // The first graph of the file that has code, or any
        firstSlot() {
            const graphs = this.doc.graphs();
            return (graphs.find(g => g.slot.key === 'body') || graphs[0])?.slot || null;
        }

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

        // Shows a graph, and selects what holds it
        show(slot) {
            if (!slot || !this.doc.graph(slot)) return;
            this.slot = slot;
            this.selection.clear();
            this.emit('graph');
        }

        // Selects an element: its graph shown if it's a node, a note or has one
        select(id, { open = true, add = false } = {}) {
            const e = this.doc.find(id);
            if (!e) {
                this.selected = null;
                if (!add) this.selection.clear();
                this.emit('selection');
                return;
            }
            this.selected = id;
            if (e.role === 'node' || e.role === 'note') {
                if (!e.slot || G.slotKey(e.slot) !== G.slotKey(this.slot)) {
                    this.slot = e.slot;
                    this.selection.clear();
                    this.emit('graph');
                }
                if (!add) this.selection.clear();
                this.selection.add(id);
            } else if (open) {
                const own = this.doc.graphs().filter(g => g.owner.id === id);
                const slot = (own.find(g => g.slot.key === 'body') || own[0])?.slot;
                if (slot && G.slotKey(slot) !== G.slotKey(this.slot)) {
                    this.slot = slot;
                    this.selection.clear();
                    this.emit('graph');
                }
            }
            this.emit('selection');
        }

        // The selection as nodes' and notes' ids of the shown graph
        selectedIn(g) {
            const nodes = new Set(), notes = new Set();
            for (const id of this.selection) {
                const e = this.doc.find(id);
                if (!e || e.graph !== g) continue;
                (e.role === 'note' ? notes : nodes).add(id);
            }
            return { nodes, notes };
        }

        // Copy, cut, paste, duplicate and delete of the canvas's selection
        copySelection() {
            const g = this.graph;
            if (!g) return null;
            const { nodes, notes } = this.selectedIn(g);
            if (!nodes.size && !notes.size) return null;
            return Doc.copy(g, nodes, notes);
        }

        deleteSelection() {
            const g = this.graph;
            if (!g) return;
            const { nodes, notes } = this.selectedIn(g);
            if (!nodes.size && !notes.size) return;
            this.edit(doc => doc.deleteNodes(g, [...nodes, ...notes]));
            this.selection.clear();
            this.emit('selection');
        }

        // Pastes a clip into the shown graph, `dx, dy` from where it was copied, and selects what came in
        paste(text, dx = 40, dy = 40) {
            const g = this.graph;
            if (!g || !Doc.isClip(text)) return [];
            const made = this.edit(doc => doc.paste(g, text, dx, dy));
            this.selection = new Set(made);
            this.selected = made[0] ?? this.selected;
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
