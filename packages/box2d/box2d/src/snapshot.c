// SPDX-FileCopyrightText: 2026 Erin Catto
// SPDX-License-Identifier: MIT

#include "snapshot.h"

#include "bitset.h"
#include "body.h"
#include "broad_phase.h"
#include "constraint_graph.h"
#include "contact.h"
#include "container.h"
#include "core.h"
#include "dynamic_tree.h"
#include "id_pool.h"
#include "island.h"
#include "joint.h"
#include "physics_world.h"
#include "platform.h"
#include "recording.h"
#include "sensor.h"
#include "shape.h"
#include "solver_set.h"
#include "table.h"

#include "box2d/box2d.h"
#include "box2d/collision.h"
#include "box2d/types.h"

#include <stddef.h>
#include <string.h>

// Snapshot image magic and version
#define B2_SNAP_MAGIC 0x32534E42u // 'BNS2'

// Bump this if any of the data structures below get modified. The layout hash only catches
// size changes, a same-size reinterpretation like the contact cache reshape needs this bump.
#define B2_SNAP_VERSION 12u // post solve restitution

// Header flag bits
#define B2_SNAP_FLAG_VALIDATION 0x1u	   // image was built with validation, only used for diagnostics
#define B2_SNAP_FLAG_DOUBLE_PRECISION 0x2u // image was built with double precision world positions

// Tide: an image in sections (see ../TIDE.md). After the header there's a table of where each section starts,
// and each is written there, with zeros between one's end and the next one's start. So a section that grows or
// shrinks a little leaves the others where they were, and an image differs from the one before it where the
// world does. Upstream's image is one run of bytes, whose every byte moves when an early part changes size.
#define B2_SNAP_FLAG_SECTIONS 0x4u
enum
{
	b2_sectionConfig,		// World scalars and the id pools
	b2_sectionFixedSets,	// The static and the disabled solver sets
	b2_sectionAwakeSet,
	b2_sectionSleepingSets,
	b2_sectionBodies,
	b2_sectionShapes,
	b2_sectionFatAABBs,
	b2_sectionContacts,
	b2_sectionJoints,
	b2_sectionChains,
	b2_sectionSensors,
	b2_sectionIslands,
	b2_sectionStaticTree,
	b2_sectionKinematicTree,
	b2_sectionDynamicTree,
	b2_sectionPairs,
	b2_sectionGraph,
	b2_sectionCount,
};
_Static_assert( b2_sectionCount == B2_SNAPSHOT_SECTIONS, "the sections of an image, as box2d.h counts them" );
_Static_assert( b2_bodyTypeCount == 3, "a tree a section" );

// Layout hash seeds from all structs we memcpy, plus key constants.
// Changing any struct or constant updates the hash. Re-purposing or swapping
// fields might not change the hash
static uint32_t b2ComputeLayoutHash( void )
{
	// Tide: only the structs that go into an image as their bytes, which are laid out the same on
	// 32-bit and 64-bit platforms (see ../TIDE.md). Upstream also mixes in the structs that hold
	// pointers, written field by field, and sizeof( void* ), which keeps an image to one pointer width.
	// FNV-1a hash
	uint32_t h = 2166136261u;
#define MIX( x )                                                                                                                 \
	h ^= (uint32_t)( x );                                                                                                        \
	h *= 16777619u;
	MIX( sizeof( b2Body ) )
	MIX( sizeof( b2BodySim ) )
	MIX( sizeof( b2BodyState ) )
	MIX( sizeof( b2Shape ) )
	MIX( sizeof( b2Contact ) )
	MIX( sizeof( b2ContactSim ) )
	MIX( sizeof( b2Joint ) )
	MIX( sizeof( b2JointSim ) )
	MIX( sizeof( b2IslandSim ) )
	MIX( sizeof( b2ContactLink ) )
	MIX( sizeof( b2JointLink ) )
	MIX( sizeof( b2Visitor ) )
	MIX( sizeof( b2TreeNode ) )
	MIX( sizeof( b2TreeProxy ) )
	MIX( sizeof( b2SetItem ) )
	MIX( sizeof( b2SurfaceMaterial ) )
	MIX( B2_GRAPH_COLOR_COUNT )
	MIX( b2_bodyTypeCount )
#undef MIX
	return h;
}

// Snapshot image header, written at offset 0
typedef struct b2SnapHeader
{
	uint32_t magic;
	uint32_t version;
	uint32_t layoutHash;
	uint32_t flags; // B2_SNAP_FLAG_*, keeps 16-byte alignment
} b2SnapHeader;

// Compile-time tripwires for the structs serialized field-by-field. If one fires, a field was added or
// reordered: resync the matching code in b2SerializeWorld / b2DeserializeIntoShell and bump
// B2_SNAP_VERSION.
#if INTPTR_MAX == INT64_MAX
_Static_assert( sizeof( b2ChainShape ) == 32, "b2ChainShape layout changed; resync snapshot chain serialization" );
_Static_assert( sizeof( b2Sensor ) == 56, "b2Sensor layout changed; resync snapshot sensor serialization" );
_Static_assert( sizeof( b2Island ) == 64, "b2Island layout changed; resync snapshot island serialization" );
#endif

// Bounds-checked read cursor, mirrors b2RecReader discipline
typedef struct b2SnapReader
{
	const uint8_t* data;
	int cursor;
	int size;
	bool ok;
	// Tide: an image in sections says where each starts
	bool sectioned;
	int bases[b2_sectionCount];
} b2SnapReader;

static void b2SnapRCheck( b2SnapReader* r, int need )
{
	if ( need < 0 || (int64_t)r->cursor + (int64_t)need > (int64_t)r->size )
	{
		r->ok = false;
	}
}

// Tide: an image as what changed of the one before it (see ../TIDE.md, and b2World_GetSnapshotParts below).
// The writer below is the one that writes an image whole, and what it appends comes here: a copy of the
// bytes in memory of the world's own, or the bytes where they are when they go into an image as they are
// (b2SnapW_Ref). Each is a part that says where in the image it goes. What the writer leaves out is what the
// image before has already.
//
// The copies are in blocks that don't move, so a part can point into one, and they're kept from one image to
// the next: they're as big as what changed, not as an image.
typedef struct b2SnapBlock
{
	uint8_t* data;
	int capacity;
} b2SnapBlock;

typedef struct b2SnapParts
{
	// What the caller gets
	b2SnapshotPart* parts;
	int count;
	int capacity;

	b2SnapBlock* blocks;
	int blockCount;
	int blockCapacity;
	// The block being filled, and how much of it is
	int block;
	int used;

	// The bytes appended last: where in the image, how many, and the copy. What's written over some of
	// them after (b2SnapAt) is written there.
	int lastOffset;
	int lastSize;
	uint8_t* last;

	// The sizes the sections had in the image before
	const int* sizesBefore;
} b2SnapParts;

_Static_assert( B2_SNAP_TRACK_SECTIONS == b2_sectionCount, "the sections of an image, as physics_world.h counts them" );

#define B2_SNAP_BLOCK_SIZE ( 64 * 1024 )

static void b2SnapPartsReset( b2SnapParts* p, const int* sizesBefore )
{
	// One block for all of them, so that what's written goes side by side the next time, with half as much
	// again: a world that's waking up has more to write at each image, and would outgrow a block that
	// just fits every time
	if ( p->blockCount > 1 )
	{
		int total = 0;
		for ( int i = 0; i < p->blockCount; ++i )
		{
			total += p->blocks[i].capacity;
			b2Free( p->blocks[i].data, p->blocks[i].capacity );
		}
		total += total / 2;
		p->blocks[0].data = b2Alloc( total );
		p->blocks[0].capacity = total;
		p->blockCount = 1;
	}

	p->count = 0;
	p->block = 0;
	p->used = 0;
	p->lastOffset = 0;
	p->lastSize = 0;
	p->last = NULL;
	p->sizesBefore = sizesBefore;
}

// Memory for `size` bytes side by side, which stays where it is until the next image
static uint8_t* b2SnapPartsRoom( b2SnapParts* p, int size )
{
	if ( p->block < p->blockCount && p->used + size > p->blocks[p->block].capacity )
	{
		p->block += 1;
		p->used = 0;
	}

	if ( p->block == p->blockCount )
	{
		if ( p->blockCount == p->blockCapacity )
		{
			int capacity = p->blockCapacity == 0 ? 8 : 2 * p->blockCapacity;
			p->blocks =
				b2GrowAlloc( p->blocks, p->blockCapacity * (int)sizeof( b2SnapBlock ), capacity * (int)sizeof( b2SnapBlock ) );
			p->blockCapacity = capacity;
		}

		b2SnapBlock* block = p->blocks + p->blockCount;
		block->capacity = size > B2_SNAP_BLOCK_SIZE ? size : B2_SNAP_BLOCK_SIZE;
		block->data = b2Alloc( block->capacity );
		p->blockCount += 1;
		p->used = 0;
	}
	else if ( p->blocks[p->block].capacity < size )
	{
		// A block that was kept and is the last, and too small: a bigger one in its place
		b2SnapBlock* block = p->blocks + p->block;
		b2Free( block->data, block->capacity );
		block->capacity = size;
		block->data = b2Alloc( size );
	}

	uint8_t* at = p->blocks[p->block].data + p->used;
	p->used += size;
	return at;
}

// A part of the image: `size` bytes that go at `offset`. One that goes on from the last, in the image and in
// memory, is the same part.
static void b2SnapPartsAdd( b2SnapParts* p, int offset, int size, const uint8_t* bytes )
{
	if ( p->count > 0 )
	{
		b2SnapshotPart* last = p->parts + ( p->count - 1 );
		if ( last->offset + last->size == offset && last->bytes + last->size == bytes )
		{
			last->size += size;
			return;
		}
	}

	if ( p->count == p->capacity )
	{
		int capacity = p->capacity == 0 ? 64 : 2 * p->capacity;
		p->parts =
			b2GrowAlloc( p->parts, p->capacity * (int)sizeof( b2SnapshotPart ), capacity * (int)sizeof( b2SnapshotPart ) );
		p->capacity = capacity;
	}

	p->parts[p->count] = ( b2SnapshotPart ){ offset, size, bytes };
	p->count += 1;
}

uint8_t* b2SnapPartsReserve( b2RecBuffer* buf, int size )
{
	b2SnapParts* p = buf->parts;
	uint8_t* at = b2SnapPartsRoom( p, size );
	b2SnapPartsAdd( p, buf->size, size, at );
	p->lastOffset = buf->size;
	p->lastSize = size;
	p->last = at;
	buf->size += size;
	return at;
}

// Zeros from where the buffer is in the image up to `end`
static void b2SnapPartsZeros( b2RecBuffer* buf, int end )
{
	int size = end - buf->size;
	if ( size <= 0 )
	{
		return;
	}

	b2SnapParts* p = buf->parts;
	uint8_t* at = b2SnapPartsRoom( p, size );
	memset( at, 0, (size_t)size );
	b2SnapPartsAdd( p, buf->size, size, at );
	buf->size += size;
}

// Tide: where the bytes last appended are, to write over some of them: the image itself, or the copy an
// image in parts has of them. `offset` is where in the image, at or past where they started.
static uint8_t* b2SnapAt( b2RecBuffer* buf, int offset )
{
	if ( buf->parts == NULL )
	{
		return buf->data + offset;
	}

	b2SnapParts* p = buf->parts;
	B2_ASSERT( p->lastOffset <= offset && offset < p->lastOffset + p->lastSize );
	return p->last + ( offset - p->lastOffset );
}

// Tide: the next section starts here. Its size goes to the caller who asked, and with a table of bases the
// writing goes on from this one's, over zeros. A section that ran past the next one's base says so, and from
// there on the image is only measured, as one run of bytes: the caller places the sections again.
static void b2SnapSection( b2RecBuffer* buf, int section )
{
	if ( buf->sizes != NULL && buf->section > 0 )
	{
		buf->sizes[buf->section - 1] = buf->size - buf->sectionStart;
	}

	if ( buf->bases != NULL && buf->overran == false && section < b2_sectionCount )
	{
		int base = buf->bases[section];
		if ( buf->size > base )
		{
			buf->overran = true;
			buf->countOnly = true;
		}
		else if ( buf->parts != NULL )
		{
			// Tide: an image in parts. Between the end of the section before and this one's base the image
			// before has zeros, but for where that section was bigger then: zeros there now.
			if ( buf->countOnly == false && buf->section > 0 )
			{
				b2SnapParts* p = buf->parts;
				int endBefore = buf->sectionStart + p->sizesBefore[buf->section - 1];
				b2SnapPartsZeros( buf, endBefore < base ? endBefore : base );
			}
			buf->size = base;
		}
		else if ( buf->countOnly || base > buf->capacity )
		{
			buf->countOnly = true;
			buf->size = base;
		}
		else
		{
			memset( buf->data + buf->size, 0, (size_t)( base - buf->size ) );
			buf->size = base;
		}
	}

	buf->section = section + 1;
	buf->sectionStart = buf->size;
}

static void b2SnapSeek( b2SnapReader* r, int section )
{
	if ( r->sectioned && r->ok )
	{
		r->cursor = r->bases[section];
	}
}

static void b2SnapR_Bytes( b2SnapReader* r, void* dst, int n )
{
	b2SnapRCheck( r, n );
	if ( !r->ok )
	{
		return;
	}
	memcpy( dst, r->data + r->cursor, n );
	r->cursor += n;
}

static int b2SnapR_I32( b2SnapReader* r )
{
	int32_t v = 0;
	b2SnapR_Bytes( r, &v, 4 );
	return (int)v;
}

static uint32_t b2SnapR_U32( b2SnapReader* r )
{
	uint32_t v = 0;
	b2SnapR_Bytes( r, &v, 4 );
	return v;
}

static void b2SnapW_I32( b2RecBuffer* buf, int v )
{
	int32_t w = (int32_t)v;
	b2RecBufAppend( buf, &w, 4 );
}

static void b2SnapW_U32( b2RecBuffer* buf, uint32_t v )
{
	b2RecBufAppend( buf, &v, 4 );
}

static void b2SnapW_Bytes( b2RecBuffer* buf, const void* src, int n )
{
	b2RecBufAppend( buf, src, n );
}

// Tide: bytes that go into an image as they are in the world, with nothing written over them after. An image
// in parts has the big ones where they are, with no copy: what reads its parts compares them there, so they
// stay as they are until its parts were read (see b2World_GetSnapshotParts).
#define B2_SNAP_REF_SIZE 512

static void b2SnapW_Ref( b2RecBuffer* buf, const void* src, int n )
{
	if ( buf->parts == NULL || buf->countOnly || n < B2_SNAP_REF_SIZE )
	{
		b2RecBufAppend( buf, src, n );
		return;
	}

	b2SnapPartsAdd( buf->parts, buf->size, n, src );
	buf->size += n;
}

// Tide: for an image in parts, whose writer leaves out what the image before has: the next bytes go at
// `offset` in the image.
static void b2SnapSkipTo( b2RecBuffer* buf, int offset )
{
	B2_ASSERT( buf->parts != NULL );
	buf->size = offset;
}

// Tide: whether an image in parts leaves the section it's at as the image before has it, which it does when
// nothing was done to what's in it: the buffer is then past it.
static bool b2SnapKeepSection( b2RecBuffer* buf, bool touched )
{
	if ( buf->parts == NULL || buf->countOnly || touched )
	{
		return false;
	}

	b2SnapParts* p = buf->parts;
	buf->size += p->sizesBefore[buf->section - 1];
	return true;
}

// Tide: zeros over `size` bytes at `offset` in each of the `count` things of `stride` bytes that were written
// from `start` on, which were the last bytes appended. Nothing, when they were only counted.
static void b2SnapZero( b2RecBuffer* buf, int start, int count, int stride, int offset, int size )
{
	if ( buf->countOnly || count <= 0 )
	{
		return;
	}

	uint8_t* at = b2SnapAt( buf, start ) + offset;
	for ( int i = 0; i < count; ++i )
	{
		memset( at, 0, (size_t)size );
		at += stride;
	}
}

// Tide: the next run of ids under `count` that are in `set`, from `*cursor` on: its first, and one past its
// last. False when there's none left.
static bool b2SnapNextRun( const b2SnapSet* set, int count, int* cursor, int* first, int* end )
{
	int limit = set->blockCount * 64 < count ? set->blockCount * 64 : count;
	int i = *cursor;
	while ( i < limit )
	{
		uint64_t word = set->bits[i >> 6] >> ( i & 63 );
		if ( word != 0 )
		{
			i += (int)b2CTZ64( word );
			break;
		}
		i = ( i | 63 ) + 1;
	}

	if ( i >= limit )
	{
		*cursor = limit;
		return false;
	}

	int j = i + 1;
	while ( j < limit && ( ( set->bits[j >> 6] >> ( j & 63 ) ) & 1 ) != 0 )
	{
		j += 1;
	}

	*first = i;
	*end = j;
	*cursor = j;
	return true;
}

// Reject a count read from the image before it reaches an allocation or memset. count must be non
// negative, its in-memory footprint must fit in int, and the stream must hold at least minStreamBytes
// per element. A corrupt or truncated image then fails.
static bool b2SnapCheckCount( const b2SnapReader* r, int count, int memSize, int minStreamBytes )
{
	if ( count < 0 || memSize < 0 || minStreamBytes < 0 )
	{
		return false;
	}
	if ( memSize > 0 && count > INT32_MAX / memSize )
	{
		return false;
	}
	int64_t remaining = (int64_t)r->size - (int64_t)r->cursor;
	return (int64_t)count * (int64_t)minStreamBytes <= remaining;
}

// Serialize a POD array: count then raw bytes
// Tide: which an image in parts has where they are (b2SnapW_Ref)
#define b2SerPodArray( buf, arr )                                                                                                \
	do                                                                                                                           \
	{                                                                                                                            \
		b2SnapW_I32( buf, ( arr ).count );                                                                                       \
		if ( ( arr ).count > 0 )                                                                                                 \
		{                                                                                                                        \
			b2SnapW_Ref( buf, ( arr ).data, ( arr ).count * (int)sizeof( *( arr ).data ) );                                      \
		}                                                                                                                        \
	}                                                                                                                            \
	while ( 0 )

// Serialize a run of a sparse struct array whose element carries a host userData pointer. userData is host
// wiring, not simulation state, so write it as NULL: images stay reproducible and never persist host
// addresses across a save. On restore the field reads back NULL.
// Tide: the elements from `first` up to `end`, which the buffer is at, as their bytes, and then zeros over
// all 8 bytes of each one's userData (see ../TIDE.md): `start` is where the first element of all goes. An
// image written whole has them all in one run, and an image in parts a run for each stretch of them that
// may have changed (b2SnapNextRun), so both have the same bytes for an element. Upstream writes a copy of
// each, one after another.
#define b2SerSimRun( buf, arr, type, start, first, end )                                                                         \
	do                                                                                                                           \
	{                                                                                                                            \
		int runCount = ( end ) - ( first );                                                                                      \
		int runStart = ( start ) + ( first ) * (int)sizeof( type );                                                              \
		b2SnapW_Bytes( buf, ( arr ).data + ( first ), runCount * (int)sizeof( type ) );                                          \
		b2SnapZero( buf, runStart, runCount, (int)sizeof( type ), (int)offsetof( type, userDataBits ), 8 );                      \
	}                                                                                                                            \
	while ( 0 )

// Tide: a sparse struct array, each element in the same room: the count, then all of them for an image
// written whole, or the ones whose bit is in `set` for an image in parts, each at its place
#define b2SerSimArray( buf, arr, type, set )                                                                                     \
	do                                                                                                                           \
	{                                                                                                                            \
		int simCount = ( arr ).count;                                                                                            \
		b2SnapW_I32( buf, simCount );                                                                                            \
		int simStart = ( buf )->size;                                                                                            \
		if ( ( buf )->parts != NULL && ( set ) != NULL )                                                                         \
		{                                                                                                                        \
			int cursor = 0, first, end;                                                                                          \
			while ( b2SnapNextRun( ( set ), simCount, &cursor, &first, &end ) )                                                  \
			{                                                                                                                    \
				b2SnapSkipTo( buf, simStart + first * (int)sizeof( type ) );                                                     \
				b2SerSimRun( buf, arr, type, simStart, first, end );                                                             \
			}                                                                                                                    \
			b2SnapSkipTo( buf, simStart + simCount * (int)sizeof( type ) );                                                      \
		}                                                                                                                        \
		else if ( simCount > 0 )                                                                                                 \
		{                                                                                                                        \
			b2SerSimRun( buf, arr, type, simStart, 0, simCount );                                                                \
		}                                                                                                                        \
	}                                                                                                                            \
	while ( 0 )

// Tide: a POD array whose elements each take the same room and go by the ids `set` holds, as b2SerSimArray
#define b2SerPodRuns( buf, arr, set )                                                                                            \
	do                                                                                                                           \
	{                                                                                                                            \
		int podCount = ( arr ).count;                                                                                            \
		int podStride = (int)sizeof( *( arr ).data );                                                                            \
		b2SnapW_I32( buf, podCount );                                                                                            \
		int podStart = ( buf )->size;                                                                                            \
		if ( ( buf )->parts != NULL )                                                                                            \
		{                                                                                                                        \
			int cursor = 0, first, end;                                                                                          \
			while ( b2SnapNextRun( ( set ), podCount, &cursor, &first, &end ) )                                                  \
			{                                                                                                                    \
				b2SnapSkipTo( buf, podStart + first * podStride );                                                               \
				b2SnapW_Bytes( buf, ( arr ).data + first, ( end - first ) * podStride );                                         \
			}                                                                                                                    \
			b2SnapSkipTo( buf, podStart + podCount * podStride );                                                                \
		}                                                                                                                        \
		else if ( podCount > 0 )                                                                                                 \
		{                                                                                                                        \
			b2SnapW_Bytes( buf, ( arr ).data, podCount * podStride );                                                            \
		}                                                                                                                        \
	}                                                                                                                            \
	while ( 0 )

// Deserialize a POD array: validate count, resize, then memcpy
#define b2DesPodArray( r, arr )                                                                                                  \
	do                                                                                                                           \
	{                                                                                                                            \
		int cnt = b2SnapR_I32( r );                                                                                              \
		int elemSize = (int)sizeof( *( arr ).data );                                                                             \
		if ( ( r )->ok && b2SnapCheckCount( r, cnt, elemSize, elemSize ) == false )                                              \
		{                                                                                                                        \
			( r )->ok = false;                                                                                                   \
		}                                                                                                                        \
		if ( ( r )->ok && cnt > 0 )                                                                                              \
		{                                                                                                                        \
			b2Array_Resize( arr, cnt );                                                                                          \
			b2SnapR_Bytes( r, ( arr ).data, cnt * elemSize );                                                                    \
		}                                                                                                                        \
		else if ( ( r )->ok )                                                                                                    \
		{                                                                                                                        \
			( arr ).count = 0;                                                                                                   \
		}                                                                                                                        \
	}                                                                                                                            \
	while ( 0 )

// Tide: a sensor's visitors, each as its two fields with zeros after them. A b2Visitor has two bytes that no
// field owns, which are whatever was in memory where its array grew: written as the array's bytes, as
// upstream does, they made an image of a world with something in a sensor other bytes from one run to the
// next (see ../TIDE.md). The reader takes them as the array's bytes, as before.
_Static_assert( sizeof( b2Visitor ) == 8 && offsetof( b2Visitor, generation ) == 4, "b2Visitor changed" );

static void b2SerVisitors( b2RecBuffer* buf, const b2Visitor* visitors, int count )
{
	b2SnapW_I32( buf, count );
	for ( int i = 0; i < count; ++i )
	{
		uint8_t bytes[sizeof( b2Visitor )] = { 0 };
		memcpy( bytes, &visitors[i].shapeId, sizeof( int ) );
		memcpy( bytes + offsetof( b2Visitor, generation ), &visitors[i].generation, sizeof( uint16_t ) );
		b2SnapW_Bytes( buf, bytes, (int)sizeof( bytes ) );
	}
}

// Id pool: nextIndex + freeArray
static void b2SerIdPool( b2RecBuffer* buf, const b2IdPool* pool )
{
	b2SnapW_I32( buf, pool->nextIndex );
	b2SerPodArray( buf, pool->freeArray );
}

static void b2DesIdPool( b2SnapReader* r, b2IdPool* pool )
{
	pool->nextIndex = b2SnapR_I32( r );
	b2DesPodArray( r, pool->freeArray );
}

// BitSet: blockCount + raw uint64_t words
static void b2SerBitSet( b2RecBuffer* buf, const b2BitSet* bs )
{
	b2SnapW_U32( buf, bs->blockCount );
	if ( bs->blockCount > 0 )
	{
		b2SnapW_Bytes( buf, bs->bits, (int)( bs->blockCount * sizeof( uint64_t ) ) );
	}
}

// Restore a bitset, leak-clean: destroy the existing one, alloc fresh at exact blockCount capacity.
// Always keep bits non-NULL, matching b2CreateBitSet, so a later b2GrowBitSet on a count-0 bitset
// has a buffer to grow from. An empty color bitset serializes as 0 blocks but must restore usable.
static void b2DesBitSet( b2SnapReader* r, b2BitSet* bs )
{
	uint32_t blockCount = b2SnapR_U32( r );
	if ( r->ok && b2SnapCheckCount( r, (int)blockCount, (int)sizeof( uint64_t ), (int)sizeof( uint64_t ) ) == false )
	{
		r->ok = false;
	}
	b2DestroyBitSet( bs );
	if ( !r->ok )
	{
		return;
	}
	uint32_t blockCapacity = blockCount > 0 ? blockCount : 1;
	bs->bits = b2Alloc( blockCapacity * sizeof( uint64_t ) );
	memset( bs->bits, 0, blockCapacity * sizeof( uint64_t ) );
	bs->blockCapacity = blockCapacity;
	bs->blockCount = blockCount;
	if ( blockCount > 0 )
	{
		b2SnapR_Bytes( r, bs->bits, (int)( blockCount * sizeof( uint64_t ) ) );
	}
}

// HashSet (pairSet): raw items at full capacity, probe order depends on it
static void b2SerHashSet( b2RecBuffer* buf, const b2HashSet* hs )
{
	b2SnapW_U32( buf, hs->capacity );
	b2SnapW_U32( buf, hs->count );
	if ( hs->capacity > 0 )
	{
		// Tide: which an image in parts has where they are (b2SnapW_Ref)
		b2SnapW_Ref( buf, hs->items, (int)( hs->capacity * sizeof( b2SetItem ) ) );
	}
}

static void b2DesHashSet( b2SnapReader* r, b2HashSet* hs )
{
	uint32_t cap = b2SnapR_U32( r );
	uint32_t cnt = b2SnapR_U32( r );
	// Probing masks with capacity-1, so capacity must be a power of two and count can't exceed it.
	// (cap & (cap-1)) == 0 also accepts 0, which the empty branch handles.
	bool valid = b2SnapCheckCount( r, (int)cap, (int)sizeof( b2SetItem ), (int)sizeof( b2SetItem ) ) && ( cap & ( cap - 1 ) ) == 0 &&
				 cnt <= cap;
	if ( r->ok && valid == false )
	{
		r->ok = false;
	}
	// Destroy the fresh empty set the shell gave us
	b2DestroySet( hs );
	if ( !r->ok )
	{
		return;
	}
	if ( cap > 0 )
	{
		hs->items = b2Alloc( cap * sizeof( b2SetItem ) );
		hs->capacity = cap;
		hs->count = cnt;
		b2SnapR_Bytes( r, hs->items, (int)( cap * sizeof( b2SetItem ) ) );
	}
	else
	{
		hs->items = NULL;
		hs->capacity = 0;
		hs->count = 0;
	}
}

static void b2SerTree( b2RecBuffer* buf, const b2DynamicTree* tree )
{
	b2SnapW_I32( buf, tree->nodeEnd );
	// Tide: the capacity is an allocation hint, which a restore trims. An image has to be the same bytes
	// whether its world was ever restored or not, so the hint is what a restore would make of it.
	b2SnapW_I32( buf, 2 * tree->nodeEnd );
	b2SnapW_I32( buf, tree->pairFreeList );
	b2SnapW_I32( buf, tree->proxyCount );
	b2SnapW_I32( buf, tree->proxyCapacity );
	b2SnapW_I32( buf, tree->proxyFreeList );
	b2SnapW_I32( buf, tree->dfsOrdered ? 1 : 0 );

	// Tide: which an image in parts has where they are (b2SnapW_Ref)
	b2SnapW_Ref( buf, tree->nodes, tree->nodeEnd * (int)sizeof( b2TreeNode ) );
	b2SnapW_Ref( buf, tree->parents, tree->nodeEnd * (int)sizeof( int32_t ) );
	b2SnapW_Ref( buf, tree->proxies, tree->proxyCapacity * (int)sizeof( b2TreeProxy ) );
}

static void b2DesTree( b2SnapReader* r, b2DynamicTree* tree )
{
	int nodeEnd = b2SnapR_I32( r );
	int nodeCapacity = b2SnapR_I32( r );
	int pairFreeList = b2SnapR_I32( r );
	int proxyCount = b2SnapR_I32( r );
	int proxyCapacity = b2SnapR_I32( r );
	int proxyFreeList = b2SnapR_I32( r );
	int ordered = b2SnapR_I32( r );

	if ( r->ok && ( nodeEnd < 2 || ( nodeEnd & 1 ) != 0 || nodeCapacity < nodeEnd || proxyCount < 0 || proxyCapacity < 1 ||
					proxyCapacity < proxyCount ) )
	{
		r->ok = false;
	}

	// Stream bounds against what is actually written
	if ( r->ok && b2SnapCheckCount( r, nodeEnd, (int)sizeof( b2TreeNode ), (int)sizeof( b2TreeNode ) + (int)sizeof( int32_t ) ) == false )
	{
		r->ok = false;
	}

	if ( r->ok && b2SnapCheckCount( r, proxyCapacity, (int)sizeof( b2TreeProxy ), (int)sizeof( b2TreeProxy ) ) == false )
	{
		r->ok = false;
	}

	// Free what the shell or a live world holds, rebuild scratch included. Destroy zeroes the
	// struct, which covers every field the image does not carry.
	b2DestroyDynamicTree( tree );

	if ( !r->ok )
	{
		// Leave a valid empty tree so the shell can still be destroyed
		*tree = b2CreateDynamicTree( 0 );
		return;
	}

	// This is just an allocation hint and the exact value doesn't matter.
	nodeCapacity = b2MinInt( nodeCapacity, 2 * nodeEnd );

	tree->nodeEnd = nodeEnd;
	tree->nodeCapacity = nodeCapacity;
	tree->pairFreeList = pairFreeList;
	tree->proxyCount = proxyCount;
	tree->proxyCapacity = proxyCapacity;
	tree->proxyFreeList = proxyFreeList;
	tree->dfsOrdered = ordered != 0;

	tree->nodes = b2AllocZero( nodeCapacity * sizeof( b2TreeNode ) );
	b2SnapR_Bytes( r, tree->nodes, nodeEnd * (int)sizeof( b2TreeNode ) );

	tree->parents = b2AllocZero( nodeCapacity * sizeof( int32_t ) );
	b2SnapR_Bytes( r, tree->parents, nodeEnd * (int)sizeof( int32_t ) );

	tree->proxies = b2AllocZero( proxyCapacity * sizeof( b2TreeProxy ) );
	b2SnapR_Bytes( r, tree->proxies, proxyCapacity * (int)sizeof( b2TreeProxy ) );
}

// Solver set: setIndex + 5 POD arrays
static void b2SerSolverSet( b2RecBuffer* buf, const b2SolverSet* set )
{
	b2SnapW_I32( buf, set->setIndex );
	b2SerPodArray( buf, set->bodySims );
	b2SerPodArray( buf, set->bodyStates );
	b2SerPodArray( buf, set->jointSims );
	b2SerPodArray( buf, set->contactSims );
	b2SerPodArray( buf, set->islandSims );
}

static void b2DesSolverSet( b2SnapReader* r, b2SolverSet* set )
{
	set->setIndex = b2SnapR_I32( r );
	b2DesPodArray( r, set->bodySims );
	b2DesPodArray( r, set->bodyStates );
	b2DesPodArray( r, set->jointSims );
	b2DesPodArray( r, set->contactSims );
	b2DesPodArray( r, set->islandSims );
}

// Graph color: bodySet + contactSims + jointSims (overflow color has no bodySet)
static void b2SerGraphColor( b2RecBuffer* buf, const b2GraphColor* color, bool isOverflow )
{
	if ( !isOverflow )
	{
		b2SerBitSet( buf, &color->bodySet );
	}
	b2SerPodArray( buf, color->contactSims );
	b2SerPodArray( buf, color->jointSims );
}

static void b2DesGraphColor( b2SnapReader* r, b2GraphColor* color, bool isOverflow )
{
	if ( !isOverflow )
	{
		b2DesBitSet( r, &color->bodySet );
	}
	b2DesPodArray( r, color->contactSims );
	b2DesPodArray( r, color->jointSims );
	// Transient wideConstraints/overflowConstraints stay NULL, wideConstraintCount 0
}

// World scalar config (simulation settings only, no runtime/shell fields)
// Only simulation scalars belong here, never host or worker state (workerCount,
// scheduler, callbacks, user data). b2World_Restore relies on that so an in-place
// restore preserves the live world's wiring.
static void b2SerWorldConfig( b2RecBuffer* buf, const b2World* world )
{
	b2SnapW_Bytes( buf, &world->gravity, sizeof( b2Vec2 ) );
	b2SnapW_Bytes( buf, &world->hitEventThreshold, sizeof( float ) );
	b2SnapW_Bytes( buf, &world->restitutionThreshold, sizeof( float ) );
	b2SnapW_I32( buf, world->restitutionIterations );
	b2SnapW_Bytes( buf, &world->maxLinearSpeed, sizeof( float ) );
	b2SnapW_Bytes( buf, &world->contactSpeed, sizeof( float ) );
	b2SnapW_Bytes( buf, &world->contactHertz, sizeof( float ) );
	b2SnapW_Bytes( buf, &world->contactDampingRatio, sizeof( float ) );
	b2SnapW_Bytes( buf, &world->contactRecycleDistance, sizeof( float ) );
	b2SnapW_Bytes( buf, &world->stepIndex, sizeof( uint64_t ) );
	b2SnapW_I32( buf, world->splitIslandId );
	// Step scaling cached for the force/torque reporting getters, which run between steps
	b2SnapW_Bytes( buf, &world->inv_h, sizeof( float ) );
	b2SnapW_Bytes( buf, &world->inv_dt, sizeof( float ) );
	// End-event double-buffer parity, so the first post-restore event query reads the right half
	b2SnapW_I32( buf, world->endEventArrayIndex );
	// maxCapacity (b2Capacity struct)
	b2SnapW_Bytes( buf, &world->maxCapacity, sizeof( b2Capacity ) );
	// bool flags packed as individual bytes for layout stability
	uint8_t flags = 0;
	flags |= world->enableSleep ? 0x01u : 0u;
	flags |= world->enableWarmStarting ? 0x02u : 0u;
	flags |= world->enableContinuous ? 0x08u : 0u;
	flags |= world->enableRestitutionPropagation ? 0x10u : 0u;
	b2RecBufAppend( buf, &flags, 1 );
}

static void b2DesWorldConfig( b2SnapReader* r, b2World* world )
{
	b2SnapR_Bytes( r, &world->gravity, sizeof( b2Vec2 ) );
	b2SnapR_Bytes( r, &world->hitEventThreshold, sizeof( float ) );
	b2SnapR_Bytes( r, &world->restitutionThreshold, sizeof( float ) );
	world->restitutionIterations = b2SnapR_I32( r );
	b2SnapR_Bytes( r, &world->maxLinearSpeed, sizeof( float ) );
	b2SnapR_Bytes( r, &world->contactSpeed, sizeof( float ) );
	b2SnapR_Bytes( r, &world->contactHertz, sizeof( float ) );
	b2SnapR_Bytes( r, &world->contactDampingRatio, sizeof( float ) );
	b2SnapR_Bytes( r, &world->contactRecycleDistance, sizeof( float ) );
	b2SnapR_Bytes( r, &world->stepIndex, sizeof( uint64_t ) );
	world->splitIslandId = b2SnapR_I32( r );
	b2SnapR_Bytes( r, &world->inv_h, sizeof( float ) );
	b2SnapR_Bytes( r, &world->inv_dt, sizeof( float ) );
	world->endEventArrayIndex = b2SnapR_I32( r );
	b2SnapR_Bytes( r, &world->maxCapacity, sizeof( b2Capacity ) );
	uint8_t flags = 0;
	b2SnapR_Bytes( r, &flags, 1 );
	world->enableSleep = ( flags & 0x01u ) != 0;
	world->enableWarmStarting = ( flags & 0x02u ) != 0;
	world->enableContinuous = ( flags & 0x08u ) != 0;
	world->enableRestitutionPropagation = ( flags & 0x10u ) != 0;
}

static inline uint8_t* b2SnapPutI32( uint8_t* at, int v )
{
	int32_t w = (int32_t)v;
	memcpy( at, &w, 4 );
	return at + 4;
}

static inline uint8_t* b2SnapPutBytes( uint8_t* at, const void* src, int n )
{
	if ( n > 0 )
	{
		memcpy( at, src, (size_t)n );
	}
	return at + n;
}

// Tide: the islands as upstream writes them, each its four numbers and then its bodies, its contacts and its
// joints, each with how many first. There's an island for every few bodies and most have a few numbers, so
// they're written into room asked for once, where upstream appends each number: the same bytes, at a fraction
// of what they cost, in every image.
_Static_assert( sizeof( b2ContactLink ) == 12 && sizeof( b2JointLink ) == 12, "an island's links in an image" );

static void b2SerIslands( b2RecBuffer* buf, const b2World* world )
{
	int islandCount = world->islands.count;
	const b2Island* islands = world->islands.data;

	int size = 4;
	for ( int i = 0; i < islandCount; ++i )
	{
		const b2Island* island = islands + i;
		size += 7 * 4 + island->bodies.count * 4 + island->contacts.count * 12 + island->joints.count * 12;
	}

	uint8_t* at = b2RecBufReserve( buf, size );
	if ( at == NULL )
	{
		// Only counted
		return;
	}

	const uint8_t* end = at + size;
	at = b2SnapPutI32( at, islandCount );
	for ( int i = 0; i < islandCount; ++i )
	{
		const b2Island* island = islands + i;
		at = b2SnapPutI32( at, island->setIndex );
		at = b2SnapPutI32( at, island->localIndex );
		at = b2SnapPutI32( at, island->islandId );
		at = b2SnapPutI32( at, island->constraintRemoveCount );
		at = b2SnapPutI32( at, island->bodies.count );
		at = b2SnapPutBytes( at, island->bodies.data, island->bodies.count * 4 );
		at = b2SnapPutI32( at, island->contacts.count );
		at = b2SnapPutBytes( at, island->contacts.data, island->contacts.count * 12 );
		at = b2SnapPutI32( at, island->joints.count );
		at = b2SnapPutBytes( at, island->joints.data, island->joints.count * 12 );
	}
	B2_ASSERT( at == end );
	B2_UNUSED( end );
}

void b2SerializeWorld( b2World* world, b2RecBuffer* buf )
{
	// Image header
	b2SnapHeader hdr;
	hdr.magic = B2_SNAP_MAGIC;
	hdr.version = B2_SNAP_VERSION;
	hdr.layoutHash = b2ComputeLayoutHash();
	hdr.flags = B2_ENABLE_VALIDATION ? B2_SNAP_FLAG_VALIDATION : 0u;
	if ( b2IsDoublePrecision() )
	{
		hdr.flags |= B2_SNAP_FLAG_DOUBLE_PRECISION;
	}
	if ( buf->bases != NULL )
	{
		hdr.flags |= B2_SNAP_FLAG_SECTIONS;
	}
	b2SnapW_Bytes( buf, &hdr, (int)sizeof( hdr ) );
	if ( buf->bases != NULL )
	{
		b2SnapW_Bytes( buf, buf->bases, b2_sectionCount * (int)sizeof( int ) );
	}

	// World config
	b2SnapSection( buf, b2_sectionConfig );
	b2SerWorldConfig( buf, world );

	// 7 id pools
	b2SerIdPool( buf, &world->bodyIdPool );
	b2SerIdPool( buf, &world->shapeIdPool );
	b2SerIdPool( buf, &world->chainIdPool );
	b2SerIdPool( buf, &world->contactIdPool );
	b2SerIdPool( buf, &world->jointIdPool );
	b2SerIdPool( buf, &world->islandIdPool );
	b2SerIdPool( buf, &world->solverSetIdPool );

	// Solver sets
	// Tide: nothing writes the static set, the disabled set or a sleeping set but what says so
	// (b2SnapTouchSet), so an image in parts leaves out the ones nothing was done to. How many sets there are
	// is written with the first two.
	b2SnapTrack* track = &world->snapTrack;
	b2SnapSection( buf, b2_sectionFixedSets );
	int setCount = world->solverSets.count;
	bool keepFixed = b2SnapKeepSection( buf, track->fixedTouched || setCount != track->setCount );
	if ( keepFixed == false )
	{
		b2SnapW_I32( buf, setCount );
	}
	for ( int i = 0; i < setCount; ++i )
	{
		if ( i == b2_awakeSet )
		{
			b2SnapSection( buf, b2_sectionAwakeSet );
		}
		else if ( i == b2_firstSleepingSet )
		{
			b2SnapSection( buf, b2_sectionSleepingSets );
			if ( b2SnapKeepSection( buf, track->setsTouched || setCount != track->setCount ) )
			{
				break;
			}
		}
		else if ( i < b2_awakeSet && keepFixed )
		{
			continue;
		}
		b2SerSolverSet( buf, world->solverSets.data + i );
	}
	// Tide: every section is come to, in order, whatever the world has: an image in parts writes zeros where
	// a section had more in the image before, as it gets to the next one
	if ( setCount <= b2_awakeSet )
	{
		b2SnapSection( buf, b2_sectionAwakeSet );
	}
	if ( setCount <= b2_firstSleepingSet )
	{
		b2SnapSection( buf, b2_sectionSleepingSets );
	}

	// Sparse arrays. Bodies, shapes and joints carry a host userData pointer scrubbed to NULL on write.
	// Contacts have no userData, so they go out as raw POD.
	// Tide: a body, a shape, its fat bounds and a contact each take the same room, so an image in parts
	// has the ones that may have changed, each at its place (b2SnapTrack). The joints are all written.
	b2SnapSection( buf, b2_sectionBodies );
	b2SerSimArray( buf, world->bodies, b2Body, &track->bodies );
	b2SnapSection( buf, b2_sectionShapes );
	b2SerSimArray( buf, world->shapes, b2Shape, &track->shapes );
	b2SnapSection( buf, b2_sectionFatAABBs );
	b2SerPodRuns( buf, world->fatAABBs, &track->shapes );
	b2SnapSection( buf, b2_sectionContacts );
	b2SerPodRuns( buf, world->contacts, &track->contacts );
	b2SnapSection( buf, b2_sectionJoints );
	b2SerSimArray( buf, world->joints, b2Joint, (const b2SnapSet*)NULL );

	// Chain shapes: POD scalars then per-live-slot heap arrays
	b2SnapSection( buf, b2_sectionChains );
	int chainCount = world->chainShapes.count;
	b2SnapW_I32( buf, chainCount );
	for ( int i = 0; i < chainCount; ++i )
	{
		b2ChainShape* chain = world->chainShapes.data + i;
		// Write POD scalars
		b2SnapW_I32( buf, chain->id );
		b2SnapW_I32( buf, chain->bodyId );
		b2SnapW_I32( buf, chain->nextChainId );
		b2SnapW_I32( buf, chain->segmentCount );
		b2SnapW_Bytes( buf, &chain->generation, sizeof( uint16_t ) );
		if ( chain->id != B2_NULL_INDEX )
		{
			// Live slot: write the two heap arrays
			b2SnapW_Bytes( buf, chain->shapeIndices, chain->segmentCount * (int)sizeof( int ) );
		}
	}

	// Sensors: shapeId + 3 visitor arrays per slot
	b2SnapSection( buf, b2_sectionSensors );
	int sensorCount = world->sensors.count;
	b2SnapW_I32( buf, sensorCount );
	for ( int i = 0; i < sensorCount; ++i )
	{
		b2Sensor* s = world->sensors.data + i;
		b2SnapW_I32( buf, s->shapeId );
		// Tide: without the bytes of a visitor that no field owns
		b2SerVisitors( buf, s->hits.data, s->hits.count );
		b2SerVisitors( buf, s->overlaps1.data, s->overlaps1.count );
		b2SerVisitors( buf, s->overlaps2.data, s->overlaps2.count );
	}

	// Islands: POD scalars + 3 inner arrays per slot
	b2SnapSection( buf, b2_sectionIslands );
	b2SerIslands( buf, world );

	// Broad phase
	b2BroadPhase* bp = &world->broadPhase;
	for ( int t = 0; t < b2_bodyTypeCount; ++t )
	{
		b2SnapSection( buf, b2_sectionStaticTree + t );
		b2SerTree( buf, &bp->trees[t] );
	}
	b2SnapSection( buf, b2_sectionPairs );
	// Tide: a pair comes and goes with its contact, which says so (b2SnapTouchPairs)
	if ( b2SnapKeepSection( buf, track->pairsTouched ) == false )
	{
		b2SerHashSet( buf, &bp->pairSet );
	}

	// Constraint graph: B2_GRAPH_COLOR_COUNT colors
	b2SnapSection( buf, b2_sectionGraph );
	b2ConstraintGraph* graph = &world->constraintGraph;
	for ( int c = 0; c < B2_GRAPH_COLOR_COUNT; ++c )
	{
		b2SerGraphColor( buf, &graph->colors[c], c == B2_OVERFLOW_INDEX );
	}
	b2SnapSection( buf, b2_sectionCount ); // Tide: the last one's size
}

// Free per-object heap the overwrite steps below don't reach, so restoring over a
// populated world doesn't leak. Outer arrays stay alive for the steps to reuse.
// Mirrors the per-object teardown in b2DestroyWorld.
static void b2FreeLiveSimElements( b2World* world )
{
	for ( int i = 0; i < world->chainShapes.count; ++i )
	{
		b2ChainShape* chain = world->chainShapes.data + i;
		if ( chain->id != B2_NULL_INDEX )
		{
			b2FreeChainData( chain );
		}
	}

	for ( int i = 0; i < world->sensors.count; ++i )
	{
		b2Sensor* sensor = world->sensors.data + i;
		b2Array_Destroy( sensor->hits );
		b2Array_Destroy( sensor->overlaps1 );
		b2Array_Destroy( sensor->overlaps2 );
	}

	for ( int i = 0; i < world->islands.count; ++i )
	{
		b2Island* island = world->islands.data + i;
		b2Array_Destroy( island->bodies );
		b2Array_Destroy( island->contacts );
		b2Array_Destroy( island->joints );
	}
}

// Overwrite a world with the simulation state from the reader. Mirrors the write
// order in b2SerializeWorld. The world must be a clean shell from b2CreateWorld, or
// a live world whose per-object heap was first freed by b2FreeLiveSimElements. Host
// wiring (scheduler, callbacks, user data) is never touched. Returns false on a
// corrupt image.
static bool b2DeserializeIntoShell( b2SnapReader* r, b2World* world )
{
	// Tide: the world becomes another: its next image is written whole (see b2SnapTrack)
	b2SnapTouchAll( world );

	// Step 1: world scalars
	b2SnapSeek( r, b2_sectionConfig );
	b2DesWorldConfig( r, world );

	// Step 2: 7 id pools (overwrite entirely, including solverSetIdPool from the 3 pre-created sets)
	b2DesIdPool( r, &world->bodyIdPool );
	b2DesIdPool( r, &world->shapeIdPool );
	b2DesIdPool( r, &world->chainIdPool );
	b2DesIdPool( r, &world->contactIdPool );
	b2DesIdPool( r, &world->jointIdPool );
	b2DesIdPool( r, &world->islandIdPool );
	b2DesIdPool( r, &world->solverSetIdPool );

	// Step 3: solver sets
	// (a) Destroy the 5 inner arrays of the 3 pre-created sets WITHOUT freeing the set id
	//     (b2DestroySolverSet would do that, but we already authored the id pool above)
	for ( int i = 0; i < world->solverSets.count; ++i )
	{
		b2SolverSet* set = world->solverSets.data + i;
		b2Array_Destroy( set->bodySims );
		b2Array_Destroy( set->bodyStates );
		b2Array_Destroy( set->contactSims );
		b2Array_Destroy( set->jointSims );
		b2Array_Destroy( set->islandSims );
	}

	// Each set writes at least setIndex plus 5 array counts
	b2SnapSeek( r, b2_sectionFixedSets );
	int setCount = b2SnapR_I32( r );
	if ( r->ok && b2SnapCheckCount( r, setCount, (int)sizeof( b2SolverSet ), 6 * (int)sizeof( int ) ) == false )
	{
		r->ok = false;
	}

	if ( r->ok )
	{
		// (b) Resize outer array to hold all sets, zeroing new slots so their headers are clean
		b2Array_ResizeAndSetZero( world->solverSets, setCount );

		// (c) Deserialize each set
		for ( int i = 0; i < setCount; ++i )
		{
			if ( i == b2_awakeSet )
			{
				b2SnapSeek( r, b2_sectionAwakeSet );
			}
			else if ( i == b2_firstSleepingSet )
			{
				b2SnapSeek( r, b2_sectionSleepingSets );
			}
			b2DesSolverSet( r, world->solverSets.data + i );
		}
	}

	// Step 4: sparse arrays. userData was written as NULL by the serializer, so a plain copy restores
	// it cleanly with no host pointers to scrub here.
	b2SnapSeek( r, b2_sectionBodies );
	b2DesPodArray( r, world->bodies );
	b2SnapSeek( r, b2_sectionShapes );
	b2DesPodArray( r, world->shapes );
	b2SnapSeek( r, b2_sectionFatAABBs );
	b2DesPodArray( r, world->fatAABBs );
	b2SnapSeek( r, b2_sectionContacts );
	b2DesPodArray( r, world->contacts );
	b2SnapSeek( r, b2_sectionJoints );
	b2DesPodArray( r, world->joints );

	// Step 5: chain shapes
	b2SnapSeek( r, b2_sectionChains );
	{
		// Destroy the shell's chainShapes array (empty, but has a backing allocation)
		b2Array_Destroy( world->chainShapes );
		b2Array_Create( world->chainShapes );

		// Each chain writes 4 ints plus a uint16 generation
		int chainCount = b2SnapR_I32( r );
		if ( r->ok && b2SnapCheckCount( r, chainCount, (int)sizeof( b2ChainShape ), 4 * (int)sizeof( int ) + (int)sizeof( uint16_t ) ) == false )
		{
			r->ok = false;
		}
		if ( r->ok )
		{
			b2Array_Resize( world->chainShapes, chainCount );
			// Zero the whole array so free slots have NULL pointers
			memset( world->chainShapes.data, 0, chainCount * sizeof( b2ChainShape ) );
		}

		for ( int i = 0; i < chainCount && r->ok; ++i )
		{
			b2ChainShape* chain = world->chainShapes.data + i;
			chain->id = b2SnapR_I32( r );
			chain->bodyId = b2SnapR_I32( r );
			chain->nextChainId = b2SnapR_I32( r );
			chain->segmentCount = b2SnapR_I32( r );
			b2SnapR_Bytes( r, &chain->generation, sizeof( uint16_t ) );
			// A partial read leaves id as 0, which is a valid slot value, so gate the live branch on r->ok
			if ( r->ok && chain->id != B2_NULL_INDEX )
			{
				if ( b2SnapCheckCount( r, chain->segmentCount, (int)sizeof( int ), (int)sizeof( int ) ) == false )
				{
					r->ok = false;
					break;
				}
				// Live slot: allocate and copy heap arrays
				chain->shapeIndices = b2Alloc( chain->segmentCount * (int)sizeof( int ) );
				b2SnapR_Bytes( r, chain->shapeIndices, chain->segmentCount * (int)sizeof( int ) );
			}
			else
			{
				// Free slot must have NULL pointers; zero init above handles this
				chain->shapeIndices = NULL;
			}
		}
	}

	// Step 6: sensors
	b2SnapSeek( r, b2_sectionSensors );
	{
		// Destroy the shell's sensors array
		b2Array_Destroy( world->sensors );
		b2Array_Create( world->sensors );

		// Each sensor writes shapeId plus 3 array counts
		int sensorCount = b2SnapR_I32( r );
		if ( r->ok && b2SnapCheckCount( r, sensorCount, (int)sizeof( b2Sensor ), 4 * (int)sizeof( int ) ) == false )
		{
			r->ok = false;
		}
		if ( r->ok )
		{
			b2Array_Resize( world->sensors, sensorCount );
			// Zero so inner array headers start clean
			memset( world->sensors.data, 0, sensorCount * sizeof( b2Sensor ) );
		}

		for ( int i = 0; i < sensorCount && r->ok; ++i )
		{
			b2Sensor* s = world->sensors.data + i;
			s->shapeId = b2SnapR_I32( r );
			// Re-init inner arrays then fill them
			b2Array_Create( s->hits );
			b2Array_Create( s->overlaps1 );
			b2Array_Create( s->overlaps2 );
			b2DesPodArray( r, s->hits );
			b2DesPodArray( r, s->overlaps1 );
			b2DesPodArray( r, s->overlaps2 );
		}
	}

	// Step 7: islands
	b2SnapSeek( r, b2_sectionIslands );
	{
		// Destroy the shell's islands array
		b2Array_Destroy( world->islands );
		b2Array_Create( world->islands );

		// Each island writes 4 ints plus 3 array counts
		int islandCount = b2SnapR_I32( r );
		if ( r->ok && b2SnapCheckCount( r, islandCount, (int)sizeof( b2Island ), 7 * (int)sizeof( int ) ) == false )
		{
			r->ok = false;
		}
		if ( r->ok )
		{
			b2Array_Resize( world->islands, islandCount );
			memset( world->islands.data, 0, islandCount * sizeof( b2Island ) );
		}

		for ( int i = 0; i < islandCount && r->ok; ++i )
		{
			b2Island* island = world->islands.data + i;
			island->setIndex = b2SnapR_I32( r );
			island->localIndex = b2SnapR_I32( r );
			island->islandId = b2SnapR_I32( r );
			island->constraintRemoveCount = b2SnapR_I32( r );
			b2Array_Create( island->bodies );
			b2Array_Create( island->contacts );
			b2Array_Create( island->joints );
			b2DesPodArray( r, island->bodies );
			b2DesPodArray( r, island->contacts );
			b2DesPodArray( r, island->joints );
		}
	}

	// Step 8: broad phase
	{
		b2BroadPhase* bp = &world->broadPhase;

		// Trees: the shell already allocated tree nodes; free them and replace
		for ( int t = 0; t < b2_bodyTypeCount; ++t )
		{
			b2SnapSeek( r, b2_sectionStaticTree + t );
			b2DesTree( r, &bp->trees[t] );
		}

		// pairSet
		b2SnapSeek( r, b2_sectionPairs );
		b2DesHashSet( r, &bp->pairSet );
	}

	// Step 9: constraint graph
	b2SnapSeek( r, b2_sectionGraph );
	{
		b2ConstraintGraph* graph = &world->constraintGraph;
		for ( int c = 0; c < B2_GRAPH_COLOR_COUNT; ++c )
		{
			b2DesGraphColor( r, &graph->colors[c], c == B2_OVERFLOW_INDEX );
		}
	}

	// Event buffers are transient and never serialized. An in-place restore reuses the live
	// world's arrays, so end events queued by a between-step mutator (b2Body_Disable and friends)
	// would survive the restore and surface after the first resimmed step. Reset to match a fresh
	// world from snapshot. The double-buffer parity is restored, but the contents must start empty.
	b2Array_Clear( world->bodyMoveEvents );
	b2Array_Clear( world->sensorBeginEvents );
	b2Array_Clear( world->sensorEndEvents[0] );
	b2Array_Clear( world->sensorEndEvents[1] );
	b2Array_Clear( world->contactBeginEvents );
	b2Array_Clear( world->contactEndEvents[0] );
	b2Array_Clear( world->contactEndEvents[1] );
	b2Array_Clear( world->contactHitEvents );
	b2Array_Clear( world->jointEvents );

	return r->ok;
}

// Validate a snapshot image header and arm the reader just past it. Returns false on a rejected image
// (too small, bad magic, wrong version, layout mismatch), leaving the caller's world untouched. A
// layout mismatch caused purely by differing validation builds is called out, since the validation
// gated fields change struct sizes and such images can never share a layout.
static bool b2OpenSnapshotImage( const uint8_t* image, int size, b2SnapReader* r )
{
	if ( image == NULL || size < (int)sizeof( b2SnapHeader ) )
	{
		return false;
	}

	b2SnapHeader hdr;
	memcpy( &hdr, image, sizeof( hdr ) );
	if ( hdr.magic != B2_SNAP_MAGIC || hdr.version != B2_SNAP_VERSION )
	{
		return false;
	}

	// World positions are stored at the build precision, so the layout differs irreconcilably across
	// modes. Called out before the layout hash so the cause is clear rather than a generic mismatch.
	bool imageDouble = ( hdr.flags & B2_SNAP_FLAG_DOUBLE_PRECISION ) != 0;
	bool buildDouble = b2IsDoublePrecision();
	if ( imageDouble != buildDouble )
	{
		b2Log( "snapshot precision mismatch: image %s, this build %s\n", imageDouble ? "double" : "float",
			   buildDouble ? "double" : "float" );
		return false;
	}

	if ( hdr.layoutHash != b2ComputeLayoutHash() )
	{
		bool imageValidation = ( hdr.flags & B2_SNAP_FLAG_VALIDATION ) != 0;
		if ( imageValidation != (bool)B2_ENABLE_VALIDATION )
		{
			b2Log( "snapshot layout mismatch: image built with validation %s, this build %s\n", imageValidation ? "on" : "off",
				   B2_ENABLE_VALIDATION ? "on" : "off" );
		}
		else
		{
			b2Log( "snapshot layout mismatch\n" );
		}
		return false;
	}

	r->data = image;
	r->cursor = (int)sizeof( hdr );
	r->size = size;
	r->ok = true;

	// Tide: where its sections start, each after the one before it and inside the image
	r->sectioned = ( hdr.flags & B2_SNAP_FLAG_SECTIONS ) != 0;
	if ( r->sectioned )
	{
		b2SnapR_Bytes( r, r->bases, (int)sizeof( r->bases ) );
		int last = r->cursor;
		for ( int i = 0; i < b2_sectionCount && r->ok; ++i )
		{
			if ( r->bases[i] < last || r->bases[i] > size )
			{
				r->ok = false;
			}
			last = r->bases[i];
		}
		if ( r->ok == false )
		{
			return false;
		}
	}
	return true;
}

b2WorldId b2CreateWorldFromSnapshot( const uint8_t* image, int size, int workerCount )
{
	b2WorldId nullId = b2_nullWorldId;

	b2SnapReader readerStorage;
	b2SnapReader* r = &readerStorage;
	if ( b2OpenSnapshotImage( image, size, r ) == false )
	{
		return nullId;
	}

	// Build a minimal valid def so b2CreateWorld produces a fully valid shell
	b2WorldDef def = b2DefaultWorldDef();
	def.workerCount = workerCount;
	def.enqueueTask = NULL;
	def.finishTask = NULL;
	def.userTaskContext = NULL;

	// Capacity is only a sizing hint. Every container is resized from the image below
	b2WorldId id = b2CreateWorld( &def );
	if ( !b2World_IsValid( id ) )
	{
		return nullId;
	}

	b2World* world = b2GetWorldFromId( id );

	if ( b2DeserializeIntoShell( r, world ) == false )
	{
		// Image was corrupt; clean up by destroying the world
		b2DestroyWorld( id );
		return nullId;
	}

	// A world loaded from scratch carries no host pointers
	world->preSolveFcn = NULL;
	world->preSolveContext = NULL;
	world->customFilterFcn = NULL;
	world->customFilterContext = NULL;
	world->userData = NULL;

	return id;
}

bool b2World_Restore( b2WorldId worldId, const uint8_t* image, int size )
{
	// Validate the image fully before touching the world so a bad image leaves it intact
	b2SnapReader readerStorage;
	b2SnapReader* r = &readerStorage;
	if ( b2OpenSnapshotImage( image, size, r ) == false )
	{
		return false;
	}

	b2World* world = b2GetWorldFromId( worldId );

	// Restoring mid step would corrupt an in-flight solve
	B2_ASSERT( world->locked == false );
	if ( world->locked )
	{
		return false;
	}

	// Point of no return. The slot, generation, and host wiring are kept, so held ids
	// resolve into the rebuilt world. A truncated payload past here leaves the world
	// unusable and the caller must destroy it.
	b2FreeLiveSimElements( world );

	return b2DeserializeIntoShell( r, world );
}

int b2World_GetSnapshot( b2WorldId worldId, uint8_t* image, int capacity )
{
	b2World* world = b2GetWorldFromId( worldId );

	// Serializing mid step would capture an inconsistent, in-flight world
	B2_ASSERT( world->locked == false );
	if ( world->locked )
	{
		return 0;
	}

	// Size query: count the bytes without allocating or copying the whole image
	if ( image == NULL )
	{
		b2RecBuffer counter = { 0 };
		counter.countOnly = true;
		b2SerializeWorld( world, &counter );
		return counter.size;
	}

	// Tide: straight into the caller's memory, once. Upstream writes the image into memory of its own,
	// which grows as it goes, and copies it over: three times the work for an image taken every step.
	// An image that doesn't fit is partly written, and its whole size is what's returned, as before.
	b2RecBuffer buf = { 0 };
	buf.data = image;
	buf.capacity = capacity;
	buf.fixed = true;
	b2SerializeWorld( world, &buf );
	return buf.size;
}

// Tide: which bodies, shapes and contacts an image in parts writes (see ../TIDE.md, and b2SnapTrack in
// physics_world.h).
//
// Between two images, a body, a shape with its fat bounds, or a contact has changed only if
// - the body, or the shape's body, or one of the contact's two, was awake at the first image, at the second,
//   or at some time between: the step writes those, on its worker threads, and nothing says which;
// - or something was done to it from outside the step's workers, which is said where it's done
//   (b2SnapTouchBody and the like): a contact that starts or ends changes the lists of both its bodies and
//   its neighbors in them, an element taken out of an array moves another into its place, and calls change
//   what they're for.
// A body that's awake at some time between and at neither image woke and fell asleep, or was made and
// destroyed, and each of those says so for the body, its shapes and its contacts. So the sets hold: what was
// awake at the last image (b2SnapCommit), what was said since, and what's awake now (b2SnapSeed).
//
// What a contact is in the solver (its manifold, its impulses) isn't in the contact but in its body's solver
// set, or in the constraint graph: the awake set and the graph are written every time, and the other sets
// when something says they were written (b2SnapTouchSet).
void b2SnapGrowSet( b2SnapSet* set, int blockCount )
{
	// Room to spare, as ids come one after another
	int grown = blockCount + blockCount / 2 + 4;
	uint64_t* bits = b2Alloc( (size_t)grown * sizeof( uint64_t ) );
	memset( bits, 0, (size_t)grown * sizeof( uint64_t ) );
	if ( set->blockCount > 0 )
	{
		memcpy( bits, set->bits, (size_t)set->blockCount * sizeof( uint64_t ) );
		b2Free( set->bits, (size_t)set->blockCount * sizeof( uint64_t ) );
	}
	set->bits = bits;
	set->blockCount = grown;
}

static void b2SnapClearSet( b2SnapSet* set )
{
	if ( set->blockCount > 0 )
	{
		memset( set->bits, 0, (size_t)set->blockCount * sizeof( uint64_t ) );
	}
}

void b2SnapTouchBodyDeep( b2World* world, const b2Body* body )
{
	b2SnapTrack* track = &world->snapTrack;
	if ( track->sparse == false || body->id == B2_NULL_INDEX )
	{
		return;
	}

	b2SnapSetBit( &track->bodies, body->id );

	int shapeId = body->headShapeId;
	while ( shapeId != B2_NULL_INDEX )
	{
		b2SnapSetBit( &track->shapes, shapeId );
		shapeId = world->shapes.data[shapeId].nextShapeId;
	}

	int contactKey = body->headContactKey;
	while ( contactKey != B2_NULL_INDEX )
	{
		int contactId = contactKey >> 1;
		b2SnapSetBit( &track->contacts, contactId );
		contactKey = world->contacts.data[contactId].edges[contactKey & 1].nextKey;
	}
}

void b2SnapNoteOp( b2World* world, int op )
{
	switch ( op )
	{
		// The step, and the calls that say what they write where they write it
		case b2_recOpStep:
		case b2_recOpCreateBody:
		case b2_recOpDestroyBody:
		case b2_recOpBodySetTransform:
		case b2_recOpBodySetLinearVelocity:
		case b2_recOpBodySetAngularVelocity:
		case b2_recOpBodyApplyLinearImpulse:
		case b2_recOpBodyApplyLinearImpulseToCenter:
		case b2_recOpBodyApplyAngularImpulse:
		case b2_recOpBodySetAwake:
		case b2_recOpBodyWakeTouching:
		case b2_recOpCreateCircleShape:
		case b2_recOpCreateCapsuleShape:
		case b2_recOpCreateSegmentShape:
		case b2_recOpCreatePolygonShape:
		case b2_recOpDestroyShape:
		// A setting of a body's, which is in its solver set's copy of it: said where it's written
		case b2_recOpBodySetLinearDamping:
		case b2_recOpBodySetAngularDamping:
		case b2_recOpBodySetGravityScale:
		// A setting of the world's, which every image writes. Turning sleeping off also wakes every set,
		// which says what it writes (b2WakeSolverSet).
		case b2_recOpWorldSetGravity:
		case b2_recOpWorldEnableSleeping:
		case b2_recOpWorldEnableContinuous:
		case b2_recOpWorldEnableWarmStarting:
		case b2_recOpWorldSetRestitutionThreshold:
		case b2_recOpWorldSetRestitutionIterations:
		case b2_recOpWorldEnableRestitutionPropagation:
		case b2_recOpWorldSetHitEventThreshold:
		case b2_recOpWorldSetContactTuning:
		case b2_recOpWorldSetContactRecycleDistance:
		case b2_recOpWorldSetMaximumLinearSpeed:
		// What changes nothing an image holds
		case b2_recOpDestroyWorld:
			return;

		// Any other: the next image is written whole
		default:
			b2SnapTouchAll( world );
			return;
	}
}

// How many of a world's bodies can be awake for its images to be written in parts: one in this many. With
// more, there's too little of an image left out for what finding it costs.
#define B2_SNAP_AWAKE_SHARE 2

static bool b2SnapMostlyAsleep( const b2World* world )
{
	int share = world->snapTrack.awakeShare > 0 ? world->snapTrack.awakeShare : B2_SNAP_AWAKE_SHARE;
	int awake = world->solverSets.data[b2_awakeSet].bodySims.count;
	return awake * share <= b2GetIdCount( &world->bodyIdPool );
}

// Every awake body, with its shapes and its contacts, into the sets
static void b2SnapSeed( b2World* world )
{
	const b2SolverSet* awakeSet = world->solverSets.data + b2_awakeSet;
	const b2BodySim* sims = awakeSet->bodySims.data;
	int count = awakeSet->bodySims.count;
	for ( int i = 0; i < count; ++i )
	{
		b2SnapTouchBodyDeep( world, world->bodies.data + sims[i].bodyId );
	}
}

// An image in sections was taken of the world, whole or in parts: what the next one goes on from
static void b2SnapCommit( b2World* world, const int* bases, const int* sizes )
{
	b2SnapTrack* track = &world->snapTrack;
	memcpy( track->bases, bases, sizeof( track->bases ) );
	memcpy( track->sizes, sizes, sizeof( track->sizes ) );
	track->bodyCount = world->bodies.count;
	track->shapeCount = world->shapes.count;
	track->contactCount = world->contacts.count;
	track->setCount = world->solverSets.count;
	track->fixedTouched = false;
	track->setsTouched = false;
	track->pairsTouched = false;

	track->sparse = b2SnapMostlyAsleep( world );
	if ( track->sparse )
	{
		b2SnapClearSet( &track->bodies );
		b2SnapClearSet( &track->shapes );
		b2SnapClearSet( &track->contacts );
		b2SnapSeed( world );
	}
}

void b2DestroySnapshotTrack( b2World* world )
{
	b2SnapTrack* track = &world->snapTrack;
	b2Free( track->bodies.bits, (size_t)track->bodies.blockCount * sizeof( uint64_t ) );
	b2Free( track->shapes.bits, (size_t)track->shapes.blockCount * sizeof( uint64_t ) );
	b2Free( track->contacts.bits, (size_t)track->contacts.blockCount * sizeof( uint64_t ) );

	b2SnapParts* p = track->parts;
	if ( p != NULL )
	{
		for ( int i = 0; i < p->blockCount; ++i )
		{
			b2Free( p->blocks[i].data, (size_t)p->blocks[i].capacity );
		}
		b2Free( p->blocks, (size_t)p->blockCapacity * sizeof( b2SnapBlock ) );
		b2Free( p->parts, (size_t)p->capacity * sizeof( b2SnapshotPart ) );
		b2Free( p, sizeof( b2SnapParts ) );
	}

	*track = ( b2SnapTrack ){ 0 };
}

void b2World_SetSnapshotAwakeShare( b2WorldId worldId, int share )
{
	b2World* world = b2GetWorldFromId( worldId );
	world->snapTrack.awakeShare = share;
}

// Tide: an image in sections, each at its base (see the top of this file)
int b2GetSnapshotHead( void )
{
	return (int)sizeof( b2SnapHeader ) + b2_sectionCount * (int)sizeof( int );
}

// Tide: how much room each thing takes in a section of things that each take the same, after the section's
// count of them: a body, a shape, its fat bounds, a contact, a joint. 0 for the other sections.
int b2GetSnapshotRecordSize( int section )
{
	switch ( section )
	{
		case b2_sectionBodies:
			return (int)sizeof( b2Body );
		case b2_sectionShapes:
			return (int)sizeof( b2Shape );
		case b2_sectionFatAABBs:
			return (int)sizeof( b2AABB );
		case b2_sectionContacts:
			return (int)sizeof( b2Contact );
		case b2_sectionJoints:
			return (int)sizeof( b2Joint );
		default:
			return 0;
	}
}

// An image in sections written whole. With `last`, it's the one the world's next image in parts goes on from.
static int b2SnapWhole( b2WorldId worldId, uint8_t* image, int capacity, const int* bases, int* sizes, bool last )
{
	b2World* world = b2GetWorldFromId( worldId );
	B2_ASSERT( world->locked == false );
	if ( world->locked )
	{
		return 0;
	}

	int ownSizes[b2_sectionCount] = { 0 };
	b2RecBuffer buf = { 0 };
	buf.data = image;
	buf.capacity = image != NULL ? capacity : 0;
	buf.fixed = true;
	buf.bases = bases;
	buf.sizes = ownSizes;
	b2SerializeWorld( world, &buf );
	if ( sizes != NULL )
	{
		memcpy( sizes, ownSizes, sizeof( ownSizes ) );
	}

	// An image in sections that's all there is what the next one in parts goes on from. Any other leaves
	// the world with none to go on from: the caller is about to ask again.
	if ( last && bases != NULL )
	{
		if ( buf.overran == false && buf.countOnly == false )
		{
			b2SnapCommit( world, bases, ownSizes );
		}
		else
		{
			world->snapTrack.sparse = false;
		}
	}

	return buf.overran ? -1 : buf.size;
}

int b2World_GetSnapshotAt( b2WorldId worldId, uint8_t* image, int capacity, const int* bases, int* sizes )
{
	return b2SnapWhole( worldId, image, capacity, bases, sizes, true );
}

int b2World_CheckSnapshotAt( b2WorldId worldId, uint8_t* image, int capacity, const int* bases, int* sizes )
{
	return b2SnapWhole( worldId, image, capacity, bases, sizes, false );
}

int b2World_GetSnapshotParts( b2WorldId worldId, const int* bases, int* sizes, const b2SnapshotPart** parts, int* partCount )
{
	b2World* world = b2GetWorldFromId( worldId );
	b2SnapTrack* track = &world->snapTrack;
	*parts = NULL;
	*partCount = 0;

	B2_ASSERT( world->locked == false );
	if ( world->locked )
	{
		return 0;
	}

	// Nothing to go on from, or sections that aren't where the last image had them: everything in one that
	// moved would be somewhere else. Or most of the world is awake. Or it has fewer bodies, shapes or
	// contacts than its last image had room for, which only a restore does.
	if ( track->sparse == false || bases == NULL || memcmp( bases, track->bases, sizeof( track->bases ) ) != 0 ||
		 world->bodies.count < track->bodyCount || world->shapes.count < track->shapeCount ||
		 world->contacts.count < track->contactCount || b2SnapMostlyAsleep( world ) == false )
	{
		return 0;
	}

	// What's awake now, with what was awake at the last image and what was said since
	b2SnapSeed( world );

	// A body, a shape or a contact that the last image had no room for goes after the ones it had, which
	// stay where they are: every one of those is written
	for ( int i = track->bodyCount; i < world->bodies.count; ++i )
	{
		b2SnapSetBit( &track->bodies, i );
	}
	for ( int i = track->shapeCount; i < world->shapes.count; ++i )
	{
		b2SnapSetBit( &track->shapes, i );
	}
	for ( int i = track->contactCount; i < world->contacts.count; ++i )
	{
		b2SnapSetBit( &track->contacts, i );
	}

	if ( track->parts == NULL )
	{
		track->parts = b2Alloc( sizeof( b2SnapParts ) );
		memset( track->parts, 0, sizeof( b2SnapParts ) );
	}
	b2SnapParts* p = track->parts;
	b2SnapPartsReset( p, track->sizes );

	int ownSizes[b2_sectionCount] = { 0 };
	b2RecBuffer buf = { 0 };
	buf.fixed = true;
	buf.bases = bases;
	buf.sizes = ownSizes;
	buf.parts = p;
	b2SerializeWorld( world, &buf );
	if ( sizes != NULL )
	{
		memcpy( sizes, ownSizes, sizeof( ownSizes ) );
	}

	if ( buf.overran )
	{
		// No image: the caller takes one whole, with its sections placed again
		track->sparse = false;
		return -1;
	}

	b2SnapCommit( world, bases, ownSizes );
	*parts = p->parts;
	*partCount = p->count;
	return buf.size;
}

bool b2GetSnapshotBases( const uint8_t* image, int size, int* bases )
{
	b2SnapReader reader;
	if ( b2OpenSnapshotImage( image, size, &reader ) == false || reader.sectioned == false )
	{
		return false;
	}
	memcpy( bases, reader.bases, sizeof( reader.bases ) );
	return true;
}

static uint64_t b2FnvMixBytes( uint64_t hash, const void* data, int n )
{
	const uint8_t* p = (const uint8_t*)data;
	for ( int i = 0; i < n; ++i )
	{
		hash = ( hash ^ (uint64_t)p[i] ) * B2_SNAP_FNV_PRIME;
	}
	return hash;
}

static uint64_t b2FnvMixFloat( uint64_t hash, float f )
{
	uint32_t bits;
	memcpy( &bits, &f, 4 );
	return ( hash ^ (uint64_t)bits ) * B2_SNAP_FNV_PRIME;
}

static uint64_t b2FnvMixInt( uint64_t hash, int v )
{
	return ( hash ^ (uint64_t)(uint32_t)v ) * B2_SNAP_FNV_PRIME;
}

uint64_t b2HashWorldStateDeep( b2World* world )
{
	uint64_t hash = B2_SNAP_FNV_INIT;

	// Bodies: same iteration order as b2HashWorldState (sparse array, skip free slots)
	int bodyCount = world->bodies.count;
	for ( int i = 0; i < bodyCount; ++i )
	{
		b2Body* body = world->bodies.data + i;
		if ( body->id != i )
		{
			continue;
		}

		b2BodySim* sim = b2GetBodySim( world, body );

		hash = b2FnvMixPosition( hash, sim->transform.p );
		hash = b2FnvMixFloat( hash, sim->transform.q.c );
		hash = b2FnvMixFloat( hash, sim->transform.q.s );

		b2BodyState* state = b2GetBodyState( world, body );
		if ( state != NULL )
		{
			hash = b2FnvMixFloat( hash, state->linearVelocity.x );
			hash = b2FnvMixFloat( hash, state->linearVelocity.y );
			hash = b2FnvMixFloat( hash, state->angularVelocity );
		}

		// Index bookkeeping
		hash = b2FnvMixInt( hash, body->setIndex );
		hash = b2FnvMixInt( hash, body->localIndex );
	}

	// Contacts: sparse array, skip free slots (colorIndex == B2_NULL_INDEX and setIndex == B2_NULL_INDEX for free)
	int contactCount = world->contacts.count;
	for ( int i = 0; i < contactCount; ++i )
	{
		b2Contact* contact = world->contacts.data + i;
		if ( contact->contactId != i )
		{
			continue;
		}

		hash = b2FnvMixInt( hash, contact->setIndex );
		hash = b2FnvMixInt( hash, contact->colorIndex );
		hash = b2FnvMixInt( hash, contact->localIndex );

		// Manifold points + impulses from the contact sim
		// Exclude B2_ENABLE_VALIDATION-gated bodyIdA/bodyIdB for build-config stability
		b2ContactSim* sim = b2GetContactSim( world, contact );
		if ( sim != NULL )
		{
			b2Manifold* m = &sim->manifold;
			hash = b2FnvMixInt( hash, m->pointCount );
			for ( int p = 0; p < m->pointCount; ++p )
			{
				hash = b2FnvMixFloat( hash, m->points[p].normalImpulse );
				hash = b2FnvMixFloat( hash, m->points[p].tangentImpulse );
				hash = b2FnvMixFloat( hash, m->points[p].totalNormalImpulse );
			}
		}
	}

	// Joints: sparse array, skip free slots
	int jointCount = world->joints.count;
	for ( int i = 0; i < jointCount; ++i )
	{
		b2Joint* joint = world->joints.data + i;
		if ( joint->jointId != i )
		{
			continue;
		}

		hash = b2FnvMixInt( hash, joint->setIndex );
		hash = b2FnvMixInt( hash, joint->colorIndex );
		hash = b2FnvMixInt( hash, joint->localIndex );

		b2JointSim* sim = b2GetJointSim( world, joint );
		if ( sim != NULL )
		{
			// Hash accumulated impulses per joint type
			switch ( sim->type )
			{
				case b2_distanceJoint:
					hash = b2FnvMixFloat( hash, sim->distanceJoint.impulse );
					hash = b2FnvMixFloat( hash, sim->distanceJoint.lowerImpulse );
					hash = b2FnvMixFloat( hash, sim->distanceJoint.upperImpulse );
					hash = b2FnvMixFloat( hash, sim->distanceJoint.motorImpulse );
					break;
				case b2_moverJoint:
					hash = b2FnvMixFloat( hash, sim->moverJoint.linearVelocityImpulse.x );
					hash = b2FnvMixFloat( hash, sim->moverJoint.linearVelocityImpulse.y );
					break;
				case b2_motorJoint:
					hash = b2FnvMixFloat( hash, sim->motorJoint.linearVelocityImpulse.x );
					hash = b2FnvMixFloat( hash, sim->motorJoint.linearVelocityImpulse.y );
					hash = b2FnvMixFloat( hash, sim->motorJoint.angularVelocityImpulse );
					hash = b2FnvMixFloat( hash, sim->motorJoint.linearSpringImpulse.x );
					hash = b2FnvMixFloat( hash, sim->motorJoint.linearSpringImpulse.y );
					hash = b2FnvMixFloat( hash, sim->motorJoint.angularSpringImpulse );
					break;
				case b2_pogoJoint:
					hash = b2FnvMixFloat( hash, sim->pogoJoint.impulse );
					hash = b2FnvMixFloat( hash, sim->pogoJoint.velocity );
					break;
				case b2_prismaticJoint:
					hash = b2FnvMixBytes( hash, &sim->prismaticJoint.impulse, sizeof( b2Vec2 ) );
					hash = b2FnvMixFloat( hash, sim->prismaticJoint.springImpulse );
					hash = b2FnvMixFloat( hash, sim->prismaticJoint.motorImpulse );
					hash = b2FnvMixFloat( hash, sim->prismaticJoint.lowerImpulse );
					hash = b2FnvMixFloat( hash, sim->prismaticJoint.upperImpulse );
					break;
				case b2_revoluteJoint:
					hash = b2FnvMixBytes( hash, &sim->revoluteJoint.linearImpulse, sizeof( b2Vec2 ) );
					hash = b2FnvMixFloat( hash, sim->revoluteJoint.springImpulse );
					hash = b2FnvMixFloat( hash, sim->revoluteJoint.motorImpulse );
					hash = b2FnvMixFloat( hash, sim->revoluteJoint.lowerImpulse );
					hash = b2FnvMixFloat( hash, sim->revoluteJoint.upperImpulse );
					break;
				case b2_weldJoint:
					hash = b2FnvMixBytes( hash, &sim->weldJoint.linearImpulse, sizeof( b2Vec2 ) );
					hash = b2FnvMixFloat( hash, sim->weldJoint.angularImpulse );
					break;
				case b2_wheelJoint:
					hash = b2FnvMixFloat( hash, sim->wheelJoint.perpImpulse );
					hash = b2FnvMixFloat( hash, sim->wheelJoint.motorImpulse );
					hash = b2FnvMixFloat( hash, sim->wheelJoint.springImpulse );
					hash = b2FnvMixFloat( hash, sim->wheelJoint.lowerImpulse );
					hash = b2FnvMixFloat( hash, sim->wheelJoint.upperImpulse );
					break;
				default:
					break;
			}
		}
	}

	// 7 id pools: nextIndex + count
	hash = b2FnvMixInt( hash, world->bodyIdPool.nextIndex );
	hash = b2FnvMixInt( hash, b2GetIdCount( &world->bodyIdPool ) );
	hash = b2FnvMixInt( hash, world->shapeIdPool.nextIndex );
	hash = b2FnvMixInt( hash, b2GetIdCount( &world->shapeIdPool ) );
	hash = b2FnvMixInt( hash, world->chainIdPool.nextIndex );
	hash = b2FnvMixInt( hash, b2GetIdCount( &world->chainIdPool ) );
	hash = b2FnvMixInt( hash, world->contactIdPool.nextIndex );
	hash = b2FnvMixInt( hash, b2GetIdCount( &world->contactIdPool ) );
	hash = b2FnvMixInt( hash, world->jointIdPool.nextIndex );
	hash = b2FnvMixInt( hash, b2GetIdCount( &world->jointIdPool ) );
	hash = b2FnvMixInt( hash, world->islandIdPool.nextIndex );
	hash = b2FnvMixInt( hash, b2GetIdCount( &world->islandIdPool ) );
	hash = b2FnvMixInt( hash, world->solverSetIdPool.nextIndex );
	hash = b2FnvMixInt( hash, b2GetIdCount( &world->solverSetIdPool ) );

	// Solver set count
	hash = b2FnvMixInt( hash, world->solverSets.count );

	return hash;
}

uint64_t b2World_GetStateHash( b2WorldId worldId )
{
	b2World* world = b2GetWorldFromId( worldId );
	return b2HashWorldStateDeep( world );
}
