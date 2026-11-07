#pragma once

#include "tide/session.h"

typedef struct tide_preparation tide_preparation;

// Runs preparation on an independent world. Updates and views retain their
// current endpoint until the completed world is acquired on the calling thread.
bool tide_preparation_update(tide_preparation **state, const tide_game *game,
                             void *world, const tide_jobs *jobs, tide_event_log *events);
bool tide_preparation_update_clients(tide_preparation **state, const tide_game *game,
                                     void *world, const tide_jobs *jobs, tide_event_log *per_player, tide_event_log *events);
void tide_preparation_free(tide_preparation *state);
// Compiler safe points publish completed preparation statements for local views.
// The published endpoint is observational only and never drives match ticks.
void tide_preparation_publish(void *world);
const void *tide_preparation_view(const tide_preparation *state);
