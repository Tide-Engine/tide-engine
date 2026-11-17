#include "tide/inspect.h"

#include <math.h>
#include <string.h>

// A field an editor can change: numbers and vectors of them, a Color (four floats), an enum's member, a bool. Never
// what's plain otherwise (text, lists, maps, entities: handles, which a write could point anywhere).
static bool editable(const tide_layout_field *f)
{
    switch (f->kind) {
    case TIDE_LAYOUT_INT:
    case TIDE_LAYOUT_FLOAT:
    case TIDE_LAYOUT_LONG:
    case TIDE_LAYOUT_BYTE:
    case TIDE_LAYOUT_DOUBLE:
    case TIDE_LAYOUT_ENUM:
        return true;
    case TIDE_LAYOUT_PLAIN: // Of those the layout carries over as they are, a bool and a Color
        return f->type && ((f->size == 1 && strcmp(f->type, "bool") == 0) || (f->size == 16 && strcmp(f->type, "Color") == 0));
    default:
        return false;
    }
}

const tide_layout_field *tide_inspect_field(const tide_layout *layout, int32_t type, uint32_t offset, const uint32_t size)
{
    if (!layout || size == 0 || size > TIDE_INSPECT_BYTES) return NULL;
    // Down through the structs the place is in, as deep as types can go without one holding itself
    for (uint32_t depth = 0; depth < 64; depth++) {
        if (type < 0 || (uint32_t)type >= layout->type_count) return NULL;
        const tide_layout_type *t = &layout->types[type];
        if (offset > t->size || size > t->size - offset) return NULL;
        const tide_layout_field *in = NULL;
        for (uint32_t i = 0; i < t->field_count; i++) {
            const tide_layout_field *f = &t->fields[i];
            if (offset >= f->offset && offset - f->offset < f->size) {
                in = f;
                break;
            }
        }
        if (!in || in->hidden) return NULL; // The engine's own, never a tool's
        if (in->kind == TIDE_LAYOUT_STRUCT) {
            offset -= in->offset;
            type = in->decl;
            continue;
        }
        return in->offset == offset && in->size == size && editable(in) ? in : NULL;
    }
    return NULL;
}

bool tide_inspect_writable(const tide_layout *layout, const int32_t type, const uint32_t offset, const void *bytes,
                           const uint32_t size)
{
    const tide_layout_field *f = tide_inspect_field(layout, type, offset, size);
    if (!f || !bytes) return false;
    const uint8_t *b = bytes;
    switch (f->kind) {
    case TIDE_LAYOUT_PLAIN:
        if (size == 1) return b[0] <= 1u; // A bool
        // fallthrough: a Color, four floats
    case TIDE_LAYOUT_FLOAT:
        for (uint32_t i = 0; i + 4u <= size; i += 4u) {
            float v;
            memcpy(&v, b + i, 4);
            if (!isfinite(v)) return false;
        }
        return true;
    case TIDE_LAYOUT_DOUBLE:
        for (uint32_t i = 0; i + 8u <= size; i += 8u) {
            double v;
            memcpy(&v, b + i, 8);
            if (!isfinite(v)) return false;
        }
        return true;
    case TIDE_LAYOUT_ENUM: {
        if (f->decl < 0 || (uint32_t)f->decl >= layout->enum_count) return false;
        // Stored as a byte or a ushort (unsigned), or an int; little-endian, as every target is
        if (size != 1 && size != 2 && size != 4) return false;
        int32_t v = 0;
        memcpy(&v, b, size);
        const tide_layout_enum *e = &layout->enums[f->decl];
        for (uint32_t i = 0; i < e->member_count; i++) {
            if (e->members[i].value == v) return true;
        }
        return false;
    }
    default:
        return true;
    }
}

static uint32_t u32_at(const uint8_t *p, const uint32_t at)
{
    uint32_t v;
    memcpy(&v, p + at, sizeof v);
    return v;
}

uint32_t tide_packet_entities(const uint8_t *packet, const uint32_t size)
{
    if (!packet || size < TIDE_PACKET_HEADER) return 0;
    const uint32_t n = u32_at(packet, 36);
    return n <= (size - TIDE_PACKET_HEADER) / TIDE_PACKET_ENTITY ? n : 0;
}

tide_entity tide_packet_entity(const uint8_t *packet, const uint32_t size, const uint32_t i, uint32_t *flags)
{
    if (flags) *flags = 0;
    if (i >= tide_packet_entities(packet, size)) return (tide_entity){0, 0};
    const uint8_t *row = packet + TIDE_PACKET_HEADER + (size_t)i * TIDE_PACKET_ENTITY;
    if (flags) *flags = u32_at(row, 16);
    return (tide_entity){(uint64_t)u32_at(row, 0) | (uint64_t)u32_at(row, 4) << 32,
                         (uint64_t)u32_at(row, 8) | (uint64_t)u32_at(row, 12) << 32};
}

bool tide_packet_next(const uint8_t *packet, const uint32_t size, uint32_t *at, tide_packet_part *out)
{
    if (!packet || size < TIDE_PACKET_HEADER) return false;
    uint64_t place = *at ? *at : TIDE_PACKET_HEADER + (uint64_t)tide_packet_entities(packet, size) * TIDE_PACKET_ENTITY;
    place = (place + 7u) & ~7ull;
    if (place + TIDE_PACKET_SECTION > size) return false;
    const uint32_t kind = u32_at(packet, (uint32_t)place), count = u32_at(packet, (uint32_t)place + 8u);
    const uint32_t stride = u32_at(packet, (uint32_t)place + 12u), bytes = u32_at(packet, (uint32_t)place + 16u);
    const uint64_t data = place + TIDE_PACKET_SECTION;
    if (data + bytes > size) return false;
    const bool has_rows = kind == TIDE_PACKET_COMPONENT || kind == TIDE_PACKET_COMPONENT_LOCAL;
    const uint64_t row_bytes = has_rows ? ((uint64_t)count * 4u + 7u) & ~7ull : 0u;
    if (kind != TIDE_PACKET_STRINGS && kind != TIDE_PACKET_EVENTS && row_bytes + (uint64_t)count * stride > bytes) return false;
    *out = (tide_packet_part){kind, u32_at(packet, (uint32_t)place + 4u), count, stride,
                              has_rows ? (const uint32_t *)(const void *)(packet + data) : NULL, packet + data + row_bytes};
    *at = (uint32_t)(data + bytes);
    return true;
}

const uint8_t *tide_packet_text_of(const uint8_t *packet, const uint32_t size, const uint64_t handle, uint32_t *length)
{
    *length = 0;
    if (!handle) return NULL;
    uint32_t at = 0;
    tide_packet_part part;
    while (tide_packet_next(packet, size, &at, &part)) {
        if (part.kind != TIDE_PACKET_STRINGS) continue;
        const uint8_t *entries = part.values;
        const uint64_t section_end = (uint64_t)at - (uint64_t)(entries - packet);
        if (handle > part.count || (uint64_t)part.count * 8u > section_end) return NULL;
        const uint64_t start = (uint64_t)part.count * 8u + u32_at(entries, (uint32_t)(handle - 1u) * 8u);
        const uint32_t n = u32_at(entries, (uint32_t)(handle - 1u) * 8u + 4u);
        if (start + n > section_end) return NULL;
        *length = n;
        return entries + start;
    }
    return NULL;
}
