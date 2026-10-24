#pragma once
#include "document.h"

// Writes the project's startScene setting, preserving surrounding source bytes
// and active prefab selection. Requires a saved, conflict-free active document
// and a syntax-valid project settings source. Refuses pending draft recovery,
// ambiguous settings and outside changes. With no settings, exclusively creates
// Startup.tide.
// Updating the active source publishes a fresh saved baseline and clears its
// undo history, as a reload does. Shadowed global prefab names are refused.
bool editor_set_startup(editor_document *d);
