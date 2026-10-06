// SPDX-FileCopyrightText: 2026 Erin Catto
// SPDX-License-Identifier: MIT

// Tide: this file isn't upstream's (see ../TIDE.md). It's simd_sse2.h's wide floats and bounds on WebAssembly's
// own SIMD, for a compiler that has no SSE2 headers to emulate with, which is every one but Emscripten's. Each
// function gives, bit for bit, what simd_sse2.h's gives for the same numbers, NaN and both zeros among them: a
// world's image has to be the same bytes in a browser as on x64. So the least and the greatest are the pseudo
// ones (f32x4.pmin, f32x4.pmax), with their operands the other way around, which are minps and maxps exactly;
// WebAssembly's own (f32x4.min, f32x4.max) take -0 for less than 0 and give NaN for either NaN. Nothing here is
// relaxed SIMD, whose results differ between machines.

#pragma once

#include "core.h"

#include "box3d/types.h"

#include <wasm_simd128.h>

// wide float holds 4 numbers
typedef v128_t b3FloatW4;

static inline b3FloatW4 b3ZeroW4( void )
{
	return wasm_f32x4_const_splat( 0.0f );
}

static inline b3FloatW4 b3SplatW4( float scalar )
{
	return wasm_f32x4_splat( scalar );
}

static inline b3FloatW4 b3SetW4( float a, float b, float c, float d )
{
	return wasm_f32x4_make( a, b, c, d );
}

static inline b3FloatW4 b3LoadW4( const float* data )
{
	return wasm_v128_load( data );
}

static inline void b3StoreW4( float* data, b3FloatW4 a )
{
	wasm_v128_store( data, a );
}

// The sign's bit alone, as SSE2's xor with -0 changes it
static inline b3FloatW4 b3NegW4( b3FloatW4 a )
{
	return wasm_f32x4_neg( a );
}

// The sign's bit alone, as SSE2's and-not with -0 clears it
static inline b3FloatW4 b3AbsW4( b3FloatW4 a )
{
	return wasm_f32x4_abs( a );
}

static inline b3FloatW4 b3AddW4( b3FloatW4 a, b3FloatW4 b )
{
	return wasm_f32x4_add( a, b );
}

static inline b3FloatW4 b3SubW4( b3FloatW4 a, b3FloatW4 b )
{
	return wasm_f32x4_sub( a, b );
}

static inline b3FloatW4 b3MulW4( b3FloatW4 a, b3FloatW4 b )
{
	return wasm_f32x4_mul( a, b );
}

static inline b3FloatW4 b3DivW4( b3FloatW4 a, b3FloatW4 b )
{
	return wasm_f32x4_div( a, b );
}

static inline b3FloatW4 b3SqrtW4( b3FloatW4 a )
{
	return wasm_f32x4_sqrt( a );
}

// a + b * c, the product rounded before the sum
static inline b3FloatW4 b3MulAddW4( b3FloatW4 a, b3FloatW4 b, b3FloatW4 c )
{
	return wasm_f32x4_add( a, wasm_f32x4_mul( b, c ) );
}

// minps: a where it's less than b, and else b
static inline b3FloatW4 b3MinW4( b3FloatW4 a, b3FloatW4 b )
{
	return wasm_f32x4_pmin( b, a );
}

// maxps: a where it's greater than b, and else b
static inline b3FloatW4 b3MaxW4( b3FloatW4 a, b3FloatW4 b )
{
	return wasm_f32x4_pmax( b, a );
}

// clamp a to [-b, b]
static inline b3FloatW4 b3SymClampW4( b3FloatW4 a, b3FloatW4 b )
{
	b3FloatW4 nb = b3NegW4( b );
	b3FloatW4 c = b3MaxW4( nb, a );
	return b3MinW4( c, b );
}

static inline b3FloatW4 b3AndW4( b3FloatW4 a, b3FloatW4 b )
{
	return wasm_v128_and( a, b );
}

static inline b3FloatW4 b3OrW4( b3FloatW4 a, b3FloatW4 b )
{
	return wasm_v128_or( a, b );
}

// a & ~b
static inline b3FloatW4 b3AndNotW4( b3FloatW4 a, b3FloatW4 b )
{
	return wasm_v128_andnot( a, b );
}

// This is used to optimize selection of contact softness.
static inline b3FloatW4 b3SoftMaskW4( const int* indexA, const int* indexB )
{
	v128_t zero = wasm_i32x4_const_splat( 0 );
	v128_t a = wasm_i32x4_eq( wasm_v128_load( indexA ), zero );
	v128_t b = wasm_i32x4_eq( wasm_v128_load( indexB ), zero );
	return wasm_v128_or( a, b );
}

// The comparisons are false for a NaN, as SSE2's are
static inline b3FloatW4 b3GreaterThanW4( b3FloatW4 a, b3FloatW4 b )
{
	return wasm_f32x4_gt( a, b );
}

static inline b3FloatW4 b3LessThanW4( b3FloatW4 a, b3FloatW4 b )
{
	return wasm_f32x4_lt( a, b );
}

static inline b3FloatW4 b3EqualsW4( b3FloatW4 a, b3FloatW4 b )
{
	return wasm_f32x4_eq( a, b );
}

static inline bool b3AllZeroW4( b3FloatW4 a )
{
	// A bit for each lane that is zero, as movmskps of the comparison
	return wasm_i32x4_bitmask( wasm_f32x4_eq( a, wasm_f32x4_const_splat( 0.0f ) ) ) == 0xF;
}

static inline bool b3AnyTrueW4( b3FloatW4 mask )
{
	// Each lane's top bit, as movmskps
	return wasm_i32x4_bitmask( mask ) != 0;
}

// component-wise returns mask ? b : a
static inline b3FloatW4 b3BlendW4( b3FloatW4 a, b3FloatW4 b, b3FloatW4 mask )
{
	return wasm_v128_bitselect( b, a, mask );
}

// Replace the low bitCount mantissa bits of each lane with baseIndex + lane. The value must be
// positive so the embedded index sorts with the value, and ties fall to the lower index.
static inline b3FloatW4 b3EmbedIndexW4( b3FloatW4 value, int baseIndex, int bitCount )
{
	int mask = ( 1 << bitCount ) - 1;
	v128_t index = wasm_i32x4_add( wasm_i32x4_splat( baseIndex ), wasm_i32x4_const( 0, 1, 2, 3 ) );
	v128_t clearLow = wasm_i32x4_splat( ~mask );
	return wasm_v128_or( wasm_v128_and( value, clearLow ), index );
}

// Recovers the index embedded by b3EmbedIndexW4 from the lane holding the minimum.
static inline int b3MinIndexW4( b3FloatW4 a, int bitCount )
{
	// With [a1 a0 a3 a2], then with [a2 a3 a0 a1], in SSE2's order
	a = b3MinW4( a, wasm_i32x4_shuffle( a, a, 1, 0, 3, 2 ) );
	a = b3MinW4( a, wasm_i32x4_shuffle( a, a, 2, 3, 0, 1 ) );
	return wasm_i32x4_extract_lane( a, 0 ) & ( ( 1 << bitCount ) - 1 );
}

B3_FORCE_INLINE void b3TransposeW4( b3FloatW4 r0, b3FloatW4 r1, b3FloatW4 r2, b3FloatW4 r3, b3FloatW4* c0, b3FloatW4* c1,
									b3FloatW4* c2, b3FloatW4* c3 )
{
	// unpcklps and unpckhps: the low halves of two rows, a lane of each in turn, and the high halves
	b3FloatW4 t0 = wasm_i32x4_shuffle( r0, r2, 0, 4, 1, 5 );
	b3FloatW4 t1 = wasm_i32x4_shuffle( r1, r3, 0, 4, 1, 5 );
	b3FloatW4 t2 = wasm_i32x4_shuffle( r0, r2, 2, 6, 3, 7 );
	b3FloatW4 t3 = wasm_i32x4_shuffle( r1, r3, 2, 6, 3, 7 );
	*c0 = wasm_i32x4_shuffle( t0, t1, 0, 4, 1, 5 );
	*c1 = wasm_i32x4_shuffle( t0, t1, 2, 6, 3, 7 );
	*c2 = wasm_i32x4_shuffle( t2, t3, 0, 4, 1, 5 );
	*c3 = wasm_i32x4_shuffle( t2, t3, 2, 6, 3, 7 );
}

// A single 3D vector (b3V32, which SSE2 keeps in a register) is simd.h's plain one here, as it is with Neon,
// where upstream expects no gain from a register for it: ray casts, and the triangles of meshes and height fields.

typedef struct b3AABBV
{
	v128_t lower;
	v128_t upper;
} b3AABBV;

B3_FORCE_INLINE b3AABBV b3LoadAABBV( const b3AABB* aabb )
{
	const float* base = &aabb->lowerBound.x;
	// Same offset as SSE2 and Neon, to read nothing past the box.
	// [lz ux uy uz]
	v128_t v1 = wasm_v128_load( base + 2 );
	b3AABBV result;
	// [lx ly lz ux]
	result.lower = wasm_v128_load( base );
	// [ux uy uz uz]
	result.upper = wasm_i32x4_shuffle( v1, v1, 1, 2, 3, 3 );
	return result;
}

B3_FORCE_INLINE bool b3OverlapAABBV( b3AABBV a, b3AABBV b )
{
	v128_t test = wasm_v128_and( wasm_f32x4_le( a.lower, b.upper ), wasm_f32x4_le( b.lower, a.upper ) );
	return ( wasm_i32x4_bitmask( test ) & 0x7 ) == 0x7;
}

B3_FORCE_INLINE bool b3OverlapNode( b3AABBV av, const b3TreeNode* node )
{
	return b3OverlapAABBV( av, b3LoadAABBV( &node->aabb ) );
}

B3_FORCE_INLINE bool b3OverlapV( const b3AABB* a, const b3AABB* b )
{
	return b3OverlapAABBV( b3LoadAABBV( a ), b3LoadAABBV( b ) );
}

B3_FORCE_INLINE b3AABBV b3UnionAABBV( b3AABBV a, b3AABBV b )
{
	// minps and maxps, as b3MinW4 and b3MaxW4
	b3AABBV result;
	result.lower = wasm_f32x4_pmin( b.lower, a.lower );
	result.upper = wasm_f32x4_pmax( b.upper, a.upper );
	return result;
}

B3_FORCE_INLINE b3AABBV b3UnionPairV( const b3TreeNode* pair )
{
	return b3UnionAABBV( b3LoadAABBV( &pair[0].aabb ), b3LoadAABBV( &pair[1].aabb ) );
}

// Conditionally store an AABB. Avoids a branch.
B3_FORCE_INLINE void b3StoreAABBV( b3AABB* aabb, b3AABBV value, bool condition )
{
	// [lx ly lz ux]
	v128_t raw0 = wasm_i32x4_shuffle( value.lower, value.upper, 0, 1, 2, 4 );
	// [lz ux uy uz]
	v128_t raw1 = wasm_i32x4_shuffle( value.lower, value.upper, 2, 4, 5, 6 );

	float* base = &aabb->lowerBound.x;
	v128_t mask = wasm_i32x4_splat( condition ? -1 : 0 );
	v128_t old0 = wasm_v128_load( base );
	v128_t old1 = wasm_v128_load( base + 2 );
	wasm_v128_store( base, wasm_v128_bitselect( raw0, old0, mask ) );
	wasm_v128_store( base + 2, wasm_v128_bitselect( raw1, old1, mask ) );
}
