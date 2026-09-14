cbuffer PerInstanceCB : register(b2)
{
  float4 cb_tonemapping_blendcolorcube_blendweights : packoffset(c0);
}

SamplerState smp_pointclamp_s : register(s0);
Texture3D<float4> ro_tonemapping_blendcolorcube_targetcolorcube : register(t0);
Texture3D<float4> ro_tonemapping_blendcolorcube_sourcecolorcube : register(t1);
// 32x32x32 linear in/out LUT
RWTexture3D<float4> rw_tonemapping_finalcolorcube : register(u0);

#define LUT_SIZE 32

[numthreads(LUT_SIZE, LUT_SIZE, 1)]
void main(uint3 vThreadID : SV_DispatchThreadID, uint3 vGroupID : SV_GroupID, uint3 vThreadIDInGroup : SV_GroupThreadID)
{
  float4 r0,r1;
  r0.xyz = (uint3)vThreadIDInGroup.xyz;
  r0.xyz = float3(0.5,0.5,0.5) + r0.xyz; //TODO: z is ThreadGroupID???
  r0.xyz /= LUT_SIZE;
  r1.xyzw = ro_tonemapping_blendcolorcube_targetcolorcube.SampleLevel(smp_pointclamp_s, r0.xyz, 0).xyzw;
  r0.xyzw = ro_tonemapping_blendcolorcube_sourcecolorcube.SampleLevel(smp_pointclamp_s, r0.xyz, 0).xyzw;
  rw_tonemapping_finalcolorcube[vThreadIDInGroup.xyz] = lerp(r0.xyzw, r1.xyzw, cb_tonemapping_blendcolorcube_blendweights.x);
}