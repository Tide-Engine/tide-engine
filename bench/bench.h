#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "tide/session.h"
#include "tide/shard.h"

// Benchmarks: games that stress the engine, played by bots.
//
// A run is a match on a dedicated server, which bots join one after another,
// each a client of its own over a network in this process, as bad as the
// options say. Time is made up, a frame every 60th of a second, so a run goes
// as fast as the machine can take it, and repeats exactly: the same options
// give the same match, bit for bit, on every platform and with any number of
// threads.
//
// A sharded game's world can be split instead (--split): each shard is a
// server of its own, the bots follow the shards near their entities as
// players do (tide/shard_client.h), and each shard is measured apart.
//
// What it measures is each machine's own work: how long the server's (each
// shard's) and each client's updates take, and of that, simulating, hashing,
// snapshotting and packing worlds; and what each sends. The machines run one
// after another on this thread (each ticking on the threads it's given), so
// the run takes about as long as all of them together.

// A benchmark's own option, beside the common ones: --name <value>.
typedef struct bench_flag {
    const char *name;
    const char *help;
    uint32_t full;  // Its value in a full run...
    uint32_t quick; // ...and with --quick
    uint32_t value; // What the run uses
    uint32_t minimum; // Smallest accepted command-line value (maximum: INT32_MAX)
} bench_flag;

typedef struct bench_options {
    uint32_t players;  // Bots that join
    double join_every; // Seconds between joins
    double seconds;    // Run on for this long once everyone's in
    uint32_t threads;  // To tick on: 0 for every core
    double latency;    // Seconds each way
    double jitter;     // Up to this many seconds more, at random
    double loss;       // The share of datagrams lost
    // The server's world's hash at the end (with shards, every shard's
    // together), which every platform has to agree on: the run fails if it's
    // another. 0 checks nothing.
    uint64_t expect;
    // A sharded game's world, split into this many regions along x to start
    // with; 0 runs it on one server, as a world one server holds.
    uint32_t split;
    uint32_t most;  // Entities a region's shard owns before it splits (0: never)
    double busiest; // The share of a frame a region's shard's updates take, steadily, before it splits (0: never)
    // Ticks a bot's verified world may be behind the server's before the run
    // stops, failing: 0 for twice what catching up is (a second, at 60 a second)
    uint32_t lag;
    // With visibility, a bot whose machine's world is compared with what the
    // server says it should be, tick by tick, saying what differs (the bot's
    // number plus 1; 0 for none): what makes a machine ask for repairs.
    uint32_t diverge;
} bench_options;

typedef struct bench_desc {
    const char *name;      // Of the benchmark, as reports say it
    const tide_game *game; // tide_game_api
    bench_options full;    // A full run's options, which flags change
    bench_options quick;   // --quick's: small and short, for tests
    bench_flag *flags;     // Its own options
    uint32_t flag_count;
    // Makes the match's first world, a zeroed tide_world, from the flags'
    // values: tide_world_init, and then whatever it changes before the first
    // tick (a singleton's size, say). `dt` is a tick's length. Split, a
    // sharded game's shards start their worlds themselves, without it.
    void (*make_world)(const bench_flag *flags, void *world, float dt);
    // A bot's input for a tick: `bot` is its number, from 0. It sees the
    // flags, for the game's size.
    void (*sample)(const bench_flag *flags, uint32_t bot, uint32_t tick, void *input);
    // What the run says about the server's world at the end, after the
    // numbers: a line or two (with shards, for each). NULL: nothing.
    void (*describe)(const void *world);
    // What a world holds, for the report on each server and on what the
    // bots' machines have: its entities, say. NULL: not reported.
    uint32_t (*held)(const void *world);
    // A sharded game's: the box its regions divide (tide_shards_desc.world)
    tide_shard_box world;
    // For --diverge: the tick a world has run (its Time.tick), and what
    // differs between two worlds of a player's, as the server says it is and
    // as their machine has it: a line each, at most `most` of them. NULL: no --diverge.
    uint32_t (*tick_of)(const void *world);
    void (*differ)(const void *server, const void *client, uint32_t most);
} bench_desc;

// Reads the command line, runs the benchmark and reports. Returns the
// program's exit code: 0 when everyone joined, no world went wrong and the
// hash is the one expected.
int bench_main(const bench_desc *desc, int argc, char **argv);
