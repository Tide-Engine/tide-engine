// Where a graph's nodes go (notes/visual-scripting.md): statements left to right along `next`, a statement's values
// to the left of it and below, and the code a statement holds below it. A row that grows too wide goes on below the
// rest. What nothing reaches goes on a row of its own at the bottom. The converter lays out what it makes with it,
// and graph editors ask tidels for it (tide/graphLayout).

#include "graph.h"

#include <string.h>

enum { NODE_GAP = 40, ROW_GAP = 60, MAX_ROW = 3200 };

typedef struct layout {
    graph_layout_node *nodes;
    int count;
    int *slots; // Open addressing, by id: index + 1, or 0
    int cap;
} layout;

static graph_layout_node *find(const layout *l, const int id)
{
    if (!id || !l->cap) return NULL;
    unsigned h = (unsigned)id * 2654435761u;
    for (;;) {
        const int slot = l->slots[h % (unsigned)l->cap];
        if (!slot) return NULL;
        if (l->nodes[slot - 1].id == id) return &l->nodes[slot - 1];
        h++;
    }
}

void graph_layout_measure(graph_layout_node *n, const int widest_text)
{
    const int text = widest_text * 8;
    n->w = 140 + (text > 260 ? 260 : text);
    const int ports = n->inputs.count + n->bodies.count + 1;
    n->h = 48 + 24 * ports;
}

// A value's tree, its right edge at `right` and its top at `top`: the height it takes
static int place_value(layout *l, const int id, const int right, const int top)
{
    graph_layout_node *n = find(l, id);
    if (!n || n->placed) return 0;
    n->placed = true;
    n->x = right - n->w;
    n->y = top;
    int height = 0;
    for (int i = 0; i < n->inputs.count; i++) {
        const int h = place_value(l, n->inputs.items[i], n->x - NODE_GAP, top + height);
        height += h ? h + 16 : 0;
    }
    if (height) height -= 16;
    return height > n->h ? height : n->h;
}

// How wide a value's tree is, from its node leftwards
static int value_width(const layout *l, const int id, const int depth)
{
    const graph_layout_node *n = find(l, id);
    if (!n || depth > 256) return 0;
    int widest = 0;
    for (int i = 0; i < n->inputs.count; i++) {
        const int w = value_width(l, n->inputs.items[i], depth + 1);
        if (w > widest) widest = w;
    }
    return n->w + (widest ? NODE_GAP + widest : 0);
}

typedef struct extent {
    int right, bottom;
} extent;

// Statements from `id` on, starting at (x, y): how far right and down they go
static extent place_chain(layout *l, int id, int x, const int y)
{
    extent e = {x, y};
    const int row_x = x;
    int row_top = y;
    int row_bottom = y;
    while (id) {
        graph_layout_node *n = find(l, id);
        if (!n || n->placed) break;
        int inputs_w = 0;
        for (int i = 0; i < n->inputs.count; i++) {
            const int w = value_width(l, n->inputs.items[i], 0);
            if (w > inputs_w) inputs_w = w;
        }
        if (x > row_x && x + inputs_w + n->w > row_x + MAX_ROW) { // A new row, below everything so far
            x = row_x;
            row_top = (row_bottom > e.bottom ? row_bottom : e.bottom) + ROW_GAP;
            row_bottom = row_top;
        }
        const int node_x = x + (inputs_w ? inputs_w + NODE_GAP : 0);
        n->placed = true;
        n->x = node_x;
        n->y = row_top;
        // Its values below it, to its left
        int values_h = 0;
        for (int i = 0; i < n->inputs.count; i++) {
            const int h = place_value(l, n->inputs.items[i], node_x - NODE_GAP, row_top + n->h + 16 + values_h);
            values_h += h ? h + 16 : 0;
        }
        int bottom = row_top + n->h + 16 + values_h;
        int right = node_x + n->w;
        // The code it holds, below
        for (int i = 0; i < n->bodies.count; i++) {
            const extent b = place_chain(l, n->bodies.items[i], node_x + 60, bottom + ROW_GAP / 2);
            if (b.bottom > bottom) bottom = b.bottom;
            if (b.right > right) right = b.right;
        }
        if (bottom > row_bottom) row_bottom = bottom;
        if (right > e.right) e.right = right;
        if (row_bottom > e.bottom) e.bottom = row_bottom;
        x = node_x + n->w + NODE_GAP * 2;
        id = n->next;
    }
    return e;
}

void graph_layout_place(graph_layout_node *nodes, const int count, const int root, const bool body)
{
    layout l = {nodes, count, NULL, 0};
    l.cap = count * 2 + 1;
    l.slots = arena_alloc(sizeof(int) * (size_t)l.cap);
    for (int i = 0; i < count; i++) {
        nodes[i].placed = false;
        unsigned h = (unsigned)nodes[i].id * 2654435761u;
        while (l.slots[h % (unsigned)l.cap]) h++;
        l.slots[h % (unsigned)l.cap] = i + 1;
    }
    if (body) place_chain(&l, root, 0, 0);
    else if (root) place_value(&l, root, 0, 0);
    int stray_x = 0, stray_y = 0;
    for (int i = 0; i < count; i++) {
        if (nodes[i].placed && nodes[i].y + nodes[i].h > stray_y) stray_y = nodes[i].y + nodes[i].h;
    }
    stray_y += ROW_GAP;
    for (int i = 0; i < count; i++) {
        if (nodes[i].placed) continue;
        nodes[i].x = stray_x;
        nodes[i].y = stray_y;
        stray_x += nodes[i].w + NODE_GAP;
    }
}

// A graph's JSON: its nodes laid out, as {"<id>": [x, y], ...}
bool graph_layout_json(const graph_json *g, sb *out)
{
    if (!g || g->kind != GJ_OBJECT) return false;
    const graph_json *nodes = graph_json_get(g, "nodes");
    if (!nodes || nodes->kind != GJ_ARRAY) return false;
    const graph_json *entry = graph_json_get(g, "entry");
    const graph_json *root = graph_json_get(g, "root");
    const bool body = !root;
    const graph_json *start = body ? entry : root;
    graph_layout_node *placed = arena_alloc(sizeof(graph_layout_node) * (size_t)(nodes->count ? nodes->count : 1));
    for (int i = 0; i < nodes->count; i++) {
        const graph_json *n = nodes->items[i];
        graph_layout_node *p = &placed[i];
        const graph_json *id = graph_json_get(n, "id");
        p->id = id && id->kind == GJ_NUMBER ? (int)id->number : 0;
        graph_data_wires(n, &p->inputs);
        graph_exec_wires(n, &p->bodies);
        const graph_json *next = graph_json_get(n, "next");
        p->next = next && next->kind == GJ_NUMBER ? (int)next->number : 0;
        int widest = 0;
        for (int k = 0; n->kind == GJ_OBJECT && k < n->count; k++) {
            if (n->items[k]->kind == GJ_STRING && n->items[k]->text.len > widest) widest = n->items[k]->text.len;
        }
        graph_layout_measure(p, widest);
    }
    graph_layout_place(placed, nodes->count, start && start->kind == GJ_NUMBER ? (int)start->number : 0, body);
    sb_put(out, "{");
    int written = 0;
    for (int i = 0; i < nodes->count; i++) {
        if (!placed[i].id) continue;
        sb_printf(out, "%s\"%d\":[%d,%d]", written++ ? "," : "", placed[i].id, placed[i].x, placed[i].y);
    }
    sb_put(out, "}");
    return true;
}
