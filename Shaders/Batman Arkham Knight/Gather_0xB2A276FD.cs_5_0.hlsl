// Batman: Arkham Knight - FGatherComputeShader<0> (CRC32 0xB2A276FD), replaced.
//
// First post process level: 2x2 downsample of the scene color, with the DOF near/far circle of confusion from the depth.
// Same as the original, except that the depth is loaded at the render resolution: after super resolution upscaling
// the post processes run at the output resolution, while the depth is still at the render resolution (top left of the texture).

#include "Includes/Common.hlsl"

cbuffer GatherCB : register(b0)
{
   float4 c0[12];
   // c0[6].x: DOF focus distance, c0[6].y: DOF scale, c0[6].z: whether to apply the t2 lens effect
   // c0[7].x: scene color scale, c0[7].z: lens effect scale
   // c0[8].xy: half view size, c0[8].zw: view origin
   // c0[10].x, c0[11].y: projection scale
}

cbuffer ViewCB : register(b1)
{
   float4 c1[21];
   // c1[20].zw: depth linearisation (1 / (d * z - w))
}

SamplerState Sampler : register(s0);
Texture2D<float4> SceneDepth : register(t0);
Texture2D<float4> SceneColor : register(t1);
Texture2D<float4> LensEffect : register(t2);
RWTexture2D<float4> Downsampled : register(u0);
RWTexture2D<float4> DownsampledNear : register(u1);
RWTexture2D<float4> DownsampledFar : register(u2);

// Color (rgb) and near/far circle of confusion of one full resolution pixel
void GatherPixel(float2 pixel, float2 max_pixel, float2 ndc_scale, out float3 color, out float near_coc, out float far_coc)
{
   int2 coords = int2(min(pixel, max_pixel));
   color = SceneColor.Load(int3(coords, 0)).rgb;
   // Identical to "coords" when not upscaling
   int2 depth_coords = int2((float2(coords) + 0.5) * LumaData.GameData.PostRenderScale.xy);
   float device_depth = min(SceneDepth.Load(int3(depth_coords, 0)).x, 1.0);
   float view_depth = 1.0 / (device_depth * c1[20].z - c1[20].w);

   float2 ndc = (pixel - c0[8].zw + 0.5) * ndc_scale + float2(-2.0, 2.0);
   float3 view_position = view_depth * float3(ndc.x / c0[10].x, ndc.y / c0[11].y, 1.0);
   float coc = (1.0 - c0[6].x / length(view_position)) * c0[6].y;
   near_coc = saturate(-coc);
   far_coc = saturate(coc);
}

[numthreads(16, 16, 1)] void main(uint3 thread_id : SV_DispatchThreadID) {
   float2 ndc_scale = float2(2.0, -2.0) / c0[8].xy;
   float2 max_pixel = c0[8].xy * 2.0 - 1.0;
   float2 pixel = float2(thread_id.xy * 2);

   float3 color_sum = 0.0;
   float near_sum = 0.0;
   float far_sum = 0.0;
   const float2 offsets[4] = {float2(0.0, 0.0), float2(1.0, 0.0), float2(0.0, 1.0), float2(1.0, 1.0)};
   [unroll] for (int i = 0; i < 4; i++)
   {
      float3 color;
      float near_coc, far_coc;
      GatherPixel(pixel + offsets[i], max_pixel, ndc_scale, color, near_coc, far_coc);
      color_sum += color;
      near_sum += near_coc;
      far_sum += far_coc;
   }
   float4 near_output = float4(color_sum, near_sum) * 0.25;
   float4 far_output = float4(color_sum, far_sum) * 0.25;
   float3 output = far_output.rgb;

   if (asuint(c0[6].z) != 0)
   {
      float4 lens_effect = LensEffect.SampleLevel(Sampler, float2(thread_id.xy) / c0[8].xy, 0.0);
      float scene_scale = 1.0 - saturate(lens_effect.a);
      float3 lens_color = lens_effect.rgb * c0[7].z;
      output = far_output.rgb * c0[7].x * scene_scale + lens_color;
      near_output.rgb = near_output.rgb * c0[7].x * scene_scale + lens_color;
      far_output.rgb = output;
   }

   DownsampledNear[thread_id.xy] = near_output;
   DownsampledFar[thread_id.xy] = far_output;
   Downsampled[thread_id.xy] = output.rgbr;
}
