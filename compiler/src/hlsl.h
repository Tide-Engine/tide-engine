#pragma once

// WGSL to HLSL, for Unity (tide unity): what tidec writes for a game's frags
// and verts, and the renderer's own shaders, as one HLSL program that Unity
// compiles for every graphics API it has. Its entry points are
// `tidehl_vertex` and `tidehl_fragment`, around the WGSL's `vertex` and
// `fragment`, and it reads what gpu.h's pipeline and draw say through names
// of its own, which Unity's replay of the draws (com.tide.unity's
// TideRender.cs) sets:
//
// - Vertices aren't vertex buffers: each @location is read from the
//   ByteAddressBuffers `_TideVertices0` to `_TideVertices2`, by the pipeline's
//   layout (`_TideAttributes[16]`, each a float4: slot, offset, format;
//   `_TideSlots[3]`, each stride and 1 for one an instance) and the draw's
//   (`_TideOffset0` to `_TideOffset2`, the bytes each slot starts at; the
//   uints `_TideFirst`, the first vertex or index, and `_TideIndexed`, 0 for
//   none, 1 for 16 bits and 2 for 32 bits each, read from `_TideIndices`).
// - The uniform block is the cbuffer `TideUniforms`, its bytes as WGSL lays
//   them out. Texture i is `_TideTexture<i>`; its sampler is how
//   `_TideSampling`'s 4 bits from 4i say (2 bits of tide_gpu_filter, then 2
//   of tide_gpu_wrap), through Unity's inline sampler states. A depth texture
//   reads and compares as gpu.h says, whichever way Unity keeps depth.
// - Clip space is gpu.h's (OpenGL's, y up) until the vertex stage's end,
//   which turns it Unity's: y times `_TideFlip` (its sign; 0 for
//   `_ProjectionParams.x`, the camera's), depth reversed where Unity's is.
//   A fragment's position is in the target's pixels from its top
//   (`_TideHeight` high), depth from 0 near.
//
// Matrices are WGSL's transposed: a WGSL column is an HLSL row, so `m[i]` is
// the same column in both and `a * b` is `mul(b, a)`. Arrays are structs of
// one (`tidehl_a<n>`), which functions return.

#include <stddef.h>

// The program, malloc'd (the caller frees it); or NULL, having written why
// into `error` (line and column of the WGSL, then what's wrong), for WGSL it
// doesn't take.
char *tide_wgsl_to_hlsl(const char *wgsl, char *error, size_t error_size);

// The Unity shader around such a program: `name` is its path in Unity's
// list ("Hidden/Tide/..."), and its render state is its material's
// properties (_TideSrcColor, _TideDstColor, _TideSrcAlpha, _TideDstAlpha,
// _TideZTest, _TideZWrite, _TideCull, _TideStencilRef, _TideStencilComp,
// _TideStencilPass), which TideRender.cs sets from a pipeline's. Malloc'd.
char *tide_hlsl_unity_shader(const char *hlsl, const char *name);
