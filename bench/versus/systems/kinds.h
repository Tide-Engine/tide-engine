#pragma once

// What both ways of versus_systems share: the four kinds of entity, the
// components each has in order (systems.tide's Start spawns them so), and how
// the digest adds up a component of an entity.

#include <stdint.h>

#include "versus.h"

enum { KINDS = 4, KIND_COMPONENTS = 8, COMPONENTS = 16, STEPS = 4, SYSTEMS = STEPS * COMPONENTS };

static const uint32_t kind_components[KINDS][KIND_COMPONENTS] = {
    {0, 1, 2, 3, 4, 5, 6, 7},
    {4, 5, 6, 7, 8, 9, 10, 11},
    {8, 9, 10, 11, 12, 13, 14, 15},
    {0, 1, 2, 3, 12, 13, 14, 15},
};

typedef struct component_item {
    uint32_t kind, row, at; // `at`: its place among the kind's components
    float a, b;
} component_item;

static inline uint64_t kind_item(const uint32_t kind, const uint32_t row, const uint32_t at, const float a, const float b)
{
    const component_item item = {kind, row, at, a, b};
    return versus_item(&item, sizeof item);
}
