#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "game.h"
#include "model.h"

extern tide_world underfoot_test_world;

// A match with this many players joined, on the first puzzle.
void underfoot_test_start(int participants);
// Sends one typed command as a player's machine would, and runs the tick that
// handles it. `serial` is the board's own unless a test says otherwise.
bool underfoot_test_send(uint32_t id, int sender, int serial, int first, int second);
bool underfoot_test_act(int sender, int paver, int action);
bool underfoot_test_do(uint32_t id, int sender, int value);

// The match's board as the model sees it, and the other way, for boards a
// test lays out itself.
void underfoot_test_to_model(uf_layout *layout, uf_state *state);
void underfoot_test_from_model(const uf_layout *layout, const uf_state *state);
bool underfoot_test_same(const uf_layout *layout, const uf_state *state);
// Seven rows in levels.tide's alphabet; short rows and missing rows are wall.
void underfoot_test_lab(const char *const rows[7]);
