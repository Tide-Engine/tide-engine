from pathlib import Path
p=Path(__file__).resolve().parent
h=(p/'tide/table.h').read_text().replace('    struct tide_table *mapping;', '    struct tide_table_cache *canonical; // Derived logical pages, never serialized.\n    struct tide_table *mapping;')
(p/'tide/table.h').write_text(h)
c=(p/'table.c').read_text()
at=c.index('static const uint32_t mapping_sizes')
c=c[:at]+'''typedef struct tide_table_cache {
    tide_table source, logical;
} tide_table_cache;
static void cache_free(tide_table *t, const tide_columns *c)
{
    if (!t->canonical) return;
    tide_table_free(&t->canonical->source, c);
    tide_table_free(&t->canonical->logical, c);
    free(t->canonical);
    t->canonical = NULL;
}

'''+c[at:]
c=c.replace('    // Its pages first,', '    cache_free(to, c);\n    // Its pages first,')
c=c.replace('    if (t->mapping) { tide_table_free(t->mapping, &mapping_columns); free(t->mapping); }', '    cache_free(t, c);\n    if (t->mapping) { tide_table_free(t->mapping, &mapping_columns); free(t->mapping); }')
start=c.index('static void canonical_copy('); end=c.index('uint64_t tide_table_hash(',start)
c=c[:start]+'''static void canonical_copy(tide_table *to, const tide_table *t, const tide_columns *c)
{
    make_room(to, c, t->chunks);
    to->count=t->count; to->chunks=t->chunks; to->last_room=t->last_room;
    for (uint32_t chunk=0;chunk<t->chunks;chunk++) {
        const uint32_t rows=tide_table_rows(t,c->shift,chunk);
        for (uint32_t k=0;k<c->count;k++) {
            tide_page *page=tide_page_new(t->pages[(size_t)chunk*c->count+k]->size);
            to->pages[(size_t)chunk*c->count+k]=page;
            uint8_t *dst=tide_page_data(page);
            memcpy(dst,tide_table_column(t,c,chunk,k),(size_t)rows*c->sizes[k]);
            for(uint32_t i=0;i<rows;i++) {
                const uint32_t logical=(chunk<<c->shift)+i;
                if(tide_table_physical_row(t,logical)==logical) continue;
                memcpy(dst+(size_t)i*c->sizes[k],tide_table_get(t,c,logical,k),c->sizes[k]);
            }
        }
    }
}

static const tide_table *canonical_view(const tide_table *table, const tide_columns *c)
{
    if (!table->mapping) return table;
    // As with a page's memoized hash, derived bytes change during serial hashing/packing only.
    tide_table *t=(tide_table *)table;
    if (!t->canonical) t->canonical=tide_alloc_zeroed(1u,sizeof *t->canonical);
    tide_table_cache *cache=t->canonical;
    const tide_table *old=&cache->source;
    bool same=old->count==t->count && old->mapping && old->mapping->chunks==t->mapping->chunks;
    for(uint32_t i=0;same && i<t->mapping->chunks*2u;i++)
        same=old->mapping->pages[i]==t->mapping->pages[i];
    if(!same) {
        tide_table_free(&cache->logical,c);
        canonical_copy(&cache->logical,t,c);
    } else {
        for(uint32_t chunk=0;chunk<t->chunks;chunk++) for(uint32_t k=0;k<c->count;k++) {
            if(old->pages[(size_t)chunk*c->count+k]==t->pages[(size_t)chunk*c->count+k]) continue;
            const uint8_t *src=tide_table_column(t,c,chunk,k);
            const uint32_t rows=tide_table_rows(t,c->shift,chunk);
            for(uint32_t i=0;i<rows;i++) {
                const uint32_t logical=tide_table_logical_row(t,(chunk<<c->shift)+i);
                const uint8_t *value=src+(size_t)i*c->sizes[k];
                const uint32_t lc=logical>>c->shift, li=logical&((1u<<c->shift)-1u);
                if(c->sizes[k]==4u) {
                    uint32_t next,was;
                    memcpy(&next,value,4u);
                    memcpy(&was,(const uint8_t *)tide_table_column(&cache->logical,c,lc,k)+(size_t)li*4u,4u);
                    if(next==was) continue;
                    memcpy((uint8_t *)tide_table_column_mut(&cache->logical,c,lc,k)+(size_t)li*4u,&next,4u);
                } else {
                    if(memcmp(tide_table_get(&cache->logical,c,logical,k),value,c->sizes[k])==0) continue;
                    memcpy(tide_table_cell(&cache->logical,c,logical,k),value,c->sizes[k]);
                }
            }
        }
    }
    tide_table_copy(&cache->source,t,c);
    return &cache->logical;
}

'''+c[end:]
# Replace temporary canonical copies at stream boundaries with the exact
# retained-page cache. Ordinary tables still use the previous implementation.
for name,ret,args,hasbase in [('hash','uint64_t','h, logical, c',False),('pack','void','logical, c, w',False),('pack_delta','void','logical, base ? canonical_view(base, c) : NULL, c, d',True),('hash_pages','void','logical, c, w',False)]:
    start=c.index(ret+' tide_table_'+name+'('); at=c.index('{',start)+1
    end=c.index('\n    }',at)+6
    guard='t->mapping || (base && base->mapping)' if hasbase else 't->mapping'
    body='\n    if ('+guard+') {\n        const tide_table *logical=canonical_view(t,c);\n        '+('return ' if ret!='void' else '')+'tide_table_'+name+'('+args+');\n'+('        return;\n' if ret=='void' else '')+'    }'
    c=c[:at]+body+c[end:]
for name,ret,args in [('unpack_delta','bool','t, canonical_view(base, c), c, d'),('need_pages','void','canonical_view(base, c), c, n')]:
    start=c.index(ret+' tide_table_'+name+'('); at=c.index('{',start)+1;end=c.index('\n    }',at)+6
    body='\n    if (base && base->mapping) {\n        '+('return ' if ret=='bool' else '')+'tide_table_'+name+'('+args+');\n'+('        return;\n' if ret=='void' else '')+'    }'
    c=c[:at]+body+c[end:]
(p/'table.c').write_text(c)
