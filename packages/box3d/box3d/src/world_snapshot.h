// SPDX-FileCopyrightText: 2026 Erin Catto
// SPDX-License-Identifier: MIT

#pragma once

#include "recording.h"
#include "recording_replay.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct b3World b3World;

// Serialize the live world into buf, interning shape geometry into rec->registry.
// On success buf holds a self-contained snapshot image. Returns the byte count.
// Tide: with no recording, the geometry is interned into the world's own (see world_snapshot.c).
int b3SerializeWorld( b3World* world, b3RecBuffer* buf, b3Recording* rec );

// Overwrite a freshly-created (shell) world with the simulation state held in the
// snapshot image [data, size). Geometry references are resolved via the shared
// registry slots in rdr. Returns false on a corrupt or incompatible image.
// Tide: with no reader, they're resolved in the world's own geometry.
bool b3DeserializeIntoShell( const uint8_t* data, int size, b3World* world, b3RecReader* rdr );

// Tide: frees the geometry a world's images name, with the world.
void b3DestroySnapshotGeometry( b3World* world );
