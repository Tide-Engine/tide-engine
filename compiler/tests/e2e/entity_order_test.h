#ifndef TIDE_ENTITY_ORDER_TEST_H
#define TIDE_ENTITY_ORDER_TEST_H

#include <stdlib.h>
#include "tide/entity.h"

// Independent oracle for the language's lexicographic full-key iteration order.
// Physical slots/rows and initializer ordinals do not determine that order.
typedef struct entity_order_value {
    tide_entity key;
    int value;
} entity_order_value;

static int entity_order_compare(const void *left, const void *right)
{
    const tide_entity a = ((const entity_order_value *)left)->key;
    const tide_entity b = ((const entity_order_value *)right)->key;
    if (a.birth != b.birth) return a.birth < b.birth ? -1 : 1;
    return a.lineage < b.lineage ? -1 : a.lineage > b.lineage;
}

#endif
