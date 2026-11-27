cbuffer fblock : register(b0)
{
  float4 renderpositiontoviewtexture : packoffset(c0);
  float4 lightprepassinverseparams : packoffset(c1);
  float4 prevviewposition : packoffset(c2);
  float4 prevviewprojectionmatrixrcx : packoffset(c3);
  float4 prevviewprojectionmatrixrcy : packoffset(c4);
  float4 prevviewprojectionmatrixrcw : packoffset(c5);
  float4 projectionmatrixz : packoffset(c6);
  float4 blurstep : packoffset(c7);
  float4 motionblurparms : packoffset(c8);
  float4 texsize : packoffset(c9);
  float4 tsaaparm : packoffset(c10);
}

SamplerState viewdepthmap_samp_state_s : register(s0);
SamplerState tex3_samp_state_s : register(s1);
SamplerState tex0_samp_state_s : register(s2);
SamplerState tex7_samp_state_s : register(s3);
SamplerState tex1_samp_state_s : register(s4);
SamplerState tex5_samp_state_s : register(s5);
SamplerState tex6_samp_state_s : register(s6);
Texture2D<float4> viewdepthmap_samp : register(t0); // Hardware depth
Texture2D<float4> tex3_samp : register(t1); // MVs
Texture2D<float4> tex0_samp : register(t2); // Scene
Texture2D<float4> tex7_samp : register(t3); // Light effects
Texture2D<float4> tex1_samp : register(t4); // History
Texture2D<float4> tex5_samp : register(t5); // ?
Texture2D<float4> tex6_samp : register(t6); // ?

// 3Dmigoto declarations
#define cmp -

void main(
  float4 v0 : TEXCOORD1,
  float4 v1 : TEXCOORD2,
  float4 v2 : SV_Position0,
  out float2 o0 : SV_Target0, // Originally history, out float4 o0 : SV_Target0
  out float4 o1 : SV_Target1, // Current frame
  out float4 o2 : SV_Target2) // Motion blur
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r12,r13,r14,r15,r16,r17,r18;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = v2.xy * renderpositiontoviewtexture.zw + renderpositiontoviewtexture.xy;
  r0.z = viewdepthmap_samp.SampleLevel(viewdepthmap_samp_state_s, r0.xy, 0).x;

  // Clamp depth. Without this we won't have MVs in the sky (not even 0).
  r0.z = min(r0.z, 1.0 - 1e-6);

  r1.xyzw = -v1.xyzw + v0.xyzw;
  r1.xyzw = r0.zzzz * r1.xyzw + v1.xyzw;
  r1.xyz = r1.xyz / r1.www;
  r1.xyz = lightprepassinverseparams.xyz + r1.xyz;
  r1.xyz = -prevviewposition.xyz + r1.xyz;
  r1.w = 1;
  r2.x = dot(r1.xyzw, prevviewprojectionmatrixrcx.xyzw);
  r2.y = dot(r1.xyzw, prevviewprojectionmatrixrcy.xyzw);
  r0.w = dot(r1.xyzw, prevviewprojectionmatrixrcw.xyzw);
  r1.xy = r2.xy / r0.ww;
  r1.xy = r1.xy * float2(0.5,0.5) + float2(0.5,0.5);
  r2.xy = tex3_samp.SampleLevel(tex3_samp_state_s, r0.xy, 0).xy;
  r0.w = cmp(r2.x < 1);
  r1.z = 1 + -r1.y;
  r1.xy = -r1.xz + r0.xy;
  r1.xy = r0.ww ? r2.xy : r1.xy;

  // MVs.
  o0.xy = r1.xy;

  r1.zw = cmp(r1.xy != r1.xy);
  r1.z = (int)r1.w | (int)r1.z;
  r2.zw = (int2)r1.xy & int2(0x7fffffff,0x7fffffff);
  r2.zw = cmp((int2)r2.zw == int2(0x7f800000,0x7f800000));
  r1.z = (int)r1.z | (int)r2.z;
  r1.z = (int)r2.w | (int)r1.z;
  r1.w = projectionmatrixz.z + r0.z;
  r3.w = projectionmatrixz.w / r1.w;
  r1.w = cmp(blurstep.y == 0.000000);
  r1.w = r0.w ? r1.w : 0;
  r1.w = (int)r1.w | (int)r1.z;
  r4.xy = r1.ww ? float2(0,0) : r1.xy;
  r2.zw = motionblurparms.zz * r4.xy;
  r1.w = dot(r2.zw, r2.zw);
  r1.w = rsqrt(r1.w);
  r1.w = saturate(motionblurparms.w * r1.w);
  r2.zw = r2.zw * r1.ww;
  r2.zw = motionblurparms.xy * r2.zw;
  r1.w = dot(r2.zw, r2.zw);
  r4.z = sqrt(r1.w);
  r1.w = -renderpositiontoviewtexture.z * 0.00999999978 + r4.z;
  r1.w = cmp(0 >= r1.w);
  r3.xyz = r1.www ? float3(0,0,0) : r4.xyz;
  r2.zw = blurstep.yx * r3.zz;
  r1.w = r0.w ? r2.z : r2.w;
  r1.w = blurstep.z * r1.w;
  r3.z = r0.w ? r1.w : r2.w;
  r4.xyz = tex0_samp.SampleLevel(tex0_samp_state_s, r0.xy, 0).xyz;
  r5.w = dot(r4.xyz, float3(0.212599993,0.715200007,0.0722000003));
  r1.w = r5.w * 4 + 4;
  r6.xyz = saturate(r4.xyz / r1.www);
  r1.xy = r1.zz ? float2(-0,-0) : -r1.xy;
  r1.xy = r1.xy + r0.xy;
  r1.zw = r1.xy * float2(2,2) + float2(-1,-1);
  r7.xyzw = tex7_samp.SampleLevel(tex7_samp_state_s, r0.xy, 0).xyzw;
  r1.zw = cmp(abs(r1.zw) >= float2(1,1));
  r1.z = (int)r1.w | (int)r1.z;
  r0.z = cmp(r0.z >= 1);
  r0.z = (int)r0.z | (int)r1.z;
  r8.xyz = float3(4,4,4) * r6.xyz;
  r1.z = dot(r6.xyz, float3(0.212599993,0.715200007,0.0722000003));
  r1.z = -r1.z * 4 + 1;
  r8.xyz = r8.xyz / r1.zzz;
  r9.xyz = r8.xyz * r7.www + r7.xyz;
  r10.xyz = cmp(r9.xyz != r9.xyz);
  r1.z = (int)r10.y | (int)r10.x;
  r1.z = (int)r10.z | (int)r1.z;
  r10.xyz = (int3)r9.xyz & int3(0x7fffffff,0x7fffffff,0x7fffffff);
  r10.xyz = cmp((int3)r10.xyz == int3(0x7f800000,0x7f800000,0x7f800000));
  r1.z = (int)r1.z | (int)r10.x;
  r1.z = (int)r10.y | (int)r1.z;
  r1.z = (int)r10.z | (int)r1.z;
  r9.xyz = r1.zzz ? float3(0,0,0) : r9.xyz;
  r9.xyz = max(float3(0,0,0), r9.xyz);
  o1.xyz = min(float3(1024,1024,1024), r9.xyz);
  //o0.xyz = r8.xyz;
  //o0.w = 0;
  o1.w = 1;
  o2.xyzw = r3.xyzw;

  // TAA.
  /*
  if (r0.z != 0) return;
  r8.xyz = r0.zzz ? r8.xyz : r6.xyz;
  r1.zw = r1.xy * texsize.xy + float2(-0.5,-0.5);
  r1.zw = floor(r1.zw);
  r9.xyzw = float4(0.5,0.5,-0.5,-0.5) + r1.zwzw;
  r2.zw = r1.xy * texsize.xy + -r9.xy;
  r10.xy = r2.zw * r2.zw;
  r10.zw = r10.xy * r2.zw;
  r11.xy = r10.xy * r2.zw + r2.zw;
  r11.xy = -r11.xy * float2(0.5,0.5) + r10.xy;
  r11.zw = float2(2.5,2.5) * r10.xy;
  r10.zw = r10.zw * float2(1.5,1.5) + -r11.zw;
  r10.zw = float2(1,1) + r10.zw;
  r2.zw = r10.xy * r2.zw + -r10.xy;
  r10.xy = float2(0.5,0.5) * r2.zw;
  r11.zw = float2(1,1) + -r11.xy;
  r11.zw = r11.zw + -r10.zw;
  r2.zw = -r2.zw * float2(0.5,0.5) + r11.zw;
  r10.zw = r10.zw + r2.zw;
  r12.xw = texsize.zw * r9.zw;
  r2.zw = r2.zw / r10.zw;
  r2.zw = r9.xy + r2.zw;
  r12.yz = texsize.wz * r2.wz;
  r1.zw = float2(2.5,2.5) + r1.zw;
  r9.xy = texsize.zw * r1.zw;
  r13.xyz = tex1_samp.SampleLevel(tex1_samp_state_s, r12.xw, 0).xyz;
  r13.xyz = r13.xyz * r11.xxx;
  r14.xyz = tex1_samp.SampleLevel(tex1_samp_state_s, r12.zw, 0).xyz;
  r14.xyz = r14.xyz * r10.zzz;
  r14.xyz = r14.xyz * r11.yyy;
  r13.xyz = r13.xyz * r11.yyy + r14.xyz;
  r9.zw = r12.wy;
  r14.xyz = tex1_samp.SampleLevel(tex1_samp_state_s, r9.xz, 0).xyz;
  r14.xyz = r14.xyz * r10.xxx;
  r11.yzw = r14.xyz * r11.yyy + r13.xyz;
  r13.xyz = tex1_samp.SampleLevel(tex1_samp_state_s, r12.xy, 0).xyz;
  r13.xyz = r13.xyz * r11.xxx;
  r11.yzw = r13.xyz * r10.www + r11.yzw;
  r13.xyz = tex1_samp.SampleLevel(tex1_samp_state_s, r12.zy, 0).xyz;
  r13.xyz = r13.xyz * r10.zzz;
  r11.yzw = r13.xyz * r10.www + r11.yzw;
  r13.xyz = tex1_samp.SampleLevel(tex1_samp_state_s, r9.xw, 0).xyz;
  r13.xyz = r13.xyz * r10.xxx;
  r11.yzw = r13.xyz * r10.www + r11.yzw;
  r12.y = r9.y;
  r13.xyz = tex1_samp.SampleLevel(tex1_samp_state_s, r12.xy, 0).xyz;
  r13.xyz = r13.xyz * r11.xxx;
  r11.xyz = r13.xyz * r10.yyy + r11.yzw;
  r12.xyz = tex1_samp.SampleLevel(tex1_samp_state_s, r12.zy, 0).xyz;
  r12.xyz = r12.xyz * r10.zzz;
  r11.xyz = r12.xyz * r10.yyy + r11.xyz;
  r9.xyz = tex1_samp.SampleLevel(tex1_samp_state_s, r9.xy, 0).xyz;
  r9.xyz = r9.xyz * r10.xxx;
  r9.xyz = r9.xyz * r10.yyy + r11.xyz;
  r0.z = dot(r9.xyz, float3(0.212599993,0.715200007,0.0722000003));
  r0.z = r0.z * 4 + 4;
  r9.xyz = saturate(r9.xyz / r0.zzz);
  r1.zw = -texsize.zw + r0.xy;
  r10.xyz = tex0_samp.SampleLevel(tex0_samp_state_s, r1.zw, 0).xyz;
  r0.z = dot(r10.xyz, float3(0.212599993,0.715200007,0.0722000003));
  r0.z = r0.z * 4 + 4;
  r10.xyz = saturate(r10.xyz / r0.zzz);
  r11.xyzw = texsize.zwzw * float4(0,-1,1,-1) + r0.xyxy;
  r12.xyz = tex0_samp.SampleLevel(tex0_samp_state_s, r11.xy, 0).xyz;
  r0.z = dot(r12.xyz, float3(0.212599993,0.715200007,0.0722000003));
  r0.z = r0.z * 4 + 4;
  r12.xyz = saturate(r12.xyz / r0.zzz);
  r11.xyz = tex0_samp.SampleLevel(tex0_samp_state_s, r11.zw, 0).xyz;
  r0.z = dot(r11.xyz, float3(0.212599993,0.715200007,0.0722000003));
  r0.z = r0.z * 4 + 4;
  r11.xyz = saturate(r11.xyz / r0.zzz);
  r13.xyzw = texsize.zwzw * float4(-1,0,1,0) + r0.xyxy;
  r14.xyz = tex0_samp.SampleLevel(tex0_samp_state_s, r13.xy, 0).xyz;
  r0.z = dot(r14.xyz, float3(0.212599993,0.715200007,0.0722000003));
  r0.z = r0.z * 4 + 4;
  r14.xyz = saturate(r14.xyz / r0.zzz);
  r13.xyz = tex0_samp.SampleLevel(tex0_samp_state_s, r13.zw, 0).xyz;
  r0.z = dot(r13.xyz, float3(0.212599993,0.715200007,0.0722000003));
  r0.z = r0.z * 4 + 4;
  r13.xyz = saturate(r13.xyz / r0.zzz);
  r15.xyzw = texsize.zwzw * float4(-1,1,0,1) + r0.xyxy;
  r16.xyz = tex0_samp.SampleLevel(tex0_samp_state_s, r15.xy, 0).xyz;
  r0.z = dot(r16.xyz, float3(0.212599993,0.715200007,0.0722000003));
  r0.z = r0.z * 4 + 4;
  r16.xyz = saturate(r16.xyz / r0.zzz);
  r15.xyz = tex0_samp.SampleLevel(tex0_samp_state_s, r15.zw, 0).xyz;
  r0.z = dot(r15.xyz, float3(0.212599993,0.715200007,0.0722000003));
  r0.z = r0.z * 4 + 4;
  r15.xyz = saturate(r15.xyz / r0.zzz);
  r1.zw = texsize.zw + r0.xy;
  r17.xyz = tex0_samp.SampleLevel(tex0_samp_state_s, r1.zw, 0).xyz;
  r0.z = dot(r17.xyz, float3(0.212599993,0.715200007,0.0722000003));
  r0.z = r0.z * 4 + 4;
  r17.xyz = saturate(r17.xyz / r0.zzz);
  r18.xyz = min(r12.xyz, r10.xyz);
  r18.xyz = min(r18.xyz, r11.xyz);
  r18.xyz = min(r18.xyz, r14.xyz);
  r18.xyz = min(r18.xyz, r6.xyz);
  r18.xyz = min(r18.xyz, r13.xyz);
  r18.xyz = min(r18.xyz, r16.xyz);
  r18.xyz = min(r18.xyz, r15.xyz);
  r18.xyz = min(r18.xyz, r17.xyz);
  r10.xyz = max(r12.xyz, r10.xyz);
  r10.xyz = max(r10.xyz, r11.xyz);
  r10.xyz = max(r10.xyz, r14.xyz);
  r10.xyz = max(r10.xyz, r6.xyz);
  r10.xyz = max(r10.xyz, r13.xyz);
  r10.xyz = max(r10.xyz, r16.xyz);
  r10.xyz = max(r10.xyz, r15.xyz);
  r10.xyz = max(r10.xyz, r17.xyz);
  r11.xyz = min(r14.xyz, r12.xyz);
  r11.xyz = min(r11.xyz, r6.xyz);
  r11.xyz = min(r13.xyz, r11.xyz);
  r11.xyz = min(r15.xyz, r11.xyz);
  r12.xyz = max(r14.xyz, r12.xyz);
  r6.xyz = max(r12.xyz, r6.xyz);
  r6.xyz = max(r13.xyz, r6.xyz);
  r6.xyz = max(r15.xyz, r6.xyz);
  r11.xyz = r18.xyz + r11.xyz;
  r11.xyz = float3(0.5,0.5,0.5) * r11.xyz;
  r6.xyz = r10.xyz + r6.xyz;
  r10.xyz = float3(0.5,0.5,0.5) * r6.xyz;
  r12.xyz = r6.xyz * float3(0.5,0.5,0.5) + r11.xyz;
  r6.xyz = r6.xyz * float3(0.5,0.5,0.5) + -r11.xyz;
  r6.xyz = float3(0.5,0.5,0.5) * r6.xyz;
  r13.xyz = -r12.xyz * float3(0.5,0.5,0.5) + r9.xyz;
  r6.xyz = r13.xyz / r6.xyz;
  r0.z = max(abs(r6.y), abs(r6.z));
  r0.z = max(abs(r6.x), r0.z);
  r1.z = cmp(1 < r0.z);
  r6.xyz = r13.xyz / r0.zzz;
  r6.xyz = r12.xyz * float3(0.5,0.5,0.5) + r6.xyz;
  r6.xyz = r1.zzz ? r6.xyz : r9.xyz;
  r0.z = dot(r6.xyz, float3(0.212599993,0.715200007,0.0722000003));
  r1.z = dot(r10.xyz, float3(0.212599993,0.715200007,0.0722000003));
  r1.w = dot(r11.xyz, float3(0.212599993,0.715200007,0.0722000003));
  r2.z = r1.z + -r1.w;
  r1.z = -r1.z + r0.z;
  r0.z = -r1.w + r0.z;
  r0.z = min(abs(r1.z), abs(r0.z));
  r1.z = 0.125 * r0.z;
  r0.z = r0.z * 0.125 + r2.z;
  r0.z = saturate(r1.z / r0.z);
  r0.z = 1 + -r0.z;
  r0.x = tex5_samp.SampleLevel(tex5_samp_state_s, r0.xy, 0).x;
  r0.x = 255 * r0.x;
  r0.x = (uint)r0.x;
  r0.y = tex6_samp.SampleLevel(tex6_samp_state_s, r1.xy, 0).x;
  r0.y = 255 * r0.y;
  r0.y = (uint)r0.y;
  r1.xyzw = (int4)r0.xxxx & int4(2,1,16,32);
  r0.x = (int)r0.y & 2;
  r0.x = cmp((int)r0.x != (int)r1.x);
  r0.x = r0.x ? 0 : r0.z;
  r0.y = dot(r2.xy, r2.xy);
  r0.y = sqrt(r0.y);
  r0.y = r0.w ? r0.y : 0;
  r0.z = max(texsize.z, texsize.w);
  r0.z = 0.0500000007 * r0.z;
  r0.y = cmp(r0.z < r0.y);
  r0.z = min(0.75, r0.x);
  r0.x = r0.y ? r0.z : r0.x;
  r0.y = cmp(tsaaparm.w == 0.000000);
  r1.xyz = cmp((int3)r1.yzw == int3(1,16,32));
  r0.yzw = r0.yyy ? r1.xyz : 0;
  r1.xy = min(float2(0.25,0.5), r0.xx);
  r0.x = r0.z ? r1.y : r0.x;
  r0.x = r0.y ? r1.x : r0.x;
  r1.xyz = r6.xyz + -r8.xyz;
  r0.xyz = r0.xxx * r1.xyz + r8.xyz;
  r1.xyz = float3(4,4,4) * r0.xyz;
  r0.x = dot(r0.xyz, float3(0.212599993,0.715200007,0.0722000003));
  r0.x = -r0.x * 4 + 1;
  r0.xyz = r1.xyz / r0.xxx;
  r5.xyz = r0.www ? r4.xyz : r0.xyz;
  r0.xyz = r5.xyz * r7.www + r7.xyz;
  r1.xyz = cmp(r0.xyz != r0.xyz);
  r0.w = (int)r1.y | (int)r1.x;
  r0.w = (int)r1.z | (int)r0.w;
  r1.xyz = (int3)r0.xyz & int3(0x7fffffff,0x7fffffff,0x7fffffff);
  r1.xyz = cmp((int3)r1.xyz == int3(0x7f800000,0x7f800000,0x7f800000));
  r0.w = (int)r0.w | (int)r1.x;
  r0.w = (int)r1.y | (int)r0.w;
  r0.w = (int)r1.z | (int)r0.w;
  r0.xyz = r0.www ? float3(0,0,0) : r0.xyz;
  r0.xyz = max(float3(0,0,0), r0.xyz);
  o1.xyz = min(float3(1024,1024,1024), r0.xyz);
  o0.xyzw = r5.xyzw;
  o1.w = 1;
  o2.xyzw = r3.xyzw;
  return;
  */
}