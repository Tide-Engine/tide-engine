#include "tide/draw.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tide/page.h"

// GPU lists' states (tide/draw.h, GPU lists), and reads of them back. What's
// on the GPU is the renderer's (platform.c), which finds a list's buffer by
// its id and makes it again where its version or the device changed.

static tide_gpu_list_state *gpu_lists;

// A capacity that's at least 0, and no more than what the list's bytes can be.
static uint32_t clamped_capacity(const int32_t capacity, const uint32_t stride)
{
    if (capacity <= 0) return 0;
    const uint32_t most = stride ? TIDE_GPU_LIST_MOST_WORDS / stride : TIDE_GPU_LIST_MOST_WORDS;
    if ((uint32_t)capacity > most) {
        fprintf(stderr, "GPUList: a list of %d elements of %u words is more than a GPU binds: it holds %u\n", (int)capacity,
                (unsigned)stride, (unsigned)most);
        return most;
    }
    return (uint32_t)capacity;
}

static tide_gpu_list made(const uint32_t capacity, const uint32_t stride)
{
    void *memory = calloc(1, sizeof(tide_gpu_list_state));
    if (!memory) tide_out_of_memory();
    tide_gpu_list_state *s = memory;
    *s = (tide_gpu_list_state){.next = gpu_lists, .owner = &gpu_lists, .capacity = capacity, .stride = stride, .version = 1,
                               .content_revision = tide_render_revision_next(), .alive = true, .memory = memory};
    gpu_lists = s;
    return (tide_gpu_list){.id = TIDE_GPU_LIST_MANAGED | (uint64_t)(uintptr_t)s, .capacity = (int32_t)capacity, .stride = stride};
}

tide_gpu_list tide_gpu_list_make(const int32_t capacity, const uint32_t stride)
{
    if (capacity < 0) fprintf(stderr, "GPUList(capacity): a capacity is 0 or more, and this asked for %d: it holds none\n",
                              (int)capacity);
    return made(clamped_capacity(capacity, stride), stride);
}

void tide_gpu_list_resize(const tide_gpu_list list, const int32_t capacity)
{
    tide_gpu_list_state *s = tide_gpu_list_state_of(list.id);
    if (!s || !s->alive) {
        fprintf(stderr, "GPUList.Resize: the list was never made, or was disposed: make one with 'GPUList<T>(capacity)'\n");
        return;
    }
    if (capacity < 0) fprintf(stderr, "GPUList.Resize: a capacity is 0 or more, and this asked for %d\n", (int)capacity);
    s->capacity = clamped_capacity(capacity, s->stride);
    s->version++;
    s->content_revision = tide_render_revision_next();
}

void tide_gpu_list_dispose(const tide_gpu_list list)
{
    tide_gpu_list_state *s = tide_gpu_list_state_of(list.id);
    if (s) s->alive = false;
}

int32_t tide_gpu_list_capacity(const tide_gpu_list list)
{
    const tide_gpu_list_state *s = tide_gpu_list_state_of(list.id);
    return s && s->alive ? (int32_t)s->capacity : 0;
}

int64_t tide_gpu_list_content_revision(const tide_gpu_list list)
{
    const tide_gpu_list_state *s = tide_gpu_list_state_of(list.id);
    return s && s->alive ? s->content_revision : 0;
}

void tide_gpu_list_made(const uint64_t id)
{
    tide_gpu_list_state *s = tide_gpu_list_state_of(id);
    if (!s) return;
    if (s->made) s->content_revision = tide_render_revision_next();
    s->made = true;
}

void tide_gpu_list_adopt(tide_gpu_list *list, const uint32_t stride)
{
    tide_gpu_list_state *s = tide_gpu_list_state_of(list->id);
    if (!s) return;
    if (s->owner != &gpu_lists) {
        tide_render_target_revision_resume((uint64_t)s->content_revision);
        tide_gpu_list_state **at = s->owner;
        while (*at && *at != s) at = &(*at)->next;
        if (*at) *at = s->next;
        s->owner = &gpu_lists;
        s->next = gpu_lists;
        gpu_lists = s;
    }
    if (s->stride != stride) { // Another element: another buffer, empty, of as many elements as fit
        s->stride = stride;
        s->capacity = clamped_capacity((int32_t)s->capacity, stride);
        s->version++;
        s->content_revision = tide_render_revision_next();
    }
    list->capacity = (int32_t)s->capacity;
    list->stride = stride;
}

tide_gpu_list tide_gpu_list_recreate(const tide_gpu_list previous, const uint32_t stride)
{
    if (!(previous.id & TIDE_GPU_LIST_MANAGED)) return (tide_gpu_list){0};
    for (const tide_gpu_list_state *s = gpu_lists; s; s = s->next) {
        if (s->reload_id == previous.id)
            return (tide_gpu_list){.id = TIDE_GPU_LIST_MANAGED | (uint64_t)(uintptr_t)s, .capacity = (int32_t)s->capacity,
                                   .stride = s->stride};
    }
    tide_gpu_list next = made(clamped_capacity(previous.capacity, stride), stride);
    tide_gpu_list_state_of(next.id)->reload_id = previous.id;
    return next;
}

void tide_gpu_lists_recreated(void)
{
    for (tide_gpu_list_state *s = gpu_lists; s; s = s->next) s->reload_id = 0;
}

void tide_gpu_lists_free(void)
{
    while (gpu_lists) {
        tide_gpu_list_state *s = gpu_lists;
        gpu_lists = s->next;
        free(s->memory);
    }
}

// ---------------------------------------------------------------------------
// Reads back

static tide_gpu_read_request reads[TIDE_GPU_READS];
static uint32_t read_serial;

tide_gpu_read_request *tide_gpu_read_requests(void)
{
    return reads;
}

// A ticket: the request's place and its serial, so that a ticket of a request let go and taken again is no longer one.
static uint32_t ticket_of(const uint32_t place)
{
    return (reads[place].serial << 6) | place;
}

static tide_gpu_read_request *request_of(const uint32_t ticket)
{
    if (!ticket) return NULL;
    tide_gpu_read_request *r = &reads[ticket & (TIDE_GPU_READS - 1u)];
    return r->state != TIDE_GPU_READ_FREE && !r->forgotten && ticket_of(ticket & (TIDE_GPU_READS - 1u)) == ticket ? r : NULL;
}

uint32_t tide_gpu_list_read_begin(const tide_gpu_list list, const bool count_only)
{
    for (uint32_t i = 0; i < TIDE_GPU_READS; i++) {
        tide_gpu_read_request *r = &reads[i];
        if (r->state != TIDE_GPU_READ_FREE) continue;
        const tide_gpu_list_state *s = tide_gpu_list_state_of(list.id);
        const uint64_t bytes = s && s->alive ? 4u * (TIDE_GPU_LIST_HEADER_WORDS + (count_only ? 0u : (uint64_t)s->capacity * s->stride)) : 0u;
        // A serial that's never 0 for place 0, so that no ticket is 0
        if (++read_serial >= (UINT32_MAX >> 6)) read_serial = 1;
        *r = (tide_gpu_read_request){.list = list.id, .bytes = (uint32_t)bytes, .state = TIDE_GPU_READ_ASKED,
                                     .serial = read_serial, .count_only = count_only};
        if (bytes) {
            r->data = malloc((size_t)bytes);
            if (!r->data) tide_out_of_memory();
        } else {
            r->state = TIDE_GPU_READ_FAILED; // No list to read
        }
        return ticket_of(i);
    }
    return 0;
}

void tide_gpu_read_finished(tide_gpu_read_request *r, const bool ok)
{
    if (r->forgotten) { // Nobody waits for it any more: its place goes free now that the GPU wrote nothing more
        free(r->data);
        *r = (tide_gpu_read_request){.serial = r->serial};
        return;
    }
    r->state = ok ? TIDE_GPU_READ_DONE : TIDE_GPU_READ_FAILED;
}

bool tide_gpu_list_read_poll(const uint32_t ticket, tide_gpu_read_result *out)
{
    *out = (tide_gpu_read_result){0};
    const tide_gpu_read_request *r = request_of(ticket);
    if (!r) return ticket != 0; // Another build's, or let go: failed. No ticket yet: not done
    if (r->state == TIDE_GPU_READ_ASKED || r->state == TIDE_GPU_READ_BEGUN) return false;
    if (r->state == TIDE_GPU_READ_FAILED || !r->data) return true;
    const tide_gpu_list_state *s = tide_gpu_list_state_of(r->list);
    const uint32_t stride = s ? s->stride : 0u;
    const uint32_t room = r->count_only || !stride ? 0u : (r->bytes / 4u - TIDE_GPU_LIST_HEADER_WORDS) / stride;
    uint32_t count = r->data[0];
    if (!r->count_only && count > room) count = room;
    if (r->count_only && count > (uint32_t)INT32_MAX) count = (uint32_t)INT32_MAX;
    *out = (tide_gpu_read_result){.ok = true, .count = (int32_t)count, .stride = stride,
                                  .words = r->data + TIDE_GPU_LIST_HEADER_WORDS};
    return true;
}

void tide_gpu_reads_frame(void)
{
    for (uint32_t i = 0; i < TIDE_GPU_READS; i++) {
        tide_gpu_read_request *r = &reads[i];
        if (r->state != TIDE_GPU_READ_DONE && r->state != TIDE_GPU_READ_FAILED) continue;
        if (++r->age <= TIDE_GPU_READ_KEPT_FRAMES) continue;
        free(r->data);
        *r = (tide_gpu_read_request){.serial = r->serial};
    }
}

void tide_gpu_list_read_forget(const uint32_t ticket)
{
    tide_gpu_read_request *r = request_of(ticket);
    if (!r) return;
    if (r->state == TIDE_GPU_READ_BEGUN) { // The GPU may still write it: let go of it once the renderer's done with it
        r->forgotten = true;
        return;
    }
    free(r->data);
    *r = (tide_gpu_read_request){.serial = r->serial};
}
