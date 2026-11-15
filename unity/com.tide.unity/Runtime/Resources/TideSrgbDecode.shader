// A Gamma project's [Linear] image (TideRender.Decode): each texel of a level of its bytes as stored (UNorm), loaded
// with no sampler, its color turned from sRGB to linear light and its alpha left as it is, into a half-float image of
// the same size, which is what draws read, filter and make smaller images of.
Shader "Hidden/Tide/SrgbDecode"
{
    HLSLINCLUDE
    #pragma target 4.5
    #include "UnityCG.cginc"
    Texture2D<float4> _TideSrgbDecode2D;
    Texture2DArray<float4> _TideSrgbDecodeArray;
    float4 _TideSrgbDecodeInfo; // the level, the layer or face
    float4 vertex(uint i : SV_VertexID) : SV_Position
    {
        float2 corner = float2((i << 1) & 2, i & 2);
        return float4(corner * 2.0 - 1.0, 0.0, 1.0);
    }
    float4 decoded(float4 c)
    {
        float3 s = saturate(c.rgb);
        return float4(s <= 0.04045 ? s / 12.92 : pow((s + 0.055) / 1.055, 2.4), c.a);
    }
    float4 fragment2D(float4 at : SV_Position) : SV_Target
    {
        return decoded(_TideSrgbDecode2D.Load(int3(int2(at.xy), (int)_TideSrgbDecodeInfo.x)));
    }
    float4 fragmentArray(float4 at : SV_Position) : SV_Target
    {
        return decoded(_TideSrgbDecodeArray.Load(int4(int2(at.xy), (int)_TideSrgbDecodeInfo.y, (int)_TideSrgbDecodeInfo.x)));
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
    }
}
