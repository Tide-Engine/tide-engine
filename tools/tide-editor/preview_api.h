#pragma once

// Compiler-generated preview module ABI. All allocations stay with the module;
// the editor copies its prepared draw list before composing the native UI.
#include <stddef.h>
#include <stdint.h>
#include "tide/draw.h"
#include "tide/gui.h"

#define TIDE_EDITOR_PREVIEW_VERSION 1u
typedef struct tide_editor_preview_api {
    uint32_t version, views;
    void *(*create)(const uint8_t *pack, size_t size);
    const tide_draw_list *(*frame)(void *context, float seconds, tide_rect viewport,
                                   tide_float4x4 camera, tide_float4x4 projection, tide_measure_fn measure);
    void (*destroy)(void *context);
} tide_editor_preview_api;
typedef const tide_editor_preview_api *(*tide_editor_preview_entry)(void);
