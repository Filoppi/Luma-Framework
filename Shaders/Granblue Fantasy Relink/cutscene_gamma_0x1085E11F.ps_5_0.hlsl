
cbuffer ParamBuffer : register(b1)
{
   float4 g_Param : packoffset(c0);
}

SamplerState g_Texture0Sampler_s : register(s0);
Texture2D<float4> g_Texture0 : register(t0);

#define cmp -

void main(float4 v0: SV_Position0, float2 v1: TEXCOORD0, out float4 o0: SV_Target0)
{
   float4 r0, r1;

   r0.x = g_Param.y + 1;
   r0.y = -g_Param.y + 1.01171875;
   r0.x = r0.x / r0.y;
   r0.x = 1.01171875 * r0.x;
   r1.xyzw = g_Texture0.Sample(g_Texture0Sampler_s, v1.xy).xyzw;

#if TONEMAP_AFTER_TAA
   o0 = r1;
   return;
#endif

   r0.yzw = g_Param.xxx + r1.xyz;
   o0.w = r1.w;
   r0.yzw = float3(-0.5, -0.5, -0.5) + r0.yzw;
   o0.xyz = r0.xxx * r0.yzw + float3(0.5, 0.5, 0.5);

   return;
}
