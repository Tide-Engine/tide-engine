// Included by the shaders tide unity writes (compiler/src/hlsl.c): fxc's warnings about what the HLSL it translates
// from Tide's WGSL can't help, which Unity would show. Unity passes a standard #pragma on to the compiler only from
// an included file. A loop fxc works out runs once (X3557), as one whose count is a constant may, and a value it
// takes for unset where every way to it sets it (X4000).
#ifndef TIDE_WARNINGS_HLSL
#define TIDE_WARNINGS_HLSL
#if !defined(UNITY_COMPILER_DXC)
#pragma warning(disable : 3557 4000)
#endif
#endif
