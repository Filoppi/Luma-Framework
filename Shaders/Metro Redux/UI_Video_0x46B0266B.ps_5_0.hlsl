#include "./Common.hlsl"

SamplerState s_image_s : register(s0);
Texture2D<float4> t_image : register(t0);

void main(
    float4 v0: SV_Position0,
    float3 v1: TEXCOORD0,
    float w1: TEXCOORD2,
    float4 v2: TEXCOORD1,
    out float4 o0: SV_Target0)
{
   const bool outside = (v2.y < v1.x) || (v1.x < v2.z) || (v1.y > 1.0) || (v1.y < 0.0);
   const float4 video_sample = t_image.Sample(s_image_s, v1.xy);

   // Match the new dump's YUV reconstruction, rather than the prior BT.709
   // helper path. The coefficients and offsets are from 0x46B0266B.ps_5_0.
   float3 video_rgb;
   video_rgb.r = (1.16406 * video_sample.z) + (1.59765005 * video_sample.x) - 0.869610012;
   video_rgb.g = (1.16406 * video_sample.z) - (0.8125 * video_sample.x) - (0.390625 * video_sample.y) + 0.53075999;
   video_rgb.b = (1.16406 * video_sample.z) + (2.01561999 * video_sample.y) - 1.07860005;
   video_rgb *= video_sample.w;
   video_rgb = outside ? 0.0 : video_rgb;
   video_rgb = saturate(video_rgb);

   // Preserve the existing HDR-video option and AutoHDR settings. Decode the
   // reconstructed SDR signal to linear before expanding it, then re-encode.
   if (LumaSettings.GameSettings.custom_hdr_videos > 0)
   {
      float peak = LumaSettings.GameSettings.custom_hdr_videos == 1
                       ? min(PeakWhiteNits, sRGB_WhiteLevelNits * 2.f)
                       : PeakWhiteNits;
      video_rgb = gamma_sRGB_to_linear(video_rgb);
      video_rgb = PumboAutoHDR(video_rgb, peak, GamePaperWhiteNits);
      video_rgb = linear_to_sRGB_gamma(video_rgb, GCT_MIRROR);
   }

   // The captured vanilla shader applies sqrt-domain dither and squares back to
   // output space. Keep its temporal noise position and amplitude.
   float2 grain_uv = v1.xy + w1;
   float noise = frac(sin(dot(grain_uv, float2(12.9898005, 78.2330017))) * 43758.5469) - 0.5;
   float dither = noise * 0.01;
   float3 output_sqrt = sqrt(max(video_rgb, 0.0)) + dither;
   o0.xyz = output_sqrt * output_sqrt;
   o0.w = 1.0;
}
