cbuffer DrawableBuffer : register(b1)
{
  float4 FogColor : packoffset(c0);
  float4 DebugColor : packoffset(c1);
  float MaterialOpacity : packoffset(c2);
  float AlphaThreshold : packoffset(c3);
}

cbuffer SceneBuffer : register(b2)
{
  row_major float4x4 View : packoffset(c0);
  row_major float4x4 ScreenMatrix : packoffset(c4);
  float2 DepthExportScale : packoffset(c8);
  float2 FogScaleOffset : packoffset(c9);
  float3 CameraPosition : packoffset(c10);
  float3 CameraDirection : packoffset(c11);
  float3 DepthFactors : packoffset(c12);
  float2 ShadowDepthBias : packoffset(c13);
  float4 SubframeViewport : packoffset(c14);
  row_major float3x4 DepthToWorld : packoffset(c15);
  float4 DepthToView : packoffset(c18);
  float4 OneOverDepthToView : packoffset(c19);
  float4 DepthToW : packoffset(c20);
  float4 ClipPlane : packoffset(c21);
  float2 ViewportDepthScaleOffset : packoffset(c22);
  float2 ColorDOFDepthScaleOffset : packoffset(c23);
  float2 TimeVector : packoffset(c24);
  float3 HeightFogParams : packoffset(c25);
  float3 GlobalAmbient : packoffset(c26);
  float4 GlobalParams[16] : packoffset(c27);
  float DX3_SSAOScale : packoffset(c43);
  float4 ScreenExtents : packoffset(c44);
  float2 ScreenResolution : packoffset(c45);
  float4 PSSMToMap1Lin : packoffset(c46);
  float4 PSSMToMap1Const : packoffset(c47);
  float4 PSSMToMap2Lin : packoffset(c48);
  float4 PSSMToMap2Const : packoffset(c49);
  float4 PSSMToMap3Lin : packoffset(c50);
  float4 PSSMToMap3Const : packoffset(c51);
  float4 PSSMDistances : packoffset(c52);
  row_major float4x4 WorldToPSSM0 : packoffset(c53);
  float StereoOffset : packoffset(c25.w);
}

cbuffer InstanceBuffer : register(b5)
{
  float4 InstanceParams[8] : packoffset(c0);
}

SamplerState p_default_Material_0D4489A4339784082_NormalBufferTexture_sampler_s : register(s0);
SamplerState p_default_Material_098651E4334612755_DepthBufferTexture_sampler_s : register(s1);
SamplerState p_default_Material_2858F59C8024843_Param_sampler_s : register(s2);
Texture2D<float4> p_default_Material_0D4489A4339784082_NormalBufferTexture_texture : register(t0);
Texture2D<float4> p_default_Material_098651E4334612755_DepthBufferTexture_texture : register(t1);
Texture2D<float4> p_default_Material_2858F59C8024843_Param_texture : register(t2);

void main(
  float4 v0 : SV_POSITION0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  r0.xy = v0.xy * ScreenExtents.zw + ScreenExtents.xy;
  r0.z = p_default_Material_098651E4334612755_DepthBufferTexture_texture.Sample(p_default_Material_098651E4334612755_DepthBufferTexture_sampler_s, r0.xy).x;
  r0.z = r0.z * DepthToW.x + DepthToW.y;
  r1.z = 1 / r0.z;
  r0.zw = r0.xy * DepthToView.xy + DepthToView.zw;
  r2.xyz = p_default_Material_0D4489A4339784082_NormalBufferTexture_texture.Sample(p_default_Material_0D4489A4339784082_NormalBufferTexture_sampler_s, r0.xy).xyz;
  r2.xyz = float3(-0.5,-0.5,-0.5) + r2.xyz;
  r2.xyz = r2.xyz + r2.xyz;
  r1.xy = r0.zw * r1.zz;
  r0.xyz = InstanceParams[0].xyz + -r1.xyz;
  r0.w = dot(r0.xyz, r0.xyz);
  r0.w = sqrt(r0.w);
  r0.xyz = r0.xyz / r0.www;
  r0.w = saturate(InstanceParams[2].x * r0.w);
  r0.w = 1 + -r0.w;
  r1.x = saturate(InstanceParams[3].x * r0.w);
  r0.w = dot(r2.xyz, r2.xyz);
  r0.w = rsqrt(r0.w);
  r2.xyz = r0.www * r2.xyz;
  r0.x = saturate(dot(r2.xyz, r0.xyz));
  r1.y = 0;
  r0.yzw = p_default_Material_2858F59C8024843_Param_texture.Sample(p_default_Material_2858F59C8024843_Param_sampler_s, r1.xy).xyz;
  r0.xyz = r0.yzw * r0.xxx;
  o0.xyz = InstanceParams[1].xyz * r0.xyz;
#if DEVELOPMENT && 0 // Luma: test materials clipping
  o0.xyz *= 30.0;
#elif 0 // Luma: boost lights (they look nicer in HDR) (this would need a lot more work to look proper)
  o0.xyz *= 1.25;
#endif
  o0.w = MaterialOpacity;
}