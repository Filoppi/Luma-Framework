// PS_LinearDepthCameraVelocity, drawn full screen with VS_2dPosTex0 0xA993990F. Vanilla writes the linear view depth to RT0 (R32F)
// and the camera velocity to RT1 (R8G8_UNORM), which we now write without the jitter. We add RT2, the R16G16_FLOAT motion vectors
// that C++ binds for SR and SMAA T2x (unbound otherwise). The object velocity pass draws over both velocity targets. See
// "Includes/Velocity.hlsl".

#include "Includes/Velocity.hlsl"

cbuffer MultiPerViewportCB : register(b0)
{
   float4 g_VP_Unused0[27];
   float2 g_VP_LinearDepthConsts; // Linear depth = y / (x - device depth)
}

SamplerState g_PointClampSampler : register(s0);
Texture2D<float4> g_ViewDepthBuffer : register(t0);

void main(float4 v0 : SV_POSITION0, float2 v1 : TEXCOORD0, out float o0 : SV_Target0, out float2 o1 : SV_Target1, out float2 o2 : SV_Target2)
{
   const float device_depth = g_ViewDepthBuffer.SampleLevel(g_PointClampSampler, v1, 0).x;
   const float view_depth = g_VP_LinearDepthConsts.y / (g_VP_LinearDepthConsts.x - device_depth);
   o0 = view_depth;

   const float3 view_pos = float3((v1 * g_MBV_ViewVec.xy + g_MBV_ViewVec.zw) * view_depth, view_depth);
   const float3 prev_view_pos = mul(float4(view_pos, 1.0), g_MBV_ViewSpaceDelta).xyz;
   VelocityOutputs(v1, prev_view_pos.xy * rcp(max(prev_view_pos.z, 0.1)), o1, o2);
}
