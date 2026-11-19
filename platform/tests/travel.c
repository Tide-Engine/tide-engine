// Time travel (tide/embed.h) and the timeline under it (tide/timeline.h), with
// platform/tests/travel.tide: pausing a match, stepping it a tick at a time,
// showing kept ticks again, recording and replaying it bit for bit, saying
// where a replay goes otherwise, and going on from a tick shown.

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/embed.h"
#include "tide/embed_game.h"
#include "tide/run.h"
#include "tide/timeline.h"
#include "tide_test.h"

extern const tide_embed_game tide_game_embed;

// What the views saw at the last frame (travel.tide's Show and Count)
static int seen_steps = -1, seen_jumps = -1, seen_total = -1;
static float seen_x;

void Saw(const int steps, const float x, const int jumps)
{
    seen_steps = steps;
    seen_x = x;
    seen_jumps = jumps;
}

void Totalled(const int total)
{
    seen_total = total;
}

// What the local handler of Bumped heard since `heard_bumps` was last zeroed (travel.tide's Hear): how many, the last
// one's value, what its local state had counted and what the match's total was as its tick left it
static int heard_bumps, heard_by, heard_count, heard_total;

void HeardBump(const int by, const int heard, const int total)
{
    heard_bumps++;
    heard_by = by;
    heard_count = heard;
    heard_total = total;
}

static uint32_t u32_at(const uint8_t *p, const uint32_t at)
{
    uint32_t v;
    memcpy(&v, p + at, sizeof v);
    return v;
}

// The last frame's packet's EVENTS, as another program reads them: how many it has as predicted and as confirmed,
// with the tick and value of the last
static void packet_events(const tide_embed *e, int *predicted, int *confirmed, uint32_t *tick, int *by)
{
    *predicted = *confirmed = 0;
    uint32_t size = 0;
    const uint8_t *p = tide_embed_view(e, &size);
    if (!p) return;
    const uint32_t entities = u32_at(p, 36), sections = u32_at(p, 40);
    uint32_t at = TIDE_PACKET_HEADER + entities * TIDE_PACKET_ENTITY;
    for (uint32_t s = 0; s < sections; s++) {
        at = (at + 7u) & ~7u;
        if (at + TIDE_PACKET_SECTION > size) return;
        const uint32_t kind = u32_at(p, at), count = u32_at(p, at + 8), bytes = u32_at(p, at + 16);
        const uint8_t *records = p + at + TIDE_PACKET_SECTION;
        at += TIDE_PACKET_SECTION + bytes;
        if (kind != TIDE_PACKET_EVENTS) continue;
        for (uint32_t i = 0, r = 0; i < count; i++) {
            if (u32_at(records, r + 8) == 1u) ++*predicted;
            else ++*confirmed;
            *tick = u32_at(records, r + 12);
            memcpy(by, records + r + TIDE_PACKET_EVENT, sizeof *by);
            r += u32_at(records, r);
        }
    }
}

static char program[] = "travel";

static void sample_none(void *user, const uint32_t tick, void *input)
{
    (void)user, (void)tick, (void)input;
}

enum { KEY_B = 1, KEY_D = 3, KEY_SPACE = 36 };

static tide_embed *start(void)
{
    char *argv[] = {program};
    seen_steps = seen_jumps = seen_total = -1;
    return tide_embed_create(&(tide_embed_desc){.game = &tide_host_game_api, .argc = 1, .argv = argv});
}

// `count` frames of a 60th of a second (two frames a tick, at 30 ticks a second), with `input` in each
static void frames(tide_embed *e, const int count, const tide_embed_input *input)
{
    for (int i = 0; i < count; i++) tide_embed_frame(e, 1.0f / 60.0f, input, 800.0f, 600.0f, 1.0f);
}

static void hold(tide_embed_input *input, const unsigned key, const bool held)
{
    if (held) input->keys[key / 8] |= (uint8_t)(1u << key % 8);
    else input->keys[key / 8] &= (uint8_t)~(1u << key % 8);
}

static tide_embed_travel travel_of(const tide_embed *e)
{
    tide_embed_travel t;
    tide_embed_travel_of(e, &t);
    return t;
}

static uint32_t verified(const tide_embed *e)
{
    tide_embed_status status;
    tide_embed_status_of(e, &status);
    return status.verified_tick;
}

static uint32_t bump_type(void)
{
    return tide_game_layout.type_count; // The first type after the layout's: the game's one command
}

TIDE_TEST(travel_is_off_until_kept)
{
    tide_embed *e = start();
    TIDE_REQUIRE(e != NULL);
    frames(e, 10, NULL);
    tide_embed_travel t = travel_of(e);
    TIDE_CHECK(t.state == TIDE_TRAVEL_OFF);
    TIDE_CHECK(strstr(t.why, "off") != NULL);
    TIDE_CHECK(!tide_embed_pause(e, true));
    TIDE_CHECK(!tide_embed_step(e));
    uint32_t size = 7;
    TIDE_CHECK(tide_embed_recording(e, &size) == NULL && size == 0);
    tide_embed_travel_keep(e, 10.0f);
    frames(e, 10, NULL);
    t = travel_of(e);
    TIDE_CHECK(t.state == TIDE_TRAVEL_LIVE);
    TIDE_CHECK(t.why[0] == '\0');
    TIDE_CHECK(t.tick_rate == 30u);
    TIDE_CHECK(t.last == verified(e)); // Recorded from the tick it was asked at, up to the match's latest
    tide_embed_destroy(e);
}

// Paused, the match runs no tick however many frames come, the views draw its latest tick as it is, and a step
// runs one, with what's held and pressed then; going on, it runs again
TIDE_TEST(travel_pauses_and_steps_one_tick)
{
    tide_embed *e = start();
    TIDE_REQUIRE(e != NULL);
    tide_embed_travel_keep(e, 10.0f);
    frames(e, 30, NULL);
    TIDE_REQUIRE(tide_embed_pause(e, true));
    frames(e, 1, NULL);
    const uint32_t at = verified(e);
    tide_embed_travel t = travel_of(e);
    TIDE_CHECK(t.state == TIDE_TRAVEL_PAUSED);
    TIDE_CHECK(!t.background);
    TIDE_CHECK(t.shown == at && t.last == at);
    const int steps = seen_steps;
    TIDE_CHECK(steps > 0);
    tide_embed_input input = {0};
    hold(&input, KEY_D, true);
    frames(e, 60, &input); // A second of frames: no tick
    TIDE_CHECK(verified(e) == at);
    TIDE_CHECK(seen_steps == steps);
    TIDE_CHECK(travel_of(e).last == at);
    const float x = seen_x;
    hold(&input, KEY_SPACE, true); // Pressed while paused: the step's tick has it
    frames(e, 1, &input);
    hold(&input, KEY_SPACE, false);
    TIDE_REQUIRE(tide_embed_step(e));
    frames(e, 1, &input);
    TIDE_CHECK(verified(e) == at + 1u);
    t = travel_of(e);
    TIDE_CHECK(t.last == at + 1u && t.shown == at + 1u);
    TIDE_CHECK(seen_steps == steps + 1); // The latest tick, not blended with the one before
    TIDE_CHECK(seen_x > x);
    TIDE_CHECK(seen_jumps == 1);
    frames(e, 10, &input);
    TIDE_CHECK(verified(e) == at + 1u);
    // A command sent while paused is in the step's tick
    const Bump value = {.by = 42};
    TIDE_REQUIRE(tide_embed_command(e, bump_type(), 0, 0, &value, sizeof value));
    TIDE_REQUIRE(tide_embed_step(e));
    frames(e, 1, NULL);
    TIDE_CHECK(verified(e) == at + 2u);
    TIDE_CHECK(seen_total == 42);
    for (int i = 0; i < 4; i++) {
        TIDE_CHECK(tide_embed_step(e));
        frames(e, 1, NULL);
    }
    TIDE_CHECK(verified(e) == at + 6u);
    TIDE_CHECK(seen_steps == steps + 6);
    TIDE_REQUIRE(tide_embed_pause(e, false));
    frames(e, 30, NULL);
    TIDE_CHECK(verified(e) >= at + 6u + 14u && verified(e) <= at + 6u + 16u); // Half a second of ticks, no burst
    TIDE_CHECK(travel_of(e).state == TIDE_TRAVEL_LIVE);
    TIDE_CHECK(!tide_embed_step(e)); // Steps are for a paused match
    tide_embed_destroy(e);
}

// A kept tick shows again as it was: its world's hash, and what the views drew of it. Ticks before those kept are
// made again from the start. The match's own local state is left as it was, and what's pressed on another tick's
// picture never reaches the match.
TIDE_TEST(travel_shows_kept_ticks_again)
{
    tide_embed *e = start();
    TIDE_REQUIRE(e != NULL);
    tide_embed_travel_keep(e, 0.5f); // 15 ticks kept
    frames(e, 4, NULL);
    TIDE_REQUIRE(tide_embed_pause(e, true));
    frames(e, 1, NULL);
    const uint32_t from = travel_of(e).last;
    uint64_t hashes[64];
    int steps_at[64];
    float x_at[64];
    tide_embed_input input = {0};
    hold(&input, KEY_D, true);
    for (uint32_t i = 0; i < 40; i++) {
        const tide_embed_travel t = travel_of(e);
        TIDE_REQUIRE(t.shown == from + i);
        hashes[i] = t.hash;
        steps_at[i] = seen_steps;
        x_at[i] = seen_x;
        TIDE_REQUIRE(tide_embed_step(e));
        frames(e, 1, i % 3 ? &input : NULL);
    }
    const uint32_t last = travel_of(e).last;
    TIDE_CHECK(last == from + 40u);
    TIDE_CHECK(travel_of(e).kept == last - 14u);
    const int total = seen_total;
    // Back through the kept ticks, and on before them, made again from the start
    for (uint32_t i = 0; i < 40; i++) {
        const uint32_t tick = from + 39u - i;
        TIDE_REQUIRE(tide_embed_show(e, tick));
        hold(&input, KEY_B, i == 5u); // B down on an old tick's picture: its Bump goes nowhere
        frames(e, 1, &input);
        const tide_embed_travel t = travel_of(e);
        TIDE_CHECK(t.shown == tick);
        TIDE_CHECK(t.hash == hashes[tick - from]);
        TIDE_CHECK(seen_steps == steps_at[tick - from]);
        TIDE_CHECK(seen_x == x_at[tick - from]);
    }
    TIDE_CHECK(!tide_embed_show(e, last + 1u));
    TIDE_CHECK(!tide_embed_show(e, travel_of(e).first - 1u));
    TIDE_CHECK(verified(e) == last); // Shown, the match ran nothing
    hold(&input, KEY_B, false);
    TIDE_REQUIRE(tide_embed_show(e, last));
    frames(e, 1, NULL);
    TIDE_CHECK(seen_total == total);
    // Steps from a tick shown go through the kept ones to the latest, then the match runs its next
    TIDE_REQUIRE(tide_embed_show(e, last - 2u));
    TIDE_REQUIRE(tide_embed_step(e));
    frames(e, 1, NULL);
    TIDE_CHECK(travel_of(e).shown == last - 1u && verified(e) == last);
    TIDE_REQUIRE(tide_embed_step(e));
    TIDE_REQUIRE(tide_embed_step(e));
    frames(e, 1, NULL);
    TIDE_CHECK(travel_of(e).shown == last + 1u && verified(e) == last + 1u);
    // Going on from a tick shown goes on from the latest: the match's, with nothing of the old tick's B
    TIDE_REQUIRE(tide_embed_show(e, from + 3u));
    frames(e, 1, NULL);
    TIDE_REQUIRE(tide_embed_pause(e, false));
    frames(e, 20, NULL);
    TIDE_CHECK(verified(e) >= last + 10u);
    TIDE_CHECK(seen_total == total);
    tide_embed_destroy(e);
}

// The ticks shown are heard: what each sent reaches the local handlers of the frames that show it, which read the
// match as that tick left it and count in a local state of their own, and the packet's EVENTS, once as predicted
// and once as confirmed. A tick shown again says nothing again; ticks come past going on are each heard, in order;
// a tick gone back to is heard alone; and a recording's ticks are heard as it plays.
TIDE_TEST(travel_shown_ticks_are_heard)
{
    tide_embed *e = start();
    TIDE_REQUIRE(e != NULL);
    tide_embed_travel_keep(e, 0.5f); // 15 ticks kept
    frames(e, 10, NULL);
    TIDE_REQUIRE(tide_embed_pause(e, true));
    frames(e, 1, NULL);
    const uint32_t from = travel_of(e).last;
    heard_bumps = 0;
    // 40 steps, a Bump in the ticks that make the worlds after from + 6, + 21 and + 31 ticks
    for (int i = 0; i < 40; i++) {
        if (i == 5 || i == 20 || i == 30) {
            const Bump value = {.by = i};
            TIDE_REQUIRE(tide_embed_command(e, bump_type(), 0, 0, &value, sizeof value));
        }
        TIDE_REQUIRE(tide_embed_step(e));
        frames(e, 1, NULL);
    }
    TIDE_CHECK(heard_bumps == 3 && heard_count == 3 && heard_total == 55); // The match's own local handlers, live
    const uint32_t last = travel_of(e).last;
    TIDE_REQUIRE(last == from + 40u);
    int predicted, confirmed, by = 0;
    uint32_t tick = 0;

    // Back before the ticks kept, to the world the first Bump's tick made
    heard_bumps = 0;
    TIDE_REQUIRE(tide_embed_show(e, from + 6u));
    frames(e, 1, NULL);
    TIDE_CHECK(heard_bumps == 1 && heard_by == 5);
    TIDE_CHECK(heard_count == 1);  // The first its local state heard: not the match's, which had heard three
    TIDE_CHECK(heard_total == 5);  // The match as that tick left it
    packet_events(e, &predicted, &confirmed, &tick, &by);
    TIDE_CHECK(predicted == 1 && confirmed == 1 && by == 5);
    TIDE_CHECK(tick == from + 5u); // The tick that sent it: the one that made the world shown
    frames(e, 3, NULL); // Shown again: said once
    TIDE_CHECK(heard_bumps == 1);
    packet_events(e, &predicted, &confirmed, &tick, &by);
    TIDE_CHECK(predicted == 0 && confirmed == 0);
    TIDE_CHECK(travel_of(e).hash != 0 && seen_total == 5);

    // The tick before it sent nothing; a step on from it hears the Bump again
    TIDE_REQUIRE(tide_embed_show(e, from + 5u));
    frames(e, 1, NULL);
    TIDE_CHECK(heard_bumps == 1 && seen_total == 0);
    TIDE_REQUIRE(tide_embed_step(e));
    frames(e, 1, NULL);
    TIDE_CHECK(heard_bumps == 2 && heard_by == 5 && heard_count == 2 && seen_total == 5);
    // Steps on, each tick heard as it's come to
    for (uint32_t at = from + 7u; at <= from + 21u; at++) {
        TIDE_CHECK(heard_bumps == 2);
        TIDE_REQUIRE(tide_embed_step(e));
        frames(e, 1, NULL);
    }
    TIDE_CHECK(travel_of(e).shown == from + 21u);
    TIDE_CHECK(heard_bumps == 3 && heard_by == 20 && heard_total == 25);
    // Several ticks on at once: those come past are heard too
    TIDE_REQUIRE(tide_embed_show(e, from + 39u));
    frames(e, 1, NULL);
    TIDE_CHECK(heard_bumps == 4 && heard_by == 30 && heard_total == 55 && seen_total == 55);
    packet_events(e, &predicted, &confirmed, &tick, &by);
    TIDE_CHECK(predicted == 1 && confirmed == 1 && by == 30 && tick == from + 30u);
    // Back: the tick landed on alone, which sent nothing here, and then one that did
    TIDE_REQUIRE(tide_embed_show(e, from + 25u));
    frames(e, 1, NULL);
    TIDE_CHECK(heard_bumps == 4);
    TIDE_REQUIRE(tide_embed_show(e, from + 21u));
    frames(e, 1, NULL);
    TIDE_CHECK(heard_bumps == 5 && heard_by == 20);
    TIDE_CHECK(verified(e) == last); // The match ran nothing meanwhile

    // The match's own local state heard none of that: its next Bump is its fourth
    TIDE_REQUIRE(tide_embed_show(e, last));
    frames(e, 1, NULL);
    TIDE_CHECK(heard_bumps == 5);
    const Bump one = {.by = 1};
    TIDE_REQUIRE(tide_embed_command(e, bump_type(), 0, 0, &one, sizeof one));
    TIDE_REQUIRE(tide_embed_step(e));
    frames(e, 1, NULL);
    TIDE_CHECK(heard_bumps == 6 && heard_by == 1 && heard_count == 4 && heard_total == 56);

    // A recording of it, played: its four Bumps, in order, each with the match as its tick left it
    uint32_t size = 0;
    const uint8_t *saved = tide_embed_recording(e, &size);
    TIDE_REQUIRE(saved != NULL && size > 0);
    uint8_t *copy = malloc(size);
    TIDE_REQUIRE(copy != NULL);
    memcpy(copy, saved, size);
    TIDE_REQUIRE(tide_embed_replay(e, copy, size));
    free(copy);
    heard_bumps = 0;
    int packets = 0, order = 0;
    static const int bys[4] = {5, 20, 30, 1}, totals[4] = {5, 25, 55, 56};
    TIDE_REQUIRE(tide_embed_pause(e, false));
    for (uint32_t frame = 0; frame < 2u * (last + 1u) + 20u; frame++) {
        const int before = heard_bumps;
        frames(e, 1, NULL);
        if (heard_bumps == before) continue;
        TIDE_REQUIRE(heard_bumps == before + 1 && before < 4);
        order += heard_by == bys[before] && heard_total == totals[before];
        packet_events(e, &predicted, &confirmed, &tick, &by);
        packets += predicted == 1 && confirmed == 1 && by == bys[before] && tick + 1u == travel_of(e).shown;
    }
    TIDE_CHECK(!travel_of(e).playing && travel_of(e).shown == last + 1u);
    TIDE_CHECK(heard_bumps == 4 && order == 4 && packets == 4);
    TIDE_CHECK(travel_of(e).differs == 0 && travel_of(e).checked == last + 1u);
    tide_embed_destroy(e);
}

// Each of the match's ticks' hashes, from first to last, as the match made them
static uint32_t live_hashes(tide_embed *e, uint64_t *out, const uint32_t room)
{
    const tide_embed_travel t = travel_of(e);
    uint32_t n = 0;
    for (uint32_t tick = t.first; tick <= t.last && n < room; tick++) {
        if (!tide_embed_show(e, tick)) return 0;
        out[n++] = travel_of(e).hash;
    }
    return n;
}

// Plays a few seconds of a match with input and commands, B's from the views among them
static void play(tide_embed *e)
{
    tide_embed_input input = {0};
    for (int i = 0; i < 180; i++) {
        hold(&input, KEY_D, i % 40 < 25);
        hold(&input, KEY_SPACE, i % 17 == 0);
        hold(&input, KEY_B, i % 50 == 10);
        if (i % 30 == 7) {
            const Bump value = {.by = i};
            TIDE_CHECK(tide_embed_command(e, bump_type(), 0, 0, &value, sizeof value));
        }
        frames(e, 1, &input);
    }
}

// A recording replays bit for bit: every tick's world has the hash the match's had, inputs, joins and commands
// in it; from bytes that are only a copy, as a tester's would be
TIDE_TEST(travel_records_and_replays_bit_for_bit)
{
    tide_embed *e = start();
    TIDE_REQUIRE(e != NULL);
    tide_embed_travel_keep(e, 100.0f);
    play(e);
    TIDE_CHECK(seen_total > 0);
    TIDE_REQUIRE(tide_embed_pause(e, true));
    frames(e, 1, NULL);
    static uint64_t hashes[512];
    const uint32_t count = live_hashes(e, hashes, 512);
    TIDE_REQUIRE(count > 80u);
    const tide_embed_travel live = travel_of(e);
    uint32_t size = 0;
    const uint8_t *saved = tide_embed_recording(e, &size);
    TIDE_REQUIRE(saved != NULL && size > 0);
    uint8_t *copy = malloc(size);
    TIDE_REQUIRE(copy != NULL);
    memcpy(copy, saved, size);
    TIDE_REQUIRE(tide_embed_replay(e, copy, size));
    tide_embed_travel t = travel_of(e);
    TIDE_CHECK(t.state == TIDE_TRAVEL_REPLAY);
    TIDE_CHECK(t.first == live.first && t.last == live.last);
    TIDE_CHECK(t.shown == t.first && !t.playing && !t.other_build);
    TIDE_CHECK(t.hash == hashes[0]);
    // Played at its tick rate, a tick every two frames, each checked as it's made
    TIDE_REQUIRE(tide_embed_pause(e, false));
    frames(e, 2 * (int)count + 10, NULL);
    t = travel_of(e);
    TIDE_CHECK(!t.playing && t.shown == t.last);
    TIDE_CHECK(t.checked == t.last);
    TIDE_CHECK(t.differs == 0 && t.unreadable == 0);
    for (uint32_t i = 0; i < count; i++) {
        TIDE_REQUIRE(tide_embed_show(e, t.first + i));
        TIDE_CHECK(travel_of(e).hash == hashes[i]);
    }
    // Stepped, and shown out of order: the same worlds
    TIDE_REQUIRE(tide_embed_show(e, t.first + 3u));
    TIDE_REQUIRE(tide_embed_step(e));
    TIDE_CHECK(travel_of(e).hash == hashes[4]);
    TIDE_CHECK(!tide_embed_show(e, t.last + 1u));
    frames(e, 1, NULL);
    TIDE_CHECK(seen_steps >= 0);
    // Back to the match, paused where it was
    tide_embed_replay_end(e);
    frames(e, 1, NULL);
    t = travel_of(e);
    TIDE_CHECK(t.state == TIDE_TRAVEL_PAUSED);
    TIDE_CHECK(t.last == live.last);
    tide_embed_destroy(e);

    // ...and in another embed, in no match, as a tester's recording opens on another machine
    e = start();
    TIDE_REQUIRE(e != NULL);
    TIDE_REQUIRE(tide_embed_request(e, TIDE_REQUEST_LEAVE, NULL, 0));
    frames(e, 2, NULL);
    TIDE_CHECK(strstr(travel_of(e).why, "no match") != NULL);
    TIDE_REQUIRE(tide_embed_replay(e, copy, size)); // Time travel off: a recording still shows
    for (uint32_t i = 0; i < count; i++) {
        TIDE_REQUIRE(tide_embed_show(e, live.first + i));
        TIDE_CHECK(travel_of(e).hash == hashes[i]);
    }
    frames(e, 1, NULL);
    TIDE_CHECK(seen_total >= 0); // The views drew it, with no match of this machine's
    free(copy);
    tide_embed_destroy(e);
}

// The bytes of record `index` in a recording: past its head, first world and count, as timeline.c lays them out
static uint8_t *record_in(uint8_t *recording, const uint32_t size, const uint32_t index, uint32_t *record_size)
{
    uint32_t at = 8u + 4u + 8u + 32u + 4u + 4u + 4u + 4u;
    uint32_t n;
    for (int i = 0; i < 3; i++) { // Its layout, its first world, and that world as its layout has it
        memcpy(&n, recording + at, 4);
        at += 4u + n;
    }
    uint32_t count;
    memcpy(&count, recording + at, 4);
    at += 4u;
    for (uint32_t i = 0; i < count && at + 4u <= size; i++) {
        memcpy(&n, recording + at, 4);
        if (i == index) {
            *record_size = n;
            return recording + at + 4u;
        }
        at += 4u + n;
    }
    return NULL;
}

// A replay says the first tick whose world isn't the recording's, with both hashes, and goes on from it; a
// recording of another layout, or cut short, isn't taken, saying why
TIDE_TEST(travel_replay_says_where_it_differs)
{
    tide_embed *e = start();
    TIDE_REQUIRE(e != NULL);
    tide_embed_travel_keep(e, 100.0f);
    play(e);
    TIDE_REQUIRE(tide_embed_pause(e, true));
    frames(e, 1, NULL);
    uint32_t size = 0;
    const uint8_t *saved = tide_embed_recording(e, &size);
    TIDE_REQUIRE(saved != NULL);
    uint8_t *copy = malloc(size);
    TIDE_REQUIRE(copy != NULL);
    memcpy(copy, saved, size);
    const uint32_t first = travel_of(e).first;
    // The hash the server's world had after tick 40, in its record (the frame's first 8 bytes, past tick and size)
    uint32_t record_size = 0;
    uint8_t *record = record_in(copy, size, 40, &record_size);
    TIDE_REQUIRE(record != NULL && record_size > 16u);
    uint64_t expected;
    memcpy(&expected, record + 8, 8);
    record[8] ^= 1u;
    TIDE_REQUIRE(tide_embed_replay(e, copy, size));
    TIDE_REQUIRE(tide_embed_show(e, travel_of(e).last));
    tide_embed_travel t = travel_of(e);
    TIDE_CHECK(t.differs == first + 41u);
    TIDE_CHECK(t.expected == (expected ^ 1u));
    TIDE_CHECK(t.got == expected);
    TIDE_CHECK(t.unreadable == 0);
    TIDE_CHECK(t.checked == t.last); // It went on past it
    tide_embed_replay_end(e);

    record[8] ^= 1u;
    // Another layout's hash: the recording says its layout, so its worlds are carried over by name (here to the same
    // fields) and its ticks run unchecked, where a recording without one was refused (travel_layout.c has a real one)
    copy[8 + 4] ^= 1u;
    TIDE_REQUIRE(tide_embed_replay(e, copy, size));
    TIDE_CHECK(travel_of(e).other_build == 2u);
    TIDE_REQUIRE(tide_embed_show(e, travel_of(e).last));
    t = travel_of(e);
    TIDE_CHECK(t.differs == 0 && t.unreadable == 0 && t.checked == t.first);
    tide_embed_replay_end(e);
    copy[8 + 4] ^= 1u;
    TIDE_CHECK(!tide_embed_replay(e, copy, size - 3u));
    TIDE_CHECK(strstr(travel_of(e).refused, "cut short") != NULL);
    TIDE_CHECK(!tide_embed_replay(e, copy, 20u));
    TIDE_CHECK(tide_embed_replay(e, copy, size));
    TIDE_CHECK(travel_of(e).refused[0] == '\0');
    free(copy);
    tide_embed_destroy(e);
}

// For platform/tests/travel_layout.c, a build of this game laid out otherwise (TIDE_TRAVEL_RECORDING, set by ctest):
// the recording shown, and beside it what the views drew of its first tick, of `middle` and of its last
static void leave_recording(tide_embed *e, const uint8_t *bytes, const uint32_t size, const uint32_t middle)
{
    const char *path = getenv("TIDE_TRAVEL_RECORDING");
    if (!path) return;
    FILE *f = fopen(path, "wb");
    TIDE_REQUIRE(f != NULL);
    TIDE_CHECK(fwrite(bytes, 1, size, f) == size);
    fclose(f);
    char said[1100];
    snprintf(said, sizeof said, "%s.txt", path);
    f = fopen(said, "w");
    TIDE_REQUIRE(f != NULL);
    const tide_embed_travel t = travel_of(e);
    const uint32_t ticks[3] = {t.first, middle, t.last};
    for (int i = 0; i < 3; i++) {
        TIDE_REQUIRE(tide_embed_show(e, ticks[i]));
        seen_steps = seen_jumps = seen_total = -1;
        seen_x = 0.0f;
        frames(e, 1, NULL);
        uint32_t bits;
        memcpy(&bits, &seen_x, sizeof bits);
        fprintf(f, "%u %d %u %d %d\n", (unsigned)ticks[i], seen_steps, (unsigned)bits, seen_jumps, seen_total);
    }
    fclose(f);
}

// Going on from a tick shown: the match goes back there, the ticks after it let go, a command applied after it
// doesn't come again, and the recording, which goes on from there, replays bit for bit
TIDE_TEST(travel_continues_from_a_tick_shown)
{
    tide_embed *e = start();
    TIDE_REQUIRE(e != NULL);
    tide_embed_travel_keep(e, 100.0f);
    frames(e, 40, NULL);
    TIDE_REQUIRE(tide_embed_pause(e, true));
    frames(e, 1, NULL);
    const uint32_t back = travel_of(e).last;
    const uint64_t back_hash = travel_of(e).hash;
    const int back_steps = seen_steps;
    TIDE_REQUIRE(tide_embed_pause(e, false));
    const Bump value = {.by = 1000};
    TIDE_REQUIRE(tide_embed_command(e, bump_type(), 0, 0, &value, sizeof value));
    frames(e, 40, NULL);
    TIDE_CHECK(seen_total == 1000);
    TIDE_REQUIRE(tide_embed_pause(e, true));
    frames(e, 1, NULL);
    const uint32_t ahead = travel_of(e).last;
    TIDE_REQUIRE(tide_embed_show(e, back));
    frames(e, 1, NULL);
    TIDE_CHECK(seen_total == 0);
    TIDE_REQUIRE(tide_embed_continue(e));
    frames(e, 1, NULL);
    tide_embed_travel t = travel_of(e);
    TIDE_CHECK(t.state == TIDE_TRAVEL_LIVE);
    TIDE_CHECK(t.last <= back + 1u); // The ticks after it are gone
    TIDE_CHECK(seen_total == 0);     // ...and the Bump with them, which doesn't come again
    TIDE_CHECK(seen_steps <= back_steps + 1);
    TIDE_CHECK(verified(e) >= ahead); // The server's count of ticks goes on
    frames(e, 60, NULL);
    TIDE_CHECK(seen_total == 0);
    const Bump again = {.by = 3};
    TIDE_REQUIRE(tide_embed_command(e, bump_type(), 0, 0, &again, sizeof again));
    frames(e, 20, NULL);
    TIDE_CHECK(seen_total == 3); // A command sent after it goes in once
    TIDE_REQUIRE(tide_embed_pause(e, true));
    frames(e, 1, NULL);
    static uint64_t hashes[512];
    const uint32_t count = live_hashes(e, hashes, 512);
    TIDE_REQUIRE(count > 0);
    TIDE_REQUIRE(tide_embed_show(e, back));
    TIDE_CHECK(travel_of(e).hash != back_hash); // Its commands are the match's as it went back: a jump
    uint32_t size = 0;
    const uint8_t *saved = tide_embed_recording(e, &size);
    TIDE_REQUIRE(saved != NULL);
    uint8_t *copy = malloc(size);
    TIDE_REQUIRE(copy != NULL);
    memcpy(copy, saved, size);
    TIDE_REQUIRE(tide_embed_replay(e, copy, size));
    t = travel_of(e);
    for (uint32_t i = 0; i < count; i++) {
        TIDE_REQUIRE(tide_embed_show(e, t.first + i));
        TIDE_CHECK(travel_of(e).hash == hashes[i]);
    }
    t = travel_of(e);
    TIDE_CHECK(t.differs == 0 && t.unreadable == 0 && t.checked == t.last);
    leave_recording(e, copy, size, back);
    free(copy);
    tide_embed_destroy(e);
}

// ---------------------------------------------------------------------------
// Another machine in the embed's match: the game's own network (tide/network.h), a loopback here

static tide_loopback *guest_net;
static double guest_now;

static bool guest_host(void *self, tide_transport *out)
{
    (void)self;
    *out = tide_loopback_endpoint(guest_net, 1);
    return true;
}

static bool guest_join(void *self, const char *address, tide_transport *out, tide_address *server)
{
    (void)self, (void)address, (void)out, (void)server;
    return false;
}

static void guest_address(void *self, char *out, const size_t size)
{
    (void)self;
    snprintf(out, size, "loopback");
}

static const tide_network guest_network = {.host = guest_host, .join = guest_join, .address = guest_address};

// `count` frames of the embed, and of the other machine's session beside it
static void both(tide_embed *e, tide_session *guest, const int count)
{
    for (int i = 0; i < count; i++) {
        guest_now += 1.0 / 60.0;
        tide_loopback_set_time(guest_net, guest_now);
        frames(e, 1, NULL);
        tide_session_update(guest, guest_now);
    }
}

static const tide_world *guest_verified(const tide_session *guest)
{
    return tide_session_view(guest).verified;
}

// With another machine in the match, a pause never stops it: the match goes on behind the tick shown, and going
// on shows its latest. A machine joining a paused match finds it going. Going on from a tick shown takes the other
// machine back too.
TIDE_TEST(travel_never_stops_a_match_for_other_machines)
{
    guest_net = tide_loopback_create(3);
    guest_now = 0.0;
    tide_network_set(&guest_network);
    tide_embed *e = start();
    TIDE_REQUIRE(e != NULL);
    tide_embed_travel_keep(e, 100.0f);
    tide_session *guest = tide_session_create(&(tide_session_desc){.game = &tide_game_api, .sample = sample_none});
    both(e, guest, 20);
    TIDE_REQUIRE(tide_embed_pause(e, true)); // Nobody else in it: a pause
    both(e, guest, 2);
    const uint32_t paused_at = verified(e);
    TIDE_CHECK(!travel_of(e).background);
    TIDE_REQUIRE(tide_embed_request(e, TIDE_REQUEST_OPEN, NULL, 0));
    both(e, guest, 2);
    tide_session_join(guest, tide_loopback_endpoint(guest_net, 2), tide_loopback_address(1), guest_now);
    both(e, guest, 60);
    TIDE_REQUIRE(tide_session_status_of(guest).client.state == TIDE_SESSION_CONNECTED); // The match went on for it
    tide_embed_travel t = travel_of(e);
    TIDE_CHECK(t.state == TIDE_TRAVEL_PAUSED && t.background);
    TIDE_CHECK(t.shown == paused_at); // ...behind the tick shown
    TIDE_CHECK(verified(e) > paused_at + 20u);
    const int shown_steps = seen_steps;
    both(e, guest, 60);
    TIDE_CHECK(seen_steps == shown_steps);
    TIDE_CHECK(verified(e) > paused_at + 45u);
    TIDE_CHECK(tide_session_status_of(guest).client.verified_tick + 10u >= verified(e));
    // What the match sends behind the tick shown, its own local handlers hear; the packets, which show that tick, don't
    const Bump quiet = {.by = 0};
    heard_bumps = 0;
    TIDE_REQUIRE(tide_embed_command(e, bump_type(), 0, 0, &quiet, sizeof quiet));
    int in_packets = 0;
    for (int i = 0; i < 20; i++) {
        int predicted, confirmed, by = 0;
        uint32_t tick = 0;
        both(e, guest, 1);
        packet_events(e, &predicted, &confirmed, &tick, &by);
        in_packets += predicted + confirmed;
    }
    TIDE_CHECK(heard_bumps == 1 && in_packets == 0);
    TIDE_REQUIRE(tide_embed_step(e)); // Through the ticks kept, as the match goes on by itself
    both(e, guest, 1);
    TIDE_CHECK(travel_of(e).shown == paused_at + 1u);
    TIDE_CHECK(seen_steps == 0); // The tick the other machine's player came in at, which its walker drew last
    // A Bump while it goes on, then back to before it
    TIDE_REQUIRE(tide_embed_pause(e, false));
    both(e, guest, 2);
    TIDE_CHECK(travel_of(e).state == TIDE_TRAVEL_LIVE);
    const uint32_t back = travel_of(e).last;
    const Bump value = {.by = 500};
    TIDE_REQUIRE(tide_embed_command(e, bump_type(), 0, 0, &value, sizeof value));
    both(e, guest, 40);
    TIDE_REQUIRE(guest_verified(guest) && guest_verified(guest)->Score.total == 500);
    TIDE_REQUIRE(tide_embed_pause(e, true));
    TIDE_REQUIRE(tide_embed_show(e, back));
    TIDE_REQUIRE(tide_embed_continue(e));
    both(e, guest, 60);
    TIDE_CHECK(seen_total == 0);
    TIDE_REQUIRE(guest_verified(guest) != NULL);
    TIDE_CHECK(guest_verified(guest)->Score.total == 0); // It went back too
    TIDE_CHECK(tide_session_status_of(guest).client.state == TIDE_SESSION_CONNECTED);
    TIDE_CHECK(tide_session_status_of(guest).client.verified_tick + 10u >= verified(e));
    tide_embed_destroy(e);
    tide_session_destroy(guest);
    tide_network_set(NULL);
    tide_loopback_destroy(guest_net);
}

// The embed's machine joins a match another machine runs: through endpoint 2, to the match at 1
static bool away_host(void *self, tide_transport *out)
{
    (void)self;
    *out = tide_loopback_endpoint(guest_net, 3); // Its own first match, which it leaves
    return true;
}

// A game's network's addresses are its own kind (tide_network_match_join): the loopback's under them
static tide_transport away_inner;

static void away_send(void *self, const tide_address to, const void *data, const uint32_t size)
{
    (void)self;
    away_inner.send(away_inner.self, tide_loopback_address(to.host), data, size);
}

static uint32_t away_receive(void *self, tide_address *from, void *data, const uint32_t capacity)
{
    (void)self;
    const uint32_t size = away_inner.receive(away_inner.self, from, data, capacity);
    if (size) from->kind = TIDE_ADDRESS_NETWORK;
    return size;
}

static bool away_join(void *self, const char *address, tide_transport *out, tide_address *server)
{
    (void)self, (void)address;
    away_inner = tide_loopback_endpoint(guest_net, 2);
    *out = (tide_transport){.send = away_send, .receive = away_receive};
    *server = tide_loopback_address(1);
    return true;
}

static const tide_network away_network = {.host = away_host, .join = away_join, .address = guest_address};

// A match another machine runs, in a game that hides nothing, is recorded here from what this machine is sent: every
// tick since it came in, each world the server's; a pause shows a tick while the match goes on, recorded behind it;
// its ticks show again with what each sent, those before the ones kept made again; it isn't this machine's to take
// back; and its recording, saved here, replays bit for bit
TIDE_TEST(travel_records_another_machines_match)
{
    guest_net = tide_loopback_create(3);
    guest_now = 0.0;
    tide_network_set(&away_network);
    tide_session *host = tide_session_create(&(tide_session_desc){.game = &tide_game_api, .sample = sample_none});
    tide_session_start(host, NULL, guest_now);
    tide_session_open(host, tide_loopback_endpoint(guest_net, 1));
    tide_embed *e = start();
    TIDE_REQUIRE(e != NULL);
    tide_embed_travel_keep(e, 1.0f); // 30 ticks kept
    both(e, host, 5);
    TIDE_REQUIRE(tide_embed_request(e, TIDE_REQUEST_JOIN, "loopback", 8));
    static uint64_t host_hashes[1024];
    #define BOTH_NOTING(count) \
        for (int n = 0; n < (count); n++) { \
            both(e, host, 1); \
            const uint32_t at = tide_session_status_of(host).client.verified_tick; \
            if (at < 1024u) host_hashes[at] = tide_game_api.hash_world(tide_session_server_world(host)); \
        }
    BOTH_NOTING(90);
    tide_embed_status status;
    tide_embed_status_of(e, &status);
    TIDE_REQUIRE(status.state == TIDE_SESSION_CONNECTED && !status.server);
    tide_embed_travel t = travel_of(e);
    TIDE_CHECK(t.state == TIDE_TRAVEL_LIVE && t.followed == 3u && t.why[0] == '\0');
    TIDE_CHECK(t.last == verified(e)); // Up to the latest tick it had confirmed...
    const uint32_t first = t.first;
    TIDE_CHECK(first > 0u && t.kept == t.last - 29u && t.kept > first); // ...from where it came in
    // A Bump, which the other machine's match takes
    const Bump value = {.by = 7};
    TIDE_REQUIRE(tide_embed_command(e, bump_type(), 0, 0, &value, sizeof value));
    BOTH_NOTING(20);
    TIDE_CHECK(seen_total == 7);
    TIDE_REQUIRE(tide_embed_pause(e, true));
    BOTH_NOTING(1);
    t = travel_of(e);
    TIDE_CHECK(t.state == TIDE_TRAVEL_PAUSED && t.background && t.followed == 3u);
    const uint32_t held = t.shown;
    TIDE_CHECK(held == t.last);
    const int steps = seen_steps;
    BOTH_NOTING(60); // The match goes on behind the tick shown, recorded as it does
    TIDE_CHECK(verified(e) > held + 20u);
    t = travel_of(e);
    TIDE_CHECK(t.shown == held && t.last == verified(e) && t.first == first);
    TIDE_CHECK(seen_steps == steps);
    TIDE_CHECK(tide_session_status_of(host).client.state == TIDE_SESSION_CONNECTED);
    // Back through its ticks to where this machine came in, those before the ones kept made again from there: each the
    // server's world, and the Bump's tick heard where it's landed on
    heard_bumps = 0;
    uint32_t bumped = 0;
    int before = 7, same = 0, in_packets = 0;
    for (uint32_t tick = held; tick >= first && tick <= held; tick--) {
        TIDE_REQUIRE(tide_embed_show(e, tick));
        BOTH_NOTING(1);
        t = travel_of(e);
        same += t.shown == tick && t.hash == host_hashes[tick];
        if (seen_total != before) {
            bumped = tick + 1u; // The world before it: the one after has the Bump
            before = seen_total;
        }
        int predicted, confirmed, by = 0;
        uint32_t sent = 0;
        packet_events(e, &predicted, &confirmed, &sent, &by);
        in_packets += predicted == 1 && confirmed == 1 && by == 7 && sent + 1u == tick;
    }
    TIDE_CHECK(same == (int)(held - first + 1u));
    TIDE_CHECK(bumped > first && bumped <= held && before == 0);
    TIDE_CHECK(heard_bumps == 1 && heard_by == 7 && heard_total == 7 && in_packets == 1);
    TIDE_CHECK(!tide_embed_show(e, first - 1u)); // Nothing from before it came in
    // On a step at a time, each tick heard as it's come to; at the latest, the match steps by itself
    TIDE_REQUIRE(tide_embed_show(e, bumped - 1u));
    BOTH_NOTING(1);
    TIDE_CHECK(heard_bumps == 1 && seen_total == 0);
    TIDE_REQUIRE(tide_embed_step(e));
    BOTH_NOTING(1);
    TIDE_CHECK(heard_bumps == 2 && seen_total == 7);
    TIDE_REQUIRE(tide_embed_show(e, travel_of(e).last));
    TIDE_CHECK(!tide_embed_step(e));
    // Its match isn't this machine's to take back
    TIDE_CHECK(!tide_embed_continue(e));
    TIDE_CHECK(tide_session_status_of(host).client.verified_tick + 10u >= verified(e));
    // ...but its recording is this machine's to save, from where it came in: it replays bit for bit, every tick's
    // world the one the server's machine had
    uint32_t size = 0;
    const uint8_t *saved = tide_embed_recording(e, &size);
    TIDE_REQUIRE(saved != NULL && size > 0);
    uint8_t *copy = malloc(size);
    TIDE_REQUIRE(copy != NULL);
    memcpy(copy, saved, size);
    const uint32_t recorded_to = travel_of(e).last;
    TIDE_REQUIRE(tide_embed_replay(e, copy, size));
    free(copy);
    t = travel_of(e);
    TIDE_CHECK(t.state == TIDE_TRAVEL_REPLAY && t.first == first && t.last == recorded_to && !t.other_build);
    same = 0;
    for (uint32_t tick = first; tick <= recorded_to; tick++) {
        TIDE_REQUIRE(tide_embed_show(e, tick));
        same += travel_of(e).hash == host_hashes[tick];
    }
    TIDE_CHECK(same == (int)(recorded_to - first + 1u));
    t = travel_of(e);
    TIDE_CHECK(t.checked == t.last && t.differs == 0 && t.unreadable == 0);
    tide_embed_replay_end(e);
    // Going on: its latest again, the same recording going on
    TIDE_REQUIRE(tide_embed_pause(e, false));
    BOTH_NOTING(10);
    t = travel_of(e);
    TIDE_CHECK(t.state == TIDE_TRAVEL_LIVE && t.followed == 3u);
    TIDE_CHECK(t.last == verified(e) && t.first == first);
    tide_embed_status_of(e, &status);
    TIDE_CHECK(status.state == TIDE_SESSION_CONNECTED);
    #undef BOTH_NOTING
    tide_embed_destroy(e);
    tide_session_destroy(host);
    tide_network_set(NULL);
    tide_loopback_destroy(guest_net);
}

// ---------------------------------------------------------------------------
// Several machines over loopback: the timeline recording the server's machine's session

static tide_loopback *net;
static double now;
static tide_session *machines[3];
static tide_local locals[3];
static int indices[3] = {0, 1, 2};

static void sample(void *user, const uint32_t tick, void *input)
{
    const int m = *(const int *)user;
    Move *move = input;
    move->right = (float)((tick / 7u + (uint32_t)m) % 2u);
    move->jump = tick % (11u + (uint32_t)m) == 0u;
}

static void run_for(const double seconds)
{
    for (const double until = now + seconds; now < until; now += 1.0 / 60.0) {
        tide_loopback_set_time(net, now);
        for (int i = 0; i < 3; i++) tide_session_update(machines[i], now);
    }
}

// A Bump of `by` from machine `m`, packed as its local code's Send would be
static void bump_from(const int m, const int by)
{
    tide_local_set_session(&locals[m], TIDE_SESSION_CONNECTED, tide_session_status_of(machines[m]).client.player, 0,
                           m == 0, m == 0, "");
    const Bump value = {.by = by};
    TIDE_REQUIRE(tide_game_embed.command(&locals[m], (int32_t)bump_type(), 0, 0, &value, sizeof value));
    uint32_t size = 0;
    const uint8_t *bytes = tide_local_command(&locals[m], 0, &size);
    TIDE_REQUIRE(bytes != NULL);
    TIDE_CHECK(tide_session_send_command(machines[m], bytes, size));
    tide_local_command_sent(&locals[m], 0);
}

static void machines_start(tide_timeline *timeline)
{
    now = 0.0;
    net = tide_loopback_create(9);
    tide_loopback_set_conditions(net, (tide_net_conditions){.latency = 0.02, .jitter = 0.01, .loss = 0.05});
    for (int i = 0; i < 3; i++) {
        machines[i] = tide_session_create(&(tide_session_desc){.game = &tide_game_api, .sample = sample, .user = &indices[i]});
        tide_local_init(&locals[i]);
    }
    const tide_session_recorder recorder = tide_timeline_recorder(timeline);
    tide_session_record(machines[0], &recorder); // From the match's start
    tide_session_start(machines[0], NULL, now);
    tide_session_open(machines[0], tide_loopback_endpoint(net, 1));
    tide_session_join(machines[1], tide_loopback_endpoint(net, 2), tide_loopback_address(1), now);
    tide_session_join(machines[2], tide_loopback_endpoint(net, 3), tide_loopback_address(1), now);
}

static void machines_stop(void)
{
    for (int i = 0; i < 3; i++) {
        tide_session_destroy(machines[i]);
        tide_local_free(&locals[i]);
    }
    tide_loopback_destroy(net);
}

static const tide_world *server_world(void)
{
    return tide_session_server_world(machines[0]);
}

// Three players' inputs, joins and commands, recorded on the server's machine, replay bit for bit
TIDE_TEST(timeline_replays_several_players_and_commands)
{
    tide_timeline *timeline = tide_timeline_create(&(tide_timeline_desc){.game = &tide_game_api, .keep = 1000});
    TIDE_REQUIRE(timeline != NULL);
    machines_start(timeline);
    run_for(1.0);
    for (int i = 0; i < 6; i++) {
        bump_from(1 + i % 2, 10 + i);
        run_for(0.25);
    }
    run_for(1.0);
    TIDE_REQUIRE(tide_session_status_of(machines[2]).client.state == TIDE_SESSION_CONNECTED);
    TIDE_CHECK(server_world()->Score.total == 10 + 11 + 12 + 13 + 14 + 15);
    TIDE_CHECK(tide_session_remote_players(machines[0]) == 2u);
    TIDE_CHECK(tide_session_remote_players(machines[1]) == 0u);
    const uint32_t first = tide_timeline_first(timeline), last = tide_timeline_last(timeline);
    TIDE_REQUIRE(tide_timeline_started(timeline) && first == 0u && last > 100u);
    TIDE_CHECK(last == tide_session_status_of(machines[0]).client.verified_tick);
    TIDE_CHECK(tide_game_api.hash_world(tide_timeline_world(timeline, last)) == tide_game_api.hash_world(server_world()));
    static uint64_t hashes[1024];
    for (uint32_t tick = first; tick <= last; tick++) hashes[tick] = tide_game_api.hash_world(tide_timeline_world(timeline, tick));
    uint32_t size = 0;
    uint8_t *bytes = tide_timeline_save(timeline, 30, 1, &size);
    TIDE_REQUIRE(bytes != NULL);
    machines_stop();
    tide_timeline_destroy(timeline);

    tide_timeline_about about;
    char why[128];
    tide_timeline *replayed = tide_timeline_load(&(tide_timeline_desc){.game = &tide_game_api, .keep = 8}, bytes, size,
                                                 &about, why, sizeof why);
    TIDE_REQUIRE(replayed != NULL);
    TIDE_CHECK(about.tick_rate == 30u && about.player == 1u && !about.other_build);
    TIDE_CHECK(tide_timeline_first(replayed) == first && tide_timeline_last(replayed) == last);
    for (uint32_t tick = first; tick <= last; tick++) {
        const void *world = tide_timeline_world(replayed, tick);
        TIDE_REQUIRE(world != NULL);
        TIDE_CHECK(tide_game_api.hash_world(world) == hashes[tick]);
    }
    tide_timeline_check check;
    tide_timeline_check_of(replayed, &check);
    TIDE_CHECK(check.checked == last && check.differs == 0 && check.unreadable == 0);
    // Back before the 8 it keeps: made again from the start, the same
    TIDE_CHECK(tide_game_api.hash_world(tide_timeline_world(replayed, first + 5u)) == hashes[first + 5u]);
    TIDE_CHECK(tide_game_api.hash_world(tide_timeline_world(replayed, first + 6u)) == hashes[first + 6u]);
    TIDE_CHECK(tide_timeline_world(replayed, last + 1u) == NULL);
    tide_timeline_destroy(replayed);
    free(bytes);
}

// A build whose code changed (the same layout) replays to the first tick it changes: there, and on from it
static tide_game changed;
static uint32_t changed_ticks;

static void changed_tick(void *world, const tide_jobs *jobs)
{
    tide_game_api.tick(world, jobs);
    if (++changed_ticks == 50u) ((tide_world *)world)->Score.total += 1;
}

TIDE_TEST(timeline_says_where_a_changed_build_differs)
{
    tide_timeline *timeline = tide_timeline_create(&(tide_timeline_desc){.game = &tide_game_api});
    TIDE_REQUIRE(timeline != NULL);
    machines_start(timeline);
    run_for(3.0);
    uint32_t size = 0;
    uint8_t *bytes = tide_timeline_save(timeline, 30, 1, &size);
    TIDE_REQUIRE(bytes != NULL);
    const uint32_t last = tide_timeline_last(timeline);
    machines_stop();
    tide_timeline_destroy(timeline);

    changed = tide_game_api;
    changed.tick = changed_tick;
    changed.simulation[0] ^= 1u;
    changed_ticks = 0;
    tide_timeline_about about;
    tide_timeline *replayed = tide_timeline_load(&(tide_timeline_desc){.game = &changed}, bytes, size, &about, NULL, 0);
    TIDE_REQUIRE(replayed != NULL);
    TIDE_CHECK(about.other_build);
    TIDE_REQUIRE(tide_timeline_world(replayed, last) != NULL);
    tide_timeline_check check;
    tide_timeline_check_of(replayed, &check);
    TIDE_CHECK(check.differs == 50u);
    TIDE_CHECK(check.expected != check.got && check.got != 0);
    TIDE_CHECK(check.checked == last);
    tide_timeline_destroy(replayed);

    changed.hash ^= 1u; // Another layout: not replayed at all
    char why[160];
    TIDE_CHECK(tide_timeline_load(&(tide_timeline_desc){.game = &changed}, bytes, size, NULL, why, sizeof why) == NULL);
    TIDE_CHECK(strstr(why, "laid out otherwise") != NULL);
    TIDE_CHECK(tide_timeline_load(&(tide_timeline_desc){.game = &tide_game_api}, bytes, 30, NULL, why, sizeof why) == NULL);
    free(bytes);
}

// The server's machine goes back with two other machines in its match: they're sent its world again and play on
// in it with no command lost or twice, and the recording replays bit for bit
TIDE_TEST(timeline_rewinds_with_other_machines_in_the_match)
{
    tide_timeline *timeline = tide_timeline_create(&(tide_timeline_desc){.game = &tide_game_api, .keep = 1000});
    TIDE_REQUIRE(timeline != NULL);
    machines_start(timeline);
    run_for(1.5);
    const uint32_t back = tide_timeline_last(timeline);
    bump_from(1, 100);
    run_for(1.0);
    TIDE_REQUIRE(server_world()->Score.total == 100);
    bump_from(2, 7); // On its way as the match goes back: it goes in, once
    const void *then = tide_timeline_world(timeline, back);
    TIDE_REQUIRE(then != NULL);
    TIDE_REQUIRE(tide_session_rewind(machines[0], then));
    TIDE_REQUIRE(tide_timeline_cut(timeline, back, server_world()));
    TIDE_CHECK(tide_timeline_last(timeline) == back);
    TIDE_CHECK(server_world()->Score.total == 0); // The 100 went with the ticks after it
    run_for(2.0);
    bump_from(1, 1);
    run_for(1.0);
    TIDE_CHECK(server_world()->Score.total == 8);
    for (int i = 0; i < 3; i++) {
        const tide_client_status s = tide_session_status_of(machines[i]).client;
        TIDE_CHECK(s.state == TIDE_SESSION_CONNECTED);
        TIDE_CHECK(s.verified_tick + 15u >= tide_session_status_of(machines[0]).client.verified_tick);
    }
    // Every machine's verified world is the server's at that tick: they went back too
    run_for(0.5);
    const uint32_t last = tide_timeline_last(timeline);
    TIDE_CHECK(tide_timeline_last(timeline) > back + 60u);
    static uint64_t hashes[2048];
    for (uint32_t tick = 0; tick <= last; tick++) hashes[tick] = tide_game_api.hash_world(tide_timeline_world(timeline, tick));
    uint32_t size = 0;
    uint8_t *bytes = tide_timeline_save(timeline, 30, 1, &size);
    TIDE_REQUIRE(bytes != NULL);
    if (getenv("TIDE_TRAVEL_RECORDING")) { // One that doesn't say its layout, for travel_layout.c's build to refuse
        char plain[1100];
        snprintf(plain, sizeof plain, "%s.plain", getenv("TIDE_TRAVEL_RECORDING"));
        FILE *f = fopen(plain, "wb");
        TIDE_REQUIRE(f != NULL);
        TIDE_CHECK(fwrite(bytes, 1, size, f) == size);
        fclose(f);
    }
    machines_stop();
    tide_timeline_destroy(timeline);
    tide_timeline *replayed = tide_timeline_load(&(tide_timeline_desc){.game = &tide_game_api, .keep = 4}, bytes, size,
                                                 NULL, NULL, 0);
    TIDE_REQUIRE(replayed != NULL);
    for (uint32_t tick = 0; tick <= last; tick++) {
        const void *world = tide_timeline_world(replayed, tick);
        TIDE_REQUIRE(world != NULL);
        TIDE_CHECK(tide_game_api.hash_world(world) == hashes[tick]);
    }
    tide_timeline_check check;
    tide_timeline_check_of(replayed, &check);
    TIDE_CHECK(check.differs == 0 && check.unreadable == 0);
    tide_timeline_destroy(replayed);
    free(bytes);
}

// A recorder that takes no records follows a match another machine runs (as every recorder does in a game that hides
// state): the latest ticks its client had confirmed, each the server's world, with what each sent; nothing before
// them, nothing to save or to go back to
TIDE_TEST(timeline_follows_a_client_that_keeps_no_records)
{
    tide_timeline *timeline = tide_timeline_create(&(tide_timeline_desc){.game = &tide_game_api, .keep = 2048});
    tide_timeline *followed = tide_timeline_create(&(tide_timeline_desc){.game = &tide_game_api, .keep = 20});
    TIDE_REQUIRE(timeline != NULL && followed != NULL);
    machines_start(timeline);
    tide_session_recorder only = tide_timeline_recorder(followed);
    only.begin = NULL;
    only.tick = NULL;
    tide_session_record(machines[1], &only);
    tide_session_events(machines[1]); // Its host takes what's heard, so its ticks record it
    run_for(2.0);
    bump_from(2, 9);
    run_for(0.4);
    TIDE_CHECK(tide_timeline_follows(followed));
    const uint32_t first = tide_timeline_first(followed), last = tide_timeline_last(followed);
    TIDE_CHECK(last == tide_session_status_of(machines[1]).client.verified_tick);
    TIDE_CHECK(last > first && last - first <= 19u);
    uint32_t same = 0, heard = 0;
    for (uint32_t tick = first; tick <= last; tick++) {
        const void *kept = tide_timeline_world(followed, tick);
        TIDE_REQUIRE(kept != NULL);
        const uint64_t hash = tide_game_api.hash_world(kept);
        const void *servers = tide_timeline_world(timeline, tick);
        same += servers && tide_game_api.hash_world(servers) == hash;
        tide_event_log log = {0};
        tide_timeline_heard(followed, tick, &log);
        heard += log.count;
        tide_event_log_free(&log);
    }
    TIDE_CHECK(same == last - first + 1u);
    TIDE_CHECK(heard == 1u); // The Bump's Bumped, at the tick that sent it
    TIDE_CHECK(first == 0u || tide_timeline_world(followed, first - 1u) == NULL);
    uint32_t size = 7;
    TIDE_CHECK(tide_timeline_save(followed, 30, 2, &size) == NULL && size == 0);
    TIDE_CHECK(!tide_timeline_cut(followed, last, tide_timeline_world(followed, last)));
    machines_stop();
    tide_timeline_destroy(timeline);
    tide_timeline_destroy(followed);
}
