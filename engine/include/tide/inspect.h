#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "tide/embed_game.h"
#include "tide/entity.h"
#include "tide/layout.h"

// A dev build's inspector: what a host needs to show the match's and this machine's state, and to change a field of
// it. tidec makes the table (tide_game_inspect(), NULL for a build without dev). The state comes as view packets
// (tide/embed_game.h lays out their bytes): one of the match, with its entities, their components, its singletons and
// the local fields of each, and one of this machine's local world, with its entities, their components and its
// singletons. A field of the match changes through the server (a dev build's TideSetField command), so it changes for
// every machine and the game's Changed handlers hear it as they would the game's own write; one of the local world
// changes here at once.

// The most bytes a field written at once has: a double4
#define TIDE_INSPECT_BYTES 32u

typedef struct tide_inspect_api {
    const tide_layout *layout; // The types the packets' sections name, their fields and enums
    // The match's packet into `match` (NULL outside a match: an empty one), and the local world's into `local`; their
    // text, lists, maps and grids in their STRINGS, as tide/embed_game.h says
    void (*view)(const void *world, const void *local_state, tide_packet *match, tide_packet *local);
    // Sends a change of `size` bytes at `offset` in the value of layout type `type` to the server: a component's of
    // the entity `target`, or a singleton's (target zeros). The server writes it only if that's a field
    // tide_inspect_writable allows, and the entity still has the component.
    void (*set)(void *local_state, tide_entity target, int32_t type, uint32_t offset, const void *bytes, uint32_t size);
    // The same for the local world, written here at once: a local entity's component, a local singleton, or the local
    // fields of the match's entity or singleton (`target` the match's entity, or zeros). False when it isn't written.
    bool (*set_local)(void *local_state, tide_entity target, int32_t type, uint32_t offset, const void *bytes,
                      uint32_t size);
} tide_inspect_api;

// Reading a packet (one written whole, by tide_packet_end): its entities, then its sections one by one, each checked
// against its size.
typedef struct tide_packet_part {
    uint32_t kind;          // tide_packet_kind
    uint32_t type;          // The layout's type
    uint32_t count;
    uint32_t stride;        // A value's bytes
    const uint32_t *rows;   // COMPONENT, COMPONENT_LOCAL: each value's entity, as its place among the entities
    const uint8_t *values;  // `count` values of `stride` bytes; STRINGS: its entries, then their bytes
} tide_packet_part;

// The packet's entities, and entity `i` (its handle, and its flags in `flags`, if that isn't NULL)
uint32_t tide_packet_entities(const uint8_t *packet, uint32_t size);
tide_entity tide_packet_entity(const uint8_t *packet, uint32_t size, uint32_t i, uint32_t *flags);
// Its next section after `*at` (0 for the first) into `out`, moving `*at` past it: false after the last, or for one
// cut short
bool tide_packet_next(const uint8_t *packet, uint32_t size, uint32_t *at, tide_packet_part *out);
// The text or list a value's field holds (the packet's handle of it): its bytes and length, NULL for none (empty)
const uint8_t *tide_packet_text_of(const uint8_t *packet, uint32_t size, uint64_t handle, uint32_t *length);

// The field of layout type `type` at `offset` that a value of `size` bytes would be written over, found through the
// fields of the types in it: NULL unless it's a whole field an editor can change (a number or a vector of them, a
// bool, a Color, an enum), never text, a list, an entity or what holds one.
const tide_layout_field *tide_inspect_field(const tide_layout *layout, int32_t type, uint32_t offset, uint32_t size);

// Whether `bytes` may be written there: such a field, a value it can hold (a bool 0 or 1, an enum one of its members,
// a float or double finite).
bool tide_inspect_writable(const tide_layout *layout, int32_t type, uint32_t offset, const void *bytes, uint32_t size);
