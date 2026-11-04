#include "asset_sizes.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

static char *copy_of(const char *text)
{
    const size_t n = strlen(text) + 1u;
    char *copy = malloc(n);
    if (!copy) abort();
    memcpy(copy, text, n);
    return copy;
}

void asset_sizes_add(void *user, const char *name, const char *what, const uint64_t bytes)
{
    asset_sizes *sizes = user;
    if (sizes->count == sizes->room) {
        sizes->room = sizes->room ? sizes->room * 2 : 32;
        sizes->items = realloc(sizes->items, sizeof *sizes->items * (size_t)sizes->room);
        if (!sizes->items) abort();
    }
    sizes->items[sizes->count++] = (struct asset_size){copy_of(name), copy_of(what), bytes};
}

static int biggest_first(const void *a, const void *b)
{
    const struct asset_size *x = a, *y = b;
    if (x->bytes != y->bytes) return x->bytes > y->bytes ? -1 : 1;
    return strcmp(x->name, y->name);
}

// Bytes as a person reads them: 512 B, 2.8 MB (decimal, as file managers say).
static void put_bytes(char out[16], const uint64_t bytes)
{
    if (bytes < 1000u) snprintf(out, 16, "%" PRIu64 " B", bytes);
    else if (bytes < 1000000u) snprintf(out, 16, "%.1f kB", (double)bytes / 1e3);
    else if (bytes < 1000000000u) snprintf(out, 16, "%.1f MB", (double)bytes / 1e6);
    else snprintf(out, 16, "%.2f GB", (double)bytes / 1e9);
}

void asset_sizes_print(FILE *to, asset_sizes *sizes)
{
    if (!sizes->count) return;
    qsort(sizes->items, (size_t)sizes->count, sizeof *sizes->items, biggest_first);
    uint64_t total = 0;
    int widest = 4;
    for (int i = 0; i < sizes->count; i++) {
        total += sizes->items[i].bytes;
        const int n = (int)strlen(sizes->items[i].name);
        if (n > widest) widest = n;
    }
    if (widest > 48) widest = 48;
    char amount[16];
    put_bytes(amount, total);
    fprintf(to, "Assets in the pack: %s in %d declaration%s, biggest first\n", amount, sizes->count, sizes->count == 1 ? "" : "s");
    for (int i = 0; i < sizes->count; i++) {
        const struct asset_size *s = &sizes->items[i];
        put_bytes(amount, s->bytes);
        const double share = total ? 100.0 * (double)s->bytes / (double)total : 0.0;
        fprintf(to, "  %10s %5.1f%%  %-*s  %s\n", amount, share, widest, s->name, s->what);
    }
}

void asset_sizes_free(asset_sizes *sizes)
{
    for (int i = 0; i < sizes->count; i++) free(sizes->items[i].name), free(sizes->items[i].what);
    free(sizes->items);
    *sizes = (asset_sizes){0};
}
