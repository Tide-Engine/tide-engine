// SPDX-FileCopyrightText: 2023 Erin Catto
// SPDX-License-Identifier: MIT

#pragma once

typedef struct b2Recording b2Recording;

#include "arena_allocator.h"
#include "bitset.h"
#include "broad_phase.h"
#include "constraint_graph.h"
#include "container.h"
#include "id_pool.h"
#include "sensor.h"
#include "shape.h"
#include "solver_set.h"

#include "box2d/types.h"

b2DeclareArray( b2BodyMoveEvent );
b2DeclareArray( b2ContactBeginTouchEvent );
b2DeclareArray( b2ContactEndTouchEvent );
b2DeclareArray( b2ContactHitEvent );
b2DeclareArray( b2JointEvent );
b2DeclareArray( b2SensorBeginTouchEvent );
b2DeclareArray( b2SensorEndTouchEvent );
b2DeclareArray( b2TaskContext );

// Per thread task storage
typedef struct b2TaskContext
{
	// Collect per thread sensor continuous hit events.
	b2Array( b2SensorHit ) sensorHits;

	// Broad-phase pairs.
	b2Array( uint64_t ) pairKeys;

	// These bits align with the contact id capacity and signal a change in contact status
	b2BitSet contactStateBitSet;

	// These bits align with the contact id capacity and signal a hit event.
	b2BitSet hitEventBitSet;

	// Fast-path flag: true when this worker set at least one bit in hitEventBitSet this step.
	bool hasHitEvents;

	// These bits align with the joint id capacity and signal a change in contact status
	b2BitSet jointStateBitSet;

	// Used to track bodies with shapes that have enlarged AABBs. This avoids having a bit array
	// that is very large when there are many static shapes.
	b2BitSet enlargedSimBitSet;

	// Used to put islands to sleep
	b2BitSet awakeIslandBitSet;

	// Per worker split island candidate
	float splitSleepTime;
	int splitIslandId;

	// Number of contacts recycled this step (collide pass).
	int recycledContactCount;

} b2TaskContext;

// Tide: which of a world's bodies, shapes and contacts may differ from what its last image has of them (see
// ../TIDE.md, and b2World_GetSnapshotParts in snapshot.c), so that the next image writes those and no
// others. A bit each, by id.
typedef struct b2SnapSet
{
	uint64_t* bits;
	int blockCount;
} b2SnapSet;

#define B2_SNAP_TRACK_SECTIONS 17

typedef struct b2SnapTrack
{
	// The sets and the flags say everything that may have changed since the last image. Not so until an
	// image in sections was taken, after a restore, and after anything was done to the world that they
	// don't follow: then the next image is written whole.
	bool sparse;

	// A shape's bit is also its fat bounds', which are an array beside the shapes
	b2SnapSet bodies;
	b2SnapSet shapes;
	b2SnapSet contacts;

	// The last image: where its sections were, how big each was, and how many bodies, shapes, contacts
	// and solver sets it had
	int bases[B2_SNAP_TRACK_SECTIONS];
	int sizes[B2_SNAP_TRACK_SECTIONS];
	int bodyCount;
	int shapeCount;
	int contactCount;
	int setCount;

	// One body in this many can be awake for an image to be written in parts, or 0 for the usual share
	// (b2World_SetSnapshotAwakeShare)
	int awakeShare;

	// Something was written in the static or the disabled set, in a sleeping set, or in the pairs, since
	// the last image. Nothing writes these but what says so (b2SnapTouchSet, b2SnapTouchPairs): the step's
	// workers only write what's awake, and a pair comes and goes with its contact. An image in parts leaves
	// out the ones that weren't touched.
	bool fixedTouched;
	bool setsTouched;
	bool pairsTouched;

	// What b2World_GetSnapshotParts hands out, kept from one image to the next (b2SnapParts)
	void* parts;
} b2SnapTrack;

// The world struct manages all physics entities, dynamic simulation,  and asynchronous queries.
// The world also contains efficient memory management facilities.
typedef struct b2World
{
	b2Stack stack;
	b2BroadPhase broadPhase;
	b2ConstraintGraph constraintGraph;

	// The body id pool is used to allocate and recycle body ids. Body ids
	// provide a stable identifier for users, but incur caches misses when used
	// to access body data. Aligns with b2Body.
	b2IdPool bodyIdPool;

	// This is a sparse array that maps body ids to the body data
	// stored in solver sets. As sims move within a set or across set.
	// Indices come from id pool.
	b2Array( b2Body ) bodies;

	// Provides free list for solver sets.
	b2IdPool solverSetIdPool;

	// Solvers sets allow sims to be stored in contiguous arrays. The first
	// set is all static sims. The second set is active sims. The third set is disabled
	// sims. The remaining sets are sleeping islands.
	b2Array( b2SolverSet ) solverSets;

	// Used to create stable ids for joints
	b2IdPool jointIdPool;

	// This is a sparse array that maps joint ids to the joint data stored in the constraint graph
	// or in the solver sets.
	b2Array( b2Joint ) joints;

	// Used to create stable ids for contacts
	b2IdPool contactIdPool;

	// This is a sparse array that maps contact ids to the contact data stored in the constraint graph
	// or in the solver sets.
	b2Array( b2Contact ) contacts;

	// Used to create stable ids for islands
	b2IdPool islandIdPool;

	// Persistent islands
	b2Array( b2Island ) islands;

	b2IdPool shapeIdPool;
	b2IdPool chainIdPool;

	// These are sparse arrays that point into the pools above
	b2Array( b2Shape ) shapes;

	// Shape enlarged bounds (hot data split).
	b2Array( b2AABB ) fatAABBs;

	b2Array( b2ChainShape ) chainShapes;

	// This is a dense array of sensor data.
	b2Array( b2Sensor ) sensors;

	// Per thread storage
	b2Array( b2TaskContext ) taskContexts;
	b2Array( b2SensorTaskContext ) sensorTaskContexts;

	b2Array( b2BodyMoveEvent ) bodyMoveEvents;
	b2Array( b2SensorBeginTouchEvent ) sensorBeginEvents;
	b2Array( b2ContactBeginTouchEvent ) contactBeginEvents;

	// End events are double buffered so that the user doesn't need to flush events
	b2Array( b2SensorEndTouchEvent ) sensorEndEvents[2];
	b2Array( b2ContactEndTouchEvent ) contactEndEvents[2];
	int endEventArrayIndex;

	b2Array( b2ContactHitEvent ) contactHitEvents;
	b2Array( b2JointEvent ) jointEvents;

	// todo consider deferred waking and impulses to make it possible
	// to apply forces and impulses from multiple threads
	// impulses must be deferred because sleeping bodies have no velocity state
	// Problems:
	// - multiple forces applied to the same body from multiple threads
	// Deferred wake
	// b2BitSet bodyWakeSet;
	// b2ImpulseArray deferredImpulses;

	// Used to track debug draw
	b2BitSet debugBodySet;
	b2BitSet debugJointSet;
	b2BitSet debugContactSet;
	b2BitSet debugIslandSet;

	// Id that is incremented every time step
	uint64_t stepIndex;

	// Identify islands for splitting as follows:
	// - I want to split islands so smaller islands can sleep
	// - when a body comes to rest and its sleep timer trips, I can look at the island and flag it for splitting
	//   if it has removed constraints
	// - islands that have removed constraints must be put split first because I don't want to wake bodies incorrectly
	// - otherwise I can use the awake islands that have bodies wanting to sleep as the splitting candidates
	// - if no bodies want to sleep then there is no reason to perform island splitting
	int splitIslandId;

	b2Vec2 gravity;
	float hitEventThreshold;
	float restitutionThreshold;
	int restitutionIterations;
	bool enableRestitutionPropagation;
	float maxLinearSpeed;
	float contactSpeed;
	float contactHertz;
	float contactDampingRatio;
	float contactRecycleDistance;

	b2FrictionCallback* frictionCallback;
	b2RestitutionCallback* restitutionCallback;

	uint16_t generation;

	b2Profile profile;

	b2Capacity maxCapacity;

	b2PreSolveFcn* preSolveFcn;
	b2PreContinuousFcn* preContinuousFcn;
	void* preSolveContext;

	b2CustomFilterFcn* customFilterFcn;
	void* customFilterContext;

	int workerCount;
	int simdWidth;
	b2EnqueueTaskCallback* enqueueTaskFcn;
	b2FinishTaskCallback* finishTaskFcn;
	void* userTaskContext;
	void* userTreeTask;

	struct b2Scheduler* scheduler;

	void* userData;

	b2Recording* recording; // NULL unless b2World_StartRecording is active, owned by the host

	// Remember type step used for reporting forces and torques
	// inverse sub-step
	float inv_h;

	// inverse full-step
	float inv_dt;

	int activeTaskCount;
	int taskCount;

	uint16_t worldId;

	bool enableSleep;
	bool locked;
	bool enableWarmStarting;
	bool enableContinuous;
	bool inUse;

	// Tide: what changed since the world's last image. Last, so that everything above is where upstream
	// has it.
	b2SnapTrack snapTrack;
} b2World;

// Tide: a body, a shape or a contact may be another than the world's last image has it (b2SnapTrack). Said
// wherever one is written that isn't an awake body's own: an awake body, its shapes and the contacts it's in
// are taken to have changed anyway, as the step writes them on its worker threads. These are only called
// from code that runs on one thread.
void b2SnapGrowSet( b2SnapSet* set, int blockCount );

static inline void b2SnapSetBit( b2SnapSet* set, int index )
{
	int block = index >> 6;
	if ( block >= set->blockCount )
	{
		b2SnapGrowSet( set, block + 1 );
	}
	set->bits[block] |= (uint64_t)1 << ( index & 63 );
}

static inline void b2SnapTouchBody( b2World* world, int bodyId )
{
	if ( world->snapTrack.sparse && bodyId >= 0 )
	{
		b2SnapSetBit( &world->snapTrack.bodies, bodyId );
	}
}

static inline void b2SnapTouchShape( b2World* world, int shapeId )
{
	if ( world->snapTrack.sparse && shapeId >= 0 )
	{
		b2SnapSetBit( &world->snapTrack.shapes, shapeId );
	}
}

static inline void b2SnapTouchContact( b2World* world, int contactId )
{
	if ( world->snapTrack.sparse && contactId >= 0 )
	{
		b2SnapSetBit( &world->snapTrack.contacts, contactId );
	}
}

// Tide: a contact by its key in a body's list, which says the contact and which of its two edges, or none
static inline void b2SnapTouchContactKey( b2World* world, int contactKey )
{
	if ( world->snapTrack.sparse && contactKey >= 0 )
	{
		b2SnapSetBit( &world->snapTrack.contacts, contactKey >> 1 );
	}
}

// Tide: something was done that the sets don't follow: the next image is written whole
static inline void b2SnapTouchAll( b2World* world )
{
	world->snapTrack.sparse = false;
}

// Tide: something was written in a solver set: a body's place or mass, a contact or an island put in or
// taken out, the set made or destroyed. It matters for every set but the awake one, which every image
// writes: the static and the disabled set are one section of an image, and the sleeping sets another.
static inline void b2SnapTouchSet( b2World* world, int setIndex )
{
	if ( setIndex >= b2_firstSleepingSet )
	{
		world->snapTrack.setsTouched = true;
	}
	else if ( setIndex == b2_staticSet || setIndex == b2_disabledSet )
	{
		world->snapTrack.fixedTouched = true;
	}
}

// Tide: a pair was added or removed
static inline void b2SnapTouchPairs( b2World* world )
{
	world->snapTrack.pairsTouched = true;
}

// Tide: a body, its shapes and every contact it's in
void b2SnapTouchBodyDeep( b2World* world, const b2Body* body );

// Tide: what a call does to the world, by the name its recording has: the sets follow the calls named
// there, through b2SnapTouchBody and the like where they write, and any other writes the next image whole.
// So a call nobody looked at costs an image, and can't make a wrong one.
void b2SnapNoteOp( b2World* world, int op );

// Tide: frees what follows a world's images
void b2DestroySnapshotTrack( b2World* world );

b2World* b2GetWorldFromId( b2WorldId id );
b2World* b2GetWorld( int index );
b2World* b2GetWorldLocked( int index );

// Union of the broad-phase root bounds across all body types. Returns false when no tree holds a
// proxy, so callers don't fold an empty world's origin into a running bounds.
bool b2ComputeWorldBounds( b2World* world, b2AABB* bounds );

void b2ValidateConnectivity( b2World* world );
void b2ValidateSolverSets( b2World* world );
void b2ValidateContacts( b2World* world );
