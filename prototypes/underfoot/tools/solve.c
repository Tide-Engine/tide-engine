// The solver Underfoot's puzzles were designed with, and the search that found
// the hard ones. Not part of the game or its tests: test/campaign.c runs the
// same search (test/search.h) on the boards the game loads.
//
//   clang -O2 -o solve tools/solve.c
//   solve [-w] [-t] [-k] [-a] levels.tide [name-filter]
//
// It reads levels.tide as it is ("// = Name" starts a board, and its rows are
// the quoted strings after it), or a plain file of "= Name" lines and rows;
// test/model.h has the alphabet. For each board it prints the fewest moves
// that solve it and how many positions it searched. -w prints that way as a
// string (NESW walk and URDL use hands; capitals are the first paver), -t tells
// it as a list of lifts, lays, pushes, slides and jumps, -k adds the fewest
// lifts, lays and pushes of any solution, and -a searches every position.
//
//   solve forge w=6 h=5 rounds=4 steps=4000 kinds=.~#oGiI loose=2 crates=2
//               special=6 void=8 minpos=4000 cap=900000 coop=2 seed=1
//
// climbs from random boards towards long solutions and prints what it reaches:
// `kinds` is what a cell may become (repeat a character to weigh it), `loose`,
// `crates`, `special` and `void` cap those pieces, `minpos` is how many
// positions a board must have to count as a puzzle and not a corridor, `coop`
// how many times each paver must use hands, `need=iI` asks that the board has
// no solution with those pieces turned to stone, and `from=file` starts from a
// board instead. What comes out is raw: every campaign puzzle was picked from
// many, checked for what it needs, and placed by hand.
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>

#include "../test/search.h"

// ---- The forge: a hill climb over small boards towards long solutions. ----
// It is how the hard puzzles were found; what it finds is then cut down and
// tidied by hand. Not a level editor, and nothing the game ships.
static uint64_t forge_seed;
static uint32_t roll(const uint32_t below)
{
    forge_seed ^= forge_seed << 13;
    forge_seed ^= forge_seed >> 7;
    forge_seed ^= forge_seed << 17;
    return (uint32_t)((forge_seed >> 11) % below);
}

typedef struct board { char ground[UF_CELLS], thing[UF_CELLS]; int w, h; } board;

static bool board_build(const board *const b, uf_layout *const layout, uf_state *const state)
{
    uf_blank(layout, state);
    int actors = 0, goals = 0, p = 0, q = 0;
    for (int y = 0; y < b->h; y++)
        for (int x = 0; x < b->w; x++) {
            const int at = y * b->w + x;
            const char g = b->ground[at], t = b->thing[at];
            if (t != ' ' && (g == '#' || g == '~')) return false;
            if (t == 'x' && (g == '1' || g == '2' || g == 'G')) return false;
            if (t == '@' || t == '&') actors++;
            if (g == 'G') goals++;
            if (g == 'p' || g == 'P') p++;
            if (g == 'q' || g == 'Q') q++;
            if (!uf_parse_cell(layout, state, (y + 1) * UF_W + x + 1, g, t)) return false;
        }
    return actors == layout->actors && goals == layout->actors && p == q && p <= 1;
}

static void board_print(FILE *const out, const board *const b, const char *const name)
{
    fprintf(out, "= %s\n", name);
    for (int y = -1; y <= b->h; y++) {
        for (int x = -1; x <= b->w; x++) {
            const bool edge = x < 0 || y < 0 || x == b->w || y == b->h;
            fprintf(out, "%c%c", edge ? '#' : b->ground[y * b->w + x], edge ? ' ' : b->thing[y * b->w + x]);
        }
        fprintf(out, "\n");
    }
    fprintf(out, "\n");
}

static int board_count(const board *const b, const char *const set, const bool things)
{
    int count = 0;
    for (int i = 0; i < b->w * b->h; i++)
        if (strchr(set, things ? b->thing[i] : b->ground[i])) count++;
    return count;
}

typedef struct forge_rules { const char *kinds; int loose, crates, cap, special, voids, minpos, coop; const char *need; } forge_rules;

static bool board_allowed(const board *const b, const forge_rules *const rules)
{
    if (board_count(b, "o", false) > rules->loose) return false;
    if (board_count(b, "x", true) > rules->crates) return false;
    if (board_count(b, "~", false) > rules->voids) return false;
    if (board_count(b, "iIaAbBpPqQ12", false) > rules->special) return false;
    if (board_count(b, "12", false) > 2 || board_count(b, "aAbB", false) > 2) return false;
    // A gate with no plate, or a plate with no gate, is noise.
    if ((board_count(b, "1", false) > 0) != (board_count(b, "aA", false) > 0)) return false;
    if ((board_count(b, "2", false) > 0) != (board_count(b, "bB", false) > 0)) return false;
    return true;
}

static int forge_positions;
static int board_score(const board *const b, const forge_rules *const rules, int *const moves, int *const work)
{
    uf_layout layout;
    uf_state state;
    if (!board_allowed(b, rules) || !board_build(b, &layout, &state)) return -1;
    const uint32_t keep = node_limit;
    node_limit = (uint32_t)rules->cap;
    const result found = solve(&layout, &state, false);
    int least = -1;
    if (found.solved) least = least_work(&layout, &state);
    node_limit = keep;
    if (!found.solved || least < 0) return -1;
    *moves = found.moves;
    *work = least;
    forge_positions = found.states;
    // A long way through few positions is a corridor, not a puzzle.
    if (found.states < rules->minpos) return found.states * 100 / (rules->minpos + 1);
    if (rules->need) {
        // The puzzle has to be about its special pieces: with each of them
        // turned to plain stone (loose staying loose) there must be no way left.
        board plain = *b;
        for (int i = 0; i < b->w * b->h; i++) {
            const char *const at = strchr(rules->need, plain.ground[i]);
            if (at) plain.ground[i] = plain.ground[i] >= 'a' ? 'o' : '.';
        }
        uf_layout bare_layout;
        uf_state bare_state;
        board_build(&plain, &bare_layout, &bare_state);
        const uint32_t kept = node_limit;
        node_limit = (uint32_t)rules->cap;
        const result bare = solve(&bare_layout, &bare_state, false);
        node_limit = kept;
        if (bare.solved) return (least * 1000 + found.moves) / 4;
    }
    if (rules->coop) {
        // Both pavers must have work to do, or one of the players only walks.
        int hands[2] = {0, 0};
        for (const char *mark = found.path; *mark; mark++) {
            if (strchr("URDL", *mark)) hands[0]++;
            if (strchr("urdl", *mark)) hands[1]++;
        }
        if (hands[0] < rules->coop || hands[1] < rules->coop) return (least * 1000 + found.moves) / 3;
    }
    return least * 1000 + found.moves;
}

static void board_mutate(board *const b, const forge_rules *const rules)
{
    const int cells = b->w * b->h;
    const int kind = (int)roll(10);
    const int at = (int)roll((uint32_t)cells);
    if (kind < 5) {
        const size_t kinds = strlen(rules->kinds);
        b->ground[at] = rules->kinds[roll((uint32_t)kinds)];
        if (b->ground[at] == 'p' || b->ground[at] == 'q') {
            // Portals come and go in pairs.
            const int other = (int)roll((uint32_t)cells);
            if (other != at && b->thing[other] == ' ' && b->ground[other] != 'G') b->ground[other] = b->ground[at] == 'p' ? 'q' : 'p';
        }
    }
    else if (kind < 7) b->thing[at] = b->thing[at] == 'x' ? ' ' : b->thing[at] == ' ' ? 'x' : b->thing[at];
    else {
        // Carry a paver, a goal or a crate somewhere else.
        const int from = (int)roll((uint32_t)cells);
        if (b->thing[from] != ' ' && b->thing[at] == ' ') { b->thing[at] = b->thing[from]; b->thing[from] = ' '; }
        else if (b->ground[from] == 'G' && b->ground[at] != 'G') { b->ground[from] = b->ground[at]; b->ground[at] = 'G'; }
    }
}

// Take out whatever the solution does not need: each cell is tried as something
// plainer, and stays that way if the puzzle is no easier for it.
static void board_trim(board *const b, const forge_rules *const rules, int score)
{
    static const char *const plainer[] = {"o.", "i.", "iI", "p.", "q.", "a.", "aA", "b.", "bB", "I.", "A.", "B.", "1.", "2.", "~#", ".#", "o#", NULL};
    bool changed = true;
    while (changed) {
        changed = false;
        for (int at = 0; at < b->w * b->h; at++) {
            int moves, work;
            if (b->thing[at] == 'x') {
                b->thing[at] = ' ';
                const int now = board_score(b, rules, &moves, &work);
                if (now >= score) { score = now; changed = true; } else b->thing[at] = 'x';
            }
            for (int i = 0; plainer[i]; i++) {
                if (b->ground[at] != plainer[i][0]) continue;
                b->ground[at] = plainer[i][1];
                const int now = board_score(b, rules, &moves, &work);
                if (now >= score) { score = now; changed = true; break; }
                b->ground[at] = plainer[i][0];
            }
        }
    }
}

static bool board_read(board *b, const char *file);
static int forge(const int w, const int h, const int rounds, const int steps, const forge_rules *const rules, const int actors, const char *const from, FILE *const out)
{
    for (int round = 0; round < rounds; round++) {
        board b = {.w = w, .h = h};
        int moves = 0, work = 0, score = -1;
        // A board to start from that can be solved at all; most random ones can't.
        for (int attempt = 0; attempt < 400 && score < 0; attempt++) {
        for (int i = 0; i < w * h; i++) {
            const uint32_t r = roll(100);
            b.ground[i] = r < 50 ? '.' : r < 78 ? '~' : r < 90 ? '#' : 'o';
            b.thing[i] = ' ';
        }
        for (int i = 0; i < actors; i++) {
            b.thing[roll((uint32_t)(w * h))] = i ? '&' : '@';
            b.ground[roll((uint32_t)(w * h))] = 'G';
        }
        for (int i = 0; i < w * h; i++)
            if (b.thing[i] != ' ' && (b.ground[i] == '#' || b.ground[i] == '~')) b.ground[i] = '.';
        for (int i = 0; i < w * h; i++) {
            if (b.ground[i] == '~' && board_count(&b, "~", false) > rules->voids) b.ground[i] = '.';
            if (b.ground[i] == 'o' && board_count(&b, "o", false) > rules->loose) b.ground[i] = '.';
        }
        // Start with the special pieces the rules allow, so the climb has them to keep.
        for (const char *want = "a1pqiib2"; *want; want++) {
            const int at = (int)roll((uint32_t)(w * h));
            if (strchr(rules->kinds, *want) && b.thing[at] == ' ' && !strchr("G1apq", b.ground[at])) b.ground[at] = *want;
        }
        if (from && !board_read(&b, from)) return 2;
        score = board_score(&b, rules, &moves, &work);
        }
        for (int step = 0; step < steps; step++) {
            board next = b;
            const int changes = 1 + (int)roll(3);
            for (int i = 0; i < changes; i++) board_mutate(&next, rules);
            int next_moves = 0, next_work = 0;
            const int now = board_score(&next, rules, &next_moves, &next_work);
            if (now < 0 && score >= 0) continue;
            if (now >= score) { b = next; score = now; moves = next_moves; work = next_work; }
        }
        if (score < 0) continue;
        board_trim(&b, rules, score);
        score = board_score(&b, rules, &moves, &work);
        char name[96];
        snprintf(name, sizeof name, "forged work %d moves %d positions %d round %d", work, moves, forge_positions, round);
        board_print(out, &b, name);
        fflush(out);
        fprintf(stderr, "round %d: work %d moves %d positions %d\n", round, work, moves, forge_positions);
    }
    return 0;
}

// The first level of a file, without its border of walls, as a place to start.
static bool board_read(board *const b, const char *const file)
{
    FILE *const in = fopen(file, "r");
    if (!in) return false;
    char line[256];
    int row = -1;
    while (fgets(line, sizeof line, in)) {
        if (line[0] == '=') { if (row >= 0) break; row = 0; continue; }
        if (row < 0 || line[0] < ' ') continue;
        if (row >= 1 && row <= b->h)
            for (int x = 0; x < b->w; x++) {
                b->ground[(row - 1) * b->w + x] = line[(x + 1) * 2];
                const char thing = line[(x + 1) * 2 + 1];
                b->thing[(row - 1) * b->w + x] = thing < ' ' ? ' ' : thing;
            }
        row++;
    }
    fclose(in);
    return row > b->h;
}

// The solution as a designer reads it: the walking left out, and each lift,
// lay, push, slide and jump said in a line, with where it happened.
static void narrate(const uf_layout *const layout, const uf_state *const start, const char *const path)
{
    static const char *const grounds[] = {"nothing", "stone", "ice", "portal P", "portal Q", "plate A", "plate B", "rubble", "wall"};
    uf_state state = *start;
    for (const char *mark = path; *mark; mark++) {
        const int actor = *mark >= 'a';
        const char *const at = strchr(marks[actor], *mark);
        const int action = (int)(at - marks[actor]);
        const uf_state before = state;
        uf_act(layout, &state, actor, action);
        const int from = before.actor[actor], to = state.actor[actor];
        const char who = actor ? '&' : '@';
        if (action >= 4) {
            const int cell = uf_step(from, action - 4);
            if (before.held[actor] == UF_VOID) printf("      %c at %d,%d lifts %s from %d,%d\n", who, from % UF_W, from / UF_W, grounds[state.held[actor]], cell % UF_W, cell / UF_W);
            else printf("      %c at %d,%d lays %s at %d,%d\n", who, from % UF_W, from / UF_W, grounds[before.held[actor]], cell % UF_W, cell / UF_W);
            continue;
        }
        const int stepped = uf_step(from, action);
        for (int i = 0; i < UF_CRATES; i++) {
            if (before.crate[i] == state.crate[i]) continue;
            if (state.crate[i] == UF_NONE) printf("      %c pushes the crate at %d,%d %c: it sinks\n", who, before.crate[i] % UF_W, before.crate[i] / UF_W, "NESW"[action]);
            else printf("      %c pushes the crate at %d,%d %c to %d,%d (%s)\n", who, before.crate[i] % UF_W, before.crate[i] / UF_W, "NESW"[action],
                state.crate[i] % UF_W, state.crate[i] / UF_W, grounds[state.ground[state.crate[i]]]);
        }
        if (to != stepped) printf("      %c steps %c from %d,%d and %s to %d,%d\n", who, "NESW"[action], from % UF_W, from / UF_W,
            before.ground[stepped] == UF_ICE ? "slides" : "jumps", to % UF_W, to / UF_W);
        if (uf_pressed(layout, &before) != uf_pressed(layout, &state)) printf("      (plates now %d, %c at %d,%d)\n", uf_pressed(layout, &state), who, to % UF_W, to / UF_W);
    }
    printf("      ends with @ at %d,%d and & at %d,%d\n", state.actor[0] % UF_W, state.actor[0] / UF_W, state.actor[1] % UF_W, state.actor[1] / UF_W);
}

int main(const int argc, char **const argv)
{
    if (argc > 1 && strcmp(argv[1], "forge") == 0) {
        // solve forge w=6 h=5 rounds=8 steps=3000 kinds=.~#oxG loose=1 crates=2 special=3
        //             cap=300000 seed=1 actors=2 void=6 minpos=2000 from=level.txt
        forge_rules rules = {.kinds = ".~#oxG", .loose = 1, .crates = 1, .special = 0, .cap = 300000, .voids = 99, .minpos = 0};
        int w = 5, h = 4, rounds = 4, steps = 1000, seed = 1, actors = 2;
        const char *from = NULL;
        for (int i = 2; i < argc; i++) {
            const char *const eq = strchr(argv[i], '=');
            if (!eq) continue;
            const char *const value = eq + 1;
            #define ARG(name) (strncmp(argv[i], name "=", sizeof name) == 0)
            if (ARG("w")) w = atoi(value); else if (ARG("h")) h = atoi(value);
            else if (ARG("rounds")) rounds = atoi(value); else if (ARG("steps")) steps = atoi(value);
            else if (ARG("kinds")) rules.kinds = value; else if (ARG("loose")) rules.loose = atoi(value);
            else if (ARG("crates")) rules.crates = atoi(value); else if (ARG("special")) rules.special = atoi(value);
            else if (ARG("cap")) rules.cap = atoi(value); else if (ARG("seed")) seed = atoi(value);
            else if (ARG("actors")) actors = atoi(value); else if (ARG("void")) rules.voids = atoi(value);
            else if (ARG("minpos")) rules.minpos = atoi(value); else if (ARG("from")) from = value;
            else if (ARG("coop")) rules.coop = atoi(value);
            else if (ARG("need")) rules.need = value;
            #undef ARG
        }
        forge_seed = 0x9E3779B97F4A7C15ull * (uint64_t)(seed + 1);
        table_mask = (1u << 22) - 1;
        node_limit = (uint32_t)rules.cap;
        nodes = malloc(sizeof(node) * node_limit);
        table = malloc(sizeof(uint32_t) * ((size_t)table_mask + 1));
        if (!nodes || !table) return 2;
        return forge(w, h, rounds, steps, &rules, actors, from, stdout);
    }
    const char *file = NULL, *filter = NULL;
    bool show = false, whole = false, work = false, tell = false;
    node_limit = 6000000;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-w") == 0) show = true;
        else if (strcmp(argv[i], "-t") == 0) tell = true;
        else if (strcmp(argv[i], "-a") == 0) whole = true;
        else if (strcmp(argv[i], "-k") == 0) work = true;
        else if (!file) file = argv[i];
        else filter = argv[i];
    }
    if (!file) { fprintf(stderr, "usage: solve [-w] [-t] [-k] [-a] levels.tide [name-filter], or solve forge ... (see the source)\n"); return 2; }
    FILE *const in = fopen(file, "r");
    if (!in) { fprintf(stderr, "cannot read %s\n", file); return 2; }
    table_mask = (1u << 24) - 1;
    nodes = malloc(sizeof(node) * node_limit);
    table = malloc(sizeof(uint32_t) * ((size_t)table_mask + 1));
    if (!nodes || !table) return 2;

    char line[256], name[128] = "";
    uf_layout layout;
    uf_state state;
    int row = -1;
    bool bad = false;
    for (;;) {
        char raw[256];
        const bool more = fgets(raw, sizeof raw, in) != NULL;
        if (more) {
            // levels.tide is read as it is: a "// = Name" comment starts a
            // board, and its rows are the quoted strings that follow.
            const char *text = raw + strspn(raw, " \t");
            if (strncmp(text, "// =", 4) == 0) text += 3;
            else if (*text == '"') {
                text++;
                char *const end = strchr(text, '"');
                if (end) *end = 0;
            }
            else if (*text != '=' && *text != '#') text = "\n";
            snprintf(line, sizeof line, "%s", text);
        }
        const bool header = more && line[0] == '=';
        if ((!more || header) && row > 0 && (!filter || strstr(name, filter))) {
            if (bad) printf("%-22s  malformed\n", name);
            else {
                const result found = solve(&layout, &state, whole);
                printf("%-22s  %s  moves %3d  positions %8d%s", name, found.solved ? "solved" : "NO WAY", found.moves,
                    found.states, found.complete ? "" : " (cut off)");
                if (work && found.solved) printf("  work %d", least_work(&layout, &state));
                printf("\n");
                if (show && found.solved) printf("    %s\n", found.path);
                if (tell && found.solved) narrate(&layout, &state, found.path);
            }
        }
        if (!more) break;
        if (header) {
            size_t length = strlen(line);
            while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r' || line[length - 1] == ' ')) line[--length] = 0;
            snprintf(name, sizeof name, "%s", line + (line[1] == ' ' ? 2 : 1));
            uf_blank(&layout, &state);
            row = 0;
            bad = false;
            continue;
        }
        if (row < 0 || line[0] == '\n' || line[0] == '\r' || line[0] == ';') continue;
        size_t length = strlen(line);
        while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r')) line[--length] = 0;
        if (row >= UF_H || length > UF_W * 2) { bad = true; continue; }
        for (size_t x = 0; x * 2 < length; x++) {
            const char thing = x * 2 + 1 < length ? line[x * 2 + 1] : ' ';
            if (!uf_parse_cell(&layout, &state, row * UF_W + (int)x, line[x * 2], thing)) bad = true;
        }
        row++;
    }
    fclose(in);
    return 0;
}
