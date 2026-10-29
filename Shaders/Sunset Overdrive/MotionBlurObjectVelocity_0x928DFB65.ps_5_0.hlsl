// PS_MotionBlurObjectVelocity, drawn with VS_ModelMotionBlurVelocity 0xD7C436BA. Vanilla writes the object velocity to RT0
// (R8G8_UNORM) over the camera velocity (RT1 of 0x524BC7C4). The VS outputs the vertex in the previous view space (with the previous
// skinning, object matrix and ambient animation) in TEXCOORD0.xyz, and SV_Position is the current, jittered raster position.
// We add RT1, the motion vectors (RT2 of 0x524BC7C4). See "Includes/Velocity.hlsl".

#include "Includes/Velocity.hlsl"

cbuffer MultiPerViewportCB : register(b0)
{
   float4 g_VP_Unused0[15];
   float4 g_VP_VPosMapping; // SV_Position.xy -> UV
}

void main(float4 v0 : SV_POSITION0, float4 v1 : TEXCOORD0, out float2 o0 : SV_Target0, out float2 o1 : SV_Target1)
{
   VelocityOutputs(v0.xy * g_VP_VPosMapping.xy + g_VP_VPosMapping.zw, v1.xy * rcp(max(v1.z, 0.1)), o0, o1);
}
