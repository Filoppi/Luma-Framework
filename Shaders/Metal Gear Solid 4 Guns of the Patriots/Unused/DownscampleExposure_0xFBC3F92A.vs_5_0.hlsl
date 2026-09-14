void main(
  float4 v0 : COLOR0,
  float4 v1 : POSITION0,
  float4 v2 : TEXCOORD1,
  float4 v3 : TEXCOORD2,
  out float4 o0 : SV_POSITION0,
  out float4 o1 : COLOR0,
  out float2 o2 : TEXCOORD0)
{
  float4 r0;
  r0.xy = v1.xy * v2.zw + v2.xy;
  o0.xy = r0.xy * float2(2,2) + float2(-1,-1);
  o0.zw = float2(1,1);
  o1.xyzw = v0.xyzw;
  o2.xy = v1.zw * v3.zw + v3.xy;
}