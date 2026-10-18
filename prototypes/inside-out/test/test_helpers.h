#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "game.h"

extern tide_world inside_out_test_world;
void inside_out_test_start(int participants);
bool inside_out_test_command(uint32_t id, int sender, int serial, int value);
bool inside_out_test_act(uint32_t id, int sender, int value);
// Sends what a player's machine has solved, then runs one tick.
bool inside_out_test_progress(int sender, int completed, bool playtest);
