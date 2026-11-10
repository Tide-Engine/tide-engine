// the renderer's solid_shape
// Made by tide unity from the game's WGSL. Do not edit: it's made again as the game is built.
Shader "Hidden/Tide/4fc08dbcca39a264"
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
        _TideAlphaToMask ("", Float) = 0
    }
    SubShader
    {
        Pass
        {
            Blend [_TideSrcColor] [_TideDstColor], [_TideSrcAlpha] [_TideDstAlpha]
            ZTest [_TideZTest]
            ZWrite [_TideZWrite]
            Cull [_TideCull]
            AlphaToMask [_TideAlphaToMask]
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
#if defined(SHADER_API_GLCORE)
#define TIDEHL_OWN_SAMPLERS 1 // Unity's OpenGL Core has no samplers apart from textures: each texture's own
#else
#define TIDEHL_OWN_SAMPLERS 0
#endif
float4 _ProjectionParams;
float _TideFlip, _TideHeight, _TideLinear;
float4 _TideAttributes[16];
float4 _TideSlots[3];
uint _TideOffset0, _TideOffset1, _TideOffset2, _TideFirst, _TideSampling, _TideSamplingHigh, _TideUnmipped;
uint _TideOwn, _TideOwnHigh; // OpenGL Core's: how each texture samples itself, 4 bits each, 15 for unknown
ByteAddressBuffer _TideVertices0, _TideVertices1, _TideVertices2;
SamplerState tidehl_point_clamp_sampler, tidehl_point_repeat_sampler, tidehl_point_mirror_sampler;
SamplerState tidehl_linear_clamp_sampler, tidehl_linear_repeat_sampler, tidehl_linear_mirror_sampler;
SamplerState tidehl_trilinear_clamp_sampler, tidehl_trilinear_repeat_sampler, tidehl_trilinear_mirror_sampler;
// Filter.Anisotropic's: Unity takes how many reads along what a pixel covers from the name (gpu.h's TIDE_GPU_ANISOTROPY)
SamplerState tidehl_trilinear_clamp_aniso8_sampler, tidehl_trilinear_repeat_aniso8_sampler, tidehl_trilinear_mirror_aniso8_sampler;

// How sampler bits `mode` read texture `t`: blended, not between smaller images, while it has none made. On
// OpenGL Core, with how the texture samples itself after (bits 4 to 7, bit 8 set), which a read that differs emulates
uint tidehl_mode(uint mode, uint t) {
    uint m = (mode & 3u) >= 2u && ((_TideUnmipped >> t) & 1u) != 0u ? (mode & ~3u) | 1u : mode;
#if TIDEHL_OWN_SAMPLERS
    uint own = (t < 8u ? _TideOwn >> (4u * t) : _TideOwnHigh >> (4u * (t - 8u))) & 15u;
    m = (m & 15u) | (own << 4) | 256u;
#endif
    return m;
}
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
uint4 tidehl_words(ByteAddressBuffer b, uint at, uint n) {
    uint4 r;
    if (n == 1u) r = uint4(b.Load(at), 0u, 0u, 0u);
    else if (n == 2u) r = uint4(b.Load2(at), 0u, 0u);
    else if (n == 3u) r = uint4(b.Load3(at), 0u);
    else r = b.Load4(at);
    return r;
}
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
// A read whose filter or wrap isn't the texture's own: its pixels fetched at their centers, which any filter gives
// exactly, and filtered and wrapped here (a cube's filter alone: it wraps nothing)
bool tidehl_emulates(uint mode, uint mask) {
    uint own = (mode >> 4) & 15u;
    return (mode & 256u) != 0u && own != 15u && (own & mask) != (mode & mask);
}
int tidehl_floor_mod(int i, int n) { return i - n * (int)floor((float)i / (float)n); }
int tidehl_wrapped(int i, int n, uint wrap) {
    int r = clamp(i, 0, n - 1);
    if (wrap == 1u) r = tidehl_floor_mod(i, n);
    else if (wrap == 2u) {
        int m = tidehl_floor_mod(i, 2 * n);
        r = m < n ? m : 2 * n - 1 - m;
    }
    return r;
}
float tidehl_lod(float2 size, float2 dx, float2 dy) {
    float2 x = dx * size, y = dy * size;
    return 0.5 * log2(max(max(dot(x, x), dot(y, y)), 1e-20));
}
float4 tidehl_texel(Texture2D<float4> t, SamplerState own, int2 p, float layer, uint level, int2 n) {
    return t.SampleLevel(own, (float2(p) + 0.5) / float2(n), (float)level);
}
float4 tidehl_texel(Texture2DArray<float4> t, SamplerState own, int2 p, float layer, uint level, int2 n) {
    return t.SampleLevel(own, float3((float2(p) + 0.5) / float2(n), layer), (float)level);
}
uint2 tidehl_size(Texture2D<float4> t, uint level, out uint levels) {
    uint w, h;
    t.GetDimensions(level, w, h, levels);
    return uint2(w, h);
}
uint2 tidehl_size(Texture2DArray<float4> t, uint level, out uint levels) {
    uint w, h, layers;
    t.GetDimensions(level, w, h, layers, levels);
    return uint2(w, h);
}
#define TIDEHL_EMULATE(T)                                                                                \
float4 tidehl_filtered(T t, SamplerState own, float2 at, float layer, uint level, uint mode) {         \
    uint levels;                                                                                       \
    int2 n = int2(tidehl_size(t, level, levels));                                                      \
    uint wrap = (mode >> 2) & 3u;                                                                      \
    if ((mode & 3u) == 0u) {                                                                           \
        int2 p = int2(floor(at * float2(n)));                                                          \
        return tidehl_texel(t, own, int2(tidehl_wrapped(p.x, n.x, wrap), tidehl_wrapped(p.y, n.y, wrap)), layer, level, n); \
    }                                                                                                  \
    float2 f = at * float2(n) - 0.5, k = f - floor(f);                                                 \
    int2 p = int2(floor(f));                                                                           \
    int x0 = tidehl_wrapped(p.x, n.x, wrap), x1 = tidehl_wrapped(p.x + 1, n.x, wrap);                  \
    int y0 = tidehl_wrapped(p.y, n.y, wrap), y1 = tidehl_wrapped(p.y + 1, n.y, wrap);                  \
    float4 top = lerp(tidehl_texel(t, own, int2(x0, y0), layer, level, n), tidehl_texel(t, own, int2(x1, y0), layer, level, n), k.x); \
    float4 bottom = lerp(tidehl_texel(t, own, int2(x0, y1), layer, level, n), tidehl_texel(t, own, int2(x1, y1), layer, level, n), k.x); \
    return lerp(top, bottom, k.y);                                                                     \
}                                                                                                      \
float4 tidehl_emulated(T t, SamplerState own, uint mode, float2 at, float layer, float lod) {          \
    if ((mode & 3u) < 2u) return tidehl_filtered(t, own, at, layer, 0u, mode);                        \
    uint levels;                                                                                       \
    tidehl_size(t, 0u, levels);                                                                        \
    float l = clamp(lod, 0.0, float(levels - 1u));                                                     \
    uint a = (uint)floor(l), b = min(a + 1u, levels - 1u);                                             \
    return lerp(tidehl_filtered(t, own, at, layer, a, mode), tidehl_filtered(t, own, at, layer, b, mode), l - floor(l)); \
}
TIDEHL_EMULATE(Texture2D<float4>)
TIDEHL_EMULATE(Texture2DArray<float4>)

// A cube's: a face, and where on it, as Direct3D's cubes are laid out, and back
float2 tidehl_cube_uv(float3 d, out uint face) {
    float3 a = abs(d);
    float ma;
    float2 st;
    if (a.x >= a.y && a.x >= a.z) { face = d.x >= 0.0 ? 0u : 1u; ma = a.x; st = float2(d.x >= 0.0 ? -d.z : d.z, -d.y); }
    else if (a.y >= a.z) { face = d.y >= 0.0 ? 2u : 3u; ma = a.y; st = float2(d.x, d.y >= 0.0 ? d.z : -d.z); }
    else { face = d.z >= 0.0 ? 4u : 5u; ma = a.z; st = float2(d.z >= 0.0 ? d.x : -d.x, -d.y); }
    return (st / max(ma, 1e-20) + 1.0) * 0.5;
}
float3 tidehl_cube_direction(uint face, float2 uv) {
    float2 s = uv * 2.0 - 1.0;
    float3 d = float3(1.0, -s.y, -s.x);
    if (face == 1u) d = float3(-1.0, -s.y, s.x);
    else if (face == 2u) d = float3(s.x, 1.0, s.y);
    else if (face == 3u) d = float3(s.x, -1.0, -s.y);
    else if (face == 4u) d = float3(s.x, -s.y, 1.0);
    else if (face == 5u) d = float3(-s.x, -s.y, -1.0);
    return d;
}
float4 tidehl_cube_texel(TextureCube<float4> t, SamplerState own, uint face, int2 p, uint level, int n) {
    p = clamp(p, 0, n - 1); // A face's edge: its own last pixels
    return t.SampleLevel(own, tidehl_cube_direction(face, (float2(p) + 0.5) / float(n)), (float)level);
}
float4 tidehl_cube_filtered(TextureCube<float4> t, SamplerState own, float3 at, uint level, uint mode) {
    uint w, h, levels, face;
    t.GetDimensions(level, w, h, levels);
    int n = (int)w;
    float2 uv = tidehl_cube_uv(at, face);
    if ((mode & 3u) == 0u) return tidehl_cube_texel(t, own, face, int2(floor(uv * float(n))), level, n);
    float2 f = uv * float(n) - 0.5, k = f - floor(f);
    int2 p = int2(floor(f));
    float4 top = lerp(tidehl_cube_texel(t, own, face, p, level, n), tidehl_cube_texel(t, own, face, p + int2(1, 0), level, n), k.x);
    float4 bottom = lerp(tidehl_cube_texel(t, own, face, p + int2(0, 1), level, n), tidehl_cube_texel(t, own, face, p + int2(1, 1), level, n), k.x);
    return lerp(top, bottom, k.y);
}
float4 tidehl_emulated(TextureCube<float4> t, SamplerState own, uint mode, float3 at, float layer, float lod) {
    if ((mode & 3u) < 2u) return tidehl_cube_filtered(t, own, at, 0u, mode);
    uint w, h, levels;
    t.GetDimensions(0u, w, h, levels);
    float l = clamp(lod, 0.0, float(levels - 1u));
    uint a = (uint)floor(l), b = min(a + 1u, levels - 1u);
    return lerp(tidehl_cube_filtered(t, own, at, a, mode), tidehl_cube_filtered(t, own, at, b, mode), l - floor(l));
}
// How far apart a pixel's neighbors read, in a level's pixels, for its level: a texture's and an array's by their
// uv, a cube's by its direction on the face it's on
float tidehl_lod_of(Texture2D<float4> t, float2 at, float2 dx, float2 dy) { uint l; return tidehl_lod(float2(tidehl_size(t, 0u, l)), dx, dy); }
float tidehl_lod_of(Texture2DArray<float4> t, float3 at, float2 dx, float2 dy) { uint l; return tidehl_lod(float2(tidehl_size(t, 0u, l)), dx, dy); }
float tidehl_lod_of(TextureCube<float4> t, float3 at, float3 dx, float3 dy) {
    uint w, h, levels;
    t.GetDimensions(0u, w, h, levels);
    float3 a = abs(at);
    float scale = float(w) * 0.5 / max(max(a.x, max(a.y, a.z)), 1e-20);
    return 0.5 * log2(max(max(dot(dx, dx), dot(dy, dy)) * scale * scale, 1e-20));
}
float2 tidehl_place(float2 at) { return at; }
float2 tidehl_place(float3 at) { return at.xy; }
float3 tidehl_whole(float3 at) { return at; }
float tidehl_layer(float2 at) { return 0.0; }
float tidehl_layer(float3 at) { return at.z; }
#define TIDEHL_SAMPLE(T, C, D, MASK, PLACE) /* D: a derivative, which an array coordinate's layer has none of */ \
float4 tidehl_sample_grad(T t, SamplerState own, uint mode, C at, D dx, D dy) {                        \
    float4 r;                                                                                          \
    if (tidehl_emulates(mode, MASK)) r = tidehl_emulated(t, own, mode, PLACE(at), tidehl_layer(at), tidehl_lod_of(t, at, dx, dy)); \
    else if ((mode & 3u) >= 2u) r = t.SampleGrad(own, at, dx, dy);                                     \
    else r = t.SampleLevel(own, at, 0.0);                                                              \
    return r;                                                                                          \
}                                                                                                      \
float4 tidehl_sample_level(T t, SamplerState own, uint mode, C at, float level) {                      \
    float4 r;                                                                                          \
    if (tidehl_emulates(mode, MASK)) r = tidehl_emulated(t, own, mode, PLACE(at), tidehl_layer(at), level); \
    else r = t.SampleLevel(own, at, (mode & 3u) >= 2u ? level : 0.0);                                  \
    return r;                                                                                          \
}
#else
#define TIDEHL_SAMPLE(T, C, D) /* D: a derivative, which an array coordinate's layer has none of */    \
float4 tidehl_sample_grad(T t, SamplerState own, uint mode, C at, D dx, D dy) {                        \
    uint filter = mode & 3u, wrap = (mode >> 2) & 3u;                                                  \
    float4 r;                                                                                          \
    if (filter == 3u) {                                                                                \
        if (wrap == 1u) r = t.SampleGrad(tidehl_trilinear_repeat_aniso8_sampler, at, dx, dy);        \
        else if (wrap == 2u) r = t.SampleGrad(tidehl_trilinear_mirror_aniso8_sampler, at, dx, dy);   \
        else r = t.SampleGrad(tidehl_trilinear_clamp_aniso8_sampler, at, dx, dy);                    \
    } else if (filter == 2u) {                                                                         \
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
    if (filter < 2u) r = tidehl_sample_grad(t, own, mode, at, (D)0, (D)0);                             \
    else if (wrap == 1u) r = t.SampleLevel(tidehl_trilinear_repeat_sampler, at, level);               \
    else if (wrap == 2u) r = t.SampleLevel(tidehl_trilinear_mirror_sampler, at, level);               \
    else r = t.SampleLevel(tidehl_trilinear_clamp_sampler, at, level);                                \
    return r;                                                                                          \
}
#endif
#define TIDEHL_SAMPLE_DERIVED(T, C, S) /* S: the part of the coordinate a derivative is of */           \
float4 tidehl_sample(T t, SamplerState own, uint mode, C at) { return tidehl_sample_grad(t, own, mode, at, ddx(at.S), ddy(at.S)); } \
float4 tidehl_sample_bias(T t, SamplerState own, uint mode, C at, float bias) {                        \
    float scale = exp2(bias);                                                                          \
    return tidehl_sample_grad(t, own, mode, at, ddx(at.S) * scale, ddy(at.S) * scale);                 \
}
#if TIDEHL_OWN_SAMPLERS
TIDEHL_SAMPLE(Texture2D<float4>, float2, float2, 15u, tidehl_place)
TIDEHL_SAMPLE(TextureCube<float4>, float3, float3, 3u, tidehl_whole)
TIDEHL_SAMPLE(Texture2DArray<float4>, float3, float2, 15u, tidehl_place)
#else
TIDEHL_SAMPLE(Texture2D<float4>, float2, float2)
TIDEHL_SAMPLE(TextureCube<float4>, float3, float3)
TIDEHL_SAMPLE(Texture2DArray<float4>, float3, float2)
#endif
TIDEHL_SAMPLE_DERIVED(Texture2D<float4>, float2, xy)
TIDEHL_SAMPLE_DERIVED(TextureCube<float4>, float3, xyz)
TIDEHL_SAMPLE_DERIVED(Texture2DArray<float4>, float3, xy)

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
    float4 g = t.GatherRed(TIDEHL_NEAREST(own), (floor(p) + 1.0) / size);
#if TIDEHL_REVERSED_Z
    g = 1.0 - g;
#endif
    float4 lit = step(d.xxxx, g); // Gathered as (0, 1), (1, 1), (1, 0), (0, 0)
    return lerp(lerp(lit.w, lit.z, f.x), lerp(lit.x, lit.y, f.x), f.y);
}
float tidehl_compare(TextureCube<float> t, SamplerState own, float3 at, float d) { return d <= tidehl_depth(t, own, at) ? 1.0 : 0.0; }
float tidehl_depth(Texture2DArray<float> t, SamplerState own, float3 at) { return tidehl_depth_in(t.SampleLevel(TIDEHL_NEAREST(own), at, 0.0)); }
float tidehl_compare(Texture2DArray<float> t, SamplerState own, float3 at, float d) { // at.z: the layer
    uint w, h, n;
    t.GetDimensions(w, h, n);
    float2 size = float2(w, h);
    float2 p = at.xy * size - 0.5;
    float2 f = frac(p);
    float4 g = t.GatherRed(TIDEHL_NEAREST(own), float3((floor(p) + 1.0) / size, at.z));
#if TIDEHL_REVERSED_Z
    g = 1.0 - g;
#endif
    float4 lit = step(d.xxxx, g);
    return lerp(lerp(lit.w, lit.z, f.x), lerp(lit.x, lit.y, f.x), f.y);
}

// Loads and sizes
float4 tidehl_load(Texture2D<float4> t, int2 at, int level) { return t.Load(int3(at, level)); }
int4 tidehl_load(Texture2D<int4> t, int2 at, int level) { return t.Load(int3(at, level)); }
uint4 tidehl_load(Texture2D<uint4> t, int2 at, int level) { return t.Load(int3(at, level)); }
float tidehl_load(Texture2D<float> t, int2 at, int level) { return tidehl_depth_in(t.Load(int3(at, level))); }
float4 tidehl_load(Texture2DMS<float4> t, int2 at, int s) { return t.Load(at, s); }
float tidehl_load(Texture2DMS<float> t, int2 at, int s) { return tidehl_depth_in(t.Load(at, s)); }
float4 tidehl_load(Texture2DArray<float4> t, int2 at, int layer, int level) { return t.Load(int4(at, layer, level)); }
int4 tidehl_load(Texture2DArray<int4> t, int2 at, int layer, int level) { return t.Load(int4(at, layer, level)); }
uint4 tidehl_load(Texture2DArray<uint4> t, int2 at, int layer, int level) { return t.Load(int4(at, layer, level)); }
float tidehl_load(Texture2DArray<float> t, int2 at, int layer, int level) { return tidehl_depth_in(t.Load(int4(at, layer, level))); }
#define TIDEHL_SIZE(T)                                                                                 \
uint2 tidehl_size(T t, uint level) { uint w, h, n; t.GetDimensions(level, w, h, n); return uint2(w, h); } \
uint tidehl_levels(T t) { uint w, h, n; t.GetDimensions(0, w, h, n); return n; }
TIDEHL_SIZE(Texture2D<float4>)
TIDEHL_SIZE(Texture2D<int4>)
TIDEHL_SIZE(Texture2D<uint4>)
TIDEHL_SIZE(Texture2D<float>)
TIDEHL_SIZE(TextureCube<float4>)
TIDEHL_SIZE(TextureCube<float>)
#define TIDEHL_SIZE_ARRAY(T)                                                                           \
uint2 tidehl_size(T t, uint level) { uint w, h, e, n; t.GetDimensions(level, w, h, e, n); return uint2(w, h); } \
uint tidehl_levels(T t) { uint w, h, e, n; t.GetDimensions(0, w, h, e, n); return n; }                \
uint tidehl_layers(T t) { uint w, h, e, n; t.GetDimensions(0, w, h, e, n); return e; }
TIDEHL_SIZE_ARRAY(Texture2DArray<float4>)
TIDEHL_SIZE_ARRAY(Texture2DArray<int4>)
TIDEHL_SIZE_ARRAY(Texture2DArray<uint4>)
TIDEHL_SIZE_ARRAY(Texture2DArray<float>)
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
    float4x4 screen;
    float2 texel;
    float2 scale;
    float4 fix;
};
struct Corner {
    float4 position;
    float2 local;
    float2 extent;
    float4 tint;
    float form;
    float4 look;
    float2 gap;
    float4 tint2;
    float4 rim;
};

cbuffer TideUniforms {
    row_major float4x4 tidehl_u_screen;
    float2 tidehl_u_texel;
    float2 tidehl_u_scale;
    float4 tidehl_u_fix;
};

Corner vertex(float2 corner, float4 shape, float4 color, float kind, float radius);
float4 whiten(float4 c, float by);
float4 over(float4 p, float4 tint, float by);
float ellipse(float2 at, float2 radii);
float4 fragment(Corner in_w);

Corner vertex(float2 corner, float4 shape, float4 color, float kind, float radius)
{
    float4 turn = float4(1.0, 0.0, radius, 0.0);
    float2 fade = float2(0.0, 0.0);
    float4 color2 = color;
    float4 rim_color = ((float4)(0.0));
    float2 c = ((corner * 2.0) - 1.0);
    Corner out_w = ((Corner)0);
    float2 p = ((float2)0);
    if ((kind > 4.5)) {
        out_w.extent = abs(shape.zw);
        out_w.local = (c * ((out_w.extent + ((kind > 7.5) ? 0.0 : max(fade.x, 0.0))) + 1.0));
        p = (shape.xy + float2(((out_w.local.x * turn.x) - (out_w.local.y * turn.y)), ((out_w.local.x * turn.y) + (out_w.local.y * turn.x))));
    } else if ((kind > 3.5)) {
        float2 d = (shape.zw - shape.xy);
        float len = length(d);
        float2 along = float2(1.0, 0.0);
        if ((len > 0.0)) {
            along = (d / len);
        }
        out_w.extent = float2(((len * 0.5) + 0.5), 0.5);
        out_w.local = (c * out_w.extent);
        p = ((((shape.xy + shape.zw) * 0.5) + (along * out_w.local.x)) + (float2((-along.y), along.x) * out_w.local.y));
    } else {
        out_w.extent = abs(shape.zw);
        float room = 0.0;
        if ((kind > 1.5)) {
            room = 1.0;
        }
        out_w.local = (c * (out_w.extent + room));
        p = (shape.xy + out_w.local);
    }
    out_w.tint = color;
    out_w.form = kind;
    out_w.look = ((kind > 7.5) ? float4(turn.zw, fade) : float4(turn.z, turn.w, fade.x, 0.0));
    out_w.gap = float2(cos(fade.y), sin(fade.y));
    out_w.tint2 = color2;
    out_w.rim = rim_color;
    out_w.position = mul(float4(p, 0.0, 1.0), tidehl_u_screen);
    return out_w;
}

float4 whiten(float4 c, float by)
{
    return float4(((c.rgb * c.a) + (by * (1.0 - c.a))), c.a);
}

float4 over(float4 p, float4 tint, float by)
{
    float4 c = (p * float4((tint.rgb * tint.a), tint.a));
    return float4((c.rgb + (by * (1.0 - c.a))), c.a);
}

float ellipse(float2 at, float2 radii)
{
    float2 q = abs(at);
    float2 t = ((float2)(0.707106769));
    float2 e = (float2(((radii.x * radii.x) - (radii.y * radii.y)), ((radii.y * radii.y) - (radii.x * radii.x))) / radii);
    {
        int i = 0;
        [loop] for (; (i < 3); i++) {
            float2 m = (((e * t) * t) * t);
            float2 w = (q - m);
            t = clamp((((w * (length(((radii * t) - m)) / max(length(w), 9.99999997e-07))) + m) / radii), ((float2)(0.0)), ((float2)(1.0)));
            float n = length(t);
            if ((n > 9.99999975e-06)) {
                t = (t / n);
            } else {
                t = float2(0.0, 1.0);
            }
        }
    }
    {
        int i = 0;
        [loop] for (; (i < 2); i++) {
            float2 on = (radii * t);
            float2 along = (radii * float2((-t.y), t.x));
            float2 to = (q - on);
            float span = dot(along, along);
            float curve = (span + dot(to, on));
            if ((curve > (0.25 * span))) {
                float slide = (dot(to, along) / curve);
                float2 next = clamp(float2((t.x - (t.y * slide)), (t.y + (t.x * slide))), ((float2)(0.0)), ((float2)(1.0)));
                float n = length(next);
                if ((n > 9.99999975e-06)) {
                    t = (next / n);
                }
            }
        }
    }
    float apart = length((q - (radii * t)));
    float2 s = (q / radii);
    if ((dot(s, s) < 1.0)) {
        return (-apart);
    } else {
        return apart;
    }
}

float4 fragment(Corner in_w)
{
    if ((in_w.form < 4.5)) {
        float cover = 1.0;
        if (((in_w.form > 0.5) && (in_w.form < 1.5))) {
            if (all((abs(in_w.local) < (in_w.extent - 1.0)))) {
                discard;
            }
        } else if (((in_w.form > 1.5) && (in_w.form < 3.5))) {
            float d = length(in_w.local);
            cover = clamp(((in_w.extent.x - d) + 0.5), 0.0, 1.0);
            if ((in_w.form > 2.5)) {
                cover = (cover * clamp(((d - in_w.extent.x) + 1.5), 0.0, 1.0));
            }
            if ((cover <= 0.0)) {
                discard;
            }
        }
        return whiten(float4(in_w.tint.rgb, (in_w.tint.a * cover)), tidehl_u_fix.x);
    } else {
        float px = ((tidehl_u_scale.x + tidehl_u_scale.y) * 0.5);
        float d = ((float)0);
        float outline = in_w.look.y;
        float soft = ((in_w.form > 7.5) ? 0.0 : in_w.look.z);
        if ((in_w.form > 7.5)) {
            float top = ((in_w.local.x < 0.0) ? in_w.look.x : in_w.look.y);
            float bottom = ((in_w.local.x < 0.0) ? in_w.look.w : in_w.look.z);
            float r = ((in_w.local.y < 0.0) ? top : bottom);
            float2 q = ((abs(in_w.local) - in_w.extent) + r);
            d = ((length(max(q, ((float2)(0.0)))) + min(max(q.x, q.y), 0.0)) - r);
            outline = 0.0;
        } else if ((in_w.form > 6.5)) {
            float2 q = float2(in_w.local.x, abs(in_w.local.y));
            float ring = (abs(((length(q) - in_w.extent.x) + (outline * 0.5))) - (outline * 0.5));
            float wedge = dot(q, float2((-in_w.gap.y), in_w.gap.x));
            if ((dot(q, in_w.gap) <= 0.0)) {
                if ((in_w.gap.x > 0.0)) {
                    wedge = length(q);
                } else {
                    wedge = (-length(q));
                }
            }
            d = (min(max(ring, wedge), 0.0) + length(max(float2(ring, wedge), ((float2)(0.0)))));
            outline = 0.0;
        } else if ((in_w.form > 5.5)) {
            d = ellipse(in_w.local, in_w.extent);
        } else {
            float2 q = ((abs(in_w.local) - in_w.extent) + in_w.look.x);
            d = ((length(max(q, ((float2)(0.0)))) + min(max(q.x, q.y), 0.0)) - in_w.look.x);
        }
        float cover = clamp((0.5 - (d * px)), 0.0, 1.0);
        if ((soft > 0.0)) {
            float glow = clamp((1.0 - (((d * px) + 0.5) / ((soft * px) + 1.0))), 0.0, 1.0);
            cover = (glow * glow);
        } else if ((soft < 0.0)) {
            float glow = clamp((1.0 + (((d * px) - 0.5) / (1.0 - (soft * px)))), 0.0, 1.0);
            cover = (cover * (glow * glow));
        }
        if ((cover <= (tidehl_u_fix.y * 0.00400000019))) {
            discard;
        }
        float down = clamp((((in_w.local.y / max(in_w.extent.y, 9.99999997e-07)) * 0.5) + 0.5), 0.0, 1.0);
        float4 paint = lerp(float4((in_w.tint.rgb * in_w.tint.a), in_w.tint.a), float4((in_w.tint2.rgb * in_w.tint2.a), in_w.tint2.a), ((float4)(down)));
        if ((outline > 0.0)) {
            paint = lerp(float4((in_w.rim.rgb * in_w.rim.a), in_w.rim.a), paint, ((float4)(clamp((0.5 - ((d + outline) * px)), 0.0, 1.0))));
        }
        if ((paint.a <= 0.0)) {
            if ((tidehl_u_fix.y < 0.5)) {
                discard;
            }
            return whiten(((float4)(0.0)), tidehl_u_fix.x);
        } else {
            return ((paint * cover) + float4(((float3)((tidehl_u_fix.x * (1.0 - (paint.a * cover))))), 0.0));
        }
    }
}

struct tidehl_varyings {
    float4 position : SV_Position;
    float2 l0 : TEXCOORD0;
    float2 l1 : TEXCOORD1;
    float4 l2 : TEXCOORD2;
    float l3 : TEXCOORD3;
    float4 l4 : TEXCOORD4;
    float2 l5 : TEXCOORD5;
    float4 l6 : TEXCOORD6;
    float4 l7 : TEXCOORD7;
};

tidehl_varyings tidehl_vertex(uint tidehl_v : SV_VertexID, uint tidehl_instance : SV_InstanceID) {
    uint tidehl_vertex = tidehl_vertex_of(tidehl_v);
    float2 tidehl_p0;
    tidehl_p0 = tidehl_float_attribute(0u, tidehl_vertex, tidehl_instance).xy;
    float4 tidehl_p1;
    tidehl_p1 = tidehl_float_attribute(1u, tidehl_vertex, tidehl_instance);
    float4 tidehl_p2;
    tidehl_p2 = tidehl_float_attribute(2u, tidehl_vertex, tidehl_instance);
    float tidehl_p3;
    tidehl_p3 = tidehl_float_attribute(3u, tidehl_vertex, tidehl_instance).x;
    float tidehl_p4;
    tidehl_p4 = tidehl_float_attribute(4u, tidehl_vertex, tidehl_instance).x;
    Corner tidehl_r = vertex(tidehl_p0, tidehl_p1, tidehl_p2, tidehl_p3, tidehl_p4);
    tidehl_varyings tidehl_o;
    tidehl_o.position = tidehl_clip(tidehl_r.position);
    tidehl_o.l0 = tidehl_r.local;
    tidehl_o.l1 = tidehl_r.extent;
    tidehl_o.l2 = tidehl_r.tint;
    tidehl_o.l3 = tidehl_r.form;
    tidehl_o.l4 = tidehl_r.look;
    tidehl_o.l5 = tidehl_r.gap;
    tidehl_o.l6 = tidehl_r.tint2;
    tidehl_o.l7 = tidehl_r.rim;
    return tidehl_o;
}

struct tidehl_targets {
    float4 c0 : SV_Target0;
};

tidehl_targets tidehl_fragment(tidehl_varyings tidehl_i, bool tidehl_front : SV_IsFrontFace) {
    Corner tidehl_p0 = ((Corner)0);
    tidehl_p0.position = tidehl_position(tidehl_i.position);
    tidehl_p0.local = tidehl_i.l0;
    tidehl_p0.extent = tidehl_i.l1;
    tidehl_p0.tint = tidehl_i.l2;
    tidehl_p0.form = tidehl_i.l3;
    tidehl_p0.look = tidehl_i.l4;
    tidehl_p0.gap = tidehl_i.l5;
    tidehl_p0.tint2 = tidehl_i.l6;
    tidehl_p0.rim = tidehl_i.l7;
    float4 tidehl_r = fragment(tidehl_p0);
    tidehl_targets tidehl_o;
    tidehl_o.c0 = tidehl_output(tidehl_r);
    return tidehl_o;
}
            ENDHLSL
        }
    }
}
