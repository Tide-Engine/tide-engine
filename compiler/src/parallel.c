// Which systems in the tick can run at the same time, and why the others
// wait: the schedule the tick's threads follow (tide/jobs.h), shown in editors
// (above each system, and on hover) and by `tidec --schedule`.

#include <string.h>

#include "ast.h"

// How many of a system's waits through others, and of the systems it can run
// alongside, the analysis lists by name: a big game has thousands of each, and
// it counts the rest (decl.waits_unlisted, decl.alongside_count).
#define SCHEDULE_LISTED 32

// The archetypes a system runs on, as a bitset.
typedef struct arch_set {
    uint64_t words[(MAX_ARCHETYPES + 63) / 64];
} arch_set;

// The archetypes whose entities a system can run for: those its filters admit,
// the components it needs that aren't sparse there and none it excludes, and
// the sparse ones it needs among what those entities can have (program.possible).
static arch_set archetypes_of(const program *prog, const decl *sys)
{
    arch_set set = {{0}};
    const comp_mask need = mask_andnot(sys->need_mask, prog->sparse_mask);
    const comp_mask without = mask_andnot(sys->without_mask, prog->sparse_mask);
    const comp_mask sparse_need = mask_and(sys->need_mask, prog->sparse_mask);
    for (int a = 0; a < prog->archetypes.count && a < MAX_ARCHETYPES; a++) {
        const comp_mask mask = prog->archetypes.items[a];
        if (prog->archetype_local.items[a] != sys->entity_local) continue; // Another world's
        if (mask_subset(need, mask) && !mask_intersects(mask, without) && mask_subset(sparse_need, prog->possible.items[a])) {
            set.words[a / 64] |= (uint64_t)1 << (a % 64);
        }
    }
    return set;
}

static bool share_archetypes(const arch_set *a, const arch_set *b)
{
    for (int i = 0; i < (MAX_ARCHETYPES + 63) / 64; i++) {
        if (a->words[i] & b->words[i]) return true;
    }
    return false;
}

// Whether two systems can be at the same rows: they share an archetype. A
// sparse component one needs and the other excludes keeps them off the same
// entity, but not off the same pages, as it's the row's, not the archetype's:
// both go through the archetype's chunks, and a page of a chunk that a
// snapshot shares is replaced by whoever writes it (tide_table_lanes_begin),
// which loses what the other wrote there meanwhile. So those wait for each
// other like any two that write the same component.
static bool can_meet(const arch_set *sa, const arch_set *sb)
{
    return share_archetypes(sa, sb);
}

static bool add_conflict(system_wait *w, const conflict_kind kind)
{
    const conflict c = {NULL, kind};
    if (w) vec_push(w->conflicts, c);
    return true;
}

int parallel_loops(const stmt *s)
{
    if (!s) return 0;
    if (s->kind == S_PARALLEL || (s->kind == S_FOREACH && s->value->type.kind == TY_GRID && s->parallel)) return 1;
    int count = 0;
    for (int i = 0; i < s->stmts.count; i++) count += parallel_loops(s->stmts.items[i]);
    for (int i = 0; i < s->cases.count; i++) {
        for (int k = 0; k < s->cases.items[i].body.count; k++) count += parallel_loops(s->cases.items[i].body.items[k]);
    }
    return count + parallel_loops(s->init) + parallel_loops(s->step) + parallel_loops(s->then_stmt)
         + parallel_loops(s->else_stmt);
}

bool world_reads_own_writes(const decl *sys)
{
    for (int i = 0; i < sys->world_uses.count; i++) {
        const world_use *u = &sys->world_uses.items[i];
        for (int k = 0; k < sys->params.count; k++) {
            const param *q = &sys->params.items[k];
            if (q->type.kind == TY_COMPONENT && q->mode == PARAM_MUT && q->type.decl == u->component) return true;
        }
    }
    return false;
}

bool system_splits(const decl *sys)
{
    if (sys->is_view || !sys->per_entity || sys->writes_text) return false;
    if ((sys->task_access[0] | sys->task_access[1]) & 2u) return false;
    for (int i = 0; i < sys->params.count; i++) {
        const param *p = &sys->params.items[i];
        if (p->type.kind == TY_SINGLETON && p->mode == PARAM_MUT) return false;
    }
    // World.Set writes any entity's component; reading through World or a
    // query a component the system changes would read rows other threads change
    for (int i = 0; i < sys->world_uses.count; i++) {
        if (sys->world_uses.items[i].writes) return false;
    }
    return !world_reads_own_writes(sys);
}

// The components and singletons two systems both use, where at least one of
// them writes. Components only count if the systems can meet the same entity.
// What's the whole world's counts whatever entities they meet: changing its
// heap (reading it alongside is fine), and the entity IDs spawns hand out,
// which a system hands out as it runs unless it splits (its spawns get theirs
// once it's done, in the tick's order, without waiting). C is trusted, so
// calling it makes no system wait.
// The component or singleton a parameter reads or writes, or NULL.
static const decl *accessed(const param *p)
{
    if (p->mode != PARAM_READ && p->mode != PARAM_MUT) return NULL;
    return p->type.kind == TY_COMPONENT || p->type.kind == TY_SINGLETON ? p->type.decl : NULL;
}

// What a system reads or writes: a parameter's component or singleton, the
// entities' it runs for, or a component it reaches through its World
// (world.Get<Health>(e), Has and Set), which is every entity's.
typedef struct access {
    const decl *data;
    bool writes;
    bool every;
} access;

typedef struct access_list {
    access *items;
    int count;
} access_list;

// In parameter order, global accesses at the first World or query parameter.
// A system that takes its entities' input, or their devices, reads whose each
// is: its Owner, after its parameters, unless one of them is the Owner.
static access_list accesses_of(const program *prog, const decl *sys)
{
    access_list list = {arena_alloc(sizeof(access) * (size_t)(sys->params.count + sys->world_uses.count + 4)), 0};
    bool any_entity = false;
    bool by_owner = false, takes_owner = false;
    for (int i = 0; i < sys->params.count; i++) {
        const param *p = &sys->params.items[i];
        for (int k = 0; !any_entity && (p->type.kind == TY_WORLD || p->type.kind == TY_QUERY) && k < sys->world_uses.count; k++) {
            const world_use *u = &sys->world_uses.items[k];
            list.items[list.count++] = (access){u->component, u->writes, true};
        }
        any_entity |= p->type.kind == TY_WORLD || p->type.kind == TY_QUERY;
        by_owner |= p->type.kind == TY_INPUT || p->type.kind == TY_RECORD;
        const decl *data = accessed(p);
        if (data) list.items[list.count++] = (access){data, p->mode == PARAM_MUT, false};
        takes_owner |= data && data == prog->owner;
    }
    if (by_owner && sys->per_entity && !takes_owner && prog->owner) list.items[list.count++] = (access){prog->owner, false, false};
    for (int side = 0; side < 2; side++) {
        if (sys->task_access[side] && prog->task_slots[side])
            list.items[list.count++] = (access){prog->task_slots[side], (sys->task_access[side] & 2u) != 0, true};
    }
    return list;
}

// Whether there are any; with a `w`, it lists them there, and without one it
// stops at the first.
static bool find_conflicts(const decl *earlier, const decl *later, const access_list *earlier_uses,
                           const access_list *later_uses, const bool same_entities, system_wait *w)
{
    bool any = false;
    // Starting a task runs its code until it first waits, which can spawn and
    // change text, and the match keeps its tasks in the order they start.
    if (earlier->starts_tasks || later->starts_tasks) {
        if ((earlier->writes_text || earlier->spawns) && (later->writes_text || later->spawns)) any = add_conflict(w, CONFLICT_TASKS);
    } else {
        if (earlier->writes_text && later->writes_text) any = add_conflict(w, CONFLICT_TEXT);
        if (earlier->spawns && later->spawns && !system_splits(later)) any = add_conflict(w, CONFLICT_SPAWN);
    }
    if (any && !w) return true;
    for (int i = 0; i < earlier_uses->count; i++) {
        const access *a = &earlier_uses->items[i];
        for (int k = 0; k < later_uses->count; k++) {
            const access *b = &later_uses->items[k];
            if (b->data != a->data) continue;
            // A component only counts where both can meet the same entity; the World reaches every entity
            if (a->data->kind == DECL_COMPONENT && !same_entities && !a->every && !b->every) continue;
            if (!a->writes && !b->writes) continue;
            if (!w) return true;
            const conflict c = {(decl *)a->data, a->writes && b->writes ? CONFLICT_BOTH_WRITE
                                                 : a->writes            ? CONFLICT_EARLIER_WRITES
                                                                        : CONFLICT_EARLIER_READS};
            vec_push(w->conflicts, c);
            any = true;
        }
    }
    return any;
}

static bool contains(const decl *const *items, const int count, const decl *d)
{
    for (int i = 0; i < count; i++) {
        if (items[i] == d) return true;
    }
    return false;
}

// Systems as bits, by where they are in the tick: 64 a word.
static bool has_bit(const uint64_t *bits, const int i)
{
    return (bits[i >> 6] >> (i & 63)) & 1u;
}

// The lowest system of a word that has one, `word` words in.
static int first_bit(const size_t word, const uint64_t bits)
{
    return (int)(word * 64u) + __builtin_ctzll(bits);
}

// A game has thousands of systems, so this keeps who runs after whom as bits
// and only spells out the waits it lists: pair by pair, whether they conflict
// is a yes or no, and what each wait is over is worked out for those.
void analyze_parallelism(program *prog)
{
    const int n = prog->systems.count;
    if (n == 0) return;
    const size_t words = ((size_t)n + 63u) / 64u;
    const size_t row_size = sizeof(uint64_t) * words;
    arch_set *sets = arena_alloc(sizeof(arch_set) * (size_t)n);
    access_list *uses = arena_alloc(sizeof(access_list) * (size_t)n);
    // before + j * words: the systems j runs after, directly or through others.
    uint64_t *before = arena_alloc(row_size * (size_t)n);
    uint64_t *waits = arena_alloc(row_size);                // The system's own: those it conflicts with or is ordered after
    uint64_t *indirect = arena_alloc(row_size);             // ...of which, those another of them already waits for
    int *through = arena_alloc(sizeof(int) * (size_t)n);    // ...and for each of those, the first that does
    int *followers = arena_alloc(sizeof(int) * (size_t)n);  // How many systems run after each
    memset(before, 0, row_size * (size_t)n);
    memset(followers, 0, sizeof(int) * (size_t)n);
    for (int i = 0; i < n; i++) sets[i] = archetypes_of(prog, prog->systems.items[i]);
    for (int i = 0; i < n; i++) uses[i] = accesses_of(prog, prog->systems.items[i]);

    // The systems are in tick order, so everything a system waits for comes first.
    for (int j = 0; j < n; j++) {
        decl *sys = prog->systems.items[j];
        uint64_t *row = before + (size_t)j * words;
        const size_t used = ((size_t)j + 63u) / 64u; // The words earlier systems are in
        memset(waits, 0, row_size);
        memset(indirect, 0, row_size);
        sys->stage = 1;
        sys->waits_unlisted = 0;
        for (int i = 0; i < j; i++) {
            const decl *earlier = prog->systems.items[i];
            if (!contains((const decl *const *)sys->after.items, sys->after.count, earlier)
                && !find_conflicts(earlier, sys, &uses[i], &uses[j], can_meet(&sets[i], &sets[j]), NULL)) {
                continue;
            }
            waits[i >> 6] |= (uint64_t)1 << (i & 63);
            if (earlier->stage + 1 > sys->stage) sys->stage = earlier->stage + 1;
        }
        // It runs after everything those run after. A wait goes through
        // another when that other system already waits for it: the first of
        // them in the tick that does.
        for (size_t w = 0; w < used; w++) {
            for (uint64_t left = waits[w]; left; left &= left - 1u) {
                const int i = first_bit(w, left);
                const uint64_t *theirs = before + (size_t)i * words;
                for (size_t k = 0; k <= w; k++) { // What it runs after comes before it
                    row[k] |= theirs[k];
                    uint64_t found = theirs[k] & waits[k] & ~indirect[k];
                    indirect[k] |= found;
                    for (; found; found &= found - 1u) through[first_bit(k, found)] = i;
                }
            }
        }
        for (size_t w = 0; w < used; w++) {
            row[w] |= waits[w];
            for (uint64_t left = row[w]; left; left &= left - 1u) followers[first_bit(w, left)]++;
        }
        // The waits it lists: every one that's its own, and the first few through others
        int listed = 0;
        for (size_t w = 0; w < used; w++) {
            for (uint64_t left = waits[w]; left; left &= left - 1u) {
                const int i = first_bit(w, left);
                const bool goes_through = has_bit(indirect, i);
                if (goes_through && listed++ >= SCHEDULE_LISTED) {
                    sys->waits_unlisted++;
                    continue;
                }
                decl *earlier = prog->systems.items[i];
                system_wait wait = {0};
                wait.on = earlier;
                find_conflicts(earlier, sys, &uses[i], &uses[j], can_meet(&sets[i], &sets[j]), &wait);
                wait.ordered = contains((const decl *const *)sys->after.items, sys->after.count, earlier);
                wait.through = goes_through ? prog->systems.items[through[i]] : NULL;
                vec_push(sys->waits, wait);
            }
        }
    }

    // A system runs alongside those it neither waits for nor is waited for by
    for (int j = 0; j < n; j++) {
        decl *sys = prog->systems.items[j];
        const uint64_t *row = before + (size_t)j * words;
        int earlier = 0;
        for (size_t w = 0; w < words; w++) earlier += __builtin_popcountll(row[w]);
        sys->alongside_count = n - 1 - earlier - followers[j];
        const int list = sys->alongside_count < SCHEDULE_LISTED ? sys->alongside_count : SCHEDULE_LISTED;
        for (int k = 0; k < n && sys->alongside.count < list; k++) {
            if (k == j || (k < j ? has_bit(row, k) : has_bit(before + (size_t)k * words, j))) continue;
            vec_push(sys->alongside, prog->systems.items[k]);
        }
    }
}

// ---------------------------------------------------------------------------
// Text

// A declaration's name as code in `from`'s namespace writes it: short in the
// same namespace, qualified elsewhere. With no `from`, always qualified. One
// of several that share the name goes with its parameters' types, as code
// names it: Move(Position, with Player).
void put_decl_name(sb *out, const decl *d, const char *quote, const decl *from)
{
    const bool same_namespace = from && decl_home(d) && decl_home(from) && str_eq(decl_home(d)->ns, decl_home(from)->ns);
    const str name = same_namespace || d->qualified.len == 0 ? d->name : d->qualified;
    sb_printf(out, "%s" STR_FMT "%s%s", quote, STR_ARG(name), d->signature ? d->signature + d->name.len : "", quote);
}

// Whether a declaration directly names another in an ordering attribute.
// Phase-derived after links have no such declaration reference.
static bool has_order_attribute(const decl *sys, const decl *other, const char *name)
{
    for (int i = 0; i < sys->attributes.count; i++) {
        const attribute *a = &sys->attributes.items[i];
        if (!str_eq_c(a->name, name)) continue;
        for (int k = 0; k < a->args.count; k++) {
            if (a->args.items[k].decl == other) return true;
        }
    }
    return false;
}

void describe_wait(const decl *sys, const system_wait *w, const char *quote, sb *out)
{
    static const char *const templates[] = {
        [CONFLICT_BOTH_WRITE] = "both write ",
        [CONFLICT_EARLIER_READS] = "it reads ",
        [CONFLICT_EARLIER_WRITES] = "it writes ",
    };
    static const char *const endings[] = {
        [CONFLICT_BOTH_WRITE] = "",
        [CONFLICT_EARLIER_READS] = ", which this writes",
        [CONFLICT_EARLIER_WRITES] = ", which this reads",
    };
    // What the whole world shares, said once each
    static const char *const shared[] = {
        [CONFLICT_TEXT] = "both change text, lists or grids, which the match keeps in one heap",
        [CONFLICT_SPAWN] = "both spawn, and entities get their IDs in order",
        [CONFLICT_TASKS] = "tasks start in order, and spawn and change text as they start",
    };
    bool first_part = true;
    for (int i = 0; i < w->conflicts.count; i++) {
        const conflict_kind kind = w->conflicts.items[i].kind;
        if (kind < CONFLICT_TEXT) continue;
        sb_put(out, first_part ? "" : "; ");
        sb_put(out, shared[kind]);
        first_part = false;
    }
    for (int kind = CONFLICT_BOTH_WRITE; kind <= CONFLICT_EARLIER_WRITES; kind++) {
        int count = 0;
        for (int i = 0; i < w->conflicts.count; i++) count += (int)w->conflicts.items[i].kind == kind;
        if (count == 0) continue;
        sb_put(out, first_part ? "" : "; ");
        sb_put(out, templates[kind]);
        int written = 0;
        for (int i = 0; i < w->conflicts.count; i++) {
            if ((int)w->conflicts.items[i].kind != kind) continue;
            sb_put(out, written == 0 ? "" : written == count - 1 ? " and " : ", ");
            put_decl_name(out, w->conflicts.items[i].data, quote, sys);
            written++;
        }
        sb_put(out, endings[kind]);
        first_part = false;
    }
    if (w->conflicts.count == 0) {
        const bool after = has_order_attribute(sys, w->on, "After");
        if (after || has_order_attribute(w->on, sys, "Before")) {
            sb_printf(out, "%s%s", after ? "this has " : "it has ", quote);
            sb_put(out, after ? "[After(" : "[Before(");
            put_decl_name(out, after ? w->on : sys, "", after ? sys : w->on);
            sb_printf(out, ")]%s", quote);
        } else {
            sb_put(out, "the stage order requires this to run after ");
            put_decl_name(out, w->on, quote, sys);
        }
    }
}

// The components or singletons a system reads (writes = false) or writes.
static void put_access(sb *out, const decl *sys, const bool writes)
{
    int count = 0;
    for (int i = 0; i < sys->params.count; i++) {
        const param *p = &sys->params.items[i];
        const decl *data = accessed(p);
        if (!data || p->mode != (writes ? PARAM_MUT : PARAM_READ)) continue;
        sb_put(out, count++ ? ", " : writes ? "         writes " : "         reads ");
        put_decl_name(out, data, "", NULL);
    }
    if (count) sb_put(out, "\n");
}

int world_use_names(const decl *sys, const bool writes, const char *quote, const decl *from, sb *out)
{
    int count = 0;
    for (int i = 0; i < sys->world_uses.count; i++) {
        const world_use *u = &sys->world_uses.items[i];
        count += writes ? u->writes : !u->writes || u->query_read;
    }
    int written = 0;
    for (int i = 0; i < sys->world_uses.count; i++) {
        const world_use *u = &sys->world_uses.items[i];
        if (writes ? !u->writes : u->writes && !u->query_read) continue;
        sb_put(out, written == 0 ? "" : written == count - 1 ? " and " : ", ");
        put_decl_name(out, u->component, quote, from);
        written++;
    }
    return count;
}

// What a system reads or writes beyond its outer entity, through World or queries.
static void put_world_access(sb *out, const decl *sys, const bool writes)
{
    for (int pass = 0; pass < (writes ? 1 : 2); pass++) {
        sb names = {0};
        int count = 0;
        for (int i = 0; i < sys->world_uses.count; i++) {
            const world_use *u = &sys->world_uses.items[i];
            count += pass ? u->query_read : u->through_world && u->writes == writes;
        }
        int written = 0;
        for (int i = 0; i < sys->world_uses.count; i++) {
            const world_use *u = &sys->world_uses.items[i];
            if (pass ? !u->query_read : !u->through_world || u->writes != writes) continue;
            sb_put(&names, written == 0 ? "" : written == count - 1 ? " and " : ", ");
            put_decl_name(&names, u->component, "", NULL);
            written++;
        }
        if (count) sb_printf(out, "         %s any entity's %s, through %s\n", writes ? "writes" : "reads", names.data,
                             pass ? "queries" : "World");
    }
}

void print_schedule(const program *prog, const char *game, sb *out)
{
    int stages = 0;
    int parallel = 0;
    for (int i = 0; i < prog->systems.count; i++) {
        const decl *sys = prog->systems.items[i];
        if (sys->stage > stages) stages = sys->stage;
        if (sys->alongside_count > 0) parallel++;
    }
    sb_printf(out, "%s: %d system%s each tick, %d stage%s deep", game, prog->systems.count,
              prog->systems.count == 1 ? "" : "s", stages, stages == 1 ? "" : "s");
    if (prog->systems.count > 1) {
        if (parallel == 0) sb_put(out, "; none can run at the same time");
        else sb_printf(out, "; %d can run alongside others", parallel);
    }
    sb_put(out, ".\nA system starts once everything it runs after is done.\n\n");

    // Group the display by dependency depth, preserving tick order within a
    // stage. The program's system order still governs execution and effects.
    int *next = arena_alloc(sizeof(int) * ((size_t)stages + 1u));
    memset(next, 0, sizeof(int) * ((size_t)stages + 1u));
    for (int i = 0; i < prog->systems.count; i++) next[prog->systems.items[i]->stage]++;
    int start = 0;
    for (int stage = 1; stage <= stages; stage++) {
        const int count = next[stage];
        next[stage] = start;
        start += count;
    }
    const decl **systems = arena_alloc(sizeof(*systems) * (size_t)prog->systems.count);
    for (int i = 0; i < prog->systems.count; i++) {
        const decl *sys = prog->systems.items[i];
        systems[next[sys->stage]++] = sys;
    }
    for (int i = 0; i < prog->systems.count; i++) {
        const decl *sys = systems[i];
        if (i > 0 && sys->stage != systems[i - 1]->stage) sb_put(out, "\n");
        sb_printf(out, "stage %-2d ", sys->stage);
        put_decl_name(out, sys, "", NULL);
        sb_put(out, sys->per_entity ? "  (per entity" : "  (once");
        if (sys->extern_system) sb_put(out, ", in C"); // Its code isn't Tide's: what it reads and writes is what it declares
        const int loops = parallel_loops(sys->body);
        if (loops) sb_printf(out, ", %d parallel loop%s", loops, loops == 1 ? "" : "s");
        sb_put(out, ")\n");
        put_access(out, sys, false);
        put_access(out, sys, true);
        put_world_access(out, sys, false);
        put_world_access(out, sys, true);
        for (int k = 0; k < sys->waits.count; k++) {
            const system_wait *w = &sys->waits.items[k];
            sb_put(out, "         after ");
            put_decl_name(out, w->on, "", NULL);
            sb_put(out, ": ");
            describe_wait(sys, w, "", out);
            if (w->through) {
                sb_put(out, " (and through ");
                put_decl_name(out, w->through, "", NULL);
                sb_put(out, ")");
            }
            sb_put(out, "\n");
        }
        if (sys->waits_unlisted) sb_printf(out, "         after %d more, through those\n", sys->waits_unlisted);
        sb_put(out, "         alongside ");
        for (int k = 0; k < sys->alongside.count; k++) {
            if (k) sb_put(out, ", ");
            put_decl_name(out, sys->alongside.items[k], "", NULL);
        }
        if (sys->alongside_count > sys->alongside.count) {
            sb_printf(out, " and %d more", sys->alongside_count - sys->alongside.count);
        }
        sb_put(out, sys->alongside_count ? "\n" : "nothing\n");
    }

    if (prog->views.count > 0) {
        sb_put(out, "\nViews, every frame in this order (they only read, so nothing waits):\n");
        for (int i = 0; i < prog->views.count; i++) {
            sb_put(out, "         ");
            put_decl_name(out, prog->views.items[i], "", NULL);
            sb_put(out, "\n");
        }
    }

    bool handled = false, heard = false;
    for (int i = 0; i < prog->events.count; i++) {
        handled |= prog->events.items[i]->handlers.count > 0;
        heard |= prog->events.items[i]->listeners.count > 0;
    }
    if (handled) {
        sb_put(out, "\nEvent handlers, at the end of the tick, as their events are sent:\n");
        for (int i = 0; i < prog->events.count; i++) {
            const decl *event = prog->events.items[i];
            if (event->handlers.count == 0) continue;
            sb_put(out, "         ");
            put_decl_name(out, event, "", NULL);
            sb_put(out, ": ");
            for (int k = 0; k < event->handlers.count; k++) {
                if (k) sb_put(out, ", ");
                put_decl_name(out, event->handlers.items[k], "", NULL);
                if (event->handlers.items[k]->is_async) sb_put(out, " (async)");
            }
            sb_put(out, "\n");
        }
    }
    // A match event's local handlers hear it on each machine, outside the tick
    if (heard) {
        sb_put(out, "\nLocal handlers of the match's events, at the start of a frame, for what the ticks since the last one sent:\n");
        for (int i = 0; i < prog->events.count; i++) {
            const decl *event = prog->events.items[i];
            if (event->listeners.count == 0) continue;
            sb_put(out, "         ");
            put_decl_name(out, event, "", NULL);
            sb_put(out, ": ");
            for (int k = 0; k < event->listeners.count; k++) {
                if (k) sb_put(out, ", ");
                put_decl_name(out, event->listeners.items[k], "", NULL);
                if (event->listeners.items[k]->listens_verified) sb_put(out, " (verified)");
            }
            sb_put(out, "\n");
        }
    }

    // Tasks go on in a pass of their own, once the tick's changes are in
    bool tasks = false;
    for (int i = 0; i < prog->decls.count && !tasks; i++) {
        const decl *d = prog->decls.items[i];
        tasks = d->is_async && (d->kind == DECL_SYSTEM ? !d->is_local : d->runs_in[0]);
    }
    if (tasks) {
        sb_put(out, "\nTasks go on after the tick's changes, in the order they started; what they change applies after.\n");
    }
}
