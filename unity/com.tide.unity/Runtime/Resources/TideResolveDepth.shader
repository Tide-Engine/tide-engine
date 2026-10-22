// A multisampled Tide render target's depth, a sample of each pixel's, into a depth texture of one sample a pixel,
// which TideRender.cs draws as Tide's renderer resolves the target's depth (gpu.h's resolve_depth).
Shader "Hidden/Tide/ResolveDepth"
{
    SubShader
    {
        Pass
        {
            ZTest Always
            ZWrite On
            ColorMask 0
            Cull Off
            HLSLPROGRAM
            #pragma vertex vertex
            #pragma fragment fragment
            #pragma target 4.5
            Texture2DMS<float> _TideDepthMS;

            float4 vertex(uint i : SV_VertexID) : SV_Position
            {
                float2 corner = float2((i << 1) & 2, i & 2); // (0, 0), (2, 0), (0, 2): a triangle over the target
                return float4(corner * 2.0 - 1.0, 0.5, 1.0);
            }

            float fragment(float4 at : SV_Position) : SV_Depth
            {
                return _TideDepthMS.Load(int2(at.xy), 0);
            }
            ENDHLSL
        }
    }
}
