// Included by the shaders tide unity writes (compiler/src/hlsl.c): fxc's warnings about what the HLSL it translates
// from Tide's WGSL can't help, which Unity would show. Unity passes a standard #pragma on to the compiler only from
// an included file. A loop fxc works out runs once (X3557), as one whose count is a constant may, and a value it
// takes for unset where every way to it sets it (X4000). In compute, one value every invocation writes to one place in
// storage or a workgroup's memory (X3583): WGSL lets a kernel do that, as WebGPU does.
#ifndef TIDE_WARNINGS_HLSL
#define TIDE_WARNINGS_HLSL
#if !defined(UNITY_COMPILER_DXC)
#pragma warning(disable : 3557 4000 3583)
#endif
#endif
