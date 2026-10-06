// Desktop GPU timing without a GPU: delayed query results, a full queue,
// cancellation, and a driver with no usable timer must never wait or reuse
// a pending query. The actual drawing test checks a real device too.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gl.h"
#include "gpu.h"
#include "window.h"

#define CHECK(condition)                                                                                              \
    do {                                                                                                              \
        if (!(condition)) {                                                                                          \
            fprintf(stderr, "GPU timing check failed at line %d: %s\n", __LINE__, #condition);                      \
            exit(1);                                                                                                 \
        }                                                                                                             \
    } while (0)

// The static library only needs its GL backend here; neither the real GL
// loader nor its window is linked into this test.
tide_gl_functions tide_gl;

void tide_window_pixel_size(int *width, int *height)
{
    *width = *height = 1;
}

typedef struct query {
    bool allocated, pending, available;
    GLuint64 nanoseconds;
} query;

static query queries[32];
static GLuint next_query, active_query;
static unsigned begun, ended, generated, deleted, polled, read_results, counter_checks;
static GLint counter_bits = 64;

static void TIDE_GL_CALL counter(GLenum target, GLenum name, GLint *out)
{
    CHECK(target == GL_TIME_ELAPSED && name == GL_QUERY_COUNTER_BITS);
    counter_checks++;
    *out = counter_bits;
}

static void TIDE_GL_CALL make_queries(GLsizei count, GLuint *out)
{
    CHECK(count == 4);
    generated++;
    for (GLsizei i = 0; i < count; i++) {
        CHECK(next_query + 1u < sizeof queries / sizeof *queries);
        out[i] = ++next_query;
        queries[out[i]].allocated = true;
    }
}

static void TIDE_GL_CALL begin_query(GLenum target, GLuint id)
{
    CHECK(target == GL_TIME_ELAPSED && active_query == 0);
    CHECK(queries[id].allocated && !queries[id].pending);
    queries[id].available = false;
    active_query = id;
    begun++;
}

static void TIDE_GL_CALL end_query(GLenum target)
{
    CHECK(target == GL_TIME_ELAPSED && active_query != 0);
    queries[active_query].pending = true;
    active_query = 0;
    ended++;
}

static void TIDE_GL_CALL query_available(GLuint id, GLenum name, GLint *out)
{
    CHECK(name == GL_QUERY_RESULT_AVAILABLE && queries[id].allocated && queries[id].pending);
    polled++;
    *out = queries[id].available;
}

static void TIDE_GL_CALL query_result(GLuint id, GLenum name, GLuint64 *out)
{
    // Reading a result before it is ready would block a real driver.
    CHECK(name == GL_QUERY_RESULT && queries[id].allocated && queries[id].pending && queries[id].available);
    read_results++;
    *out = queries[id].nanoseconds;
    queries[id].pending = false;
}

static void TIDE_GL_CALL delete_queries(GLsizei count, const GLuint *ids)
{
    CHECK(count == 4 && active_query == 0);
    deleted++;
    for (GLsizei i = 0; i < count; i++) {
        CHECK(queries[ids[i]].allocated);
        queries[ids[i]] = (query){0};
    }
}

static void finish(GLuint id, GLuint64 nanoseconds)
{
    CHECK(queries[id].pending);
    queries[id].nanoseconds = nanoseconds;
    queries[id].available = true;
}

int main(void)
{
    tide_gl = (tide_gl_functions){
        .GetQueryiv = counter,
        .GenQueries = make_queries,
        .BeginQuery = begin_query,
        .EndQuery = end_query,
        .GetQueryObjectiv = query_available,
        .GetQueryObjectui64v = query_result,
        .DeleteQueries = delete_queries,
    };
    const tide_gpu *gpu = &tide_gpu_gl;
    CHECK(gpu->time_read() < 0 && polled == 0);
    gpu->time_end();
    CHECK(ended == 0);
    gpu->time_begin();
    gpu->time_begin(); // A caller cannot nest timer queries.
    CHECK(begun == 1 && generated == 1 && counter_checks == 1);
    CHECK(gpu->time_read() < 0 && polled == 0); // An active query isn't read.
    gpu->time_end();
    gpu->time_end();
    CHECK(ended == 1);
    CHECK(gpu->time_read() < 0 && polled == 1 && read_results == 0);
    finish(1, 2500000);
    CHECK(gpu->time_read() == 2.5 && read_results == 1);
    CHECK(gpu->time_read() < 0 && read_results == 1); // A sample is returned once.

    // All four can be in flight. A fifth frame still draws, without timing.
    for (unsigned i = 0; i < 4; i++) {
        gpu->time_begin();
        gpu->time_end();
    }
    CHECK(begun == 5 && ended == 5 && generated == 1);
    gpu->time_begin();
    gpu->time_end();
    CHECK(begun == 5 && ended == 5);
    finish(3, 1000000);
    CHECK(gpu->time_read() < 0 && read_results == 1); // The oldest is still pending.
    finish(2, 4300000000ull); // Greater than 32 bits, and converted from ns to ms.
    CHECK(gpu->time_read() == 4300.0);
    CHECK(gpu->time_read() == 1.0);
    gpu->time_begin(); // A completed query can be reused while others remain pending.
    CHECK(begun == 6);
    gpu->time_reset(); // Closing while a query is active ends and releases it.
    CHECK(ended == 6 && deleted == 1 && active_query == 0);
    const unsigned previous_polls = polled;
    CHECK(gpu->time_read() < 0 && polled == previous_polls);
    gpu->time_reset();
    CHECK(deleted == 1);

    // No usable counter: no query allocation, no polling, and no fake 0 ms.
    counter_bits = 0;
    gpu->time_begin();
    gpu->time_begin();
    gpu->time_end();
    CHECK(counter_checks == 2 && generated == 1 && begun == 6 && ended == 6);
    CHECK(gpu->time_read() < 0 && polled == previous_polls);
    gpu->time_reset();
    CHECK(deleted == 1);

    // Missing core entry points also leave timing unavailable while drawing
    // remains usable. Reset then allows a new device to be checked again.
    tide_gl.BeginQuery = NULL;
    gpu->time_begin();
    CHECK(counter_checks == 2 && generated == 1);
    CHECK(gpu->time_read() < 0);
    gpu->time_reset();
    tide_gl.BeginQuery = begin_query;
    counter_bits = 64;
    gpu->time_begin();
    gpu->time_end();
    CHECK(counter_checks == 3 && generated == 2);
    CHECK(gpu->time_read() < 0);
    finish(5, 750000);
    CHECK(gpu->time_read() == 0.75);
    gpu->time_reset();
    CHECK(deleted == 2);
    puts("GPU timing: delayed results, full queue, reset, and unavailable timers passed");
    return 0;
}
