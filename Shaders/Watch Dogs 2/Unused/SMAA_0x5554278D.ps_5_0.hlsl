cbuffer PostFxSMAA : register(b0)
{
  float4x4 CameraSpaceToPreviousProjectedSpace : packoffset(c0);
  float4 QuadParams : packoffset(c4);
  float4 SceneTextureSize : packoffset(c5);
  float4 TemporalParameters : packoffset(c6);
}

SamplerState ColorClamp2D_s : register(s0);
Texture2D<float4> PostFxSMAA__BlendTexture__TexObj__ : register(t0);
Texture2D<float4> PostFxSMAA__ColorTexture__TexObj__ : register(t1);

#define cmp

void main(
  linear centroid float2 v0 : TEXCOORD0,
  float4 v1 : SV_Position0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  r0.xyzw = SceneTextureSize.zwzw * float4(1,0,0,1) + v0.xyxy;
  r1.x = PostFxSMAA__BlendTexture__TexObj__.Sample(ColorClamp2D_s, r0.xy).w;
  r1.y = PostFxSMAA__BlendTexture__TexObj__.Sample(ColorClamp2D_s, r0.zw).y;
  r1.zw = PostFxSMAA__BlendTexture__TexObj__.Sample(ColorClamp2D_s, v0.xy).zx;
  r0.x = dot(r1.xyzw, float4(1,1,1,1));
  r0.x = cmp(r0.x < 9.99999975e-006);
  if (r0.x != 0) {
    o0.xyzw = PostFxSMAA__ColorTexture__TexObj__.SampleLevel(ColorClamp2D_s, v0.xy, 0).xyzw;
  } else {
    r0.xy = max(r1.xy, r1.zw);
    r0.x = cmp(r0.y < r0.x);
    r2.xz = r0.xx ? r1.xz : 0;
    r2.yw = r0.xx ? float2(0,0) : r1.yw;
    r1.x = r0.x ? r1.x : r1.y;
    r1.y = r0.x ? r1.z : r1.w;
    r0.x = dot(r1.xy, float2(1,1));
    r0.xy = r1.xy / r0.xx;
    r1.xyzw = float4(1,1,-1,-1) * SceneTextureSize.zwzw;
    r1.xyzw = r2.xyzw * r1.xyzw + v0.xyxy;
    r2.xyzw = PostFxSMAA__ColorTexture__TexObj__.SampleLevel(ColorClamp2D_s, r1.xy, 0).xyzw;
    r1.xyzw = PostFxSMAA__ColorTexture__TexObj__.SampleLevel(ColorClamp2D_s, r1.zw, 0).xyzw;
    r1.xyzw = r1.xyzw * r0.y;
    o0.xyzw = r0.x * r2.xyzw + r1.xyzw;
  }
}