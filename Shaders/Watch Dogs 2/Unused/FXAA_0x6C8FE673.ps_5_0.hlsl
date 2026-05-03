cbuffer FXAAPostFX : register(b0)
{
  float4 QuadParams : packoffset(c0);
  float4 UV0Params : packoffset(c1);
  float4 constDir360 : packoffset(c2);
  float4 rcp360FrameOpt2 : packoffset(c3);
  float4 rcpFrame : packoffset(c4);
  float4 rcpFrameOpt : packoffset(c5);
  float4 rcpFrameOpt2 : packoffset(c6);
}

SamplerState FXAAPostFX__SourceSampler1__SampObj___s : register(s0);
Texture2D<float4> FXAAPostFX__SourceSampler1__TexObj__ : register(t0);

#define cmp

void main(
  linear centroid float2 v0 : TEXCOORD0,
  float4 v1 : SV_Position0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6;
  r0.xyzw = FXAAPostFX__SourceSampler1__TexObj__.SampleLevel(FXAAPostFX__SourceSampler1__SampObj___s, v0.xy, 0).xyzw;
  r0.xyzw = log2(abs(r0.xyzw));
  r1.w = 0.454545468 * r0.y;
  r2.x = exp2(r1.w);
  r2.y = FXAAPostFX__SourceSampler1__TexObj__.SampleLevel(FXAAPostFX__SourceSampler1__SampObj___s, v0.xy, 0, int2(0, 1)).y;
  r2.y = log2(abs(r2.y));
  r2.y = 0.454545468 * r2.y;
  r2.y = exp2(r2.y);
  r2.z = FXAAPostFX__SourceSampler1__TexObj__.SampleLevel(FXAAPostFX__SourceSampler1__SampObj___s, v0.xy, 0, int2(1, 0)).y;
  r2.z = log2(abs(r2.z));
  r2.z = 0.454545468 * r2.z;
  r2.z = exp2(r2.z);
  r2.w = FXAAPostFX__SourceSampler1__TexObj__.SampleLevel(FXAAPostFX__SourceSampler1__SampObj___s, v0.xy, 0, int2(0, -1)).y;
  r2.w = log2(abs(r2.w));
  r2.w = 0.454545468 * r2.w;
  r2.w = exp2(r2.w);
  r3.x = FXAAPostFX__SourceSampler1__TexObj__.SampleLevel(FXAAPostFX__SourceSampler1__SampObj___s, v0.xy, 0, int2(-1, 0)).y;
  r3.x = log2(abs(r3.x));
  r3.x = 0.454545468 * r3.x;
  r3.x = exp2(r3.x);
  r3.y = max(r2.y, r2.x);
  r3.z = min(r2.y, r2.x);
  r3.z = min(r3.z, r2.z);
  r3.yw = max(r3.yx, r2.zw);
  r4.x = min(r3.x, r2.w);
  r3.y = max(r3.w, r3.y);
  r3.z = min(r4.x, r3.z);
  r3.w = 0.165999994 * r3.y;
  r3.y = r3.y + -r3.z;
  r3.z = max(0.0833000019, r3.w);
  r3.z = cmp(r3.y >= r3.z);
  if (r3.z != 0) {
    r3.z = FXAAPostFX__SourceSampler1__TexObj__.SampleLevel(FXAAPostFX__SourceSampler1__SampObj___s, v0.xy, 0, int2(-1, -1)).y;
    r3.z = log2(abs(r3.z));
    r3.z = 0.454545468 * r3.z;
    r3.z = exp2(r3.z);
    r3.w = FXAAPostFX__SourceSampler1__TexObj__.SampleLevel(FXAAPostFX__SourceSampler1__SampObj___s, v0.xy, 0, int2(1, 1)).y;
    r3.w = log2(abs(r3.w));
    r3.w = 0.454545468 * r3.w;
    r3.w = exp2(r3.w);
    r4.x = FXAAPostFX__SourceSampler1__TexObj__.SampleLevel(FXAAPostFX__SourceSampler1__SampObj___s, v0.xy, 0, int2(1, -1)).y;
    r4.x = log2(abs(r4.x));
    r4.x = 0.454545468 * r4.x;
    r4.x = exp2(r4.x);
    r4.y = FXAAPostFX__SourceSampler1__TexObj__.SampleLevel(FXAAPostFX__SourceSampler1__SampObj___s, v0.xy, 0, int2(-1, 1)).y;
    r4.y = log2(abs(r4.y));
    r4.y = 0.454545468 * r4.y;
    r4.y = exp2(r4.y);
    r4.z = r2.w + r2.y;
    r4.w = r3.x + r2.z;
    r3.y = 1 / r3.y;
    r5.x = r4.z + r4.w;
    r4.z = r2.x * -2 + r4.z;
    r4.w = r2.x * -2 + r4.w;
    r5.y = r4.x + r3.w;
    r4.x = r4.x + r3.z;
    r5.z = r2.z * -2 + r5.y;
    r4.x = r2.w * -2 + r4.x;
    r3.z = r4.y + r3.z;
    r3.w = r4.y + r3.w;
    r4.y = abs(r4.z) * 2 + abs(r5.z);
    r4.x = abs(r4.w) * 2 + abs(r4.x);
    r4.z = r3.x * -2 + r3.z;
    r3.w = r2.y * -2 + r3.w;
    r4.y = abs(r4.z) + r4.y;
    r3.w = abs(r3.w) + r4.x;
    r3.z = r3.z + r5.y;
    r3.w = cmp(r4.y >= r3.w);
    r3.z = r5.x * 2 + r3.z;
    r2.w = r3.w ? r2.w : r3.x;
    r2.y = r3.w ? r2.y : r2.z;
    r2.z = r3.w ? rcpFrame.y : rcpFrame.x;
    r3.x = r3.z * 0.0833333358 + -r2.x;
    r3.z = r2.w + -r2.x;
    r4.x = r2.y + -r2.x;
    r2.yw = r2.yw + r2.xx;
    r4.y = cmp(abs(r3.z) >= abs(r4.x));
    r3.z = max(abs(r4.x), abs(r3.z));
    r2.z = r4.y ? -r2.z : r2.z;
    r3.x = saturate(abs(r3.x) * r3.y);
    r3.y = r3.w ? rcpFrame.x : 0;
    r4.x = r3.w ? 0 : rcpFrame.y;
    r4.zw = r2.zz * float2(0.5,0.5) + v0.xy;
    r4.z = r3.w ? v0.x : r4.z;
    r4.w = r3.w ? r4.w : v0.y;
    r5.x = r4.z + -r3.y;
    r5.y = r4.w + -r4.x;
    r6.x = r4.z + r3.y;
    r6.y = r4.w + r4.x;
    r4.z = r3.x * -2 + 3;
    r4.w = FXAAPostFX__SourceSampler1__TexObj__.SampleLevel(FXAAPostFX__SourceSampler1__SampObj___s, r5.xy, 0).y;
    r4.w = log2(abs(r4.w));
    r4.w = 0.454545468 * r4.w;
    r4.w = exp2(r4.w);
    r3.x = r3.x * r3.x;
    r5.z = FXAAPostFX__SourceSampler1__TexObj__.SampleLevel(FXAAPostFX__SourceSampler1__SampObj___s, r6.xy, 0).y;
    r5.z = log2(abs(r5.z));
    r5.z = 0.454545468 * r5.z;
    r5.z = exp2(r5.z);
    r2.y = r4.y ? r2.w : r2.y;
    r2.w = 0.25 * r3.z;
    r2.x = -r2.y * 0.5 + r2.x;
    r3.x = r4.z * r3.x;
    r2.x = cmp(r2.x < 0);
    r4.y = -r2.y * 0.5 + r4.w;
    r4.z = -r2.y * 0.5 + r5.z;
    r5.zw = cmp(abs(r4.yz) >= r2.ww);
    r3.z = -r3.y * 1.5 + r5.x;
    r6.z = r5.z ? r5.x : r3.z;
    r3.z = -r4.x * 1.5 + r5.y;
    r6.w = r5.z ? r5.y : r3.z;
    r5.xy = ~(int2)r5.zw;
    r3.z = (int)r5.y | (int)r5.x;
    r4.w = r3.y * 1.5 + r6.x;
    r5.x = r5.w ? r6.x : r4.w;
    r4.w = r4.x * 1.5 + r6.y;
    r5.y = r5.w ? r6.y : r4.w;
    if (r3.z != 0) {
      if (r5.z == 0) {
        r3.z = FXAAPostFX__SourceSampler1__TexObj__.SampleLevel(FXAAPostFX__SourceSampler1__SampObj___s, r6.zw, 0).y;
        r3.z = log2(abs(r3.z));
        r3.z = 0.454545468 * r3.z;
        r4.y = exp2(r3.z);
      }
      if (r5.w == 0) {
        r3.z = FXAAPostFX__SourceSampler1__TexObj__.SampleLevel(FXAAPostFX__SourceSampler1__SampObj___s, r5.xy, 0).y;
        r3.z = log2(abs(r3.z));
        r3.z = 0.454545468 * r3.z;
        r4.z = exp2(r3.z);
      }
      r3.z = -r2.y * 0.5 + r4.y;
      r4.y = r5.z ? r4.y : r3.z;
      r3.z = -r2.y * 0.5 + r4.z;
      r4.z = r5.w ? r4.z : r3.z;
      r5.zw = cmp(abs(r4.yz) >= r2.ww);
      r3.z = -r3.y * 2 + r6.z;
      r6.z = r5.z ? r6.z : r3.z;
      r3.z = -r4.x * 2 + r6.w;
      r6.w = r5.z ? r6.w : r3.z;
      r6.xy = ~(int2)r5.zw;
      r3.z = (int)r6.y | (int)r6.x;
      r4.w = r3.y * 2 + r5.x;
      r5.x = r5.w ? r5.x : r4.w;
      r4.w = r4.x * 2 + r5.y;
      r5.y = r5.w ? r5.y : r4.w;
      if (r3.z != 0) {
        if (r5.z == 0) {
          r3.z = FXAAPostFX__SourceSampler1__TexObj__.SampleLevel(FXAAPostFX__SourceSampler1__SampObj___s, r6.zw, 0).y;
          r3.z = log2(abs(r3.z));
          r3.z = 0.454545468 * r3.z;
          r4.y = exp2(r3.z);
        }
        if (r5.w == 0) {
          r3.z = FXAAPostFX__SourceSampler1__TexObj__.SampleLevel(FXAAPostFX__SourceSampler1__SampObj___s, r5.xy, 0).y;
          r3.z = log2(abs(r3.z));
          r3.z = 0.454545468 * r3.z;
          r4.z = exp2(r3.z);
        }
        r3.z = -r2.y * 0.5 + r4.y;
        r4.y = r5.z ? r4.y : r3.z;
        r3.z = -r2.y * 0.5 + r4.z;
        r4.z = r5.w ? r4.z : r3.z;
        r5.zw = cmp(abs(r4.yz) >= r2.ww);
        r3.z = -r3.y * 4 + r6.z;
        r6.z = r5.z ? r6.z : r3.z;
        r3.z = -r4.x * 4 + r6.w;
        r6.w = r5.z ? r6.w : r3.z;
        r6.xy = ~(int2)r5.zw;
        r3.z = (int)r6.y | (int)r6.x;
        r4.w = r3.y * 4 + r5.x;
        r5.x = r5.w ? r5.x : r4.w;
        r4.w = r4.x * 4 + r5.y;
        r5.y = r5.w ? r5.y : r4.w;
        if (r3.z != 0) {
          if (r5.z == 0) {
            r3.z = FXAAPostFX__SourceSampler1__TexObj__.SampleLevel(FXAAPostFX__SourceSampler1__SampObj___s, r6.zw, 0).y;
            r3.z = log2(abs(r3.z));
            r3.z = 0.454545468 * r3.z;
            r4.y = exp2(r3.z);
          }
          if (r5.w == 0) {
            r3.z = FXAAPostFX__SourceSampler1__TexObj__.SampleLevel(FXAAPostFX__SourceSampler1__SampObj___s, r5.xy, 0).y;
            r3.z = log2(abs(r3.z));
            r3.z = 0.454545468 * r3.z;
            r4.z = exp2(r3.z);
          }
          r3.z = -r2.y * 0.5 + r4.y;
          r4.y = r5.z ? r4.y : r3.z;
          r2.y = -r2.y * 0.5 + r4.z;
          r4.z = r5.w ? r4.z : r2.y;
          r2.yw = cmp(abs(r4.yz) >= r2.ww);
          r3.z = -r3.y * 12 + r6.z;
          r6.z = r2.y ? r6.z : r3.z;
          r3.z = -r4.x * 12 + r6.w;
          r6.w = r2.y ? r6.w : r3.z;
          r2.y = r3.y * 12 + r5.x;
          r5.x = r2.w ? r5.x : r2.y;
          r2.y = r4.x * 12 + r5.y;
          r5.y = r2.w ? r5.y : r2.y;
        }
      }
    }
    r2.y = v0.x + -r6.z;
    r2.w = -v0.x + r5.x;
    r3.y = v0.y + -r6.w;
    r2.y = r3.w ? r2.y : r3.y;
    r3.y = -v0.y + r5.y;
    r2.w = r3.w ? r2.w : r3.y;
    r3.yz = cmp(r4.yz < float2(0,0));
    r4.x = r2.w + r2.y;
    r3.yz = cmp((int2)r2.xx != (int2)r3.yz);
    r2.x = 1 / r4.x;
    r4.x = cmp(r2.y < r2.w);
    r2.y = min(r2.y, r2.w);
    r2.w = r4.x ? r3.y : r3.z;
    r3.x = r3.x * r3.x;
    r2.x = r2.y * -r2.x + 0.5;
    r2.y = 0.75 * r3.x;
    r2.x = (int)r2.x & (int)r2.w;
    r2.x = max(r2.x, r2.y);
    r2.xy = r2.xx * r2.zz + v0.xy;
    r3.x = r3.w ? v0.x : r2.x;
    r3.y = r3.w ? r2.y : v0.y;
    r2.xyz = FXAAPostFX__SourceSampler1__TexObj__.SampleLevel(FXAAPostFX__SourceSampler1__SampObj___s, r3.xy, 0).xyz;
    r2.xyz = log2(abs(r2.xyz));
    r1.xyz = float3(0.454545468,0.454545468,0.454545468) * r2.xyz;
    r1.xyzw = float4(2.20000005,2.20000005,2.20000005,2.20000005) * r1.xyzw;
    o0.xyzw = exp2(r1.xyzw);
  } else {
    r0.xyzw = float4(1,1,1,1) * r0.xyzw;
    o0.xyzw = exp2(r0.xyzw);
  }
}