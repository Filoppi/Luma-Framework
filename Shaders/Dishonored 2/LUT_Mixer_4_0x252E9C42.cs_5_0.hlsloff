cbuffer PerInstanceCB : register(b2)
{
  float4 cb_tonemapping_blendcolorcube_blendweights : packoffset(c0);
}

SamplerState smp_pointclamp_s : register(s0);
Texture3D<float4> ro_tonemapping_overridecolorcube_colorcube3 : register(t0);
Texture3D<float4> ro_tonemapping_overridecolorcube_colorcube2 : register(t1);
Texture3D<float4> ro_tonemapping_overridecolorcube_colorcube1 : register(t2);
Texture3D<float4> ro_tonemapping_overridecolorcube_colorcube0 : register(t3);
Texture3D<float4> ro_tonemapping_blendcolorcube_sourcecolorcube : register(t4);
// 32x32x32 linear in/out LUT
RWTexture3D<float4> rw_tonemapping_finalcolorcube : register(u0);

#define cmp -

[numthreads(32, 32, 1)]
void main(uint3 vThreadID : SV_DispatchThreadID, uint3 vGroupID : SV_GroupID, uint3 vThreadIDInGroup : SV_GroupThreadID)
{
  float4 r0,r1,r2,r3,r4;
  r0.xyz = (uint3)vThreadIDInGroup.xyz;
  r0.xyz = float3(0.5,0.5,0.5) + r0.xyz;
  r0.xyz = float3(0.03125,0.03125,0.03125) * r0.xyz;
  r1.xyzw = ro_tonemapping_blendcolorcube_sourcecolorcube.SampleLevel(smp_pointclamp_s, r0.xyz, 0).xyzw;
  r2.xyzw = ro_tonemapping_overridecolorcube_colorcube0.SampleLevel(smp_pointclamp_s, r0.xyz, 0).xyzw;
  r2.xyzw = cb_tonemapping_blendcolorcube_blendweights.xxxx * r2.xyzw;
  r3.xyz = cmp(float3(9.99999997e-007,9.99999997e-007,9.99999997e-007) < cb_tonemapping_blendcolorcube_blendweights.yzw);
  if (r3.x != 0) {
    r4.xyzw = ro_tonemapping_overridecolorcube_colorcube1.SampleLevel(smp_pointclamp_s, r0.xyz, 0).xyzw;
    r2.xyzw = cb_tonemapping_blendcolorcube_blendweights.yyyy * r4.xyzw + r2.xyzw;
  }
  if (r3.y != 0) {
    r4.xyzw = ro_tonemapping_overridecolorcube_colorcube2.SampleLevel(smp_pointclamp_s, r0.xyz, 0).xyzw;
    r2.xyzw = cb_tonemapping_blendcolorcube_blendweights.zzzz * r4.xyzw + r2.xyzw;
  }
  if (r3.z != 0) {
    r0.xyzw = ro_tonemapping_overridecolorcube_colorcube3.SampleLevel(smp_pointclamp_s, r0.xyz, 0).xyzw;
    r2.xyzw = cb_tonemapping_blendcolorcube_blendweights.wwww * r0.xyzw + r2.xyzw;
  }
  r0.x = dot(cb_tonemapping_blendcolorcube_blendweights.xyzw, float4(1,1,1,1));
  r2.xyzw = r2.xyzw + -r1.xyzw;
  r0.xyzw = r0.xxxx * r2.xyzw + r1.xyzw;
  r1.xyz = vThreadIDInGroup.xyz;
  rw_tonemapping_finalcolorcube[r1.xyz] = r0.xyzw;
}