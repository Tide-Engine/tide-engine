// SPDX-FileCopyrightText: 2026 Erin Catto
// SPDX-License-Identifier: MIT

#if defined( _MSC_VER ) && !defined( _CRT_SECURE_NO_WARNINGS )
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "world_snapshot.h"

#include "bitset.h"
#include "body.h"
#include "broad_phase.h"
#include "compound.h"
#include "constraint_graph.h"
#include "contact.h"
#include "container.h"
#include "core.h"
#include "ctz.h"
#include "id_pool.h"
#include "island.h"
#include "joint.h"
#include "physics_world.h"
#include "recording.h"
#include "sensor.h"
#include "shape.h"
#include "solver_set.h"
#include "table.h"

#include "box3d/box3d.h"
#include "box3d/collision.h"
#include "box3d/types.h"

#include <stddef.h>
#include <string.h>

// Snapshot image magic 'BNS3' and version
#define B3_SNAP_MAGIC 0x33534E42u
#define B3_SNAP_VERSION 10u // post solve restitution

#define B3_SNAP_FLAG_VALIDATION 0x1u
#define B3_SNAP_FLAG_DOUBLE_PRECISION 0x2u

// Tide: an image in sections (see ../TIDE.md). After the header there's a table of where each section starts,
// and each is written there, with zeros between one's end and the next one's start. So a section that grows or
// shrinks a little leaves the others where they were, and an image differs from the one before it where the
// world does. Upstream's image is one run of bytes, whose every byte moves when an early part changes size.
#define B3_SNAP_FLAG_SECTIONS 0x4u
enum
{
	b3_sectionConfig,		// World scalars and the id pools
	b3_sectionFixedSets,	// The static and the disabled solver sets
	b3_sectionAwakeSet,
	b3_sectionSleepingSets,
	b3_sectionBodies,
	b3_sectionShapes,
	b3_sectionFatAABBs,
	b3_sectionContacts,
	b3_sectionJoints,
	b3_sectionSensors,
	b3_sectionIslands,
	b3_sectionStaticTree,
	b3_sectionKinematicTree,
	b3_sectionDynamicTree,
	b3_sectionPairs,
	b3_sectionGraph,
	b3_sectionNames,
	b3_sectionCount,
};
_Static_assert( b3_sectionCount == B3_SNAPSHOT_SECTIONS, "the sections of an image, as box3d.h counts them" );
_Static_assert( b3_bodyTypeCount == 3, "a tree a section" );

// Tide: how an image is laid out beyond what upstream's is (see ../TIDE.md), which goes into the layout hash:
// an image of another making is refused, upstream's own included.
#define B3_SNAP_TIDE_FORMAT 1u

// Layout hash over all POD-copied structs + key constants.
// Changing a struct size updates this, catching ABI drift early.
static uint32_t b3ComputeLayoutHash( void )
{
	// Tide: only the structs that go into an image as their bytes, which are laid out the same on
	// 32-bit and 64-bit platforms (see ../TIDE.md). Upstream also mixes in the structs that hold
	// pointers, written field by field, and sizeof( void* ), which keeps an image to one pointer width.
	uint32_t h = 2166136261u;
#define MIX( x )                                                                                                                 \
	h ^= (uint32_t)( x );                                                                                                        \
	h *= 16777619u;
	MIX( sizeof( b3Body ) )
	MIX( sizeof( b3BodySim ) )
	MIX( sizeof( b3BodyState ) )
	MIX( sizeof( b3Shape ) )
	MIX( sizeof( b3Contact ) )
	MIX( sizeof( b3Manifold ) )
	MIX( sizeof( b3Joint ) )
	MIX( sizeof( b3JointSim ) )
	MIX( sizeof( b3IslandSim ) )
	MIX( sizeof( b3ContactLink ) )
	MIX( sizeof( b3JointLink ) )
	MIX( sizeof( b3Visitor ) )
	MIX( sizeof( b3TreeNode ) )
	MIX( sizeof( b3TreeProxy ) )
	MIX( sizeof( b3SetItem ) )
	MIX( sizeof( b3SurfaceMaterial ) )
	MIX( sizeof( b3ContactSpec ) )
	MIX( sizeof( b3TriangleCache ) )
	MIX( sizeof( b3Capacity ) )
	MIX( B3_GRAPH_COLOR_COUNT )
	MIX( b3_bodyTypeCount )
	MIX( B3_SNAP_TIDE_FORMAT )
#undef MIX
	return h;
}

// Tide: what the writer below takes these structs to be, on every platform: where their pointers are, and
// where the bytes are that no field owns. An image has zeros in both (b3SnapZero), as what's there in memory
// is whatever was there before, which is another thing on another machine. If one of these fires, a struct
// changed: bring the offsets in b3SerializeWorld back in line with it.
_Static_assert( sizeof( b3Body ) == 136 && offsetof( b3Body, userDataBits ) == 0 && offsetof( b3Body, generation ) == 128,
				"b3Body changed" );
_Static_assert( sizeof( b3Joint ) == 72 && offsetof( b3Joint, userDataBits ) == 0 && offsetof( b3Joint, collideConnected ) == 66,
				"b3Joint changed" );
_Static_assert( sizeof( b3Filter ) == 24 && offsetof( b3Filter, groupIndex ) == 16, "b3Filter changed" );
_Static_assert( sizeof( b3Shape ) == 208 && offsetof( b3Shape, materialsBits ) == 120 && offsetof( b3Shape, filter ) == 128 &&
					offsetof( b3Shape, userDataBits ) == 152 && offsetof( b3Shape, userShapeBits ) == 160 &&
					offsetof( b3Shape, flags ) == 174 && offsetof( b3Shape, geometryBits ) == 176,
				"b3Shape changed" );
_Static_assert( sizeof( b3Contact ) == 216 && offsetof( b3Contact, manifoldsBits ) == 72 && offsetof( b3Contact, friction ) == 144 &&
					offsetof( b3Contact, convexContact ) == 152 && offsetof( b3Contact, restitution ) == 192,
				"b3Contact changed" );
_Static_assert( sizeof( b3ContactCache ) == 16 && sizeof( b3SimplexCache ) == 16 && offsetof( b3SimplexCache, indexB ) == 10,
				"b3ContactCache changed" );
_Static_assert( sizeof( b3TriangleCache ) == 20 && offsetof( b3TriangleCache, cache ) == 4, "b3TriangleCache changed" );
_Static_assert( sizeof( b3ManifoldPoint ) == 56 && offsetof( b3ManifoldPoint, persisted ) == 52 && sizeof( b3Manifold ) == 268 &&
					B3_MAX_MANIFOLD_POINTS == 4,
				"b3Manifold changed" );
_Static_assert( sizeof( b3BodyState ) == 64 && offsetof( b3BodyState, padding1 ) == 12 && offsetof( b3BodyState, padding2 ) == 28,
				"b3BodyState changed" );
_Static_assert( sizeof( b3JointSim ) == 444 && offsetof( b3JointSim, fixedRotation ) == 180 &&
					offsetof( b3JointSim, distanceJoint ) == 184,
				"b3JointSim changed" );
_Static_assert( sizeof( b3Visitor ) == 8 && offsetof( b3Visitor, generation ) == 4, "b3Visitor changed" );
_Static_assert( sizeof( b3SetItem ) == 16 && offsetof( b3SetItem, hash ) == 8, "b3SetItem changed" );
_Static_assert( sizeof( b3ContactSpec ) == 12 && offsetof( b3ContactSpec, manifoldCount ) == 8, "b3ContactSpec changed" );
_Static_assert( sizeof( b3BodySim ) == 216 && sizeof( b3TreeNode ) == 32 && sizeof( b3TreeProxy ) == 24 &&
					sizeof( b3SurfaceMaterial ) == 40 && sizeof( b3ContactLink ) == 12 && sizeof( b3JointLink ) == 12 &&
					sizeof( b3IslandSim ) == 4 && sizeof( b3Capacity ) == 20 && sizeof( b3AABB ) == 24,
				"a struct an image holds as its bytes changed" );

typedef struct b3SnapHeader
{
	uint32_t magic;
	uint32_t version;
	uint32_t layoutHash;
	uint32_t flags;
} b3SnapHeader;

// Bounds-checked read cursor
typedef struct b3SnapReader
{
	const uint8_t* data;
	int cursor;
	int size;
	bool ok;
	// Tide: an image in sections says where each starts
	bool sectioned;
	int bases[b3_sectionCount];
} b3SnapReader;

static void b3SnapRCheck( b3SnapReader* r, int need )
{
	if ( need < 0 || (int64_t)r->cursor + (int64_t)need > (int64_t)r->size )
	{
		r->ok = false;
	}
}

// Tide: an image as what changed of the one before it (see ../TIDE.md, and b3World_GetSnapshotParts below).
// The writer below is the one that writes an image whole, and what it appends comes here: a copy of the
// bytes in memory of the world's own, or the bytes where they are when they go into an image as they are
// (b3SnapW_Ref). Each is a part that says where in the image it goes. What the writer leaves out is what the
// image before has already.
//
// The copies are in blocks that don't move, so a part can point into one, and they're kept from one image to
// the next: they're as big as what changed, not as an image.
typedef struct b3SnapBlock
{
	uint8_t* data;
	int capacity;
} b3SnapBlock;

typedef struct b3SnapParts
{
	// What the caller gets
	b3SnapshotPart* parts;
	int count;
	int capacity;

	b3SnapBlock* blocks;
	int blockCount;
	int blockCapacity;
	// The block being filled, and how much of it is
	int block;
	int used;

	// The bytes appended last: where in the image, how many, and the copy. What's written over some of
	// them after (b3SnapAt) is written there.
	int lastOffset;
	int lastSize;
	uint8_t* last;

	// The sizes the sections had in the image before
	const int* sizesBefore;
} b3SnapParts;

_Static_assert( B3_SNAP_TRACK_SECTIONS == b3_sectionCount, "the sections of an image, as physics_world.h counts them" );

#define B3_SNAP_BLOCK_SIZE ( 64 * 1024 )

static void b3SnapPartsReset( b3SnapParts* p, const int* sizesBefore )
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
			b3Free( p->blocks[i].data, (size_t)p->blocks[i].capacity );
		}
		total += total / 2;
		p->blocks[0].data = b3Alloc( (size_t)total );
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
static uint8_t* b3SnapPartsRoom( b3SnapParts* p, int size )
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
				b3GrowAlloc( p->blocks, p->blockCapacity * (int)sizeof( b3SnapBlock ), capacity * (int)sizeof( b3SnapBlock ) );
			p->blockCapacity = capacity;
		}

		b3SnapBlock* block = p->blocks + p->blockCount;
		block->capacity = size > B3_SNAP_BLOCK_SIZE ? size : B3_SNAP_BLOCK_SIZE;
		block->data = b3Alloc( (size_t)block->capacity );
		p->blockCount += 1;
		p->used = 0;
	}
	else if ( p->blocks[p->block].capacity < size )
	{
		// A block that was kept and is the last, and too small: a bigger one in its place
		b3SnapBlock* block = p->blocks + p->block;
		b3Free( block->data, (size_t)block->capacity );
		block->capacity = size;
		block->data = b3Alloc( (size_t)size );
	}

	uint8_t* at = p->blocks[p->block].data + p->used;
	p->used += size;
	return at;
}

// A part of the image: `size` bytes that go at `offset`. One that goes on from the last, in the image and in
// memory, is the same part.
static void b3SnapPartsAdd( b3SnapParts* p, int offset, int size, const uint8_t* bytes )
{
	if ( p->count > 0 )
	{
		b3SnapshotPart* last = p->parts + ( p->count - 1 );
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
			b3GrowAlloc( p->parts, p->capacity * (int)sizeof( b3SnapshotPart ), capacity * (int)sizeof( b3SnapshotPart ) );
		p->capacity = capacity;
	}

	p->parts[p->count] = ( b3SnapshotPart ){ offset, size, bytes };
	p->count += 1;
}

uint8_t* b3SnapPartsReserve( b3RecBuffer* buf, int size )
{
	b3SnapParts* p = buf->parts;
	uint8_t* at = b3SnapPartsRoom( p, size );
	b3SnapPartsAdd( p, buf->size, size, at );
	p->lastOffset = buf->size;
	p->lastSize = size;
	p->last = at;
	buf->size += size;
	return at;
}

// Zeros from where the buffer is in the image up to `end`
static void b3SnapPartsZeros( b3RecBuffer* buf, int end )
{
	int size = end - buf->size;
	if ( size <= 0 )
	{
		return;
	}

	b3SnapParts* p = buf->parts;
	uint8_t* at = b3SnapPartsRoom( p, size );
	memset( at, 0, (size_t)size );
	b3SnapPartsAdd( p, buf->size, size, at );
	buf->size += size;
}

// Tide: where the bytes last appended are, to write over some of them: the image itself, or the copy an
// image in parts has of them. `offset` is where in the image, at or past where they started.
static uint8_t* b3SnapAt( b3RecBuffer* buf, int offset )
{
	if ( buf->parts == NULL )
	{
		return buf->data + offset;
	}

	b3SnapParts* p = buf->parts;
	B3_ASSERT( p->lastOffset <= offset && offset < p->lastOffset + p->lastSize );
	return p->last + ( offset - p->lastOffset );
}

// Tide: the next section starts here. Its size goes to the caller who asked, and with a table of bases the
// writing goes on from this one's, over zeros. A section that ran past the next one's base says so, and from
// there on the image is only measured, as one run of bytes: the caller places the sections again.
static void b3SnapSection( b3RecBuffer* buf, int section )
{
	if ( buf->sizes != NULL && buf->section > 0 )
	{
		buf->sizes[buf->section - 1] = buf->size - buf->sectionStart;
	}

	if ( buf->bases != NULL && buf->overran == false && section < b3_sectionCount )
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
				b3SnapParts* p = buf->parts;
				int endBefore = buf->sectionStart + p->sizesBefore[buf->section - 1];
				b3SnapPartsZeros( buf, endBefore < base ? endBefore : base );
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

static void b3SnapSeek( b3SnapReader* r, int section )
{
	if ( r->sectioned && r->ok )
	{
		r->cursor = r->bases[section];
	}
}

static void b3SnapR_Bytes( b3SnapReader* r, void* dst, int n )
{
	b3SnapRCheck( r, n );
	if ( !r->ok )
	{
		return;
	}
	memcpy( dst, r->data + r->cursor, n );
	r->cursor += n;
}

static int b3SnapR_I32( b3SnapReader* r )
{
	int32_t v = 0;
	b3SnapR_Bytes( r, &v, 4 );
	return (int)v;
}

static uint32_t b3SnapR_U32( b3SnapReader* r )
{
	uint32_t v = 0;
	b3SnapR_Bytes( r, &v, 4 );
	return v;
}

static void b3SnapW_I32( b3RecBuffer* buf, int v )
{
	int32_t w = (int32_t)v;
	b3RecBufAppend( buf, &w, 4 );
}

static void b3SnapW_U32( b3RecBuffer* buf, uint32_t v )
{
	b3RecBufAppend( buf, &v, 4 );
}

static void b3SnapW_Bytes( b3RecBuffer* buf, const void* src, int n )
{
	b3RecBufAppend( buf, src, n );
}

// Tide: bytes that go into an image as they are in the world, with nothing written over them after. An image
// in parts has the big ones where they are, with no copy: what reads its parts compares them there, so they
// stay as they are until its parts were read (see b3World_GetSnapshotParts).
#define B3_SNAP_REF_SIZE 512

static void b3SnapW_Ref( b3RecBuffer* buf, const void* src, int n )
{
	if ( buf->parts == NULL || buf->countOnly || n < B3_SNAP_REF_SIZE )
	{
		b3RecBufAppend( buf, src, n );
		return;
	}

	b3SnapPartsAdd( buf->parts, buf->size, n, src );
	buf->size += n;
}

// Tide: for an image in parts, whose writer leaves out what the image before has: the next bytes go at
// `offset` in the image.
static void b3SnapSkipTo( b3RecBuffer* buf, int offset )
{
	B3_ASSERT( buf->parts != NULL );
	buf->size = offset;
}

// Tide: whether an image in parts leaves the section it's at as the image before has it, which it does when
// nothing was done to what's in it: the buffer is then past it.
static bool b3SnapKeepSection( b3RecBuffer* buf, bool touched )
{
	if ( buf->parts == NULL || buf->countOnly || touched )
	{
		return false;
	}

	b3SnapParts* p = buf->parts;
	buf->size += p->sizesBefore[buf->section - 1];
	return true;
}

// Tide: zeros over `size` bytes at `offset` in each of the `count` things of `stride` bytes that were written
// from `start` on: the bytes of a struct that no field owns, and its pointers. Nothing, when they were only
// counted.
static void b3SnapZero( b3RecBuffer* buf, int start, int count, int stride, int offset, int size )
{
	if ( buf->countOnly || count <= 0 )
	{
		return;
	}

	uint8_t* at = b3SnapAt( buf, start ) + offset;
	for ( int i = 0; i < count; ++i )
	{
		memset( at, 0, (size_t)size );
		at += stride;
	}
}

// Tide: the next run of ids under `count` that are in `set`, from `*cursor` on: its first, and one past its
// last. False when there's none left.
static bool b3SnapNextRun( const b3SnapSet* set, int count, int* cursor, int* first, int* end )
{
	int limit = set->blockCount * 64 < count ? set->blockCount * 64 : count;
	int i = *cursor;
	while ( i < limit )
	{
		uint64_t word = set->bits[i >> 6] >> ( i & 63 );
		if ( word != 0 )
		{
			i += (int)b3CTZ64( word );
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

// Bounds check before allocating from image
static bool b3SnapCheckCount( const b3SnapReader* r, int count, int memSize, int minStreamBytes )
{
	if ( count < 0 || memSize < 0 || minStreamBytes < 0 )
	{
		return false;
	}
	if ( memSize > 0 && count > 0x7FFFFFFF / memSize )
	{
		return false;
	}
	int64_t remaining = (int64_t)r->size - (int64_t)r->cursor;
	return (int64_t)count * (int64_t)minStreamBytes <= remaining;
}

// POD array: count + raw bytes
// Tide: which an image in parts has where they are (b3SnapW_Ref)
#define b3SerPodArray( buf, arr )                                                                                                \
	do                                                                                                                           \
	{                                                                                                                            \
		b3SnapW_I32( buf, ( arr ).count );                                                                                       \
		if ( ( arr ).count > 0 )                                                                                                 \
		{                                                                                                                        \
			b3SnapW_Ref( buf, ( arr ).data, ( arr ).count * (int)sizeof( *( arr ).data ) );                                      \
		}                                                                                                                        \
	}                                                                                                                            \
	while ( 0 )

// Tide: a POD array whose elements have `holeSize` bytes at `holeOffset` that no field owns
#define b3SerPodArrayZero( buf, arr, holeOffset, holeSize )                                                                      \
	do                                                                                                                           \
	{                                                                                                                            \
		b3SnapW_I32( buf, ( arr ).count );                                                                                       \
		int podStart = ( buf )->size;                                                                                            \
		if ( ( arr ).count > 0 )                                                                                                 \
		{                                                                                                                        \
			b3SnapW_Bytes( buf, ( arr ).data, ( arr ).count * (int)sizeof( *( arr ).data ) );                                    \
		}                                                                                                                        \
		b3SnapZero( buf, podStart, ( arr ).count, (int)sizeof( *( arr ).data ), holeOffset, holeSize );                          \
	}                                                                                                                            \
	while ( 0 )

#define b3DesPodArray( r, arr )                                                                                                  \
	do                                                                                                                           \
	{                                                                                                                            \
		int cnt = b3SnapR_I32( r );                                                                                              \
		int elemSize = (int)sizeof( *( arr ).data );                                                                             \
		if ( ( r )->ok && b3SnapCheckCount( r, cnt, elemSize, elemSize ) == false )                                              \
		{                                                                                                                        \
			( r )->ok = false;                                                                                                   \
		}                                                                                                                        \
		if ( ( r )->ok && cnt > 0 )                                                                                              \
		{                                                                                                                        \
			b3Array_Resize( arr, cnt );                                                                                          \
			b3SnapR_Bytes( r, ( arr ).data, cnt * elemSize );                                                                    \
		}                                                                                                                        \
		else if ( ( r )->ok )                                                                                                    \
		{                                                                                                                        \
			( arr ).count = 0;                                                                                                   \
		}                                                                                                                        \
	}                                                                                                                            \
	while ( 0 )

// Id pool: nextIndex + freeArray
static void b3SerIdPool( b3RecBuffer* buf, const b3IdPool* pool )
{
	b3SnapW_I32( buf, pool->nextIndex );
	b3SerPodArray( buf, pool->freeArray );
}

static void b3DesIdPool( b3SnapReader* r, b3IdPool* pool )
{
	pool->nextIndex = b3SnapR_I32( r );
	b3DesPodArray( r, pool->freeArray );
}

// BitSet: blockCount + raw words
static void b3SerBitSet( b3RecBuffer* buf, const b3BitSet* bs )
{
	b3SnapW_U32( buf, bs->blockCount );
	if ( bs->blockCount > 0 )
	{
		b3SnapW_Bytes( buf, bs->bits, (int)( bs->blockCount * sizeof( uint64_t ) ) );
	}
}

static void b3DesBitSet( b3SnapReader* r, b3BitSet* bs )
{
	uint32_t blockCount = b3SnapR_U32( r );
	if ( r->ok && b3SnapCheckCount( r, (int)blockCount, (int)sizeof( uint64_t ), (int)sizeof( uint64_t ) ) == false )
	{
		r->ok = false;
	}
	b3DestroyBitSet( bs );
	if ( !r->ok )
	{
		return;
	}
	uint32_t blockCapacity = blockCount > 0 ? blockCount : 1;
	bs->bits = (uint64_t*)b3Alloc( blockCapacity * sizeof( uint64_t ) );
	memset( bs->bits, 0, blockCapacity * sizeof( uint64_t ) );
	bs->blockCapacity = blockCapacity;
	bs->blockCount = blockCount;
	if ( blockCount > 0 )
	{
		b3SnapR_Bytes( r, bs->bits, (int)( blockCount * sizeof( uint64_t ) ) );
	}
}

// HashSet: capacity + count + raw items (probe order depends on layout)
static void b3SerHashSet( b3RecBuffer* buf, const b3HashSet* hs )
{
	b3SnapW_U32( buf, hs->capacity );
	b3SnapW_U32( buf, hs->count );
	if ( hs->capacity > 0 )
	{
		int start = buf->size;
		b3SnapW_Bytes( buf, hs->items, (int)( hs->capacity * sizeof( b3SetItem ) ) );
		// Tide: after an item's hash
		b3SnapZero( buf, start, (int)hs->capacity, (int)sizeof( b3SetItem ), 12, 4 );
	}
}

static void b3DesHashSet( b3SnapReader* r, b3HashSet* hs )
{
	uint32_t cap = b3SnapR_U32( r );
	uint32_t cnt = b3SnapR_U32( r );
	bool valid = b3SnapCheckCount( r, (int)cap, (int)sizeof( b3SetItem ), (int)sizeof( b3SetItem ) ) &&
				 ( cap & ( cap - 1 ) ) == 0 && cnt <= cap;
	if ( r->ok && valid == false && ( cap != 0 || cnt != 0 ) )
	{
		r->ok = false;
	}
	b3DestroySet( hs );
	if ( !r->ok )
	{
		return;
	}
	if ( cap > 0 )
	{
		hs->items = (b3SetItem*)b3Alloc( cap * sizeof( b3SetItem ) );
		hs->capacity = cap;
		hs->count = cnt;
		b3SnapR_Bytes( r, hs->items, (int)( cap * sizeof( b3SetItem ) ) );
	}
	else
	{
		hs->items = NULL;
		hs->capacity = 0;
		hs->count = 0;
	}
}

// DynamicTree: version, scalars, then nodes and parents to nodeEnd and proxies at capacity.
// The empty node and the free pairs are sentinels below the end and travel as they are. Only the
// stale tail above the end, the spare array and the rebuild scratch are skipped.
static void b3SerTree( b3RecBuffer* buf, const b3DynamicTree* tree )
{
	b3SnapW_Bytes( buf, &tree->version, sizeof( uint64_t ) );
	b3SnapW_I32( buf, tree->nodeEnd );
	b3SnapW_I32( buf, tree->pairFreeList );
	b3SnapW_I32( buf, tree->proxyCount );
	b3SnapW_I32( buf, tree->proxyCapacity );
	b3SnapW_I32( buf, tree->proxyFreeList );
	b3SnapW_I32( buf, tree->dfsOrdered ? 1 : 0 );
	// Tide: which an image in parts has where they are (b3SnapW_Ref)
	if ( tree->nodeEnd > 0 )
	{
		b3SnapW_Ref( buf, tree->nodes, tree->nodeEnd * (int)sizeof( b3TreeNode ) );
		b3SnapW_Ref( buf, tree->parents, tree->nodeEnd * (int)sizeof( int32_t ) );
	}
	if ( tree->proxyCapacity > 0 )
	{
		b3SnapW_Ref( buf, tree->proxies, tree->proxyCapacity * (int)sizeof( b3TreeProxy ) );
	}
}

static void b3DesTree( b3SnapReader* r, b3DynamicTree* tree )
{
	uint64_t version;
	b3SnapR_Bytes( r, &version, sizeof( uint64_t ) );
	int nodeEnd = b3SnapR_I32( r );
	int pairFreeList = b3SnapR_I32( r );
	int proxyCount = b3SnapR_I32( r );
	int proxyCapacity = b3SnapR_I32( r );
	int proxyFreeList = b3SnapR_I32( r );
	bool dfsOrdered = b3SnapR_I32( r ) != 0;

	if ( r->ok && b3SnapCheckCount( r, nodeEnd, (int)sizeof( b3TreeNode ), (int)sizeof( b3TreeNode ) ) == false )
	{
		r->ok = false;
	}

	if ( r->ok && b3SnapCheckCount( r, proxyCapacity, (int)sizeof( b3TreeProxy ), (int)sizeof( b3TreeProxy ) ) == false )
	{
		r->ok = false;
	}

	if ( r->ok && ( nodeEnd < 2 || ( nodeEnd & 1 ) != 0 || proxyCapacity < 1 ) )
	{
		r->ok = false;
	}

	// Free existing allocation including the spare array and any rebuild scratch
	b3Free( tree->nodes, tree->nodeCapacity * (int)sizeof( b3TreeNode ) );
	b3Free( tree->parents, tree->nodeCapacity * (int)sizeof( int32_t ) );
	b3Free( tree->proxies, tree->proxyCapacity * (int)sizeof( b3TreeProxy ) );
	b3Free( tree->swapNodes, tree->nodeCapacity * (int)sizeof( b3TreeNode ) );
	b3Free( tree->leafIndices, tree->rebuildCapacity * (int)sizeof( int ) );
	b3Free( tree->leafNodes, tree->rebuildCapacity * (int)sizeof( b3TreeNode ) );
	b3Free( tree->leafBoxes, tree->rebuildCapacity * (int)sizeof( b3AABB ) );
	b3Free( tree->leafCenters, tree->rebuildCapacity * (int)sizeof( b3Vec3 ) );
	b3Free( tree->binIndices, tree->rebuildCapacity * (int)sizeof( int ) );
	tree->nodes = NULL;
	tree->parents = NULL;
	tree->proxies = NULL;
	tree->swapNodes = NULL;
	tree->leafIndices = NULL;
	tree->leafNodes = NULL;
	tree->leafBoxes = NULL;
	tree->leafCenters = NULL;
	tree->binIndices = NULL;
	tree->nodeEnd = 0;
	tree->nodeCapacity = 0;
	tree->proxyCapacity = 0;
	tree->rebuildCapacity = 0;

	if ( !r->ok )
	{
		return;
	}

	tree->version = version;
	tree->nodeEnd = nodeEnd;
	tree->nodeCapacity = nodeEnd;
	tree->pairFreeList = pairFreeList;
	tree->proxyCount = proxyCount;
	tree->proxyCapacity = proxyCapacity;
	tree->proxyFreeList = proxyFreeList;
	tree->dfsOrdered = dfsOrdered;

	tree->nodes = (b3TreeNode*)b3Alloc( nodeEnd * (int)sizeof( b3TreeNode ) );
	tree->parents = (int32_t*)b3Alloc( nodeEnd * (int)sizeof( int32_t ) );
	b3SnapR_Bytes( r, tree->nodes, nodeEnd * (int)sizeof( b3TreeNode ) );
	b3SnapR_Bytes( r, tree->parents, nodeEnd * (int)sizeof( int32_t ) );

	tree->proxies = (b3TreeProxy*)b3Alloc( proxyCapacity * (int)sizeof( b3TreeProxy ) );
	b3SnapR_Bytes( r, tree->proxies, proxyCapacity * (int)sizeof( b3TreeProxy ) );
}

// Tide: body states, whose two padding floats the 4-wide and 8-wide solvers write zeros to and the scalar
// one leaves alone: zeros in an image, whichever solved
static void b3SerBodyStates( b3RecBuffer* buf, const b3SolverSet* set )
{
	b3SnapW_I32( buf, set->bodyStates.count );
	int start = buf->size;
	if ( set->bodyStates.count > 0 )
	{
		b3SnapW_Bytes( buf, set->bodyStates.data, set->bodyStates.count * (int)sizeof( b3BodyState ) );
	}
	b3SnapZero( buf, start, set->bodyStates.count, (int)sizeof( b3BodyState ), 12, 4 );
	b3SnapZero( buf, start, set->bodyStates.count, (int)sizeof( b3BodyState ), 28, 4 );
}

// Solver set: setIndex + 4 arrays (note: contactIndices is int array, not contactSims)
static void b3SerSolverSet( b3RecBuffer* buf, const b3SolverSet* set )
{
	b3SnapW_I32( buf, set->setIndex );
	b3SerPodArray( buf, set->bodySims );
	b3SerBodyStates( buf, set );
	// Tide: after a joint's fixedRotation
	b3SerPodArrayZero( buf, set->jointSims, 181, 3 );
	b3SerPodArray( buf, set->contactIndices );
	b3SerPodArray( buf, set->islandSims );
}

static void b3DesSolverSet( b3SnapReader* r, b3SolverSet* set )
{
	set->setIndex = b3SnapR_I32( r );
	b3DesPodArray( r, set->bodySims );
	b3DesPodArray( r, set->bodyStates );
	b3DesPodArray( r, set->jointSims );
	b3DesPodArray( r, set->contactIndices );
	b3DesPodArray( r, set->islandSims );
}

static void b3SerNames( b3RecBuffer* buf, const b3NameCache* cache )
{
	b3SnapW_I32( buf, cache->entries.count );
	int count = cache->entries.count;
	for ( int i = 0; i < count; ++i )
	{
		const b3NameEntry* entry = cache->entries.data + i;
		b3SnapW_U32( buf, entry->hash );
		b3SnapW_I32( buf, entry->length );
		b3RecBufAppend( buf, entry->name, entry->length );
	}
}

static void b3DesNames( b3SnapReader* r, b3NameCache* cache )
{
	int count = b3SnapR_I32( r );

	if ( r->ok && b3SnapCheckCount( r, count, (int)sizeof( b3NameEntry ), 8 ) == false )
	{
		r->ok = false;
	}

	if ( r->ok == false )
	{
		return;
	}

	// Tide: the image's names and no others, in its order: a world that had names of its own would write
	// them into its next image, which a world made from nothing wouldn't
	b3DestroyNameCache( cache );
	*cache = b3CreateNameCache();

	b3Array_Reserve( cache->entries, count );
	for ( int i = 0; i < count; ++i )
	{
		uint32_t hash = b3SnapR_U32( r );
		int length = b3SnapR_I32( r );
		if ( r->ok == false || length < 0 || length > r->size - r->cursor )
		{
			r->ok = false;
			return;
		}
		char* name = b3Alloc( length + 1 );
		b3SnapR_Bytes( r, name, length );
		name[length] = 0;
		b3LoadName( cache, hash, name, length );
	}
}

// Graph color: bodySet (non-overflow only) + jointSims + convexContacts + contacts
static void b3SerGraphColor( b3RecBuffer* buf, const b3GraphColor* color, bool isOverflow )
{
	if ( !isOverflow )
	{
		b3SerBitSet( buf, &color->bodySet );
	}
	// Tide: after a joint's fixedRotation, and after a contact spec's manifoldCount
	b3SerPodArrayZero( buf, color->jointSims, 181, 3 );
	b3SerPodArray( buf, color->convexContacts );
	b3SerPodArrayZero( buf, color->contacts, 10, 2 );
	// wideConstraints / manifoldConstraints / contactConstraints are transient, not serialized
}

static void b3DesGraphColor( b3SnapReader* r, b3GraphColor* color, bool isOverflow )
{
	if ( !isOverflow )
	{
		b3DesBitSet( r, &color->bodySet );
	}
	b3DesPodArray( r, color->jointSims );
	b3DesPodArray( r, color->convexContacts );
	b3DesPodArray( r, color->contacts );
	// Transient pointers left at NULL/0 from shell
}

// World simulation scalars (never host/callback/worker state)
static void b3SerWorldConfig( b3RecBuffer* buf, const b3World* world )
{
	b3SnapW_Bytes( buf, &world->gravity, sizeof( b3Vec3 ) );
	b3SnapW_Bytes( buf, &world->hitEventThreshold, sizeof( float ) );
	b3SnapW_Bytes( buf, &world->restitutionThreshold, sizeof( float ) );
	b3SnapW_I32( buf, world->restitutionIterations );
	b3SnapW_Bytes( buf, &world->maxLinearSpeed, sizeof( float ) );
	b3SnapW_Bytes( buf, &world->contactSpeed, sizeof( float ) );
	b3SnapW_Bytes( buf, &world->contactHertz, sizeof( float ) );
	b3SnapW_Bytes( buf, &world->contactDampingRatio, sizeof( float ) );
	b3SnapW_Bytes( buf, &world->contactRecycleDistance, sizeof( float ) );
	b3SnapW_Bytes( buf, &world->stepIndex, sizeof( uint64_t ) );
	b3SnapW_I32( buf, world->splitIslandId );
	b3SnapW_Bytes( buf, &world->inv_h, sizeof( float ) );
	b3SnapW_Bytes( buf, &world->inv_dt, sizeof( float ) );
	b3SnapW_I32( buf, world->endEventArrayIndex );
	b3SnapW_I32( buf, world->compoundShapeCount );
	b3SnapW_Bytes( buf, &world->maxCapacity, sizeof( b3Capacity ) );
	uint8_t flags = 0;
	flags |= world->enableSleep ? 0x01u : 0u;
	flags |= world->enableWarmStarting ? 0x02u : 0u;
	flags |= world->enableContinuous ? 0x04u : 0u;
	flags |= world->enableSpeculative ? 0x08u : 0u;
	flags |= world->enableRestitutionPropagation ? 0x10u : 0u;
	b3RecBufAppend( buf, &flags, 1 );
}

static void b3DesWorldConfig( b3SnapReader* r, b3World* world )
{
	b3SnapR_Bytes( r, &world->gravity, sizeof( b3Vec3 ) );
	b3SnapR_Bytes( r, &world->hitEventThreshold, sizeof( float ) );
	b3SnapR_Bytes( r, &world->restitutionThreshold, sizeof( float ) );
	world->restitutionIterations = b3SnapR_I32( r );
	b3SnapR_Bytes( r, &world->maxLinearSpeed, sizeof( float ) );
	b3SnapR_Bytes( r, &world->contactSpeed, sizeof( float ) );
	b3SnapR_Bytes( r, &world->contactHertz, sizeof( float ) );
	b3SnapR_Bytes( r, &world->contactDampingRatio, sizeof( float ) );
	b3SnapR_Bytes( r, &world->contactRecycleDistance, sizeof( float ) );
	b3SnapR_Bytes( r, &world->stepIndex, sizeof( uint64_t ) );
	world->splitIslandId = b3SnapR_I32( r );
	b3SnapR_Bytes( r, &world->inv_h, sizeof( float ) );
	b3SnapR_Bytes( r, &world->inv_dt, sizeof( float ) );
	world->endEventArrayIndex = b3SnapR_I32( r );
	world->compoundShapeCount = b3SnapR_I32( r );
	b3SnapR_Bytes( r, &world->maxCapacity, sizeof( b3Capacity ) );
	uint8_t flags = 0;
	b3SnapR_Bytes( r, &flags, 1 );
	world->enableSleep = ( flags & 0x01u ) != 0;
	world->enableWarmStarting = ( flags & 0x02u ) != 0;
	world->enableContinuous = ( flags & 0x04u ) != 0;
	world->enableSpeculative = ( flags & 0x08u ) != 0;
	world->enableRestitutionPropagation = ( flags & 0x10u ) != 0;
}

// Tide: the geometry a world's images name (see ../TIDE.md). An image says which hull, mesh or height field a
// shape has by a number, and this is what the numbers are: each geometry once, in the order images first
// named them, which is the same on every machine as images are taken of the same worlds. It's only ever
// added to, so what's written of it (b3World_GetSnapshotGeometry) only ever grows at its end.
//
// Upstream keeps this in a recording, beside the images. Here it's the world's own, as an image goes into a
// match and the geometry has to go there with it: a machine that has only the match makes the world from both.
typedef struct b3SnapGeometryEntry
{
	// The geometry's own hash of itself, which finds it
	uint64_t hash;
	// b3GeometryKind
	int kind;
	int byteCount;
	// The world's copy
	uint8_t* bytes;
	// Where a shape last had it: the same again isn't compared again
	const void* source;
	// The next entry in its bucket, or B3_NULL_INDEX
	int next;
} b3SnapGeometryEntry;

typedef struct b3SnapGeometry
{
	b3SnapGeometryEntry* entries;
	int count;
	int capacity;
	// The first entry of each hash, by its low bits
	int* buckets;
	int bucketCount;
	// What b3World_GetSnapshotGeometry writes
	int byteCount;
	// The last one asked for of each kind, which the next shape often has too
	const void* lastData[3];
	int lastId[3];
} b3SnapGeometry;

// Hulls, meshes and height fields start alike: a version, their hash, and further on their size. A compound's
// bytes hold a tree with its pointers, which is another layout where pointers are another width, so an image
// can't name one.
_Static_assert( b3_geometryHull == 0 && b3_geometryMesh == 1 && b3_geometryHeightField == 2, "the kinds an image names" );
_Static_assert( offsetof( b3HullData, hash ) == 8 && offsetof( b3MeshData, hash ) == 8 && offsetof( b3HeightFieldData, hash ) == 8,
				"geometry starts with its version and its hash" );

// As it's written: its kind, its size, its bytes, and zeros to the next multiple of 8
static int b3SnapGeometryRecordSize( int byteCount )
{
	return 8 + ( ( byteCount + 7 ) & ~7 );
}

static b3SnapGeometry* b3GetSnapGeometry( b3World* world )
{
	if ( world->snapGeometry == NULL )
	{
		b3SnapGeometry* geometry = b3Alloc( sizeof( b3SnapGeometry ) );
		memset( geometry, 0, sizeof( b3SnapGeometry ) );
		world->snapGeometry = geometry;
	}
	return world->snapGeometry;
}

static void b3SnapGeometryRehash( b3SnapGeometry* geometry, int bucketCount )
{
	b3Free( geometry->buckets, (size_t)geometry->bucketCount * sizeof( int ) );
	geometry->buckets = b3Alloc( (size_t)bucketCount * sizeof( int ) );
	geometry->bucketCount = bucketCount;
	for ( int i = 0; i < bucketCount; ++i )
	{
		geometry->buckets[i] = B3_NULL_INDEX;
	}
	for ( int i = 0; i < geometry->count; ++i )
	{
		b3SnapGeometryEntry* entry = geometry->entries + i;
		int bucket = (int)( entry->hash & (uint64_t)( bucketCount - 1 ) );
		entry->next = geometry->buckets[bucket];
		geometry->buckets[bucket] = i;
	}
}

// A copy of the bytes, as the next entry
static int b3SnapGeometryAdd( b3SnapGeometry* geometry, int kind, const void* data, int byteCount, const void* source )
{
	if ( geometry->count == geometry->capacity )
	{
		int capacity = geometry->capacity == 0 ? 8 : 2 * geometry->capacity;
		geometry->entries = b3GrowAlloc( geometry->entries, geometry->capacity * (int)sizeof( b3SnapGeometryEntry ),
										 capacity * (int)sizeof( b3SnapGeometryEntry ) );
		geometry->capacity = capacity;
	}

	int id = geometry->count;
	b3SnapGeometryEntry* entry = geometry->entries + id;
	entry->kind = kind;
	entry->byteCount = byteCount;
	entry->bytes = b3Alloc( (size_t)byteCount );
	memcpy( entry->bytes, data, (size_t)byteCount );
	memcpy( &entry->hash, entry->bytes + 8, sizeof( uint64_t ) );
	entry->source = source;
	entry->next = B3_NULL_INDEX;
	geometry->count += 1;
	geometry->byteCount += b3SnapGeometryRecordSize( byteCount );

	if ( 2 * geometry->count > geometry->bucketCount )
	{
		b3SnapGeometryRehash( geometry, geometry->bucketCount == 0 ? 16 : 2 * geometry->bucketCount );
	}
	else
	{
		int bucket = (int)( entry->hash & (uint64_t)( geometry->bucketCount - 1 ) );
		entry->next = geometry->buckets[bucket];
		geometry->buckets[bucket] = id;
	}
	return id;
}

// Only the first `count` entries stay
static void b3SnapGeometryTruncate( b3SnapGeometry* geometry, int count )
{
	if ( count >= geometry->count )
	{
		return;
	}

	for ( int i = count; i < geometry->count; ++i )
	{
		b3SnapGeometryEntry* entry = geometry->entries + i;
		geometry->byteCount -= b3SnapGeometryRecordSize( entry->byteCount );
		b3Free( entry->bytes, (size_t)entry->byteCount );
	}
	geometry->count = count;
	if ( geometry->bucketCount > 0 )
	{
		b3SnapGeometryRehash( geometry, geometry->bucketCount );
	}
}

// Which entry a shape's geometry is: the one with its bytes, or a new one
static uint32_t b3SnapGeometryId( b3World* world, int kind, const void* data, int byteCount )
{
	b3SnapGeometry* geometry = b3GetSnapGeometry( world );
	if ( geometry->lastData[kind] == data )
	{
		return (uint32_t)geometry->lastId[kind];
	}

	uint64_t hash;
	memcpy( &hash, (const uint8_t*)data + 8, sizeof( uint64_t ) );

	int id = B3_NULL_INDEX;
	if ( geometry->bucketCount > 0 )
	{
		int index = geometry->buckets[hash & (uint64_t)( geometry->bucketCount - 1 )];
		for ( ; index != B3_NULL_INDEX; index = geometry->entries[index].next )
		{
			b3SnapGeometryEntry* entry = geometry->entries + index;
			if ( entry->hash != hash || entry->kind != kind || entry->byteCount != byteCount )
			{
				continue;
			}

			// The hash is how it's found, and the bytes are how it's known, compared once for each place a
			// shape has them: a mesh is megabytes, and an image is taken every step
			if ( entry->source == data || memcmp( entry->bytes, data, (size_t)byteCount ) == 0 )
			{
				entry->source = data;
				id = index;
				break;
			}
		}
	}

	if ( id == B3_NULL_INDEX )
	{
		id = b3SnapGeometryAdd( geometry, kind, data, byteCount, data );
	}

	geometry->lastData[kind] = data;
	geometry->lastId[kind] = id;
	return (uint32_t)id;
}

void b3DestroySnapshotGeometry( b3World* world )
{
	b3SnapGeometry* geometry = world->snapGeometry;
	if ( geometry == NULL )
	{
		return;
	}

	b3SnapGeometryTruncate( geometry, 0 );
	b3Free( geometry->entries, (size_t)geometry->capacity * sizeof( b3SnapGeometryEntry ) );
	b3Free( geometry->buckets, (size_t)geometry->bucketCount * sizeof( int ) );
	b3Free( geometry, sizeof( b3SnapGeometry ) );
	world->snapGeometry = NULL;
}

// Whether these bytes are geometry as b3World_GetSnapshotGeometry writes it, whole
static bool b3SnapGeometryIsValid( const uint8_t* bytes, int size )
{
	if ( size < 0 || ( size > 0 && bytes == NULL ) )
	{
		return false;
	}

	int cursor = 0;
	while ( cursor < size )
	{
		if ( size - cursor < 8 )
		{
			return false;
		}

		uint32_t kind, byteCount;
		memcpy( &kind, bytes + cursor, 4 );
		memcpy( &byteCount, bytes + cursor + 4, 4 );
		if ( byteCount > (uint32_t)( size - cursor - 8 ) || b3SnapGeometryRecordSize( (int)byteCount ) > size - cursor )
		{
			return false;
		}

		// Each kind says its version first, and its own size further on
		const uint8_t* data = bytes + cursor + 8;
		uint64_t version = 0;
		int32_t ownByteCount = -1;
		if ( kind == b3_geometryHull && byteCount >= sizeof( b3HullData ) )
		{
			version = B3_HULL_VERSION;
			memcpy( &ownByteCount, data + offsetof( b3HullData, byteCount ), 4 );
		}
		else if ( kind == b3_geometryMesh && byteCount >= sizeof( b3MeshData ) )
		{
			version = B3_MESH_VERSION;
			memcpy( &ownByteCount, data + offsetof( b3MeshData, byteCount ), 4 );
		}
		else if ( kind == b3_geometryHeightField && byteCount >= sizeof( b3HeightFieldData ) )
		{
			version = B3_HEIGHT_FIELD_VERSION;
			memcpy( &ownByteCount, data + offsetof( b3HeightFieldData, byteCount ), 4 );
		}
		else
		{
			return false;
		}

		if ( memcmp( data, &version, 8 ) != 0 || ownByteCount != (int32_t)byteCount )
		{
			return false;
		}

		cursor += b3SnapGeometryRecordSize( (int)byteCount );
	}
	return true;
}

// The world's geometry becomes what these bytes say, which were checked. What it has of it already stays
// where it is: its entries are the same up to where the two part, as both only ever grow.
static void b3SnapGeometryLoad( b3World* world, const uint8_t* bytes, int size )
{
	b3SnapGeometry* geometry = b3GetSnapGeometry( world );
	memset( geometry->lastData, 0, sizeof( geometry->lastData ) );

	int cursor = 0;
	int index = 0;
	while ( cursor < size )
	{
		uint32_t kind, byteCount;
		memcpy( &kind, bytes + cursor, 4 );
		memcpy( &byteCount, bytes + cursor + 4, 4 );
		const uint8_t* data = bytes + cursor + 8;

		if ( index < geometry->count )
		{
			b3SnapGeometryEntry* entry = geometry->entries + index;
			if ( entry->kind != (int)kind || entry->byteCount != (int)byteCount || memcmp( entry->bytes, data, byteCount ) != 0 )
			{
				b3SnapGeometryTruncate( geometry, index );
			}
			else
			{
				// The shapes that had it are gone
				entry->source = NULL;
			}
		}

		if ( index == geometry->count )
		{
			b3SnapGeometryAdd( geometry, (int)kind, data, (int)byteCount, NULL );
		}

		index += 1;
		cursor += b3SnapGeometryRecordSize( (int)byteCount );
	}
	b3SnapGeometryTruncate( geometry, index );
}

// Tide: a shape's geometry as an image has it, in the 32 bytes the shape's own takes. A sphere and a capsule
// are themselves, and what a shape has a pointer to is its number among the world's geometry, with a mesh's
// scale after it. The rest is zeros.
_Static_assert( sizeof( b3Capsule ) == 28 && sizeof( b3Sphere ) == 16, "a shape's geometry in an image" );
#define B3_SNAP_GEOMETRY_SIZE 32

// Tide: a shape with a material array of its own has this where its pointer was, and its materials after
// the shapes
#define B3_SNAP_OWNS_MATERIALS 1u

// Shapes carry pointer fields: materials, userData, userShape, and the geometry union.
// Tide: all the shapes as their bytes, with zeros for the pointers and for what no field owns, and the
// geometry as above. Then the material arrays of the shapes that own one, in the shapes' order. So a shape
// keeps its place whatever the others are, where upstream writes each shape's materials and geometry right
// after it, and they differ in size.
// Hull/mesh/heightField/compound are interned into the recording registry; sphere/capsule inline.
// Tide: or, with no recording, into the world's own.
// Tide: the shapes from `first` up to `end`, which the buffer is at: `start` is where the first shape of all
// goes. An image written whole has them all in one run, and an image in parts a run for each stretch of
// shapes that may have changed (b3SnapNextRun), so both have the same bytes for a shape.
static void b3SerShapeRun( b3RecBuffer* buf, b3World* world, b3Recording* rec, int start, int first, int end )
{
	const int stride = (int)sizeof( b3Shape );
	int count = end - first;
	int runStart = start + first * stride;
	b3SnapW_Bytes( buf, world->shapes.data + first, count * stride );
	b3SnapZero( buf, runStart, count, stride, (int)offsetof( b3Shape, filter ) + 20, 4 );
	b3SnapZero( buf, runStart, count, stride, (int)offsetof( b3Shape, userDataBits ), 8 );
	b3SnapZero( buf, runStart, count, stride, (int)offsetof( b3Shape, userShapeBits ), 8 );
	b3SnapZero( buf, runStart, count, stride, (int)offsetof( b3Shape, flags ) + 1, 1 );

	for ( int i = first; i < end; ++i )
	{
		const b3Shape* src = world->shapes.data + i;
		bool isLive = ( src->id == i );

		uint64_t owns = 0;
		uint8_t geo[B3_SNAP_GEOMETRY_SIZE] = { 0 };

		// A free slot has neither: zeros, so free-slot images have deterministic bytes
		if ( isLive )
		{
			// Only multi material meshes and compounds have an array. A single material already rode along
			// inline in the struct image.
			owns = src->materials != NULL ? B3_SNAP_OWNS_MATERIALS : 0;

			uint32_t gid = 0;
			switch ( src->type )
			{
				case b3_sphereShape:
					memcpy( geo, &src->sphere, sizeof( b3Sphere ) );
					break;
				case b3_capsuleShape:
					memcpy( geo, &src->capsule, sizeof( b3Capsule ) );
					break;
				case b3_hullShape:
					gid = rec != NULL ? b3RecInternHull( rec, src->hull )
									  : b3SnapGeometryId( world, b3_geometryHull, src->hull, src->hull->byteCount );
					memcpy( geo, &gid, 4 );
					break;
				case b3_meshShape:
					gid = rec != NULL ? b3RecInternMesh( rec, src->mesh.data )
									  : b3SnapGeometryId( world, b3_geometryMesh, src->mesh.data, src->mesh.data->byteCount );
					memcpy( geo, &gid, 4 );
					memcpy( geo + 4, &src->mesh.scale, sizeof( b3Vec3 ) );
					break;
				case b3_heightShape:
					gid = rec != NULL ? b3RecInternHeightField( rec, src->heightField )
									  : b3SnapGeometryId( world, b3_geometryHeightField, src->heightField,
														  src->heightField->byteCount );
					memcpy( geo, &gid, 4 );
					break;
				case b3_compoundShape:
					if ( rec != NULL )
					{
						gid = b3RecInternCompound( rec, src->compound );
					}
					else
					{
						// Tide: a compound's bytes aren't the same on every machine (see b3SnapGeometry)
						buf->failed = true;
					}
					memcpy( geo, &gid, 4 );
					break;
				default:
					// A live shape must have a known geometry type. Fail loudly rather than emit a shape
					// with no geometry that would silently lose its collision on restore.
					B3_ASSERT( false );
					buf->failed = true;
					break;
			}
		}

		if ( buf->countOnly == false )
		{
			uint8_t* at = b3SnapAt( buf, runStart ) + ( i - first ) * stride;
			memcpy( at + offsetof( b3Shape, materialsBits ), &owns, 8 );
			memcpy( at + offsetof( b3Shape, geometryBits ), geo, B3_SNAP_GEOMETRY_SIZE );
		}
	}
}

static void b3SerShapes( b3RecBuffer* buf, b3World* world, b3Recording* rec )
{
	int count = world->shapes.count;
	b3SnapW_I32( buf, count );

	int start = buf->size;
	const int stride = (int)sizeof( b3Shape );

	if ( world->snapGeometry != NULL )
	{
		b3SnapGeometry* geometry = world->snapGeometry;
		memset( geometry->lastData, 0, sizeof( geometry->lastData ) );
	}

	if ( buf->parts != NULL )
	{
		// Tide: an image in parts has the shapes that may have changed, each at its place. A shape of new
		// geometry is one of them, so the world's geometry gets it in the order a whole image gives it.
		int cursor = 0, first, end;
		while ( b3SnapNextRun( &world->snapTrack.shapes, count, &cursor, &first, &end ) )
		{
			b3SnapSkipTo( buf, start + first * stride );
			b3SerShapeRun( buf, world, rec, start, first, end );
		}
		b3SnapSkipTo( buf, start + count * stride );
	}
	else if ( count > 0 )
	{
		b3SerShapeRun( buf, world, rec, start, 0, count );
	}

	// Tide: an image in parts of a world that had no shape with an array of its own looks for none
	if ( buf->parts != NULL && world->snapTrack.oddShapeCount == 0 )
	{
		return;
	}

	for ( int i = 0; i < count; ++i )
	{
		const b3Shape* src = world->shapes.data + i;
		if ( src->id == i && src->materials != NULL )
		{
			b3SnapW_Bytes( buf, src->materials, src->materialCount * (int)sizeof( b3SurfaceMaterial ) );
		}
	}
}

// Tide: the bytes of the geometry an image names by `id`, of the kind a shape of that type has: the
// recording's, or the world's own. NULL, and the read fails, when there's no such geometry.
static const uint8_t* b3SnapGeometryBytes( b3SnapReader* r, b3World* world, b3RecReader* rdr, uint32_t id, int kind )
{
	if ( !r->ok )
	{
		return NULL;
	}

	if ( rdr != NULL )
	{
		if ( id >= (uint32_t)rdr->slotCount )
		{
			r->ok = false;
			return NULL;
		}
		return rdr->slots[id].bytes;
	}

	const b3SnapGeometry* geometry = world->snapGeometry;
	if ( geometry == NULL || id >= (uint32_t)geometry->count || geometry->entries[id].kind != kind )
	{
		r->ok = false;
		return NULL;
	}
	return geometry->entries[id].bytes;
}

static void b3DesShapes( b3SnapReader* r, b3World* world, b3RecReader* rdr )
{
	int count = b3SnapR_I32( r );
	if ( r->ok && b3SnapCheckCount( r, count, (int)sizeof( b3Shape ), (int)sizeof( b3Shape ) ) == false )
	{
		r->ok = false;
	}
	if ( !r->ok )
	{
		return;
	}

	// Save renderer handles before the array is wiped. A keyframe restore is a deterministic replay
	// state, so a live shape that still occupies the same slot with the same generation is the same
	// shape with the same geometry. Carrying its handle over avoids tearing down and rebuilding every
	// GPU mesh on each seek, which the host (a 3D renderer) would otherwise pay for. Handles that are
	// not reclaimed below belong to shapes that are gone or were replaced, and get released so the host
	// pool does not leak across seeks.
	int oldShapeCount = world->shapes.count;
	void** savedUserShape = NULL;
	uint16_t* savedGeneration = NULL;
	if ( oldShapeCount > 0 )
	{
		savedUserShape = (void**)b3Alloc( (size_t)oldShapeCount * sizeof( void* ) );
		savedGeneration = (uint16_t*)b3Alloc( (size_t)oldShapeCount * sizeof( uint16_t ) );
		for ( int i = 0; i < oldShapeCount; ++i )
		{
			b3Shape* old = world->shapes.data + i;
			bool oldLive = ( old->id == i );
			savedUserShape[i] = oldLive ? old->userShape : NULL;
			savedGeneration[i] = old->generation;
		}
	}

	b3Array_Resize( world->shapes, count );
	memset( world->shapes.data, 0, (size_t)count * sizeof( b3Shape ) );

	// Tide: all the shapes, then each one's geometry from where it is in its bytes
	if ( count > 0 )
	{
		b3SnapR_Bytes( r, world->shapes.data, count * (int)sizeof( b3Shape ) );
	}

	for ( int i = 0; i < count && r->ok; ++i )
	{
		b3Shape* dst = world->shapes.data + i;

		uint8_t geo[B3_SNAP_GEOMETRY_SIZE];
		memcpy( geo, dst->geometryBits, B3_SNAP_GEOMETRY_SIZE );
		uint32_t gid;
		memcpy( &gid, geo, 4 );

		// Pointer fields were written as zeros, or as a mark; set them cleanly
		bool ownsMaterials = dst->materialsBits == B3_SNAP_OWNS_MATERIALS;
		dst->materialsBits = 0;
		dst->materials = NULL;
		dst->userDataBits = 0;
		dst->userData = NULL;
		dst->userShapeBits = 0;
		dst->userShape = NULL;
		memset( dst->geometryBits, 0, sizeof( dst->geometryBits ) );

		bool isLive = ( dst->id == i );

		// Carry the renderer handle over when the same shape still occupies this slot. Consumed
		// handles are nulled so the teardown sweep only releases the ones that vanished.
		if ( isLive && i < oldShapeCount && savedUserShape != NULL && savedUserShape[i] != NULL &&
			 savedGeneration[i] == dst->generation )
		{
			dst->userShape = savedUserShape[i];
			savedUserShape[i] = NULL;
		}

		if ( !isLive )
		{
			// Free slot: no materials or geometry
			continue;
		}

		// The owned material array comes after the shapes; until then the count says how many
		if ( ownsMaterials )
		{
			if ( dst->materialCount <= 0 || b3SnapCheckCount( r, dst->materialCount, (int)sizeof( b3SurfaceMaterial ),
															  (int)sizeof( b3SurfaceMaterial ) ) == false )
			{
				r->ok = false;
				break;
			}
			dst->materials = (b3SurfaceMaterial*)b3Alloc( (size_t)dst->materialCount * sizeof( b3SurfaceMaterial ) );
			memset( dst->materials, 0, (size_t)dst->materialCount * sizeof( b3SurfaceMaterial ) );
		}

		// Geometry
		switch ( dst->type )
		{
			case b3_sphereShape:
				memcpy( &dst->sphere, geo, sizeof( b3Sphere ) );
				break;
			case b3_capsuleShape:
				memcpy( &dst->capsule, geo, sizeof( b3Capsule ) );
				break;
			case b3_hullShape:
			{
				const uint8_t* bytes = b3SnapGeometryBytes( r, world, rdr, gid, b3_geometryHull );
				if ( bytes == NULL )
				{
					break;
				}
				// Hull is cloned into the world DB; pass raw bytes directly
				dst->hull = b3AddHullToDatabase( world, (const b3HullData*)bytes );
				break;
			}
			case b3_meshShape:
			{
				const uint8_t* bytes = b3SnapGeometryBytes( r, world, rdr, gid, b3_geometryMesh );
				if ( bytes == NULL )
				{
					break;
				}
				// Mesh is a self-contained blob used by reference; point straight at the pristine bytes.
				dst->mesh.data = (const b3MeshData*)bytes;
				memcpy( &dst->mesh.scale, geo + 4, sizeof( b3Vec3 ) );
				break;
			}
			case b3_heightShape:
			{
				const uint8_t* bytes = b3SnapGeometryBytes( r, world, rdr, gid, b3_geometryHeightField );
				if ( bytes == NULL )
				{
					break;
				}
				// Self-contained blob used by reference; point straight at the pristine bytes.
				dst->heightField = (const b3HeightFieldData*)bytes;
				break;
			}
			case b3_compoundShape:
			{
				// Tide: only a recording has compounds (see b3SnapGeometry)
				if ( rdr == NULL || gid >= (uint32_t)rdr->slotCount )
				{
					r->ok = false;
					break;
				}
				b3RegistrySlot* slot = rdr->slots + gid;
				if ( slot->live == NULL )
				{
					slot->live = b3Alloc( (size_t)slot->byteCount );
					memcpy( slot->live, slot->bytes, (size_t)slot->byteCount );
					b3ConvertBytesToCompound( (uint8_t*)slot->live, slot->byteCount );
				}
				dst->compound = (const b3CompoundData*)slot->live;
				break;
			}
			default:
				// Unknown geometry kind means a corrupt or unsupported snapshot. Fail the load instead
				// of leaving a shape with no geometry.
				r->ok = false;
				break;
		}
	}

	// Tide: the material arrays of the shapes that own one
	for ( int i = 0; i < count && r->ok; ++i )
	{
		b3Shape* dst = world->shapes.data + i;
		if ( dst->id == i && dst->materials != NULL )
		{
			b3SnapR_Bytes( r, dst->materials, dst->materialCount * (int)sizeof( b3SurfaceMaterial ) );
		}
	}

	// Release handles for shapes that are gone or were replaced this restore, so the host pool and any
	// GPU resources they pinned do not leak across seeks.
	if ( savedUserShape != NULL )
	{
		for ( int i = 0; i < oldShapeCount; ++i )
		{
			if ( savedUserShape[i] != NULL && world->destroyDebugShape != NULL )
			{
				world->destroyDebugShape( savedUserShape[i], world->userDebugShapeContext );
			}
		}
		b3Free( savedUserShape, (size_t)oldShapeCount * sizeof( void* ) );
		b3Free( savedGeneration, (size_t)oldShapeCount * sizeof( uint16_t ) );
	}
}

// Tide: a manifold as an image has it: zeros after each point's `persisted`
static void b3SerManifolds( b3RecBuffer* buf, const b3Manifold* manifolds, int count )
{
	int start = buf->size;
	b3SnapW_Bytes( buf, manifolds, count * (int)sizeof( b3Manifold ) );
	for ( int point = 0; point < B3_MAX_MANIFOLD_POINTS; ++point )
	{
		b3SnapZero( buf, start, count, (int)sizeof( b3Manifold ), point * (int)sizeof( b3ManifoldPoint ) + 53, 3 );
	}
}

// Contact serialization. b3Contact is not fully POD:
// - manifolds: heap array of b3Manifold, allocated via b3AllocateManifolds
// - meshContact.triangleCache: heap b3Array, active when b3_simMeshContact flag is set
// Tide: all the contacts as their bytes, with zeros for their pointers and for what no field owns. Then a
// manifold for each: its first one, or zeros when it has none, so that a contact that starts or stops
// touching leaves every other where it was. Then what only mesh contacts have, in the contacts' order: their
// other manifolds, and their triangle caches. Upstream writes each contact's manifolds right after it.
// Tide: the contacts from `first` up to `end` as their bytes, which the buffer is at: `start` is where the
// first contact of all goes. As b3SerShapeRun, for an image written whole and for one in parts alike.
static void b3SerContactRun( b3RecBuffer* buf, b3World* world, int start, int first, int end )
{
	const int stride = (int)sizeof( b3Contact );
	int count = end - first;
	int runStart = start + first * stride;
	b3SnapW_Bytes( buf, world->contacts.data + first, count * stride );
	b3SnapZero( buf, runStart, count, stride, (int)offsetof( b3Contact, manifoldsBits ), 8 );
	b3SnapZero( buf, runStart, count, stride, (int)offsetof( b3Contact, friction ) + 4, 4 );

	const int cache = (int)offsetof( b3Contact, convexContact );
	const int cacheEnd = (int)offsetof( b3Contact, restitution );
	if ( buf->countOnly == false )
	{
		for ( int i = first; i < end; ++i )
		{
			uint8_t* at = b3SnapAt( buf, runStart ) + ( i - first ) * stride;
			if ( world->contacts.data[i].flags & b3_simMeshContact )
			{
				// The triangle cache's array
				memset( at + cache, 0, 16 );
			}
			else
			{
				// After a simplex cache's indices, and past the cache: what's there is a mesh contact's
				memset( at + cache + 14, 0, (size_t)( cacheEnd - cache - 14 ) );
			}
		}
	}
}

// Tide: a manifold for each of the contacts from `first` up to `end`, which the buffer is at: its first one,
// or zeros when it has none
static void b3SerContactManifoldRun( b3RecBuffer* buf, b3World* world, int first, int end )
{
	static const b3Manifold zeroManifold = { 0 };
	for ( int i = first; i < end; ++i )
	{
		const b3Contact* c = world->contacts.data + i;
		bool has = c->contactId == i && c->manifoldCount > 0 && c->manifolds != NULL;
		b3SerManifolds( buf, has ? c->manifolds : &zeroManifold, 1 );
	}
}

static void b3SerContacts( b3RecBuffer* buf, b3World* world )
{
	int count = world->contacts.count;
	b3SnapW_I32( buf, count );

	int start = buf->size;
	const int stride = (int)sizeof( b3Contact );

	if ( buf->parts != NULL )
	{
		// Tide: an image in parts has the contacts that may have changed, and the manifold of each, each at
		// its place
		const int manifoldStart = start + count * stride;
		int cursor = 0, first, end;
		while ( b3SnapNextRun( &world->snapTrack.contacts, count, &cursor, &first, &end ) )
		{
			b3SnapSkipTo( buf, start + first * stride );
			b3SerContactRun( buf, world, start, first, end );
		}
		cursor = 0;
		while ( b3SnapNextRun( &world->snapTrack.contacts, count, &cursor, &first, &end ) )
		{
			b3SnapSkipTo( buf, manifoldStart + first * (int)sizeof( b3Manifold ) );
			b3SerContactManifoldRun( buf, world, first, end );
		}
		b3SnapSkipTo( buf, manifoldStart + count * (int)sizeof( b3Manifold ) );
	}
	else
	{
		if ( count > 0 )
		{
			b3SerContactRun( buf, world, start, 0, count );
		}
		b3SerContactManifoldRun( buf, world, 0, count );
	}

	// Tide: an image in parts of a world that had no mesh, height field or compound looks for no contact
	// with one, which are the contacts that have more here
	if ( buf->parts != NULL && world->snapTrack.oddShapeCount == 0 )
	{
		return;
	}

	for ( int i = 0; i < count; ++i )
	{
		const b3Contact* c = world->contacts.data + i;
		if ( c->contactId != i )
		{
			continue;
		}

		if ( c->manifoldCount > 1 && c->manifolds != NULL )
		{
			b3SerManifolds( buf, c->manifolds + 1, c->manifoldCount - 1 );
		}

		// Mesh triangleCache
		if ( c->flags & b3_simMeshContact )
		{
			b3SnapW_I32( buf, c->meshContact.triangleCache.count );
			int cacheStart = buf->size;
			if ( c->meshContact.triangleCache.count > 0 )
			{
				b3SnapW_Bytes( buf, c->meshContact.triangleCache.data,
							   c->meshContact.triangleCache.count * (int)sizeof( b3TriangleCache ) );
			}
			// After a simplex cache's indices
			b3SnapZero( buf, cacheStart, c->meshContact.triangleCache.count, (int)sizeof( b3TriangleCache ), 18, 2 );
		}
	}
}

static void b3DesContacts( b3SnapReader* r, b3World* world )
{
	int count = b3SnapR_I32( r );
	if ( r->ok &&
		 b3SnapCheckCount( r, count, (int)sizeof( b3Contact ), (int)sizeof( b3Contact ) + (int)sizeof( b3Manifold ) ) == false )
	{
		r->ok = false;
	}
	if ( !r->ok )
	{
		return;
	}

	b3Array_Resize( world->contacts, count );
	memset( world->contacts.data, 0, (size_t)count * sizeof( b3Contact ) );
	if ( count > 0 )
	{
		b3SnapR_Bytes( r, world->contacts.data, count * (int)sizeof( b3Contact ) );
	}

	for ( int i = 0; i < count && r->ok; ++i )
	{
		b3Contact* dst = world->contacts.data + i;
		dst->manifoldsBits = 0;
		dst->manifolds = NULL;
		if ( dst->flags & b3_simMeshContact )
		{
			dst->meshContact.triangleCacheBits[0] = 0;
			dst->meshContact.triangleCacheBits[1] = 0;
			dst->meshContact.triangleCache.data = NULL;
			dst->meshContact.triangleCache.count = 0;
			dst->meshContact.triangleCache.capacity = 0;
		}

		bool isLive = ( dst->contactId == i );
		int manifoldCount = dst->manifoldCount;

		// Tide: its first manifold, which is zeros when it has none
		if ( isLive && manifoldCount > 0 )
		{
			if ( b3SnapCheckCount( r, manifoldCount, (int)sizeof( b3Manifold ), 0 ) == false )
			{
				r->ok = false;
				break;
			}
			dst->manifolds = b3AllocateManifolds( world, manifoldCount );
			b3SnapR_Bytes( r, dst->manifolds, (int)sizeof( b3Manifold ) );
		}
		else
		{
			b3Manifold none;
			b3SnapR_Bytes( r, &none, (int)sizeof( b3Manifold ) );
			dst->manifoldCount = 0;
		}
	}

	for ( int i = 0; i < count && r->ok; ++i )
	{
		b3Contact* dst = world->contacts.data + i;
		if ( dst->contactId != i )
		{
			continue;
		}

		if ( dst->manifoldCount > 1 )
		{
			b3SnapR_Bytes( r, dst->manifolds + 1, ( dst->manifoldCount - 1 ) * (int)sizeof( b3Manifold ) );
		}

		// Mesh triangleCache
		if ( dst->flags & b3_simMeshContact )
		{
			int cacheCount = b3SnapR_I32( r );
			if ( !r->ok )
			{
				break;
			}
			if ( cacheCount > 0 )
			{
				if ( b3SnapCheckCount( r, cacheCount, (int)sizeof( b3TriangleCache ), (int)sizeof( b3TriangleCache ) ) == false )
				{
					r->ok = false;
					break;
				}
				b3Array_Resize( dst->meshContact.triangleCache, cacheCount );
				b3SnapR_Bytes( r, dst->meshContact.triangleCache.data, cacheCount * (int)sizeof( b3TriangleCache ) );
			}
		}
	}
}

// Free per-object heap that b3DeserializeIntoShell will overwrite,
// so restoring over a populated world doesn't leak.
static void b3FreeLiveSimElements( b3World* world )
{
	// Shape heap: materials and hull DB references
	for ( int i = 0; i < world->shapes.count; ++i )
	{
		b3Shape* s = world->shapes.data + i;
		if ( s->id != i )
		{
			continue;
		}
		// A single material lives inline (materials == NULL). Multi material meshes and compounds own
		// the array, so free it exactly as b3DestroyShapeAllocations does.
		if ( s->materials != NULL )
		{
			b3Free( s->materials, (size_t)s->materialCount * sizeof( b3SurfaceMaterial ) );
			s->materials = NULL;
			s->materialCount = 0;
		}
		// Hull is ref-counted in the world DB; release before overwrite so re-adding is ref-neutral.
		if ( s->type == b3_hullShape && s->hull != NULL )
		{
			b3RemoveHullFromDatabase( world, s->hull );
			s->hull = NULL;
		}
		// name / userData / userShape are host-owned; do not free
	}

	// Contact heap: manifolds + mesh triangleCache
	for ( int i = 0; i < world->contacts.count; ++i )
	{
		b3Contact* c = world->contacts.data + i;
		if ( c->contactId == i )
		{
			if ( c->manifolds != NULL )
			{
				b3FreeManifolds( world, c->manifolds, c->manifoldCount );
				c->manifolds = NULL;
				c->manifoldCount = 0;
			}
			if ( c->flags & b3_simMeshContact )
			{
				b3Array_Destroy( c->meshContact.triangleCache );
			}
		}
	}

	// Sensor heap: inner arrays
	for ( int i = 0; i < world->sensors.count; ++i )
	{
		b3Sensor* sensor = world->sensors.data + i;
		b3Array_Destroy( sensor->hits );
		b3Array_Destroy( sensor->overlaps1 );
		b3Array_Destroy( sensor->overlaps2 );
	}

	// Island heap: inner arrays
	for ( int i = 0; i < world->islands.count; ++i )
	{
		b3Island* island = world->islands.data + i;
		b3Array_Destroy( island->bodies );
		b3Array_Destroy( island->contacts );
		b3Array_Destroy( island->joints );
	}
}

// Tide: the bodies from `first` up to `end`, which the buffer is at: `start` is where the first body of all
// goes. As b3SerShapeRun, for an image written whole and for one in parts alike.
static void b3SerBodyRun( b3RecBuffer* buf, b3World* world, int start, int first, int end )
{
	const int stride = (int)sizeof( b3Body );
	int count = end - first;
	int runStart = start + first * stride;
	b3SnapW_Bytes( buf, world->bodies.data + first, count * stride );
	b3SnapZero( buf, runStart, count, stride, (int)offsetof( b3Body, userDataBits ), 8 );
	b3SnapZero( buf, runStart, count, stride, 130, 6 );
}

static void b3SerBodies( b3RecBuffer* buf, b3World* world )
{
	int count = world->bodies.count;
	b3SnapW_I32( buf, count );

	int start = buf->size;
	const int stride = (int)sizeof( b3Body );

	if ( buf->parts != NULL )
	{
		// Tide: an image in parts has the bodies that may have changed, each at its place
		int cursor = 0, first, end;
		while ( b3SnapNextRun( &world->snapTrack.bodies, count, &cursor, &first, &end ) )
		{
			b3SnapSkipTo( buf, start + first * stride );
			b3SerBodyRun( buf, world, start, first, end );
		}
		b3SnapSkipTo( buf, start + count * stride );
	}
	else if ( count > 0 )
	{
		b3SerBodyRun( buf, world, start, 0, count );
	}
}

static inline uint8_t* b3SnapPutI32( uint8_t* at, int v )
{
	int32_t w = (int32_t)v;
	memcpy( at, &w, 4 );
	return at + 4;
}

static inline uint8_t* b3SnapPutBytes( uint8_t* at, const void* src, int n )
{
	if ( n > 0 )
	{
		memcpy( at, src, (size_t)n );
	}
	return at + n;
}

// Tide: the islands as upstream writes them, each its four numbers and then its bodies, its contacts and its
// joints, each with how many first. There's an island for every few bodies and most have a few numbers, so
// they're written into room asked for once, where upstream appends each number: the same bytes, at a tenth
// of what they cost.
_Static_assert( sizeof( b3ContactLink ) == 12 && sizeof( b3JointLink ) == 12, "an island's links in an image" );

static void b3SerIslands( b3RecBuffer* buf, const b3World* world )
{
	int islandCount = world->islands.count;
	const b3Island* islands = world->islands.data;

	int size = 4;
	for ( int i = 0; i < islandCount; ++i )
	{
		const b3Island* island = islands + i;
		size += 7 * 4 + island->bodies.count * 4 + island->contacts.count * 12 + island->joints.count * 12;
	}

	uint8_t* at = b3RecBufReserve( buf, size );
	if ( at == NULL )
	{
		// Only counted
		return;
	}

	const uint8_t* end = at + size;
	at = b3SnapPutI32( at, islandCount );
	for ( int i = 0; i < islandCount; ++i )
	{
		const b3Island* island = islands + i;
		at = b3SnapPutI32( at, island->setIndex );
		at = b3SnapPutI32( at, island->localIndex );
		at = b3SnapPutI32( at, island->islandId );
		at = b3SnapPutI32( at, island->constraintRemoveCount );
		at = b3SnapPutI32( at, island->bodies.count );
		at = b3SnapPutBytes( at, island->bodies.data, island->bodies.count * 4 );
		at = b3SnapPutI32( at, island->contacts.count );
		at = b3SnapPutBytes( at, island->contacts.data, island->contacts.count * 12 );
		at = b3SnapPutI32( at, island->joints.count );
		at = b3SnapPutBytes( at, island->joints.data, island->joints.count * 12 );
	}
	B3_ASSERT( at == end );
	B3_UNUSED( end );
}

int b3SerializeWorld( b3World* world, b3RecBuffer* buf, b3Recording* rec )
{
	int startSize = buf->size;

	// Snapshot header
	b3SnapHeader hdr;
	hdr.magic = B3_SNAP_MAGIC;
	hdr.version = B3_SNAP_VERSION;
	hdr.layoutHash = b3ComputeLayoutHash();
	hdr.flags = B3_ENABLE_VALIDATION ? B3_SNAP_FLAG_VALIDATION : 0u;
#if defined( BOX3D_DOUBLE_PRECISION )
	hdr.flags |= B3_SNAP_FLAG_DOUBLE_PRECISION;
#endif
	if ( buf->bases != NULL )
	{
		hdr.flags |= B3_SNAP_FLAG_SECTIONS;
	}
	b3SnapW_Bytes( buf, &hdr, (int)sizeof( hdr ) );
	if ( buf->bases != NULL )
	{
		b3SnapW_Bytes( buf, buf->bases, b3_sectionCount * (int)sizeof( int ) );
	}

	// World scalars
	b3SnapSection( buf, b3_sectionConfig );
	b3SerWorldConfig( buf, world );

	// 6 id pools (Box3D has no chainIdPool)
	b3SerIdPool( buf, &world->bodyIdPool );
	b3SerIdPool( buf, &world->shapeIdPool );
	b3SerIdPool( buf, &world->contactIdPool );
	b3SerIdPool( buf, &world->jointIdPool );
	b3SerIdPool( buf, &world->islandIdPool );
	b3SerIdPool( buf, &world->solverSetIdPool );

	// Solver sets
	b3SnapSection( buf, b3_sectionFixedSets );
	int setCount = world->solverSets.count;
	b3SnapW_I32( buf, setCount );
	for ( int i = 0; i < setCount; ++i )
	{
		if ( i == b3_awakeSet )
		{
			b3SnapSection( buf, b3_sectionAwakeSet );
		}
		else if ( i == b3_firstSleepingSet )
		{
			b3SnapSection( buf, b3_sectionSleepingSets );

			// Tide: nothing writes a sleeping set but what says so (b3SnapTouchSleepingSet)
			if ( b3SnapKeepSection( buf, world->snapTrack.setsTouched ) )
			{
				break;
			}
		}
		b3SerSolverSet( buf, world->solverSets.data + i );
	}
	if ( setCount <= b3_awakeSet )
	{
		b3SnapSection( buf, b3_sectionAwakeSet );
	}
	if ( setCount <= b3_firstSleepingSet )
	{
		b3SnapSection( buf, b3_sectionSleepingSets );
	}

	// Sparse body array (userData is host wiring, zero it)
	// Tide: all 8 bytes of it, and what's after a body's generation
	b3SnapSection( buf, b3_sectionBodies );
	b3SerBodies( buf, world );

	// Shape sparse array with geometry interning
	b3SnapSection( buf, b3_sectionShapes );
	b3SerShapes( buf, world, rec );

	b3SnapSection( buf, b3_sectionFatAABBs );
	b3SerPodArray( buf, world->fatAABBs );

	// Contact sparse array with manifold and mesh triangleCache
	b3SnapSection( buf, b3_sectionContacts );
	b3SerContacts( buf, world );

	// Joint sparse array (userData scrubbed)
	// Tide: all 8 bytes of it, and what's after a joint's collideConnected
	b3SnapSection( buf, b3_sectionJoints );
	{
		int jointCount = world->joints.count;
		b3SnapW_I32( buf, jointCount );
		int start = buf->size;
		if ( jointCount > 0 )
		{
			b3SnapW_Bytes( buf, world->joints.data, jointCount * (int)sizeof( b3Joint ) );
		}
		b3SnapZero( buf, start, jointCount, (int)sizeof( b3Joint ), (int)offsetof( b3Joint, userDataBits ), 8 );
		b3SnapZero( buf, start, jointCount, (int)sizeof( b3Joint ), 67, 5 );
	}

	// Sensors: shapeId + 3 inner arrays each
	b3SnapSection( buf, b3_sectionSensors );
	{
		int sensorCount = world->sensors.count;
		b3SnapW_I32( buf, sensorCount );
		for ( int i = 0; i < sensorCount; ++i )
		{
			b3Sensor* s = world->sensors.data + i;
			b3SnapW_I32( buf, s->shapeId );
			// Tide: after a visitor's generation
			b3SerPodArrayZero( buf, s->hits, 6, 2 );
			b3SerPodArrayZero( buf, s->overlaps1, 6, 2 );
			b3SerPodArrayZero( buf, s->overlaps2, 6, 2 );
		}
	}

	// Islands: 4 scalars + 3 inner arrays each
	b3SnapSection( buf, b3_sectionIslands );
	b3SerIslands( buf, world );

	// Broad phase
	b3BroadPhase* bp = &world->broadPhase;
	for ( int t = 0; t < b3_bodyTypeCount; ++t )
	{
		b3SnapSection( buf, b3_sectionStaticTree + t );
		b3SerTree( buf, &bp->trees[t] );
	}
	b3SnapSection( buf, b3_sectionPairs );
	// Tide: a pair comes and goes with its contact, which says so (b3SnapTouchPairs)
	if ( b3SnapKeepSection( buf, world->snapTrack.pairsTouched ) == false )
	{
		b3SerHashSet( buf, &bp->pairSet );
	}

	// Constraint graph
	b3SnapSection( buf, b3_sectionGraph );
	b3ConstraintGraph* graph = &world->constraintGraph;
	for ( int c = 0; c < B3_GRAPH_COLOR_COUNT; ++c )
	{
		b3SerGraphColor( buf, &graph->colors[c], c == B3_OVERFLOW_INDEX );
	}

	b3SnapSection( buf, b3_sectionNames );
	b3SerNames( buf, &world->names );
	b3SnapSection( buf, b3_sectionCount ); // Tide: the last one's size

	return buf->size - startSize;
}

// Tide: checks an image's header and readies a reader past it, with where its sections start. False for
// bytes that are no image of this build's, with nothing else done.
static bool b3OpenSnapshotImage( const uint8_t* data, int size, b3SnapReader* r )
{
	if ( data == NULL || size < (int)sizeof( b3SnapHeader ) )
	{
		return false;
	}

	// Validate header
	b3SnapHeader hdr;
	memcpy( &hdr, data, sizeof( hdr ) );
	if ( hdr.magic != B3_SNAP_MAGIC || hdr.version != B3_SNAP_VERSION )
	{
		printf( "b3DeserializeIntoShell: bad magic/version\n" );
		return false;
	}
	bool imageDouble = ( hdr.flags & B3_SNAP_FLAG_DOUBLE_PRECISION ) != 0;
#if defined( BOX3D_DOUBLE_PRECISION )
	bool buildDouble = true;
#else
	bool buildDouble = false;
#endif
	if ( imageDouble != buildDouble )
	{
		printf( "b3DeserializeIntoShell: precision mismatch\n" );
		return false;
	}
	if ( hdr.layoutHash != b3ComputeLayoutHash() )
	{
		printf( "b3DeserializeIntoShell: layout hash mismatch\n" );
		return false;
	}

	r->data = data;
	r->cursor = (int)sizeof( b3SnapHeader );
	r->size = size;
	r->ok = true;

	// Tide: where its sections start, each after the one before it and inside the image
	r->sectioned = ( hdr.flags & B3_SNAP_FLAG_SECTIONS ) != 0;
	if ( r->sectioned )
	{
		b3SnapR_Bytes( r, r->bases, (int)sizeof( r->bases ) );
		int last = r->cursor;
		for ( int i = 0; i < b3_sectionCount && r->ok; ++i )
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

bool b3DeserializeIntoShell( const uint8_t* data, int size, b3World* world, b3RecReader* rdr )
{
	b3SnapReader readerStorage;
	b3SnapReader* r = &readerStorage;
	if ( b3OpenSnapshotImage( data, size, r ) == false )
	{
		return false;
	}

	// Tide: the world becomes another: its next image is written whole (see b3SnapTrack)
	b3SnapTouchAll( world );

	// Free existing per-object heap before overwriting
	b3FreeLiveSimElements( world );

	// 1. World scalars
	b3SnapSeek( r, b3_sectionConfig );
	b3DesWorldConfig( r, world );

	// 2. 6 id pools; destroy the pre-created sets' pool state first
	b3DesIdPool( r, &world->bodyIdPool );
	b3DesIdPool( r, &world->shapeIdPool );
	b3DesIdPool( r, &world->contactIdPool );
	b3DesIdPool( r, &world->jointIdPool );
	b3DesIdPool( r, &world->islandIdPool );
	b3DesIdPool( r, &world->solverSetIdPool );

	// 3. Solver sets: destroy inner arrays of existing sets first
	for ( int i = 0; i < world->solverSets.count; ++i )
	{
		b3SolverSet* set = world->solverSets.data + i;
		b3Array_Destroy( set->bodySims );
		b3Array_Destroy( set->bodyStates );
		b3Array_Destroy( set->jointSims );
		b3Array_Destroy( set->contactIndices );
		b3Array_Destroy( set->islandSims );
	}

	b3SnapSeek( r, b3_sectionFixedSets );
	int setCount = b3SnapR_I32( r );
	if ( r->ok && b3SnapCheckCount( r, setCount, (int)sizeof( b3SolverSet ), 6 * (int)sizeof( int ) ) == false )
	{
		r->ok = false;
	}
	if ( r->ok )
	{
		b3Array_Resize( world->solverSets, setCount );
		memset( world->solverSets.data, 0, (size_t)setCount * sizeof( b3SolverSet ) );
		for ( int i = 0; i < setCount; ++i )
		{
			if ( i == b3_awakeSet )
			{
				b3SnapSeek( r, b3_sectionAwakeSet );
			}
			else if ( i == b3_firstSleepingSet )
			{
				b3SnapSeek( r, b3_sectionSleepingSets );
			}
			b3DesSolverSet( r, world->solverSets.data + i );
		}
	}

	if ( !r->ok )
	{
		return false;
	}

	// 4. Body sparse array
	b3SnapSeek( r, b3_sectionBodies );
	{
		int bodyCount = b3SnapR_I32( r );
		if ( r->ok && b3SnapCheckCount( r, bodyCount, (int)sizeof( b3Body ), (int)sizeof( b3Body ) ) == false )
		{
			r->ok = false;
		}
		if ( r->ok )
		{
			b3Array_Resize( world->bodies, bodyCount );
			if ( bodyCount > 0 )
			{
				b3SnapR_Bytes( r, world->bodies.data, bodyCount * (int)sizeof( b3Body ) );
			}
			for ( int i = 0; i < bodyCount; ++i )
			{
				world->bodies.data[i].userDataBits = 0;
				world->bodies.data[i].userData = NULL;
			}
		}
	}

	if ( !r->ok )
	{
		return false;
	}

	// 5. Shape sparse array
	b3SnapSeek( r, b3_sectionShapes );
	b3DesShapes( r, world, rdr );

	if ( !r->ok )
	{
		return false;
	}

	b3SnapSeek( r, b3_sectionFatAABBs );
	b3DesPodArray( r, world->fatAABBs );

	if ( !r->ok )
	{
		return false;
	}

	// 6. Contact sparse array
	b3SnapSeek( r, b3_sectionContacts );
	b3DesContacts( r, world );

	if ( !r->ok )
	{
		return false;
	}

	// 7. Joint sparse array
	b3SnapSeek( r, b3_sectionJoints );
	{
		int jointCount = b3SnapR_I32( r );
		if ( r->ok && b3SnapCheckCount( r, jointCount, (int)sizeof( b3Joint ), (int)sizeof( b3Joint ) ) == false )
		{
			r->ok = false;
		}
		if ( r->ok )
		{
			b3Array_Resize( world->joints, jointCount );
			if ( jointCount > 0 )
			{
				b3SnapR_Bytes( r, world->joints.data, jointCount * (int)sizeof( b3Joint ) );
			}
			for ( int i = 0; i < jointCount; ++i )
			{
				world->joints.data[i].userDataBits = 0;
				world->joints.data[i].userData = NULL;
			}
		}
	}

	// 8. Sensors
	b3SnapSeek( r, b3_sectionSensors );
	{
		b3Array_Destroy( world->sensors );
		b3Array_Create( world->sensors );

		int sensorCount = b3SnapR_I32( r );
		if ( r->ok && b3SnapCheckCount( r, sensorCount, (int)sizeof( b3Sensor ), 4 * (int)sizeof( int ) ) == false )
		{
			r->ok = false;
		}
		if ( r->ok )
		{
			b3Array_Resize( world->sensors, sensorCount );
			memset( world->sensors.data, 0, (size_t)sensorCount * sizeof( b3Sensor ) );
		}

		for ( int i = 0; i < sensorCount && r->ok; ++i )
		{
			b3Sensor* s = world->sensors.data + i;
			s->shapeId = b3SnapR_I32( r );
			b3Array_Create( s->hits );
			b3Array_Create( s->overlaps1 );
			b3Array_Create( s->overlaps2 );
			b3DesPodArray( r, s->hits );
			b3DesPodArray( r, s->overlaps1 );
			b3DesPodArray( r, s->overlaps2 );
		}
	}

	// 9. Islands
	b3SnapSeek( r, b3_sectionIslands );
	{
		b3Array_Destroy( world->islands );
		b3Array_Create( world->islands );

		int islandCount = b3SnapR_I32( r );
		if ( r->ok && b3SnapCheckCount( r, islandCount, (int)sizeof( b3Island ), 7 * (int)sizeof( int ) ) == false )
		{
			r->ok = false;
		}
		if ( r->ok )
		{
			b3Array_Resize( world->islands, islandCount );
			memset( world->islands.data, 0, (size_t)islandCount * sizeof( b3Island ) );
		}

		for ( int i = 0; i < islandCount && r->ok; ++i )
		{
			b3Island* island = world->islands.data + i;
			island->setIndex = b3SnapR_I32( r );
			island->localIndex = b3SnapR_I32( r );
			island->islandId = b3SnapR_I32( r );
			island->constraintRemoveCount = b3SnapR_I32( r );
			b3Array_Create( island->bodies );
			b3Array_Create( island->contacts );
			b3Array_Create( island->joints );
			b3DesPodArray( r, island->bodies );
			b3DesPodArray( r, island->contacts );
			b3DesPodArray( r, island->joints );
		}
	}

	// 10. Broad phase
	{
		b3BroadPhase* bp = &world->broadPhase;

		for ( int t = 0; t < b3_bodyTypeCount; ++t )
		{
			b3SnapSeek( r, b3_sectionStaticTree + t );
			b3DesTree( r, &bp->trees[t] );
		}
		b3SnapSeek( r, b3_sectionPairs );
		b3DesHashSet( r, &bp->pairSet );
		// The gathered moved siblings are per step scratch and stay at the shell's NULL
	}

	// 11. Constraint graph
	b3SnapSeek( r, b3_sectionGraph );
	{
		b3ConstraintGraph* graph = &world->constraintGraph;
		for ( int c = 0; c < B3_GRAPH_COLOR_COUNT; ++c )
		{
			b3DesGraphColor( r, &graph->colors[c], c == B3_OVERFLOW_INDEX );
		}
	}

	b3SnapSeek( r, b3_sectionNames );
	b3DesNames( r, &world->names );

	// Tide: events are a step's, and aren't in an image. A world restored in place has the ones its last
	// step left, which the world the image was taken of may not have had: none, as a world made from
	// nothing has.
	b3Array_Clear( world->bodyMoveEvents );
	b3Array_Clear( world->sensorBeginEvents );
	b3Array_Clear( world->sensorEndEvents[0] );
	b3Array_Clear( world->sensorEndEvents[1] );
	b3Array_Clear( world->contactBeginEvents );
	b3Array_Clear( world->contactEndEvents[0] );
	b3Array_Clear( world->contactEndEvents[1] );
	b3Array_Clear( world->contactHitEvents );
	b3Array_Clear( world->jointEvents );

	return r->ok;
}

// Tide: which bodies, shapes and contacts an image in parts writes (see ../TIDE.md, and b3SnapTrack in
// physics_world.h).
//
// Between two images, a body, a shape or a contact, with its manifold, has changed only if
// - the body, or the shape's body, or one of the contact's two, was awake at the first image, at the second,
//   or at some time between: the step writes those, on its worker threads, and nothing says which;
// - or something was done to it from outside, which is said where it's done (b3SnapTouchBody and the like):
//   a contact that starts or ends changes the lists of both its bodies and its neighbors in them, an
//   element taken out of an array moves another into its place, and calls change what they're for.
// A body that's awake at some time between and at neither image woke and fell asleep, or was made and
// destroyed, and each of those says so for the body, its shapes and its contacts. So the sets hold: what was
// awake at the last image (b3SnapCommit), what was said since, and what's awake now (b3SnapSeed).
void b3SnapGrowSet( b3SnapSet* set, int blockCount )
{
	// Room to spare, as ids come one after another
	int grown = blockCount + blockCount / 2 + 4;
	uint64_t* bits = b3Alloc( (size_t)grown * sizeof( uint64_t ) );
	memset( bits, 0, (size_t)grown * sizeof( uint64_t ) );
	if ( set->blockCount > 0 )
	{
		memcpy( bits, set->bits, (size_t)set->blockCount * sizeof( uint64_t ) );
		b3Free( set->bits, (size_t)set->blockCount * sizeof( uint64_t ) );
	}
	set->bits = bits;
	set->blockCount = grown;
}

static void b3SnapClearSet( b3SnapSet* set )
{
	if ( set->blockCount > 0 )
	{
		memset( set->bits, 0, (size_t)set->blockCount * sizeof( uint64_t ) );
	}
}

void b3SnapTouchBodyDeep( b3World* world, const b3Body* body )
{
	b3SnapTrack* track = &world->snapTrack;
	if ( track->sparse == false || body->id == B3_NULL_INDEX )
	{
		return;
	}

	b3SnapSetBit( &track->bodies, body->id );

	int shapeId = body->headShapeId;
	while ( shapeId != B3_NULL_INDEX )
	{
		b3SnapSetBit( &track->shapes, shapeId );
		shapeId = world->shapes.data[shapeId].nextShapeId;
	}

	int contactKey = body->headContactKey;
	while ( contactKey != B3_NULL_INDEX )
	{
		int contactId = contactKey >> 1;
		b3SnapSetBit( &track->contacts, contactId );
		contactKey = world->contacts.data[contactId].edges[contactKey & 1].nextKey;
	}
}

void b3SnapNoteOp( b3World* world, int op )
{
	switch ( op )
	{
		// The step, and the calls that say what they write where they write it. b3CreateMeshShape and the
		// like aren't here by name, as they're recorded by hand: b3CreateShapeInternal says what's needed
		// for every shape.
		case b3_recOpStep:
		case b3_recOpCreateBody:
		case b3_recOpDestroyBody:
		case b3_recOpBodySetTransform:
		case b3_recOpBodySetLinearVelocity:
		case b3_recOpBodySetAngularVelocity:
		case b3_recOpBodySetTargetTransform:
		case b3_recOpBodyApplyForce:
		case b3_recOpBodyApplyForceToCenter:
		case b3_recOpBodyApplyTorque:
		case b3_recOpBodyApplyLinearImpulse:
		case b3_recOpBodyApplyLinearImpulseToCenter:
		case b3_recOpBodyApplyAngularImpulse:
		case b3_recOpCreateSphereShape:
		case b3_recOpCreateCapsuleShape:
		case b3_recOpCreateHullShape:
		// A setting of the world's, which every image writes
		case b3_recOpWorldSetGravity:
		// What changes nothing
		case b3_recOpDestroyWorld:
		case b3_recOpQueryOverlapAABB:
		case b3_recOpQueryOverlapShape:
		case b3_recOpQueryCastRay:
		case b3_recOpQueryCastShape:
		case b3_recOpQueryCastRayClosest:
		case b3_recOpQueryCastMover:
		case b3_recOpQueryCollideMover:
		case b3_recOpQueryTag:
		case b3_recOpStateHash:
		case b3_recOpRecordingBounds:
			return;

		// Any other: the next image is written whole
		default:
			b3SnapTouchAll( world );
			return;
	}
}

// How many of a world's bodies can be awake for its images to be written in parts: one in this many. With
// more, there's too little of an image left out for what finding it costs.
#define B3_SNAP_AWAKE_SHARE 2

static bool b3SnapMostlyAsleep( const b3World* world )
{
	int share = world->snapTrack.awakeShare > 0 ? world->snapTrack.awakeShare : B3_SNAP_AWAKE_SHARE;
	int awake = world->solverSets.data[b3_awakeSet].bodySims.count;
	return awake * share <= b3GetIdCount( &world->bodyIdPool );
}

// Every awake body, with its shapes and its contacts, into the sets
static void b3SnapSeed( b3World* world )
{
	const b3SolverSet* awakeSet = world->solverSets.data + b3_awakeSet;
	const b3BodySim* sims = awakeSet->bodySims.data;
	int count = awakeSet->bodySims.count;
	for ( int i = 0; i < count; ++i )
	{
		b3SnapTouchBodyDeep( world, world->bodies.data + sims[i].bodyId );
	}
}

// An image in sections was taken of the world, whole or in parts: what the next one goes on from
static void b3SnapCommit( b3World* world, const int* bases, const int* sizes, bool whole )
{
	b3SnapTrack* track = &world->snapTrack;
	memcpy( track->bases, bases, sizeof( track->bases ) );
	memcpy( track->sizes, sizes, sizeof( track->sizes ) );
	track->bodyCount = world->bodies.count;
	track->shapeCount = world->shapes.count;
	track->contactCount = world->contacts.count;
	track->setsTouched = false;
	track->pairsTouched = false;

	if ( whole )
	{
		int odd = 0;
		for ( int i = 0; i < world->shapes.count; ++i )
		{
			const b3Shape* shape = world->shapes.data + i;
			bool round = shape->type == b3_sphereShape || shape->type == b3_capsuleShape || shape->type == b3_hullShape;
			odd += shape->id == i && ( shape->materials != NULL || round == false );
		}
		track->oddShapeCount = odd;
	}

	track->sparse = b3SnapMostlyAsleep( world );
	if ( track->sparse )
	{
		b3SnapClearSet( &track->bodies );
		b3SnapClearSet( &track->shapes );
		b3SnapClearSet( &track->contacts );
		b3SnapSeed( world );
	}
}

void b3DestroySnapshotTrack( b3World* world )
{
	b3SnapTrack* track = &world->snapTrack;
	b3Free( track->bodies.bits, (size_t)track->bodies.blockCount * sizeof( uint64_t ) );
	b3Free( track->shapes.bits, (size_t)track->shapes.blockCount * sizeof( uint64_t ) );
	b3Free( track->contacts.bits, (size_t)track->contacts.blockCount * sizeof( uint64_t ) );

	b3SnapParts* p = track->parts;
	if ( p != NULL )
	{
		for ( int i = 0; i < p->blockCount; ++i )
		{
			b3Free( p->blocks[i].data, (size_t)p->blocks[i].capacity );
		}
		b3Free( p->blocks, (size_t)p->blockCapacity * sizeof( b3SnapBlock ) );
		b3Free( p->parts, (size_t)p->capacity * sizeof( b3SnapshotPart ) );
		b3Free( p, sizeof( b3SnapParts ) );
	}

	*track = ( b3SnapTrack ){ 0 };
}

void b3World_SetSnapshotAwakeShare( b3WorldId worldId, int share )
{
	b3World* world = b3GetWorldFromId( worldId );
	world->snapTrack.awakeShare = share;
}

// Tide: a world's image, and the world back from one, as calls of their own (see ../TIDE.md). Upstream has
// both inside its recording only.
int b3GetSnapshotHead( void )
{
	return (int)sizeof( b3SnapHeader ) + b3_sectionCount * (int)sizeof( int );
}

// An image written whole. With `last`, it's the one the world's next image in parts goes on from.
static int b3SnapWhole( b3WorldId worldId, uint8_t* image, int capacity, const int* bases, int* sizes, bool last )
{
	b3World* world = b3GetWorldFromId( worldId );

	// Serializing mid step would capture an inconsistent, in-flight world
	B3_ASSERT( world->locked == false );
	if ( world->locked )
	{
		return 0;
	}

	// Straight into the caller's memory, once: what doesn't fit is counted and not written
	int ownSizes[b3_sectionCount];
	b3RecBuffer buf = { 0 };
	buf.data = image;
	buf.capacity = image != NULL ? capacity : 0;
	buf.fixed = true;
	buf.bases = bases;
	buf.sizes = ownSizes;
	b3SerializeWorld( world, &buf, NULL );
	if ( sizes != NULL )
	{
		memcpy( sizes, ownSizes, sizeof( ownSizes ) );
	}

	// An image in sections that's all there is what the next one in parts goes on from. Any other leaves
	// the world with none to go on from: the caller is about to ask again.
	if ( last && bases != NULL )
	{
		if ( buf.failed == false && buf.overran == false && buf.countOnly == false )
		{
			b3SnapCommit( world, bases, ownSizes, true );
		}
		else
		{
			world->snapTrack.sparse = false;
		}
	}

	if ( buf.failed )
	{
		return 0;
	}
	return buf.overran ? -1 : buf.size;
}

int b3World_GetSnapshotAt( b3WorldId worldId, uint8_t* image, int capacity, const int* bases, int* sizes )
{
	return b3SnapWhole( worldId, image, capacity, bases, sizes, true );
}

int b3World_CheckSnapshotAt( b3WorldId worldId, uint8_t* image, int capacity, const int* bases, int* sizes )
{
	return b3SnapWhole( worldId, image, capacity, bases, sizes, false );
}

int b3World_GetSnapshotParts( b3WorldId worldId, const int* bases, int* sizes, const b3SnapshotPart** parts, int* partCount )
{
	b3World* world = b3GetWorldFromId( worldId );
	b3SnapTrack* track = &world->snapTrack;
	*parts = NULL;
	*partCount = 0;

	B3_ASSERT( world->locked == false );
	if ( world->locked )
	{
		return 0;
	}

	// Nothing to go on from, or sections that aren't where the last image had them, or room in it for
	// another number of bodies, shapes or contacts: every one after would be somewhere else. Or most
	// of the world is awake.
	if ( track->sparse == false || bases == NULL || memcmp( bases, track->bases, sizeof( track->bases ) ) != 0 ||
		 world->bodies.count != track->bodyCount || world->shapes.count != track->shapeCount ||
		 world->contacts.count != track->contactCount || b3SnapMostlyAsleep( world ) == false )
	{
		return 0;
	}

	// What's awake now, with what was awake at the last image and what was said since
	b3SnapSeed( world );

	if ( track->parts == NULL )
	{
		track->parts = b3Alloc( sizeof( b3SnapParts ) );
		memset( track->parts, 0, sizeof( b3SnapParts ) );
	}
	b3SnapParts* p = track->parts;
	b3SnapPartsReset( p, track->sizes );

	int ownSizes[b3_sectionCount];
	b3RecBuffer buf = { 0 };
	buf.fixed = true;
	buf.bases = bases;
	buf.sizes = ownSizes;
	buf.parts = p;
	b3SerializeWorld( world, &buf, NULL );
	if ( sizes != NULL )
	{
		memcpy( sizes, ownSizes, sizeof( ownSizes ) );
	}

	if ( buf.failed || buf.overran )
	{
		// No image: the caller takes one whole, with its sections placed again when one overran
		track->sparse = false;
		return buf.failed ? 0 : -1;
	}

	b3SnapCommit( world, bases, ownSizes, false );
	*parts = p->parts;
	*partCount = p->count;
	return buf.size;
}

bool b3GetSnapshotBases( const uint8_t* image, int size, int* bases )
{
	b3SnapReader reader;
	if ( b3OpenSnapshotImage( image, size, &reader ) == false || reader.sectioned == false )
	{
		return false;
	}
	memcpy( bases, reader.bases, sizeof( reader.bases ) );
	return true;
}

int b3World_GetSnapshotGeometry( b3WorldId worldId, uint8_t* geometry, int capacity )
{
	b3World* world = b3GetWorldFromId( worldId );
	const b3SnapGeometry* own = world->snapGeometry;
	if ( own == NULL )
	{
		return 0;
	}

	if ( geometry == NULL || capacity < own->byteCount )
	{
		return own->byteCount;
	}

	uint8_t* at = geometry;
	for ( int i = 0; i < own->count; ++i )
	{
		const b3SnapGeometryEntry* entry = own->entries + i;
		uint32_t kind = (uint32_t)entry->kind;
		uint32_t byteCount = (uint32_t)entry->byteCount;
		int recordSize = b3SnapGeometryRecordSize( entry->byteCount );
		memset( at, 0, (size_t)recordSize );
		memcpy( at, &kind, 4 );
		memcpy( at + 4, &byteCount, 4 );
		memcpy( at + 8, entry->bytes, (size_t)entry->byteCount );
		at += recordSize;
	}
	return own->byteCount;
}

bool b3World_Restore( b3WorldId worldId, const uint8_t* image, int size, const uint8_t* geometry, int geometrySize )
{
	// Validate what can be before touching the world, so bytes that are no image leave it intact
	b3SnapReader reader;
	if ( b3OpenSnapshotImage( image, size, &reader ) == false || b3SnapGeometryIsValid( geometry, geometrySize ) == false )
	{
		return false;
	}

	b3World* world = b3GetWorldFromId( worldId );

	// Restoring mid step would corrupt an in-flight solve
	B3_ASSERT( world->locked == false );
	if ( world->locked )
	{
		return false;
	}

	// Point of no return. The slot, generation, and host wiring are kept, so held ids resolve into the
	// rebuilt world. A truncated payload past here leaves the world unusable and the caller must destroy it.
	// The shapes let go of their geometry first, as some of it may go.
	b3FreeLiveSimElements( world );
	b3SnapGeometryLoad( world, geometry, geometrySize );

	return b3DeserializeIntoShell( image, size, world, NULL );
}
