// A recording replayed by a build whose data is laid out otherwise (tide/timeline.h, tide/embed.h): this is
// platform/tests/travel_layout.tide's build, and the recordings are ones platform/tests/travel.c's build left
// (TIDE_TRAVEL_RECORDING: a file a test of its leaves, with what its views drew at three ticks beside it, and one
// that doesn't say its layout). Its worlds are carried over by name, its ticks run with this build's code.

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/embed.h"
#include "tide/run.h"
#include "tide/timeline.h"
#include "tide_test.h"

extern const tide_layout tide_game_layout;

// What the views saw at the last frame
static int seen_steps, seen_jumps, seen_lives, seen_total, seen_bonus, seen_season;
static float seen_x;

void Saw(const int steps, const float x, const int jumps, const int lives)
{
    seen_steps = steps;
    seen_x = x;
    seen_jumps = jumps;
    seen_lives = lives;
}

void Totalled(const int total, const int bonus, const int season)
{
    seen_total = total;
    seen_bonus = bonus;
    seen_season = season;
}

static void unseen(void)
{
    seen_steps = seen_jumps = seen_lives = seen_total = seen_bonus = seen_season = -1;
    seen_x = 0.0f;
}

static uint8_t *read_file(const char *path, uint32_t *size)
{
    *size = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *bytes = n > 0 ? malloc((size_t)n) : NULL;
    if (bytes && fread(bytes, 1, (size_t)n, f) == (size_t)n) *size = (uint32_t)n;
    fclose(f);
    return bytes;
}

// The other build's recording, or NULL with a message where it left none
static uint8_t *recording(const char *suffix, uint32_t *size, char *path, const size_t room)
{
    const char *left = getenv("TIDE_TRAVEL_RECORDING");
    if (!left) {
        fprintf(stderr, "TIDE_TRAVEL_RECORDING isn't set: run this through ctest, after tide_platform_travel\n");
        return NULL;
    }
    snprintf(path, room, "%s%s", left, suffix);
    return read_file(path, size);
}

static char program[] = "travel_layout";

// The other layout's recording replays here: its worlds carried over by name (what its views drew of three ticks
// drawn again, the new fields at their defaults), every tick run with this build's code, and nothing checked
TIDE_TEST(travel_replays_a_recording_of_another_layout)
{
    char path[1024], said[1100];
    uint32_t size = 0;
    uint8_t *bytes = recording("", &size, path, sizeof path);
    TIDE_REQUIRE(bytes != NULL && size > 0);
    snprintf(said, sizeof said, "%s.txt", path);
    FILE *f = fopen(said, "r");
    TIDE_REQUIRE(f != NULL);
    unsigned ticks[3], bits[3];
    int steps[3], jumps[3], totals[3];
    for (int i = 0; i < 3; i++) {
        TIDE_REQUIRE(fscanf(f, "%u %d %u %d %d", &ticks[i], &steps[i], &bits[i], &jumps[i], &totals[i]) == 5);
    }
    fclose(f);

    char *argv[] = {program};
    tide_embed *e = tide_embed_create(&(tide_embed_desc){.game = &tide_host_game_api, .argc = 1, .argv = argv});
    TIDE_REQUIRE(e != NULL);
    TIDE_REQUIRE(tide_embed_request(e, TIDE_REQUEST_LEAVE, NULL, 0));
    for (int i = 0; i < 2; i++) tide_embed_frame(e, 1.0f / 60.0f, NULL, 800.0f, 600.0f, 1.0f);
    const bool replayed = tide_embed_replay(e, bytes, size);
    tide_embed_travel t;
    tide_embed_travel_of(e, &t);
    if (!replayed) fprintf(stderr, "not replayed: %s\n", t.refused);
    TIDE_REQUIRE(replayed);
    TIDE_CHECK(t.state == TIDE_TRAVEL_REPLAY);
    TIDE_CHECK(t.other_build == 2u); // Laid out otherwise, and carried over
    TIDE_CHECK(t.fields_reset == 1u && t.entities_dropped == 0u && t.tasks_dropped == 0u); // Score.drift, a float there
    TIDE_CHECK(t.first == ticks[0] && t.last == ticks[2]);
    for (int i = 0; i < 3; i++) {
        TIDE_REQUIRE(tide_embed_show(e, ticks[i]));
        unseen();
        tide_embed_frame(e, 1.0f / 60.0f, NULL, 800.0f, 600.0f, 1.0f);
        uint32_t x_bits;
        memcpy(&x_bits, &seen_x, sizeof x_bits);
        // What the recording's build drew of that tick, by name, wherever the fields are now
        TIDE_CHECK(seen_steps == steps[i] && seen_jumps == jumps[i] && seen_total == totals[i]);
        TIDE_CHECK(steps[i] < 0 || x_bits == bits[i]);
        // ...and what it never had, as this build declares it
        TIDE_CHECK(steps[i] < 0 || seen_lives == 3);
        TIDE_CHECK(seen_bonus == 10 && seen_season == 2);
    }
    TIDE_CHECK(steps[2] > 30 && totals[2] == 3); // A recording of something: its walker's steps and its last Bump
    tide_embed_travel_of(e, &t);
    TIDE_CHECK(t.unreadable == 0 && t.differs == 0);
    TIDE_CHECK(t.checked == t.first); // Nothing to check its ticks against
    TIDE_CHECK(t.shown == t.last && t.hash != 0);
    // Played from its start, at its rate, to its end
    TIDE_REQUIRE(tide_embed_show(e, t.first));
    TIDE_REQUIRE(tide_embed_pause(e, false));
    for (uint32_t i = 0; i < 2u * (t.last - t.first) + 20u; i++) tide_embed_frame(e, 1.0f / 60.0f, NULL, 800.0f, 600.0f, 1.0f);
    tide_embed_travel_of(e, &t);
    TIDE_CHECK(!t.playing && t.shown == t.last && t.unreadable == 0);
    TIDE_CHECK(seen_steps == steps[2] && seen_total == totals[2]);
    tide_embed_destroy(e);

    // The timeline's own account: carried over, but for the one field whose type changed
    tide_timeline_about about;
    char why[200];
    tide_timeline *timeline = tide_timeline_load(&(tide_timeline_desc){.game = &tide_game_api, .layout = &tide_game_layout},
                                                 bytes, size, &about, why, sizeof why);
    TIDE_REQUIRE(timeline != NULL);
    TIDE_CHECK(about.other_layout && about.fields_reset == 1 && about.entities_dropped == 0 && about.tick_rate == 30u);
    TIDE_CHECK(tide_timeline_world(timeline, tide_timeline_last(timeline)) != NULL);
    tide_timeline_check check;
    tide_timeline_check_of(timeline, &check);
    TIDE_CHECK(check.differs == 0 && check.unreadable == 0 && check.checked == tide_timeline_first(timeline));
    // It isn't saved again as this layout's: its ticks' hashes are the other's
    uint32_t again_size = 7;
    TIDE_CHECK(tide_timeline_save(timeline, 30, 1, &again_size) == NULL && again_size == 0);
    tide_timeline_destroy(timeline);

    // A build that doesn't say its own layout can't carry one over
    TIDE_CHECK(tide_timeline_load(&(tide_timeline_desc){.game = &tide_game_api}, bytes, size, &about, why, sizeof why) == NULL);
    TIDE_CHECK(strstr(why, "laid out otherwise") != NULL);
    // Cut short inside the layout it carries: refused, read no further
    TIDE_CHECK(tide_timeline_load(&(tide_timeline_desc){.game = &tide_game_api, .layout = &tide_game_layout}, bytes, 200u,
                                  &about, why, sizeof why) == NULL);
    TIDE_CHECK(strstr(why, "cut short") != NULL);
    free(bytes);
}

// A recording that doesn't say the layout it was recorded with (a timeline made without one) is refused by a build
// laid out otherwise, saying so
TIDE_TEST(travel_refuses_another_layouts_recording_that_says_no_layout)
{
    char path[1024], why[240];
    uint32_t size = 0;
    uint8_t *bytes = recording(".plain", &size, path, sizeof path);
    TIDE_REQUIRE(bytes != NULL && size > 0);
    tide_timeline_about about;
    TIDE_CHECK(tide_timeline_load(&(tide_timeline_desc){.game = &tide_game_api, .layout = &tide_game_layout}, bytes, size,
                                  &about, why, sizeof why) == NULL);
    TIDE_CHECK(strstr(why, "laid out otherwise") != NULL && strstr(why, "doesn't say how") != NULL);
    free(bytes);
}
