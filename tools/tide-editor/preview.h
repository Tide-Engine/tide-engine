#pragma once
#include "document.h"
#include "preview_api.h"
#include "sys.h"

typedef struct editor_preview {
    bool enabled, local;
    uint64_t revision, building_revision, attempted_revision;
    sys_process *builder;
    void *library, *context;
    const tide_editor_preview_api *api;
    char *directory, *building_directory;
    char message[4096];
} editor_preview;

void editor_preview_update(editor_preview *p, const editor_document *d, tide_draw_list *host, int wait_ms);
bool editor_preview_draw(editor_preview *p, tide_draw_list *host, float seconds, tide_rect viewport,
                          tide_float4x4 camera, tide_float4x4 projection, tide_measure_fn measure);
void editor_preview_close(editor_preview *p, tide_draw_list *host);
