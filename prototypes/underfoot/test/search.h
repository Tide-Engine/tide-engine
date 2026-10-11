#pragma once

// Breadth-first search over test/model.h's rules: the fewest actions that solve
// a board, and the fewest lifts, lays and pushes. tools/solve.c designs puzzles
// with it and test/campaign.c holds each puzzle's stated shortest way to it.
#include <stdio.h>
#include <stdlib.h>

#include "model.h"

typedef struct node {
    uf_state state;
    uint32_t parent;
    uint8_t action;
    uint16_t work;
} node;

static node *nodes;
static uint32_t *table;
static uint32_t node_count, node_limit, table_mask;

static inline void canonical(uf_state *const state)
{
    for (int i = 1; i < UF_CRATES; i++)
        for (int j = i; j > 0 && state->crate[j - 1] > state->crate[j]; j--) {
            const uint8_t swap = state->crate[j];
            state->crate[j] = state->crate[j - 1];
            state->crate[j - 1] = swap;
        }
}

static inline uint64_t hash_state(const uf_state *const state)
{
    uint64_t hash = 1469598103934665603ull;
    const uint8_t *const bytes = (const uint8_t *)state;
    for (size_t i = 0; i < sizeof *state; i++) hash = (hash ^ bytes[i]) * 1099511628211ull;
    return hash ^ (hash >> 29);
}

// Returns the node's index, and whether it is new.
static inline uint32_t intern(const uf_state *const state, bool *const fresh)
{
    uint32_t slot = (uint32_t)hash_state(state) & table_mask;
    for (;; slot = (slot + 1) & table_mask) {
        if (table[slot] == UINT32_MAX) break;
        if (memcmp(&nodes[table[slot]].state, state, sizeof *state) == 0) { *fresh = false; return table[slot]; }
    }
    *fresh = true;
    if (node_count == node_limit) return UINT32_MAX;
    nodes[node_count].state = *state;
    table[slot] = node_count;
    return node_count++;
}

static const char *const marks[2] = {"NESWURDL", "neswurdl"};

static inline void witness(const uint32_t goal, char *const out, const size_t size)
{
    size_t length = 0;
    for (uint32_t at = goal; nodes[at].parent != UINT32_MAX; at = nodes[at].parent) length++;
    if (length >= size) { out[0] = 0; return; }
    out[length] = 0;
    for (uint32_t at = goal; nodes[at].parent != UINT32_MAX; at = nodes[at].parent)
        out[--length] = marks[nodes[at].action >> 3][nodes[at].action & 7];
}

typedef struct result { int moves, work, states; bool solved, complete; char path[2048]; } result;

// Breadth first: the first solved position is the fewest actions. `work` is a
// second pass over the same graph, the fewest lifts, lays and pushes, since
// walking is free to a player who has seen what to do.
static inline result solve(const uf_layout *const layout, const uf_state *const start, const bool whole)
{
    result found = {0};
    node_count = 0;
    memset(table, 0xFF, sizeof(uint32_t) * ((size_t)table_mask + 1));
    bool fresh;
    uf_state first = *start;
    canonical(&first);
    intern(&first, &fresh);
    nodes[0].parent = UINT32_MAX;
    found.complete = true;
    for (uint32_t head = 0; head < node_count; head++) {
        const uf_state here = nodes[head].state;
        if (uf_solved(layout, &here)) {
            if (!found.solved) {
                found.solved = true;
                witness(head, found.path, sizeof found.path);
                for (uint32_t at = head; nodes[at].parent != UINT32_MAX; at = nodes[at].parent) found.moves++;
                if (!whole) break;
            }
            continue;
        }
        for (int actor = 0; actor < layout->actors; actor++)
            for (int action = 0; action < 8; action++) {
                uf_state next = here;
                if (!uf_act(layout, &next, actor, action)) continue;
                canonical(&next);
                const uint32_t index = intern(&next, &fresh);
                if (index == UINT32_MAX) { found.complete = false; goto done; }
                if (fresh) { nodes[index].parent = head; nodes[index].action = (uint8_t)(actor << 3 | action); }
            }
    }
done:
    found.states = (int)node_count;
    return found;
}

// Fewest lifts, lays and pushes: walking costs nothing (0-1 search).
static inline int least_work(const uf_layout *const layout, const uf_state *const start)
{
    node_count = 0;
    memset(table, 0xFF, sizeof(uint32_t) * ((size_t)table_mask + 1));
    bool fresh;
    uf_state first = *start;
    canonical(&first);
    intern(&first, &fresh);
    nodes[0].work = 0;
    uint32_t *const deque = malloc(sizeof(uint32_t) * 2 * ((size_t)node_limit + 1));
    if (!deque) return -1;
    size_t front = node_limit, back = node_limit;
    deque[back++] = 0;
    int best = -1;
    while (front < back) {
        const uint32_t head = deque[front++];
        const uf_state here = nodes[head].state;
        const int work = nodes[head].work;
        if (uf_solved(layout, &here)) { best = work; break; }
        for (int actor = 0; actor < layout->actors; actor++)
            for (int action = 0; action < 8; action++) {
                uf_state next = here;
                if (!uf_act(layout, &next, actor, action)) continue;
                int cost = action >= 4;
                if (!cost) {
                    uf_state a = here, b = next;
                    a.actor[actor] = b.actor[actor] = 0;
                    cost = memcmp(&a, &b, sizeof a) != 0;
                }
                canonical(&next);
                const uint32_t index = intern(&next, &fresh);
                if (index == UINT32_MAX) { free(deque); return -2; }
                if (!fresh && nodes[index].work <= work + cost) continue;
                nodes[index].work = (uint16_t)(work + cost);
                if (cost) { if (back < 2 * (size_t)node_limit) deque[back++] = index; }
                else if (front > 0) deque[--front] = index;
            }
    }
    free(deque);
    return best;
}
