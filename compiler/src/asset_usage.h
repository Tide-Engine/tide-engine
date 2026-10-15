#pragma once

#include "ast.h"

// After checking, retain assets referenced anywhere in emitted Tide code,
// defaults or shaders. This changes only the pack's declaration list, in its
// original order. Callers opt in only when C cannot refer to otherwise unused
// assets; checking and editor/debug behavior remain unchanged.
void program_strip_unused_assets(program *prog);
// Closed server builds: simulation roots, followed through checked helpers and
// defaults. Uncalled local code remains compilable but does not load its assets.
void program_strip_server_assets(program *prog);
