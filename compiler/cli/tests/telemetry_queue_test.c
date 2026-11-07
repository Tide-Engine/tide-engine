// Force queue interleavings without timing two processes. File operations and
// process locks are real; only HTTP, the clock and the interleaving are supplied.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sys.h"

static FILE *fixture_fopen(const char *path, const char *mode);
static int fixture_run(const char *const *argv, const char *cwd, bool quiet);
static int64_t fixture_now_ms(void);
static void fixture_sleep_ms(int ms);

#define fopen fixture_fopen
#define sys_run fixture_run
#define sys_now_ms fixture_now_ms
#define sys_sleep_ms fixture_sleep_ms
#include "../telemetry.c"
#undef sys_sleep_ms
#undef sys_now_ms
#undef sys_run
#undef fopen

#define WORK "telemetry_queue_test"
#define QUEUE WORK "/queue"
#define LOCK WORK "/queue.lock"

static const char first[] = "{\"command\":\"first\"}\n";
static const char second[] = "{\"command\":\"second\"}\n";
static const char third[] = "{\"command\":\"third\"}\n";
static int64_t now;
static int release_at;
static int requests;
static int response;
static bool claim_while_open;
static bool append_while_sending;
static bool lock_after_sending;
static sys_lock *held;
static char body[4096];

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

static int64_t fixture_now_ms(void) { return now; }

static void fixture_sleep_ms(const int ms)
{
    now += ms;
    if (release_at && now >= release_at) {
        CHECK(held);
        sys_unlock(held);
        held = NULL;
        release_at = 0;
    }
}

static FILE *fixture_fopen(const char *path, const char *mode)
{
    FILE *f = fopen(path, mode);
    if (claim_while_open && strcmp(mode, "ab") == 0) {
        claim_while_open = false;
        // On POSIX, the former sender could rename/read/delete this open
        // file now. The writer then appended into an unlinked file.
        CHECK(send_queued(WORK, QUEUE, false) == 0);
        CHECK(requests == 0);
    }
    return f;
}

static int fixture_run(const char *const *argv, const char *cwd, const bool quiet)
{
    (void)cwd;
    (void)quiet;
    CHECK(strcmp(argv[9], "--data-binary") == 0 && argv[10][0] == '@');
    char *text = sys_read_file(argv[10] + 1, NULL);
    CHECK(text && strlen(text) < sizeof body);
    strcpy(body, text);
    free(text);
    requests++;
    // No queue lock may span the HTTP request, even when it is slow/offline.
    sys_lock *lock = sys_lock_file(LOCK);
    CHECK(lock);
    sys_unlock(lock);
    if (append_while_sending) {
        append_while_sending = false;
        const int64_t before = now;
        CHECK(append_queue(WORK, third, strlen(third)));
        CHECK(now == before);
    }
    if (lock_after_sending) {
        lock_after_sending = false;
        held = sys_lock_file(LOCK);
        CHECK(held);
    }
    return response;
}

// These unrelated entry points are linked but never used by the queue cases.
bool rtc_random(void *out, const size_t size) { (void)out; (void)size; return false; }
bool tide_version_dev(const char *version) { (void)version; return true; }

int main(void)
{
    CHECK(sys_mkdirs(WORK));
    CHECK(sys_write_text(QUEUE, first));
    claim_while_open = true;
    CHECK(append_queue(WORK, second, strlen(second)));
    CHECK(!claim_while_open && requests == 0);
    append_while_sending = true;
    CHECK(send_queued(WORK, QUEUE, false) == 1);
    CHECK(strcmp(body, "{\"events\":[{\"command\":\"first\"},{\"command\":\"second\"}]}") == 0);
    CHECK(send_queued(WORK, QUEUE, false) == 1);
    CHECK(strcmp(body, "{\"events\":[{\"command\":\"third\"}]}") == 0);
    CHECK(send_queued(WORK, QUEUE, false) == 0 && requests == 2);

    held = sys_lock_file(LOCK);
    CHECK(held);
    const int64_t before = now;
    CHECK(!append_queue(WORK, first, strlen(first)));
    CHECK(now == before + 100 && !sys_exists(QUEUE));
    release_at = (int)now + 3;
    CHECK(append_queue(WORK, first, strlen(first)));
    CHECK(!held && now == before + 103);

    // An offline batch whose requeue is blocked keeps its only disk copy,
    // which the next sender recovers after the original sender has ended.
    response = 7;
    lock_after_sending = true;
    CHECK(send_queued(WORK, QUEUE, false) == -1);
    CHECK(held);
    sys_unlock(held);
    held = NULL;
    char sending[128];
    snprintf(sending, sizeof sending, WORK "/sending-%u", (unsigned)sys_pid());
    CHECK(sys_exists(sending) && !sys_exists(QUEUE));
    CHECK(sys_rename(sending, WORK "/sending-0"));
    response = 0;
    CHECK(send_queued(WORK, QUEUE, true) == 1);
    CHECK(strcmp(body, "{\"events\":[{\"command\":\"first\"}]}") == 0);
    CHECK(!sys_exists(WORK "/sending-0") && !sys_exists(sending));
    CHECK(sys_remove(LOCK));
    CHECK(sys_remove(WORK));
    puts("ok: telemetry queue claims preserve concurrent appends without locking HTTP");
    return 0;
}
