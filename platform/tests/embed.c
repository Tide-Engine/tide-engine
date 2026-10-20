// Tide inside another program's frame (tide/embed.h), as that program runs
// it: platform/tests/embed.tide, its frames' lengths and input made up here.

#include <stdio.h>
#include <string.h>

#include "game.h"
#include "tide/embed.h"
#include "tide/run.h"
#include "tide_test.h"

// What the game's view saw at the last frame (embed.tide's Show).
static int seen_ticks = -1, seen_presses = -1;
static float seen_width, seen_height;

void Seen(const int ticks, const int presses, const float width, const float height)
{
    seen_ticks = ticks;
    seen_presses = presses;
    seen_width = width;
    seen_height = height;
}

static char program[] = "embed";

static tide_embed *start(int argc, char **argv)
{
    seen_ticks = seen_presses = -1;
    return tide_embed_create(&(tide_embed_desc){.game = &tide_host_game_api, .argc = argc, .argv = argv});
}

// `count` frames of a 60th of a second, with `input` in each.
static void frames(tide_embed *e, const int count, const tide_embed_input *input)
{
    for (int i = 0; i < count; i++) tide_embed_frame(e, 1.0f / 60.0f, input, 800.0f, 600.0f, 2.0f);
}

static void hold_key(tide_embed_input *input, const unsigned key, const bool held)
{
    if (held) input->keys[key / 8] |= (uint8_t)(1u << key % 8);
    else input->keys[key / 8] &= (uint8_t)~(1u << key % 8);
}

// Main's match starts on this machine, and ticks at its rate by the frames' lengths: a second of them is 30 ticks.
TIDE_TEST(embed_runs_main)
{
    char *argv[] = {program};
    tide_embed *e = start(1, argv);
    TIDE_CHECK(e != NULL);
    TIDE_CHECK(start(1, argv) == NULL); // One at a time
    TIDE_CHECK(tide_embed_abi() == TIDE_EMBED_ABI);
    frames(e, 60, NULL);
    tide_embed_status status;
    tide_embed_status_of(e, &status);
    TIDE_CHECK(status.server);
    TIDE_CHECK(status.state == TIDE_SESSION_CONNECTED);
    TIDE_CHECK(!status.open);
    TIDE_CHECK(status.room[0] == '\0'); // A closed match is in no room
    TIDE_CHECK(seen_ticks >= 27 && seen_ticks <= 30);
    TIDE_CHECK(seen_width == 800.0f && seen_height == 600.0f); // The screen, in logical pixels
    tide_embed_destroy(e);
}

// A key held for one frame between two ticks reaches the next tick's input once, as from a window.
TIDE_TEST(embed_input_reaches_ticks)
{
    char *argv[] = {program};
    tide_embed *e = start(1, argv);
    TIDE_CHECK(e != NULL);
    tide_embed_input input = {0};
    frames(e, 10, &input);
    hold_key(&input, 36, true); // space: after the 26 letters and 10 digits, in tide_keyboard's order
    frames(e, 1, &input);
    hold_key(&input, 36, false);
    frames(e, 10, &input);
    TIDE_CHECK(seen_presses == 1);
    tide_embed_destroy(e);
}

// Requests go as local code's do: out of the match and into Main's again; a scene by name isn't taken yet.
TIDE_TEST(embed_requests)
{
    char *argv[] = {program};
    tide_embed *e = start(1, argv);
    TIDE_CHECK(e != NULL);
    frames(e, 5, NULL);
    TIDE_CHECK(!tide_embed_request(e, TIDE_REQUEST_START, "Main", 0));
    TIDE_CHECK(!tide_embed_request(e, TIDE_REQUEST_JOIN, "", 0));
    TIDE_CHECK(!tide_embed_request(e, TIDE_REQUEST_COPY, "text", 0));
    TIDE_CHECK(tide_embed_request(e, TIDE_REQUEST_LEAVE, NULL, 0));
    frames(e, 2, NULL);
    tide_embed_status status;
    tide_embed_status_of(e, &status);
    TIDE_CHECK(status.state == TIDE_SESSION_OFFLINE);
    TIDE_CHECK(tide_embed_request(e, TIDE_REQUEST_START, NULL, 0));
    seen_ticks = -1;
    frames(e, 30, NULL);
    tide_embed_status_of(e, &status);
    TIDE_CHECK(status.state == TIDE_SESSION_CONNECTED);
    TIDE_CHECK(seen_ticks >= 12 && seen_ticks <= 15); // A new match, counted from its start
    tide_embed_destroy(e);
}

// A flag that's wrong is left out, and the program goes on, where the standard host would end it.
TIDE_TEST(embed_bad_flags_left_out)
{
    char seed[] = "--seed", bad[] = "lots", dir[] = "--data-dir", dash[] = "-x";
    char *argv[] = {program, seed, bad, dir, dash};
    tide_embed *e = start(5, argv);
    TIDE_CHECK(e != NULL);
    frames(e, 30, NULL);
    TIDE_CHECK(seen_ticks >= 12);
    tide_embed_destroy(e);
}

// Frames whose lengths make no time (NaN, below 0) add none; the screen falls back to the smallest.
TIDE_TEST(embed_bad_frames)
{
    char *argv[] = {program};
    tide_embed *e = start(1, argv);
    TIDE_CHECK(e != NULL);
    frames(e, 30, NULL);
    const int ticks = seen_ticks;
    float nan_value;
    const uint32_t nan_bits = 0x7fc00000u;
    memcpy(&nan_value, &nan_bits, sizeof nan_value);
    tide_embed_frame(e, nan_value, NULL, nan_value, -5.0f, nan_value);
    tide_embed_frame(e, -1.0f, NULL, nan_value, -5.0f, 0.0f);
    TIDE_CHECK(seen_ticks == ticks);
    TIDE_CHECK(seen_width == 1.0f && seen_height == 1.0f);
    tide_embed_destroy(e);
}
