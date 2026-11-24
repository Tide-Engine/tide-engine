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
    graph_schema_hold(true);
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
    graph_schema_hold(false);
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

// ---------------------------------------------------------------------------
// The board: a file's declarations, each a card (data) or a frame (code)

enum { CARD_W = 300, CARD_ROW = 26, CARD_HEAD = 48, ENTRY_W = 320, FRAME_HEAD = 48, FRAME_PAD = 40, LANE_GAP = 80, CARD_GAP = 40,
       FRAME_GAP = 80 };

typedef struct board {
    graph_board *out;
    int data_y, code_y;
} board;

static str key_str(const graph_json *v, const char *key)
{
    const graph_json *s = graph_json_get(v, key);
    return s && s->kind == GJ_STRING ? s->text : (str){NULL, 0};
}

static int id_of(const graph_json *v)
{
    const graph_json *id = graph_json_get(v, "id");
    return id && id->kind == GJ_NUMBER ? (int)id->number : 0;
}

static int count_of(const graph_json *v, const char *key)
{
    const graph_json *a = graph_json_get(v, key);
    return a && a->kind == GJ_ARRAY ? a->count : 0;
}

static bool one_of(const str kind, const char *const *kinds)
{
    for (int i = 0; kinds[i]; i++) {
        if (str_eq_c(kind, kinds[i])) return true;
    }
    return false;
}

// A frame: its entry (what the code takes) on the left, its nodes as they're placed to the right of it
static void place_frame(board *b, const graph_json *owner)
{
    int right = 0, bottom = 0;
    const graph_json *body = graph_json_get(owner, "body");
    const graph_json *nodes = body ? graph_json_get(body, "nodes") : NULL;
    for (int i = 0; nodes && nodes->kind == GJ_ARRAY && i < nodes->count; i++) {
        const graph_json *n = nodes->items[i];
        graph_layout_node size = {0};
        graph_data_wires(n, &size.inputs);
        graph_exec_wires(n, &size.bodies);
        int widest = 0;
        for (int k = 0; n->kind == GJ_OBJECT && k < n->count; k++) {
            if (n->items[k]->kind == GJ_STRING && n->items[k]->text.len > widest) widest = n->items[k]->text.len;
        }
        graph_layout_measure(&size, widest);
        const graph_json *x = graph_json_get(n, "x"), *y = graph_json_get(n, "y");
        const int nx = x && x->kind == GJ_NUMBER ? (int)x->number : 0, ny = y && y->kind == GJ_NUMBER ? (int)y->number : 0;
        if (nx + size.w > right) right = nx + size.w;
        if (ny + size.h > bottom) bottom = ny + size.h;
    }
    const int entry_h = 60 + CARD_ROW * (count_of(owner, "params") + count_of(owner, "payload") + 1);
    const int h = FRAME_HEAD + (bottom > entry_h ? bottom : entry_h) + FRAME_PAD;
    const graph_board_place p = {id_of(owner), CARD_W + LANE_GAP, b->code_y};
    if (p.id) vec_push(*b->out, p);
    (void)right;
    b->code_y += h + FRAME_GAP;
}

static void place_decls(board *b, const graph_json *decls)
{
    static const char *const code[] = {"function", "audio", "system", "view", "handler", "command", "frag", "vert", "compute", NULL};
    static const char *const member_code[] = {"method", "operator", "sample", "sanitize", "predict", "prepare", NULL};
    for (int i = 0; decls && decls->kind == GJ_ARRAY && i < decls->count; i++) {
        const graph_json *d = decls->items[i];
        const str kind = key_str(d, "kind");
        if (str_eq_c(kind, "namespace")) {
            place_decls(b, graph_json_get(d, "decls"));
            continue;
        }
        if (one_of(kind, code)) {
            place_frame(b, d);
            continue;
        }
        // A card: a row for each field, member, entry or line of text
        int rows = count_of(d, "members") + count_of(d, "entries");
        const str text = key_str(d, "text");
        for (int k = 0; k < text.len; k++) rows += text.ptr[k] == '\n';
        if (rows > 24) rows = 24;
        const graph_board_place p = {id_of(d), 0, b->data_y};
        if (p.id) vec_push(*b->out, p);
        b->data_y += CARD_HEAD + CARD_ROW * (rows + 2) + CARD_GAP;
        // ...and a frame for each member with code of its own
        const graph_json *members = graph_json_get(d, "members");
        for (int m = 0; members && members->kind == GJ_ARRAY && m < members->count; m++) {
            const graph_json *member = members->items[m];
            const str mk = key_str(member, "kind");
            if (one_of(mk, member_code)) place_frame(b, member);
            const graph_json *accessors = graph_json_get(member, "accessors");
            for (int a = 0; accessors && accessors->kind == GJ_ARRAY && a < accessors->count; a++) place_frame(b, accessors->items[a]);
        }
    }
}

void graph_layout_board(const graph_json *file, graph_board *out)
{
    board b = {out, 0, 0};
    graph_schema_hold(true);
    place_decls(&b, graph_json_get(file, "decls"));
    graph_schema_hold(false);
}

bool graph_layout_board_json(const graph_json *file, sb *out)
{
    if (!file || file->kind != GJ_OBJECT || !graph_json_get(file, "decls")) return false;
    graph_board places = {0};
    graph_layout_board(file, &places);
    sb_put(out, "{");
    for (int i = 0; i < places.count; i++) {
        sb_printf(out, "%s\"%d\":[%d,%d]", i ? "," : "", places.items[i].id, places.items[i].x, places.items[i].y);
    }
    sb_put(out, "}");
    return true;
}
