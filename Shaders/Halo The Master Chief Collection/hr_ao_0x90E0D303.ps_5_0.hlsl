// ---- Created with 3Dmigoto v1.3.16 on Wed Aug 05 17:40:06 2026

// cbuffer HDAOPS : register(b0)
// {
//   float4 pixel_size : packoffset(c0);
//   float4 scale : packoffset(c1);
//   float4 corner_params : packoffset(c2);
//   float4 bounds_params : packoffset(c3);
//   float4 curve_params : packoffset(c4);
//   float4 fade_params : packoffset(c5);
//   float4 channel_scale : packoffset(c6);
//   float4 channel_offset : packoffset(c7);
// }

// cbuffer SSAOLocalDepthPS : register(b1)
// {
//   float4 local_depth_constants : packoffset(c0);
// }

// New, bound by Luma
cbuffer ViewVS : register(b6)
{
  float4x4 View_Projection : packoffset(c0);
// -1.20691
// 0.245012
// -1.51971e-07
// -111.365
// 
// 0.0111751
// 0.055049
// 2.18867
// -35.5318
// 
// 1.51736e-07
// 7.47443e-07
// -1.95743e-08
// 0.00780883
// 
// -0.198884
// -0.979687
// 0.0256564
// 4.82235

  float4x4 Camera_To_World : packoffset(c4);
// -0.98001
// 0.198949
// -1.23866e-07
// 0
// 
// 0.00510419
// 0.0251435
// 0.999671
// -0
// 
// 0.198884
// 0.979687
// -0.0256564
// 0
// 
// -87.5785
// 23.123
// 16.1
// 1

  float4 v_clip_plane : packoffset(c8);
// 0
// 0
// 0
// 0
}

SamplerState GlobalSampler_depth_sampler_s : register(s0);
SamplerState GlobalSampler_depth_low_sampler_s : register(s1);
// SamplerState GlobalTexture_normal_sampler_s : register(s2); // unbound from prev
Texture2D<float4> GlobalTexture_depth_sampler : register(t0);
Texture2D<float4> GlobalTexture_depth_low_sampler : register(t1);
Texture2D<float4> GlobalTexture_normal_sampler : register(t2); // unbound from prev (now guarenteed by Luma)


// 3Dmigoto declarations
#define cmp -
// #include "./Includes/Common.hlsl" 
#include "./Luma_HR_XeGTAO.hlsl"

// TODO: depth prepass, use normals (so also world->view mat), cs main, denoise
void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6;
  uint4 bitmask, uiDest;
  float4 fDest;

  // t0 = depth
  // t1 = world space normal
  GTAOConstants c = (GTAOConstants)0;
  uint2 pixCoord = v0.xy;

  // size
  c.ViewportSize = LumaSettings.SwapchainSize;
  c.ViewportPixelSize = LumaSettings.SwapchainInvSize;

  // // NDC to View (placeholder test)
  // float tanHalfFOV = tan(50 * XE_GTAO_PI_OVER_360);
  // float aspect = c.ViewportSize.x / c.ViewportSize.y;
  // c.NDCToViewMul = float2(2.0, -2.0) * float2(aspect * tanHalfFOV, tanHalfFOV);
  // c.NDCToViewAdd = float2(-1.0, 1.0) * float2(aspect * tanHalfFOV, tanHalfFOV);
  // c.NDCToViewMul_x_PixelSize = c.NDCToViewMul * c.ViewportPixelSize;

  // NDC to View (from constructed Projection mat)
  float4x4 View_Projection_t = transpose(View_Projection); //bruh https://github.com/halohlsl/HaloReach-Shader-Source/blob/5928a1f385a167e5faba9d66dcd1f966b887e331/source/omaha/rasterizer/hlsl/hlsl_constant_global_list.fx#L22
  float4x4 Projection = mul(View_Projection_t, Camera_To_World);
  float tanHalfFovX = 1.0 / abs(Projection[0][0]);
  float tanHalfFovY = 1.0 / abs(Projection[1][1]);
  c.NDCToViewMul = float2(2.0, -2.0) * float2(tanHalfFovX, tanHalfFovY);
  c.NDCToViewAdd = float2(-1.0, 1.0) * float2(tanHalfFovX, tanHalfFovY);
  c.NDCToViewMul_x_PixelSize = c.NDCToViewMul * c.ViewportPixelSize;

  // ViewNormal
  float3 normal = GlobalTexture_normal_sampler.Sample(GlobalSampler_depth_sampler_s, v1.xy).xyz;
    normal = mad(normal, 2.0, -1.0);
    normal = normalize(normal);
  float3x3 WorldToView_Rot = transpose((float3x3)Camera_To_World);
  float3 viewNormal = normalize(mul(WorldToView_Rot, normal));
  viewNormal.z *= -1;
  
  // Noise
  // #if HALO2_GTAO_NOISE == 0
  //     const float frameIndex = 0;
  // #else
      const float frameIndex = LumaSettings.FrameIndex;
  // #endif
  float2 localNoise = SpatioTemporalNoise(pixCoord, frameIndex);

  o0 = 1;
  float2 r = XeGTAO_MainPassCS(pixCoord, localNoise, viewNormal, GlobalTexture_depth_sampler, GlobalSampler_depth_sampler_s, c);
  // r.y = 1 - r.y;
  // r.x += r.y;
  o0.x = r.x;
  o0.xyzw = channel_scale.xyzw * o0.x + channel_offset.xyzw;

  // r0.x = GlobalTexture_depth_sampler.Sample(GlobalSampler_depth_sampler_s, v1.xy).x;
  // r0.x = r0.x * local_depth_constants.y + local_depth_constants.x;
  // r0.x = 1 / r0.x;
  // r0.yz = saturate(r0.xx * fade_params.xz + fade_params.yw);
  // r1.x = -pixel_size.x;
  // r1.yw = float2(0,0);
  // r1.xy = v1.xy + r1.xy;
  // r0.w = GlobalTexture_depth_low_sampler.Sample(GlobalSampler_depth_low_sampler_s, r1.xy).x;
  // r1.z = pixel_size.x;
  // r1.xy = v1.xy + r1.zw;
  // r1.x = GlobalTexture_depth_low_sampler.Sample(GlobalSampler_depth_low_sampler_s, r1.xy).x;
  // r0.w = r1.x + -r0.w;
  // r0.w = cmp(0 < r0.w);
  // r1.xyzw = r0.wwww ? float4(-3,-1.5,4,2.5) : float4(-4,-2.5,3,1.5);
  // r1.xyzw = r1.xyzw * pixel_size.xyxy + v1.xyxy;
  // r2.xyzw = GlobalTexture_depth_low_sampler.Sample(GlobalSampler_depth_low_sampler_s, r1.xy).xyzw;
  // r1.xyzw = GlobalTexture_depth_low_sampler.Sample(GlobalSampler_depth_low_sampler_s, r1.zw).xyzw;
  // r3.x = bounds_params.x / r0.x;
  // r4.xyzw = saturate(r2.xyzw * r3.xxxx + bounds_params.yyyy);
  // r5.xyzw = saturate(r1.xyzw * r3.xxxx + bounds_params.yyyy);
  // r4.xyzw = r5.xyzw * r4.xyzw;
  // r1.xyzw = r2.xyzw + r1.xyzw;
  // r0.x = corner_params.x / r0.x;
  // r1.xyzw = saturate(r1.xyzw * r0.xxxx + corner_params.yyyy);
  // r1.xyzw = r4.xyzw * r1.xyzw;
  // r1.x = dot(r1.xyzw, float4(1,1,1,1));
  // r2.xyzw = r0.wwww ? float4(-1.5,4,2.5,-3) : float4(-2.5,3,1.5,-4);
  // r2.xyzw = r2.xyzw * pixel_size.xyxy + v1.xyxy;
  // r4.xyzw = GlobalTexture_depth_low_sampler.Sample(GlobalSampler_depth_low_sampler_s, r2.xy).xyzw;
  // r2.xyzw = GlobalTexture_depth_low_sampler.Sample(GlobalSampler_depth_low_sampler_s, r2.zw).xyzw;
  // r5.xyzw = saturate(r4.xyzw * r3.xxxx + bounds_params.yyyy);
  // r6.xyzw = saturate(r2.xyzw * r3.xxxx + bounds_params.yyyy);
  // r5.xyzw = r6.xyzw * r5.xyzw;
  // r2.xyzw = r4.xyzw + r2.xyzw;
  // r2.xyzw = saturate(r2.xyzw * r0.xxxx + corner_params.yyyy);
  // r2.xyzw = r5.xyzw * r2.xyzw;
  // r1.y = dot(r2.xyzw, float4(1,1,1,1));
  // r1.x = r1.x + r1.y;
  // r2.xyzw = r0.wwww ? float4(-1.5,-3.5,2.5,4.5) : float4(-2.5,-4.5,1.5,3.5);
  // r2.xyzw = r2.xyzw * pixel_size.xyxy + v1.xyxy;
  // r4.xyzw = GlobalTexture_depth_low_sampler.Sample(GlobalSampler_depth_low_sampler_s, r2.xy).xyzw;
  // r2.xyzw = GlobalTexture_depth_low_sampler.Sample(GlobalSampler_depth_low_sampler_s, r2.zw).xyzw;
  // r5.xyzw = saturate(r4.xyzw * r3.xxxx + bounds_params.yyyy);
  // r6.xyzw = saturate(r2.xyzw * r3.xxxx + bounds_params.yyyy);
  // r5.xyzw = r6.xyzw * r5.xyzw;
  // r2.xyzw = r4.xyzw + r2.xyzw;
  // r2.xyzw = saturate(r2.xyzw * r0.xxxx + corner_params.yyyy);
  // r2.xyzw = r5.xyzw * r2.xyzw;
  // r1.y = dot(r2.xyzw, float4(1,1,1,1));
  // r1.x = r1.x + r1.y;
  // r2.xyzw = r0.wwww ? float4(-3.5,2.5,4.5,-1.5) : float4(-4.5,1.5,3.5,-2.5);
  // r2.xyzw = r2.xyzw * pixel_size.xyxy + v1.xyxy;
  // r4.xyzw = GlobalTexture_depth_low_sampler.Sample(GlobalSampler_depth_low_sampler_s, r2.xy).xyzw;
  // r2.xyzw = GlobalTexture_depth_low_sampler.Sample(GlobalSampler_depth_low_sampler_s, r2.zw).xyzw;
  // r5.xyzw = saturate(r4.xyzw * r3.xxxx + bounds_params.yyyy);
  // r6.xyzw = saturate(r2.xyzw * r3.xxxx + bounds_params.yyyy);
  // r5.xyzw = r6.xyzw * r5.xyzw;
  // r2.xyzw = r4.xyzw + r2.xyzw;
  // r2.xyzw = saturate(r2.xyzw * r0.xxxx + corner_params.yyyy);
  // r2.xyzw = r5.xyzw * r2.xyzw;
  // r1.y = dot(r2.xyzw, float4(1,1,1,1));
  // r1.x = r1.x + r1.y;
  // r1.y = cmp(1 < r1.x);
  // if (r1.y != 0) {
  //   r2.xyzw = r0.wwww ? float4(-0.5,-1,1.5,2) : float4(-1.5,-2,0.5,1);
  //   r2.xyzw = r2.xyzw * pixel_size.xyxy + v1.xyxy;
  //   r4.xyzw = GlobalTexture_depth_low_sampler.Sample(GlobalSampler_depth_low_sampler_s, r2.xy).xyzw;
  //   r2.xyzw = GlobalTexture_depth_low_sampler.Sample(GlobalSampler_depth_low_sampler_s, r2.zw).xyzw;
  //   r5.xyzw = saturate(r4.xyzw * r3.xxxx + bounds_params.yyyy);
  //   r6.xyzw = saturate(r2.xyzw * r3.xxxx + bounds_params.yyyy);
  //   r5.xyzw = r6.xyzw * r5.xyzw;
  //   r2.xyzw = r4.xyzw + r2.xyzw;
  //   r2.xyzw = saturate(r2.xyzw * r0.xxxx + corner_params.yyyy);
  //   r2.xyzw = r5.xyzw * r2.xyzw;
  //   r1.y = dot(r2.xyzw, float4(1,1,1,1));
  //   r1.y = r1.x + r1.y;
  //   r2.xyzw = r0.wwww ? float4(0.5,-2,0.5,3) : float4(-0.5,-3,-0.5,2);
  //   r2.xyzw = r2.xyzw * pixel_size.xyxy + v1.xyxy;
  //   r4.xyzw = GlobalTexture_depth_low_sampler.Sample(GlobalSampler_depth_low_sampler_s, r2.xy).xyzw;
  //   r2.xyzw = GlobalTexture_depth_low_sampler.Sample(GlobalSampler_depth_low_sampler_s, r2.zw).xyzw;
  //   r5.xyzw = saturate(r4.xyzw * r3.xxxx + bounds_params.yyyy);
  //   r3.xyzw = saturate(r2.xyzw * r3.xxxx + bounds_params.yyyy);
  //   r3.xyzw = r5.xyzw * r3.xyzw;
  //   r2.xyzw = r4.xyzw + r2.xyzw;
  //   r2.xyzw = saturate(r2.xyzw * r0.xxxx + corner_params.yyyy);
  //   r2.xyzw = r3.xyzw * r2.xyzw;
  //   r0.x = dot(r2.xyzw, float4(1,1,1,1));
  //   r1.x = r1.y + r0.x;
  // }
  // r0.x = -r0.y * r0.z + 1;
  // r0.y = r1.x * r1.x;
  // r0.y = curve_params.z * r0.y;
  // r0.y = exp2(r0.y);
  // r0.x = max(r0.x, r0.y);
  // o0.xyzw = channel_scale.xyzw * r0.xxxx + channel_offset.xyzw;
  return;
}