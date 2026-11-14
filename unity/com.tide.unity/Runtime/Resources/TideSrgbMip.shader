// Sample the previous level through its sRGB view before filtering; the sRGB attachment encodes the result.
Shader "Hidden/Tide/SrgbMip"
{
    HLSLINCLUDE
    #pragma target 4.5
    #include "UnityCG.cginc"
    Texture2D<float4> _TideSrgbSource2D;
    Texture2DArray<float4> _TideSrgbSourceArray;
    TextureCube<float4> _TideSrgbSourceCube;
    SamplerState tide_srgb_linear_clamp_sampler;
    #if defined(SHADER_API_GLCORE)
    SamplerState sampler_TideSrgbSource2D;
    SamplerState sampler_TideSrgbSourceArray;
    SamplerState sampler_TideSrgbSourceCube;
    #endif
    float4 _TideSrgbMipInfo; // output width/height, source mip level, layer or face
    float4 vertex(uint i : SV_VertexID) : SV_Position
    {
        float2 corner = float2((i << 1) & 2, i & 2);
        return float4(corner * 2.0 - 1.0, 0.0, 1.0);
    }
    float3 cubeDirection(float2 uv)
    {
        float2 q = uv * 2.0 - 1.0;
        float3 direction = float3(1.0, -q.y, -q.x);
        if (_TideSrgbMipInfo.w == 1.0) direction = float3(-1.0, -q.y, q.x);
        if (_TideSrgbMipInfo.w == 2.0) direction = float3(q.x, 1.0, q.y);
        if (_TideSrgbMipInfo.w == 3.0) direction = float3(q.x, -1.0, -q.y);
        if (_TideSrgbMipInfo.w == 4.0) direction = float3(q.x, -q.y, 1.0);
        if (_TideSrgbMipInfo.w == 5.0) direction = float3(-q.x, -q.y, -1.0);
        return direction;
    }
    float4 fragment2D(float4 at : SV_Position) : SV_Target
    {
        float2 uv = at.xy / _TideSrgbMipInfo.xy;
        #if defined(SHADER_API_GLCORE)
        // Unity GL uses the texture's own sampler, which a previous draw may have set to Point.
        // Center taps decode sRGB before this explicit bilinear interpolation, preserving that draw's state.
        uint width, height, levels;
        _TideSrgbSource2D.GetDimensions((uint)_TideSrgbMipInfo.z, width, height, levels);
        float2 source = uv * float2(width, height) - 0.5;
        int2 low = int2(floor(source));
        int2 hi = int2(width, height) - 1;
        float4 a = _TideSrgbSource2D.SampleLevel(sampler_TideSrgbSource2D, (float2(clamp(low + int2(0, 0), int2(0, 0), hi)) + 0.5) / float2(width, height), _TideSrgbMipInfo.z);
        float4 b = _TideSrgbSource2D.SampleLevel(sampler_TideSrgbSource2D, (float2(clamp(low + int2(1, 0), int2(0, 0), hi)) + 0.5) / float2(width, height), _TideSrgbMipInfo.z);
        float4 c = _TideSrgbSource2D.SampleLevel(sampler_TideSrgbSource2D, (float2(clamp(low + int2(0, 1), int2(0, 0), hi)) + 0.5) / float2(width, height), _TideSrgbMipInfo.z);
        float4 d = _TideSrgbSource2D.SampleLevel(sampler_TideSrgbSource2D, (float2(clamp(low + int2(1, 1), int2(0, 0), hi)) + 0.5) / float2(width, height), _TideSrgbMipInfo.z);
        return lerp(lerp(a, b, frac(source.x)), lerp(c, d, frac(source.x)), frac(source.y));
        #else
        return _TideSrgbSource2D.SampleLevel(tide_srgb_linear_clamp_sampler, uv, _TideSrgbMipInfo.z);
        #endif
    }
    float4 fragmentArray(float4 at : SV_Position) : SV_Target
    {
        float2 uv = at.xy / _TideSrgbMipInfo.xy;
        #if defined(SHADER_API_GLCORE)
        uint width, height, layers, levels;
        _TideSrgbSourceArray.GetDimensions((uint)_TideSrgbMipInfo.z, width, height, layers, levels);
        float2 source = uv * float2(width, height) - 0.5;
        int2 low = int2(floor(source));
        int2 hi = int2(width, height) - 1;
        float4 a = _TideSrgbSourceArray.SampleLevel(sampler_TideSrgbSourceArray, float3((float2(clamp(low + int2(0, 0), int2(0, 0), hi)) + 0.5) / float2(width, height), _TideSrgbMipInfo.w), _TideSrgbMipInfo.z);
        float4 b = _TideSrgbSourceArray.SampleLevel(sampler_TideSrgbSourceArray, float3((float2(clamp(low + int2(1, 0), int2(0, 0), hi)) + 0.5) / float2(width, height), _TideSrgbMipInfo.w), _TideSrgbMipInfo.z);
        float4 c = _TideSrgbSourceArray.SampleLevel(sampler_TideSrgbSourceArray, float3((float2(clamp(low + int2(0, 1), int2(0, 0), hi)) + 0.5) / float2(width, height), _TideSrgbMipInfo.w), _TideSrgbMipInfo.z);
        float4 d = _TideSrgbSourceArray.SampleLevel(sampler_TideSrgbSourceArray, float3((float2(clamp(low + int2(1, 1), int2(0, 0), hi)) + 0.5) / float2(width, height), _TideSrgbMipInfo.w), _TideSrgbMipInfo.z);
        return lerp(lerp(a, b, frac(source.x)), lerp(c, d, frac(source.x)), frac(source.y));
        #else
        return _TideSrgbSourceArray.SampleLevel(tide_srgb_linear_clamp_sampler, float3(uv, _TideSrgbMipInfo.w), _TideSrgbMipInfo.z);
        #endif
    }
    float4 fragmentCube(float4 at : SV_Position) : SV_Target
    {
        float2 uv = at.xy / _TideSrgbMipInfo.xy;
        #if defined(SHADER_API_GLCORE)
        uint width, height, levels;
        _TideSrgbSourceCube.GetDimensions((uint)_TideSrgbMipInfo.z, width, height, levels);
        float2 dimensions = float2(width, height);
        float2 source = uv * dimensions - 0.5;
        float2 low = floor(source);
        float2 hi = dimensions - 1.0;
        float2 a = (clamp(low, 0.0, hi) + 0.5) / dimensions;
        float2 b = (clamp(low + float2(1, 0), 0.0, hi) + 0.5) / dimensions;
        float2 c = (clamp(low + float2(0, 1), 0.0, hi) + 0.5) / dimensions;
        float2 d = (clamp(low + float2(1, 1), 0.0, hi) + 0.5) / dimensions;
        float4 ca = _TideSrgbSourceCube.SampleLevel(sampler_TideSrgbSourceCube, cubeDirection(a), _TideSrgbMipInfo.z);
        float4 cb = _TideSrgbSourceCube.SampleLevel(sampler_TideSrgbSourceCube, cubeDirection(b), _TideSrgbMipInfo.z);
        float4 cc = _TideSrgbSourceCube.SampleLevel(sampler_TideSrgbSourceCube, cubeDirection(c), _TideSrgbMipInfo.z);
        float4 cd = _TideSrgbSourceCube.SampleLevel(sampler_TideSrgbSourceCube, cubeDirection(d), _TideSrgbMipInfo.z);
        return lerp(lerp(ca, cb, frac(source.x)), lerp(cc, cd, frac(source.x)), frac(source.y));
        #else
        return _TideSrgbSourceCube.SampleLevel(tide_srgb_linear_clamp_sampler, cubeDirection(uv), _TideSrgbMipInfo.z);
        #endif
    }
    ENDHLSL
    SubShader
    {
        ZTest Always ZWrite Off Cull Off Blend Off
        Pass
        {
            HLSLPROGRAM
            #pragma vertex vertex
            #pragma fragment fragment2D
            ENDHLSL
        }
        Pass
        {
            HLSLPROGRAM
            #pragma vertex vertex
            #pragma fragment fragmentArray
            ENDHLSL
        }
        Pass
        {
            HLSLPROGRAM
            #pragma vertex vertex
            #pragma fragment fragmentCube
            ENDHLSL
        }
    }
}
