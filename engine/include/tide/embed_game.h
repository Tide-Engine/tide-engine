#pragma once

// What a game's generated C gives tide/embed.h (tidec --embed): each frame's
// view packet, its commands from another program, its scenes by name, and
// what of its types its data layout (tide/layout.h) doesn't say.
//
// The view packet is what views would see this frame, as plain bytes another
// language reads in place: no pointers, nothing the compiler pads, every
// count checkable against its size.
//
//   header, 48 bytes:  u32 magic "TIDV", u32 version, u64 the game's hash
//                      (tide_game.hash), u32 size, u32 flags (1: in a
//                      match), u32 tick, u32 verified tick, f32 alpha,
//                      u32 entities, u32 sections, u32 0
//   entities, 20 bytes each: u32 index, u32 generation, u32 flags (1: this
//                      machine's player owns it, 2: a player owns it),
//                      u32 prefab (UINT32_MAX: none), u32 parent (its place
//                      among the entities, UINT32_MAX: a root)
//   sections, each from an 8-byte boundary, a 24-byte header first:
//                      u32 kind, u32 type (the layout's type), u32 count,
//                      u32 stride, u32 bytes (after the header), u32 0
//     COMPONENT, COMPONENT_LOCAL: count u32 rows (places among the
//                      entities), up to 8 bytes, then count values
//     SINGLETON, SINGLETON_LOCAL: one value
//     STRINGS:         count {u32 offset, u32 length} entries, then the bytes
//                      they're in. A text or list field of a value holds an
//                      entry's place + 1 here, 0 for empty: a list's length is
//                      in bytes, its elements side by side.

#include <stdbool.h>
#include <stdint.h>

#include "tide/layout.h"
#include "tide/list.h"

#define TIDE_PACKET_MAGIC 0x56444954u // "TIDV"
#define TIDE_PACKET_VERSION 1u
#define TIDE_PACKET_HEADER 48u
#define TIDE_PACKET_ENTITY 20u
#define TIDE_PACKET_SECTION 24u
#define TIDE_PACKET_NONE 0xFFFFFFFFu

typedef enum tide_packet_kind {
    TIDE_PACKET_COMPONENT = 1,
    TIDE_PACKET_SINGLETON = 2,
    TIDE_PACKET_COMPONENT_LOCAL = 3,
    TIDE_PACKET_SINGLETON_LOCAL = 4,
    TIDE_PACKET_STRINGS = 5,
} tide_packet_kind;

// A packet being written, and what it keeps from frame to frame to write the
// next with: its buffers grow, and stay.
typedef struct tide_packet {
    uint8_t *bytes;
    uint32_t size;
    uint32_t capacity;
    uint32_t sections;
    uint8_t *text; // STRINGS' bytes, and its entries
    uint32_t text_size, text_capacity;
    uint32_t *entries;
    uint32_t entry_count, entry_capacity;
    uint32_t *place;  // Each entity index's place among the packet's entities, as of this frame
    uint32_t place_capacity;
} tide_packet;

// Starts the frame's packet: an empty one with room for the header.
void tide_packet_begin(tide_packet *p);
// `size` more bytes at the end, zeroed, from an 8-byte boundary when `align8`: where they go.
uint8_t *tide_packet_grow(tide_packet *p, uint32_t size, bool align8);
// A section of `count` values of `stride` bytes, and for components their rows, at the end: where its values go.
uint8_t *tide_packet_section(tide_packet *p, tide_packet_kind kind, uint32_t type, uint32_t count, uint32_t stride,
                             uint32_t **rows);
// `size` bytes kept in STRINGS: what a field holds for them (0 for none).
uint32_t tide_packet_text(tide_packet *p, const void *bytes, uint32_t size);
// A list's elements of `element` bytes each, side by side, kept in STRINGS: what its field holds (0 for none).
uint32_t tide_packet_list(tide_packet *p, tide_list list, uint32_t element);
// Room for the places of entity indices below `count`, every one UINT32_MAX.
uint32_t *tide_packet_places(tide_packet *p, uint32_t count);
// Ends the packet: STRINGS, then the header's counts.
void tide_packet_end(tide_packet *p, uint64_t hash, bool in_match, uint32_t tick, uint32_t verified_tick, float alpha,
                     uint32_t entities);
void tide_packet_free(tide_packet *p);

// A field of a type the layout calls plain that another language reads otherwise.
typedef enum tide_embed_field_kind {
    TIDE_EMBED_TEXT = 1,   // A packet's STRINGS entry
    TIDE_EMBED_LIST = 2,   // ...of `element` bytes each, `element_type` the layout's type of them, or -1
    TIDE_EMBED_MAP = 3,    // Not in the packet: 0
    TIDE_EMBED_GRID = 4,   // Not in the packet: 0
    TIDE_EMBED_ENTITY = 5, // tide_entity: index, generation
} tide_embed_field_kind;

typedef struct tide_embed_field {
    int32_t type;      // The layout's type it's a field of
    const char *name;  // ...and its name there
    uint32_t kind;     // tide_embed_field_kind
    uint32_t element;  // A list's element size
    int32_t element_type;
} tide_embed_field;

typedef struct tide_embed_named {
    const char *name;
    int32_t type; // The layout's type
} tide_embed_named;

typedef struct tide_embed_game {
    uint32_t component_count; // The match's components, by the layout's type: what the packet's sections say
    const int32_t *components;
    const int32_t *component_locals; // ...each one's local fields, or -1
    uint32_t singleton_count;
    const int32_t *singletons;
    const int32_t *singleton_locals;
    uint32_t command_count;
    const tide_embed_named *commands;
    uint32_t scene_count; // Scenes a match can start in, by name
    const char *const *scenes;
    uint32_t prefab_count;
    const char *const *prefabs;
    uint32_t field_count;
    const tide_embed_field *fields;
    // Types the layout hasn't, which commands are: the schema numbers them after the layout's, in this order
    uint32_t extra_type_count;
    const tide_layout_type *extra_types;
    // Writes the frame's packet: `world` blended from `previous` by `alpha`, as views see it, with this machine's
    // `local` state. `world` NULL outside a match.
    void (*view)(const void *world, const void *previous, float alpha, const void *local, uint32_t verified_tick,
                 tide_packet *out);
    // Sends a command from another program, as local code's Send: `type` the schema's, to the entity `target` (index,
    // generation; zeros for none), `value` its bytes. False for a type that's no command, a size that isn't its, or one
    // holding text or lists.
    bool (*command)(void *local, int32_t type, uint32_t target_index, uint32_t target_generation, const void *value,
                    uint32_t size);
    // A tide_start for the scene named `scene`: false for none of that name.
    bool (*start)(void *start, const char *scene);
    // The game's schema as JSON (tide_schema_json) into `out`, as much as `capacity` holds with its NUL: its length
    uint32_t (*schema)(char *out, uint32_t capacity);
    // A match component's value with its fields' declared defaults into `out`, its text and lists empty: false for
    // a type that's none of the match's components or a size that isn't its.
    bool (*defaults)(int32_t type, void *out, uint32_t size);
} tide_embed_game;

// The schema another program binds a game by, as JSON: the layout's types with their fields' offsets, what each
// is (a component, a singleton, a component's or singleton's local fields, a command after the layout's, or a
// struct), a component's default value (its bytes as hex), its enums, commands, prefabs and scenes. Into `out`, as much as `capacity` holds with a NUL; returns its
// whole length, so a call with no room says how much to make.
#define TIDE_SCHEMA_ABI 1u
uint32_t tide_schema_json(const tide_layout *layout, const tide_embed_game *embed, uint64_t hash, const char *game,
                          uint32_t tick_rate, char *out, uint32_t capacity);
