#pragma once

#include "ast.h"

// Pure functions (docs/spec.md, Pure functions; notes/pure-functions.md).
//
// A function or method is pure when its result comes from its arguments alone
// and running it leaves nothing changed: it takes nothing `mut` or `out`, sends,
// spawns and changes nothing, rolls nothing, reads no Time, Session, Devices or
// frame, starts no task, reaches no C, and calls only pure functions. Nothing
// is written for it: the checker works it out (decl.pure), and says why not
// (decl.impure_why, impure_at, impure_through).

// Works every function's and method's purity out, once all bodies are checked
// and what each one does is known through its calls (after mark_c_callers and
// the passes before it), and reports the `cached` functions that can't be.
void infer_purity(program *prog);

// Whether an expression's value is the same wherever in `code`'s body it is
// worked out, while the names it reads are in scope: constants, read-only
// parameters and locals, fields and elements of those, arithmetic, and pure
// calls of them. Never one that reads what the code can change.
bool pure_stable_expr(const decl *code, const expr *e);

// Whether two expressions are the same code: they name the same things and do
// the same with them.
bool pure_same_expr(const expr *a, const expr *b);

// Repeated calls of a pure function with the same arguments, in one body,
// become one call: the first is worked out into a local just before its
// statement, and the ones after it in that block read the local. Changes the
// tree in place, once (program.calls_merged), for code the CPU runs alone;
// returns how many calls it replaced.
int merge_pure_calls(program *prog);

// One line for editors: "pure: its result comes from its arguments alone", or
// "not pure: it rolls with Random (line 12)". NULL for a declaration with no
// code of its own.
const char *purity_summary(const decl *d);
