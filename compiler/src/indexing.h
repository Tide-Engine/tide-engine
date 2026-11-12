#pragma once

#include "ast.h"

// Indexed loops (notes/indexed-loops.md): what a loop's guard says about the
// rows it can run for. A loop over a query's rows or a list's elements whose
// body starts by skipping rows on a condition gets bounds on some of each
// row's numbers; generated code finds the rows within them through an index
// (tide/index.h) and runs the loop for those alone, in the loop's own order,
// with the guard still checking each one. The results are the plain loop's.

// A bound on a key: a double computed before the loop from values the loop
// doesn't change.
typedef enum bound_op {
    BOUND_LEAF, // `leaf`, an expression of the code around the loop, as a double; `comp` picks a vector's component (-1: a scalar)
    BOUND_ADD,  // a + b
    BOUND_SUB,  // a - b
    BOUND_SQRT, // sqrt(a): a radius from a squared one
    BOUND_MIN,  // the smaller of a and b, or NaN when either is NaN (NaN leaves the bound open)
    BOUND_MAX,  // the larger of a and b, or NaN when either is NaN
} bound_op;

typedef struct bound {
    bound_op op;
    const expr *leaf;
    int comp;
    const struct bound *a;
    const struct bound *b;
} bound;

// One number of each row the index sorts or places rows by.
typedef struct index_key {
    const expr *path; // The row's value, an expression on the loop's variable (or, over a list in a parallel loop, on list[i])
    int comp;         // Its component, for a vector; -1 for a scalar
    type type;        // The number's own type: float, double, an integer, an enum or bool
    const bound *lo;  // Every row the loop runs for has key >= lo (NULL: no lower bound)
    const bound *hi;  // ...and key <= hi (NULL: no upper bound)
} index_key;

typedef struct loop_index {
    index_key keys[3]; // One to three keys: a grid over a vector's components, or one key sorted
    int count;
    const decl *component; // A query's key: the component its field is in
    // A list's: the local (S_VAR) the list is, when nothing in the code writes it after declaring it, so an index
    // made of it holds for as long as the declaration does (index_codegen.inc keeps it across the loop's runs)
    const stmt *list_local;
} loop_index;

// Plans every loop's index once the program is checked, and reports the
// loops written `indexed` that can't have one.
void plan_loop_indexes(program *prog);

// What a loop plan says, for the editor and `tidec --schedule`: "indexed by
// Body.position (x, y)" or "not indexed: <why>". NULL for loops over anything
// but a query or a list.
const char *loop_index_summary(const stmt *loop);

// Calls `fn` for each loop over a query or a list that starts by skipping
// rows, or is written `indexed`, with the code it's in.
typedef void (*loop_visitor)(void *user, const decl *code, const stmt *loop);
void each_guarded_loop(const program *prog, loop_visitor fn, void *user);
// Every parallel loop: a `parallel`, and a grid's foreach whose steps only touch their own cell
void each_parallel_loop(const program *prog, loop_visitor fn, void *user);

// The schedule's section on loops that skip rows (tidec --schedule).
void print_loop_indexes(const program *prog, sb *out);
