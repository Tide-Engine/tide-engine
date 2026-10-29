from pathlib import Path

root = Path(__file__).resolve().parents[2]
out = Path(__file__).resolve().parent
(out / 'tide').mkdir(exist_ok=True)
h = (root / 'engine/include/tide/table.h').read_text()
h = h.replace('    tide_page **pages;  // Chunk', '    struct tide_table *mapping; // Logical->physical and physical->logical COW columns, or identity.\n    tide_page **pages;  // Chunk')
at = h.index('// Rows in use in a chunk.')
h = h[:at] + '''// Mapping rows use fixed 2048-row pages of uint32 values, independent of data shape.
static inline uint32_t tide_table_physical_row(const tide_table *t, uint32_t row)
{
    if (!t->mapping) return row;
    const uint32_t *p = tide_page_data(t->mapping->pages[(size_t)(row >> 11) * 2u]);
    return p[row & 2047u];
}
static inline uint32_t tide_table_logical_row(const tide_table *t, uint32_t row)
{
    if (!t->mapping) return row;
    const uint32_t *p = tide_page_data(t->mapping->pages[(size_t)(row >> 11) * 2u + 1u]);
    return p[row & 2047u];
}
uint32_t tide_table_partition(tide_table *t, const tide_columns *c, uint32_t presence, bool need);

''' + h[at:]
h = h.replace('    const uint8_t *rows = tide_table_column(t, c, row >> c->shift, column);\n    return rows + (size_t)(row &', '    const uint32_t physical = tide_table_physical_row(t, row);\n    const uint8_t *rows = tide_table_column(t, c, physical >> c->shift, column);\n    return rows + (size_t)(physical &')
(out / 'tide/table.h').write_text(h)
c = (root / 'engine/src/table.c').read_text()
c = c.replace('#define FIRST_ROOM', 'static const uint32_t mapping_sizes[] = {4u, 4u};\nstatic const tide_columns mapping_columns = {2u, 11u, mapping_sizes};\n\n#define FIRST_ROOM')
c = c.replace('    uint8_t *rows = tide_table_column_mut(t, c, row >> c->shift, column);\n    return rows + (size_t)(row &', '    const uint32_t physical = tide_table_physical_row(t, row);\n    uint8_t *rows = tide_table_column_mut(t, c, physical >> c->shift, column);\n    return rows + (size_t)(physical &')
for fn in ['gather', 'scatter']:
    start = c.index('void tide_table_' + fn)
    end = c.index('\n}', start) + 2
    part = c[start:end].replace('    const uint32_t chunk = row >> c->shift;', '    const uint32_t physical = tide_table_physical_row(t, row);\n    const uint32_t chunk = physical >> c->shift;').replace('(row & (full_room(c)', '(physical & (full_room(c)')
    c = c[:start] + part + c[end:]
c = c.replace('    t->count++;\n    for (uint32_t k', '''    if (t->mapping) {
        const uint32_t added = tide_table_add(t->mapping, &mapping_columns);
        *(uint32_t *)tide_table_cell(t->mapping, &mapping_columns, added, 0u) = added;
        *(uint32_t *)tide_table_cell(t->mapping, &mapping_columns, added, 1u) = added;
    }
    t->count++;
    for (uint32_t k''')
start = c.index('void tide_table_remove(')
end = c.index('uint32_t tide_table_move(', start)
c = c[:start] + '''static void remove_physical(tide_table *t, const tide_columns *c, uint32_t logical)
{
    const uint32_t last = t->count - 1u;
    const uint32_t physical = tide_table_physical_row(t, logical);
    if (physical != last) {
        for (uint32_t k = 0; k < c->count; k++) {
            uint8_t *gap = tide_table_column_mut(t, c, physical >> c->shift, k);
            const uint8_t *tail = tide_table_column(t, c, last >> c->shift, k);
            memcpy(gap + (size_t)(physical & (full_room(c)-1u)) * c->sizes[k],
                   tail + (size_t)(last & (full_room(c)-1u)) * c->sizes[k], c->sizes[k]);
        }
    }
    if (t->mapping) {
        const uint32_t tail_logical = tide_table_logical_row(t, last);
        *(uint32_t *)tide_table_cell(t->mapping, &mapping_columns, tail_logical, 0u) = physical;
        *(uint32_t *)tide_table_cell(t->mapping, &mapping_columns, physical, 1u) = tail_logical;
        if (logical != last) {
            const uint32_t last_physical = tide_table_physical_row(t, last);
            *(uint32_t *)tide_table_cell(t->mapping, &mapping_columns, logical, 0u) = last_physical;
            *(uint32_t *)tide_table_cell(t->mapping, &mapping_columns, last_physical, 1u) = logical;
        }
        tide_table_remove_row(t->mapping, &mapping_columns, last);
    }
    t->count = last;
    if (t->count == (t->chunks - 1u) << c->shift) {
        t->chunks--;
        for (uint32_t k = 0; k < c->count; k++) tide_page_release(t->pages[t->chunks * c->count + k], 1u);
        t->last_room = t->chunks ? full_room(c) : 0u;
    }
}

void tide_table_remove(tide_table *t, const tide_columns *c, uint32_t row, tide_entities *entities, uint32_t archetype)
{
    tide_entity moved = {0};
    const bool moves = row != t->count - 1u;
    if (moves) memcpy(&moved, tide_table_get(t, c, t->count - 1u, 0u), sizeof moved);
    remove_physical(t, c, row);
    if (moves) tide_entity_set_location(entities, moved, (tide_location){archetype, row});
}

void tide_table_remove_row(tide_table *t, const tide_columns *c, uint32_t row)
{
    remove_physical(t, c, row);
}

''' + c[end:]
c = c.replace('    // Its pages first,', '''    if (from->mapping) {
        if (!to->mapping) to->mapping = tide_alloc_zeroed(1u, sizeof *to->mapping);
        tide_table_copy(to->mapping, from->mapping, &mapping_columns);
    } else if (to->mapping) {
        tide_table_free(to->mapping, &mapping_columns);
        free(to->mapping);
        to->mapping = NULL;
    }
    // Its pages first,''')
c = c.replace('    free(t->pages);\n', '    if (t->mapping) { tide_table_free(t->mapping, &mapping_columns); free(t->mapping); }\n    free(t->pages);\n')
# All external byte streams remain in logical order. The first integration
# deliberately measures straightforward canonicalization, before caching it.
at = c.index('uint64_t tide_table_hash(')
c = c[:at] + '''static void canonical_copy(tide_table *to, const tide_table *t, const tide_columns *c)
{
    for (uint32_t row = 0; row < t->count; row++) tide_table_add(to, c);
    for (uint32_t k = 0; k < c->count; k++) {
        for (uint32_t row = 0; row < t->count; row++)
            memcpy(tide_table_cell(to, c, row, k), tide_table_get(t, c, row, k), c->sizes[k]);
    }
}

''' + c[at:]
for name, ret, args, has_base in [
 ('hash','uint64_t','h, &logical, c',False),
 ('pack','void','&logical, c, w',False),
 ('pack_delta','void','&logical, base ? &logical_base : NULL, c, d',True),
 ('hash_pages','void','&logical, c, w',False),
]:
    start = c.index(ret + ' tide_table_' + name + '(')
    at = c.index('{', start) + 1
    call = ('const uint64_t result = ' if ret != 'void' else '') + 'tide_table_' + name + '(' + args + ');'
    guard = 't->mapping || (base && base->mapping)' if has_base else 't->mapping'
    body = '\n    if (' + guard + ') {\n        tide_table logical = {0};\n        canonical_copy(&logical, t, c);\n'
    if has_base: body += '        tide_table logical_base = {0};\n        if (base) canonical_copy(&logical_base, base, c);\n'
    body += '        ' + call + '\n        tide_table_free(&logical, c);\n'
    if has_base: body += '        tide_table_free(&logical_base, c);\n'
    body += '        return' + (' result' if ret != 'void' else '') + ';\n    }\n'
    c = c[:at] + body + c[at:]
start = c.index('void tide_table_stale('); at = c.index('{',start)+1
c = c[:at] + '\n    if (t->mapping) return; // Logical hashes are computed by the caller.\n' + c[at:]
for name, ret, args in [('unpack_delta','bool','t, &logical_base, c, d'), ('need_pages','void','&logical_base, c, n')]:
    start = c.index(ret + ' tide_table_' + name + '('); at=c.index('{',start)+1
    body='\n    if (base && base->mapping) {\n        tide_table logical_base = {0};\n        canonical_copy(&logical_base, base, c);\n        '
    body+=('const bool result = ' if ret=='bool' else '')+'tide_table_'+name+'('+args+');\n        tide_table_free(&logical_base, c);\n        return'+(' result' if ret=='bool' else '')+';\n    }\n'
    c=c[:at]+body+c[at:]
c += '''
uint32_t tide_table_partition(tide_table *t, const tide_columns *c, uint32_t presence, bool need)
{
    if (presence >= c->count || c->sizes[presence] != 1u) return 0u;
    if (!t->count) return 0u;
    uint32_t lo = 0u, hi = t->count;
    // No mapping/allocation for a table already in the requested order.
    while (lo < hi) {
        const uint8_t *v = tide_table_column(t, c, lo >> c->shift, presence);
        if ((v[lo & (full_room(c)-1u)] != 0u) != need) break;
        lo++;
    }
    while (lo < hi) {
        const uint32_t at = hi - 1u;
        const uint8_t *v = tide_table_column(t, c, at >> c->shift, presence);
        if ((v[at & (full_room(c)-1u)] != 0u) == need) break;
        hi--;
    }
    if (lo == hi) return lo;
    if (!t->mapping) {
        t->mapping = tide_alloc_zeroed(1u, sizeof *t->mapping);
        for (uint32_t r = 0; r < t->count; r++) {
            tide_table_add(t->mapping, &mapping_columns);
            *(uint32_t *)tide_table_cell(t->mapping, &mapping_columns, r, 0u) = r;
            *(uint32_t *)tide_table_cell(t->mapping, &mapping_columns, r, 1u) = r;
        }
    }
    uint32_t width = 1u;
    for (uint32_t k = 0; k < c->count; k++) if (c->sizes[k] > width) width = c->sizes[k];
    uint8_t *scratch = tide_alloc(width);
    while (lo < hi) {
        const uint32_t a = lo, b = hi - 1u;
        for (uint32_t k = 0; k < c->count; k++) {
            uint8_t *x = tide_table_column_mut(t, c, a >> c->shift, k);
            uint8_t *y = tide_table_column_mut(t, c, b >> c->shift, k);
            x += (size_t)(a & (full_room(c)-1u)) * c->sizes[k];
            y += (size_t)(b & (full_room(c)-1u)) * c->sizes[k];
            memcpy(scratch, x, c->sizes[k]); memcpy(x, y, c->sizes[k]); memcpy(y, scratch, c->sizes[k]);
        }
        const uint32_t la = tide_table_logical_row(t, a), lb = tide_table_logical_row(t, b);
        *(uint32_t *)tide_table_cell(t->mapping, &mapping_columns, la, 0u) = b;
        *(uint32_t *)tide_table_cell(t->mapping, &mapping_columns, lb, 0u) = a;
        *(uint32_t *)tide_table_cell(t->mapping, &mapping_columns, a, 1u) = lb;
        *(uint32_t *)tide_table_cell(t->mapping, &mapping_columns, b, 1u) = la;
        do { lo++; if (lo == hi) break;
            const uint8_t *v = tide_table_column(t, c, lo >> c->shift, presence);
            if ((v[lo & (full_room(c)-1u)] != 0u) != need) break;
        } while (lo < hi);
        while (lo < hi) { const uint32_t at = hi-1u;
            const uint8_t *v = tide_table_column(t, c, at >> c->shift, presence);
            if ((v[at & (full_room(c)-1u)] != 0u) == need) break;
            hi--;
        }
    }
    free(scratch);
    return lo;
}
'''
(out / 'table.c').write_text(c)
print(out)
