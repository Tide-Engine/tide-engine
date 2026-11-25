// Where a graph's nodes go (notes/visual-scripting.md): statements left to right along `next`, a statement's values
// to the left of it and below, and the code a statement holds below it. A row that grows too wide goes on below the
// rest. What nothing reaches goes on a row of its own at the bottom. The converter lays out what it makes with it,
// and graph editors ask tidels for it (tide/graphLayout).

#include "graph.h"

#include <string.h>

enum { NODE_GAP = 40, ROW_GAP = 60, MAX_ROW = 3200 };

static str key_text(const graph_json *v, const char *key)
{
    const graph_json *s = graph_json_get(v, key);
    return s && s->kind == GJ_STRING ? s->text : (str){NULL, 0};
}

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
    n->text = widest_text;
    const int text = widest_text * 8;
    if (str_eq_c(n->kind, "reroute")) { // A knot
        n->w = 34;
        n->h = 18;
        return;
    }
    if (str_eq_c(n->kind, "name") || str_eq_c(n->kind, "local") || str_eq_c(n->kind, "member") || str_eq_c(n->kind, "this")) {
        // A Get: its name and its pin, and a member's Target pin
        n->w = 48 + (text > 200 ? 200 : text) + (str_eq_c(n->kind, "member") && n->inputs.count ? 60 : 0);
        n->h = 32;
        return;
    }
    n->w = 140 + (text > 260 ? 260 : text);
    const int ports = n->inputs.count + n->bodies.count + 1;
    n->h = 48 + 24 * ports;
}

static void index_nodes(layout *l)
{
    l->cap = l->count * 2 + 1;
    l->slots = arena_alloc(sizeof(int) * (size_t)l->cap);
    for (int i = 0; i < l->count; i++) {
        unsigned h = (unsigned)l->nodes[i].id * 2654435761u;
        while (l->slots[h % (unsigned)l->cap]) h++;
        l->slots[h % (unsigned)l->cap] = i + 1;
    }
}

// Whether a node is shown in the input that reads it: 1 yes, 0 no, worked out once (`state`: 0 not yet, 1 being
// worked out, 2 no, 3 yes)
static bool folds(const layout *l, const graph_layout_node *n, const int *readers, char *state)
{
    const int at = (int)(n - l->nodes);
    if (state[at]) return state[at] == 3;
    state[at] = 1;
    bool yes = false;
    const int shown = readers[at] == 1 ? graph_schema_inline(n->kind) : 0;
    const graph_layout_node *of = n->inputs.count == 1 ? find(l, n->inputs.items[0]) : NULL;
    if (shown == 1) yes = n->inputs.count == 0;
    else if (shown == 2) yes = n->inputs.count == 0 || (of && graph_schema_inline(of->kind) != 3 && folds(l, of, readers, state));
    else if (shown == 5) yes = n->inputs.count == 0 && n->name.len && n->name.ptr[0] >= 'A' && n->name.ptr[0] <= 'Z';
    else if (shown == 3) yes = str_eq_c(n->op, "-") && of && (str_eq_c(of->kind, "int") || str_eq_c(of->kind, "float")) && folds(l, of, readers, state);
    else if (shown == 4 && graph_schema_numbers(n->name)) { // float2(16, -14): each argument a number shown there
        yes = n->inputs.count > 0;
        for (int i = 0; yes && i < n->inputs.count; i++) {
            const graph_layout_node *arg = find(l, n->inputs.items[i]);
            const int number = arg ? graph_schema_inline(arg->kind) : 0;
            yes = arg && (str_eq_c(arg->kind, "int") || str_eq_c(arg->kind, "float") || number == 3) && folds(l, arg, readers, state);
        }
    }
    state[at] = yes ? 3 : 2;
    return yes;
}

// How many characters a folded value shows: a path's names and its dots
static int folded_text(const layout *l, const graph_layout_node *n, const int depth)
{
    if (graph_schema_inline(n->kind) == 4) return 8 * n->inputs.count; // A box for each of its numbers
    const graph_layout_node *of = n->inputs.count == 1 && depth < 64 ? find(l, n->inputs.items[0]) : NULL;
    return n->text + (of && of->folded ? 1 + folded_text(l, of, depth + 1) : 0);
}

void graph_layout_fold(graph_layout_node *nodes, const int count)
{
    layout l = {nodes, count, NULL, 0};
    index_nodes(&l);
    int *readers = arena_alloc(sizeof(int) * (size_t)(count ? count : 1));
    char *state = arena_alloc((size_t)(count ? count : 1));
    memset(readers, 0, sizeof(int) * (size_t)count);
    memset(state, 0, (size_t)count);
    for (int i = 0; i < count; i++) {
        for (int k = 0; k < nodes[i].inputs.count; k++) {
            const graph_layout_node *read = find(&l, nodes[i].inputs.items[k]);
            if (read) readers[read - nodes]++;
        }
    }
    // What a Set changes, read by it alone, is in its title: Set score, Set speed, Set scale (what it's a member
    // of is wired in)
    for (int i = 0; i < count; i++) {
        const graph_layout_node *target = nodes[i].target ? find(&l, nodes[i].target) : NULL;
        if (!target || readers[target - nodes] != 1 || !graph_schema_titled(nodes[i].kind)) continue;
        if (str_eq_c(target->kind, "name") || str_eq_c(target->kind, "local") || str_eq_c(target->kind, "member")) {
            state[target - nodes] = 3;
        }
    }
    for (int i = 0; i < count; i++) nodes[i].folded = folds(&l, &nodes[i], readers, state);
    // A reader is as wide as its widest input's row: the input's name, then what's shown in it
    for (int i = 0; i < count; i++) {
        if (nodes[i].folded) continue;
        for (int k = 0; k < nodes[i].inputs.count; k++) {
            const graph_layout_node *read = find(&l, nodes[i].inputs.items[k]);
            if (!read || !read->folded) continue;
            int chip = folded_text(&l, read, 0) * 8 + 24;
            if (chip > 260) chip = 260;
            if (nodes[i].w < 100 + chip) nodes[i].w = 100 + chip;
        }
    }
    for (int i = 0; i < count; i++) {
        if (nodes[i].folded) nodes[i].w = nodes[i].h = 0;
    }
}

// A value's tree, its right edge at `right` and its top at `top`: the height it takes
static int place_value(layout *l, const int id, const int right, const int top)
{
    graph_layout_node *n = find(l, id);
    if (!n || n->placed) return 0;
    if (n->folded) { // Where its reader is: it's shown there, and what's wired into it (a titled member's
                     // object) is left of the reader, taking room as its own inputs would
        n->placed = true;
        n->x = right + NODE_GAP;
        n->y = top;
        int height = 0;
        for (int i = 0; i < n->inputs.count; i++) {
            const int h = place_value(l, n->inputs.items[i], right, top + height);
            height += h ? h + 16 : 0;
        }
        return height ? height - 16 : 0;
    }
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
    if (n->folded) return widest; // Shown in its reader: only what's wired into it takes room
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
    graph_schema_hold(true);
    graph_layout_fold(nodes, count);
    graph_schema_hold(false);
    index_nodes(&l);
    for (int i = 0; i < count; i++) nodes[i].placed = false;
    graph_layout_node *first = find(&l, root);
    if (first && first->folded) first->folded = false; // (A graph's own first node is never in another's input)
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
        if (nodes[i].folded) continue; // (Read by a node that's a stray itself: beside it)
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
        p->kind = key_text(n, "k");
        p->op = key_text(n, "op");
        p->name = key_text(n, "name");
        const graph_json *target = graph_json_get(n, "target");
        p->target = target && target->kind == GJ_NUMBER ? (int)target->number : 0;
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
       FRAME_GAP = 80, CARD_ROWS_MOST = 24, LANE_SHORTEST = 1200 };

// A card or a frame of the file, in the file's order, and its size
typedef struct board_item {
    int id, w, h;
    bool frame;
} board_item;
typedef VEC(board_item) board_items;

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
static void measure_frame(board_items *items, const graph_json *owner)
{
    int left = 0, top = 0, right = 0, bottom = 0; // Values are placed to the left of what reads them: below 0 too
    const graph_json *body = graph_json_get(owner, "body");
    const graph_json *nodes = body ? graph_json_get(body, "nodes") : NULL;
    const int count = nodes && nodes->kind == GJ_ARRAY ? nodes->count : 0;
    graph_layout_node *sizes = arena_alloc(sizeof(graph_layout_node) * (size_t)(count ? count : 1));
    memset(sizes, 0, sizeof(graph_layout_node) * (size_t)count);
    for (int i = 0; i < count; i++) {
        const graph_json *n = nodes->items[i];
        graph_layout_node *size = &sizes[i];
        size->id = id_of(n);
        size->kind = key_str(n, "k");
        size->op = key_str(n, "op");
        size->name = key_str(n, "name");
        const graph_json *target = graph_json_get(n, "target");
        size->target = target && target->kind == GJ_NUMBER ? (int)target->number : 0;
        graph_data_wires(n, &size->inputs);
        graph_exec_wires(n, &size->bodies);
        int widest = 0;
        for (int k = 0; n->kind == GJ_OBJECT && k < n->count; k++) {
            if (n->items[k]->kind == GJ_STRING && n->items[k]->text.len > widest) widest = n->items[k]->text.len;
        }
        graph_layout_measure(size, widest);
    }
    graph_layout_fold(sizes, count);
    for (int i = 0; i < count; i++) {
        const graph_json *n = nodes->items[i];
        const graph_layout_node size = sizes[i];
        if (size.folded) continue; // Shown in its reader
        const graph_json *x = graph_json_get(n, "x"), *y = graph_json_get(n, "y");
        const int nx = x && x->kind == GJ_NUMBER ? (int)x->number : 0, ny = y && y->kind == GJ_NUMBER ? (int)y->number : 0;
        if (nx < left) left = nx;
        if (ny < top) top = ny;
        if (nx + size.w > right) right = nx + size.w;
        if (ny + size.h > bottom) bottom = ny + size.h;
    }
    right -= left;
    bottom -= top;
    const int entry_h = 60 + CARD_ROW * (count_of(owner, "params") + count_of(owner, "payload") + 1);
    const board_item item = {id_of(owner), FRAME_PAD + ENTRY_W + LANE_GAP + right + FRAME_PAD,
                             FRAME_HEAD + (bottom > entry_h ? bottom : entry_h) + FRAME_PAD, true};
    if (item.id) vec_push(*items, item);
}

// A prefab's rows: each entity, and each component of it
static int prefab_rows(const graph_json *entity)
{
    if (!entity || entity->kind != GJ_OBJECT) return 0;
    int rows = 1 + count_of(entity, "components");
    const graph_json *children = graph_json_get(entity, "children");
    for (int i = 0; children && children->kind == GJ_ARRAY && i < children->count; i++) rows += prefab_rows(children->items[i]);
    return rows;
}

static void measure_decls(board_items *items, const graph_json *decls)
{
    static const char *const code[] = {"function", "audio", "system", "view", "handler", "command", "frag", "vert", "compute", NULL};
    static const char *const member_code[] = {"method", "operator", "sample", "sanitize", "predict", "prepare", NULL};
    for (int i = 0; decls && decls->kind == GJ_ARRAY && i < decls->count; i++) {
        const graph_json *d = decls->items[i];
        const str kind = key_str(d, "kind");
        if (str_eq_c(kind, "namespace")) {
            measure_decls(items, graph_json_get(d, "decls"));
            continue;
        }
        if (one_of(kind, code)) {
            measure_frame(items, d);
            continue;
        }
        // A card: a row for each field, member, entry, entity or line of text, and for its note and its attributes
        int rows = count_of(d, "members") + count_of(d, "entries") + prefab_rows(graph_json_get(d, "root"));
        const str text = key_str(d, "text");
        for (int k = 0; k < text.len; k++) rows += text.ptr[k] == '\n';
        if (rows > CARD_ROWS_MOST) rows = CARD_ROWS_MOST;
        rows += (key_str(d, "comment").len > 0) + (count_of(d, "attributes") > 0);
        const board_item card = {id_of(d), CARD_W, CARD_HEAD + CARD_ROW * (rows + 2), false};
        if (card.id) vec_push(*items, card);
        // ...and a frame for each member with code of its own
        const graph_json *members = graph_json_get(d, "members");
        for (int m = 0; members && members->kind == GJ_ARRAY && m < members->count; m++) {
            const graph_json *member = members->items[m];
            const str mk = key_str(member, "kind");
            if (one_of(mk, member_code)) measure_frame(items, member);
            const graph_json *accessors = graph_json_get(member, "accessors");
            for (int a = 0; accessors && accessors->kind == GJ_ARRAY && a < accessors->count; a++) measure_frame(items, accessors->items[a]);
        }
    }
}

// Frames in rows like a page's words, in the file's order, in a shape about as wide as a screen; the cards in
// columns to their left, as tall as the frames are together. Returns the frames' rows' height, placing them from
// `left` (not at all for `out` NULL).
static int place_frames(const board_items *items, const int wide, const int left, graph_board *out)
{
    int x = 0, y = 0, row = 0;
    for (int i = 0; i < items->count; i++) {
        const board_item *f = &items->items[i];
        if (!f->frame) continue;
        if (x > 0 && x + f->w > wide) {
            x = 0;
            y += row + FRAME_GAP;
            row = 0;
        }
        const graph_board_place p = {f->id, left + x, y};
        if (out) vec_push(*out, p);
        x += f->w + FRAME_GAP;
        if (f->h > row) row = f->h;
    }
    return y + row;
}

void graph_layout_board(const graph_json *file, graph_board *out)
{
    board_items items = {0};
    graph_schema_hold(true);
    measure_decls(&items, graph_json_get(file, "decls"));
    graph_schema_hold(false);
    // The frames' rows are as wide as makes the whole about 16:10, and no narrower than the widest frame
    double area = 0;
    int widest = 0, cards_h = 0;
    for (int i = 0; i < items.count; i++) {
        const board_item *item = &items.items[i];
        if (!item->frame) {
            cards_h += item->h + CARD_GAP;
            continue;
        }
        area += (double)(item->w + FRAME_GAP) * (item->h + FRAME_GAP);
        if (item->w > widest) widest = item->w;
    }
    int wide = widest;
    while ((double)wide * wide < area * 1.6) wide += 400;
    const int frames_h = place_frames(&items, wide, 0, NULL);
    const int lane_h = frames_h > LANE_SHORTEST ? frames_h : LANE_SHORTEST;
    int columns = 0;
    for (int i = 0, y = 0; i < items.count; i++) {
        const board_item *card = &items.items[i];
        if (card->frame) continue;
        if (!columns) columns = 1;
        if (y > 0 && y + card->h > lane_h) {
            columns++;
            y = 0;
        }
        const graph_board_place p = {card->id, (columns - 1) * (CARD_W + CARD_GAP), y};
        vec_push(*out, p);
        y += card->h + CARD_GAP;
    }
    (void)cards_h;
    place_frames(&items, wide, columns ? columns * (CARD_W + CARD_GAP) - CARD_GAP + LANE_GAP : 0, out);
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
