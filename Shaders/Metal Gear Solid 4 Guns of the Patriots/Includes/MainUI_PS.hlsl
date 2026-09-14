#include "../../Includes/Common.hlsl"
#include "UI.hlsl"

// ROV/UAV custom blends
#ifndef ENABLE_EMULATED_HARDWARE_BLENDS
#define ENABLE_EMULATED_HARDWARE_BLENDS 0
#endif

#ifndef ENABLE_HIGH_QUALITY_NIGHT_VISION
#define ENABLE_HIGH_QUALITY_NIGHT_VISION 1
#endif

Texture2D<float4> t0 : register(t0);

SamplerState s0_s : register(s0);

// Note: this can also be used for full screen downscales and other stretching/copy operations.
// TODO: implement UI brightness scaling? However, it's not really needed in this game and it'd be a bit annoying to implement as the UI/game split isn't so clear (some UI shaders are used for game etc)
void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  float2 uv : TEXCOORD0,
  float4 v3 : TEXCOORD1,
  out float4 o0 : SV_TARGET0)
{
  float4 r0;

  r0.xyzw = t0.Sample(s0_s, uv).xyzw;

  bool preTonemapped = false;

  float2 sceneSize;
  t0.GetDimensions(sceneSize.x, sceneSize.y);
  // TODO: optimize this branch? Swap the shader from the CPU? Nah, it's fine.
  if (LumaSettings.RenderSize.x == sceneSize.x && LumaSettings.RenderSize.y == sceneSize.y)
  {
    // Fullscreen textures (videos, textures copies, night vision) are to be considered pre-tonemapped and shouldn't be re-tonemapped
    preTonemapped = true;

#if ENABLE_HIGH_QUALITY_NIGHT_VISION
    float2 outputPixelSize = abs(ddx(uv)) + abs(ddy(uv));
    float2 outputResolution = round(rcp(outputPixelSize));
    [branch]
    if (outputResolution.x == 512.0 && outputResolution.y == 512.0) // TODO1
    {
      r0.xyzw = SampleArea(t0, s0_s, uv, sceneSize, outputResolution); // 32 samples per pixel at 4k, doable
    }
#endif
  }

  r0.w = min(1.0, r0.w);
#if 0 // Luma: disabled clamping to SDR (this is also used to cache the scene background when starting pause so it's important). Doesn't help to restore the clipped look anyway, we still need "ENABLE_VANILLA_UI" for that.
  r0.xyz = min(1.0, r0.xyz);
#endif
  r0.xyzw = max(v3.xyzw, r0.xyzw); // Note: we could remove this but it's generally UI so it's whatever
  o0.xyzw = v1.xyzw * r0.xyzw;

#if ENABLE_EMULATED_HARDWARE_BLENDS
  // Note: a cheaper way of doing tonemapping or clamping of the UI is to store on alpha whether a pixel is UI or not (when we can?),
  // and clamping at the end in the swapchain copy, but it might not always work.
  o0 = EmulateHardwareBlendMGS4(v0.xy, o0, preTonemapped);
#endif
}