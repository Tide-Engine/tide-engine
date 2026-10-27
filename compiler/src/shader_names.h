#pragma once

#include "common.h"

// Rename only the emitter's source-derived identifiers, leaving shader entry
// points, renderer bindings, intrinsics and every operation unchanged. The
// optional private map receives alias<TAB>original lines. Result is arena-owned.
const char *shader_minify_names(const char *source, sb *names);
