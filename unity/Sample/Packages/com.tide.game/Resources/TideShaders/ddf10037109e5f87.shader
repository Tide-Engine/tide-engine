// the renderer's mesh_3d
// Made by tide unity from the game's WGSL. Do not edit: it's made again as the game is built.
Shader "Hidden/Tide/ddf10037109e5f87"
{
    Properties
    {
        _TideSrcColor ("", Float) = 1
        _TideDstColor ("", Float) = 10
        _TideSrcAlpha ("", Float) = 1
        _TideDstAlpha ("", Float) = 10
        _TideZTest ("", Float) = 8
        _TideZWrite ("", Float) = 0
        _TideCull ("", Float) = 0
        _TideStencilRef ("", Float) = 0
        _TideStencilComp ("", Float) = 8
        _TideStencilPass ("", Float) = 0
    }
    SubShader
    {
        Pass
        {
            Blend [_TideSrcColor] [_TideDstColor], [_TideSrcAlpha] [_TideDstAlpha]
            ZTest [_TideZTest]
            ZWrite [_TideZWrite]
            Cull [_TideCull]
            Stencil
            {
                Ref [_TideStencilRef]
                Comp [_TideStencilComp]
                Pass [_TideStencilPass]
            }
            HLSLPROGRAM
            #pragma vertex tidehl_vertex
            #pragma fragment tidehl_fragment
            #pragma target 4.5
            #pragma exclude_renderers gles3
            #include "Packages/com.tide.unity/Runtime/Shaders/TideWarnings.hlsl"
// Made by tidec from WGSL (compiler/src/hlsl.c). Do not edit.
#if defined(SHADER_API_D3D11) || defined(SHADER_API_PSSL) || defined(SHADER_API_METAL) || defined(SHADER_API_VULKAN) \
    || defined(SHADER_API_SWITCH) || defined(SHADER_API_XBOXONE) || defined(SHADER_API_GAMECORE) || defined(SHADER_API_WEBGPU)
#define TIDEHL_REVERSED_Z 1 // Unity's depth is 1 near and 0 far
#else
#define TIDEHL_REVERSED_Z 0 // OpenGL's: clip depth from -w near to w far
#endif
#if defined(SHADER_API_GLCORE) || defined(SHADER_API_GLES3) || defined(SHADER_API_GLES) || defined(SHADER_API_WEBGL)
#define TIDEHL_OWN_SAMPLERS 1 // Unity's OpenGL has no samplers apart from textures: each texture's own
#else
#define TIDEHL_OWN_SAMPLERS 0
#endif
#if defined(SHADER_API_GLES3) || defined(SHADER_API_GLES) || defined(SHADER_API_WEBGL)
#define TIDEHL_TEXTURE_BUFFERS 1 // OpenGL ES 3.0's and WebGL 2's: buffers as textures, a word a pixel's bytes, 4,096 a row
#else
#define TIDEHL_TEXTURE_BUFFERS 0
#endif
float4 _ProjectionParams;
float _TideFlip, _TideHeight, _TideLinear;
float4 _TideAttributes[16];
float4 _TideSlots[3];
uint _TideOffset0, _TideOffset1, _TideOffset2, _TideFirst, _TideSampling;
#if TIDEHL_TEXTURE_BUFFERS
Texture2D<float4> _TideVertices0, _TideVertices1, _TideVertices2;
#else
ByteAddressBuffer _TideVertices0, _TideVertices1, _TideVertices2;
#endif
SamplerState tidehl_point_clamp_sampler, tidehl_point_repeat_sampler, tidehl_point_mirror_sampler;
SamplerState tidehl_linear_clamp_sampler, tidehl_linear_repeat_sampler, tidehl_linear_mirror_sampler;
SamplerState tidehl_trilinear_clamp_sampler, tidehl_trilinear_repeat_sampler, tidehl_trilinear_mirror_sampler;

// Which way up the target is: -1 where Unity keeps its rows from the bottom
float tidehl_flip() { return _TideFlip != 0.0 ? sign(_TideFlip) : _ProjectionParams.x; }
float4 tidehl_clip(float4 p) {
    p.y *= tidehl_flip();
#if TIDEHL_REVERSED_Z
    p.z = p.w - p.z;
#else
    p.z = 2.0 * p.z - p.w;
#endif
    return p;
}
float tidehl_depth_in(float d) { return TIDEHL_REVERSED_Z ? 1.0 - d : d; }
float tidehl_depth_out(float d) { return TIDEHL_REVERSED_Z ? 1.0 - d : d; }
float4 tidehl_position(float4 p) {
    if (tidehl_flip() < 0.0) p.y = _TideHeight - p.y;
    p.z = tidehl_depth_in(p.z);
    p.w = 1.0 / p.w;
    return p;
}
bool tidehl_facing(bool front) { return tidehl_flip() < 0.0 ? !front : front; }
// A color into Unity's own target, which takes linear colors where its project is in linear color space
float4 tidehl_output(float4 c) {
    if (_TideLinear > 0.5 && c.a > 0.0) {
        float3 s = saturate(c.rgb / c.a);
        c.rgb = (s <= 0.04045 ? s / 12.92 : pow((s + 0.055) / 1.055, 2.4)) * c.a;
    }
    return c;
}

// Vertices, from their buffers
uint tidehl_vertex_of(uint v) { return _TideFirst + v; } // An indexed draw's first is 0: SV_VertexID is the index
#if TIDEHL_TEXTURE_BUFFERS
uint tidehl_word(Texture2D<float4> b, uint at) {
    uint w = at >> 2;
    uint4 bytes = uint4(round(b.Load(int3(int(w & 4095u), int(w >> 12), 0)) * 255.0));
    return bytes.x | bytes.y << 8 | bytes.z << 16 | bytes.w << 24;
}
uint4 tidehl_words(Texture2D<float4> b, uint at, uint n) {
    uint4 r = uint4(tidehl_word(b, at), 0u, 0u, 0u);
    if (n > 1u) r.y = tidehl_word(b, at + 4u);
    if (n > 2u) r.z = tidehl_word(b, at + 8u);
    if (n > 3u) r.w = tidehl_word(b, at + 12u);
    return r;
}
#else
uint4 tidehl_words(ByteAddressBuffer b, uint at, uint n) {
    uint4 r;
    if (n == 1u) r = uint4(b.Load(at), 0u, 0u, 0u);
    else if (n == 2u) r = uint4(b.Load2(at), 0u, 0u);
    else if (n == 3u) r = uint4(b.Load3(at), 0u);
    else r = b.Load4(at);
    return r;
}
#endif
uint4 tidehl_raw(uint location, uint vertex, uint instance, out uint format) {
    float4 a = _TideAttributes[location];
    uint slot = min((uint)a.x, 2u);
    format = (uint)a.z;
    float4 s = _TideSlots[slot];
    uint at = (s.y > 0.5 ? instance : vertex) * (uint)s.x + (uint)a.y;
    uint n = format == 4u ? 1u : format < 4u ? format + 1u : format - 4u;
    uint4 r;
    if (slot == 0u) r = tidehl_words(_TideVertices0, at + _TideOffset0, n);
    else if (slot == 1u) r = tidehl_words(_TideVertices1, at + _TideOffset1, n);
    else r = tidehl_words(_TideVertices2, at + _TideOffset2, n);
    return r;
}
float4 tidehl_float_attribute(uint location, uint vertex, uint instance) {
    uint format;
    uint4 r = tidehl_raw(location, vertex, instance, format);
    float4 f = asfloat(r);
    if (format == 4u) f = float4(r.x & 255u, (r.x >> 8) & 255u, (r.x >> 16) & 255u, r.x >> 24) / 255.0;
    else if (format == 0u) f = float4(f.x, 0.0, 0.0, 1.0);
    else if (format == 1u) f = float4(f.xy, 0.0, 1.0);
    else if (format == 2u) f = float4(f.xyz, 1.0);
    return f;
}
int4 tidehl_int_attribute(uint location, uint vertex, uint instance) {
    uint format;
    int4 r = asint(tidehl_raw(location, vertex, instance, format));
    if (format == 5u) r = int4(r.x, 0, 0, 1);
    else if (format == 6u) r = int4(r.xy, 0, 1);
    else if (format == 7u) r = int4(r.xyz, 1);
    return r;
}

// Sampling, as `mode` says (_TideSampling): nearest and linear read a texture's own pixels, mipmaps its smaller ones too
#if TIDEHL_OWN_SAMPLERS
#define TIDEHL_SAMPLE(T, C)                                                                            \
float4 tidehl_sample_grad(T t, SamplerState own, uint mode, C at, C dx, C dy) {                        \
    float4 r;                                                                                          \
    if ((mode & 3u) == 2u) r = t.SampleGrad(own, at, dx, dy);                                          \
    else r = t.SampleLevel(own, at, 0.0);                                                              \
    return r;                                                                                          \
}                                                                                                      \
float4 tidehl_sample_level(T t, SamplerState own, uint mode, C at, float level) {                      \
    return t.SampleLevel(own, at, (mode & 3u) == 2u ? level : 0.0);                                    \
}
#else
#define TIDEHL_SAMPLE(T, C)                                                                            \
float4 tidehl_sample_grad(T t, SamplerState own, uint mode, C at, C dx, C dy) {                        \
    uint filter = mode & 3u, wrap = (mode >> 2) & 3u;                                                  \
    float4 r;                                                                                          \
    if (filter == 2u) {                                                                                \
        if (wrap == 1u) r = t.SampleGrad(tidehl_trilinear_repeat_sampler, at, dx, dy);                \
        else if (wrap == 2u) r = t.SampleGrad(tidehl_trilinear_mirror_sampler, at, dx, dy);           \
        else r = t.SampleGrad(tidehl_trilinear_clamp_sampler, at, dx, dy);                            \
    } else if (filter == 1u) {                                                                         \
        if (wrap == 1u) r = t.SampleLevel(tidehl_linear_repeat_sampler, at, 0.0);                     \
        else if (wrap == 2u) r = t.SampleLevel(tidehl_linear_mirror_sampler, at, 0.0);                \
        else r = t.SampleLevel(tidehl_linear_clamp_sampler, at, 0.0);                                 \
    } else {                                                                                           \
        if (wrap == 1u) r = t.SampleLevel(tidehl_point_repeat_sampler, at, 0.0);                      \
        else if (wrap == 2u) r = t.SampleLevel(tidehl_point_mirror_sampler, at, 0.0);                 \
        else r = t.SampleLevel(tidehl_point_clamp_sampler, at, 0.0);                                  \
    }                                                                                                  \
    return r;                                                                                          \
}                                                                                                      \
float4 tidehl_sample_level(T t, SamplerState own, uint mode, C at, float level) {                      \
    uint filter = mode & 3u, wrap = (mode >> 2) & 3u;                                                  \
    float4 r;                                                                                          \
    if (filter != 2u) r = tidehl_sample_grad(t, own, mode, at, (C)0, (C)0);                            \
    else if (wrap == 1u) r = t.SampleLevel(tidehl_trilinear_repeat_sampler, at, level);               \
    else if (wrap == 2u) r = t.SampleLevel(tidehl_trilinear_mirror_sampler, at, level);               \
    else r = t.SampleLevel(tidehl_trilinear_clamp_sampler, at, level);                                \
    return r;                                                                                          \
}
#endif
#define TIDEHL_SAMPLE_DERIVED(T, C)                                                                    \
float4 tidehl_sample(T t, SamplerState own, uint mode, C at) { return tidehl_sample_grad(t, own, mode, at, ddx(at), ddy(at)); } \
float4 tidehl_sample_bias(T t, SamplerState own, uint mode, C at, float bias) {                        \
    float scale = exp2(bias);                                                                          \
    return tidehl_sample_grad(t, own, mode, at, ddx(at) * scale, ddy(at) * scale);                     \
}
TIDEHL_SAMPLE(Texture2D<float4>, float2)
TIDEHL_SAMPLE(TextureCube<float4>, float3)
TIDEHL_SAMPLE_DERIVED(Texture2D<float4>, float2)
TIDEHL_SAMPLE_DERIVED(TextureCube<float4>, float3)

// Depth, 0 near and 1 far whichever way Unity keeps it: the nearest pixel's, or 1 where it isn't nearer than `d`
#if TIDEHL_OWN_SAMPLERS
#define TIDEHL_NEAREST(own) own // A depth texture's own sampler is the nearest pixel's (TideRender.cs)
#else
#define TIDEHL_NEAREST(own) tidehl_point_clamp_sampler
#endif
float tidehl_depth(Texture2D<float> t, SamplerState own, float2 at) { return tidehl_depth_in(t.SampleLevel(TIDEHL_NEAREST(own), at, 0.0)); }
float tidehl_depth(TextureCube<float> t, SamplerState own, float3 at) { return tidehl_depth_in(t.SampleLevel(TIDEHL_NEAREST(own), at, 0.0)); }
float tidehl_compare(Texture2D<float> t, SamplerState own, float2 at, float d) {
    uint w, h;
    t.GetDimensions(w, h);
    float2 size = float2(w, h);
    float2 p = at * size - 0.5;
    float2 f = frac(p);
#if TIDEHL_TEXTURE_BUFFERS
    // OpenGL ES 3.0 gathers nothing: the four pixels, each loaded
    int2 i = int2(floor(p)), most = int2(w, h) - 1;
    float4 g = float4(t.Load(int3(clamp(i + int2(0, 1), 0, most), 0)), t.Load(int3(clamp(i + int2(1, 1), 0, most), 0)),
                      t.Load(int3(clamp(i + int2(1, 0), 0, most), 0)), t.Load(int3(clamp(i, 0, most), 0)));
#else
    float4 g = t.GatherRed(TIDEHL_NEAREST(own), (floor(p) + 1.0) / size);
#endif
#if TIDEHL_REVERSED_Z
    g = 1.0 - g;
#endif
    float4 lit = step(d.xxxx, g); // Gathered as (0, 1), (1, 1), (1, 0), (0, 0)
    return lerp(lerp(lit.w, lit.z, f.x), lerp(lit.x, lit.y, f.x), f.y);
}
float tidehl_compare(TextureCube<float> t, SamplerState own, float3 at, float d) { return d <= tidehl_depth(t, own, at) ? 1.0 : 0.0; }

// Loads and sizes
float4 tidehl_load(Texture2D<float4> t, int2 at, int level) { return t.Load(int3(at, level)); }
int4 tidehl_load(Texture2D<int4> t, int2 at, int level) { return t.Load(int3(at, level)); }
uint4 tidehl_load(Texture2D<uint4> t, int2 at, int level) { return t.Load(int3(at, level)); }
float tidehl_load(Texture2D<float> t, int2 at, int level) { return tidehl_depth_in(t.Load(int3(at, level))); }
float4 tidehl_load(Texture2DMS<float4> t, int2 at, int s) { return t.Load(at, s); }
float tidehl_load(Texture2DMS<float> t, int2 at, int s) { return tidehl_depth_in(t.Load(at, s)); }
#define TIDEHL_SIZE(T)                                                                                 \
uint2 tidehl_size(T t, uint level) { uint w, h, n; t.GetDimensions(level, w, h, n); return uint2(w, h); } \
uint tidehl_levels(T t) { uint w, h, n; t.GetDimensions(0, w, h, n); return n; }
TIDEHL_SIZE(Texture2D<float4>)
TIDEHL_SIZE(Texture2D<int4>)
TIDEHL_SIZE(Texture2D<uint4>)
TIDEHL_SIZE(Texture2D<float>)
TIDEHL_SIZE(TextureCube<float4>)
TIDEHL_SIZE(TextureCube<float>)
#define TIDEHL_SIZE_MS(T)                                                                              \
uint2 tidehl_size(T t) { uint w, h, n; t.GetDimensions(w, h, n); return uint2(w, h); }                \
uint tidehl_samples(T t) { uint w, h, n; t.GetDimensions(w, h, n); return n; }
TIDEHL_SIZE_MS(Texture2DMS<float4>)
TIDEHL_SIZE_MS(Texture2DMS<float>)

// Numbers as WebGPU has them: division and remainder by zero, or the least int by -1, as by 1; floats to ints clamped
#define TIDEHL_INTS(T, U, F)                                                                           \
T tidehl_divisor(T a, T b) { return (b == (T)0) || ((a == (T)(-2147483647 - 1)) && (b == (T)(-1))) ? (T)1 : b; } \
T tidehl_div(T a, T b) {                                                                               \
    T d = tidehl_divisor(a, b);                                                                        \
    U q = asuint(a < (T)0 ? -a : a) / asuint(d < (T)0 ? -d : d);                                       \
    return ((a < (T)0) != (d < (T)0)) ? -asint(q) : asint(q);                                          \
}                                                                                                      \
T tidehl_rem(T a, T b) {                                                                               \
    T d = tidehl_divisor(a, b);                                                                        \
    U r = asuint(a < (T)0 ? -a : a) % asuint(d < (T)0 ? -d : d);                                       \
    return a < (T)0 ? -asint(r) : asint(r);                                                            \
}                                                                                                      \
U tidehl_div(U a, U b) { return a / (b == (U)0 ? (U)1 : b); }                                         \
U tidehl_rem(U a, U b) { return a % (b == (U)0 ? (U)1 : b); }                                         \
T tidehl_f2i(F f) { return (T)clamp(f, -2147483648.0, 2147483520.0); }                               \
U tidehl_f2u(F f) { return (U)clamp(f, 0.0, 4294967040.0); }
TIDEHL_INTS(int, uint, float)
TIDEHL_INTS(int2, uint2, float2)
TIDEHL_INTS(int3, uint3, float3)
TIDEHL_INTS(int4, uint4, float4)
uint tidehl_extract(uint e, uint offset, uint count) {
    uint o = min(offset, 32u), n = min(count, 32u - o);
    return n == 0u ? 0u : (e >> o) & (n == 32u ? 0xFFFFFFFFu : (1u << n) - 1u);
}
int tidehl_extract_signed(int e, uint offset, uint count) {
    uint o = min(offset, 32u), n = min(count, 32u - o);
    return n == 0u ? 0 : asint(asuint(e) << (32u - o - n)) >> (32u - n);
}
uint tidehl_insert(uint e, uint v, uint offset, uint count) {
    uint o = min(offset, 32u), n = min(count, 32u - o);
    if (n == 0u) return e;
    uint mask = (n == 32u ? 0xFFFFFFFFu : (1u << n) - 1u) << o;
    return (e & ~mask) | ((v << o) & mask);
}
#define TIDEHL_HYPERBOLIC(T)                                                                           \
T tidehl_pow(T x, T y) { return exp2(log2(x) * y); }                                                   \
T tidehl_asinh(T v) { return log(v + sqrt(v * v + 1.0)); }                                           \
T tidehl_acosh(T v) { return log(v + sqrt(v * v - 1.0)); }                                           \
T tidehl_atanh(T v) { return 0.5 * log((1.0 + v) / (1.0 - v)); }                                     \
T tidehl_fwidth_fine(T v) { return abs(ddx_fine(v)) + abs(ddy_fine(v)); }                             \
T tidehl_fwidth_coarse(T v) { return abs(ddx_coarse(v)) + abs(ddy_coarse(v)); }
TIDEHL_HYPERBOLIC(float)
TIDEHL_HYPERBOLIC(float2)
TIDEHL_HYPERBOLIC(float3)
TIDEHL_HYPERBOLIC(float4)
uint tidehl_pack4x8unorm(float4 v) { uint4 b = (uint4)round(saturate(v) * 255.0); return b.x | b.y << 8 | b.z << 16 | b.w << 24; }
uint tidehl_pack4x8snorm(float4 v) { uint4 b = asuint((int4)round(clamp(v, -1.0, 1.0) * 127.0)) & 255u; return b.x | b.y << 8 | b.z << 16 | b.w << 24; }
uint tidehl_pack2x16unorm(float2 v) { uint2 b = (uint2)round(saturate(v) * 65535.0); return b.x | b.y << 16; }
uint tidehl_pack2x16snorm(float2 v) { uint2 b = asuint((int2)round(clamp(v, -1.0, 1.0) * 32767.0)) & 0xFFFFu; return b.x | b.y << 16; }
uint tidehl_pack2x16float(float2 v) { return f32tof16(v.x) | f32tof16(v.y) << 16; }
float4 tidehl_unpack4x8unorm(uint u) { return float4(u & 255u, (u >> 8) & 255u, (u >> 16) & 255u, u >> 24) / 255.0; }
float4 tidehl_unpack4x8snorm(uint u) { int4 b = asint(uint4(u << 24, u << 16, u << 8, u)) >> 24; return max(float4(b) / 127.0, -1.0); }
float2 tidehl_unpack2x16unorm(uint u) { return float2(u & 0xFFFFu, u >> 16) / 65535.0; }
float2 tidehl_unpack2x16snorm(uint u) { int2 b = asint(uint2(u << 16, u)) >> 16; return max(float2(b) / 32767.0, -1.0); }
float2 tidehl_unpack2x16float(uint u) { return float2(f16tof32(u & 0xFFFFu), f16tof32(u >> 16)); }

struct Uniforms {
    float4x4 camera;
    float4 tint;
    float4 fix;
};
struct Corner {
    float4 position;
    float2 at;
    float4 tint;
    float white;
};

cbuffer TideUniforms {
    row_major float4x4 tidehl_u_camera;
    float4 tidehl_u_tint;
    float4 tidehl_u_fix;
};
Texture2D<float4> _TideTexture0;
SamplerState sampler_TideTexture0;

float4 whiten(float4 c, float by);
float4 over(float4 p, float4 tint, float by);
Corner vertex(float3 position, float2 uv, float4 color, float4 model0, float4 model1, float4 model2, float4 model3);
float4 fragment(Corner in_w);

float4 whiten(float4 c, float by)
{
    return float4(((c.rgb * c.a) + (by * (1.0 - c.a))), c.a);
}

float4 over(float4 p, float4 tint, float by)
{
    float4 c = (p * float4((tint.rgb * tint.a), tint.a));
    return float4((c.rgb + (by * (1.0 - c.a))), c.a);
}

Corner vertex(float3 position, float2 uv, float4 color, float4 model0, float4 model1, float4 model2, float4 model3)
{
    float4 p = mul(mul(float4(position, 1.0), float4x4(model0, model1, model2, model3)), tidehl_u_camera);
    Corner out_w = ((Corner)0);
    out_w.at = uv;
    out_w.tint = (color * tidehl_u_tint);
    out_w.white = tidehl_u_fix.x;
    out_w.position = float4(p.xy, ((p.z + p.w) * 0.5), p.w);
    return out_w;
}

float4 fragment(Corner in_w)
{
    return over(tidehl_sample(_TideTexture0, sampler_TideTexture0, ((_TideSampling >> 0u) & 15u), in_w.at), in_w.tint, in_w.white);
}

struct tidehl_varyings {
    float4 position : SV_Position;
    float2 l0 : TEXCOORD0;
    float4 l1 : TEXCOORD1;
    float l2 : TEXCOORD2;
};

tidehl_varyings tidehl_vertex(uint tidehl_v : SV_VertexID, uint tidehl_instance : SV_InstanceID) {
    uint tidehl_vertex = tidehl_vertex_of(tidehl_v);
    float3 tidehl_p0;
    tidehl_p0 = tidehl_float_attribute(0u, tidehl_vertex, tidehl_instance).xyz;
    float2 tidehl_p1;
    tidehl_p1 = tidehl_float_attribute(1u, tidehl_vertex, tidehl_instance).xy;
    float4 tidehl_p2;
    tidehl_p2 = tidehl_float_attribute(2u, tidehl_vertex, tidehl_instance);
    float4 tidehl_p3;
    tidehl_p3 = tidehl_float_attribute(3u, tidehl_vertex, tidehl_instance);
    float4 tidehl_p4;
    tidehl_p4 = tidehl_float_attribute(4u, tidehl_vertex, tidehl_instance);
    float4 tidehl_p5;
    tidehl_p5 = tidehl_float_attribute(5u, tidehl_vertex, tidehl_instance);
    float4 tidehl_p6;
    tidehl_p6 = tidehl_float_attribute(6u, tidehl_vertex, tidehl_instance);
    Corner tidehl_r = vertex(tidehl_p0, tidehl_p1, tidehl_p2, tidehl_p3, tidehl_p4, tidehl_p5, tidehl_p6);
    tidehl_varyings tidehl_o;
    tidehl_o.position = tidehl_clip(tidehl_r.position);
    tidehl_o.l0 = tidehl_r.at;
    tidehl_o.l1 = tidehl_r.tint;
    tidehl_o.l2 = tidehl_r.white;
    return tidehl_o;
}

struct tidehl_targets {
    float4 c0 : SV_Target0;
};

tidehl_targets tidehl_fragment(tidehl_varyings tidehl_i, bool tidehl_front : SV_IsFrontFace) {
    Corner tidehl_p0 = ((Corner)0);
    tidehl_p0.position = tidehl_position(tidehl_i.position);
    tidehl_p0.at = tidehl_i.l0;
    tidehl_p0.tint = tidehl_i.l1;
    tidehl_p0.white = tidehl_i.l2;
    float4 tidehl_r = fragment(tidehl_p0);
    tidehl_targets tidehl_o;
    tidehl_o.c0 = tidehl_output(tidehl_r);
    return tidehl_o;
}
            ENDHLSL
        }
    }
    SubShader
    {
        Pass
        {
            Blend [_TideSrcColor] [_TideDstColor], [_TideSrcAlpha] [_TideDstAlpha]
            ZTest [_TideZTest]
            ZWrite [_TideZWrite]
            Cull [_TideCull]
            Stencil
            {
                Ref [_TideStencilRef]
                Comp [_TideStencilComp]
                Pass [_TideStencilPass]
            }
            HLSLPROGRAM
            #pragma vertex tidehl_vertex
            #pragma fragment tidehl_fragment
            #pragma target 3.5
            #pragma only_renderers gles3
            #include "Packages/com.tide.unity/Runtime/Shaders/TideWarnings.hlsl"
// Made by tidec from WGSL (compiler/src/hlsl.c). Do not edit.
#if defined(SHADER_API_D3D11) || defined(SHADER_API_PSSL) || defined(SHADER_API_METAL) || defined(SHADER_API_VULKAN) \
    || defined(SHADER_API_SWITCH) || defined(SHADER_API_XBOXONE) || defined(SHADER_API_GAMECORE) || defined(SHADER_API_WEBGPU)
#define TIDEHL_REVERSED_Z 1 // Unity's depth is 1 near and 0 far
#else
#define TIDEHL_REVERSED_Z 0 // OpenGL's: clip depth from -w near to w far
#endif
#if defined(SHADER_API_GLCORE) || defined(SHADER_API_GLES3) || defined(SHADER_API_GLES) || defined(SHADER_API_WEBGL)
#define TIDEHL_OWN_SAMPLERS 1 // Unity's OpenGL has no samplers apart from textures: each texture's own
#else
#define TIDEHL_OWN_SAMPLERS 0
#endif
#if defined(SHADER_API_GLES3) || defined(SHADER_API_GLES) || defined(SHADER_API_WEBGL)
#define TIDEHL_TEXTURE_BUFFERS 1 // OpenGL ES 3.0's and WebGL 2's: buffers as textures, a word a pixel's bytes, 4,096 a row
#else
#define TIDEHL_TEXTURE_BUFFERS 0
#endif
float4 _ProjectionParams;
float _TideFlip, _TideHeight, _TideLinear;
float4 _TideAttributes[16];
float4 _TideSlots[3];
uint _TideOffset0, _TideOffset1, _TideOffset2, _TideFirst, _TideSampling;
#if TIDEHL_TEXTURE_BUFFERS
Texture2D<float4> _TideVertices0, _TideVertices1, _TideVertices2;
#else
ByteAddressBuffer _TideVertices0, _TideVertices1, _TideVertices2;
#endif
SamplerState tidehl_point_clamp_sampler, tidehl_point_repeat_sampler, tidehl_point_mirror_sampler;
SamplerState tidehl_linear_clamp_sampler, tidehl_linear_repeat_sampler, tidehl_linear_mirror_sampler;
SamplerState tidehl_trilinear_clamp_sampler, tidehl_trilinear_repeat_sampler, tidehl_trilinear_mirror_sampler;

// Which way up the target is: -1 where Unity keeps its rows from the bottom
float tidehl_flip() { return _TideFlip != 0.0 ? sign(_TideFlip) : _ProjectionParams.x; }
float4 tidehl_clip(float4 p) {
    p.y *= tidehl_flip();
#if TIDEHL_REVERSED_Z
    p.z = p.w - p.z;
#else
    p.z = 2.0 * p.z - p.w;
#endif
    return p;
}
float tidehl_depth_in(float d) { return TIDEHL_REVERSED_Z ? 1.0 - d : d; }
float tidehl_depth_out(float d) { return TIDEHL_REVERSED_Z ? 1.0 - d : d; }
float4 tidehl_position(float4 p) {
    if (tidehl_flip() < 0.0) p.y = _TideHeight - p.y;
    p.z = tidehl_depth_in(p.z);
    p.w = 1.0 / p.w;
    return p;
}
bool tidehl_facing(bool front) { return tidehl_flip() < 0.0 ? !front : front; }
// A color into Unity's own target, which takes linear colors where its project is in linear color space
float4 tidehl_output(float4 c) {
    if (_TideLinear > 0.5 && c.a > 0.0) {
        float3 s = saturate(c.rgb / c.a);
        c.rgb = (s <= 0.04045 ? s / 12.92 : pow((s + 0.055) / 1.055, 2.4)) * c.a;
    }
    return c;
}

// Vertices, from their buffers
uint tidehl_vertex_of(uint v) { return _TideFirst + v; } // An indexed draw's first is 0: SV_VertexID is the index
#if TIDEHL_TEXTURE_BUFFERS
uint tidehl_word(Texture2D<float4> b, uint at) {
    uint w = at >> 2;
    uint4 bytes = uint4(round(b.Load(int3(int(w & 4095u), int(w >> 12), 0)) * 255.0));
    return bytes.x | bytes.y << 8 | bytes.z << 16 | bytes.w << 24;
}
uint4 tidehl_words(Texture2D<float4> b, uint at, uint n) {
    uint4 r = uint4(tidehl_word(b, at), 0u, 0u, 0u);
    if (n > 1u) r.y = tidehl_word(b, at + 4u);
    if (n > 2u) r.z = tidehl_word(b, at + 8u);
    if (n > 3u) r.w = tidehl_word(b, at + 12u);
    return r;
}
#else
uint4 tidehl_words(ByteAddressBuffer b, uint at, uint n) {
    uint4 r;
    if (n == 1u) r = uint4(b.Load(at), 0u, 0u, 0u);
    else if (n == 2u) r = uint4(b.Load2(at), 0u, 0u);
    else if (n == 3u) r = uint4(b.Load3(at), 0u);
    else r = b.Load4(at);
    return r;
}
#endif
uint4 tidehl_raw(uint location, uint vertex, uint instance, out uint format) {
    float4 a = _TideAttributes[location];
    uint slot = min((uint)a.x, 2u);
    format = (uint)a.z;
    float4 s = _TideSlots[slot];
    uint at = (s.y > 0.5 ? instance : vertex) * (uint)s.x + (uint)a.y;
    uint n = format == 4u ? 1u : format < 4u ? format + 1u : format - 4u;
    uint4 r;
    if (slot == 0u) r = tidehl_words(_TideVertices0, at + _TideOffset0, n);
    else if (slot == 1u) r = tidehl_words(_TideVertices1, at + _TideOffset1, n);
    else r = tidehl_words(_TideVertices2, at + _TideOffset2, n);
    return r;
}
float4 tidehl_float_attribute(uint location, uint vertex, uint instance) {
    uint format;
    uint4 r = tidehl_raw(location, vertex, instance, format);
    float4 f = asfloat(r);
    if (format == 4u) f = float4(r.x & 255u, (r.x >> 8) & 255u, (r.x >> 16) & 255u, r.x >> 24) / 255.0;
    else if (format == 0u) f = float4(f.x, 0.0, 0.0, 1.0);
    else if (format == 1u) f = float4(f.xy, 0.0, 1.0);
    else if (format == 2u) f = float4(f.xyz, 1.0);
    return f;
}
int4 tidehl_int_attribute(uint location, uint vertex, uint instance) {
    uint format;
    int4 r = asint(tidehl_raw(location, vertex, instance, format));
    if (format == 5u) r = int4(r.x, 0, 0, 1);
    else if (format == 6u) r = int4(r.xy, 0, 1);
    else if (format == 7u) r = int4(r.xyz, 1);
    return r;
}

// Sampling, as `mode` says (_TideSampling): nearest and linear read a texture's own pixels, mipmaps its smaller ones too
#if TIDEHL_OWN_SAMPLERS
#define TIDEHL_SAMPLE(T, C)                                                                            \
float4 tidehl_sample_grad(T t, SamplerState own, uint mode, C at, C dx, C dy) {                        \
    float4 r;                                                                                          \
    if ((mode & 3u) == 2u) r = t.SampleGrad(own, at, dx, dy);                                          \
    else r = t.SampleLevel(own, at, 0.0);                                                              \
    return r;                                                                                          \
}                                                                                                      \
float4 tidehl_sample_level(T t, SamplerState own, uint mode, C at, float level) {                      \
    return t.SampleLevel(own, at, (mode & 3u) == 2u ? level : 0.0);                                    \
}
#else
#define TIDEHL_SAMPLE(T, C)                                                                            \
float4 tidehl_sample_grad(T t, SamplerState own, uint mode, C at, C dx, C dy) {                        \
    uint filter = mode & 3u, wrap = (mode >> 2) & 3u;                                                  \
    float4 r;                                                                                          \
    if (filter == 2u) {                                                                                \
        if (wrap == 1u) r = t.SampleGrad(tidehl_trilinear_repeat_sampler, at, dx, dy);                \
        else if (wrap == 2u) r = t.SampleGrad(tidehl_trilinear_mirror_sampler, at, dx, dy);           \
        else r = t.SampleGrad(tidehl_trilinear_clamp_sampler, at, dx, dy);                            \
    } else if (filter == 1u) {                                                                         \
        if (wrap == 1u) r = t.SampleLevel(tidehl_linear_repeat_sampler, at, 0.0);                     \
        else if (wrap == 2u) r = t.SampleLevel(tidehl_linear_mirror_sampler, at, 0.0);                \
        else r = t.SampleLevel(tidehl_linear_clamp_sampler, at, 0.0);                                 \
    } else {                                                                                           \
        if (wrap == 1u) r = t.SampleLevel(tidehl_point_repeat_sampler, at, 0.0);                      \
        else if (wrap == 2u) r = t.SampleLevel(tidehl_point_mirror_sampler, at, 0.0);                 \
        else r = t.SampleLevel(tidehl_point_clamp_sampler, at, 0.0);                                  \
    }                                                                                                  \
    return r;                                                                                          \
}                                                                                                      \
float4 tidehl_sample_level(T t, SamplerState own, uint mode, C at, float level) {                      \
    uint filter = mode & 3u, wrap = (mode >> 2) & 3u;                                                  \
    float4 r;                                                                                          \
    if (filter != 2u) r = tidehl_sample_grad(t, own, mode, at, (C)0, (C)0);                            \
    else if (wrap == 1u) r = t.SampleLevel(tidehl_trilinear_repeat_sampler, at, level);               \
    else if (wrap == 2u) r = t.SampleLevel(tidehl_trilinear_mirror_sampler, at, level);               \
    else r = t.SampleLevel(tidehl_trilinear_clamp_sampler, at, level);                                \
    return r;                                                                                          \
}
#endif
#define TIDEHL_SAMPLE_DERIVED(T, C)                                                                    \
float4 tidehl_sample(T t, SamplerState own, uint mode, C at) { return tidehl_sample_grad(t, own, mode, at, ddx(at), ddy(at)); } \
float4 tidehl_sample_bias(T t, SamplerState own, uint mode, C at, float bias) {                        \
    float scale = exp2(bias);                                                                          \
    return tidehl_sample_grad(t, own, mode, at, ddx(at) * scale, ddy(at) * scale);                     \
}
TIDEHL_SAMPLE(Texture2D<float4>, float2)
TIDEHL_SAMPLE(TextureCube<float4>, float3)
TIDEHL_SAMPLE_DERIVED(Texture2D<float4>, float2)
TIDEHL_SAMPLE_DERIVED(TextureCube<float4>, float3)

// Depth, 0 near and 1 far whichever way Unity keeps it: the nearest pixel's, or 1 where it isn't nearer than `d`
#if TIDEHL_OWN_SAMPLERS
#define TIDEHL_NEAREST(own) own // A depth texture's own sampler is the nearest pixel's (TideRender.cs)
#else
#define TIDEHL_NEAREST(own) tidehl_point_clamp_sampler
#endif
float tidehl_depth(Texture2D<float> t, SamplerState own, float2 at) { return tidehl_depth_in(t.SampleLevel(TIDEHL_NEAREST(own), at, 0.0)); }
float tidehl_depth(TextureCube<float> t, SamplerState own, float3 at) { return tidehl_depth_in(t.SampleLevel(TIDEHL_NEAREST(own), at, 0.0)); }
float tidehl_compare(Texture2D<float> t, SamplerState own, float2 at, float d) {
    uint w, h;
    t.GetDimensions(w, h);
    float2 size = float2(w, h);
    float2 p = at * size - 0.5;
    float2 f = frac(p);
#if TIDEHL_TEXTURE_BUFFERS
    // OpenGL ES 3.0 gathers nothing: the four pixels, each loaded
    int2 i = int2(floor(p)), most = int2(w, h) - 1;
    float4 g = float4(t.Load(int3(clamp(i + int2(0, 1), 0, most), 0)), t.Load(int3(clamp(i + int2(1, 1), 0, most), 0)),
                      t.Load(int3(clamp(i + int2(1, 0), 0, most), 0)), t.Load(int3(clamp(i, 0, most), 0)));
#else
    float4 g = t.GatherRed(TIDEHL_NEAREST(own), (floor(p) + 1.0) / size);
#endif
#if TIDEHL_REVERSED_Z
    g = 1.0 - g;
#endif
    float4 lit = step(d.xxxx, g); // Gathered as (0, 1), (1, 1), (1, 0), (0, 0)
    return lerp(lerp(lit.w, lit.z, f.x), lerp(lit.x, lit.y, f.x), f.y);
}
float tidehl_compare(TextureCube<float> t, SamplerState own, float3 at, float d) { return d <= tidehl_depth(t, own, at) ? 1.0 : 0.0; }

// Loads and sizes
float4 tidehl_load(Texture2D<float4> t, int2 at, int level) { return t.Load(int3(at, level)); }
int4 tidehl_load(Texture2D<int4> t, int2 at, int level) { return t.Load(int3(at, level)); }
uint4 tidehl_load(Texture2D<uint4> t, int2 at, int level) { return t.Load(int3(at, level)); }
float tidehl_load(Texture2D<float> t, int2 at, int level) { return tidehl_depth_in(t.Load(int3(at, level))); }
float4 tidehl_load(Texture2DMS<float4> t, int2 at, int s) { return t.Load(at, s); }
float tidehl_load(Texture2DMS<float> t, int2 at, int s) { return tidehl_depth_in(t.Load(at, s)); }
#define TIDEHL_SIZE(T)                                                                                 \
uint2 tidehl_size(T t, uint level) { uint w, h, n; t.GetDimensions(level, w, h, n); return uint2(w, h); } \
uint tidehl_levels(T t) { uint w, h, n; t.GetDimensions(0, w, h, n); return n; }
TIDEHL_SIZE(Texture2D<float4>)
TIDEHL_SIZE(Texture2D<int4>)
TIDEHL_SIZE(Texture2D<uint4>)
TIDEHL_SIZE(Texture2D<float>)
TIDEHL_SIZE(TextureCube<float4>)
TIDEHL_SIZE(TextureCube<float>)
#define TIDEHL_SIZE_MS(T)                                                                              \
uint2 tidehl_size(T t) { uint w, h, n; t.GetDimensions(w, h, n); return uint2(w, h); }                \
uint tidehl_samples(T t) { uint w, h, n; t.GetDimensions(w, h, n); return n; }
TIDEHL_SIZE_MS(Texture2DMS<float4>)
TIDEHL_SIZE_MS(Texture2DMS<float>)

// Numbers as WebGPU has them: division and remainder by zero, or the least int by -1, as by 1; floats to ints clamped
#define TIDEHL_INTS(T, U, F)                                                                           \
T tidehl_divisor(T a, T b) { return (b == (T)0) || ((a == (T)(-2147483647 - 1)) && (b == (T)(-1))) ? (T)1 : b; } \
T tidehl_div(T a, T b) {                                                                               \
    T d = tidehl_divisor(a, b);                                                                        \
    U q = asuint(a < (T)0 ? -a : a) / asuint(d < (T)0 ? -d : d);                                       \
    return ((a < (T)0) != (d < (T)0)) ? -asint(q) : asint(q);                                          \
}                                                                                                      \
T tidehl_rem(T a, T b) {                                                                               \
    T d = tidehl_divisor(a, b);                                                                        \
    U r = asuint(a < (T)0 ? -a : a) % asuint(d < (T)0 ? -d : d);                                       \
    return a < (T)0 ? -asint(r) : asint(r);                                                            \
}                                                                                                      \
U tidehl_div(U a, U b) { return a / (b == (U)0 ? (U)1 : b); }                                         \
U tidehl_rem(U a, U b) { return a % (b == (U)0 ? (U)1 : b); }                                         \
T tidehl_f2i(F f) { return (T)clamp(f, -2147483648.0, 2147483520.0); }                               \
U tidehl_f2u(F f) { return (U)clamp(f, 0.0, 4294967040.0); }
TIDEHL_INTS(int, uint, float)
TIDEHL_INTS(int2, uint2, float2)
TIDEHL_INTS(int3, uint3, float3)
TIDEHL_INTS(int4, uint4, float4)
uint tidehl_extract(uint e, uint offset, uint count) {
    uint o = min(offset, 32u), n = min(count, 32u - o);
    return n == 0u ? 0u : (e >> o) & (n == 32u ? 0xFFFFFFFFu : (1u << n) - 1u);
}
int tidehl_extract_signed(int e, uint offset, uint count) {
    uint o = min(offset, 32u), n = min(count, 32u - o);
    return n == 0u ? 0 : asint(asuint(e) << (32u - o - n)) >> (32u - n);
}
uint tidehl_insert(uint e, uint v, uint offset, uint count) {
    uint o = min(offset, 32u), n = min(count, 32u - o);
    if (n == 0u) return e;
    uint mask = (n == 32u ? 0xFFFFFFFFu : (1u << n) - 1u) << o;
    return (e & ~mask) | ((v << o) & mask);
}
#define TIDEHL_HYPERBOLIC(T)                                                                           \
T tidehl_pow(T x, T y) { return exp2(log2(x) * y); }                                                   \
T tidehl_asinh(T v) { return log(v + sqrt(v * v + 1.0)); }                                           \
T tidehl_acosh(T v) { return log(v + sqrt(v * v - 1.0)); }                                           \
T tidehl_atanh(T v) { return 0.5 * log((1.0 + v) / (1.0 - v)); }                                     \
T tidehl_fwidth_fine(T v) { return abs(ddx_fine(v)) + abs(ddy_fine(v)); }                             \
T tidehl_fwidth_coarse(T v) { return abs(ddx_coarse(v)) + abs(ddy_coarse(v)); }
TIDEHL_HYPERBOLIC(float)
TIDEHL_HYPERBOLIC(float2)
TIDEHL_HYPERBOLIC(float3)
TIDEHL_HYPERBOLIC(float4)
uint tidehl_pack4x8unorm(float4 v) { uint4 b = (uint4)round(saturate(v) * 255.0); return b.x | b.y << 8 | b.z << 16 | b.w << 24; }
uint tidehl_pack4x8snorm(float4 v) { uint4 b = asuint((int4)round(clamp(v, -1.0, 1.0) * 127.0)) & 255u; return b.x | b.y << 8 | b.z << 16 | b.w << 24; }
uint tidehl_pack2x16unorm(float2 v) { uint2 b = (uint2)round(saturate(v) * 65535.0); return b.x | b.y << 16; }
uint tidehl_pack2x16snorm(float2 v) { uint2 b = asuint((int2)round(clamp(v, -1.0, 1.0) * 32767.0)) & 0xFFFFu; return b.x | b.y << 16; }
uint tidehl_pack2x16float(float2 v) { return f32tof16(v.x) | f32tof16(v.y) << 16; }
float4 tidehl_unpack4x8unorm(uint u) { return float4(u & 255u, (u >> 8) & 255u, (u >> 16) & 255u, u >> 24) / 255.0; }
float4 tidehl_unpack4x8snorm(uint u) { int4 b = asint(uint4(u << 24, u << 16, u << 8, u)) >> 24; return max(float4(b) / 127.0, -1.0); }
float2 tidehl_unpack2x16unorm(uint u) { return float2(u & 0xFFFFu, u >> 16) / 65535.0; }
float2 tidehl_unpack2x16snorm(uint u) { int2 b = asint(uint2(u << 16, u)) >> 16; return max(float2(b) / 32767.0, -1.0); }
float2 tidehl_unpack2x16float(uint u) { return float2(f16tof32(u & 0xFFFFu), f16tof32(u >> 16)); }

struct Uniforms {
    float4x4 camera;
    float4 tint;
    float4 fix;
};
struct Corner {
    float4 position;
    float2 at;
    float4 tint;
    float white;
};

cbuffer TideUniforms {
    row_major float4x4 tidehl_u_camera;
    float4 tidehl_u_tint;
    float4 tidehl_u_fix;
};
Texture2D<float4> _TideTexture0;
SamplerState sampler_TideTexture0;

float4 whiten(float4 c, float by);
float4 over(float4 p, float4 tint, float by);
Corner vertex(float3 position, float2 uv, float4 color, float4 model0, float4 model1, float4 model2, float4 model3);
float4 fragment(Corner in_w);

float4 whiten(float4 c, float by)
{
    return float4(((c.rgb * c.a) + (by * (1.0 - c.a))), c.a);
}

float4 over(float4 p, float4 tint, float by)
{
    float4 c = (p * float4((tint.rgb * tint.a), tint.a));
    return float4((c.rgb + (by * (1.0 - c.a))), c.a);
}

Corner vertex(float3 position, float2 uv, float4 color, float4 model0, float4 model1, float4 model2, float4 model3)
{
    float4 p = mul(mul(float4(position, 1.0), float4x4(model0, model1, model2, model3)), tidehl_u_camera);
    Corner out_w = ((Corner)0);
    out_w.at = uv;
    out_w.tint = (color * tidehl_u_tint);
    out_w.white = tidehl_u_fix.x;
    out_w.position = float4(p.xy, ((p.z + p.w) * 0.5), p.w);
    return out_w;
}

float4 fragment(Corner in_w)
{
    return over(tidehl_sample(_TideTexture0, sampler_TideTexture0, ((_TideSampling >> 0u) & 15u), in_w.at), in_w.tint, in_w.white);
}

struct tidehl_varyings {
    float4 position : SV_Position;
    float2 l0 : TEXCOORD0;
    float4 l1 : TEXCOORD1;
    float l2 : TEXCOORD2;
};

tidehl_varyings tidehl_vertex(uint tidehl_v : SV_VertexID, uint tidehl_instance : SV_InstanceID) {
    uint tidehl_vertex = tidehl_vertex_of(tidehl_v);
    float3 tidehl_p0;
    tidehl_p0 = tidehl_float_attribute(0u, tidehl_vertex, tidehl_instance).xyz;
    float2 tidehl_p1;
    tidehl_p1 = tidehl_float_attribute(1u, tidehl_vertex, tidehl_instance).xy;
    float4 tidehl_p2;
    tidehl_p2 = tidehl_float_attribute(2u, tidehl_vertex, tidehl_instance);
    float4 tidehl_p3;
    tidehl_p3 = tidehl_float_attribute(3u, tidehl_vertex, tidehl_instance);
    float4 tidehl_p4;
    tidehl_p4 = tidehl_float_attribute(4u, tidehl_vertex, tidehl_instance);
    float4 tidehl_p5;
    tidehl_p5 = tidehl_float_attribute(5u, tidehl_vertex, tidehl_instance);
    float4 tidehl_p6;
    tidehl_p6 = tidehl_float_attribute(6u, tidehl_vertex, tidehl_instance);
    Corner tidehl_r = vertex(tidehl_p0, tidehl_p1, tidehl_p2, tidehl_p3, tidehl_p4, tidehl_p5, tidehl_p6);
    tidehl_varyings tidehl_o;
    tidehl_o.position = tidehl_clip(tidehl_r.position);
    tidehl_o.l0 = tidehl_r.at;
    tidehl_o.l1 = tidehl_r.tint;
    tidehl_o.l2 = tidehl_r.white;
    return tidehl_o;
}

struct tidehl_targets {
    float4 c0 : SV_Target0;
};

tidehl_targets tidehl_fragment(tidehl_varyings tidehl_i, bool tidehl_front : SV_IsFrontFace) {
    Corner tidehl_p0 = ((Corner)0);
    tidehl_p0.position = tidehl_position(tidehl_i.position);
    tidehl_p0.at = tidehl_i.l0;
    tidehl_p0.tint = tidehl_i.l1;
    tidehl_p0.white = tidehl_i.l2;
    float4 tidehl_r = fragment(tidehl_p0);
    tidehl_targets tidehl_o;
    tidehl_o.c0 = tidehl_output(tidehl_r);
    return tidehl_o;
}
            ENDHLSL
        }
    }
}
